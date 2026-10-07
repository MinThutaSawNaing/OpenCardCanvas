#include "core/CardDocument.h"

#include "core/GroupObject.h"
#include "core/ObjectFactory.h"
#include "core/Units.h"
#include "personalization/CsvImporter.h"
#include "personalization/TemplateEngine.h"
#include "project/AssetStore.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <cmath>

namespace occ {

namespace {

QString documentTr(const char *text)
{
    return QCoreApplication::translate("CardDocument", text);
}

// Keeps at least this much of an object inside the card when the card is
// resized or the object is dragged far away. Without it an object could be
// pushed off the visible area for good, which looks exactly like data loss.
constexpr double kMinVisibleMm = 1.0;

void clampObjectsOnSide(CardSide &side, const QRectF &card)
{
    for (CardObject *object : side.objects()) {
        if (!object)
            continue;

        QRectF r = object->rectMm();
        const double w = r.width();
        const double h = r.height();

        const double minX = card.left() + kMinVisibleMm - w;
        const double maxX = card.right() - kMinVisibleMm;
        const double minY = card.top() + kMinVisibleMm - h;
        const double maxY = card.bottom() - kMinVisibleMm;

        double x = qBound(minX, r.x(), maxX);
        double y = qBound(minY, r.y(), maxY);

        // A card smaller than the object leaves qBound with minX > maxX, in
        // which case the object is centred on the card instead.
        if (minX > maxX)
            x = card.center().x() - w / 2.0;
        if (minY > maxY)
            y = card.center().y() - h / 2.0;

        if (!qFuzzyCompare(x, r.x()) || !qFuzzyCompare(y, r.y()))
            object->setRectMm(QRectF(x, y, w, h));
    }
}

QStringList placeholdersInSide(const CardSide &side)
{
    QStringList keys;
    for (CardObject *object : side.objects()) {
        if (!object)
            continue;
        const QByteArray text =
            QJsonDocument(object->toJson()).toJson(QJsonDocument::Compact);
        if (!TemplateEngine::containsPlaceholders(QString::fromUtf8(text)))
            continue;
        for (const QString &key : TemplateEngine::placeholders(QString::fromUtf8(text))) {
            if (!keys.contains(key))
                keys.append(key);
        }
    }
    return keys;
}

} // namespace

CardDocument::CardDocument(QObject *parent)
    : QObject(parent), m_geometry(CardGeometry::isoId1()),
      m_front(CardSideId::Front), m_back(CardSideId::Back),
      m_assets(new AssetStore)
{
    m_created = QDateTime::currentDateTimeUtc();
    m_modified = m_created;
}

CardDocument::~CardDocument()
{
    delete m_assets;
    m_assets = nullptr;
}

void CardDocument::setFilePath(const QString &path)
{
    if (m_filePath == path)
        return;
    m_filePath = path;
    emit filePathChanged(m_filePath);
}

void CardDocument::setDirty(bool dirty)
{
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged(m_dirty);
}

QString CardDocument::title() const
{
    if (m_filePath.isEmpty())
        return documentTr("Untitled");
    const QString base = QFileInfo(m_filePath).completeBaseName();
    return base.isEmpty() ? documentTr("Untitled") : base;
}

void CardDocument::setAuthor(const QString &author)
{
    m_author = author;
}

void CardDocument::setNotes(const QString &notes)
{
    m_notes = notes;
}

void CardDocument::touch()
{
    m_modified = QDateTime::currentDateTimeUtc();
}

void CardDocument::setIsTemplate(bool on)
{
    m_isTemplate = on;
}

void CardDocument::setGeometry(const CardGeometry &geometry)
{
    if (geometry.widthMm() == m_geometry.widthMm()
        && geometry.heightMm() == m_geometry.heightMm()
        && geometry.bleedMm() == m_geometry.bleedMm()
        && geometry.renderDpi() == m_geometry.renderDpi()
        && geometry.presetId() == m_geometry.presetId()) {
        return;
    }

    m_geometry = geometry;
    const QRectF card = geometry.boundsMm();
    clampObjectsOnSide(m_front, card);
    clampObjectsOnSide(m_back, card);

    emit geometryChanged();
    emit sideChanged(CardSideId::Front);
    emit sideChanged(CardSideId::Back);
    notifyChanged();
}

CardSide &CardDocument::side(CardSideId id)
{
    return id == CardSideId::Back ? m_back : m_front;
}

const CardSide &CardDocument::side(CardSideId id) const
{
    return id == CardSideId::Back ? m_back : m_front;
}

void CardDocument::copySideContent(CardSideId from, CardSideId to)
{
    const CardSide &source = side(from);
    CardSide &target = side(to);

    target.takeAll();
    target.setBackground(source.background());
    for (CardObject *object : source.objects()) {
        if (object)
            target.insertObject(object->clone());
    }

    emit sideChanged(to);
    notifyChanged();
}

void CardDocument::clearSide(CardSideId id)
{
    CardSide &target = side(id);
    target.takeAll();
    target.resetBackground();

    emit sideChanged(id);
    emit backgroundChanged(id);
    notifyChanged();
}

AssetStore *CardDocument::assets()
{
    return m_assets;
}

const AssetStore *CardDocument::assets() const
{
    return m_assets;
}

QStringList CardDocument::placeholders() const
{
    QStringList keys = placeholdersInSide(m_front);
    for (const QString &key : placeholdersInSide(m_back)) {
        if (!keys.contains(key))
            keys.append(key);
    }
    keys.sort(Qt::CaseInsensitive);
    return keys;
}

QStringList CardDocument::suggestedPlaceholders() const
{
    QStringList result = placeholders();
    // The predefined field names are offered as well, in the order the CSV
    // dialog presents them, so a designer who has not yet typed a placeholder
    // still sees the standard HR column names to choose from.
    for (const QString &field : CsvImporter::predefinedFields()) {
        if (!result.contains(field, Qt::CaseInsensitive))
            result.append(field);
    }
    return result;
}

void CardDocument::newDocument()
{
    m_geometry = CardGeometry::isoId1();
    m_front.takeAll();
    m_front.resetBackground();
    m_back.takeAll();
    m_back.resetBackground();
    if (m_assets)
        m_assets->clear();

    m_filePath.clear();
    m_author.clear();
    m_notes.clear();
    m_isTemplate = false;
    m_created = QDateTime::currentDateTimeUtc();
    m_modified = m_created;

    const bool wasDirty = m_dirty;
    m_dirty = false;

    emit filePathChanged(m_filePath);
    emit geometryChanged();
    emit sideChanged(CardSideId::Front);
    emit sideChanged(CardSideId::Back);
    emit backgroundChanged(CardSideId::Front);
    emit backgroundChanged(CardSideId::Back);
    if (wasDirty)
        emit dirtyChanged(false);
    emit contentsChanged();
}

void CardDocument::notifyChanged()
{
    touch();
    setDirty(true);
    emit contentsChanged();
}

void CardDocument::notifyReloaded()
{
    emit geometryChanged();
    emit sideChanged(CardSideId::Front);
    emit sideChanged(CardSideId::Back);
    emit backgroundChanged(CardSideId::Front);
    emit backgroundChanged(CardSideId::Back);
    emit contentsChanged();
}

} // namespace occ
