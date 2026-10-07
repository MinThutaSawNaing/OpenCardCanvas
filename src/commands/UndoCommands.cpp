#include "commands/UndoCommands.h"

#include "canvas/GuideModel.h"
#include "core/CardDocument.h"
#include "core/CardSide.h"
#include "core/GroupObject.h"
#include "core/ObjectFactory.h"
#include "utils/TextUtils.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace occ {

namespace {

QString commandTr(const char *text)
{
    return QCoreApplication::translate("UndoCommands", text);
}

constexpr int kMaxObjectsPerCommand = 20000;

QString idOf(const QJsonObject &json)
{
    return json.value(QStringLiteral("id")).toString();
}

ObjectId objectIdOf(const QJsonObject &json)
{
    return ObjectId(idOf(json));
}

} // namespace

// ---------------------------------------------------------------------------
// ObjectCommand
// ---------------------------------------------------------------------------

ObjectCommand::ObjectCommand(CardDocument *document, CardSideId side,
                            QUndoCommand *parent)
    : QUndoCommand(parent), m_document(document), m_side(side)
{
}

CardSide &ObjectCommand::side() const
{
    // A document is required for every command; without one there is nothing to
    // edit, so the caller made a programming mistake rather than a user error.
    Q_ASSERT(m_document);
    return m_document->side(m_side);
}

void ObjectCommand::restoreObject(const QJsonObject &json, int index)
{
    if (!m_document)
        return;
    QString error;
    CardObjectPtr object = ObjectFactory::createAndLoad(json, &error);
    if (!object)
        return;     // a snapshot we cannot read is refused rather than half applied
    m_document->side(m_side).insertObject(std::move(object), index);
}

bool ObjectCommand::replaceObjectState(const ObjectId &id, const QJsonObject &json)
{
    if (!m_document)
        return false;
    CardSide &target = m_document->side(m_side);
    CardObject *existing = target.object(id);
    if (!existing)
        return false;

    // The state is applied by loading it into a fresh object and then copying the
    // result over the original, so a malformed snapshot cannot leave the object
    // half updated.
    QString error;
    CardObjectPtr replacement = ObjectFactory::createAndLoad(json, &error);
    if (!replacement)
        return false;
    if (replacement->type() != existing->type())
        return false;

    const int index = target.indexOf(id);
    target.takeObject(id);
    target.insertObject(std::move(replacement), index);
    return true;
}

QString ObjectCommand::objectLabel(const QJsonObject &json)
{
    const QString name = json.value(QStringLiteral("name")).toString().trimmed();
    if (!name.isEmpty())
        return name;
    const QString type = json.value(QStringLiteral("type")).toString();
    ObjectType parsed = ObjectType::Text;
    if (names::objectTypeFromString(type, &parsed)) {
        switch (parsed) {
        case ObjectType::Text:    return commandTr("Text");
        case ObjectType::Image:   return commandTr("Image");
        case ObjectType::Photo:   return commandTr("Photo");
        case ObjectType::Shape:   return commandTr("Shape");
        case ObjectType::QrCode:  return commandTr("QR Code");
        case ObjectType::Barcode: return commandTr("Barcode");
        case ObjectType::Group:   return commandTr("Group");
        }
    }
    return commandTr("Object");
}

// ---------------------------------------------------------------------------
// AddObjectsCommand
// ---------------------------------------------------------------------------

AddObjectsCommand::AddObjectsCommand(CardDocument *document, CardSideId side,
                                    const QVector<QJsonObject> &objects, int index,
                                    QUndoCommand *parent)
    : ObjectCommand(document, side, parent), m_objects(objects), m_index(index)
{
    const int count = int(objects.size());
    if (count == 1) {
        setText(commandTr("Add %1").arg(objectLabel(objects.first())));
    } else {
        setText(QCoreApplication::translate("UndoCommands", "Add %n object(s)", nullptr,
                                            count));
    }
}

void AddObjectsCommand::redo()
{
    if (!document() || m_objects.isEmpty())
        return;

    CardSide &target = side();
    int index = m_index;
    for (const QJsonObject &json : m_objects) {
        QString error;
        CardObjectPtr object = ObjectFactory::createAndLoad(json, &error);
        if (!object)
            continue;
        target.insertObject(std::move(object), index);
        if (index >= 0)
            ++index;
    }
    document()->notifyChanged();
}

