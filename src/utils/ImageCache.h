#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QSize>
#include <QString>

// ---------------------------------------------------------------------------
// ImageCache - a bounded, thread safe LRU cache of decoded images.
//
// Decoding is the single most expensive thing the personalization path does: a
// 1 000 record batch re-uses one logo and a handful of photographs, and decoding
// them once instead of a thousand times is the difference between a batch taking
// seconds and taking minutes.
//
// Safety properties that matter more than speed here:
//   * a decode failure caches a NULL image, so a broken asset is not retried on
//     every repaint (and never throws)
//   * an optional pixel budget refuses absurd images BEFORE allocating, so a
//     deliberately oversized PNG cannot exhaust memory
//   * every entry point is mutex protected, so the cache can be used from a
//     worker thread without the caller having to reason about it
// ---------------------------------------------------------------------------
namespace occ {

class ImageCache
{
public:
    struct Limits
    {
        qint64 budgetBytes = 256LL * 1024 * 1024;   // total decoded pixels budget
        int    maxEntries = 256;
        // Dimensions above these are rejected instead of decoded.
        int    maxWidthPx = 20000;
        int    maxHeightPx = 20000;
        // Total decoded pixels of a single image. 80 MP is far beyond any card
        // artwork and still small enough to be harmless if it happened.
        qint64 maxPixels = 80LL * 1000 * 1000;
    };

    struct Stats
    {
        int    entries = 0;
        qint64 bytes = 0;
        qint64 hits = 0;
        qint64 misses = 0;
        qint64 evictions = 0;
    };

    ImageCache();
    explicit ImageCache(const Limits &limits);
    ~ImageCache();

    ImageCache(const ImageCache &) = delete;
    ImageCache &operator=(const ImageCache &) = delete;

    // The process wide cache used by AssetStore and the renderer.
    static ImageCache &instance();

    // --- decoding -----------------------------------------------------------
    // Decodes `data` (PNG/JPEG/BMP/TIFF/...) honouring the size limits. Returns
    // a null image for undecodable or oversized data - never throws, never
    // blocks on anything but its own mutex.
    // `key` identifies the payload; pass the asset id where one exists, or any
    // stable string for the bytes. An empty key decodes without caching.
    QImage decode(const QString &key, const QByteArray &data);
    // Same, from a file. The file's size and modification time are part of the
    // cache key so an edited file is re-read.
    QImage decodeFile(const QString &path);

    // Inserts an already decoded image (used when the caller decoded it outside
    // the cache, e.g. during import).
    void insert(const QString &key, const QImage &image);

    // --- management ---------------------------------------------------------
    bool contains(const QString &key) const;
    QImage value(const QString &key) const;
    void remove(const QString &key);
    void clear();
    void setLimits(const Limits &limits);
    Limits limits() const;
    Stats stats() const;
    void resetStats();

    // --- helpers ------------------------------------------------------------
    // True when the byte array starts with a known image signature. Cheap check
    // used by AssetStore before accepting an import.
    static bool looksLikeImage(const QByteArray &data);
    // Human readable image kind ("PNG", "JPEG", ...) or an empty string.
    static QString imageKind(const QByteArray &data);

private:
    struct Entry
    {
        QImage  image;
        qint64  bytes = 0;
        quint64 lastUse = 0;    // monotonic clock, larger value = more recent
    };

    QImage lookupLocked(const QString &key, bool *found);
    void   insertLocked(const QString &key, const QImage &image);
    void   evictLocked();
    qint64 bytesFor(const QImage &image);

    mutable QMutex       m_mutex;
    Limits               m_limits;
    QHash<QString, Entry> m_entries;
    quint64              m_tick = 0;
    qint64               m_bytes = 0;
    mutable qint64       m_hits = 0;
    mutable qint64       m_misses = 0;
    qint64               m_evictions = 0;
};

} // namespace occ
