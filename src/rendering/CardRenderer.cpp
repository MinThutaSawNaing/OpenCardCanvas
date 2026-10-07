#include "rendering/CardRenderer.h"

#include "core/CardDocument.h"
#include "core/Units.h"
#include "project/AssetStore.h"

#include <QCoreApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

namespace occ {

namespace {

// The background is painted over the whole printable area, bleed included, which
// is what makes a card whose artwork runs to the edge actually print that way.
QRectF backgroundArea(const CardGeometry &geom)
{
    return geom.bleedBoundsMm();
}

QLinearGradient linearGradientFor(const CardSide::Background &background,
                                  const QRectF &area)
{
    const double radians = qDegreesToRadians(background.angleDeg);
    const QPointF direction(std::cos(radians), std::sin(radians));
    const QPointF centre = area.center();
    // Half the length of the area's projection on the direction: enough to span
    // the whole rectangle from either end, at any angle.
    const double reach = (qAbs(direction.x()) * area.width()
                          + qAbs(direction.y()) * area.height()) / 2.0;

    QLinearGradient gradient(centre - direction * reach, centre + direction * reach);
    gradient.setColorAt(0.0, background.color);
    gradient.setColorAt(1.0, background.color2);
    return gradient;
}

void paintImageBackground(QPainter &painter, const CardSide::Background &background,
                          const QRectF &area, const RenderContext &ctx)
{
    const QImage image = ctx.image(background.assetId, ObjectId());
    if (image.isNull()) {
        // A missing background image is reported by RenderContext; the area is
        // left alone so the user sees exactly what is missing instead of an
        // unexplained blank card.
        return;
    }

    if (background.stretchImage) {
        painter.drawImage(area, image);
        return;
    }

    // Not stretched: the image is centred at its natural aspect ratio.
    const QSize target = image.size().scaled(area.size().toSize(), Qt::KeepAspectRatio);
    if (target.isEmpty())
        return;
    QRectF targetRect(QPointF(0.0, 0.0), QSizeF(target));
    targetRect.moveCenter(area.center());
    painter.drawImage(targetRect, image);

    // Tiling the remainder keeps a small logo from leaving visible gaps.
    const double stepX = targetRect.width();
    const double stepY = targetRect.height();
    for (double y = area.top(); y < area.bottom(); y += stepY) {
        for (double x = area.left(); x < area.right(); x += stepX) {
            if (qFuzzyCompare(x, targetRect.left())
                && qFuzzyCompare(y, targetRect.top())) {
                continue;
            }
            painter.drawImage(QRectF(x, y, stepX, stepY), image);
        }
    }
}

} // namespace

double CardRenderer::Options::pxPerMm() const
{
    if (pxPerMmOverride > 0.0)
        return pxPerMmOverride;
    return units::pxPerMmFromDpi(dpi);
}

double CardRenderer::Options::deviceDpi() const
{
    return pxPerMm() * units::kMmPerInch;
}

QRectF CardRenderer::paintAreaMm(const CardGeometry &geom, const Options &options)
{
    return options.includeBleed ? geom.bleedBoundsMm() : geom.boundsMm();
}

QSize CardRenderer::targetSizePx(const CardGeometry &geom, const Options &options)
{
    const QRectF area = paintAreaMm(geom, options);
    const double pxPerMm = options.pxPerMm();
    // qRound, not qCeil: the renderer's output size must be exactly
    // CardGeometry::pixelSize() so a card and its 300 dpi export can never differ
    // by a pixel. Half a pixel of the outermost millimetre is not visible on a
    // printed card, but a one pixel mismatch between the preview and the file is a
    // bug report.
    const int w = qMax(1, qRound(area.width() * pxPerMm));
    const int h = qMax(1, qRound(area.height() * pxPerMm));
    return QSize(w, h);
}

RenderContext CardRenderer::makeContext(const CardGeometry &geom, const Options &options,
                                       const ImageProvider *images)
{
    Q_UNUSED(geom);
    RenderContext ctx(options.pxPerMm());
    ctx.setImageProvider(images);
    ctx.setForPrinting(options.forPrinting);
    return ctx;
}

void CardRenderer::paintBackground(QPainter &painter, const CardSide &side,
                                   const CardGeometry &geom, const RenderContext &ctx)
{
    const CardSide::Background &background = side.background();
    if (background.kind == CardSide::Background::Kind::None)
        return;

    const QRectF area = backgroundArea(geom);
    if (area.isEmpty())
        return;

    painter.save();
    painter.setOpacity(1.0);
    painter.setPen(Qt::NoPen);

    switch (background.kind) {
    case CardSide::Background::Kind::None:
        break;
    case CardSide::Background::Kind::Solid:
        painter.fillRect(area, background.color);
        break;
    case CardSide::Background::Kind::LinearGradient:
        painter.fillRect(area, linearGradientFor(background, area));
        break;
    case CardSide::Background::Kind::RadialGradient: {
        QRadialGradient gradient(area.center(), qMax(area.width(), area.height()) / 2.0);
        gradient.setColorAt(0.0, background.color);
        gradient.setColorAt(1.0, background.color2);
        painter.fillRect(area, gradient);
        break;
    }
    case CardSide::Background::Kind::Image:
        paintImageBackground(painter, background, area, ctx);
        break;
    }

    painter.restore();
}

void CardRenderer::paintSide(QPainter &painter, const CardSide &side,
                             const CardGeometry &geom, const RenderContext &ctx)
{
    // The single loop in the whole application that paints a card side. The
    // canvas, the preview, PNG/JPEG/PDF export and the printer all arrive here,
    // which is why they cannot disagree with each other.
    //
    // The painter's origin is the top-left corner of the paint area and one user
    // unit is one millimetre; the caller has already established both.
    painter.save();

    // Objects outside the card are not part of the output. With bleed switched on
    // the clip is the bleed box, exactly as CardGeometry documents.
    painter.setClipRect(geom.bleedBoundsMm(), Qt::IntersectClip);

    paintBackground(painter, side, geom, ctx);

    for (CardObject *object : side.objects()) {
        if (!object || !object->isVisible() || object->opacity() <= 0.0)
            continue;   // a hidden object is never drawn, exported or printed
        object->paint(painter, ctx);
    }

    painter.restore();
}

QImage CardRenderer::renderSide(const CardSide &side, const CardGeometry &geom,
                                const AssetStore *assets, const Options &options,
                                QVector<RenderContext::Warning> *warnings)
{
    QString geometryError;
    if (!geom.isValid(&geometryError)) {
        if (warnings) {
            warnings->append(
                { QCoreApplication::translate(
                      "CardRenderer",
                      "This card cannot be drawn because its size is not valid: %1")
                      .arg(geometryError),
                  ObjectId() });
        }
        return QImage();
    }

    RenderContext ctx = makeContext(geom, options, assets);
    ctx.setForEditing(false);
    const QImage image = renderSideWithContext(side, geom, options, ctx);
    if (warnings)
        *warnings = ctx.warnings();
    return image;
}

QImage CardRenderer::renderSideWithContext(const CardSide &side, const CardGeometry &geom,
                                          const Options &options, RenderContext &ctx)
{
    QString geometryError;
    if (!geom.isValid(&geometryError))
        return QImage();

    const QRectF area = paintAreaMm(geom, options);
    const QSize size = targetSizePx(geom, options);

    QImage image = makeRenderTarget(QSizeF(size), ctx);
    if (image.isNull())
        return image;

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // One user unit == one millimetre, and the user origin is the top-left corner
    // of the paint area (the bleed corner when bleed is included).
    painter.scale(options.pxPerMm(), options.pxPerMm());
    painter.translate(-area.x(), -area.y());

    paintSide(painter, side, geom, ctx);
    painter.end();
    return image;
}

QImage CardRenderer::renderSide(const CardDocument &doc, CardSideId side,
                                const Options &options,
                                QVector<RenderContext::Warning> *warnings)
{
    return renderSide(doc.side(side), doc.geometry(), doc.assets(), options, warnings);
}

QImage CardRenderer::renderPreviewThumbnail(const CardDocument &doc, CardSideId side,
                                            int maxWidthPx)
{
    const CardGeometry &geom = doc.geometry();
    if (geom.widthMm() <= 0.0)
        return QImage();

    Options options;
    const int width = qBound(16, maxWidthPx, 4096);
    options.pxPerMmOverride = double(width) / geom.widthMm();
    // forPrinting stays false: a thumbnail is a screen artefact, not output.
    return renderSide(doc, side, options, nullptr);
}

} // namespace occ
