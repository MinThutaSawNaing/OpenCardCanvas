#pragma once

#include "core/CardObject.h"
#include "core/CardTypes.h"

#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <functional>
#include <vector>

namespace occ {

// ---------------------------------------------------------------------------
// CardSide - one printable side of a card (FRONT or BACK).
//
// Owns its objects and is solely responsible for z-order: objects are stored in
// paint order, so index 0 is the bottom layer. Every reorder goes through this
// class, which guarantees the Layers panel and the renderer agree.
// ---------------------------------------------------------------------------
class CardSide
{
public:
    struct Background
    {
        enum class Kind { None, Solid, LinearGradient, RadialGradient, Image };
        Kind   kind = Kind::Solid;
        QColor color = QColor(Qt::white);
        QColor color2 = QColor(Qt::white);
        // Gradient direction in degrees (0 = left to right).
        double angleDeg = 0.0;
        QString assetId;
        bool    stretchImage = true;

        void reset();
        QJsonObject toJson() const;
        bool fromJson(const QJsonObject &json);
    };

    explicit CardSide(CardSideId id);
    ~CardSide();

    CardSide(const CardSide &) = delete;
    CardSide &operator=(const CardSide &) = delete;

    CardSideId id() const { return m_id; }

    // --- background ---------------------------------------------------------
    const Background &background() const { return m_background; }
    void setBackground(const Background &background) { m_background = background; }
    void resetBackground() { m_background.reset(); }

    // --- objects ------------------------------------------------------------
    int count() const { return int(m_objects.size()); }
    bool isEmpty() const { return m_objects.empty(); }

    // Bottom-to-top order (index 0 is painted first).
    const QVector<CardObject *> objects() const;
    CardObject *object(const ObjectId &id) const;
    int indexOf(const ObjectId &id) const;

    // Inserts at `index` (-1 = on top) and takes ownership.
    void insertObject(CardObjectPtr object, int index = -1);
    CardObjectPtr takeObject(const ObjectId &id);
    // Removes every object and returns them (used by "clear side").
    //
    // NOTE: this returns std::vector rather than QVector. QVector is an alias
    // for QList, and QList<std::unique_ptr<T>> cannot be compiled by this
    // toolchain: MSVC instantiates QGenericArrayOps<T>::copyAppend eagerly and
    // that member requires a copy constructor, which unique_ptr does not have
    // (verified on MSVC 19.44 / Qt 6.8.3, see the build report). std::vector
    // has none of that trouble, is the container Qt itself recommends for
    // move-only types, and leaves every other name and signature in this class
    // unchanged. The object *storage* below is std::vector for the same reason.
    std::vector<CardObjectPtr> takeAll();

    // --- ordering -----------------------------------------------------------
    // Moves `id` to the given index among the top level objects.
    bool moveTo(const ObjectId &id, int index);
    bool moveToFront(const ObjectId &id);
    bool moveForward(const ObjectId &id);
    bool moveBackward(const ObjectId &id);
    bool moveToBack(const ObjectId &id);

    // Applies an explicit order. Every id must exist and no id may be missing;
    // returns false otherwise and leaves the order untouched.
    bool applyOrder(const QVector<ObjectId> &order);
    QVector<ObjectId> objectIds() const;

    // --- queries ------------------------------------------------------------
    // Hit test in card millimetres, top-most first. Locked and hidden objects
    // are skipped unless `includeLocked` / `includeHidden` are set.
    CardObject *hitTest(const QPointF &pointMm, bool includeLocked = false,
                        bool includeHidden = false) const;
    QVector<CardObject *> objectsIn(const QRectF &rectMm, bool includeLocked = false,
                                    bool includeHidden = false) const;
    // Union of every visible object's rotated bounding box (used for Fit).
    QRectF contentBoundsMm() const;
    bool hasPlaceholders() const;

    // --- persistence --------------------------------------------------------
    QJsonObject toJson() const;
    // `createObject` lets the loader build concrete objects; it is injected so
    // CardSide does not need to know about ObjectFactory.
    using ObjectReader = std::function<CardObjectPtr(const QJsonObject &, QString *)>;
    bool fromJson(const QJsonObject &json, const ObjectReader &createObject, QString *error);

    void setLocked(bool locked);
    void setVisible(bool visible);

private:
    void renumber();

    CardSideId              m_id;
    Background              m_background;
    std::vector<CardObjectPtr> m_objects;   // paint order, index 0 = bottom
};

} // namespace occ
