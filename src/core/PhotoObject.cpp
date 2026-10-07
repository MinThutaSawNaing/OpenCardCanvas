#include "core/PhotoObject.h"

#include "rendering/RenderContext.h"

#include <QCoreApplication>
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

} // namespace

PhotoObject::PhotoObject()
    : ImageObject(ObjectType::Photo, ObjectId::createUuid())
{
    // A portrait aspect ratio with a circular crop is the common default for an
    // ID photograph.
    setRectMm(QRectF(5.0, 5.0, 25.0, 30.0));
    m_name = defaultName();
}

void PhotoObject::setCornerRadiusMm(double mm)
{
    if (!std::isfinite(mm))
        return;
    m_cornerRadiusMm = qBound(0.0, mm, 100.0);
}

void PhotoObject::setBorderWidthMm(double mm)
{
    if (!std::isfinite(mm))
        return;
    m_borderWidthMm = qBound(0.0, mm, 10.0);
}

void PhotoObject::setLockedAspectRatio(double ratio)
{
    if (!std::isfinite(ratio) || ratio <= 0.0) {
        m_lockedAspect = 0.0;
        return;
    }
    m_lockedAspect = qBound(0.01, ratio, 100.0);
}

QPainterPath PhotoObject::shapePath() const
{
    const QRectF box(0.0, 0.0, widthMm(), heightMm());
    QPainterPath path;
    switch (m_cropShape) {
    case CropShape::Rectangle:
        path.addRect(box);
        break;
    case CropShape::RoundedRectangle: {
        const double r = qMin(m_cornerRadiusMm, qMin(box.width(), box.height()) / 2.0);
        path.addRoundedRect(box, r, r);
        break;
    }
    case CropShape::Ellipse:
        path.addEllipse(box);
        break;
    }
    return path;
}

void PhotoObject::applyCircularCrop()
{
    const double d = qMin(widthMm(), heightMm());
    setRectMm(QRectF(xMm(), yMm(), d, d));
    setCropShape(CropShape::Ellipse);
}

void PhotoObject::paintObject(QPainter &painter, const RenderContext &ctx) const
{
    // Clip the photograph to the crop shape, then draw the photograph itself.
    painter.save();
    painter.setClipPath(shapePath(), Qt::IntersectClip);
    ImageObject::paintObject(painter, ctx);
    painter.restore();

    // The border is drawn outside the clip so it stays crisp on the edge.
    drawBorder(painter);
}

void PhotoObject::drawBorder(QPainter &painter) const
{
    if (m_borderWidthMm <= 0.0 || m_borderColor.alpha() == 0)
        return;

    painter.save();
    QPen pen(m_borderColor);
    pen.setWidthF(m_borderWidthMm);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Inset by half the pen width so the stroke stays inside the frame.
    const double inset = m_borderWidthMm / 2.0;
    QPainterPath path;
    const QRectF box(inset, inset,
                     qMax(0.0, widthMm() - m_borderWidthMm),
                     qMax(0.0, heightMm() - m_borderWidthMm));
    switch (m_cropShape) {
    case CropShape::Rectangle:
        path.addRect(box);
        break;
    case CropShape::RoundedRectangle: {
        const double r = qMax(0.0, qMin(m_cornerRadiusMm - inset,
                                        qMin(box.width(), box.height()) / 2.0));
        path.addRoundedRect(box, r, r);
        break;
    }
    case CropShape::Ellipse:
        path.addEllipse(box);
        break;
    }
    painter.drawPath(path);
    painter.restore();
}


QJsonObject PhotoObject::propertiesToJson() const
{
    QJsonObject o = ImageObject::propertiesToJson();
    o.insert(QStringLiteral("cropShape"), names::cropShape(m_cropShape));
    o.insert(QStringLiteral("cornerRadiusMm"), m_cornerRadiusMm);
    o.insert(QStringLiteral("borderWidthMm"), m_borderWidthMm);
    o.insert(QStringLiteral("borderColor"), m_borderColor.name(QColor::HexArgb));
    o.insert(QStringLiteral("lockedAspectRatio"), m_lockedAspect);
    return o;
}

bool PhotoObject::propertiesFromJson(const QJsonObject &json, QString *error)
{
    if (!ImageObject::propertiesFromJson(json, error))
        return false;

    CropShape shape = m_cropShape;
    if (names::cropShapeFromString(json.value(QStringLiteral("cropShape")).toString(), &shape))
        m_cropShape = shape;

    setCornerRadiusMm(json.value(QStringLiteral("cornerRadiusMm")).toDouble(m_cornerRadiusMm));
    setBorderWidthMm(json.value(QStringLiteral("borderWidthMm")).toDouble(m_borderWidthMm));
    m_borderColor = colorFromJson(json.value(QStringLiteral("borderColor")), m_borderColor);
    setLockedAspectRatio(json.value(QStringLiteral("lockedAspectRatio")).toDouble(m_lockedAspect));
    return true;
}

QString PhotoObject::defaultName() const
{
    return QCoreApplication::translate("PhotoObject", "Photo");
}

CardObjectPtr PhotoObject::cloneImpl() const
{
    auto copy = std::make_unique<PhotoObject>();
    copy->m_assetId = m_assetId;
    copy->m_fitMode = m_fitMode;
    copy->m_sourceRect = m_sourceRect;
    copy->m_background = m_background;
    copy->m_cropShape = m_cropShape;
    copy->m_cornerRadiusMm = m_cornerRadiusMm;
    copy->m_borderWidthMm = m_borderWidthMm;
    copy->m_borderColor = m_borderColor;
    copy->m_lockedAspect = m_lockedAspect;
    return copy;
}

} // namespace occ
