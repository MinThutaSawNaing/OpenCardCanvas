#include "core/CardObject.h"

#include "rendering/RenderContext.h"

#include <QCoreApplication>
#include <QJsonValue>
#include <QTransform>
#include <QtMath>

#include <cmath>

namespace occ {

namespace {

// A zero sized object can never be selected or resized again, which would
// silently lose work, so sizes are clamped.
constexpr double kMinObjectMm = 0.2;
constexpr double kMaxObjectMm = 1000.0;

double round3(double v)
{
    return qRound(v * 1000.0) / 1000.0;
}

} // namespace

CardObject::CardObject(ObjectType type, const ObjectId &id)
    : m_type(type), m_id(id.isNull() ? ObjectId::createUuid() : id)
{
}

CardObject::~CardObject() = default;

void CardObject::regenerateId()
{
    m_id = ObjectId::createUuid();
}

double CardObject::saneSize(double mm)
{
    if (!std::isfinite(mm))
        return kMinObjectMm;
    return qBound(kMinObjectMm, mm, kMaxObjectMm);
}

void CardObject::setRectMm(const QRectF &rect)
{
    const double w = saneSize(rect.width());
    const double h = saneSize(rect.height());
    const double x = std::isfinite(rect.x()) ? rect.x() : 0.0;
    const double y = std::isfinite(rect.y()) ? rect.y() : 0.0;
    m_rectMm = QRectF(round3(x), round3(y), round3(w), round3(h));
}

void CardObject::setXMm(double x)
{
    m_rectMm.moveLeft(std::isfinite(x) ? round3(x) : 0.0);
}

void CardObject::setYMm(double y)
{
    m_rectMm.moveTop(std::isfinite(y) ? round3(y) : 0.0);
}

void CardObject::setWidthMm(double w)  { m_rectMm.setWidth(saneSize(w)); }
void CardObject::setHeightMm(double h) { m_rectMm.setHeight(saneSize(h)); }

void CardObject::setCenterMm(const QPointF &center)
{
    m_rectMm.moveCenter(QPointF(round3(center.x()), round3(center.y())));
}

void CardObject::moveByMm(double dx, double dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy))
        return;
    m_rectMm.translate(dx, dy);
    m_rectMm = QRectF(round3(m_rectMm.x()), round3(m_rectMm.y()),
                      round3(m_rectMm.width()), round3(m_rectMm.height()));
}

void CardObject::setRotationDeg(double degrees)
{
    if (!std::isfinite(degrees))
        return;
    double d = std::fmod(degrees, 360.0);
    if (d < 0.0)
        d += 360.0;
    m_rotationDeg = round3(d);
}

void CardObject::setOpacity(double opacity)
{
    m_opacity = qBound(0.0, opacity, 1.0);
}

QPolygonF CardObject::outlineMm() const
{
    const QRectF r = m_rectMm;
    const QPolygonF local({ r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft() });
    if (qFuzzyIsNull(m_rotationDeg))
        return local;
    QTransform t;
    t.translate(r.center().x(), r.center().y());
    t.rotate(m_rotationDeg);
    t.translate(-r.center().x(), -r.center().y());
    return t.map(local);
}

QRectF CardObject::boundingRectMm() const
{
    if (qFuzzyIsNull(m_rotationDeg))
        return m_rectMm;
    return outlineMm().boundingRect();
}

bool CardObject::containsMm(const QPointF &pointMm) const
{
    QPointF p = pointMm;
    if (!qFuzzyIsNull(m_rotationDeg)) {
        QTransform t;
        t.translate(m_rectMm.center().x(), m_rectMm.center().y());
        t.rotate(-m_rotationDeg);
        t.translate(-m_rectMm.center().x(), -m_rectMm.center().y());
        p = t.map(p);
    }
    return m_rectMm.contains(p);
}

bool CardObject::intersectsMm(const QRectF &rectMm) const
{
    return boundingRectMm().intersects(rectMm);
}

void CardObject::paint(QPainter &painter, const RenderContext &ctx) const
{
    if (!m_visible || m_opacity <= 0.0)
        return;

    painter.save();
    painter.setOpacity(painter.opacity() * m_opacity);
    if (!qFuzzyIsNull(m_rotationDeg)) {
        painter.translate(m_rectMm.center());
        painter.rotate(m_rotationDeg);
        painter.translate(-m_rectMm.center());
    }
    // Local origin is the object's top-left corner, still in millimetres.
    painter.translate(m_rectMm.topLeft());
    paintObject(painter, ctx);
    painter.restore();
}


