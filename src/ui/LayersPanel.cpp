#include "ui/LayersPanel.h"

#include "canvas/CardCanvas.h"
#include "commands/UndoCommands.h"
#include "core/CardDocument.h"
#include "core/CardSide.h"
#include "ui/IconFactory.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QMenu>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTreeWidget>
#include <QUndoStack>
#include <QVBoxLayout>

namespace occ {

// ---------------------------------------------------------------------------
// LayerTreeWidget routes viewport input to the panel and exposes base handlers
// for the standard selection, rename and row-move behaviour.
//
// The panel needs to fall through to the standard behaviour (start a rename,
// let the drag-and-drop machinery move the row) but it is not derived from the
// view, and a protected virtual cannot be called on another object. Two small
// forwarders solve that without duplicating any of Qt's behaviour. Overrides
// are essential: events belong to the tree, not its parent LayersPanel.
// ---------------------------------------------------------------------------
class LayerTreeWidget : public QTreeWidget
{
public:
    explicit LayerTreeWidget(LayersPanel *panel) : QTreeWidget(panel), m_panel(panel) {}

    void forwardMousePress(QMouseEvent *event) { QTreeWidget::mousePressEvent(event); }
    void forwardMouseDoubleClick(QMouseEvent *event)
    {
        QTreeWidget::mouseDoubleClickEvent(event);
    }
    void forwardDrop(QDropEvent *event) { QTreeWidget::dropEvent(event); }
    // A drop ONTO a row would nest the object; a flat layer list has no nesting.
    bool dropIsOnItem() const
    {
        return dropIndicatorPosition() == QAbstractItemView::OnItem;
    }
protected:
    void mousePressEvent(QMouseEvent *event) override { m_panel->mousePressEvent(event); }
    void mouseDoubleClickEvent(QMouseEvent *event) override { m_panel->mouseDoubleClickEvent(event); }
    void dropEvent(QDropEvent *event) override { m_panel->dropEvent(event); }
    void contextMenuEvent(QContextMenuEvent *event) override { m_panel->contextMenuEvent(event); }
private:
    LayersPanel *m_panel;
};

namespace {

constexpr int kColVisible = 0;
constexpr int kColLocked  = 1;
constexpr int kColName    = 2;

// Item data roles.
constexpr int kRoleId   = Qt::UserRole + 1;
constexpr int kRoleName = Qt::UserRole + 2;

QString iconNameFor(ObjectType type)
{
    switch (type) {
    case ObjectType::Text:    return QStringLiteral("text");
    case ObjectType::Image:   return QStringLiteral("image");
    case ObjectType::Photo:   return QStringLiteral("photo");
    case ObjectType::Shape:   return QStringLiteral("shape");
    case ObjectType::QrCode:  return QStringLiteral("qr");
    case ObjectType::Barcode: return QStringLiteral("barcode");
    case ObjectType::Group:   return QStringLiteral("group");
    }
    return QStringLiteral("select");
}

} // namespace

LayersPanel::LayersPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("LayersPanel"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tree = new LayerTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("LayersTree"));
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels(QStringList{ QString(), QString(), tr("Layer") });
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked
                            | QAbstractItemView::EditKeyPressed);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setMinimumWidth(180);
    m_tree->header()->setSectionResizeMode(kColVisible, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(kColLocked, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    // The rail columns are checkboxes, nothing else; they get no text.
    m_tree->headerItem()->setToolTip(kColVisible, tr("Visibility"));
    m_tree->headerItem()->setToolTip(kColLocked, tr("Locked"));
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &LayersPanel::onTreeSelectionChanged);
    connect(m_tree, &QTreeWidget::itemChanged, this, &LayersPanel::onItemChanged);
}

