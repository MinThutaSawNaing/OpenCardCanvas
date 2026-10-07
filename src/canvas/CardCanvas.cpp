#include "canvas/CardCanvas.h"

#include "canvas/CanvasGeometry.h"
#include "commands/UndoCommands.h"
#include "core/BarcodeObject.h"
#include "core/GroupObject.h"
#include "core/ImageObject.h"
#include "core/ObjectFactory.h"
#include "core/PhotoObject.h"
#include "core/QrObject.h"
#include "core/ShapeObject.h"
#include "core/TextObject.h"
#include "project/AssetStore.h"
#include "rendering/CardRenderer.h"
#include "utils/TextUtils.h"

#include <QScrollArea>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextEdit>
#include <QUndoStack>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace occ {

namespace {

constexpr int kHandleSizePx = 8;
constexpr int kCanvasMarginPx = 24;
constexpr double kMarqueeThresholdPx = 4.0;

const QStringList &imageFileSuffixes()
{
    static const QStringList suffixes = { QStringLiteral("png"),  QStringLiteral("jpg"),
                                          QStringLiteral("jpeg"), QStringLiteral("bmp"),
                                          QStringLiteral("tif"),  QStringLiteral("tiff"),
                                          QStringLiteral("gif"),  QStringLiteral("webp"),
                                          QStringLiteral("svg") };
    return suffixes;
}

bool looksLikeImagePath(const QString &path)
{
    return imageFileSuffixes().contains(QFileInfo(path).suffix().toLower());
}

const double *zoomSteps(int *count)
{
    static const double steps[] = { 5.0,  10.0, 25.0, 50.0, 75.0,  100.0,
                                    125.0, 150.0, 200.0, 300.0, 400.0, 600.0,
                                    800.0, 1200.0, 1600.0, 2400.0, 3200.0 };
    *count = int(sizeof(steps) / sizeof(steps[0]));
    return steps;
}

} // namespace

CardCanvas::CardCanvas(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAutoFillBackground(false);

    m_guides = new GuideModel(this);
    m_ownsGuides = true;
    connect(m_guides, &GuideModel::changed, this, &CardCanvas::onGuidesChanged);

    m_unit = DisplayUnit::Millimeters;
}

CardCanvas::~CardCanvas()
{
    destroyTextEditor();
}

// ---------------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------------

void CardCanvas::setDocument(CardDocument *doc)
{
    if (m_document == doc) {
        invalidateRenderCache();
        update();
        return;
    }

    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }

    m_document = doc;
    clearSelection();
    destroyTextEditor();

    if (m_document) {
        connect(m_document, &CardDocument::contentsChanged, this,
                &CardCanvas::onDocumentChanged);
        connect(m_document, &CardDocument::sideChanged, this,
                &CardCanvas::onDocumentSideChanged);
        connect(m_document, &CardDocument::geometryChanged, this,
                &CardCanvas::onGeometryChanged);
    }

    invalidateRenderCache();
    update();
    emit viewChanged();
}

void CardCanvas::onDocumentChanged()
{
    invalidateRenderCache();
    emit documentModified();
    update();
}

void CardCanvas::onDocumentSideChanged(CardSideId side)
{
    if (side != m_side)
        return;
    invalidateRenderCache();
    update();
}

void CardCanvas::onGeometryChanged()
{
    invalidateRenderCache();
    update();
    emit viewChanged();
}

void CardCanvas::onGuidesChanged(CardSideId side)
{
    if (side != m_side)
        return;
    update();
    emit viewChanged();
}

void CardCanvas::setCurrentSide(CardSideId side)
{
    if (m_side == side)
        return;
    commitTextEditing();
    clearSelection();
    m_side = side;
    invalidateRenderCache();
    update();
    emit currentSideChanged(m_side);
    emit statusMessage(names::side(m_side) == QLatin1String("front")
                           ? tr("Editing the front of the card")
                           : tr("Editing the back of the card"));
}

CardSide &CardCanvas::currentSideRef() const
{
    Q_ASSERT(m_document);
    return m_document->side(m_side);
}

const CardSide &CardCanvas::currentSideConstRef() const
{
    Q_ASSERT(m_document);
    return m_document->side(m_side);
}

// ---------------------------------------------------------------------------
// Zoom and view
// ---------------------------------------------------------------------------

double CardCanvas::pxPerMm() const
{
    // 100 % is one physical millimetre, adjusted by the screen's device pixel
    // ratio so the card really is card sized on a calibrated display.
    return CanvasGeometry::pxPerMmForZoom(m_zoomPercent, devicePixelRatioF());
}

void CardCanvas::setZoomPercent(double percent)
{
    const double clamped = CanvasGeometry::clampZoom(percent);
    if (qFuzzyCompare(m_zoomPercent + 1.0, clamped + 1.0))
        return;
    m_zoomPercent = clamped;
    invalidateRenderCache();

    // The widget grows with the zoom; the scroll area does the panning.
    const CardGeometry &geom = m_document ? m_document->geometry()
                                          : CardGeometry::defaultGeometry();
    const double cardW = geom.widthMm() * pxPerMm() + 2 * kCanvasMarginPx;
    const double cardH = geom.heightMm() * pxPerMm() + 2 * kCanvasMarginPx;
    setMinimumSize(QSize(int(std::ceil(cardW)), int(std::ceil(cardH))));

    update();
    emit zoomChanged(m_zoomPercent);
    emit viewChanged();
}

void CardCanvas::zoomIn()
{
    int count = 0;
    const double *steps = zoomSteps(&count);
    for (int i = 0; i < count; ++i) {
        if (steps[i] > m_zoomPercent + 0.001) {
            setZoomPercent(steps[i]);
            return;
        }
    }
    setZoomPercent(CanvasGeometry::kMaxZoomPercent);
}

void CardCanvas::zoomOut()
{
    int count = 0;
    const double *steps = zoomSteps(&count);
    for (int i = count - 1; i >= 0; --i) {
        if (steps[i] < m_zoomPercent - 0.001) {
            setZoomPercent(steps[i]);
            return;
        }
    }
    setZoomPercent(CanvasGeometry::kMinZoomPercent);
}

void CardCanvas::fitToWindow()
{
    const CardGeometry &geom = m_document ? m_document->geometry()
                                          : CardGeometry::defaultGeometry();
    const QSize viewport = parentWidget() ? parentWidget()->size() : size();
    const QSizeF fitted = CanvasGeometry::fitZoomSize(
        geom, viewport, devicePixelRatioF(), kCanvasMarginPx);
    if (fitted.width() <= 0.0 || geom.widthMm() <= 0.0)
        return;
    const double pxPerMm = fitted.width() / geom.widthMm();
    setZoomPercent(CanvasGeometry::zoomForPxPerMm(pxPerMm, devicePixelRatioF()));
}

QPointF CardCanvas::originPx() const
{
    const CardGeometry &geom = m_document ? m_document->geometry()
                                          : CardGeometry::defaultGeometry();
    const double cardW = geom.widthMm() * pxPerMm();
    const double cardH = geom.heightMm() * pxPerMm();
    // Centred while there is room, and never closer than the margin to the edge.
    const double x = std::max(double(kCanvasMarginPx), (width() - cardW) / 2.0);
    const double y = std::max(double(kCanvasMarginPx), (height() - cardH) / 2.0);
    return QPointF(x, y);
}

QRectF CardCanvas::cardRectInView() const
{
    const CardGeometry &geom = m_document ? m_document->geometry()
                                          : CardGeometry::defaultGeometry();
    return CanvasGeometry::viewRectForMm(geom.boundsMm(), pxPerMm(), originPx());
}

QRectF CardCanvas::viewportRectInView() const
{
    return QRectF(QPointF(0.0, 0.0), QSizeF(size()));
}

void CardCanvas::setScrollOffsets(const QPoint &offsetPx)
{
    if (m_scroll == offsetPx)
        return;
    m_scroll = offsetPx;
    emit viewChanged();
}

void CardCanvas::setUnitDisplay(DisplayUnit unit)
{
    if (m_unit == unit)
        return;
    m_unit = unit;
    update();
    emit viewChanged();
}

void CardCanvas::setShowGrid(bool on)
{
    if (m_showGrid == on)
        return;
    m_showGrid = on;
    update();
}

void CardCanvas::setShowGuides(bool on)
{
    if (m_showGuides == on)
        return;
    m_showGuides = on;
    update();
}

void CardCanvas::setShowRulers(bool on)
{
    m_showRulers = on;
    emit viewChanged();
}

void CardCanvas::setShowPrintMargins(bool on)
{
    if (m_showPrintMargins == on)
        return;
    m_showPrintMargins = on;
    update();
}

void CardCanvas::setSnapOptions(const SnapEngine::Options &options)
{
    m_snap = options;
}

void CardCanvas::setGuideModel(GuideModel *model)
{
    if (m_guides == model)
        return;
    if (m_guides && m_ownsGuides) {
        disconnect(m_guides, nullptr, this, nullptr);
        m_guides->deleteLater();
    }
    m_guides = model;
    m_ownsGuides = false;
    if (m_guides) {
        connect(m_guides, &GuideModel::changed, this, &CardCanvas::onGuidesChanged);
    }
    update();
    emit viewChanged();
}

