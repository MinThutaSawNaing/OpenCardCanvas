#pragma once

#include "rendering/RenderContext.h"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

// ---------------------------------------------------------------------------
// AssetStore - every image a project uses, held in memory and addressable by a
// stable id.
//
// Why an indirection instead of file paths: a project must be self contained
// and portable. Images are copied into the store (and therefore into the
// .occard container) the moment they are imported, so a design keeps working
// after the original file has been moved, renamed or deleted - which is exactly
// what happens to a photo that arrived on a memory stick.
//
// The original bytes are preserved verbatim. Nothing is re-encoded on import
// unless the caller hands over an already decoded QImage (in which case PNG is
// the lossless choice). Decoding happens lazily and is funnelled through
// ImageCache so that a card repeated at 300 dpi decodes its photo once.
//
// The class implements occ::ImageProvider, which is how the renderer resolves
// images. That keeps the rendering code free of any file system access and
// makes it unit testable with a five line stub.
// ---------------------------------------------------------------------------
namespace occ {

class AssetStore : public ImageProvider
{
public:
    AssetStore();
    ~AssetStore() override;

    AssetStore(const AssetStore &) = delete;
    AssetStore &operator=(const AssetStore &) = delete;

    // A fresh, globally unique asset id (a UUID without braces).
    static QString newAssetId();

    // --- adding -------------------------------------------------------------
    // Encodes `image` as PNG (lossless) and stores it. `preferredFileName` is
    // kept for display and for the extension inside the container.
    QString addImage(const QImage &image, const QString &preferredFileName,
                     QString *error = nullptr);

    // Reads `path` and stores its bytes unchanged. Returns an empty string and
    // fills `error` when the file is missing, unreadable or not an image.
    QString addImageFile(const QString &path, QString *error = nullptr);

    // Stores raw bytes. The payload is validated by attempting a header probe,
    // so a text file cannot enter the store pretending to be a photo.
    QString addImageData(const QByteArray &data, const QString &fileName,
                         QString *error = nullptr);

    // --- ImageProvider ------------------------------------------------------
    bool has(const QString &assetId) const override;
    QImage image(const QString &assetId) const override;
    QString description(const QString &assetId) const override;

    // --- inspection ---------------------------------------------------------
    QString fileName(const QString &assetId) const;
    QByteArray rawData(const QString &assetId) const;
    QStringList assetIds() const;      // insertion order
    int count() const;
    bool isEmpty() const;

    // --- modification -------------------------------------------------------
    bool remove(const QString &assetId);
    void clear();

    // --- persistence --------------------------------------------------------
    // One entry per asset: "<assetId>.<ext>" -> raw bytes.
    QVector<QPair<QString, QByteArray>> serialise() const;
    // Restores a store from its serialised form. Entries whose name or id is
    // malformed are skipped rather than aborting the whole load; the number of
    // skipped entries is appended to `warnings` when it is non-null.
    void deserialise(const QVector<QPair<QString, QByteArray>> &entries,
                     QStringList *warnings = nullptr);

    // Total bytes held in memory (used by the diagnostics dialog).
    qint64 memoryUsageBytes() const;

private:
    struct Asset
    {
        QString    originalFileName;
        QString    fileExtension;
        QByteArray bytes;
        QString    mimeKind;      // "png", "jpeg", ... as detected from the data
    };

    void recomputeMemoryUsage();
    static QString detectKind(const QByteArray &data);
    static QString extensionForKind(const QString &kind, const QString &fallbackName);

    QHash<QString, Asset> m_assets;
    QStringList           m_order;      // deterministic serialisation order
    qint64                m_memoryUsage = 0;
};

} // namespace occ
