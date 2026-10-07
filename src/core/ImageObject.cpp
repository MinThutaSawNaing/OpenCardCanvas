#include "core/ImageObject.h"

#include "core/Units.h"
#include "rendering/RenderContext.h"

#include <QCoreApplication>
#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <QtMath>

namespace occ {

namespace {

QColor colorFromJson(const QJsonValue &value, const QColor &fallback)
{
    if (!value.isString())
        return fallback;
    const QColor c(value.toString());
    return c.isValid() ? c : fallback;
}

// Draws the conventional "missing image" marker: a dashed border with a
// diagonal cross. This is an honest indication that something is missing
// rather than a substitute picture.
void paintMissing(QPainter &painter, double wMm, double hMm)
{
    painter.save();
    QPen pen(QColor(200, 40, 40, 220));
    pen.setWidthF(0.15);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(QColor(255, 240, 240, 120));
    const QRectF box(0.0, 0.0, wMm, hMm);
    painter.drawRect(box);
    painter.drawLine(box.topLeft(), box.bottomRight());
    painter.drawLine(box.topRight(), box.bottomLeft());
    painter.restore();
}

} // namespace

int ImageObject::assumedDpi()
{
    return 300;
}

ImageObject::ImageObject() : CardObject(ObjectType::Image, ObjectId::createUuid())
{
    setRectMm(QRectF(5.0, 5.0, 25.0, 30.0));
    m_name = defaultName();
}

ImageObject::ImageObject(ObjectType type, const ObjectId &id)
    : CardObject(type, id)
{
    setRectMm(QRectF(5.0, 5.0, 25.0, 30.0));
}

void ImageObject::setSourceRect(const QRectF &rect)
{
    QRectF r = rect.normalized();
    // Clamp into the unit square; a zero-area crop would draw nothing.
    r.setLeft(qBound(0.0, r.left(), 1.0));
    r.setTop(qBound(0.0, r.top(), 1.0));
    r.setRight(qBound(0.0, r.right(), 1.0));
    r.setBottom(qBound(0.0, r.bottom(), 1.0));
    if (r.width() < 0.01)
        r.setWidth(0.01);
    if (r.height() < 0.01)
        r.setHeight(0.01);
    m_sourceRect = r;
}

bool ImageObject::isCropped() const
{
    return !qFuzzyCompare(m_sourceRect.x(), 0.0) || !qFuzzyCompare(m_sourceRect.y(), 0.0)
           || !qFuzzyCompare(m_sourceRect.width(), 1.0)
           || !qFuzzyCompare(m_sourceRect.height(), 1.0);
}

void ImageObject::paintObject(QPainter &painter, const RenderContext &ctx) const
{
    const double wMm = widthMm();
    const double hMm = heightMm();
    if (wMm <= 0.0 || hMm <= 0.0)
        return;

    const QImage image = ctx.image(m_assetId, id());
    if (image.isNull()) {
        paintMissing(painter, wMm, hMm);
        return;
    }

    // Non-destructive crop, in source pixels.
    const QRect imageBounds = image.rect();
    QRectF src(imageBounds.width() * m_sourceRect.x(), imageBounds.height() * m_sourceRect.y(),
               imageBounds.width() * m_sourceRect.width(),
               imageBounds.height() * m_sourceRect.height());
    src = src.intersected(QRectF(0, 0, imageBounds.width(), imageBounds.height()));
    if (src.width() < 1.0 || src.height() < 1.0)
        src = QRectF(0, 0, imageBounds.width(), imageBounds.height());

    const QRectF target(0.0, 0.0, wMm, hMm);

    if (m_background.alpha() > 0)
        painter.fillRect(target, m_background);

    painter.save();
    painter.setClipRect(target);

    switch (m_fitMode) {
    case ImageFitMode::Stretch:
        painter.drawImage(target, image, src);
        break;

    case ImageFitMode::Contain: {
        const double sx = target.width() / src.width();
        const double sy = target.height() / src.height();
        const double s = qMin(sx, sy);
        const double dw = src.width() * s;
        const double dh = src.height() * s;
        const QRectF dst(target.center().x() - dw / 2.0, target.center().y() - dh / 2.0, dw, dh);
        painter.drawImage(dst, image, src);
        break;
    }

    case ImageFitMode::Cover: {
        // Fill the frame, cropping the overflow. This is the normal choice for
        // an ID photograph.
        const double sx = target.width() / src.width();
        const double sy = target.height() / src.height();
        const double s = qMax(sx, sy);
        const double sw = target.width() / s;
        const double sh = target.height() / s;
        const QRectF adj(src.center().x() - sw / 2.0, src.center().y() - sh / 2.0, sw, sh);
        painter.drawImage(target, image, adj);
        break;
    }

    case ImageFitMode::Center: {
        // Natural size: honour the image's physical resolution when it has one.
        double dpi = ImageObject::assumedDpi();
        if (image.dotsPerMeterX() > 0)
            dpi = image.dotsPerMeterX() * 0.0254;
        const double natW = units::pxToMm(image.width(), dpi);
        const double natH = units::pxToMm(image.height(), dpi);
        const QRectF dst(target.center().x() - natW / 2.0, target.center().y() - natH / 2.0,
                         natW, natH);
        painter.drawImage(dst, image, src);
        break;
    }

    case ImageFitMode::Tile: {
        double dpi = ImageObject::assumedDpi();
        if (image.dotsPerMeterX() > 0)
            dpi = image.dotsPerMeterX() * 0.0254;
        const double tileW = qMax(0.5, units::pxToMm(src.width(), dpi));
        const double tileH = qMax(0.5, units::pxToMm(src.height(), dpi));
        for (double y = 0.0; y < hMm; y += tileH) {
            for (double x = 0.0; x < wMm; x += tileW) {
                const double tw = qMin(tileW, wMm - x);
                const double th = qMin(tileH, hMm - y);
                painter.drawImage(QRectF(x, y, tw, th), image, src);
            }
        }
        break;
    }
    }

    painter.restore();
}


QJsonObject ImageObject::propertiesToJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("assetId"), m_assetId);
    o.insert(QStringLiteral("fit"), names::imageFit(m_fitMode));
    QJsonObject src;
    src.insert(QStringLiteral("x"), m_sourceRect.x());
    src.insert(QStringLiteral("y"), m_sourceRect.y());
    src.insert(QStringLiteral("w"), m_sourceRect.width());
    src.insert(QStringLiteral("h"), m_sourceRect.height());
    o.insert(QStringLiteral("sourceRect"), src);
    o.insert(QStringLiteral("background"), m_background.name(QColor::HexArgb));
    return o;
}