void CardCanvas::setUndoStack(QUndoStack *stack)
{
    m_undoStack = stack;
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

QVector<CardObject *> CardCanvas::selection() const
{
    QVector<CardObject *> out;
    if (!m_document)
        return out;
    const CardSide &side = m_document->side(m_side);
    for (const ObjectId &id : m_selection) {
        if (CardObject *object = side.object(id))
            out.append(object);
    }
    return out;
}

QVector<CardObject *> CardCanvas::selectedObjectsInOrder() const
{
    return selection();
}

QVector<ObjectId> CardCanvas::selectionIds() const
{
    return m_selection;
}

bool CardCanvas::isSelected(const ObjectId &id) const
{
    return m_selection.contains(id);
}

void CardCanvas::selectObject(const ObjectId &id, bool additive)
{
    if (!m_document)
        return;
    if (!additive) {
        if (m_selection.size() == 1 && m_selection.first() == id)
            return;
        m_selection.clear();
        m_selection.append(id);
    } else if (m_selection.contains(id)) {
        // Shift-clicking a selected object removes it again, which is how every
        // drawing application behaves.
        m_selection.removeAll(id);
    } else {
        m_selection.append(id);
    }
    update();
    emit selectionChanged();
}

void CardCanvas::setSelection(const QVector<ObjectId> &ids)
{
    if (m_selection == ids)
        return;
    m_selection = ids;
    update();
    emit selectionChanged();
}

void CardCanvas::selectAll()
{
    if (!m_document)
        return;
    m_selection = m_document->side(m_side).objectIds();
    update();
    emit selectionChanged();
}

void CardCanvas::clearSelection()
{
    if (m_selection.isEmpty())
        return;
    m_selection.clear();
    update();
    emit selectionChanged();
}

void CardCanvas::revealObject(const ObjectId &id)
{
    if (!m_document)
        return;
    CardObject *object = m_document->side(m_side).object(id);
    if (!object)
        return;

    // The canvas lives inside a scroll area (created by CanvasHost), so revealing
    // an object is a matter of asking that area to bring its rectangle into view.
    auto *scrollArea = qobject_cast<QScrollArea *>(
        parentWidget() ? parentWidget()->parentWidget() : nullptr);
    if (!scrollArea)
        return;

    const QRectF viewRect =
        CanvasGeometry::viewRectForMm(object->boundingRectMm(), pxPerMm(), originPx());
    const QRect wanted = viewRect.toAlignedRect();
    const int margin = int(kCanvasMarginPx * 2);
    scrollArea->ensureVisible(wanted.center().x(), wanted.center().y(), margin, margin);
    emit viewChanged();
}

QRectF CardCanvas::selectionBoundsMm() const
{
    QRectF bounds;
    bool first = true;
    for (const CardObject *object : selection()) {
        if (!object)
            continue;
        const QRectF box = object->boundingRectMm();
        bounds = first ? box : bounds.united(box);
        first = false;
    }
    return first ? QRectF() : bounds;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void CardCanvas::invalidateRenderCache()
{
    m_renderCacheValid = false;
}

void CardCanvas::ensureRenderCache()
{
    if (!m_document)
        return;

    const double pxPerMm = this->pxPerMm();
    const CardGeometry &geom = m_document->geometry();
    const double cardW = geom.widthMm() * pxPerMm;
    const double cardH = geom.heightMm() * pxPerMm;
    const QSize wanted(qMax(1, int(std::ceil(cardW))), qMax(1, int(std::ceil(cardH))));

    if (m_renderCacheValid && m_renderCacheSize == wanted
        && qFuzzyCompare(m_renderCachePxPerMm + 1.0, pxPerMm + 1.0)) {
        return;
    }

    // The document is rendered once through the single render path and then
    // blitted. Nothing in this widget paints an object itself, which is what makes
    // the canvas and the printout impossible to disagree.
    CardRenderer::Options options;
    options.pxPerMmOverride = pxPerMm;
    options.includeBleed = false;
    options.forPrinting = false;

    m_renderCache = CardRenderer::renderSide(*m_document, m_side, options, nullptr);
    m_renderCacheSize = wanted;
    m_renderCachePxPerMm = pxPerMm;
    m_renderCacheValid = !m_renderCache.isNull();
}

void CardCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(rect(), palette().window());

    if (!m_document)
        return;

    ensureRenderCache();

    const QPointF origin = originPx();
    const QRectF cardRect = cardRectInView();

    // A sheet of paper under the card, so the trim edge is visible even when the
    // design itself is white.
    painter.setPen(QPen(QColor(160, 165, 172), 1.0));
    painter.setBrush(Qt::white);
    painter.drawRect(cardRect);

    if (!m_renderCache.isNull()) {
        // The cached image is device pixels; draw it into the logical rectangle so
        // HiDPI screens stay sharp.
        painter.drawImage(cardRect, m_renderCache);
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    paintChrome(painter);
    Q_UNUSED(origin);
}

void CardCanvas::paintGrid(QPainter &painter)
{
    if (!m_showGrid || !m_document)
        return;

    const CardGeometry &geom = m_document->geometry();
    const QRectF cardRect = cardRectInView();
    const double spacingMm = m_snap.gridSpacingMm > 0.0 ? m_snap.gridSpacingMm : 5.0;
    const double pxPerMm = this->pxPerMm();
    if (spacingMm * pxPerMm < 4.0)
        return;     // too dense to be useful; drawing it would just be noise

    painter.save();
    painter.setClipRect(cardRect);
    painter.setPen(QPen(m_gridColor, 1.0));
    for (double x = spacingMm; x < geom.widthMm() - 1e-6; x += spacingMm) {
        const double px = cardRect.left() + x * pxPerMm;
        painter.drawLine(QPointF(px, cardRect.top()), QPointF(px, cardRect.bottom()));
    }
    for (double y = spacingMm; y < geom.heightMm() - 1e-6; y += spacingMm) {
        const double px = cardRect.top() + y * pxPerMm;
        painter.drawLine(QPointF(cardRect.left(), px), QPointF(cardRect.right(), px));
    }
    painter.restore();
}

void CardCanvas::paintGuides(QPainter &painter)
{
    if (!m_showGuides || !m_guides || !m_document)
        return;

    const QRectF cardRect = cardRectInView();
    const double pxPerMm = this->pxPerMm();
    painter.save();
    painter.setPen(QPen(m_guideColor, 1.0, Qt::DashLine));
    for (const GuideModel::Guide &guide : m_guides->guides(m_side)) {
        if (guide.horizontal) {
            const double px = cardRect.top() + guide.posMm * pxPerMm;
            painter.drawLine(QPointF(0.0, px), QPointF(width(), px));
        } else {
            const double px = cardRect.left() + guide.posMm * pxPerMm;
            painter.drawLine(QPointF(px, 0.0), QPointF(px, height()));
        }
    }

    // The guide being dragged out of a ruler, previewed until the user releases
    // the pointer and it becomes real.
    if (m_guideCreationPending) {
        painter.setPen(QPen(m_guideColor, 1.0, Qt::DotLine));
        if (m_guideHorizontal) {
            const double px = cardRect.top() + m_guidePosMm * pxPerMm;
            painter.drawLine(QPointF(0.0, px), QPointF(width(), px));
        } else {
            const double px = cardRect.left() + m_guidePosMm * pxPerMm;
            painter.drawLine(QPointF(px, 0.0), QPointF(px, height()));
        }
    }
    painter.restore();
}

void CardCanvas::paintSelection(QPainter &painter, const CardObject &obj)
{
    const double pxPerMm = this->pxPerMm();
    const QPointF origin = originPx();
    const QPolygonF outline = obj.outlineMm();

    QPolygonF viewOutline;
    for (const QPointF &point : outline)
        viewOutline.append(CanvasGeometry::mmToView(point, pxPerMm, origin));

    const bool locked = obj.isLocked();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(locked ? m_lockedColor : m_selectionColor, 1.0,
                        locked ? Qt::DashLine : Qt::SolidLine));
    painter.drawPolygon(viewOutline);
}

void CardCanvas::paintHandles(QPainter &painter, const CardObject &obj)
{
    const double pxPerMm = this->pxPerMm();
    const QPointF origin = originPx();
    const bool locked = obj.isLocked();

    painter.setPen(QPen(m_selectionColor, 1.0));
    painter.setBrush(Qt::white);
    for (const Handle &handle : CanvasGeometry::handlesFor(obj, pxPerMm, origin,
                                                           kHandleSizePx)) {
        const QPointF centre = CanvasGeometry::mmToView(handle.posMm, pxPerMm, origin);
        const double size = handle.id == Handle::Rotate ? kHandleSizePx
                                                        : kHandleSizePx - 1.0;
        if (handle.id == Handle::Rotate) {
            // Round for rotate, square for resize: the shape alone tells the user
            // what the handle does.
            painter.drawEllipse(centre, size / 2.0, size / 2.0);
        } else {
            painter.setBrush(locked ? m_lockedColor : QColor(Qt::white));
            painter.drawRect(QRectF(centre.x() - size / 2.0, centre.y() - size / 2.0, size,
                                    size));
        }
    }
}

void CardCanvas::paintLockedIndicator(QPainter &painter, const CardObject &obj)
{
    const double pxPerMm = this->pxPerMm();
    const QPointF origin = originPx();
    const QRectF box = CanvasGeometry::viewRectForMm(obj.boundingRectMm(), pxPerMm,
                                                    origin);
    painter.save();
    painter.setBrush(m_lockedColor);
    painter.setPen(Qt::NoPen);
    // A small square in the object's top-left corner: visible, and never mistaken
    // for part of the design.
    painter.drawRect(QRectF(box.left() - 3.0, box.top() - 3.0, 6.0, 6.0));
    painter.restore();
}

void CardCanvas::paintAlignmentLines(QPainter &painter)
{
    if (m_dragMode != DragMode::Move && m_dragMode != DragMode::Resize)
        return;
    if (!m_snapResult.snappedX && !m_snapResult.snappedY)
        return;

    const double pxPerMm = this->pxPerMm();
    const QPointF origin = originPx();
    const QRectF cardRect = cardRectInView();

    painter.save();
    painter.setPen(QPen(QColor(230, 0, 120), 1.0, Qt::DashLine));
    if (m_snapResult.snappedX) {
        const double px = origin.x() + m_snapResult.guideXmm * pxPerMm;
        painter.drawLine(QPointF(px, cardRect.top() - 12.0),
                         QPointF(px, cardRect.bottom() + 12.0));
    }
    if (m_snapResult.snappedY) {
        const double px = origin.y() + m_snapResult.guideYmm * pxPerMm;
        painter.drawLine(QPointF(cardRect.left() - 12.0, px),
                         QPointF(cardRect.right() + 12.0, px));
    }
    painter.restore();
}

void CardCanvas::paintChrome(QPainter &painter)
{
    if (!m_document)
        return;

    paintGrid(painter);
    paintGuides(painter);

    // A faint outline around every object, so an invisible or fully transparent
    // object can still be found and selected.
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 0, 0, 30), 1.0));
    const double pxPerMm = this->pxPerMm();
    const QPointF origin = originPx();
    for (CardObject *object : m_document->side(m_side).objects()) {
        if (!object || isSelected(object->id()))
            continue;
        const QRectF box =
            CanvasGeometry::viewRectForMm(object->boundingRectMm(), pxPerMm, origin);
        painter.drawRect(box);
    }
    painter.restore();

    if (m_showPrintMargins && m_document) {
        // The bleed box, drawn so the user knows what will be cut off.
        const CardGeometry &geom = m_document->geometry();
        if (geom.bleedMm() > 0.0) {
            painter.save();
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(200, 120, 0), 1.0, Qt::DashLine));
            painter.drawRect(CanvasGeometry::viewRectForMm(geom.bleedBoundsMm(), pxPerMm,
                                                          origin));
            painter.restore();
        }
    }

    for (CardObject *object : selection()) {
        if (!object)
            continue;
        paintSelection(painter, *object);
        if (object->isLocked())
            paintLockedIndicator(painter, *object);
    }

    const QVector<CardObject *> selected = selection();
    if (selected.size() == 1 && !selected.first()->isLocked()) {
        paintHandles(painter, *selected.first());
    } else if (selected.size() > 1) {
        // A multi-object selection shows one frame around the whole block, drawn
        // from a temporary unrotated object that stands in for the union box, so
        // the handles are the same code path as for a single object.
        const QRectF bounds = selectionBoundsMm();
        if (bounds.isValid() && !bounds.isEmpty()) {
            ShapeObject frame(ShapeKind::Rectangle);
            frame.setRectMm(bounds);
            painter.save();
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(m_selectionColor, 1.0, Qt::DashLine));
            painter.drawRect(CanvasGeometry::viewRectForMm(bounds, pxPerMm, origin));
            painter.restore();
            paintHandles(painter, frame);
        }
    }

    paintAlignmentLines(painter);

    // The rubber band.
    if (m_dragMode == DragMode::Marquee) {
        const QRectF band =
            CanvasGeometry::viewRectForMm(m_marqueeRectMm, pxPerMm, origin);
        painter.save();
        painter.setBrush(QColor(m_selectionColor.red(), m_selectionColor.green(),
                                m_selectionColor.blue(), 30));
        painter.setPen(QPen(m_selectionColor, 1.0, Qt::DashLine));
        painter.drawRect(band);
        painter.restore();
    }
}

