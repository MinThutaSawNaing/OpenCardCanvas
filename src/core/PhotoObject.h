#pragma once

#include "core/ImageObject.h"

#include <QPainterPath>

namespace occ {

// ---------------------------------------------------------------------------
// PhotoObject - the ID card photograph element.
//
// Adds cropping (rectangular, rounded or circular), an optional border and an
// optional locked aspect ratio. The photograph itself is preserved exactly:
// no face generation, no retouching, no re-encoding - only the sampling window
// and the mask change.
// ---------------------------------------------------------------------------
class PhotoObject : public ImageObject
{
public:
    PhotoObject();

    CropShape cropShape() const { return m_cropShape; }
    void setCropShape(CropShape shape) { m_cropShape = shape; }

    // Corner radius used by CropShape::RoundedRectangle.
    double cornerRadiusMm() const { return m_cornerRadiusMm; }
    void setCornerRadiusMm(double mm);

    // Border drawn inside the object frame, so enabling it never changes the
    // layout of the card.
    double borderWidthMm() const { return m_borderWidthMm; }
    void setBorderWidthMm(double mm);
    QColor borderColor() const { return m_borderColor; }
    void setBorderColor(const QColor &color) { m_borderColor = color; }

    // 0 means "free"; a positive value locks the width:height ratio while
    // resizing on the canvas.
    double lockedAspectRatio() const { return m_lockedAspect; }
    void setLockedAspectRatio(double ratio);

    // The clip path in local millimetres, used by the renderer and by hit tests.
    QPainterPath shapePath() const;

    // Convenience used by the Photo tool: makes the object square and circular.
    void applyCircularCrop();

protected:
    void paintObject(QPainter &painter, const RenderContext &ctx) const override;
    QJsonObject propertiesToJson() const override;
    bool propertiesFromJson(const QJsonObject &json, QString *error) override;
    QString defaultName() const override;
    CardObjectPtr cloneImpl() const override;

private:
    void drawBorder(QPainter &painter) const;

    CropShape m_cropShape = CropShape::Rectangle;
    double    m_cornerRadiusMm = 3.0;
    double    m_borderWidthMm = 0.0;
    QColor    m_borderColor = QColor(Qt::black);
    double    m_lockedAspect = 0.0;
};

} // namespace occ
