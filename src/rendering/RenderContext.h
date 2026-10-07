#pragma once

#include "core/CardTypes.h"

#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QImage>
#include <QString>
#include <QVector>

class QPainter;

namespace occ {

// ---------------------------------------------------------------------------
// ImageProvider - everything the renderer needs in order to resolve an image
// asset. AssetStore implements it in the application; tests use a simple
// in-memory implementation. Keeping the renderer behind this interface means
// rendering can be unit tested without touching the file system.
// ---------------------------------------------------------------------------
class ImageProvider
{
public:
    virtual ~ImageProvider() = default;
    virtual bool has(const QString &assetId) const = 0;
    virtual QImage image(const QString &assetId) const = 0;
    // Human readable name used in error messages ("photo.png").
    virtual QString description(const QString &assetId) const = 0;
};

// ---------------------------------------------------------------------------
// RenderContext - the single source of truth for one render pass.
//
// Every renderer (canvas, preview, PNG/PDF export, printer output) builds a
// context and calls the very same CardRenderer code. That is what guarantees
// "what you see is what prints".
// ---------------------------------------------------------------------------
class RenderContext
{
public:
    struct Warning
    {
        QString  message;      // user facing, already translated
        ObjectId objectId;     // may be null when not object specific
    };

    explicit RenderContext(double pxPerMm = 4.0) : m_pxPerMm(pxPerMm) {}

    // --- scale --------------------------------------------------------------
    // Device units (pixels) per millimetre. The paint device's logical DPI must
    // be pxPerMm * 25.4 and the painter must be scaled by pxPerMm, which is what
    // makes font point sizes physically exact on every output device.
    double pxPerMm() const { return m_pxPerMm; }
    void setPxPerMm(double value) { m_pxPerMm = value; }
    double mmToPx(double mm) const { return mm * m_pxPerMm; }
    double pxToMm(double px) const { return m_pxPerMm > 0.0 ? px / m_pxPerMm : 0.0; }

    // Required logical DPI for a paint device used with this context.
    double deviceDpi() const { return m_pxPerMm * 25.4; }

    // --- assets -------------------------------------------------------------
    const ImageProvider *imageProvider() const { return m_images; }
    void setImageProvider(const ImageProvider *provider) { m_images = provider; }
    // Returns a null image when the asset cannot be resolved (a warning is
    // recorded so the UI can tell the user exactly what is missing).
    QImage image(const QString &assetId, const ObjectId &owner) const;

    // --- personalization placeholders --------------------------------------
    // When set, every user visible string is expanded through the template
    // engine. The pointer is borrowed and must outlive the render pass.
    void setPlaceholders(const QHash<QString, QString> *values) { m_placeholders = values; }
    const QHash<QString, QString> *placeholders() const { return m_placeholders; }
    bool hasPlaceholders() const { return m_placeholders != nullptr; }
    // Expands {{key}} occurrences; missing keys are left for the engine to
    // report through warningMissingPlaceholders().
    QString resolveText(const QString &text, const ObjectId &owner) const;

    // --- fonts --------------------------------------------------------------
    // Builds a font with an exact physical point size. Point sizes are physical
    // because the paint device DPI is derived from pxPerMm().
    QFont makeFont(double pointSize, const QString &family, bool bold, bool italic,
                   bool underline) const;

    // Font metrics resolved against the painter's device, so advances are
    // returned in DEVICE pixels regardless of the painter's world transform.
    // Callers convert to millimetres with pxToMm(). Returns metrics for a
    // default device when the painter has none (never happens for our targets,
    // but keeps the renderer from crashing on an unusual paint device).
    QFontMetricsF metrics(const QFont &font, const QPainter &painter) const;
    // Returns false (and records a warning once per family) when the requested
    // font family is not installed.
    bool fontFamilyAvailable(const QString &family, const ObjectId &owner) const;

    // --- warnings -----------------------------------------------------------
    void addWarning(const QString &message, const ObjectId &objectId = ObjectId()) const;
    QVector<Warning> warnings() const { return m_warnings; }
    void clearWarnings() const { m_warnings.clear(); }
    bool hasWarnings() const { return !m_warnings.isEmpty(); }

    // --- options ------------------------------------------------------------
    // Set while rendering for the printer, where a small amount of colour
    // adjustment may be requested by the user.
    bool forPrinting() const { return m_forPrinting; }
    void setForPrinting(bool on) { m_forPrinting = on; }

    // Set while rendering the editing canvas: selection chrome, object borders
    // and locked-object indicators are drawable, but never for export/print.
    bool forEditing() const { return m_forEditing; }
    void setForEditing(bool on) { m_forEditing = on; }

private:
    double                       m_pxPerMm = 4.0;
    const ImageProvider         *m_images = nullptr;
    const QHash<QString, QString> *m_placeholders = nullptr;
    bool                         m_forPrinting = false;
    bool                         m_forEditing = false;

    mutable QVector<Warning>     m_warnings;
    mutable QStringList          m_reportedFontFamilies;
    mutable QStringList          m_reportedAssets;
};

// ---------------------------------------------------------------------------
// Convenience: creates an image whose logical DPI matches the context so that
// text drawn into it uses physically exact point sizes.
// ---------------------------------------------------------------------------
QImage makeRenderTarget(const QSizeF &sizePx, const RenderContext &ctx);

} // namespace occ