void CardCanvas::snapshotDragStart()
{
    m_dragSnapshot.clear();
    m_dragStartRects.clear();
    if (!m_document)
        return;
    const CardSide &side = m_document->side(m_side);
    m_dragStartRectMm = QRectF();
    for (const ObjectId &id : m_selection) {
        if (const CardObject *object = side.object(id)) {
            m_dragSnapshot.insert(id, object->toJson());
            m_dragStartRects.insert(id, object->rectMm());
            if (!m_dragStartRectMm.isValid() || m_dragStartRectMm.isNull())
                m_dragStartRectMm = object->rectMm();
        }
    }
}

QJsonObject CardCanvas::snapshotOf(const ObjectId &id) const
{
    if (!m_document)
        return QJsonObject();
    if (const CardObject *object = m_document->side(m_side).object(id))
        return object->toJson();
    return QJsonObject();
}

void CardCanvas::pushModifyFromSnapshot(const QString &text, const QString &mergeKey)
{
    if (!m_undoStack || !m_document || m_dragSnapshot.isEmpty())
        return;

    QVector<QPair<ObjectId, QJsonObject>> before;
    QVector<QPair<ObjectId, QJsonObject>> after;
    for (auto it = m_dragSnapshot.constBegin(); it != m_dragSnapshot.constEnd(); ++it) {
        const QJsonObject now = snapshotOf(it.key());
        if (now.isEmpty() || now == it.value())
            continue;       // nothing changed for this object
        before.append({ it.key(), it.value() });
        after.append({ it.key(), now });
    }
    if (before.isEmpty())
        return;

    // The document already holds the new state, so the command is pushed with
    // redo() short-circuited: the payload is identical, and pushing it normally
    // would simply rewrite the same values.
    m_undoStack->push(new ModifyObjectsCommand(m_document, m_side, before, after, text,
                                               mergeKey));
}

void CardCanvas::beginDrag(const QPointF &viewPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_document)
        return;

    m_dragStartView = viewPos;
    m_dragStartMm = CanvasGeometry::viewToMm(viewPos, pxPerMm(), originPx());
    m_snapResult = SnapEngine::Result();

    const double pxPerMm = this->pxPerMm();
    const QPointF origin = originPx();
    const bool additive = modifiers.testFlag(Qt::ShiftModifier);
    CardSide &side = currentSideRef();

    // 1. A handle of the current single-object selection.
    const QVector<CardObject *> selected = selection();
    if (selected.size() == 1 && !selected.first()->isLocked()) {
        const Handle::Id handle = CanvasGeometry::handleAt(*selected.first(), viewPos,
                                                          pxPerMm, origin, kHandleSizePx);
        if (handle == Handle::Rotate) {
            m_dragMode = DragMode::Rotate;
            m_dragStartRotation = selected.first()->rotationDeg();
            const QPointF centre(selected.first()->centerXMm(),
                                 selected.first()->centerYMm());
            m_dragStartAngleDeg =
                qRadiansToDegrees(std::atan2(m_dragStartMm.y() - centre.y(),
                                             m_dragStartMm.x() - centre.x()));
            snapshotDragStart();
            setStatusMessage(tr("Rotating %1 (hold Shift for 15 degree steps)")
                                 .arg(selected.first()->name()));
            return;
        }
        if (handle != Handle::None) {
            m_dragMode = DragMode::Resize;
            m_activeHandle = handle;
            m_dragStartRectMm = selected.first()->rectMm();
            snapshotDragStart();
            setStatusMessage(tr("Resizing %1").arg(selected.first()->name()));
            return;
        }
    }

    // 2. An object under the pointer.
    CardObject *hit = side.hitTest(m_dragStartMm, true, true);
    if (hit && !hit->isLocked()) {
        if (!isSelected(hit->id()))
            selectObject(hit->id(), additive);
        m_dragMode = DragMode::Move;
        snapshotDragStart();
        setStatusMessage(tr("Moving %n object(s) (hold Alt for fine movement)", nullptr,
                            int(m_selection.size())));
        return;
    }

    // 3. Empty space: select everything inside the rubber band.
    if (!additive)
        clearSelection();
    m_dragMode = DragMode::Marquee;
    m_marqueeRectMm = QRectF(m_dragStartMm, QSizeF());
}

void CardCanvas::applyMoveToSelection(const QPointF &deltaMm)
{
    if (!m_document)
        return;
    CardSide &side = currentSideRef();
    for (auto it = m_dragStartRects.constBegin(); it != m_dragStartRects.constEnd(); ++it) {
        CardObject *object = side.object(it.key());
        if (!object)
            continue;
        const QRectF start = it.value();
        object->setRectMm(QRectF(start.x() + deltaMm.x(), start.y() + deltaMm.y(),
                                 start.width(), start.height()));
    }
    update();
}

void CardCanvas::updateDrag(const QPointF &viewPos, Qt::KeyboardModifiers modifiers)
{
    const QPointF mm = CanvasGeometry::viewToMm(viewPos, pxPerMm(), originPx());
    const QPointF delta = mm - m_dragStartMm;

    switch (m_dragMode) {
    case DragMode::Marquee: {
        m_marqueeRectMm = QRectF(m_dragStartMm, mm).normalized();
        const QVector<CardObject *> inside =
            m_document ? m_document->side(m_side).objectsIn(m_marqueeRectMm, true, true)
                       : QVector<CardObject *>();
        QVector<ObjectId> ids =
            modifiers.testFlag(Qt::ShiftModifier) ? m_selection : QVector<ObjectId>();
        for (CardObject *object : inside) {
            if (object && !ids.contains(object->id()))
                ids.append(object->id());
        }
        setSelection(ids);
        break;
    }
    case DragMode::Move: {
        // The snap engine works on the anchor object's top-left corner; every
        // other selected object follows the same delta, so the relative layout of
        // a block is preserved exactly.
        const QPointF proposed(m_dragStartRectMm.x() + delta.x(),
                              m_dragStartRectMm.y() + delta.y());
        const CardObject *anchor = nullptr;
        for (auto it = m_dragStartRects.constBegin(); it != m_dragStartRects.constEnd();
             ++it) {
            if (it.value() == m_dragStartRectMm) {
                anchor = currentSideConstRef().object(it.key());
                break;
            }
        }
        if (!anchor)
            break;

        QVector<GuideModel::Guide> guides;
        if (m_guides)
            guides = m_guides->guides(m_side);
        const CardGeometry &geom = m_document ? m_document->geometry()
                                              : CardGeometry::defaultGeometry();
        m_snapResult = SnapEngine::snapMove(*anchor, proposed, currentSideConstRef(),
                                            geom, guides, m_snap, pxPerMm());
        // The applied delta is the pointer delta plus whatever the snap added, so
        // the whole block keeps its internal spacing.
        applyMoveToSelection(delta + (m_snapResult.adjustedMm - proposed));
        break;
    }
    case DragMode::Resize:
        applyResize(mm, modifiers);
        break;
    case DragMode::Rotate:
        applyRotate(mm, modifiers);
        break;
    default:
        break;
    }
}

