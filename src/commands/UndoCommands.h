#pragma once

#include "core/CardGeometry.h"
#include "core/CardSide.h"
#include "core/CardTypes.h"

#include <QByteArray>
#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QVector>
#include <QUndoCommand>

// ---------------------------------------------------------------------------
// UndoCommands - the editing history.
//
// Design note that makes this simple: an object's complete state is snapshotted
// as JSON, exactly the JSON that goes into a project file. That one mechanism
// therefore covers move, resize, rotate, opacity, visibility, lock, rename,
// text edits, style changes and property changes - there is no separate command
// class per property, and no way for a command to forget part of the state.
//
// The payloads are tiny because images never appear in them: an ImageObject
// stores its asset *id* and the bytes live in the AssetStore. A hundred undo
// steps on a photographic card therefore cost a few hundred kilobytes, not
// hundreds of megabytes.
//
// Every command takes the CardDocument it edits and calls
// CardDocument::notifyChanged() from redo() and undo(), so views follow the
// history for free. A command is bound to one document: it must not be shared
// between documents.
// ---------------------------------------------------------------------------
namespace occ {

class CardDocument;
class CardSide;
class GuideModel;

// ---------------------------------------------------------------------------
// Base class: holds the document and resolves the side, so every subclass can
// simply ask for it.
// ---------------------------------------------------------------------------
class ObjectCommand : public QUndoCommand
{
public:
    ObjectCommand(CardDocument *document, CardSideId side, QUndoCommand *parent = nullptr);

protected:
    CardDocument *document() const { return m_document; }
    CardSide     &side() const;
    CardSideId    sideId() const { return m_side; }

    // Re-inserts an object from a snapshot at the given paint-order index.
    void restoreObject(const QJsonObject &json, int index);
    // Replaces an object's state in place; returns false when it is gone.
    bool replaceObjectState(const ObjectId &id, const QJsonObject &json);