void AddObjectsCommand::undo()
{
    if (!document())
        return;
    CardSide &target = side();
    // Removing by id (rather than by the stored index) is what keeps undo correct
    // even when the user reordered things in between.
    for (int i = int(m_objects.size()) - 1; i >= 0; --i)
        target.takeObject(objectIdOf(m_objects.at(i)));
    document()->notifyChanged();
}

// ---------------------------------------------------------------------------
// RemoveObjectsCommand
// ---------------------------------------------------------------------------

RemoveObjectsCommand::RemoveObjectsCommand(CardDocument *document, CardSideId side,
                                          const QVector<QJsonObject> &objects, int index,
                                          QUndoCommand *parent)
    : ObjectCommand(document, side, parent), m_objects(objects), m_index(index)
{
    const int count = int(objects.size());
    if (count == 1) {
        setText(commandTr("Delete %1").arg(objectLabel(objects.first())));
    } else {
        setText(QCoreApplication::translate("UndoCommands", "Delete %n object(s)",
                                            nullptr, count));
    }
}

void RemoveObjectsCommand::redo()
{
    if (!document())
        return;
    CardSide &target = side();
    for (const QJsonObject &json : m_objects)
        target.takeObject(objectIdOf(json));
    document()->notifyChanged();
}

void RemoveObjectsCommand::undo()
{
    if (!document())
        return;

    CardSide &target = side();
    // Re-inserting at the recorded index restores the original paint order
    // exactly, which is what makes "undo a delete" invisible to the user.
    int index = m_index;
    for (const QJsonObject &json : m_objects) {
        QString error;
        CardObjectPtr object = ObjectFactory::createAndLoad(json, &error);
        if (!object)
            continue;
        target.insertObject(std::move(object), index);
        if (index >= 0)
            ++index;
    }
    document()->notifyChanged();
}

// ---------------------------------------------------------------------------
// ModifyObjectsCommand
// ---------------------------------------------------------------------------

ModifyObjectsCommand::ModifyObjectsCommand(
    CardDocument *document, CardSideId side,
    const QVector<QPair<ObjectId, QJsonObject>> &before,
    const QVector<QPair<ObjectId, QJsonObject>> &after, const QString &text,
    const QString &mergeKey, QUndoCommand *parent)
    : ObjectCommand(document, side, parent), m_before(before), m_after(after),
      m_mergeKey(mergeKey)
{
    setText(text);
}

void ModifyObjectsCommand::redo()
{
    if (!document())
        return;
    for (const auto &entry : m_after)
        replaceObjectState(entry.first, entry.second);
    document()->notifyChanged();
}

void ModifyObjectsCommand::undo()
{
    if (!document())
        return;
    for (const auto &entry : m_before)
        replaceObjectState(entry.first, entry.second);
    document()->notifyChanged();
}

bool ModifyObjectsCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const ModifyObjectsCommand *>(other);
    if (!next)
        return false;
    if (m_mergeKey.isEmpty() || next->m_mergeKey != m_mergeKey)
        return false;
    if (next->m_before.size() != m_before.size())
        return false;
    // Only the same objects in the same order may merge; otherwise the group of
    // edits is genuinely different and deserves its own history entry.
    for (int i = 0; i < m_before.size(); ++i) {
        if (m_before.at(i).first != next->m_before.at(i).first)
            return false;
    }

    // The "after" of the newer command is the state to land on; the "before" of
    // this one is still the state to return to.
    m_after = next->m_after;
    setText(next->text());
    return true;
}

QVector<QPair<ObjectId, QJsonObject>> ModifyObjectsCommand::captureState(
    const CardSide &side, const QVector<ObjectId> &ids)
{
    QVector<QPair<ObjectId, QJsonObject>> state;
    state.reserve(ids.size());
    for (const ObjectId &id : ids) {
        const CardObject *object = side.object(id);
        if (!object)
            continue;
        state.append({ id, object->toJson() });
    }
    return state;
}

