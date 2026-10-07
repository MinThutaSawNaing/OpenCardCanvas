#pragma once

#include "core/CardGeometry.h"
#include "core/CardSide.h"
#include "core/CardTypes.h"
#include "rendering/RenderContext.h"

#include <QImage>
#include <QVector>

// ---------------------------------------------------------------------------
// CardRenderer - THE single render path.
//
// This is the only place in the application that iterates a CardSide in order
// to paint it. The editing canvas, the zoom preview, PNG/JPEG export, PDF
// output and the printer all end up here, which is the mechanism that makes
// "what you see is what you print" true rather than aspirational: there is no
// second implementation that could disagree with the first one.
//
// Coordinate system: `paintSide` expects a painter whose origin is the card's
// top-left corner (or the bleed box's top-left corner when includeBleed is
// set) and which has been scaled by RenderContext::pxPerMm(). Every object then
// paints itself in MILLIMETRES. The paint device's logical DPI must equal
// ctx.deviceDpi(); makeRenderTarget() builds an image that satisfies this, so
// font point sizes come out physically exact.
// ---------------------------------------------------------------------------
namespace occ {

class AssetStore;
class CardDocument;

class CardRenderer
{
public:
    struct Options
    {
        int    dpi = 300;
        bool   includeBleed = false;
        bool   forPrinting = false;
        // When non-zero this wins over `dpi`; used by the canvas, which renders
        // at the screen's own resolution rather than at a print resolution.
        double pxPerMmOverride = 0.0;

        double pxPerMm() const;
        double deviceDpi() const;
    };

    // --- painting -----------------------------------------------------------
    // Paints one side. The painter must already be in millimetre space (see the
    // class comment). `ctx` supplies the scale, the image provider and the
    // placeholder values.
    static void paintSide(QPainter &painter, const CardSide &side,
                          const CardGeometry &geom, const RenderContext &ctx);

    // Paints the side's background only. Separated because the canvas repaints
    // the background far more often than the objects while a user drags a
    // colour slider.
    static void paintBackground(QPainter &painter, const CardSide &side,
                                const CardGeometry &geom, const RenderContext &ctx);

    // --- rendering into an image -------------------------------------------
    static QImage renderSide(const CardDocument &doc, CardSideId side,
                             const Options &options,
                             QVector<RenderContext::Warning> *warnings = nullptr);

    static QImage renderSide(const CardSide &side, const CardGeometry &geom,
                             const AssetStore *assets, const Options &options,
                             QVector<RenderContext::Warning> *warnings = nullptr);

    // Renders using a context the caller has prepared. Batch personalization uses
    // this to supply per-record placeholder values and a per-record image
    // provider, so a batch card is produced by exactly the same paintSide() call
    // as everything else. The context's pxPerMm() must equal options.pxPerMm().
    static QImage renderSideWithContext(const CardSide &side, const CardGeometry &geom,
                                        const Options &options, RenderContext &ctx);

    // A small front/back preview for Open Recent and the template browser.
    static QImage renderPreviewThumbnail(const CardDocument &doc, CardSideId side,
                                         int maxWidthPx);

    // --- geometry helpers ---------------------------------------------------
    // Pixel size renderSide() will produce, so callers can reserve buffers.
    static QSize targetSizePx(const CardGeometry &geom, const Options &options);

    // The rectangle the side occupies in millimetres, including bleed when the
    // options ask for it. Origin is (0,0) for the trim box and
    // (-bleed,-bleed) for the bleed box.
    static QRectF paintAreaMm(const CardGeometry &geom, const Options &options);

    // Builds the context used by every render pass, so the DPI relationship
    // pxPerMm * 25.4 == deviceDpi() cannot drift.
    static RenderContext makeContext(const CardGeometry &geom, const Options &options,
                                     const ImageProvider *images);
};

} // namespace occ