void LayersPanel::setDocument(CardDocument *document)
{
    if (m_document == document)
        return;
    if (m_document)
        m_document->disconnect(this);
    m_document = document;
    if (m_document) {
        connect(m_document, &CardDocument::contentsChanged, this, &LayersPanel::refresh);
        connect(m_document, &CardDocument::sideChanged, this, [this](CardSideId) { refresh(); });
        connect(m_document, &CardDocument::geometryChanged, this, &LayersPanel::refresh);
    }
    refresh();
}

void LayersPanel::setCanvas(CardCanvas *canvas)
{
    if (m_canvas == canvas)
        return;
    if (m_canvas)
        m_canvas->disconnect(this);
    m_canvas = canvas;
    if (m_canvas) {
        connect(m_canvas, &CardCanvas::selectionChanged,
                this, &LayersPanel::onCanvasSelectionChanged);
        connect(m_canvas, &CardCanvas::currentSideChanged,
                this, [this](CardSideId) { refresh(); });
    }
    refresh();
}

bool LayersPanel::isRailColumn(int column) const
{
    return column == kColVisible || column == kColLocked;
}

void LayersPanel::refresh()
{
    if (!m_document) {
        m_updating = true;
        m_tree->clear();
        m_updating = false;
        return;
    }

    const CardSideId side = m_canvas ? m_canvas->currentSide() : CardSideId::Front;
    const CardSide &cardSide = m_document->side(side);

    m_updating = true;
    m_tree->clear();

    // Top layer first: the document stores paint order, the user reads the
    // stack from the front of the card backwards.
    const QVector<CardObject *> objects = cardSide.objects();
    for (auto it = objects.crbegin(); it != objects.crend(); ++it) {
        CardObject *object = *it;
        auto *item = new QTreeWidgetItem(m_tree);
        item->setData(0, kRoleId, QVariant::fromValue(object->id()));
        item->setData(0, kRoleName, object->name());

        item->setCheckState(kColVisible, object->isVisible() ? Qt::Checked : Qt::Unchecked);
        item->setCheckState(kColLocked, object->isLocked() ? Qt::Checked : Qt::Unchecked);
        item->setIcon(kColName, IconFactory::icon(iconNameFor(object->type())));
        item->setText(kColName, object->name().isEmpty() ? object->typeDisplayName()
                                                        : object->name());
        item->setToolTip(kColName, tr("%1 - %2").arg(object->typeDisplayName(), object->name()));
        item->setToolTip(kColVisible, tr("Show or hide this layer"));
        item->setToolTip(kColLocked, tr("Lock or unlock this layer"));

        // The rail columns are toggles, not text: an editable checkbox column
        // would let a stray keystroke rename nothing.
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable
                       | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
        item->setTextAlignment(kColName, Qt::AlignLeft | Qt::AlignVCenter);

        if (object->isLocked())
            item->setForeground(kColName, QBrush(QColor(150, 150, 150)));
        if (!object->isVisible()) {
            QFont f = item->font(kColName);
            f.setItalic(true);
            item->setFont(kColName, f);
        }
    }

    // Restore the selection without notifying the canvas (the canvas is the
    // source of truth for the selection, not the tree).
    if (m_canvas) {
        const QVector<ObjectId> ids = m_canvas->selectionIds();
        QTreeWidgetItem *current = nullptr;
        for (const ObjectId &id : ids) {
            if (QTreeWidgetItem *item = itemFor(id)) {
                item->setSelected(true);
                if (!current)
                    current = item;
            }
        }
        if (current)
            m_tree->setCurrentItem(current, kColName,
                                   QItemSelectionModel::NoUpdate);
    }
    m_updating = false;
}

QTreeWidgetItem *LayersPanel::itemFor(const ObjectId &id) const
{
    const int count = m_tree->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->data(0, kRoleId).value<ObjectId>() == id)
            return item;
    }
    return nullptr;
}

QVector<ObjectId> LayersPanel::idsTopFirst() const
{
    QVector<ObjectId> ids;
    const int count = m_tree->topLevelItemCount();
    ids.reserve(count);
    for (int i = 0; i < count; ++i)
        ids.append(m_tree->topLevelItem(i)->data(0, kRoleId).value<ObjectId>());
    return ids;
}

