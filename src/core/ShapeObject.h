#pragma once

#include "core/CardObject.h"

#include <QColor>
#include <QPainterPath>

namespace occ {

// ---------------------------------------------------------------------------
// ShapeObject - vector primitives: rectangle, rounded rectangle, ellipse, line,
// triangle, regular polygon, star and arrow.
//
// Fill and stroke are independent and both support transparency. A line is a
// horizontal segment whose angle is controlled by the object's rotation, which
// keeps its bounding box predictable while dragging.
// ---------------------------------------------------------------------------
class ShapeObject : public CardObject
{
public:
    ShapeObject();
    explicit ShapeObject(ShapeKind kind);

    ShapeKind kind() const { return m_kind; }
    void setKind(ShapeKind kind) { m_kind = kind; }

    QColor fillColor() const { return m_fill; }
    void setFillColor(const QColor &color) { m_fill = color; }

    QColor strokeColor() const { return m_stroke; }
    void setStrokeColor(const QColor &color) { m_stroke = color; }

    double strokeWidthMm() const { return m_strokeWidthMm; }
    void setStrokeWidthMm(double mm);

    double cornerRadiusMm() const { return m_cornerRadiusMm; }
    void setCornerRadiusMm(double mm);

    // Number of sides (Polygon) or points (Star/Triangle).
    int sides() const { return m_sides; }
    void setSides(int sides);

    // Inner/outer radius ratio for Star.
    double starInnerRatio() const { return m_starInnerRatio; }
    void setStarInnerRatio(double ratio);

    // The shape outline in local millimetres (0,0 .. width,height).
    QPainterPath path() const;

protected:
    void paintObject(QPainter &painter, const RenderContext &ctx) const override;
    QJsonObject propertiesToJson() const override;
    bool propertiesFromJson(const QJsonObject &json, QString *error) override;
    QString defaultName() const override;
    CardObjectPtr cloneImpl() const override;

private:
    ShapeKind m_kind = ShapeKind::Rectangle;
    QColor    m_fill = QColor(200, 210, 220, 255);
    QColor    m_stroke = QColor(60, 70, 80, 255);
    double    m_strokeWidthMm = 0.3;
    double    m_cornerRadiusMm = 2.0;
    int       m_sides = 6;
    double    m_starInnerRatio = 0.45;
};

} // namespace occ