    static QString objectLabel(const QJsonObject &json);

private:
    CardDocument *m_document = nullptr;
    CardSideId    m_side = CardSideId::Front;
};

// ---------------------------------------------------------------------------
// Adding objects. `objects` are JSON snapshots (CardObject::toJson()) in paint
// order; `index` is the insertion index, -1 meaning "on top".
// ---------------------------------------------------------------------------
class AddObjectsCommand : public ObjectCommand
{
public:
    AddObjectsCommand(CardDocument *document, CardSideId side,
                      const QVector<QJsonObject> &objects, int index = -1,
                      QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

    const QVector<QJsonObject> &objects() const { return m_objects; }

private:
    QVector<QJsonObject> m_objects;
    int m_index;
};

// ---------------------------------------------------------------------------
// Removing objects. Remembers their positions so undo restores z-order exactly.
// ---------------------------------------------------------------------------
class RemoveObjectsCommand : public ObjectCommand
{
public:
    RemoveObjectsCommand(CardDocument *document, CardSideId side,
                         const QVector<QJsonObject> &objects, int index = -1,
                         QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

    const QVector<QJsonObject> &objects() const { return m_objects; }

private:
    QVector<QJsonObject> m_objects;
    int m_index;
};

// ---------------------------------------------------------------------------
// Changing objects. One command, every property.
// ---------------------------------------------------------------------------
class ModifyObjectsCommand : public ObjectCommand
{
public:
    ModifyObjectsCommand(CardDocument *document, CardSideId side,
                         const QVector<QPair<ObjectId, QJsonObject>> &before,
                         const QVector<QPair<ObjectId, QJsonObject>> &after,
                         const QString &text, const QString &mergeKey = QString(),
                         QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id() const override { return 1003; }
    bool mergeWith(const QUndoCommand *other) override;

    // Snapshots the current state of `ids`, in order. Missing ids are skipped.
    static QVector<QPair<ObjectId, QJsonObject>> captureState(const CardSide &side,
                                                              const QVector<ObjectId> &ids);

    // Convenience for the common "one object" case.
    static QVector<QPair<ObjectId, QJsonObject>> single(const ObjectId &id,
                                                        const QJsonObject &state);

    const QString &mergeKey() const { return m_mergeKey; }

private:
    QVector<QPair<ObjectId, QJsonObject>> m_before;
    QVector<QPair<ObjectId, QJsonObject>> m_after;
    // Commands sharing a merge key on the same objects collapse into one
    // history entry, so dragging a slider does not create two hundred steps.
    QString m_mergeKey;
};

// ---------------------------------------------------------------------------
// Paint order.
// ---------------------------------------------------------------------------
class ReorderObjectsCommand : public ObjectCommand
{
public:
    ReorderObjectsCommand(CardDocument *document, CardSideId side,
                          const QVector<ObjectId> &before, const QVector<ObjectId> &after,
                          const QString &text, QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    QVector<ObjectId> m_before;
    QVector<ObjectId> m_after;
};

// ---------------------------------------------------------------------------
// Renaming (kept separate so the Layers panel can merge keystrokes).
// ---------------------------------------------------------------------------
class RenameObjectCommand : public ObjectCommand
{
public:
    RenameObjectCommand(CardDocument *document, CardSideId side, const ObjectId &id,
                        const QString &before, const QString &after,
                        QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id() const override { return 1005; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    ObjectId m_id;
    QString  m_before;
    QString  m_after;
};

// ---------------------------------------------------------------------------
// Side background. Stored as JSON so this header stays light and so a
// background written by a later version round-trips unchanged.
// ---------------------------------------------------------------------------
class BackgroundCommand : public ObjectCommand
{
public:
    BackgroundCommand(CardDocument *document, CardSideId side,
                      const CardSide::Background &before,
                      const CardSide::Background &after,
                      const QString &text, QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id() const override { return 1006; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    QJsonObject m_before;
    QJsonObject m_after;
};

// ---------------------------------------------------------------------------
// Card geometry. Objects are re-validated against the new card by the document.
// ---------------------------------------------------------------------------
class GeometryCommand : public QUndoCommand
{
public:
    GeometryCommand(CardDocument *document, const CardGeometry &before,
                    const CardGeometry &after, const QString &text,
                    QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id() const override { return 1007; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    CardDocument *m_document = nullptr;
    CardGeometry  m_before;
    CardGeometry  m_after;
};

// ---------------------------------------------------------------------------
// Grouping / ungrouping.
// ---------------------------------------------------------------------------
class GroupCommand : public ObjectCommand
{
public:
    // `members` are the top-level objects to combine, in paint order.
    GroupCommand(CardDocument *document, CardSideId side, const QVector<ObjectId> &members,
                 const ObjectId &groupId, QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

    ObjectId groupId() const { return m_groupId; }

private:
    QVector<ObjectId> m_members;
    ObjectId          m_groupId;
    int               m_index = -1;
};

class UngroupCommand : public ObjectCommand
{
public:
    // `children` are the group's children as JSON, needed to redo the grouping.
    // The group's own rectangle and rotation are remembered as well, because a
    // group is positioned in card coordinates and its children are not moved
    // when it is ungrouped.
    UngroupCommand(CardDocument *document, CardSideId side, const ObjectId &groupId,
                   const QVector<QJsonObject> &children, QUndoCommand *parent = nullptr);
    // Variant that snapshots the group itself, used when the caller has not
    // already captured the children.
    UngroupCommand(CardDocument *document, CardSideId side, const ObjectId &groupId,
                   QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

    ObjectId groupId() const { return m_groupId; }

private:
    ObjectId             m_groupId;
    QVector<QJsonObject> m_children;
    QJsonObject          m_groupState;
    int                  m_index = -1;

    // Reads the group's current state and children, for the undo step.
    void captureGroup();
};

// ---------------------------------------------------------------------------
// Guides. These live in GuideModel rather than in the document, so they take
// the model directly; the model is persisted through the side's JSON.
// ---------------------------------------------------------------------------
class AddGuideCommand : public QUndoCommand
{
public:
    AddGuideCommand(GuideModel *guides, CardSideId side, bool horizontal, double posMm,
                    QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;
    ObjectId guideId() const { return m_id; }

private:
    GuideModel *m_guides = nullptr;
    CardSideId  m_side = CardSideId::Front;
    bool        m_horizontal = true;
    double      m_posMm = 0.0;
    ObjectId    m_id;
};

class RemoveGuideCommand : public QUndoCommand
{
public:
    RemoveGuideCommand(GuideModel *guides, CardSideId side, const ObjectId &id,
                       QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    GuideModel *m_guides = nullptr;
    CardSideId  m_side = CardSideId::Front;
    ObjectId    m_id;
    bool        m_horizontal = true;
    double      m_posMm = 0.0;
};

class MoveGuideCommand : public QUndoCommand
{
public:
    MoveGuideCommand(GuideModel *guides, CardSideId side, const ObjectId &id,
                     double beforeMm, double afterMm, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;
    int  id() const override { return 1012; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    GuideModel *m_guides = nullptr;
    CardSideId  m_side = CardSideId::Front;
    ObjectId    m_id;
    double      m_before = 0.0;
    double      m_after = 0.0;
};

// ---------------------------------------------------------------------------
// Clipboard support: JSON snapshots, shared by Copy / Cut / Paste / Duplicate.
// ---------------------------------------------------------------------------
namespace clipboard {

// Serialises snapshots as a small JSON document carrying a format marker, so
// data copied from another application is never mistaken for our own.
QByteArray encode(const QVector<QJsonObject> &objects);
// Decodes them; returns an empty vector for anything that is not ours.
QVector<QJsonObject> decode(const QByteArray &data);
// True when the payload carries our format marker.
bool contains(const QByteArray &data);
// The marker written into the clipboard document.
QString formatMarker();

} // namespace clipboard

} // namespace occ