void LayersPanel::onCanvasSelectionChanged()
{
    if (m_updating || !m_canvas)
        return;

    m_updating = true;
    const QVector<ObjectId> ids = m_canvas->selectionIds();

    m_tree->clearSelection();
    QTreeWidgetItem *current = nullptr;
    for (const ObjectId &id : ids) {
        if (QTreeWidgetItem *item = itemFor(id)) {
            item->setSelected(true);
            if (!current)
                current = item;
        }
    }
    if (current)
        m_tree->setCurrentItem(current, kColName, QItemSelectionModel::NoUpdate);
    else
        m_tree->setCurrentItem(nullptr);
    m_updating = false;
}

void LayersPanel::onTreeSelectionChanged()
{
    if (m_updating || !m_canvas)
        return;

    QVector<ObjectId> ids;
    const QList<QTreeWidgetItem *> items = m_tree->selectedItems();
    ids.reserve(items.size());
    for (QTreeWidgetItem *item : items)
        ids.append(item->data(0, kRoleId).value<ObjectId>());

    m_updating = true;
    m_canvas->setSelection(ids);
    if (!ids.isEmpty())
        m_canvas->revealObject(ids.first());
    m_updating = false;
}

void LayersPanel::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_updating || !m_document || !item)
        return;

    const ObjectId id = item->data(0, kRoleId).value<ObjectId>();
    if (id.isNull())
        return;

    if (column == kColVisible) {
        const bool visible = item->checkState(kColVisible) == Qt::Checked;
        mutateObject(id,
                     [visible](CardObject &object) { object.setVisible(visible); },
                     visible ? tr("Show layer") : tr("Hide layer"));
        emit statusMessage(visible ? tr("Layer shown.") : tr("Layer hidden."));
    } else if (column == kColLocked) {
        const bool locked = item->checkState(kColLocked) == Qt::Checked;
        mutateObject(id,
                     [locked](CardObject &object) { object.setLocked(locked); },
                     locked ? tr("Lock layer") : tr("Unlock layer"));
        emit statusMessage(locked ? tr("Layer locked.") : tr("Layer unlocked."));
    } else if (column == kColName) {
        const QString before = item->data(0, kRoleName).toString();
        const QString after = item->text(kColName);
        if (before == after)
            return;
        if (after.isEmpty()) {
            // An unnamed layer is not allowed: put the old name back rather
            // than silently renaming to nothing.
            m_updating = true;
            item->setText(kColName, before);
            m_updating = false;
            emit statusMessage(tr("A layer name cannot be empty."));
            return;
        }
        if (m_canvas && m_canvas->undoStack()) {
            m_canvas->undoStack()->push(new RenameObjectCommand(
                m_document, m_canvas->currentSide(), id, before, after));
            // The command emits contentsChanged and refresh() destroys this
            // item. The rebuilt row already contains the new name.
        }
        emit statusMessage(tr("Layer renamed to \"%1\".").arg(after));
    }
}

void LayersPanel::mutateObject(const ObjectId &id,
                               const std::function<void(CardObject &)> &mutate,
                               const QString &text)
{
    if (!m_document || !m_canvas || !m_canvas->undoStack())
        return;

    const CardSideId side = m_canvas->currentSide();
    CardSide &cardSide = m_document->side(side);
    CardObject *object = cardSide.object(id);
    if (!object)
        return;

    const QVector<ObjectId> ids{ id };
    const QVector<QPair<ObjectId, QJsonObject>> before =
        ModifyObjectsCommand::captureState(cardSide, ids);
    mutate(*object);
    const QVector<QPair<ObjectId, QJsonObject>> after =
        ModifyObjectsCommand::captureState(cardSide, ids);
    if (before == after)
        return;

    m_canvas->undoStack()->push(
        new ModifyObjectsCommand(m_document, side, before, after, text));
}

