#include "project/AssetStore.h"

#include "utils/ImageCache.h"
#include "utils/TextUtils.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace occ {

namespace {

QString assetTr(const char *text)
{
    return QCoreApplication::translate("AssetStore", text);
}

// Refuse anything implausible before it reaches memory. A card image is a few
// megabytes at most; 64 MB is already far beyond a sane photograph.
constexpr qint64 kMaxAssetBytes = 64LL * 1024 * 1024;

} // namespace

AssetStore::AssetStore() = default;

AssetStore::~AssetStore() = default;

QString AssetStore::newAssetId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString AssetStore::extensionForKind(const QString &kind, const QString &fallbackName)
{
    if (kind == QLatin1String("PNG"))  return QStringLiteral("png");
    if (kind == QLatin1String("JPEG")) return QStringLiteral("jpg");
    if (kind == QLatin1String("BMP"))  return QStringLiteral("bmp");
    if (kind == QLatin1String("TIFF")) return QStringLiteral("tif");
    if (kind == QLatin1String("GIF"))  return QStringLiteral("gif");
    if (kind == QLatin1String("WEBP")) return QStringLiteral("webp");
    if (kind == QLatin1String("SVG"))  return QStringLiteral("svg");

    const QString suffix = QFileInfo(fallbackName).suffix().toLower();
    if (!suffix.isEmpty() && suffix.size() <= 5 && suffix.at(0).isLetter()) {
        return suffix;
    }
    return QStringLiteral("png");
}

QString AssetStore::detectKind(const QByteArray &data)
{
    return ImageCache::imageKind(data);
}

QString AssetStore::addImageData(const QByteArray &data, const QString &fileName,
                                 QString *error)
{
    const auto fail = [error](const QString &message) -> QString {
        if (error)
            *error = message;
        return QString();
    };

    if (data.isEmpty())
        return fail(assetTr("The image file is empty."));
    if (data.size() > kMaxAssetBytes) {
        return fail(assetTr("\"%1\" is %2, which is larger than the %3 an image "
                            "asset may use.")
                        .arg(QFileInfo(fileName).fileName(),
                             text::formatBytes(data.size()),
                             text::formatBytes(kMaxAssetBytes)));
    }
    if (!ImageCache::looksLikeImage(data)) {
        return fail(assetTr("\"%1\" is not an image OpenCardCanvas can use. "
                            "Supported formats are PNG, JPEG, BMP, TIFF, GIF, "
                            "WEBP and SVG.")
                        .arg(QFileInfo(fileName).fileName()));
    }
    // A real decode proves the payload is complete; a truncated download must
    // fail here rather than at print time.
    if (ImageCache::instance().decode(QString(), data).isNull()) {
        return fail(assetTr("\"%1\" could not be decoded. The file may be "
                            "damaged or incomplete.")
                        .arg(QFileInfo(fileName).fileName()));
    }

    Asset asset;
    asset.originalFileName = QFileInfo(fileName).fileName();
    asset.mimeKind = detectKind(data);
    asset.fileExtension = extensionForKind(asset.mimeKind, fileName);
    asset.bytes = data;

    const QString id = newAssetId();
    m_assets.insert(id, asset);
    m_order.append(id);
    m_memoryUsage += data.size();
    if (error)
        error->clear();
    return id;
}

QString AssetStore::addImage(const QImage &image, const QString &preferredFileName,
                             QString *error)
{
    if (image.isNull()) {
        if (error)
            *error = assetTr("There is no image to add.");
        return QString();
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        if (error)
            *error = assetTr("The image could not be prepared for storage.");
        return QString();
    }
    // PNG is lossless, so an image the caller has already decoded keeps every
    // pixel it arrived with.
    const bool saved = image.save(&buffer, "PNG");
    buffer.close();
    if (!saved || bytes.isEmpty()) {
        if (error)
            *error = assetTr("The image could not be encoded for storage.");
        return QString();
    }

    Asset asset;
    asset.originalFileName = preferredFileName.isEmpty()
                                 ? assetTr("image.png")
                                 : QFileInfo(preferredFileName).fileName();
    asset.fileExtension = QStringLiteral("png");
    asset.mimeKind = QStringLiteral("PNG");
    asset.bytes = bytes;

    const QString id = newAssetId();
    m_assets.insert(id, asset);
    m_order.append(id);
    m_memoryUsage += bytes.size();
    if (error)
        error->clear();
    return id;
}

QString AssetStore::addImageFile(const QString &path, QString *error)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        if (error) {
            *error = assetTr("\"%1\" does not exist.")
                         .arg(info.fileName().isEmpty() ? path : info.fileName());
        }
        return QString();
    }
    if (info.size() > kMaxAssetBytes) {
        if (error) {
            *error = assetTr("\"%1\" is %2, which is larger than the %3 an image "
                             "asset may use.")
                         .arg(info.fileName(), text::formatBytes(info.size()),
                              text::formatBytes(kMaxAssetBytes));
        }
        return QString();
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = assetTr("\"%1\" could not be opened for reading. It may be in "
                             "use by another application.")
                         .arg(info.fileName());
        }
        return QString();
    }
    const QByteArray data = file.read(kMaxAssetBytes + 1);
    file.close();

    // The original bytes are stored unchanged, so a JPEG is never re-encoded
    // and no quality is lost by importing it.
    return addImageData(data, info.fileName(), error);
}