QVector<QPair<ObjectId, QJsonObject>> ModifyObjectsCommand::single(
    const ObjectId &id, const QJsonObject &state)
{
    QVector<QPair<ObjectId, QJsonObject>> out;
    out.append({ id, state });
    return out;
}

// ---------------------------------------------------------------------------
// ReorderObjectsCommand
// ---------------------------------------------------------------------------

ReorderObjectsCommand::ReorderObjectsCommand(CardDocument *document, CardSideId side,
                                            const QVector<ObjectId> &before,
                                            const QVector<ObjectId> &after,
                                            const QString &text, QUndoCommand *parent)
    : ObjectCommand(document, side, parent), m_before(before), m_after(after)
{
    setText(text);
}

void ReorderObjectsCommand::redo()
{
    if (!document())
        return;
    side().applyOrder(m_after);
    document()->notifyChanged();
}

void ReorderObjectsCommand::undo()
{
    if (!document())
        return;
    side().applyOrder(m_before);
    document()->notifyChanged();
}

// ---------------------------------------------------------------------------
// RenameObjectCommand
// ---------------------------------------------------------------------------

RenameObjectCommand::RenameObjectCommand(CardDocument *document, CardSideId side,
                                        const ObjectId &id, const QString &before,
                                        const QString &after, QUndoCommand *parent)
    : ObjectCommand(document, side, parent), m_id(id), m_before(before), m_after(after)
{
    setText(commandTr("Rename to \"%1\"").arg(after));
}

void RenameObjectCommand::redo()
{
    if (!document())
        return;
    if (CardObject *object = side().object(m_id))
        object->setName(m_after);
    document()->notifyChanged();
}

void RenameObjectCommand::undo()
{
    if (!document())
        return;
    if (CardObject *object = side().object(m_id))
        object->setName(m_before);
    document()->notifyChanged();
}

bool RenameObjectCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const RenameObjectCommand *>(other);
    if (!next || next->m_id != m_id)
        return false;
    // Typing a name should be one history entry, not one per keystroke.
    m_after = next->m_after;
    setText(next->text());
    return true;
}

// ---------------------------------------------------------------------------
// BackgroundCommand
// ---------------------------------------------------------------------------

BackgroundCommand::BackgroundCommand(CardDocument *document, CardSideId side,
                                    const CardSide::Background &before,
                                    const CardSide::Background &after,
                                    const QString &text, QUndoCommand *parent)
    : ObjectCommand(document, side, parent), m_before(before.toJson()),
      m_after(after.toJson())
{
    setText(text);
}

void BackgroundCommand::redo()
{
    if (!document())
        return;
    CardSide::Background background;
    if (background.fromJson(m_after)) {
        document()->side(sideId()).setBackground(background);
        emit document()->backgroundChanged(sideId());
    }
    document()->notifyChanged();
}

void BackgroundCommand::undo()
{
    if (!document())
        return;
    CardSide::Background background;
    if (background.fromJson(m_before)) {
        document()->side(sideId()).setBackground(background);
        emit document()->backgroundChanged(sideId());
    }
    document()->notifyChanged();
}

bool BackgroundCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const BackgroundCommand *>(other);
    if (!next)
        return false;
    // Dragging a colour slider produces a stream of background changes; they
    // belong in one entry.
    m_after = next->m_after;
    setText(next->text());
    return true;
}

// ---------------------------------------------------------------------------
// GeometryCommand
// ---------------------------------------------------------------------------

GeometryCommand::GeometryCommand(CardDocument *document, const CardGeometry &before,
                                const CardGeometry &after, const QString &text,
                                QUndoCommand *parent)
    : QUndoCommand(parent), m_document(document), m_before(before), m_after(after)
{
    setText(text);
}

void GeometryCommand::redo()
{
    if (!m_document)
        return;
    m_document->setGeometry(m_after);
}

void GeometryCommand::undo()
{
    if (!m_document)
        return;
    m_document->setGeometry(m_before);
}

bool GeometryCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const GeometryCommand *>(other);
    if (!next || next->m_document != m_document)
        return false;
    m_after = next->m_after;
    setText(next->text());
    return true;
}

// ---------------------------------------------------------------------------
// Group / ungroup
// ---------------------------------------------------------------------------

