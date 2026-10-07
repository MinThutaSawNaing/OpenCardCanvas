#include "core/CardSide.h"

#include "core/CardObject.h"
#include "core/GroupObject.h"
#include "personalization/TemplateEngine.h"
#include "rendering/RenderContext.h"

#include <QCoreApplication>
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QPointF>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace occ {

namespace {

QString sideTr(const char *text)
{
    return QCoreApplication::translate("CardSide", text);
}

constexpr std::pair<const char *, CardSide::Background::Kind> kBackgroundKinds[] = {
    { "none",   CardSide::Background::Kind::None },
    { "solid",  CardSide::Background::Kind::Solid },
    { "linear", CardSide::Background::Kind::LinearGradient },
    { "radial", CardSide::Background::Kind::RadialGradient },
    { "image",  CardSide::Background::Kind::Image },
};

QString backgroundKindName(CardSide::Background::Kind kind)
{
    return names::toName(kind, kBackgroundKinds, QStringLiteral("solid"));
}

bool backgroundKindFromString(const QString &text, CardSide::Background::Kind *out)
{
    return names::fromName(text, out, kBackgroundKinds);
}

QColor colorFromJson(const QJsonValue &value, const QColor &fallback)
{
    if (!value.isString())
        return fallback;
    const QColor parsed(value.toString());
    return parsed.isValid() ? parsed : fallback;
}

// Upper bound on the objects a single side may contain. It exists so that a
// damaged or hostile project cannot turn into an out of memory condition while
// it is being read; no real card comes close to it.
constexpr int kMaxObjectsPerSide = 10000;

QString colorToJson(const QColor &color)
{
    return color.name(QColor::HexArgb);
}

} // namespace

// ---------------------------------------------------------------------------
// Background
// ---------------------------------------------------------------------------

void CardSide::Background::reset()
{
    kind = Kind::Solid;
    color = QColor(Qt::white);
    color2 = QColor(Qt::white);
    angleDeg = 0.0;
    assetId.clear();
    stretchImage = true;
}

QJsonObject CardSide::Background::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("kind"), backgroundKindName(kind));
    o.insert(QStringLiteral("color"), colorToJson(color));
    o.insert(QStringLiteral("color2"), colorToJson(color2));
    o.insert(QStringLiteral("angleDeg"), angleDeg);
    o.insert(QStringLiteral("assetId"), assetId);
    o.insert(QStringLiteral("stretchImage"), stretchImage);
    return o;
}

bool CardSide::Background::fromJson(const QJsonObject &json)
{
    if (!json.isEmpty() && !json.contains(QStringLiteral("kind"))) {
        // A background block without a kind cannot be interpreted; this is a
        // hard error so a corrupt project is reported rather than silently
        // rendered with the wrong background.
        return false;
    }

    Background::Kind parsedKind = kind;
    const QJsonValue kindValue = json.value(QStringLiteral("kind"));
    if (kindValue.isString()) {
        if (!backgroundKindFromString(kindValue.toString(), &parsedKind)) {
            // Unknown kinds fall back to the documented default.
            parsedKind = Kind::Solid;
        }
    }
    kind = parsedKind;

    color = colorFromJson(json.value(QStringLiteral("color")), color);
    color2 = colorFromJson(json.value(QStringLiteral("color2")), color2);

    const QJsonValue angle = json.value(QStringLiteral("angleDeg"));
    if (angle.isDouble()) {
        const double value = angle.toDouble();
        angleDeg = std::isfinite(value) ? std::fmod(std::fmod(value, 360.0) + 360.0, 360.0) : 0.0;
    }

    assetId = json.value(QStringLiteral("assetId")).toString(assetId);
    stretchImage = json.value(QStringLiteral("stretchImage")).toBool(stretchImage);
    return true;
}

// ---------------------------------------------------------------------------
// CardSide
// ---------------------------------------------------------------------------

CardSide::CardSide(CardSideId id) : m_id(id)
{
}

CardSide::~CardSide() = default;

const QVector<CardObject *> CardSide::objects() const
{
    QVector<CardObject *> out;
    out.reserve(int(m_objects.size()));
    for (const CardObjectPtr &object : m_objects) {
        if (object)
            out.append(object.get());
    }
    return out;
}

CardObject *CardSide::object(const ObjectId &id) const
{
    for (const CardObjectPtr &object : m_objects) {
        if (object && object->id() == id)
            return object.get();
    }
    return nullptr;
}