QJsonObject CardObject::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), names::objectType(m_type));
    o.insert(QStringLiteral("id"), m_id.toString(QUuid::WithoutBraces));
    o.insert(QStringLiteral("name"), m_name);

    QJsonObject geom;
    geom.insert(QStringLiteral("x"), m_rectMm.x());
    geom.insert(QStringLiteral("y"), m_rectMm.y());
    geom.insert(QStringLiteral("w"), m_rectMm.width());
    geom.insert(QStringLiteral("h"), m_rectMm.height());
    o.insert(QStringLiteral("rectMm"), geom);

    o.insert(QStringLiteral("rotation"), m_rotationDeg);
    o.insert(QStringLiteral("opacity"), m_opacity);
    o.insert(QStringLiteral("visible"), m_visible);
    o.insert(QStringLiteral("locked"), m_locked);
    o.insert(QStringLiteral("z"), m_zOrder);
    o.insert(QStringLiteral("properties"), propertiesToJson());
    return o;
}

bool CardObject::fromJson(const QJsonObject &json, QString *error)
{
    const auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    const auto tr = [](const char *s, const QString &a1 = QString(),
                       const QString &a2 = QString()) {
        return QCoreApplication::translate("CardObject", s).arg(a1, a2);
    };

    // The stored type must agree with the concrete class the reader created.
    const QString typeText = json.value(QStringLiteral("type")).toString();
    ObjectType parsedType = m_type;
    if (!typeText.isEmpty() && !names::objectTypeFromString(typeText, &parsedType))
        return fail(tr("Unknown object type \"%1\".", typeText));
    if (!typeText.isEmpty() && parsedType != m_type) {
        return fail(tr("Object type mismatch: expected \"%1\" but the file contains \"%2\".",
                       names::objectType(m_type), names::objectType(parsedType)));
    }

    const QString idText = json.value(QStringLiteral("id")).toString();
    if (!idText.isEmpty()) {
        const QUuid parsed(idText);
        if (parsed.isNull())
            return fail(tr("An object has an invalid identifier."));
        m_id = parsed;
    }

    m_name = json.value(QStringLiteral("name")).toString(m_name);

    const QJsonValue rectValue = json.value(QStringLiteral("rectMm"));
    if (!rectValue.isObject())
        return fail(tr("An object is missing its position and size."));
    const QJsonObject geom = rectValue.toObject();
    const auto number = [&geom](const char *key, bool *numOk) -> double {
        const QJsonValue v = geom.value(QLatin1String(key));
        if (!v.isDouble()) {
            *numOk = false;
            return 0.0;
        }
        *numOk = true;
        return v.toDouble();
    };
    bool ok = true;
    const double x = number("x", &ok);
    if (!ok) return fail(tr("An object has an invalid X position."));
    const double y = number("y", &ok);
    if (!ok) return fail(tr("An object has an invalid Y position."));
    const double w = number("w", &ok);
    if (!ok) return fail(tr("An object has an invalid width."));
    const double h = number("h", &ok);
    if (!ok) return fail(tr("An object has an invalid height."));
    if (w <= 0.0 || h <= 0.0)
        return fail(tr("An object has a zero or negative size."));
    setRectMm(QRectF(x, y, w, h));

    setRotationDeg(json.value(QStringLiteral("rotation")).toDouble(m_rotationDeg));
    setOpacity(json.value(QStringLiteral("opacity")).toDouble(m_opacity));
    m_visible = json.value(QStringLiteral("visible")).toBool(m_visible);
    m_locked  = json.value(QStringLiteral("locked")).toBool(m_locked);
    m_zOrder  = json.value(QStringLiteral("z")).toInt(m_zOrder);

    const QJsonValue props = json.value(QStringLiteral("properties"));
    if (!props.isObject()) {
        return fail(tr("An object of type \"%1\" is missing its properties.",
                       names::objectType(m_type)));
    }
    return propertiesFromJson(props.toObject(), error);
}

CardObjectPtr CardObject::clone() const
{
    CardObjectPtr copy = cloneImpl();
    if (copy)
        copyBaseTo(*copy);
    return copy;
}

void CardObject::copyBaseTo(CardObject &target) const
{
    target.m_id          = m_id;
    target.m_name        = m_name;
    target.m_rectMm      = m_rectMm;
    target.m_rotationDeg = m_rotationDeg;
    target.m_opacity     = m_opacity;
    target.m_visible     = m_visible;
    target.m_locked      = m_locked;
    target.m_zOrder      = m_zOrder;
    // m_type is fixed by the concrete class.
}

QString CardObject::typeDisplayName() const
{
    switch (m_type) {
    case ObjectType::Text:    return QCoreApplication::translate("CardObject", "Text");
    case ObjectType::Image:   return QCoreApplication::translate("CardObject", "Image");
    case ObjectType::Photo:   return QCoreApplication::translate("CardObject", "Photo");
    case ObjectType::Shape:   return QCoreApplication::translate("CardObject", "Shape");
    case ObjectType::QrCode:  return QCoreApplication::translate("CardObject", "QR Code");
    case ObjectType::Barcode: return QCoreApplication::translate("CardObject", "Barcode");
    case ObjectType::Group:   return QCoreApplication::translate("CardObject", "Group");
    }
    return QCoreApplication::translate("CardObject", "Object");
}

} // namespace occ
