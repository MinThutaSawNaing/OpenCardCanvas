#include "core/ShapeObject.h"

#include "rendering/RenderContext.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QPainter>
#include <QtMath>

#include <cmath>

namespace occ {

namespace {

constexpr int kMinSides = 3;
constexpr int kMaxSides = 24;

QColor colorFromJson(const QJsonValue &value, const QColor &fallback)
{
    if (!value.isString())
        return fallback;
    const QColor c(value.toString());
    return c.isValid() ? c : fallback;
}

} // namespace

ShapeObject::ShapeObject() : CardObject(ObjectType::Shape, ObjectId::createUuid())
{
    setRectMm(QRectF(5.0, 5.0, 30.0, 20.0));
    m_name = defaultName();
}

ShapeObject::ShapeObject(ShapeKind kind) : ShapeObject()
{
    m_kind = kind;
    m_name = defaultName();
}

void ShapeObject::setStrokeWidthMm(double mm)
{
    if (!std::isfinite(mm))
        return;
    m_strokeWidthMm = qBound(0.0, mm, 20.0);
}

void ShapeObject::setCornerRadiusMm(double mm)
{
    if (!std::isfinite(mm))
        return;
    m_cornerRadiusMm = qBound(0.0, mm, 200.0);
}

void ShapeObject::setSides(int sides)
{
    m_sides = qBound(kMinSides, sides, kMaxSides);
}

void ShapeObject::setStarInnerRatio(double ratio)
{
    if (!std::isfinite(ratio))
        return;
    m_starInnerRatio = qBound(0.05, ratio, 0.95);
}

QPainterPath ShapeObject::path() const
{
    const QRectF box(0.0, 0.0, widthMm(), heightMm());
    const QPointF c = box.center();
    const double rx = box.width() / 2.0;
    const double ry = box.height() / 2.0;
    QPainterPath p;

    switch (m_kind) {
    case ShapeKind::Rectangle:
        p.addRect(box);
        break;

    case ShapeKind::RoundedRectangle: {
        const double r = qMin(m_cornerRadiusMm, qMin(box.width(), box.height()) / 2.0);
        p.addRoundedRect(box, r, r);
        break;
    }

    case ShapeKind::Ellipse:
        p.addEllipse(box);
        break;

    case ShapeKind::Line:
        // Horizontal segment across the middle; rotation sets the angle.
        p.moveTo(box.left(), c.y());
        p.lineTo(box.right(), c.y());
        break;

    case ShapeKind::Triangle:
    case ShapeKind::Polygon: {
        const int n = (m_kind == ShapeKind::Triangle) ? 3 : m_sides;
        for (int i = 0; i < n; ++i) {
            // Start at the top so a polygon looks upright.
            const double angle = -M_PI / 2.0 + (2.0 * M_PI * i) / double(n);
            const QPointF pt(c.x() + rx * std::cos(angle), c.y() + ry * std::sin(angle));
            if (i == 0)
                p.moveTo(pt);
            else
                p.lineTo(pt);
        }
        p.closeSubpath();
        break;
    }

    case ShapeKind::Star: {
        const int points = qMax(3, m_sides);
        const int vertices = points * 2;
        for (int i = 0; i < vertices; ++i) {
            const bool outer = (i % 2 == 0);
            const double factor = outer ? 1.0 : m_starInnerRatio;
            const double angle = -M_PI / 2.0 + (M_PI * i) / double(points);
            const QPointF pt(c.x() + rx * factor * std::cos(angle),
                             c.y() + ry * factor * std::sin(angle));
            if (i == 0)
                p.moveTo(pt);
            else
                p.lineTo(pt);
        }
        p.closeSubpath();
        break;
    }

    case ShapeKind::Arrow: {
        // A block arrow pointing right; the shaft is 50 % of the height.
        const double shaftTop = box.top() + box.height() * 0.25;
        const double shaftBottom = box.bottom() - box.height() * 0.25;
        const double headX = box.left() + box.width() * 0.6;
        p.moveTo(box.left(), shaftTop);
        p.lineTo(headX, shaftTop);
        p.lineTo(headX, box.top());
        p.lineTo(box.right(), c.y());
        p.lineTo(headX, box.bottom());
        p.lineTo(headX, shaftBottom);
        p.lineTo(box.left(), shaftBottom);
        p.closeSubpath();
        break;
    }
    }
    return p;
}


void ShapeObject::paintObject(QPainter &painter, const RenderContext &ctx) const
{
    Q_UNUSED(ctx);
    const QPainterPath p = path();
    if (p.isEmpty())
        return;

    const bool isLine = (m_kind == ShapeKind::Line);

    // A shape with neither fill nor stroke would be invisible; keep a thin
    // stroke in that case so the object can never be lost on the canvas.
    QColor fill = m_fill;
    QColor stroke = m_stroke;
    double strokeWidth = m_strokeWidthMm;
    if (!isLine && fill.alpha() == 0 && (stroke.alpha() == 0 || strokeWidth <= 0.0)) {
        stroke = QColor(120, 120, 120, 200);
        strokeWidth = 0.2;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (!isLine && fill.alpha() > 0)
        painter.setBrush(fill);
    else
        painter.setBrush(Qt::NoBrush);

    if (stroke.alpha() > 0 && strokeWidth > 0.0) {
        QPen pen(stroke);
        pen.setWidthF(strokeWidth);
        pen.setJoinStyle(Qt::MiterJoin);
        pen.setCapStyle(Qt::FlatCap);
        painter.setPen(pen);
    } else {
        painter.setPen(Qt::NoPen);
    }

    painter.drawPath(p);
    painter.restore();
}

QJsonObject ShapeObject::propertiesToJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("kind"), names::shapeKind(m_kind));
    o.insert(QStringLiteral("fill"), m_fill.name(QColor::HexArgb));
    o.insert(QStringLiteral("stroke"), m_stroke.name(QColor::HexArgb));
    o.insert(QStringLiteral("strokeWidthMm"), m_strokeWidthMm);
    o.insert(QStringLiteral("cornerRadiusMm"), m_cornerRadiusMm);
    o.insert(QStringLiteral("sides"), m_sides);
    o.insert(QStringLiteral("starInnerRatio"), m_starInnerRatio);
    return o;
}

