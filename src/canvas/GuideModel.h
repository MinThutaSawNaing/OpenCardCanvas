#pragma once

#include "core/CardTypes.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

// ---------------------------------------------------------------------------
// GuideModel - the alignment guides of each card side.
//
// Guides belong to a side, not to the document, because the front and the back
// of an ID card are laid out independently: a guide that lines up the photo on
// the front is meaningless on the back, and a shared guide set would force the
// user to delete it to get it out of the way.
//
// A guide is a line at a millimetre position. Horizontal guides are the ones
// you drag out of the horizontal ruler; the position is measured from the top
// edge of the card. Vertical guides are measured from the left edge.
//
// Identity is a UUID so an undo command can refer to a guide that has been
// removed and put back.
// ---------------------------------------------------------------------------
namespace occ {

class GuideModel : public QObject
{
    Q_OBJECT
public:
    struct Guide
    {
        ObjectId id;
        bool     horizontal = true;
        double   posMm = 0.0;

        bool operator==(const Guide &other) const
        { return id == other.id && horizontal == other.horizontal && posMm == other.posMm; }
    };

    explicit GuideModel(QObject *parent = nullptr);
    ~GuideModel() override;

    // --- editing ------------------------------------------------------------
    // Adds a guide and returns its id. A position that already carries a guide
    // is still added (the user may want a second one while dragging); use
    // guideAt() to find existing ones.
    ObjectId add(CardSideId side, bool horizontal, double posMm);
    bool remove(CardSideId side, const ObjectId &id);
    bool move(CardSideId side, const ObjectId &id, double newPosMm);
    void clear(CardSideId side);
    void clearAll();

    // --- queries ------------------------------------------------------------
    QVector<Guide> guides(CardSideId side) const;
    QVector<Guide> guides(CardSideId side, bool horizontal) const;
    // Position of a guide, or false when it does not exist.
    bool position(CardSideId side, const ObjectId &id, double *posMm) const;
    bool contains(CardSideId side, const ObjectId &id) const;
    int  count(CardSideId side) const;
    // The guide nearest to `posMm` within `toleranceMm`, or a null id.
    ObjectId guideAt(CardSideId side, bool horizontal, double posMm,
                     double toleranceMm, double *actualPosMm = nullptr) const;

    // --- persistence --------------------------------------------------------
    QJsonObject toJson(CardSideId side) const;
    bool fromJson(CardSideId side, const QJsonObject &json);
    // "guides" object for both sides, for the project file.
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &json);

signals:
    void changed(occ::CardSideId side);

private:
    QVector<Guide> &listFor(CardSideId side);
    const QVector<Guide> &listFor(CardSideId side) const;

    QVector<Guide> m_front;
    QVector<Guide> m_back;
};

} // namespace occ
