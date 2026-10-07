#include "utils/ImageCache.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMutexLocker>

namespace occ {

namespace {

QString cacheTr(const char *text)
{
    return QCoreApplication::translate("ImageCache", text);
}

constexpr qint64 kMaxFileBytes = 512LL * 1024 * 1024;

// A cheap signature test. It exists so that "that file is not an image" can be
// reported immediately, without Qt trying to decode a 400 MB video.
QByteArray signatureOf(const QByteArray &data)
{
    static const QByteArray pngMagic = QByteArray::fromHex("89504e470d0a1a0a");
    static const QByteArray jpegMagic = QByteArray::fromHex("ffd8ff");
    static const QByteArray tiffLe = QByteArray::fromHex("49492a00");
    static const QByteArray tiffBe = QByteArray::fromHex("4d4d002a");

    if (data.size() >= pngMagic.size() && data.startsWith(pngMagic))
        return QByteArrayLiteral("PNG");
    if (data.size() >= jpegMagic.size() && data.startsWith(jpegMagic))
        return QByteArrayLiteral("JPEG");
    if (data.size() >= 2 && data.at(0) == 'B' && data.at(1) == 'M')
        return QByteArrayLiteral("BMP");
    if (data.size() >= 4 && (data.startsWith(tiffLe) || data.startsWith(tiffBe)))
        return QByteArrayLiteral("TIFF");
    if (data.size() >= 6 && data.startsWith("GIF8"))
        return QByteArrayLiteral("GIF");
    if (data.size() >= 12 && data.startsWith("RIFF")) {
        if (data.mid(8, 4) == QByteArrayLiteral("WEBP"))
            return QByteArrayLiteral("WEBP");
        return QByteArrayLiteral("RIFF");   // WAV / AVI - a common mis-drag
    }
    // SVG is markup; only accept it when the text really starts like it.
    const QByteArray head = data.left(512).trimmed().toLower();
    if (head.startsWith("<?xml") || head.startsWith("<svg"))
        return QByteArrayLiteral("SVG");
    return QByteArray();
}

} // namespace

ImageCache::ImageCache() = default;

ImageCache::ImageCache(const Limits &limits) : m_limits(limits)
{
}

ImageCache::~ImageCache() = default;

ImageCache &ImageCache::instance()
{
    static ImageCache cache;
    return cache;
}

qint64 ImageCache::bytesFor(const QImage &image)
{
    if (image.isNull())
        return 0;
    return qint64(image.sizeInBytes());
}

QImage ImageCache::lookupLocked(const QString &key, bool *found)
{
    *found = false;
    const auto it = m_entries.find(key);
    if (it == m_entries.end())
        return QImage();
    *found = true;
    it->lastUse = ++m_tick;
    return it->image;
}

void ImageCache::insertLocked(const QString &key, const QImage &image)
{
    const qint64 bytes = bytesFor(image);
    if (bytes > m_limits.budgetBytes)
        return;     // a single image larger than the whole budget is not cached

    const auto existing = m_entries.find(key);
    if (existing != m_entries.end()) {
        m_bytes -= existing->bytes;
        m_entries.erase(existing);
    }
    Entry entry;
    entry.image = image;
    entry.bytes = bytes;
    entry.lastUse = ++m_tick;
    m_entries.insert(key, entry);
    m_bytes += bytes;
    evictLocked();
}

void ImageCache::evictLocked()
{
    while (!m_entries.isEmpty()
           && (m_entries.size() > m_limits.maxEntries || m_bytes > m_limits.budgetBytes)) {
        // Evicting the least recently used entry: a linear scan is plenty for a
        // few hundred entries and keeps the bookkeeping trivially correct.
        auto victim = m_entries.begin();
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (it->lastUse < victim->lastUse)
                victim = it;
        }
        m_bytes -= victim->bytes;
        m_entries.erase(victim);
        ++m_evictions;
    }
}