bool ShapeObject::propertiesFromJson(const QJsonObject &json, QString *error)
{
    Q_UNUSED(error);
    ShapeKind kind = m_kind;
    if (names::shapeKindFromString(json.value(QStringLiteral("kind")).toString(), &kind))
        m_kind = kind;
    m_fill = colorFromJson(json.value(QStringLiteral("fill")), m_fill);
    m_stroke = colorFromJson(json.value(QStringLiteral("stroke")), m_stroke);
    setStrokeWidthMm(json.value(QStringLiteral("strokeWidthMm")).toDouble(m_strokeWidthMm));
    setCornerRadiusMm(json.value(QStringLiteral("cornerRadiusMm")).toDouble(m_cornerRadiusMm));
    setSides(json.value(QStringLiteral("sides")).toInt(m_sides));
    setStarInnerRatio(json.value(QStringLiteral("starInnerRatio")).toDouble(m_starInnerRatio));
    return true;
}

QString ShapeObject::defaultName() const
{
    switch (m_kind) {
    case ShapeKind::Rectangle:        return QCoreApplication::translate("ShapeObject", "Rectangle");
    case ShapeKind::RoundedRectangle: return QCoreApplication::translate("ShapeObject", "Rounded rectangle");
    case ShapeKind::Ellipse:          return QCoreApplication::translate("ShapeObject", "Ellipse");
    case ShapeKind::Line:             return QCoreApplication::translate("ShapeObject", "Line");
    case ShapeKind::Triangle:         return QCoreApplication::translate("ShapeObject", "Triangle");
    case ShapeKind::Polygon:          return QCoreApplication::translate("ShapeObject", "Polygon");
    case ShapeKind::Star:             return QCoreApplication::translate("ShapeObject", "Star");
    case ShapeKind::Arrow:            return QCoreApplication::translate("ShapeObject", "Arrow");
    }
    return QCoreApplication::translate("ShapeObject", "Shape");
}

CardObjectPtr ShapeObject::cloneImpl() const
{
    auto copy = std::make_unique<ShapeObject>();
    copy->m_kind = m_kind;
    copy->m_fill = m_fill;
    copy->m_stroke = m_stroke;
    copy->m_strokeWidthMm = m_strokeWidthMm;
    copy->m_cornerRadiusMm = m_cornerRadiusMm;
    copy->m_sides = m_sides;
    copy->m_starInnerRatio = m_starInnerRatio;
    return copy;
}

} // namespace occ