bool ImageObject::propertiesFromJson(const QJsonObject &json, QString *error)
{
    m_assetId = json.value(QStringLiteral("assetId")).toString(m_assetId);

    ImageFitMode fit = m_fitMode;
    if (names::imageFitFromString(json.value(QStringLiteral("fit")).toString(), &fit))
        m_fitMode = fit;

    const QJsonValue srcValue = json.value(QStringLiteral("sourceRect"));
    if (srcValue.isObject()) {
        const QJsonObject src = srcValue.toObject();
        const double x = src.value(QStringLiteral("x")).toDouble(0.0);
        const double y = src.value(QStringLiteral("y")).toDouble(0.0);
        const double w = src.value(QStringLiteral("w")).toDouble(1.0);
        const double h = src.value(QStringLiteral("h")).toDouble(1.0);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h)) {
            if (error) {
                *error = QCoreApplication::translate(
                    "ImageObject", "An image object in this project has an invalid crop area.");
            }
            return false;
        }
        setSourceRect(QRectF(x, y, w, h));
    }

    m_background = colorFromJson(json.value(QStringLiteral("background")), m_background);
    return true;
}

QString ImageObject::defaultName() const
{
    return QCoreApplication::translate("ImageObject", "Image");
}

CardObjectPtr ImageObject::cloneImpl() const
{
    auto copy = std::make_unique<ImageObject>();
    copy->m_assetId = m_assetId;
    copy->m_fitMode = m_fitMode;
    copy->m_sourceRect = m_sourceRect;
    copy->m_background = m_background;
    return copy;
}

} // namespace occ