void LayersPanel::toggleVisibility(const ObjectId &id)
{
    if (!m_document || !m_canvas)
        return;
    CardObject *object = m_document->side(m_canvas->currentSide()).object(id);
    if (!object)
        return;
    const bool visible = !object->isVisible();
    mutateObject(id, [visible](CardObject &o) { o.setVisible(visible); },
                 visible ? tr("Show layer") : tr("Hide layer"));
    emit statusMessage(visible ? tr("Layer shown.") : tr("Layer hidden."));
}

void LayersPanel::toggleLock(const ObjectId &id)
{
    if (!m_document || !m_canvas)
        return;
    CardObject *object = m_document->side(m_canvas->currentSide()).object(id);
    if (!object)
        return;
    const bool locked = !object->isLocked();
    mutateObject(id, [locked](CardObject &o) { o.setLocked(locked); },
                 locked ? tr("Lock layer") : tr("Unlock layer"));
    emit statusMessage(locked ? tr("Layer locked.") : tr("Layer unlocked."));
}

void LayersPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (QTreeWidgetItem *item = m_tree->itemAt(event->pos())) {
            const int column = m_tree->columnAt(event->pos().x());
            if (isRailColumn(column)) {
                // The eye and the lock are toggles: they must not disturb the
                // selection, so the base implementation (which would change it)
                // is deliberately not called.
                const ObjectId id = item->data(0, kRoleId).value<ObjectId>();
                if (column == kColVisible)
                    toggleVisibility(id);
                else
                    toggleLock(id);
                event->accept();
                return;
            }
        }
    }
    m_tree->forwardMousePress(event);
}

void LayersPanel::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        m_tree->forwardMouseDoubleClick(event);
        return;
    }
    if (QTreeWidgetItem *item = m_tree->itemAt(event->pos())) {
        const int column = m_tree->columnAt(event->pos().x());
        if (column == kColVisible) {
            // The first press already toggled this rail. A double-click must
            // not immediately reverse that change.
            event->accept();
            return;
        }
        if (column == kColLocked) {
            event->accept();
            return;
        }
        // On the name column the base class starts the in-place rename.
    }
    m_tree->forwardMouseDoubleClick(event);
}

void LayersPanel::dropEvent(QDropEvent *event)
{
    if (!m_document || !m_canvas || event->source() != m_tree) {
        event->ignore();
        return;
    }
    // Re-parenting makes no sense for a flat layer list, so a drop ONTO a row
    // is rejected instead of quietly nesting the object.
    if (m_tree->dropIsOnItem()) {
        event->ignore();
        return;
    }

    const CardSideId side = m_canvas->currentSide();
    const QVector<ObjectId> before = m_document->side(side).objectIds();

    // Qt changes row selection while moving items. Keep that transient state
    // from clearing the canvas selection before the command rebuilds the list.
    m_updating = true;
    m_tree->forwardDrop(event);
    m_updating = false;

    // The tree now shows top-first order; the document wants bottom-first.
    const QVector<ObjectId> topFirst = idsTopFirst();
    QVector<ObjectId> after;
    after.reserve(topFirst.size());
    for (auto it = topFirst.crbegin(); it != topFirst.crend(); ++it)
        after.append(*it);

    if (before != after && before.size() == after.size() && m_canvas->undoStack()) {
        m_canvas->undoStack()->push(new ReorderObjectsCommand(
            m_document, side, before, after, tr("Reorder layers")));
        emit statusMessage(tr("Layers reordered."));
    }

    // Whatever happened, the list is rebuilt from the document so the two can
    // never disagree, even if the command was refused.
    refresh();
}

void LayersPanel::contextMenuEvent(QContextMenuEvent *event)
{
    buildContextMenu(event->pos());
    event->accept();
}

