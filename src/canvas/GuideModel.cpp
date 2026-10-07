#include "canvas/GuideModel.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonValue>
#include <QUuid>

#include <cmath>

namespace occ {

GuideModel::GuideModel(QObject *parent) : QObject(parent) {}

GuideModel::~GuideModel() = default;

QVector<GuideModel::Guide> &GuideModel::listFor(CardSideId side)
{
    return side == CardSideId::Back ? m_back : m_front;
}

const QVector<GuideModel::Guide> &GuideModel::listFor(CardSideId side) const
{
    return side == CardSideId::Back ? m_back : m_front;
}

ObjectId GuideModel::add(CardSideId side, bool horizontal, double posMm)
{
    Guide guide;
    guide.id = QUuid::createUuid();
    guide.horizontal = horizontal;
    guide.posMm = std::isfinite(posMm) ? posMm : 0.0;
    listFor(side).append(guide);
    emit changed(side);
    return guide.id;
}

bool GuideModel::remove(CardSideId side, const ObjectId &id)
{
    QVector<Guide> &list = listFor(side);
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).id == id) {
            list.removeAt(i);
            emit changed(side);
            return true;
        }
    }
    return false;
}

bool GuideModel::move(CardSideId side, const ObjectId &id, double newPosMm)
{
    if (!std::isfinite(newPosMm))
        return false;
    QVector<Guide> &list = listFor(side);
    for (Guide &guide : list) {
        if (guide.id != id)
            continue;
        if (qFuzzyCompare(guide.posMm + 1.0, newPosMm + 1.0))
            return false;
        guide.posMm = newPosMm;
        emit changed(side);
        return true;
    }
    return false;
}

void GuideModel::clear(CardSideId side)
{
    QVector<Guide> &list = listFor(side);
    if (list.isEmpty())
        return;
    list.clear();
    emit changed(side);
}

void GuideModel::clearAll()
{
    const bool hadFront = !m_front.isEmpty();
    const bool hadBack = !m_back.isEmpty();
    m_front.clear();
    m_back.clear();
    if (hadFront)
        emit changed(CardSideId::Front);
    if (hadBack)
        emit changed(CardSideId::Back);
}

QVector<GuideModel::Guide> GuideModel::guides(CardSideId side) const
{
    return listFor(side);
}

QVector<GuideModel::Guide> GuideModel::guides(CardSideId side, bool horizontal) const
{
    QVector<Guide> out;
    for (const Guide &guide : listFor(side)) {
        if (guide.horizontal == horizontal)
            out.append(guide);
    }
    return out;
}

bool GuideModel::position(CardSideId side, const ObjectId &id, double *posMm) const
{
    for (const Guide &guide : listFor(side)) {
        if (guide.id != id)
            continue;
        if (posMm)
            *posMm = guide.posMm;
        return true;
    }
    return false;
}

bool GuideModel::contains(CardSideId side, const ObjectId &id) const
{
    for (const Guide &guide : listFor(side)) {
        if (guide.id == id)
            return true;
    }
    return false;
}

int GuideModel::count(CardSideId side) const
{
    return int(listFor(side).size());
}

ObjectId GuideModel::guideAt(CardSideId side, bool horizontal, double posMm,
                             double toleranceMm, double *actualPosMm) const
{
    const double limit = toleranceMm >= 0.0 ? toleranceMm : 0.0;
    ObjectId best;
    double bestDistance = limit + 1.0;
    for (const Guide &guide : listFor(side)) {
        if (guide.horizontal != horizontal)
            continue;
        const double distance = qAbs(guide.posMm - posMm);
        if (distance > limit || distance >= bestDistance)
            continue;
        best = guide.id;
        bestDistance = distance;
        if (actualPosMm)
            *actualPosMm = guide.posMm;
    }
    return best;
}

QJsonObject GuideModel::toJson(CardSideId side) const
{
    QJsonArray array;
    for (const Guide &guide : listFor(side)) {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), guide.id.toString(QUuid::WithoutBraces));
        entry.insert(QStringLiteral("horizontal"), guide.horizontal);
        entry.insert(QStringLiteral("posMm"), guide.posMm);
        array.append(entry);
    }
    QJsonObject o;
    o.insert(QStringLiteral("guides"), array);
    return o;
}

bool GuideModel::fromJson(CardSideId side, const QJsonObject &json)
{
    const QJsonValue value = json.value(QStringLiteral("guides"));
    if (value.isUndefined() || value.isNull()) {
        clear(side);
        return true;
    }
    if (!value.isArray())
        return false;

    QVector<Guide> loaded;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        if (!entry.isObject())
            continue;
        const QJsonObject object = entry.toObject();
        Guide guide;
        const QString idText = object.value(QStringLiteral("id")).toString();
        if (!idText.isEmpty()) {
            const QUuid parsed(idText);
            if (!parsed.isNull())
                guide.id = parsed;
        }
        if (guide.id.isNull())
            guide.id = QUuid::createUuid();
        guide.horizontal = object.value(QStringLiteral("horizontal")).toBool(true);
        const double pos = object.value(QStringLiteral("posMm")).toDouble(0.0);
        guide.posMm = std::isfinite(pos) ? pos : 0.0;
        loaded.append(guide);
    }

    listFor(side).swap(loaded);
    emit changed(side);
    return true;
}

QJsonObject GuideModel::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("front"), toJson(CardSideId::Front));
    o.insert(QStringLiteral("back"), toJson(CardSideId::Back));
    return o;
}

bool GuideModel::fromJson(const QJsonObject &json)
{
    bool ok = fromJson(CardSideId::Front,
                       json.value(QStringLiteral("front")).toObject());
    ok = fromJson(CardSideId::Back, json.value(QStringLiteral("back")).toObject()) && ok;
    return ok;
}

} // namespace occ
