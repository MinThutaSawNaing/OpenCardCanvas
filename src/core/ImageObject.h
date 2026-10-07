#pragma once

#include "core/CardObject.h"

#include <QColor>
#include <QRectF>

namespace occ {

// ---------------------------------------------------------------------------
// ImageObject - a raster image placed on the card.
//
// Nothing about the source image is ever modified: cropping is stored as a
// normalised source rectangle and every transform is applied at render time.
// Replacing the file behind an asset therefore keeps the design intact.
// ---------------------------------------------------------------------------
class ImageObject : public CardObject
{
public:
    ImageObject();

    QString assetId() const { return m_assetId; }
    void setAssetId(const QString &assetId) { m_assetId = assetId; }

    ImageFitMode fitMode() const { return m_fitMode; }
    void setFitMode(ImageFitMode mode) { m_fitMode = mode; }

    // Normalised source rectangle, (0,0,1,1) meaning "the whole image".
    // Non-destructive crop.
    QRectF sourceRect() const { return m_sourceRect; }
    void setSourceRect(const QRectF &rect);
    void resetCrop() { m_sourceRect = QRectF(0.0, 0.0, 1.0, 1.0); }
    bool isCropped() const;

    // Used behind images drawn with Contain / Center / Tile.
    QColor backgroundFill() const { return m_background; }
    void setBackgroundFill(const QColor &color) { m_background = color; }

    // DPI assumed when an image has no physical resolution metadata.
    static int assumedDpi();

protected:
    // Used by PhotoObject, which is an image with additional decoration.
    ImageObject(ObjectType type, const ObjectId &id);

    void paintObject(QPainter &painter, const RenderContext &ctx) const override;
    QJsonObject propertiesToJson() const override;
    bool propertiesFromJson(const QJsonObject &json, QString *error) override;
    QString defaultName() const override;
    CardObjectPtr cloneImpl() const override;

    QString  m_assetId;
    ImageFitMode m_fitMode = ImageFitMode::Cover;
    QRectF   m_sourceRect{0.0, 0.0, 1.0, 1.0};
    QColor   m_background = QColor(0, 0, 0, 0);
};

} // namespace occ