void LayersPanel::buildContextMenu(const QPoint &viewPos)
{
    if (!m_document || !m_canvas)
        return;

    QTreeWidgetItem *item = m_tree->itemAt(viewPos);
    if (!item)
        return;

    // Right-clicking a row that is not selected selects it first: acting on a
    // different object than the one under the cursor is never what was meant.
    const ObjectId id = item->data(0, kRoleId).value<ObjectId>();
    if (!item->isSelected()) {
        m_updating = true;
        m_tree->clearSelection();
        item->setSelected(true);
        m_tree->setCurrentItem(item, kColName, QItemSelectionModel::NoUpdate);
        m_updating = false;
        m_canvas->setSelection(QVector<ObjectId>{ id });
    }

    CardObject *object = m_document->side(m_canvas->currentSide()).object(id);
    if (!object)
        return;

    const int selectionCount = m_canvas->selectionCount();
    const bool isGroup = object->type() == ObjectType::Group;

    QMenu menu(this);
    QAction *rename = menu.addAction(IconFactory::icon(QStringLiteral("text")), tr("Rename"));
    menu.addSeparator();
    QAction *bringFront = menu.addAction(IconFactory::icon(QStringLiteral("bring_front")),
                                         tr("Bring to Front"));
    QAction *bringForward = menu.addAction(IconFactory::icon(QStringLiteral("bring_forward")),
                                           tr("Bring Forward"));
    QAction *sendBackward = menu.addAction(IconFactory::icon(QStringLiteral("send_backward")),
                                           tr("Send Backward"));
    QAction *sendBack = menu.addAction(IconFactory::icon(QStringLiteral("send_back")),
                                       tr("Send to Back"));
    menu.addSeparator();
    QAction *duplicate = menu.addAction(IconFactory::icon(QStringLiteral("duplicate")),
                                        tr("Duplicate"));
    QAction *remove = menu.addAction(IconFactory::icon(QStringLiteral("delete")), tr("Delete"));
    menu.addSeparator();
    QAction *group = menu.addAction(IconFactory::icon(QStringLiteral("group")), tr("Group"));
    QAction *ungroup = menu.addAction(IconFactory::icon(QStringLiteral("ungroup")), tr("Ungroup"));
    menu.addSeparator();
    QAction *lock = menu.addAction(IconFactory::icon(QStringLiteral("lock")), tr("Lock"));
    QAction *unlock = menu.addAction(IconFactory::icon(QStringLiteral("unlock")), tr("Unlock"));
    QAction *hide = menu.addAction(IconFactory::icon(QStringLiteral("hidden")), tr("Hide"));
    QAction *show = menu.addAction(IconFactory::icon(QStringLiteral("visible")), tr("Show"));

    // Only offer what the selection can actually do.
    group->setEnabled(selectionCount >= 2);
    ungroup->setEnabled(isGroup);
    lock->setEnabled(!object->isLocked());
    unlock->setEnabled(object->isLocked());
    hide->setEnabled(object->isVisible());
    show->setEnabled(!object->isVisible());

    QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(viewPos));
    if (!chosen)
        return;

    if (chosen == rename)
        m_tree->editItem(item, kColName);
    else if (chosen == bringFront)
        m_canvas->bringToFront();
    else if (chosen == bringForward)
        m_canvas->bringForward();
    else if (chosen == sendBackward)
        m_canvas->sendBackward();
    else if (chosen == sendBack)
        m_canvas->sendToBack();
    else if (chosen == duplicate)
        m_canvas->duplicateSelection();
    else if (chosen == remove)
        m_canvas->deleteSelection();
    else if (chosen == group)
        m_canvas->groupSelection();
    else if (chosen == ungroup)
        m_canvas->ungroupSelection();
    else if (chosen == lock)
        m_canvas->setSelectionLocked(true);
    else if (chosen == unlock)
        m_canvas->setSelectionLocked(false);
    else if (chosen == hide)
        m_canvas->setSelectionVisible(false);
    else if (chosen == show)
        m_canvas->setSelectionVisible(true);
}

} // namespace occ