int CardSide::indexOf(const ObjectId &id) const
{
    for (int i = 0; i < m_objects.size(); ++i) {
        if (m_objects.at(i) && m_objects.at(i)->id() == id)
            return i;
    }
    return -1;
}

void CardSide::insertObject(CardObjectPtr object, int index)
{
    if (!object)
        return;

    // An id that already exists would make every lookup ambiguous, so it is
    // replaced rather than duplicated. This happens when an object is pasted
    // back over itself.
    const int existing = indexOf(object->id());
    if (existing >= 0)
        m_objects.erase(m_objects.begin() + existing);

    int target = index;
    if (target < 0 || target > int(m_objects.size()))
        target = int(m_objects.size());
    m_objects.insert(m_objects.begin() + target, std::move(object));
    renumber();
}

CardObjectPtr CardSide::takeObject(const ObjectId &id)
{
    const int index = indexOf(id);
    if (index < 0)
        return nullptr;
    CardObjectPtr taken = std::move(m_objects.at(std::size_t(index)));
    m_objects.erase(m_objects.begin() + index);
    renumber();
    return taken;
}

std::vector<CardObjectPtr> CardSide::takeAll()
{
    std::vector<CardObjectPtr> out;
    out.swap(m_objects);
    return out;
}

bool CardSide::moveTo(const ObjectId &id, int index)
{
    const int from = indexOf(id);
    if (from < 0)
        return false;

    const int last = int(m_objects.size()) - 1;
    const int target = qBound(0, index, last);
    if (target == from)
        return false;

    CardObjectPtr moved = std::move(m_objects.at(std::size_t(from)));
    m_objects.erase(m_objects.begin() + from);
    m_objects.insert(m_objects.begin() + target, std::move(moved));
    renumber();
    return true;
}

bool CardSide::moveToFront(const ObjectId &id)
{
    return moveTo(id, int(m_objects.size()) - 1);
}

bool CardSide::moveForward(const ObjectId &id)
{
    const int from = indexOf(id);
    if (from < 0)
        return false;
    return moveTo(id, from + 1);
}

bool CardSide::moveBackward(const ObjectId &id)
{
    const int from = indexOf(id);
    if (from < 0)
        return false;
    return moveTo(id, from - 1);
}

bool CardSide::moveToBack(const ObjectId &id)
{
    return moveTo(id, 0);
}

bool CardSide::applyOrder(const QVector<ObjectId> &order)
{
    if (order.size() != int(m_objects.size()))
        return false;

    std::vector<CardObjectPtr> reordered;
    reordered.reserve(order.size());
    std::vector<bool> used(m_objects.size(), false);

    for (const ObjectId &id : order) {
        bool found = false;
        for (std::size_t i = 0; i < m_objects.size(); ++i) {
            if (!used.at(i) && m_objects.at(i) && m_objects.at(i)->id() == id) {
                used[i] = true;
                reordered.push_back(std::move(m_objects[i]));
                found = true;
                break;
            }
        }
        if (!found) {
            // An unknown or repeated id: put everything back exactly as it was
            // and report failure, so a command can never half-apply an order.
            for (std::size_t i = 0; i < m_objects.size(); ++i) {
                if (!used.at(i) && m_objects.at(i))
                    reordered.push_back(std::move(m_objects[i]));
            }
            m_objects.swap(reordered);
            return false;
        }
    }

    m_objects.swap(reordered);
    renumber();
    return true;
}

QVector<ObjectId> CardSide::objectIds() const
{
    QVector<ObjectId> ids;
    ids.reserve(int(m_objects.size()));
    for (const CardObjectPtr &object : m_objects) {
        if (object)
            ids.append(object->id());
    }
    return ids;
}

CardObject *CardSide::hitTest(const QPointF &pointMm, bool includeLocked,
                              bool includeHidden) const
{
    // Top-most first: the last object in paint order is the one the user sees
    // on top, and therefore the one a click must select.
    for (int i = int(m_objects.size()) - 1; i >= 0; --i) {
        CardObject *object = m_objects.at(i).get();
        if (!object)
            continue;
        if (!includeHidden && !object->isVisible())
            continue;
        if (!includeLocked && object->isLocked())
            continue;
        if (object->containsMm(pointMm))
            return object;
    }
    return nullptr;
}

