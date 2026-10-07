#pragma once

#include "canvas/CanvasGeometry.h"
#include "canvas/GuideModel.h"
#include "canvas/SnapEngine.h"
#include "core/CardDocument.h"
#include "core/CardGeometry.h"
#include "core/CardTypes.h"
#include "core/Units.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

class QUndoStack;

// ---------------------------------------------------------------------------
// CardCanvas - the design surface.
//
// Rendering rule, and the reason this class is small for what it does: the
// document is NEVER painted object by object here. The whole side is rendered
// once through CardRenderer::renderSide() into a cached QImage, and that image
// is blitted. Selection chrome, handles, guides, the grid and the alignment
// lines are then drawn on top in widget coordinates, which is the only part
// that has to be fast because it changes on every mouse move.
//
// Consequences, all of them intended:
//   * the canvas cannot disagree with the printout, because it uses the same
//     renderer as the printout
//   * the cache is only invalidated when the document or the zoom changes
//   * a 300 dpi card on a 4K screen costs one image per change, not one paint
//     call per object per mouse move
//
// Undo: every edit goes through a QUndoCommand (see commands/UndoCommands.h),
// so the canvas has no history of its own and Ctrl+Z is simply the stack.
// ---------------------------------------------------------------------------
class QTextEdit;

namespace occ {

class CardCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit CardCanvas(QWidget *parent = nullptr);
    ~CardCanvas() override;

    // --- document -----------------------------------------------------------
    void setDocument(CardDocument *doc);
    CardDocument *document() const { return m_document; }

    CardSideId currentSide() const { return m_side; }
    void setCurrentSide(CardSideId side);
    void showFront() { setCurrentSide(CardSideId::Front); }
    void showBack() { setCurrentSide(CardSideId::Back); }

    // --- zoom ---------------------------------------------------------------
    double zoomPercent() const { return m_zoomPercent; }
    void setZoomPercent(double percent);
    void zoomIn();
    void zoomOut();
    void fitToWindow();
    // Logical pixels per millimetre currently in use.
    double pxPerMm() const;

    // --- display ------------------------------------------------------------
    void setUnitDisplay(DisplayUnit unit);
    DisplayUnit unitDisplay() const { return m_unit; }

    void setShowGrid(bool on);
    bool showGrid() const { return m_showGrid; }
    void setShowGuides(bool on);
    bool showGuides() const { return m_showGuides; }
    void setShowRulers(bool on);
    bool showRulers() const { return m_showRulers; }
    void setShowPrintMargins(bool on);
    bool showPrintMargins() const { return m_showPrintMargins; }

    // --- snapping -----------------------------------------------------------
    void setSnapOptions(const SnapEngine::Options &options);
    SnapEngine::Options snapOptions() const { return m_snap; }

    // --- guides -------------------------------------------------------------
    GuideModel *guides() const { return m_guides; }
    void setGuideModel(GuideModel *model);

    // --- undo ---------------------------------------------------------------
    QUndoStack *undoStack() const { return m_undoStack; }
    void setUndoStack(QUndoStack *stack);

    // --- selection ----------------------------------------------------------
    QVector<CardObject *> selection() const;
    QVector<ObjectId>     selectionIds() const;
    void selectObject(const ObjectId &id, bool additive = false);
    void setSelection(const QVector<ObjectId> &ids);
    void selectAll();
    void clearSelection();
    bool isSelected(const ObjectId &id) const;
    int  selectionCount() const { return int(m_selection.size()); }

    // Scrolls the view so `id` is visible.
    void revealObject(const ObjectId &id);

signals:
    void selectionChanged();
    void documentModified();
    void zoomChanged(double percent);
    void mousePositionChanged(const QPointF &mm);
    void statusMessage(const QString &message);
    void currentSideChanged(occ::CardSideId side);
    // Emitted when the canvas wants the host to refresh the rulers.
    void viewChanged();

public:
    // --- editing entry points (all create undo commands) --------------------
    void beginTextEditing(const ObjectId &id);
    void commitTextEditing();
    void cancelTextEditing();
    bool isEditingText() const { return !m_editing.isNull(); }

    void deleteSelection();
    void duplicateSelection();
    void copySelection();
    void cutSelection();
    void pasteClipboard();
    void nudgeSelection(double dxMm, double dyMm);

    // --- object insertion ---------------------------------------------------
    void addTextObject();
    void addImageFromFile(const QString &path);
    void addPhotoFromFile(const QString &path);
    void addShape(ShapeKind kind);
    void addQrObject();
    void addBarcodeObject();

    // --- z-order / grouping -------------------------------------------------
    void bringToFront();
    void bringForward();
    void sendBackward();
    void sendToBack();
    void groupSelection();
    void ungroupSelection();
    void setSelectionVisible(bool visible);
    void setSelectionLocked(bool locked);

    // --- alignment / distribution -------------------------------------------
    // AlignLeft / AlignHCenter / AlignRight / AlignTop / AlignVCenter /
    // AlignBottom. With one object selected it aligns against the card.
    void alignSelection(Qt::Alignment flag);
    void distributeSelectionHorizontally();
    void distributeSelectionVertically();