QImage ImageCache::decode(const QString &key, const QByteArray &data)
{
    if (data.isEmpty()) {
        QMutexLocker locker(&m_mutex);
        ++m_misses;
        return QImage();
    }

    if (!key.isEmpty()) {
        QMutexLocker locker(&m_mutex);
        bool found = false;
        const QImage cached = lookupLocked(key, &found);
        if (found) {
            ++m_hits;
            return cached;
        }
        ++m_misses;
    }

    // The decode itself happens outside the lock: two threads decoding
    // different images must not serialise, and a decode never touches the
    // cache's state.
    const QByteArray kind = signatureOf(data);
    QImage decoded;
    if (!kind.isEmpty()) {
        QBuffer buffer;
        buffer.setData(data);
        if (buffer.open(QIODevice::ReadOnly)) {
            QImageReader reader(&buffer);
            reader.setAutoTransform(true);
            const QSize declared = reader.size();
            const bool tooLarge =
                declared.isValid()
                && (declared.width() > m_limits.maxWidthPx
                    || declared.height() > m_limits.maxHeightPx
                    || qint64(declared.width()) * qint64(declared.height())
                           > m_limits.maxPixels);
            // Refuse oversized images before allocating, so a deliberately
            // huge file cannot exhaust memory.
            if (!tooLarge) {
                QImage image;
                if (reader.read(&image) && !image.isNull()) {
                    if (image.format() != QImage::Format_ARGB32_Premultiplied)
                        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                    decoded = image;
                }
            }
        }
    }

    if (!key.isEmpty()) {
        QMutexLocker locker(&m_mutex);
        // A null result is cached on purpose: a broken asset must not be
        // re-decoded on every repaint.
        insertLocked(key, decoded);
    }
    return decoded;
}

QImage ImageCache::decodeFile(const QString &path)
{
    if (path.isEmpty())
        return QImage();

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return QImage();
    const qint64 size = info.size();
    if (size <= 0 || size > kMaxFileBytes)
        return QImage();

    // Size and modification time are part of the key, so editing the file in
    // another application invalidates the cached decode.
    const QString key = QStringLiteral("file:%1|%2|%3")
                            .arg(info.absoluteFilePath())
                            .arg(size)
                            .arg(info.lastModified().toMSecsSinceEpoch());

    {
        QMutexLocker locker(&m_mutex);
        bool found = false;
        const QImage cached = lookupLocked(key, &found);
        if (found) {
            ++m_hits;
            return cached;
        }
        ++m_misses;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QImage();
    const QByteArray data = file.read(kMaxFileBytes + 1);
    file.close();
    if (data.size() > kMaxFileBytes)
        return QImage();

    const QImage decoded = decode(QString(), data);
    {
        QMutexLocker locker(&m_mutex);
        insertLocked(key, decoded);
    }
    return decoded;
}

void ImageCache::insert(const QString &key, const QImage &image)
{
    if (key.isEmpty())
        return;
    QMutexLocker locker(&m_mutex);
    insertLocked(key, image);
}

bool ImageCache::contains(const QString &key) const
{
    QMutexLocker locker(&m_mutex);
    return m_entries.contains(key);
}

QImage ImageCache::value(const QString &key) const
{
    QMutexLocker locker(&m_mutex);
    bool found = false;
    const QImage image = const_cast<ImageCache *>(this)->lookupLocked(key, &found);
    if (found)
        ++m_hits;
    else
        ++m_misses;
    return image;
}

void ImageCache::remove(const QString &key)
{
    QMutexLocker locker(&m_mutex);
    const auto it = m_entries.find(key);
    if (it == m_entries.end())
        return;
    m_bytes -= it->bytes;
    m_entries.erase(it);
}

void ImageCache::clear()
{
    QMutexLocker locker(&m_mutex);
    m_entries.clear();
    m_bytes = 0;
}

void ImageCache::setLimits(const Limits &limits)
{
    QMutexLocker locker(&m_mutex);
    m_limits = limits;
    evictLocked();
}

ImageCache::Limits ImageCache::limits() const
{
    QMutexLocker locker(&m_mutex);
    return m_limits;
}

ImageCache::Stats ImageCache::stats() const
{
    QMutexLocker locker(&m_mutex);
    Stats s;
    s.entries = int(m_entries.size());
    s.bytes = m_bytes;
    s.hits = m_hits;
    s.misses = m_misses;
    s.evictions = m_evictions;
    return s;
}

void ImageCache::resetStats()
{
    QMutexLocker locker(&m_mutex);
    m_hits = 0;
    m_misses = 0;
    m_evictions = 0;
}

bool ImageCache::looksLikeImage(const QByteArray &data)
{
    return !signatureOf(data).isEmpty();
}

QString ImageCache::imageKind(const QByteArray &data)
{
    const QByteArray kind = signatureOf(data);
    if (kind.isEmpty())
        return QString();
    if (kind == QByteArrayLiteral("RIFF"))
        return cacheTr("an audio file");
    return QString::fromLatin1(kind);
}

} // namespace occ