QVector<CardObject *> CardSide::objectsIn(const QRectF &rectMm, bool includeLocked,
                                          bool includeHidden) const
{
    QVector<CardObject *> out;
    for (const CardObjectPtr &object : m_objects) {
        if (!object)
            continue;
        if (!includeHidden && !object->isVisible())
            continue;
        if (!includeLocked && object->isLocked())
            continue;
        if (object->intersectsMm(rectMm))
            out.append(object.get());
    }
    return out;
}

QRectF CardSide::contentBoundsMm() const
{
    QRectF bounds;
    bool first = true;
    for (const CardObjectPtr &object : m_objects) {
        if (!object || !object->isVisible())
            continue;
        const QRectF r = object->boundingRectMm();
        bounds = first ? r : bounds.united(r);
        first = false;
    }
    if (first)
        return QRectF();
    return bounds;
}

bool CardSide::hasPlaceholders() const
{
    for (const CardObjectPtr &object : m_objects) {
        if (!object)
            continue;
        const QByteArray text =
            QJsonDocument(object->toJson()).toJson(QJsonDocument::Compact);
        if (TemplateEngine::containsPlaceholders(QString::fromUtf8(text)))
            return true;
    }
    return false;
}

QJsonObject CardSide::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("background"), m_background.toJson());

    QJsonArray array;
    for (const CardObjectPtr &object : m_objects) {
        if (object)
            array.append(object->toJson());
    }
    o.insert(QStringLiteral("objects"), array);
    return o;
}

bool CardSide::fromJson(const QJsonObject &json, const ObjectReader &createObject,
                        QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    // A missing block means "an empty side", which is legal: a project may
    // define only the front, and the back is then simply blank.
    if (json.isEmpty()) {
        if (error)
            error->clear();
        return true;
    }

    Background background;
    const QJsonValue backgroundValue = json.value(QStringLiteral("background"));
    if (backgroundValue.isObject()) {
        if (!background.fromJson(backgroundValue.toObject()))
            return fail(sideTr("The background of the %1 side could not be read.")
                            .arg(names::side(m_id)));
    }

    std::vector<CardObjectPtr> owning;
    const QJsonValue objectsValue = json.value(QStringLiteral("objects"));
    if (objectsValue.isArray()) {
        const QJsonArray array = objectsValue.toArray();
        if (array.size() > kMaxObjectsPerSide) {
            return fail(sideTr("The %1 side contains %2 objects, which is more than "
                               "OpenCardCanvas can open.")
                            .arg(names::side(m_id))
                            .arg(array.size()));
        }
        owning.reserve(std::size_t(array.size()));
        for (int i = 0; i < array.size(); ++i) {
            const QJsonValue value = array.at(i);
            if (!value.isObject()) {
                return fail(sideTr("Object %1 on the %2 side is not valid.")
                                .arg(i + 1)
                                .arg(names::side(m_id)));
            }
            QString objectError;
            CardObjectPtr object = createObject(value.toObject(), &objectError);
            if (!object) {
                return fail(sideTr("Object %1 on the %2 side could not be read: %3")
                                .arg(i + 1)
                                .arg(names::side(m_id))
                                .arg(objectError));
            }
            owning.push_back(std::move(object));
        }
    } else if (!objectsValue.isUndefined() && !objectsValue.isNull()) {
        return fail(sideTr("The object list on the %1 side is not a list.")
                        .arg(names::side(m_id)));
    }

    // Commit only once the whole side has loaded, so a damaged file cannot
    // leave the document in a half-filled state.
    m_background = background;
    m_objects.swap(owning);
    renumber();

    if (error)
        error->clear();
    return true;
}

void CardSide::setLocked(bool locked)
{
    for (const CardObjectPtr &object : m_objects) {
        if (!object)
            continue;
        object->setLocked(locked);
        if (auto *group = dynamic_cast<GroupObject *>(object.get())) {
            for (CardObject *child : group->children()) {
                if (child)
                    child->setLocked(locked);
            }
        }
    }
}

void CardSide::setVisible(bool visible)
{
    for (const CardObjectPtr &object : m_objects) {
        if (!object)
            continue;
        object->setVisible(visible);
        if (auto *group = dynamic_cast<GroupObject *>(object.get())) {
            for (CardObject *child : group->children()) {
                if (child)
                    child->setVisible(visible);
            }
        }
    }
}

void CardSide::renumber()
{
    for (int i = 0; i < m_objects.size(); ++i) {
        if (m_objects.at(i))
            m_objects.at(i)->setZOrder(i);
    }
}

} // namespace occ