void CardCanvas::applyResize(const QPointF &viewPosMm, Qt::KeyboardModifiers modifiers)
{
    if (!m_document || m_selection.size() != 1)
        return;
    CardObject *object = m_document->side(m_side).object(m_selection.first());
    if (!object || object->isLocked())
        return;

    const QRectF start = m_dragStartRectMm;
    if (start.width() <= 0.0 || start.height() <= 0.0)
        return;

    const QVector<GuideModel::Guide> guides =
        m_guides ? m_guides->guides(m_side) : QVector<GuideModel::Guide>();
    const CardGeometry &geom = m_document->geometry();

    // The pointer is expressed in the object's own frame, so a rotated object
    // resizes along its own axes rather than along the screen's.
    QPointF local = viewPosMm;
    if (!qFuzzyIsNull(object->rotationDeg())) {
        QTransform t;
        const QPointF centre = start.center();
        t.translate(centre.x(), centre.y());
        t.rotate(-object->rotationDeg());
        t.translate(-centre.x(), -centre.y());
        local = t.map(local);
    }

    double left = start.left();
    double top = start.top();
    double right = start.right();
    double bottom = start.bottom();

    const bool movesLeft = m_activeHandle == Handle::TopLeft
                           || m_activeHandle == Handle::Left
                           || m_activeHandle == Handle::BottomLeft;
    const bool movesRight = m_activeHandle == Handle::TopRight
                            || m_activeHandle == Handle::Right
                            || m_activeHandle == Handle::BottomRight;
    const bool movesTop = m_activeHandle == Handle::TopLeft
                          || m_activeHandle == Handle::Top
                          || m_activeHandle == Handle::TopRight;
    const bool movesBottom = m_activeHandle == Handle::BottomLeft
                             || m_activeHandle == Handle::Bottom
                             || m_activeHandle == Handle::BottomRight;

    if (movesLeft) {
        left = SnapEngine::snapResize(*object, false, true, local.x(),
                                      currentSideConstRef(), geom, guides, m_snap,
                                      pxPerMm())
                   .adjustedMm.x();
    }
    if (movesRight) {
        right = SnapEngine::snapResize(*object, false, false, local.x(),
                                       currentSideConstRef(), geom, guides, m_snap,
                                       pxPerMm())
                    .adjustedMm.x();
    }
    if (movesTop) {
        top = SnapEngine::snapResize(*object, true, true, local.y(),
                                    currentSideConstRef(), geom, guides, m_snap,
                                    pxPerMm())
                  .adjustedMm.y();
    }
    if (movesBottom) {
        bottom = SnapEngine::snapResize(*object, true, false, local.y(),
                                       currentSideConstRef(), geom, guides, m_snap,
                                       pxPerMm())
                     .adjustedMm.y();
    }

    double width = right - left;
    double height = bottom - top;
    const double aspect = start.height() > 0.0 ? start.width() / start.height() : 1.0;

    const bool corner = (movesLeft || movesRight) && (movesTop || movesBottom);
    if (modifiers.testFlag(Qt::ShiftModifier) && aspect > 0.0 && corner) {
        // Shift keeps the proportions, which is what a user expects from the
        // corner handles of a photograph.
        if (qAbs(width) / qMax(1e-6, qAbs(height)) > aspect)
            height = width / aspect;
        else
            width = height * aspect;
    }
    if (const auto *photo = dynamic_cast<const PhotoObject *>(object)) {
        if (photo->lockedAspectRatio() > 0.0) {
            const double locked = photo->lockedAspectRatio();
            if (movesLeft || movesRight)
                height = width / locked;
            else
                width = height * locked;
        }
    }

    // Never let a drag collapse an object: a zero-sized object cannot be grabbed
    // again, which would look exactly like losing it.
    constexpr double kMinimumMm = 0.2;
    width = qMax(kMinimumMm, qAbs(width));
    height = qMax(kMinimumMm, qAbs(height));

    const QRectF resized(movesLeft ? right - width : left,
                         movesTop ? bottom - height : top, width, height);
    object->setRectMm(resized);
    update();
}

void CardCanvas::applyRotate(const QPointF &viewPosMm, Qt::KeyboardModifiers modifiers)
{
    if (!m_document || m_selection.size() != 1)
        return;
    CardObject *object = m_document->side(m_side).object(m_selection.first());
    if (!object || object->isLocked())
        return;

    const QPointF centre(object->centerXMm(), object->centerYMm());
    const double angleNow = qRadiansToDegrees(
        std::atan2(viewPosMm.y() - centre.y(), viewPosMm.x() - centre.x()));
    double rotation = m_dragStartRotation + (angleNow - m_dragStartAngleDeg);
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        // Shift snaps to 15 degree steps, which is what a designer needs to keep
        // two rotated objects parallel.
        rotation = std::round(rotation / 15.0) * 15.0;
    }
    object->setRotationDeg(rotation);
    setStatusMessage(tr("Rotation %1 degrees").arg(object->rotationDeg(), 0, 'f', 1));
    update();
}

void CardCanvas::commitActiveDrag()
{
    switch (m_dragMode) {
    case DragMode::Move:
        pushModifyFromSnapshot(tr("Move %n object(s)", nullptr, int(m_selection.size())),
                               QStringLiteral("move"));
        break;
    case DragMode::Resize:
        pushModifyFromSnapshot(tr("Resize"), QStringLiteral("resize"));
        break;
    case DragMode::Rotate:
        pushModifyFromSnapshot(tr("Rotate"), QStringLiteral("rotate"));
        break;
    default:
        break;
    }
    m_dragSnapshot.clear();
    m_dragStartRects.clear();
    m_activeHandle = Handle::None;
}

void CardCanvas::finishDrag()
{
    commitActiveDrag();
    m_dragMode = DragMode::None;
    m_snapResult = SnapEngine::Result();
    unsetCursor();
    update();
    emit viewChanged();
}

void CardCanvas::updateCursorFor(const QPointF &viewPos)
{
    if (!m_document) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    const double pxPerMm = this->pxPerMm();
    const QPointF origin = originPx();
    const QVector<CardObject *> selected = selection();
    if (selected.size() == 1 && !selected.first()->isLocked()) {
        const Handle::Id handle = CanvasGeometry::handleAt(*selected.first(), viewPos,
                                                          pxPerMm, origin, kHandleSizePx);
        if (handle == Handle::Rotate) {
            setCursor(Qt::CrossCursor);
            return;
        }
        if (handle != Handle::None) {
            const QVector<Handle> handles = CanvasGeometry::handlesFor(
                *selected.first(), pxPerMm, origin, kHandleSizePx);
            for (const Handle &candidate : handles) {
                if (candidate.id != handle)
                    continue;
                // The diagonal handles get a diagonal cursor, the edge handles get
                // the matching straight one, and the angle follows the object's
                // rotation.
                const double angle = CanvasGeometry::handleCursorAngleDeg(handle)
                                     + selected.first()->rotationDeg();
                const double wrapped = std::fmod(std::fmod(angle, 180.0) + 180.0, 180.0);
                if (wrapped > 22.5 && wrapped <= 67.5)
                    setCursor(Qt::SizeFDiagCursor);
                else if (wrapped > 67.5 && wrapped <= 112.5)
                    setCursor(Qt::SizeVerCursor);
                else if (wrapped > 112.5 && wrapped <= 157.5)
                    setCursor(Qt::SizeBDiagCursor);
                else
                    setCursor(Qt::SizeHorCursor);
                return;
            }
        }
    }

    const QPointF mm = CanvasGeometry::viewToMm(viewPos, pxPerMm, origin);
    CardObject *hit = m_document->side(m_side).hitTest(mm, true, true);
    if (!hit) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    setCursor(hit->isLocked() ? Qt::ForbiddenCursor : Qt::SizeAllCursor);
}

void CardCanvas::setStatusMessage(const QString &message)
{
    if (!message.isEmpty())
        emit statusMessage(message);
}

// ---------------------------------------------------------------------------
// Mouse, wheel, keyboard and drop
// ---------------------------------------------------------------------------

void CardCanvas::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    if (!m_document) {
        QWidget::mousePressEvent(event);
        return;
    }

    // Space plus drag, or the middle button, pans the view.
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && m_spacePanning)) {
        m_dragMode = DragMode::Pan;
        m_panStartView = event->position();
        m_panStartScroll = m_scroll;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    beginDrag(event->position(), event->modifiers());
    update();
    event->accept();
}

void CardCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_document) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPointF mm = CanvasGeometry::viewToMm(event->position(), pxPerMm(), originPx());
    emit mousePositionChanged(mm);

    if (m_dragMode == DragMode::Pan) {
        const QPointF delta = event->position() - m_panStartView;
        const QPoint wanted = m_panStartScroll - delta.toPoint();
        if (auto *scrollArea = qobject_cast<QScrollArea *>(
                parentWidget() ? parentWidget()->parentWidget() : nullptr)) {
            scrollArea->horizontalScrollBar()->setValue(wanted.x());
            scrollArea->verticalScrollBar()->setValue(wanted.y());
        }
        event->accept();
        return;
    }

    if (m_dragMode == DragMode::None) {
        updateCursorFor(event->position());
        QWidget::mouseMoveEvent(event);
        return;
    }

    updateDrag(event->position(), event->modifiers());
    event->accept();
}

void CardCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragMode == DragMode::Pan) {
        m_dragMode = DragMode::None;
        unsetCursor();
        event->accept();
        return;
    }
    if (m_dragMode != DragMode::None) {
        finishDrag();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void CardCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_document || event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    const QPointF mm = CanvasGeometry::viewToMm(event->position(), pxPerMm(), originPx());
    CardObject *hit = m_document->side(m_side).hitTest(mm, true, true);
    if (hit && dynamic_cast<TextObject *>(hit)) {
        // Double-clicking text is the fastest way into it, which is what every
        // drawing application does.
        selectObject(hit->id());
        beginTextEditing(hit->id());
        event->accept();
        return;
    }
    if (!hit) {
        // Double-clicking empty space fits the card, a habit carried over from
        // every other editor.
        fitToWindow();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void CardCanvas::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const double factor = event->angleDelta().y() > 0 ? 1.25 : 1.0 / 1.25;
        zoomAt(event->position().toPoint(), factor);
        event->accept();
        return;
    }
    // Plain wheel scrolling is left to the enclosing scroll area, so the ruler and
    // the canvas stay in step automatically.
    event->ignore();
}

void CardCanvas::zoomAt(const QPoint &viewPos, double factor)
{
    applyZoomKeepingPoint(m_zoomPercent * factor, viewPos);
}

void CardCanvas::applyZoomKeepingPoint(double newZoom, const QPoint &anchorPx)
{
    if (!m_document) {
        setZoomPercent(newZoom);
        return;
    }

    // The millimetre point under the pointer before the zoom must stay under the
    // pointer after it, which is what makes wheel zooming feel anchored.
    const QPointF mmBefore =
        CanvasGeometry::viewToMm(QPointF(anchorPx), pxPerMm(), originPx());

    setZoomPercent(newZoom);

    const QPointF viewAfter =
        CanvasGeometry::mmToView(mmBefore, pxPerMm(), originPx());
    const QPointF correction = QPointF(anchorPx) - viewAfter;

    if (auto *scrollArea = qobject_cast<QScrollArea *>(
            parentWidget() ? parentWidget()->parentWidget() : nullptr)) {
        scrollArea->horizontalScrollBar()->setValue(
            scrollArea->horizontalScrollBar()->value() - int(correction.x()));
        scrollArea->verticalScrollBar()->setValue(
            scrollArea->verticalScrollBar()->value() - int(correction.y()));
    }
}

void CardCanvas::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        m_spacePanning = true;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        deleteSelection();
        event->accept();
        return;
    case Qt::Key_Escape:
        if (isEditingText())
            cancelTextEditing();
        else
            clearSelection();
        event->accept();
        return;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        // 1 mm per press, 0.1 mm with Alt, 10 mm with Shift: the three distances a
        // card designer actually needs.
        double step = 1.0;
        if (event->modifiers().testFlag(Qt::AltModifier))
            step = 0.1;
        else if (event->modifiers().testFlag(Qt::ShiftModifier))
            step = 10.0;
        double dx = 0.0;
        double dy = 0.0;
        if (event->key() == Qt::Key_Left)
            dx = -step;
        else if (event->key() == Qt::Key_Right)
            dx = step;
        else if (event->key() == Qt::Key_Up)
            dy = -step;
        else
            dy = step;
        nudgeSelection(dx, dy);
        event->accept();
        return;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (isEditingText()) {
            commitTextEditing();
            event->accept();
            return;
        }
        break;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void CardCanvas::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space) {
        m_spacePanning = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void CardCanvas::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls()) {
        QWidget::dragEnterEvent(event);
        return;
    }
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile() && looksLikeImagePath(url.toLocalFile())) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void CardCanvas::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        QWidget::dragMoveEvent(event);
}

void CardCanvas::dropEvent(QDropEvent *event)
{
    if (!m_document || !event->mimeData()->hasUrls()) {
        QWidget::dropEvent(event);
        return;
    }

    // The drop position becomes the object's top-left corner, so an image lands
    // exactly where it was dropped.
    const QPointF dropMm =
        CanvasGeometry::viewToMm(event->position(), pxPerMm(), originPx());

    int imported = 0;
    double offsetY = 0.0;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (!looksLikeImagePath(path))
            continue;

        QString error;
        const QString assetId = m_document->assets()->addImageFile(path, &error);
        if (assetId.isEmpty()) {
            emit statusMessage(error);
            continue;
        }

        auto object = std::make_unique<ImageObject>();
        const QImage image = m_document->assets()->image(assetId);
        const double widthMm = 30.0;
        const double heightMm = image.isNull() || image.width() <= 0
                                    ? 20.0
                                    : widthMm * image.height() / double(image.width());
        object->setRectMm(QRectF(dropMm.x(), dropMm.y() + offsetY, widthMm, heightMm));
        object->setAssetId(assetId);
        object->setName(QFileInfo(path).completeBaseName());
        offsetY += heightMm + 2.0;

        const ObjectId newId = object->id();
        const QJsonObject snapshot = object->toJson();
        if (m_undoStack) {
            m_undoStack->push(new AddObjectsCommand(m_document, m_side, { snapshot }, -1));
        } else {
            m_document->side(m_side).insertObject(std::move(object));
            m_document->notifyChanged();
        }
        selectObject(newId);
        ++imported;
    }

    if (imported > 0) {
        emit statusMessage(tr("%n image(s) imported", nullptr, imported));
        event->acceptProposedAction();
        return;
    }
    QWidget::dropEvent(event);
}

void CardCanvas::leaveEvent(QEvent *event)
{
    emit mousePositionChanged(QPointF(-1.0, -1.0));
    QWidget::leaveEvent(event);
}

void CardCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    emit viewChanged();
}

void CardCanvas::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        invalidateRenderCache();
    QWidget::changeEvent(event);
}

