#pragma once

#include "core/CardObject.h"

#include <QVector>

#include <vector>

namespace occ {

// ---------------------------------------------------------------------------
// GroupObject - a container that behaves as a single object.
//
// Children keep their coordinates in CARD millimetres. The group applies its own
// move / rotate transform to them, which is why grouping, ungrouping, moving and
// rotating a group are all lossless: no child geometry is rewritten.
// ---------------------------------------------------------------------------
class GroupObject : public CardObject
{
public:
    GroupObject();

    int childCount() const { return int(m_children.size()); }
    const QVector<CardObject *> children() const;
    CardObject *child(const ObjectId &id) const;

    // Takes ownership. The child's own coordinates stay in card millimetres.
    void addChild(CardObjectPtr child);
    CardObjectPtr takeChild(const ObjectId &id);
    // Removes and returns every child (used by Ungroup).
    // Returns std::vector for the reason documented in CardSide::takeAll():
    // QVector cannot hold a move-only element type on this toolchain.
    std::vector<CardObjectPtr> takeAllChildren();

    // Recomputes the group rectangle as the union of the children's rotated
    // bounding boxes. Must be called after a child is edited.
    void syncBounds();

    // Scales every child so the group's content follows a resize of the group
    // frame. `from` is the frame before the resize.
    void scaleChildrenFrom(const QRectF &fromRect);

protected:
    void paintObject(QPainter &painter, const RenderContext &ctx) const override;
    QJsonObject propertiesToJson() const override;
    bool propertiesFromJson(const QJsonObject &json, QString *error) override;
    QString defaultName() const override;
    CardObjectPtr cloneImpl() const override;

private:
    std::vector<CardObjectPtr> m_children;
};

} // namespace occ