    // --- view helpers -------------------------------------------------------
    QPointF originPx() const;
    QRectF  cardRectInView() const;
    QRectF  viewportRectInView() const;
    // Scroll offset of the enclosing scroll area, pushed in by CanvasHost. Kept
    // as state rather than asked of parentWidget() so the widget also works
    // stand alone.
    void setScrollOffsets(const QPoint &offsetPx);
    QPoint scrollOffsets() const { return m_scroll; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onDocumentChanged();
    void onDocumentSideChanged(occ::CardSideId side);
    void onGeometryChanged();
    void onGuidesChanged(occ::CardSideId side);

private:
    // --- rendering ----------------------------------------------------------
    void invalidateRenderCache();
    void ensureRenderCache();
    void paintChrome(QPainter &painter);
    void paintGrid(QPainter &painter);
    void paintGuides(QPainter &painter);
    void paintSelection(QPainter &painter, const CardObject &obj);
    void paintHandles(QPainter &painter, const CardObject &obj);
    void paintAlignmentLines(QPainter &painter);
    void paintLockedIndicator(QPainter &painter, const CardObject &obj);

    // --- interaction --------------------------------------------------------
    enum class DragMode { None, Marquee, Move, Resize, Rotate, Pan, Guide };
    void beginDrag(const QPointF &viewPos, Qt::KeyboardModifiers modifiers);
    void updateDrag(const QPointF &viewPos, Qt::KeyboardModifiers modifiers);
    void finishDrag();
    void updateCursorFor(const QPointF &viewPos);
    void applyResize(const QPointF &viewPosMm, Qt::KeyboardModifiers modifiers);
    void applyRotate(const QPointF &viewPosMm, Qt::KeyboardModifiers modifiers);
    // Moves every selected object by the same delta, so the layout of a block is
    // preserved exactly.
    void applyMoveToSelection(const QPointF &deltaMm);
    void commitActiveDrag();

    // --- helpers ------------------------------------------------------------
    CardSide &currentSideRef() const;
    const CardSide &currentSideConstRef() const;
    QVector<CardObject *> selectedObjectsInOrder() const;
    QRectF selectionBoundsMm() const;
    void snapshotDragStart();
    QJsonObject snapshotOf(const ObjectId &id) const;
    void pushModifyFromSnapshot(const QString &text, const QString &mergeKey);

    void setStatusMessage(const QString &message);
    // Inserts one object through the undo stack and selects it.
    void addObject(CardObjectPtr object, const QString &undoText);
    void updateTextEditorGeometry();
    void destroyTextEditor();
    void zoomAt(const QPoint &viewPos, double factor);
    void applyZoomKeepingPoint(double newZoom, const QPoint &anchorPx);
    QPointF clampToCard(const QPointF &topLeftMm, const QSizeF &sizeMm) const;

public:
    // Guide dragging brought in from the rulers by CanvasHost, and guide creation
    // from a ruler double-click.
    void startGuideDrag(bool horizontal, double posMm);
    void moveGuideDrag(bool horizontal, double posMm);
    void finishGuideDrag(bool horizontal, double posMm);
    void addGuide(bool horizontal, double posMm);

private:

    // --- state --------------------------------------------------------------
    CardDocument *m_document = nullptr;
    CardSideId    m_side = CardSideId::Front;
    GuideModel   *m_guides = nullptr;
    bool          m_ownsGuides = false;
    QUndoStack   *m_undoStack = nullptr;

    double      m_zoomPercent = 100.0;
    DisplayUnit m_unit = DisplayUnit::Millimeters;
    QPoint      m_scroll;             // scroll area offset, logical px

    bool   m_showGrid = false;
    bool   m_showGuides = true;
    bool   m_showRulers = true;
    bool   m_showPrintMargins = false;
    QColor m_gridColor = QColor(120, 140, 170, 60);
    QColor m_guideColor = QColor(0, 160, 200);
    QColor m_selectionColor = QColor(0, 120, 215);
    QColor m_lockedColor = QColor(150, 150, 150);

    SnapEngine::Options m_snap;

    QVector<ObjectId> m_selection;

    // Drag state.
    DragMode   m_dragMode = DragMode::None;
    Handle::Id m_activeHandle = Handle::None;
    QPointF    m_dragStartView;
    QPointF    m_dragStartMm;
    QRectF     m_dragStartRectMm;
    QHash<ObjectId, QRectF> m_dragStartRects;   // one per selected object
    double     m_dragStartRotation = 0.0;
    double     m_dragStartAngleDeg = 0.0;
    QRectF     m_marqueeRectMm;
    QPoint     m_panStartScroll;
    QPointF    m_panStartView;
    bool       m_spacePanning = false;
    SnapEngine::Result m_snapResult;
    QHash<ObjectId, QJsonObject> m_dragSnapshot;

    // Text editing overlay.
    ObjectId         m_editing;
    QTextEdit       *m_textEditor = nullptr;

    // Guide interaction.
    bool     m_guideHorizontal = true;
    double   m_guidePosMm = 0.0;
    ObjectId m_activeGuideId;
    bool     m_guideCreationPending = false;

    // Render cache.
    QImage m_renderCache;
    QSize  m_renderCacheSize;
    double m_renderCachePxPerMm = 0.0;
    bool   m_renderCacheValid = false;
};

} // namespace occ