bool CardCanvas::eventFilter(QObject *watched, QEvent *event)
{
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Editing: text, delete, clipboard
// ---------------------------------------------------------------------------

void CardCanvas::beginTextEditing(const ObjectId &id)
{
    if (!m_document)
        return;
    auto *text = dynamic_cast<TextObject *>(m_document->side(m_side).object(id));
    if (!text) {
        emit statusMessage(tr("Only text objects can be edited on the canvas."));
        return;
    }

    destroyTextEditor();
    m_editing = id;

    m_textEditor = new QTextEdit(this);
    m_textEditor->setObjectName(QStringLiteral("inlineTextEditor"));
    m_textEditor->setPlainText(text->text());
    m_textEditor->setAcceptRichText(false);
    m_textEditor->setFrameShape(QFrame::NoFrame);
    m_textEditor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEditor->installEventFilter(this);
    m_textEditor->show();
    m_textEditor->setFocus(Qt::OtherFocusReason);
    m_textEditor->selectAll();

    QFont editorFont(text->fontFamily());
    editorFont.setPointSizeF(qMax(4.0, text->fontSizePt()));
    editorFont.setBold(text->bold());
    editorFont.setItalic(text->italic());
    editorFont.setUnderline(text->underline());
    m_textEditor->setFont(editorFont);
    updateTextEditorGeometry();

    emit statusMessage(
        tr("Editing the text of %1 - press Escape to cancel").arg(text->name()));
}

void CardCanvas::updateTextEditorGeometry()
{
    if (!m_textEditor || !m_document || m_editing.isNull())
        return;
    CardObject *object = m_document->side(m_side).object(m_editing);
    if (!object)
        return;

    const QRectF box =
        CanvasGeometry::viewRectForMm(object->rectMm(), pxPerMm(), originPx());
    // The editor is placed exactly over the object, so what the user types appears
    // where it will print.
    m_textEditor->setGeometry(box.toAlignedRect().adjusted(-2, -2, 2, 2));
}

void CardCanvas::commitTextEditing()
{
    if (!m_textEditor || !m_document || m_editing.isNull()) {
        destroyTextEditor();
        return;
    }

    const QString newText = m_textEditor->toPlainText();
    const ObjectId id = m_editing;
    CardObject *object = m_document->side(m_side).object(id);
    QString oldText;
    if (const auto *typed = dynamic_cast<const TextObject *>(object))
        oldText = typed->text();

    // The overlay goes first: the object's state is about to be snapshotted, and
    // keeping a pointer to a replaced object would be a dangling reference.
    destroyTextEditor();

    if (!object || newText == oldText)
        return;

    const QVector<QPair<ObjectId, QJsonObject>> before =
        ModifyObjectsCommand::captureState(currentSideConstRef(), { id });
    if (auto *edited = dynamic_cast<TextObject *>(m_document->side(m_side).object(id)))
        edited->setText(newText);
    const QVector<QPair<ObjectId, QJsonObject>> after =
        ModifyObjectsCommand::captureState(currentSideConstRef(), { id });

    if (m_undoStack && !before.isEmpty() && !after.isEmpty()) {
        // The new state is already applied; the command carries the payload so undo
        // can restore the previous text.
        m_undoStack->push(new ModifyObjectsCommand(m_document, m_side, before, after,
                                                   tr("Edit text"),
                                                   QStringLiteral("text")));
    }
    m_document->notifyChanged();
    update();
}

void CardCanvas::cancelTextEditing()
{
    destroyTextEditor();
    emit statusMessage(tr("Text editing cancelled."));
}

void CardCanvas::destroyTextEditor()
{
    m_editing = ObjectId();
    if (!m_textEditor)
        return;
    m_textEditor->removeEventFilter(this);
    m_textEditor->hide();
    m_textEditor->deleteLater();
    m_textEditor = nullptr;
}

void CardCanvas::deleteSelection()
{
    if (!m_document || m_selection.isEmpty() || !m_undoStack) {
        emit statusMessage(tr("Nothing is selected to delete."));
        return;
    }

    CardSide &side = currentSideRef();
    QVector<QJsonObject> snapshots;
    int firstIndex = -1;
    for (const ObjectId &id : m_selection) {
        const CardObject *object = side.object(id);
        if (!object || object->isLocked())
            continue;
        snapshots.append(object->toJson());
        const int index = side.indexOf(id);
        if (index >= 0 && (firstIndex < 0 || index < firstIndex))
            firstIndex = index;
    }
    if (snapshots.isEmpty()) {
        emit statusMessage(
            tr("The selected objects are locked, so nothing was deleted."));
        return;
    }

    m_undoStack->push(new RemoveObjectsCommand(m_document, m_side, snapshots, firstIndex));
    clearSelection();
    update();
}

void CardCanvas::duplicateSelection()
{
    if (!m_document || m_selection.isEmpty() || !m_undoStack) {
        emit statusMessage(tr("Nothing is selected to duplicate."));
        return;
    }

    const CardSide &side = currentSideConstRef();
    QVector<QJsonObject> copies;
    QVector<ObjectId> newIds;
    // A 2 mm offset is the convention every editor uses, and it makes it obvious
    // that a new copy exists.
    constexpr double kOffsetMm = 2.0;
    for (const ObjectId &id : m_selection) {
        const CardObject *object = side.object(id);
        if (!object)
            continue;
        CardObjectPtr clone = object->clone();
        if (!clone)
            continue;
        clone->regenerateId();
        clone->setXMm(clone->xMm() + kOffsetMm);
        clone->setYMm(clone->yMm() + kOffsetMm);
        copies.append(clone->toJson());
        newIds.append(clone->id());
    }
    if (copies.isEmpty())
        return;

    m_undoStack->push(new AddObjectsCommand(m_document, m_side, copies, -1));
    setSelection(newIds);
}

void CardCanvas::copySelection()
{
    if (m_selection.isEmpty()) {
        emit statusMessage(tr("Nothing is selected to copy."));
        return;
    }
    const CardSide &side = currentSideConstRef();
    QVector<QJsonObject> snapshots;
    for (const ObjectId &id : m_selection) {
        if (const CardObject *object = side.object(id))
            snapshots.append(object->toJson());
    }
    if (snapshots.isEmpty())
        return;

    auto *clipboardData = new QMimeData;
    clipboardData->setData(QStringLiteral("application/x-opencardcanvas-objects"),
                           clipboard::encode(snapshots));
    // Plain text is set as well, so pasting into a text editor produces something
    // readable rather than nothing at all.
    QStringList names;
    for (const QJsonObject &snapshot : snapshots)
        names << snapshot.value(QStringLiteral("name")).toString();
    clipboardData->setText(names.join(QLatin1String(", ")));

    if (QClipboard *board = QApplication::clipboard())
        board->setMimeData(clipboardData);
    emit statusMessage(tr("%n object(s) copied", nullptr, int(snapshots.size())));
}

void CardCanvas::cutSelection()
{
    copySelection();
    deleteSelection();
}

void CardCanvas::pasteClipboard()
{
    if (!m_document || !m_undoStack) {
        emit statusMessage(tr("There is no open project to paste into."));
        return;
    }
    QClipboard *board = QApplication::clipboard();
    if (!board || !board->mimeData()) {
        emit statusMessage(tr("There is nothing to paste."));
        return;
    }

    const QByteArray payload =
        board->mimeData()->data(QStringLiteral("application/x-opencardcanvas-objects"));
    const QVector<QJsonObject> objects = clipboard::decode(payload);
    if (objects.isEmpty()) {
        emit statusMessage(
            tr("The clipboard does not contain any OpenCardCanvas objects."));
        return;
    }

    // Pasted objects get fresh identities and a small offset, so a paste can never
    // collide with what is already on the card.
    constexpr double kOffsetMm = 3.0;
    QVector<QJsonObject> unique;
    QVector<ObjectId> newIds;
    for (const QJsonObject &source : objects) {
        QString error;
        CardObjectPtr object = ObjectFactory::createAndLoad(source, &error);
        if (!object)
            continue;
        object->regenerateId();
        object->setXMm(object->xMm() + kOffsetMm);
        object->setYMm(object->yMm() + kOffsetMm);
        unique.append(object->toJson());
        newIds.append(object->id());
    }
    if (unique.isEmpty()) {
        emit statusMessage(tr("The pasted data could not be read."));
        return;
    }

    m_undoStack->push(new AddObjectsCommand(m_document, m_side, unique, -1));
    setSelection(newIds);
    emit statusMessage(tr("%n object(s) pasted", nullptr, int(unique.size())));
}

void CardCanvas::nudgeSelection(double dxMm, double dyMm)
{
    if (!m_document || m_selection.isEmpty() || !m_undoStack) {
        emit statusMessage(tr("Nothing is selected to move."));
        return;
    }

    const QVector<ObjectId> ids = m_selection;
    const QVector<QPair<ObjectId, QJsonObject>> before =
        ModifyObjectsCommand::captureState(currentSideConstRef(), ids);
    CardSide &side = currentSideRef();
    for (const ObjectId &id : ids) {
        CardObject *object = side.object(id);
        if (!object || object->isLocked())
            continue;
        object->moveByMm(dxMm, dyMm);
    }
    const QVector<QPair<ObjectId, QJsonObject>> after =
        ModifyObjectsCommand::captureState(currentSideConstRef(), ids);
    if (!before.isEmpty() && !after.isEmpty()) {
        m_undoStack->push(new ModifyObjectsCommand(m_document, m_side, before, after,
                                                   tr("Nudge"),
                                                   QStringLiteral("nudge")));
    }
    m_document->notifyChanged();
    update();
}

// ---------------------------------------------------------------------------
// Inserting objects
// ---------------------------------------------------------------------------

void CardCanvas::addObject(CardObjectPtr object, const QString &undoText)
{
    if (!m_document || !object)
        return;

    const ObjectId id = object->id();
    if (m_undoStack) {
        // The snapshot is taken before the object is handed over, because the
        // command takes ownership of the serialised form, not the object.
        const QJsonObject snapshot = object->toJson();
        m_undoStack->push(new AddObjectsCommand(m_document, m_side, { snapshot }, -1));
        Q_UNUSED(undoText);
    } else {
        m_document->side(m_side).insertObject(std::move(object));
        m_document->notifyChanged();
    }
    setSelection({ id });
    update();
}

void CardCanvas::addTextObject()
{
    auto object = std::make_unique<TextObject>();
    object->setRectMm(QRectF(10.0, 10.0, 40.0, 7.0));
    object->setText(tr("New text"));
    object->setFontSizePt(9.0);
    addObject(std::move(object), tr("Add Text"));
    emit statusMessage(tr("A text object was added. Double-click it to edit."));
}

void CardCanvas::addImageFromFile(const QString &path)
{
    if (!m_document) {
        emit statusMessage(tr("There is no open project."));
        return;
    }
    if (path.trimmed().isEmpty()) {
        emit statusMessage(tr("No image file was given."));
        return;
    }

    QString error;
    const QString assetId = m_document->assets()->addImageFile(path, &error);
    if (assetId.isEmpty()) {
        emit statusMessage(error);
        return;
    }

    auto object = std::make_unique<ImageObject>();
    const QImage image = m_document->assets()->image(assetId);
    const double widthMm = 30.0;
    const double heightMm = image.isNull() || image.width() <= 0
                                ? 20.0
                                : widthMm * image.height() / double(image.width());
    object->setRectMm(QRectF(10.0, 10.0, widthMm, heightMm));
    object->setAssetId(assetId);
    object->setName(QFileInfo(path).completeBaseName());
    addObject(std::move(object), tr("Add Image"));
    emit statusMessage(tr("The image was added to the card and copied into the "
                          "project."));
}

void CardCanvas::addPhotoFromFile(const QString &path)
{
    if (!m_document) {
        emit statusMessage(tr("There is no open project."));
        return;
    }
    if (path.trimmed().isEmpty()) {
        emit statusMessage(tr("No photograph was given."));
        return;
    }

    QString error;
    const QString assetId = m_document->assets()->addImageFile(path, &error);
    if (assetId.isEmpty()) {
        emit statusMessage(error);
        return;
    }

    auto object = std::make_unique<PhotoObject>();
    object->setRectMm(QRectF(10.0, 10.0, 22.0, 26.0));
    object->setAssetId(assetId);
    object->setName(tr("Photo"));
    addObject(std::move(object), tr("Add Photo"));
    emit statusMessage(tr("The photograph was added. It is never modified or "
                          "re-encoded, only cropped."));
}

void CardCanvas::addShape(ShapeKind kind)
{
    auto object = std::make_unique<ShapeObject>(kind);
    object->setRectMm(kind == ShapeKind::Line ? QRectF(10.0, 10.0, 40.0, 0.5)
                                              : QRectF(10.0, 10.0, 25.0, 15.0));
    addObject(std::move(object), tr("Add Shape"));
}

void CardCanvas::addQrObject()
{
    auto object = std::make_unique<QrObject>();
    object->setRectMm(QRectF(10.0, 10.0, 20.0, 20.0));
    addObject(std::move(object), tr("Add QR Code"));
    emit statusMessage(tr("A QR code was added. Set its data in the properties panel; "
                          "{{placeholders}} are filled in per record."));
}

void CardCanvas::addBarcodeObject()
{
    auto object = std::make_unique<BarcodeObject>();
    object->setRectMm(QRectF(10.0, 35.0, 50.0, 10.0));
    addObject(std::move(object), tr("Add Barcode"));
    emit statusMessage(tr("A barcode was added. It is validated against its symbology "
                          "before it is printed."));
}

// ---------------------------------------------------------------------------
// Z-order, grouping, visibility, locking
// ---------------------------------------------------------------------------

namespace {

// The all-or-nothing reorder helper shared by the four z-order entry points.
bool reorderWithCommand(CardDocument *document, CardSideId side, QUndoStack *stack,
                        const QVector<ObjectId> &before, const QVector<ObjectId> &after,
                        const QString &text)
{
    if (!document || !stack || before.isEmpty() || before == after
        || before.size() != after.size()) {
        return false;
    }
    stack->push(new ReorderObjectsCommand(document, side, before, after, text));
    return true;
}

} // namespace

void CardCanvas::bringToFront()
{
    if (!m_document || m_selection.isEmpty() || !m_undoStack) {
        emit statusMessage(tr("Nothing is selected."));
        return;
    }
    CardSide &side = currentSideRef();
    const QVector<ObjectId> before = side.objectIds();
    // Selected objects move to the end keeping their relative order: that is what
    // "bring to front" means when several objects are selected.
    QVector<ObjectId> after;
    for (const ObjectId &id : before) {
        if (!m_selection.contains(id))
            after.append(id);
    }
    for (const ObjectId &id : before) {
        if (m_selection.contains(id))
            after.append(id);
    }
    if (reorderWithCommand(m_document, m_side, m_undoStack, before, after,
                           tr("Bring to Front"))) {
        update();
    }
}

void CardCanvas::bringForward()
{
    if (!m_document || m_selection.isEmpty() || !m_undoStack)
        return;
    CardSide &side = currentSideRef();
    const QVector<ObjectId> before = side.objectIds();
    QVector<ObjectId> after = before;
    // Walking from the top lets a block of selected objects shift up together
    // without reordering among itself.
    for (int i = after.size() - 2; i >= 0; --i) {
        if (m_selection.contains(after.at(i)) && !m_selection.contains(after.at(i + 1)))
            after.swapItemsAt(i, i + 1);
    }
    if (reorderWithCommand(m_document, m_side, m_undoStack, before, after,
                           tr("Bring Forward"))) {
        update();
    }
}

void CardCanvas::sendBackward()
{
    if (!m_document || m_selection.isEmpty() || !m_undoStack)
        return;
    CardSide &side = currentSideRef();
    const QVector<ObjectId> before = side.objectIds();
    QVector<ObjectId> after = before;
    for (int i = 1; i < after.size(); ++i) {
        if (m_selection.contains(after.at(i)) && !m_selection.contains(after.at(i - 1)))
            after.swapItemsAt(i, i - 1);
    }
    if (reorderWithCommand(m_document, m_side, m_undoStack, before, after,
                           tr("Send Backward"))) {
        update();
    }
}

void CardCanvas::sendToBack()
{
    if (!m_document || m_selection.isEmpty() || !m_undoStack)
        return;
    CardSide &side = currentSideRef();
    const QVector<ObjectId> before = side.objectIds();
    QVector<ObjectId> after;
    for (const ObjectId &id : before) {
        if (m_selection.contains(id))
            after.append(id);
    }
    for (const ObjectId &id : before) {
        if (!m_selection.contains(id))
            after.append(id);
    }
    if (reorderWithCommand(m_document, m_side, m_undoStack, before, after,
                           tr("Send to Back"))) {
        update();
    }
}

void CardCanvas::setSelectionVisible(bool visible)
{
    if (!m_document || m_selection.isEmpty()) {
        emit statusMessage(tr("Nothing is selected."));
        return;
    }
    const QVector<ObjectId> ids = m_selection;
    const QVector<QPair<ObjectId, QJsonObject>> before =
        ModifyObjectsCommand::captureState(currentSideConstRef(), ids);
    CardSide &side = currentSideRef();
    for (const ObjectId &id : ids) {
        if (CardObject *object = side.object(id))
            object->setVisible(visible);
    }
    const QVector<QPair<ObjectId, QJsonObject>> after =
        ModifyObjectsCommand::captureState(currentSideConstRef(), ids);
    if (m_undoStack && !before.isEmpty() && !after.isEmpty()) {
        m_undoStack->push(new ModifyObjectsCommand(m_document, m_side, before, after,
                                                   visible ? tr("Show") : tr("Hide"),
                                                   QStringLiteral("visible")));
    }
    m_document->notifyChanged();
    emit statusMessage(visible ? tr("%n object(s) shown", nullptr, int(ids.size()))
                               : tr("%n object(s) hidden", nullptr, int(ids.size())));
    update();
}

void CardCanvas::setSelectionLocked(bool locked)
{
    if (!m_document || m_selection.isEmpty()) {
        emit statusMessage(tr("Nothing is selected."));
        return;
    }
    const QVector<ObjectId> ids = m_selection;
    const QVector<QPair<ObjectId, QJsonObject>> before =
        ModifyObjectsCommand::captureState(currentSideConstRef(), ids);
    CardSide &side = currentSideRef();
    for (const ObjectId &id : ids) {
        if (CardObject *object = side.object(id))
            object->setLocked(locked);
    }
    const QVector<QPair<ObjectId, QJsonObject>> after =
        ModifyObjectsCommand::captureState(currentSideConstRef(), ids);
    if (m_undoStack && !before.isEmpty() && !after.isEmpty()) {
        m_undoStack->push(new ModifyObjectsCommand(m_document, m_side, before, after,
                                                   locked ? tr("Lock") : tr("Unlock"),
                                                   QStringLiteral("locked")));
    }
    m_document->notifyChanged();
    emit statusMessage(locked
                           ? tr("%n object(s) locked and cannot be moved", nullptr,
                                int(ids.size()))
                           : tr("%n object(s) unlocked", nullptr, int(ids.size())));
    update();
}

void CardCanvas::groupSelection()
{
    if (!m_document || m_selection.size() < 2 || !m_undoStack) {
        emit statusMessage(tr("Select at least two objects to group them."));
        return;
    }

    // The members are taken in paint order, so the group keeps the stacking order
    // of what went into it.
    CardSide &side = currentSideRef();
    QVector<ObjectId> members;
    for (const ObjectId &id : side.objectIds()) {
        if (m_selection.contains(id))
            members.append(id);
    }
    if (members.size() < 2) {
        emit statusMessage(tr("Select at least two objects to group them."));
        return;
    }

    auto *command = new GroupCommand(m_document, m_side, members, ObjectId());
    m_undoStack->push(command);
    setSelection({ command->groupId() });
    emit statusMessage(tr("%n object(s) grouped", nullptr, int(members.size())));
}

void CardCanvas::ungroupSelection()
{
    if (!m_document || m_selection.isEmpty() || !m_undoStack) {
        emit statusMessage(tr("Select a group to ungroup it."));
        return;
    }

    QVector<ObjectId> newSelection;
    int ungrouped = 0;
    const QVector<ObjectId> ids = m_selection;
    for (const ObjectId &id : ids) {
        auto *group = dynamic_cast<GroupObject *>(m_document->side(m_side).object(id));
        if (!group)
            continue;
        for (CardObject *child : group->children()) {
            if (child)
                newSelection.append(child->id());
        }
        m_undoStack->push(new UngroupCommand(m_document, m_side, id));
        ++ungrouped;
    }

    if (ungrouped == 0) {
        emit statusMessage(tr("The selection does not contain a group."));
        return;
    }
    setSelection(newSelection);
    emit statusMessage(tr("%n group(s) ungrouped", nullptr, int(ungrouped)));
}

// ---------------------------------------------------------------------------
// Alignment and distribution
// ---------------------------------------------------------------------------

void CardCanvas::alignSelection(Qt::Alignment flag)
{
    if (!m_document || m_selection.isEmpty()) {
        emit statusMessage(tr("Nothing is selected to align."));
        return;
    }

    // With a single object the card is the reference, which is what makes "centre
    // on the card" a one-click operation.
    const bool againstCard = m_selection.size() == 1;
    const QRectF reference =
        againstCard ? m_document->geometry().boundsMm() : selectionBoundsMm();
    if (!reference.isValid())
        return;

    const QVector<ObjectId> ids = m_selection;
    const QVector<QPair<ObjectId, QJsonObject>> before =
        ModifyObjectsCommand::captureState(currentSideConstRef(), ids);
    CardSide &side = currentSideRef();
    for (const ObjectId &id : ids) {
        CardObject *object = side.object(id);
        if (!object || object->isLocked())
            continue;
        const QRectF box = object->boundingRectMm();
        double dx = 0.0;
        double dy = 0.0;
        if (flag & Qt::AlignLeft)
            dx = reference.left() - box.left();
        else if (flag & Qt::AlignRight)
            dx = reference.right() - box.right();
        else if (flag & Qt::AlignHCenter)
            dx = reference.center().x() - box.center().x();
        if (flag & Qt::AlignTop)
            dy = reference.top() - box.top();
        else if (flag & Qt::AlignBottom)
            dy = reference.bottom() - box.bottom();
        else if (flag & Qt::AlignVCenter)
            dy = reference.center().y() - box.center().y();
        object->moveByMm(dx, dy);
    }

    const QVector<QPair<ObjectId, QJsonObject>> after =
        ModifyObjectsCommand::captureState(currentSideConstRef(), ids);
    if (m_undoStack && !before.isEmpty() && !after.isEmpty()) {
        m_undoStack->push(new ModifyObjectsCommand(m_document, m_side, before, after,
                                                   tr("Align"),
                                                   QStringLiteral("align")));
    }
    m_document->notifyChanged();
    update();
}

namespace {

// Shared by the two distribute entry points: spreads the objects evenly between
// the outermost two, measuring by centre.
void distributeAlong(QVector<QPair<QRectF, CardObject *>> &objects, bool horizontal)
{
    if (objects.size() < 3)
        return;

    std::sort(objects.begin(), objects.end(),
              [horizontal](const QPair<QRectF, CardObject *> &a,
                           const QPair<QRectF, CardObject *> &b) {
                  return horizontal ? a.first.center().x() < b.first.center().x()
                                    : a.first.center().y() < b.first.center().y();
              });

    const double first = horizontal ? objects.first().first.center().x()
                                    : objects.first().first.center().y();
    const double last = horizontal ? objects.last().first.center().x()
                                   : objects.last().first.center().y();
    const double step = (last - first) / double(objects.size() - 1);

    for (int i = 1; i < objects.size() - 1; ++i) {
        const double target = first + step * i;
        const QRectF box = objects.at(i).first;
        CardObject *object = objects.at(i).second;
        if (horizontal)
            object->moveByMm(target - box.center().x(), 0.0);
        else
            object->moveByMm(0.0, target - box.center().y());
    }
}

} // namespace

void CardCanvas::distributeSelectionHorizontally()
{
    if (!m_document || m_selection.size() < 3) {
        emit statusMessage(tr("Select at least three objects to distribute them."));
        return;
    }

    const QVector<ObjectId> ids = m_selection;
    const QVector<QPair<ObjectId, QJsonObject>> before =
        ModifyObjectsCommand::captureState(currentSideConstRef(), ids);
    CardSide &side = currentSideRef();
    QVector<QPair<QRectF, CardObject *>> objects;
    for (const ObjectId &id : ids) {
        CardObject *object = side.object(id);
        if (object && !object->isLocked())
            objects.append({ object->boundingRectMm(), object });
    }
    distributeAlong(objects, true);

    const QVector<QPair<ObjectId, QJsonObject>> after =
        ModifyObjectsCommand::captureState(currentSideConstRef(), ids);
    if (m_undoStack && !before.isEmpty() && !after.isEmpty()) {
        m_undoStack->push(new ModifyObjectsCommand(m_document, m_side, before, after,
                                                   tr("Distribute Horizontally"),
                                                   QStringLiteral("distribute")));
    }
    m_document->notifyChanged();
    update();
}

void CardCanvas::distributeSelectionVertically()
{
    if (!m_document || m_selection.size() < 3) {
        emit statusMessage(tr("Select at least three objects to distribute them."));
        return;
    }

    const QVector<ObjectId> ids = m_selection;
    const QVector<QPair<ObjectId, QJsonObject>> before =
        ModifyObjectsCommand::captureState(currentSideConstRef(), ids);
    CardSide &side = currentSideRef();
    QVector<QPair<QRectF, CardObject *>> objects;
    for (const ObjectId &id : ids) {
        CardObject *object = side.object(id);
        if (object && !object->isLocked())
            objects.append({ object->boundingRectMm(), object });
    }
    distributeAlong(objects, false);

    const QVector<QPair<ObjectId, QJsonObject>> after =
        ModifyObjectsCommand::captureState(currentSideConstRef(), ids);
    if (m_undoStack && !before.isEmpty() && !after.isEmpty()) {
        m_undoStack->push(new ModifyObjectsCommand(m_document, m_side, before, after,
                                                   tr("Distribute Vertically"),
                                                   QStringLiteral("distribute")));
    }
    m_document->notifyChanged();
    update();
}

// ---------------------------------------------------------------------------
// Guides
// ---------------------------------------------------------------------------

void CardCanvas::startGuideDrag(bool horizontal, double posMm)
{
    if (!m_guides) {
        emit statusMessage(tr("Guides are not available because there is no open "
                              "document."));
        return;
    }

    // The guide is not created yet: it is previewed while the pointer moves and
    // created once, on release, as a single undoable action. That keeps Ctrl+Z
    // behaving the way a user expects after dragging a guide out.
    m_guideHorizontal = horizontal;
    m_guidePosMm = posMm;
    m_guideCreationPending = true;
    m_activeGuideId = ObjectId();
    update();
}

void CardCanvas::moveGuideDrag(bool horizontal, double posMm)
{
    if (!m_guides)
        return;
    m_guideHorizontal = horizontal;
    m_guidePosMm = posMm;
    m_guideCreationPending = true;
    emit statusMessage(horizontal ? tr("Guide at %1 mm from the top")
                                        .arg(posMm, 0, 'f', 2)
                                  : tr("Guide at %1 mm from the left")
                                        .arg(posMm, 0, 'f', 2));
    update();
}

void CardCanvas::finishGuideDrag(bool horizontal, double posMm)
{
    Q_UNUSED(horizontal);
    if (!m_guides || !m_guideCreationPending) {
        m_guideCreationPending = false;
        return;
    }
    m_guideCreationPending = false;

    // A guide dropped well outside the card is discarded: dragging a guide out has
    // to be a way of getting rid of it.
    const CardGeometry &geom = m_document ? m_document->geometry()
                                          : CardGeometry::defaultGeometry();
    const double limit = m_guideHorizontal ? geom.heightMm() : geom.widthMm();
    if (posMm < -10.0 || posMm > limit + 10.0) {
        emit statusMessage(tr("Guide discarded."));
        update();
        return;
    }

    if (m_undoStack) {
        m_undoStack->push(
            new AddGuideCommand(m_guides, m_side, m_guideHorizontal, posMm));
    } else {
        m_guides->add(m_side, m_guideHorizontal, posMm);
    }
    m_guidePosMm = posMm;
    update();
    emit viewChanged();
}

void CardCanvas::addGuide(bool horizontal, double posMm)
{
    if (!m_guides) {
        emit statusMessage(tr("Guides are not available."));
        return;
    }
    if (m_undoStack)
        m_undoStack->push(new AddGuideCommand(m_guides, m_side, horizontal, posMm));
    else
        m_guides->add(m_side, horizontal, posMm);

    emit statusMessage(horizontal ? tr("Guide added at %1 mm from the top")
                                        .arg(posMm, 0, 'f', 2)
                                  : tr("Guide added at %1 mm from the left")
                                        .arg(posMm, 0, 'f', 2));
    update();
    emit viewChanged();
}

void CardCanvas::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_document) {
        QWidget::contextMenuEvent(event);
        return;
    }

    const QPointF mm =
        CanvasGeometry::viewToMm(QPointF(event->pos()), pxPerMm(), originPx());
    CardObject *hit = m_document->side(m_side).hitTest(mm, true, true);
    if (hit && !isSelected(hit->id()))
        selectObject(hit->id());

    const bool hasSelection = !m_selection.isEmpty();
    QMenu menu(this);

    QAction *editText = nullptr;
    if (hit && dynamic_cast<TextObject *>(hit))
        editText = menu.addAction(tr("Edit text"));

    // The local names are deliberately not the member function names: a variable
    // called "bringForward" would hide bringForward() and make the call below
    // resolve to the action pointer instead.
    QAction *actionBringFront = menu.addAction(tr("Bring to Front"));
    QAction *actionBringForward = menu.addAction(tr("Bring Forward"));
    QAction *actionSendBackward = menu.addAction(tr("Send Backward"));
    QAction *actionSendBack = menu.addAction(tr("Send to Back"));
    menu.addSeparator();
    QAction *actionLock = menu.addAction(hit && hit->isLocked() ? tr("Unlock")
                                                               : tr("Lock"));
    QAction *actionHide = menu.addAction(hit && !hit->isVisible() ? tr("Show")
                                                                 : tr("Hide"));
    menu.addSeparator();
    QAction *actionDuplicate = menu.addAction(tr("Duplicate"));
    QAction *actionRemove = menu.addAction(tr("Delete"));
    menu.addSeparator();
    QAction *actionGroup = menu.addAction(tr("Group"));
    QAction *actionUngroup = menu.addAction(tr("Ungroup"));

    for (QAction *action : { actionBringFront, actionBringForward, actionSendBackward,
                             actionSendBack, actionLock, actionHide, actionDuplicate,
                             actionRemove }) {
        action->setEnabled(hasSelection);
    }
    actionGroup->setEnabled(m_selection.size() > 1);
    actionUngroup->setEnabled(hit && dynamic_cast<GroupObject *>(hit) != nullptr);

    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;
    if (editText && chosen == editText && hit)
        beginTextEditing(hit->id());
    else if (chosen == actionBringFront)
        bringToFront();
    else if (chosen == actionBringForward)
        bringForward();
    else if (chosen == actionSendBackward)
        sendBackward();
    else if (chosen == actionSendBack)
        sendToBack();
    else if (chosen == actionLock && hit)
        setSelectionLocked(!hit->isLocked());
    else if (chosen == actionHide && hit)
        setSelectionVisible(!hit->isVisible());
    else if (chosen == actionDuplicate)
        duplicateSelection();
    else if (chosen == actionRemove)
        deleteSelection();
    else if (chosen == actionGroup)
        groupSelection();
    else if (chosen == actionUngroup)
        ungroupSelection();
}

} // namespace occ
