#pragma once

#include "core/CardTypes.h"

#include <QVector>
#include <QWidget>

#include <functional>

class QTreeWidget;
class QTreeWidgetItem;

// ---------------------------------------------------------------------------
// LayersPanel - the layer list of the side currently being edited.
//
// The list is TOP LAYER FIRST while the document stores objects bottom first.
// That is the one conversion this panel performs, and it performs it in exactly
// two places (build and drop), so the two orders cannot drift.
//
// Per row: a visibility checkbox, a lock checkbox, the object's type icon and
// its EDITABLE name.
//
// Selection is two-way: picking a row selects the object on the canvas and
// selecting on the canvas highlights the row. Neither direction is allowed to
// re-enter the other, so there is no feedback loop.
//
// Reordering by dragging pushes exactly ONE ReorderObjectsCommand, which is
// what makes Ctrl+Z put the stack back the way it was.
//
// The checkboxes do not change the selection and the double click toggles
// visibility: hiding a layer is a one-gesture operation, not a select-and-find-
// the-context-menu operation.
// ---------------------------------------------------------------------------
namespace occ {

class CardCanvas;
class CardDocument;
class CardObject;
class LayerTreeWidget;

class LayersPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LayersPanel(QWidget *parent = nullptr);

    void setDocument(CardDocument *document);
    void setCanvas(CardCanvas *canvas);
    // Rebuilds the list from the document. Cheap enough to call on every
    // document change; it is a handful of rows.
    void refresh();

signals:
    void statusMessage(const QString &message);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    friend class LayerTreeWidget;
    void onCanvasSelectionChanged();
    void onTreeSelectionChanged();
    void onItemChanged(QTreeWidgetItem *item, int column);
    void toggleVisibility(const ObjectId &id);
    void toggleLock(const ObjectId &id);
    void mutateObject(const ObjectId &id,
                      const std::function<void(CardObject &)> &mutate,
                      const QString &text);
    QVector<ObjectId> idsTopFirst() const;
    QTreeWidgetItem *itemFor(const ObjectId &id) const;
    bool isRailColumn(int column) const;
    void buildContextMenu(const QPoint &viewPos);

    CardDocument *m_document = nullptr;
    CardCanvas   *m_canvas = nullptr;
    LayerTreeWidget *m_tree = nullptr;

    // Guards both directions of the selection synchronisation and the change
    // signal while the tree is being rebuilt.
    bool m_updating = false;
};

} // namespace occ
