#pragma once

#include "core/CardGeometry.h"
#include "core/CardSide.h"
#include "core/CardTypes.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

namespace occ {

class AssetStore;

// ---------------------------------------------------------------------------
// CardDocument - the model behind one project.
//
// A document owns the card geometry, the two sides, the asset store and the
// project metadata. It is a QObject only so that views can be notified; it does
// not know anything about editing commands (those live in the command layer) or
// about rendering.
//
// After any change made through this class, contentsChanged() is emitted and the
// document is marked dirty. Commands are responsible for the undo state; the
// document only reports that its content changed.
// ---------------------------------------------------------------------------
class CardDocument : public QObject
{
    Q_OBJECT
public:
    explicit CardDocument(QObject *parent = nullptr);
    ~CardDocument() override;

    // --- identity / persistence --------------------------------------------
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path);
    bool isNew() const { return m_filePath.isEmpty(); }

    bool isDirty() const { return m_dirty; }
    void setDirty(bool dirty);

    QString title() const;
    QString author() const { return m_author; }
    void setAuthor(const QString &author);
    QString notes() const { return m_notes; }
    void setNotes(const QString &notes);

    QDateTime created() const { return m_created; }
    QDateTime modified() const { return m_modified; }
    void touch();

    // True when this document originated from a template.
    bool isTemplate() const { return m_isTemplate; }
    void setIsTemplate(bool on);

    // --- geometry -----------------------------------------------------------
    const CardGeometry &geometry() const { return m_geometry; }
    // Replaces the geometry and clamps every object into the new card.
    void setGeometry(const CardGeometry &geometry);

    // --- sides --------------------------------------------------------------
    CardSide &side(CardSideId id);
    const CardSide &side(CardSideId id) const;
    CardSide &front() { return side(CardSideId::Front); }
    CardSide &back() { return side(CardSideId::Back); }
    const CardSide &front() const { return side(CardSideId::Front); }
    const CardSide &back() const { return side(CardSideId::Back); }

    // Copies every object of `from` onto `to`, replacing its content.
    void copySideContent(CardSideId from, CardSideId to);
    void clearSide(CardSideId id);

    // --- assets -------------------------------------------------------------
    AssetStore *assets();
    const AssetStore *assets() const;

    // --- placeholders -------------------------------------------------------
    // Every distinct {{placeholder}} used anywhere in the document.
    QStringList placeholders() const;
    // Convenience used by the personalization dialog for suggested mappings.
    QStringList suggestedPlaceholders() const;

    // --- high level operations ---------------------------------------------
    // Resets to an empty document with an ID-1 front and back.
    void newDocument();

    // Emits contentsChanged() and marks the document dirty.
    void notifyChanged();
    // Emits the signal without touching the dirty flag (used after load).
    void notifyReloaded();

signals:
    void contentsChanged();
    void sideChanged(occ::CardSideId side);
    void geometryChanged();
    void dirtyChanged(bool dirty);
    void filePathChanged(const QString &path);
    void backgroundChanged(occ::CardSideId side);

private:
    CardGeometry  m_geometry;
    CardSide      m_front;
    CardSide      m_back;
    AssetStore   *m_assets = nullptr;

    QString   m_filePath;
    QString   m_author;
    QString   m_notes;
    QDateTime m_created;
    QDateTime m_modified;
    bool      m_dirty = false;
    bool      m_isTemplate = false;
};

} // namespace occ
