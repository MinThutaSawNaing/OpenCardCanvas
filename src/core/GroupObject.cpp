#include "core/GroupObject.h"

#include "core/ObjectFactory.h"
#include "rendering/RenderContext.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QTransform>

#include <cmath>

namespace occ {

namespace {

QString groupTr(const char *text)
{
    return QCoreApplication::translate("GroupObject", text);
}

double round3(double v)
{
    return qRound(v * 1000.0) / 1000.0;
}

QRectF roundedRect(const QRectF &r)
{
    return QRectF(round3(r.x()), round3(r.y()), round3(r.width()), round3(r.height()));
}

// When paintObject() runs, the base class has already applied
//     v -> R(theta, centre) * (v + topLeft)
// to the painter, so the frame-to-card transform is that expression written
// out; cardToFrame() is its exact inverse. Because the two are inverses, the
// children are drawn at their own card coordinates no matter what the group's
// rectangle says - which is what makes grouping and ungrouping lossless.
QTransform frameToCardTransform(const QRectF &rect, double rotationDeg)
{
    QTransform t;
    const QPointF c = rect.center();
    t.translate(c.x(), c.y());
    t.rotate(rotationDeg);
    t.translate(-c.x(), -c.y());
    t.translate(rect.x(), rect.y());
    return t;
}

QTransform cardToFrameTransform(const QRectF &rect, double rotationDeg)
{
    QTransform t;
    const QPointF c = rect.center();
    t.translate(-rect.x(), -rect.y());
    t.translate(c.x(), c.y());
    t.rotate(-rotationDeg);
    t.translate(-c.x(), -c.y());
    return t;
}

// A rectangle turned about its own centre, measured as the tight axis aligned
// box of the result. Used to size a group frame that carries a rotation.
QRectF deRotatedBounds(const QRectF &r, double rotationDeg)
{
    if (qFuzzyIsNull(rotationDeg))
        return r;
    QTransform t;
    const QPointF c = r.center();
    t.translate(c.x(), c.y());
    t.rotate(-rotationDeg);
    t.translate(-c.x(), -c.y());
    return t.mapRect(r);
}

} // namespace

GroupObject::GroupObject() : CardObject(ObjectType::Group, ObjectId::createUuid())
{
    setRectMm(QRectF(0.0, 0.0, 10.0, 10.0));
    m_name = defaultName();
}

const QVector<CardObject *> GroupObject::children() const
{
    QVector<CardObject *> out;
    out.reserve(int(m_children.size()));
    for (const CardObjectPtr &child : m_children) {
        if (child)
            out.append(child.get());
    }
    return out;
}

CardObject *GroupObject::child(const ObjectId &id) const
{
    for (const CardObjectPtr &child : m_children) {
        if (child && child->id() == id)
            return child.get();
    }
    return nullptr;
}

void GroupObject::addChild(CardObjectPtr child)
{
    if (!child)
        return;
    m_children.push_back(std::move(child));
}

CardObjectPtr GroupObject::takeChild(const ObjectId &id)
{
    for (std::size_t i = 0; i < m_children.size(); ++i) {
        if (m_children.at(i) && m_children.at(i)->id() == id) {
            CardObjectPtr taken = std::move(m_children.at(i));
            m_children.erase(m_children.begin() + std::ptrdiff_t(i));
            return taken;
        }
    }
    return nullptr;
}

std::vector<CardObjectPtr> GroupObject::takeAllChildren()
{
    std::vector<CardObjectPtr> out;
    out.swap(m_children);
    return out;
}

void GroupObject::syncBounds()
{
    if (m_children.empty())
        return;

    QRectF unionRect;
    bool first = true;
    for (const CardObjectPtr &child : m_children) {
        if (!child)
            continue;
        const QRectF r = child->boundingRectMm();
        unionRect = first ? r : unionRect.united(r);
        first = false;
    }
    if (first)
        return;

    // The frame is described in the group's own (de-rotated) space. Measuring
    // the union there gives a frame that wraps the children tightly whether the
    // group is rotated or not, and because the frame transform is the exact
    // inverse of the paint transform, changing the frame never moves a child.
    const QPointF centre = unionRect.center();
    const QRectF measured = deRotatedBounds(unionRect, rotationDeg());
    setRectMm(QRectF(round3(centre.x() - measured.width() / 2.0),
                     round3(centre.y() - measured.height() / 2.0),
                     round3(measured.width()), round3(measured.height())));
}

void GroupObject::scaleChildrenFrom(const QRectF &fromRect)
{
    const QRectF toRect = rectMm();
    if (fromRect.width() <= 0.0 || fromRect.height() <= 0.0)
        return;

    const double sx = toRect.width() / fromRect.width();
    const double sy = toRect.height() / fromRect.height();
    if (!std::isfinite(sx) || !std::isfinite(sy) || sx <= 0.0 || sy <= 0.0)
        return;

    const double rotation = rotationDeg();
    const QTransform oldToFrame = cardToFrameTransform(fromRect, rotation);
    const QTransform newToCard = frameToCardTransform(toRect, rotation);

    for (const CardObjectPtr &child : m_children) {
        if (!child)
            continue;

        const QRectF localRect = oldToFrame.mapRect(child->rectMm());
        // Scale about the frame's local origin, which is the fixed point of the
        // resize, then map the result back into card millimetres.
        const QRectF scaledLocal(localRect.x() * sx, localRect.y() * sy,
                                 localRect.width() * sx, localRect.height() * sy);
        const QRectF cardRect = roundedRect(newToCard.mapRect(scaledLocal));

        if (qFuzzyIsNull(child->rotationDeg())) {
            child->setRectMm(cardRect);
        } else {
            // A rotated child cannot follow a non-uniform scale exactly, so its
            // centre follows the resize and its size takes the mean factor;
            // its rotation is preserved rather than rewritten, because
            // changing it would be visible damage the user did not ask for.
            const double mean = (sx + sy) / 2.0;
            const QSizeF size(child->widthMm() * mean, child->heightMm() * mean);
            child->setRectMm(QRectF(cardRect.center().x() - size.width() / 2.0,
                                    cardRect.center().y() - size.height() / 2.0,
                                    size.width(), size.height()));
        }

        // A nested group scales its own children with the same factors.
        if (auto *nested = dynamic_cast<GroupObject *>(child.get()))
            nested->scaleChildrenFrom(fromRect);
    }
}

void GroupObject::paintObject(QPainter &painter, const RenderContext &ctx) const
{
    if (m_children.empty())
        return;

    painter.save();
    // Undo the frame transform exactly, so the children paint at their card
    // coordinates and a group stays a pure container.
    const QRectF rect = rectMm();
    const QPointF c = rect.center();
    painter.translate(-rect.x(), -rect.y());
    painter.translate(c.x(), c.y());
    painter.rotate(-rotationDeg());
    painter.translate(-c.x(), -c.y());

    for (const CardObjectPtr &child : m_children) {
        if (child)
            child->paint(painter, ctx);
    }
    painter.restore();
}

QJsonObject GroupObject::propertiesToJson() const
{
    QJsonArray array;
    for (const CardObjectPtr &child : m_children) {
        if (child)
            array.append(child->toJson());
    }
    QJsonObject o;
    o.insert(QStringLiteral("children"), array);
    return o;
}

bool GroupObject::propertiesFromJson(const QJsonObject &json, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (!json.contains(QStringLiteral("children")))
        return true;    // an empty group is legal
    const QJsonValue childrenValue = json.value(QStringLiteral("children"));
    if (!childrenValue.isArray())
        return fail(groupTr("A group is missing its list of objects."));

    const QJsonArray array = childrenValue.toArray();
    if (array.size() > 5000) {
        return fail(groupTr("A group contains %1 objects, which is more than "
                            "OpenCardCanvas can load.")
                        .arg(array.size()));
    }

    std::vector<CardObjectPtr> loaded;
    loaded.reserve(std::size_t(array.size()));
    for (int i = 0; i < array.size(); ++i) {
        const QJsonValue value = array.at(i);
        if (!value.isObject())
            return fail(groupTr("Object %1 inside a group is not valid.").arg(i + 1));

        QString childError;
        CardObjectPtr child = ObjectFactory::createAndLoad(value.toObject(), &childError);
        if (!child) {
            return fail(groupTr("Object %1 inside a group could not be read: %2")
                            .arg(i + 1)
                            .arg(childError));
        }
        loaded.push_back(std::move(child));
    }

    // The children are replaced only once every one of them has loaded, so a
    // malformed group cannot leave the object half populated.
    m_children.swap(loaded);
    return true;
}

QString GroupObject::defaultName() const
{
    return groupTr("Group");
}

CardObjectPtr GroupObject::cloneImpl() const
{
    auto copy = std::make_unique<GroupObject>();
    for (const CardObjectPtr &child : m_children) {
        if (child)
            copy->addChild(child->clone());
    }
    return copy;
}

} // namespace occ