bool AssetStore::has(const QString &assetId) const
{
    return !assetId.isEmpty() && m_assets.contains(assetId);
}

QImage AssetStore::image(const QString &assetId) const
{
    const auto it = m_assets.constFind(assetId);
    if (it == m_assets.constEnd())
        return QImage();
    // Decoding is funnelled through the shared cache, so a photo repeated across
    // a hundred batch cards is decoded once. A null result is cached too, which
    // keeps a broken asset from being retried on every repaint.
    return ImageCache::instance().decode(QStringLiteral("asset:") + assetId, it->bytes);
}

QString AssetStore::description(const QString &assetId) const
{
    const auto it = m_assets.constFind(assetId);
    if (it == m_assets.constEnd())
        return assetTr("an unnamed image");
    if (!it->originalFileName.isEmpty())
        return it->originalFileName;
    return assetTr("image.%1").arg(it->fileExtension);
}

QString AssetStore::fileName(const QString &assetId) const
{
    const auto it = m_assets.constFind(assetId);
    return it == m_assets.constEnd() ? QString() : it->originalFileName;
}

QByteArray AssetStore::rawData(const QString &assetId) const
{
    const auto it = m_assets.constFind(assetId);
    return it == m_assets.constEnd() ? QByteArray() : it->bytes;
}

QStringList AssetStore::assetIds() const
{
    return m_order;
}

int AssetStore::count() const
{
    return int(m_assets.size());
}

bool AssetStore::isEmpty() const
{
    return m_assets.isEmpty();
}

qint64 AssetStore::memoryUsageBytes() const
{
    return m_memoryUsage;
}

bool AssetStore::remove(const QString &assetId)
{
    const auto it = m_assets.find(assetId);
    if (it == m_assets.end())
        return false;
    m_memoryUsage -= it->bytes.size();
    m_assets.erase(it);
    m_order.removeAll(assetId);
    ImageCache::instance().remove(QStringLiteral("asset:") + assetId);
    return true;
}

void AssetStore::clear()
{
    const QStringList ids = m_order;
    for (const QString &id : ids)
        ImageCache::instance().remove(QStringLiteral("asset:") + id);
    m_assets.clear();
    m_order.clear();
    m_memoryUsage = 0;
}

QVector<QPair<QString, QByteArray>> AssetStore::serialise() const
{
    QVector<QPair<QString, QByteArray>> entries;
    entries.reserve(m_order.size());
    for (const QString &id : m_order) {
        const auto it = m_assets.constFind(id);
        if (it == m_assets.constEnd())
            continue;
        // The entry name is "<assetId>.<extension>"; ProjectSerializer places it
        // in the container's assets/ directory.
        entries.append({ QStringLiteral("%1.%2").arg(id, it->fileExtension), it->bytes });
    }
    return entries;
}

void AssetStore::deserialise(const QVector<QPair<QString, QByteArray>> &entries,
                             QStringList *warnings)
{
    int skipped = 0;
    for (const auto &entry : entries) {
        QString name = entry.first;
        if (name.startsWith(QLatin1String("assets/")))
            name = name.mid(7);

        const int dot = name.lastIndexOf(QLatin1Char('.'));
        if (dot <= 0) {
            ++skipped;
            continue;
        }
        const QString id = name.left(dot);
        const QString extension = name.mid(dot + 1).toLower();
        if (id.isEmpty() || extension.isEmpty() || id.contains(QLatin1Char('/'))
            || id.contains(QLatin1Char('\\')) || entry.second.isEmpty()) {
            ++skipped;
            continue;
        }

        Asset asset;
        asset.originalFileName = QStringLiteral("%1.%2").arg(id, extension);
        asset.fileExtension = extension;
        asset.mimeKind = detectKind(entry.second);
        asset.bytes = entry.second;

        // A repeated id would make "which image is this?" ambiguous; the last
        // entry wins and the collision is reported.
        if (m_assets.contains(id)) {
            m_memoryUsage -= m_assets.value(id).bytes.size();
            m_order.removeAll(id);
            if (warnings) {
                warnings->append(
                    assetTr("The image \"%1\" appears more than once in this "
                            "project; the last copy was used.")
                        .arg(asset.originalFileName));
            }
        }
        m_assets.insert(id, asset);
        m_order.append(id);
        m_memoryUsage += asset.bytes.size();
        // The stored bytes replace anything cached under this id.
        ImageCache::instance().remove(QStringLiteral("asset:") + id);
    }

    if (skipped > 0 && warnings) {
        warnings->append(assetTr("%1 image entries in this project could not be "
                                 "used and were skipped.")
                             .arg(skipped));
    }
}

} // namespace occ