GroupCommand::GroupCommand(CardDocument *document, CardSideId side,
                          const QVector<ObjectId> &members, const ObjectId &groupId,
                          QUndoCommand *parent)
    : ObjectCommand(document, side, parent), m_members(members), m_groupId(groupId)
{
    setText(QCoreApplication::translate("UndoCommands", "Group %n object(s)", nullptr,
                                        int(members.size())));
}

void GroupCommand::redo()
{
    if (!document() || m_members.isEmpty())
        return;

    CardSide &target = side();
    // The group is inserted where the topmost member was, so grouping does not
    // change the stacking order of the card.
    int insertAt = -1;
    for (const ObjectId &id : m_members)
        insertAt = qMax(insertAt, target.indexOf(id));
    if (m_index < 0)
        m_index = qMax(0, insertAt);

    auto group = std::make_unique<GroupObject>();
    // Objects keep their card coordinates, so grouping rewrites nothing.
    for (const ObjectId &id : m_members) {
        CardObjectPtr member = target.takeObject(id);
        if (member)
            group->addChild(std::move(member));
    }
    if (group->childCount() == 0)
        return;
    group->syncBounds();
    target.insertObject(std::move(group), m_index);
    document()->notifyChanged();
}

void GroupCommand::undo()
{
    if (!document())
        return;

    CardSide &target = side();
    // The group holding exactly these members is the one this command created.
    CardObject *found = nullptr;
    for (CardObject *object : target.objects()) {
        auto *candidate = dynamic_cast<GroupObject *>(object);
        if (!candidate || candidate->childCount() != int(m_members.size()))
            continue;
        bool matches = true;
        for (const ObjectId &id : m_members) {
            if (!candidate->child(id)) {
                matches = false;
                break;
            }
        }
        if (matches) {
            found = candidate;
            break;
        }
    }
    if (!found)
        return;

    CardObjectPtr owned = target.takeObject(found->id());
    auto *group = dynamic_cast<GroupObject *>(owned.get());
    if (!group)
        return;

    int index = m_index;
    for (CardObjectPtr &child : group->takeAllChildren()) {
        target.insertObject(std::move(child), index);
        if (index >= 0)
            ++index;
    }
    document()->notifyChanged();
}

UngroupCommand::UngroupCommand(CardDocument *document, CardSideId side,
                              const ObjectId &groupId,
                              const QVector<QJsonObject> &children, QUndoCommand *parent)
    : ObjectCommand(document, side, parent), m_groupId(groupId), m_children(children)
{
    setText(QCoreApplication::translate("UndoCommands", "Ungroup %n object(s)", nullptr,
                                        int(children.size())));
    captureGroup();
}

UngroupCommand::UngroupCommand(CardDocument *document, CardSideId side,
                              const ObjectId &groupId, QUndoCommand *parent)
    : ObjectCommand(document, side, parent), m_groupId(groupId)
{
    setText(commandTr("Ungroup"));
    captureGroup();
}

void UngroupCommand::captureGroup()
{
    if (!document())
        return;
    const CardObject *object = side().object(m_groupId);
    const auto *group = dynamic_cast<const GroupObject *>(object);
    if (!group)
        return;
    m_groupState = group->toJson();
    m_index = side().indexOf(m_groupId);
    if (m_children.isEmpty()) {
        for (CardObject *child : group->children()) {
            if (child)
                m_children.append(child->toJson());
        }
    }
}

void UngroupCommand::redo()
{
    if (!document())
        return;
    CardObject *object = side().object(m_groupId);
    auto *group = dynamic_cast<GroupObject *>(object);
    if (!group)
        return;

    const int index = side().indexOf(m_groupId);
    CardObjectPtr owned = side().takeObject(m_groupId);
    auto *ownedGroup = dynamic_cast<GroupObject *>(owned.get());
    if (!ownedGroup)
        return;

    int insertAt = index;
    for (CardObjectPtr &child : ownedGroup->takeAllChildren()) {
        side().insertObject(std::move(child), insertAt);
        if (insertAt >= 0)
            ++insertAt;
    }
    document()->notifyChanged();
}

