#pragma once

#include "core/CardTypes.h"

#include <QJsonObject>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>

#include <memory>

namespace occ {

class RenderContext;

// ---------------------------------------------------------------------------
// CardObject - abstract base for everything that can be placed on a card.
//
// Geometry is expressed in MILLIMETRES relative to the card's trim rectangle
// (origin = top-left corner of the card). That makes a design physically
// accurate regardless of screen DPI or output resolution, and lets the same
// document be rendered for the editor, the print preview, a PNG export and the
// printer without any conversion drift.
//
// Rotation is applied about the object's centre. Opacity, visibility and the
// lock state are common to every object type, as is the layer name.
// ---------------------------------------------------------------------------
class CardObject
{
public:
    CardObject(ObjectType type, const ObjectId &id);
    virtual ~CardObject();

    CardObject(const CardObject &) = delete;
    CardObject &operator=(const CardObject &) = delete;

    // --- identity -----------------------------------------------------------
    ObjectType type() const { return m_type; }
    ObjectId id() const { return m_id; }
    // Assigns a fresh id (used when pasting a copy).
    void regenerateId();

    // Layer name shown in the Layers panel.
    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    // --- geometry (millimetres) --------------------------------------------
    QRectF rectMm() const { return m_rectMm; }
    void setRectMm(const QRectF &rect);

    double xMm() const { return m_rectMm.x(); }
    double yMm() const { return m_rectMm.y(); }
    double widthMm() const { return m_rectMm.width(); }
    double heightMm() const { return m_rectMm.height(); }
    double centerXMm() const { return m_rectMm.center().x(); }
    double centerYMm() const { return m_rectMm.center().y(); }

    void setXMm(double x);
    void setYMm(double y);
    void setWidthMm(double w);
    void setHeightMm(double h);
    void setCenterMm(const QPointF &center);
    void moveByMm(double dx, double dy);

    // Rotation in degrees, clockwise, about the object's centre.
    double rotationDeg() const { return m_rotationDeg; }
    void setRotationDeg(double degrees);

    // --- appearance / state -------------------------------------------------
    double opacity() const { return m_opacity; }
    void setOpacity(double opacity);                        // clamped to 0..1

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }

    bool isLocked() const { return m_locked; }
    void setLocked(bool locked) { m_locked = locked; }

    // Painter's-algorithm z order. Owned and renumbered by CardSide.
    int zOrder() const { return m_zOrder; }
    void setZOrder(int z) { m_zOrder = z; }

    // --- geometry helpers ---------------------------------------------------
    // The four corners of the rotated object, in millimetres.
    QPolygonF outlineMm() const;
    // Axis-aligned bounding box of the rotated object, in millimetres.
    QRectF boundingRectMm() const;
    // Hit test in card millimetres, honouring rotation.
    bool containsMm(const QPointF &pointMm) const;
    // True when the object's bounding box intersects the given band.
    bool intersectsMm(const QRectF &rectMm) const;

    // --- rendering ----------------------------------------------------------
    // Paints the object. The painter is expected to be scaled so that one
    // user-space unit equals one millimetre (see RenderContext::pxPerMm).
    void paint(QPainter &painter, const RenderContext &ctx) const;

    // --- cloning / persistence ---------------------------------------------
    std::unique_ptr<CardObject> clone() const;

    QJsonObject toJson() const;
    // Returns false and fills `error` for an unusable object definition. The
    // object is left untouched when loading fails.
    bool fromJson(const QJsonObject &json, QString *error);

    // Human readable type name for the UI.
    QString typeDisplayName() const;

protected:
    // Paints the object using local coordinates: (0,0) is the top-left corner
    // of the object and the size is widthMm() x heightMm(). Rotation, opacity
    // and the millimetre scaling are already applied by paint().
    virtual void paintObject(QPainter &painter, const RenderContext &ctx) const = 0;

    // Subclass specific properties, merged into toJson().
    virtual QJsonObject propertiesToJson() const = 0;
    virtual bool propertiesFromJson(const QJsonObject &json, QString *error) = 0;

    // Default display name for a newly created object of this type.
    virtual QString defaultName() const = 0;

    // Non-destructive clone of the subclass payload.
    virtual std::unique_ptr<CardObject> cloneImpl() const = 0;

    // Copies common state during clone().
    void copyBaseTo(CardObject &target) const;

    // Clamps a value into the legal range for the object (used after resize or
    // a property edit so an object can never become degenerate).
    static double saneSize(double mm);

    // The layer name. It is protected rather than private because every
    // concrete object sets it from its own constructor
    // (m_name = defaultName()), which is how the Layers panel gets a sensible
    // label ("Text", "Photo", "QR Code", ...) for a freshly inserted object.
    QString m_name;

private:
    ObjectType m_type;
    ObjectId   m_id;
    QRectF     m_rectMm{0, 0, 10, 10};
    double     m_rotationDeg = 0.0;
    double     m_opacity = 1.0;
    bool       m_visible = true;
    bool       m_locked = false;
    int        m_zOrder = 0;
};

using CardObjectPtr = std::unique_ptr<CardObject>;

} // namespace occ