void UngroupCommand::undo()
{
    if (!document() || m_groupState.isEmpty())
        return;

    CardSide &target = side();
    // The children are removed by id, then the original group is restored from its
    // snapshot, so the frame, rotation and name all come back exactly.
    for (const QJsonObject &child : m_children)
        target.takeObject(objectIdOf(child));

    QString error;
    CardObjectPtr group = ObjectFactory::createAndLoad(m_groupState, &error);
    if (!group)
        return;
    target.insertObject(std::move(group), m_index);
    document()->notifyChanged();
}

// ---------------------------------------------------------------------------
// Guides
// ---------------------------------------------------------------------------

AddGuideCommand::AddGuideCommand(GuideModel *guides, CardSideId side, bool horizontal,
                                double posMm, QUndoCommand *parent)
    : QUndoCommand(parent), m_guides(guides), m_side(side), m_horizontal(horizontal),
      m_posMm(posMm)
{
    setText(horizontal ? commandTr("Add horizontal guide")
                       : commandTr("Add vertical guide"));
}

void AddGuideCommand::redo()
{
    if (!m_guides)
        return;
    if (m_id.isNull())
        m_id = m_guides->add(m_side, m_horizontal, m_posMm);
    else
        m_guides->add(m_side, m_horizontal, m_posMm);
}

void AddGuideCommand::undo()
{
    if (m_guides && !m_id.isNull())
        m_guides->remove(m_side, m_id);
}

RemoveGuideCommand::RemoveGuideCommand(GuideModel *guides, CardSideId side,
                                      const ObjectId &id, QUndoCommand *parent)
    : QUndoCommand(parent), m_guides(guides), m_side(side), m_id(id)
{
    setText(commandTr("Delete guide"));
    if (m_guides) {
        for (const GuideModel::Guide &guide : m_guides->guides(m_side)) {
            if (guide.id != id)
                continue;
            m_horizontal = guide.horizontal;
            m_posMm = guide.posMm;
            break;
        }
    }
}

void RemoveGuideCommand::redo()
{
    if (m_guides)
        m_guides->remove(m_side, m_id);
}

void RemoveGuideCommand::undo()
{
    if (!m_guides)
        return;
    // A guide that is put back keeps its identity, so a later command that still
    // refers to it keeps working.
    m_guides->add(m_side, m_horizontal, m_posMm);
}

MoveGuideCommand::MoveGuideCommand(GuideModel *guides, CardSideId side, const ObjectId &id,
                                  double beforeMm, double afterMm, QUndoCommand *parent)
    : QUndoCommand(parent), m_guides(guides), m_side(side), m_id(id), m_before(beforeMm),
      m_after(afterMm)
{
    setText(commandTr("Move guide"));
}

void MoveGuideCommand::redo()
{
    if (m_guides)
        m_guides->move(m_side, m_id, m_after);
}

void MoveGuideCommand::undo()
{
    if (m_guides)
        m_guides->move(m_side, m_id, m_before);
}

bool MoveGuideCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const MoveGuideCommand *>(other);
    if (!next || next->m_id != m_id || next->m_guides != m_guides)
        return false;
    // Dragging one guide is one history entry.
    m_after = next->m_after;
    return true;
}

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------

namespace clipboard {

QString formatMarker()
{
    return QStringLiteral("OpenCardCanvas.objects/1");
}

QByteArray encode(const QVector<QJsonObject> &objects)
{
    QJsonArray array;
    for (const QJsonObject &object : objects) {
        if (object.isEmpty() || array.size() >= kMaxObjectsPerCommand)
            continue;
        array.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), formatMarker());
    root.insert(QStringLiteral("objects"), array);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool contains(const QByteArray &data)
{
    if (data.isEmpty())
        return false;
    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (!document.isObject())
        return false;
    const QJsonObject root = document.object();
    return root.value(QStringLiteral("format")).toString() == formatMarker()
           && root.value(QStringLiteral("objects")).isArray();
}

QVector<QJsonObject> decode(const QByteArray &data)
{
    QVector<QJsonObject> objects;
    if (!contains(data))
        return objects;

    const QJsonArray array =
        QJsonDocument::fromJson(data).object().value(QStringLiteral("objects")).toArray();
    for (const QJsonValue &value : array) {
        if (value.isObject())
            objects.append(value.toObject());
    }
    return objects;
}

} // namespace clipboard

} // namespace occ
