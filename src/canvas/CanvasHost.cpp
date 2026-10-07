#include "canvas/CanvasHost.h"

#include "canvas/GuideModel.h"
#include "core/CardDocument.h"

#include <QCoreApplication>
#include <QGridLayout>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QUndoStack>

namespace occ {

namespace {

constexpr int kCornerSize = 22;

} // namespace

CanvasHost::CanvasHost(QWidget *parent) : QWidget(parent)
{
    buildUi();
}

CanvasHost::~CanvasHost() = default;

void CanvasHost::buildUi()
{
    m_rulerThickness = qMax(18, fontMetrics().height() + 6);
    m_cornerSize = kCornerSize;

    m_canvas = new CardCanvas(this);
    m_canvas->setObjectName(QStringLiteral("cardCanvas"));
    m_canvas->setFocusPolicy(Qt::StrongFocus);
    m_canvas->setMouseTracking(true);
    m_canvas->setAcceptDrops(true);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("canvasScrollArea"));
    m_scrollArea->setWidget(m_canvas);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->viewport()->setAutoFillBackground(true);

    m_hRuler = new RulerWidget(Qt::Horizontal, this);
    m_vRuler = new RulerWidget(Qt::Vertical, this);
    m_hRuler->setObjectName(QStringLiteral("horizontalRuler"));
    m_vRuler->setObjectName(QStringLiteral("verticalRuler"));
    m_hRuler->setCardGeometry(CardGeometry::isoId1());
    m_vRuler->setCardGeometry(CardGeometry::isoId1());

    m_cornerWidget = new QWidget(this);
    m_cornerWidget->setFixedSize(m_cornerSize, m_cornerSize);
    m_cornerButton = new QToolButton(m_cornerWidget);
    m_cornerButton->setObjectName(QStringLiteral("cornerButton"));
    m_cornerButton->setAutoRaise(true);
    m_cornerButton->setFixedSize(m_cornerSize, m_cornerSize);
    m_cornerButton->setToolTip(tr("Fit the card to the window and clear the selection"));
    m_cornerButton->setText(QStringLiteral("\u25A1"));
    connect(m_cornerButton, &QToolButton::clicked, this, [this] {
        m_canvas->clearSelection();
        fitToWindow();
    });

    // The classic drawing-application arrangement: a ruler along the top and the
    // left, the corner square where they meet, and the canvas filling the rest.
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    grid->addWidget(m_cornerWidget, 0, 0);
    grid->addWidget(m_hRuler, 0, 1);
    grid->addWidget(m_vRuler, 1, 0);
    grid->addWidget(m_scrollArea, 1, 1);
    grid->setRowStretch(1, 1);
    grid->setColumnStretch(1, 1);

    connect(m_scrollArea->horizontalScrollBar(), &QScrollBar::valueChanged, this,
            &CanvasHost::onHorizontalScroll);
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &CanvasHost::onVerticalScroll);
    connect(m_canvas, &CardCanvas::viewChanged, this, &CanvasHost::onCanvasViewChanged);
    connect(m_canvas, &CardCanvas::zoomChanged, this, &CanvasHost::onCanvasZoomChanged);
    connect(m_canvas, &CardCanvas::selectionChanged, this,
            &CanvasHost::onCanvasSelectionChanged);
    connect(m_canvas, &CardCanvas::statusMessage, this, &CanvasHost::statusMessage);
    connect(m_canvas, &CardCanvas::mousePositionChanged, this, [this](const QPointF &mm) {
        m_hRuler->setCursorPosition(mm.x());
        m_vRuler->setCursorPosition(mm.y());
    });

    for (RulerWidget *ruler : { m_hRuler, m_vRuler }) {
        connect(ruler, &RulerWidget::guideDragStarted, this,
                &CanvasHost::onGuideDragStarted);
        connect(ruler, &RulerWidget::guideDragMoved, this, &CanvasHost::onGuideDragMoved);
        connect(ruler, &RulerWidget::guideDragFinished, this,
                &CanvasHost::onGuideDragFinished);
        connect(ruler, &RulerWidget::guideCreated, this, &CanvasHost::onGuideCreated);
    }

    updateRulerGeometry();
    updateRulerState();
}

void CanvasHost::setDocument(CardDocument *doc)
{
    m_canvas->setDocument(doc);
    if (doc) {
        m_hRuler->setCardGeometry(doc->geometry());
        m_vRuler->setCardGeometry(doc->geometry());
    }
    updateRulerState();
}

CardDocument *CanvasHost::document() const
{
    return m_canvas->document();
}

void CanvasHost::setUnitDisplay(DisplayUnit unit)
{
    m_canvas->setUnitDisplay(unit);
    m_hRuler->setUnitDisplay(unit);
    m_vRuler->setUnitDisplay(unit);
    updateRulerState();
}

DisplayUnit CanvasHost::unitDisplay() const
{
    return m_canvas->unitDisplay();
}

void CanvasHost::setShowRulers(bool on)
{
    m_showRulersActive = on;
    m_cornerWidget->setVisible(on);
    m_hRuler->setVisible(on);
    m_vRuler->setVisible(on);
    m_canvas->setShowRulers(on);
    updateRulerGeometry();
    updateRulerState();
}

bool CanvasHost::showRulers() const
{
    return m_showRulersActive;
}

void CanvasHost::setShowGrid(bool on)
{
    m_canvas->setShowGrid(on);
}

void CanvasHost::setShowGuides(bool on)
{
    m_canvas->setShowGuides(on);
    m_hRuler->setGuideEnabled(on);
    m_vRuler->setGuideEnabled(on);
}

void CanvasHost::setShowPrintMargins(bool on)
{
    m_canvas->setShowPrintMargins(on);
}

void CanvasHost::setSnapOptions(const SnapEngine::Options &options)
{
    m_canvas->setSnapOptions(options);
}

void CanvasHost::setGuideModel(GuideModel *model)
{
    m_canvas->setGuideModel(model);
}

void CanvasHost::setUndoStack(QUndoStack *stack)
{
    m_canvas->setUndoStack(stack);
}

void CanvasHost::setZoomPercent(double percent)
{
    m_canvas->setZoomPercent(percent);
}

double CanvasHost::zoomPercent() const
{
    return m_canvas->zoomPercent();
}

void CanvasHost::fitToWindow()
{
    m_canvas->fitToWindow();
}

void CanvasHost::updateRulerGeometry()
{
    if (!m_showRulersActive)
        return;
    m_rulerThickness = m_hRuler->rulerThickness();
    m_hRuler->setFixedHeight(m_rulerThickness);
    m_vRuler->setFixedWidth(m_rulerThickness);
    m_cornerWidget->setFixedSize(m_rulerThickness, m_rulerThickness);
    m_cornerButton->setFixedSize(m_rulerThickness, m_rulerThickness);
}

void CanvasHost::updateRulerState()
{
    // The rulers draw exactly the same scale and offset as the canvas, which is
    // what makes a tick line up with an object edge.
    const double pxPerMm = m_canvas->pxPerMm();
    const QPointF origin = m_canvas->originPx();
    m_hRuler->setPxPerMm(pxPerMm);
    m_vRuler->setPxPerMm(pxPerMm);
    m_hRuler->setOriginOffset(origin.x());
    m_vRuler->setOriginOffset(origin.y());

    if (const CardDocument *doc = m_canvas->document()) {
        m_hRuler->setCardGeometry(doc->geometry());
        m_vRuler->setCardGeometry(doc->geometry());
    }

    onHorizontalScroll(m_scrollArea->horizontalScrollBar()->value());
    onVerticalScroll(m_scrollArea->verticalScrollBar()->value());

    // The selection is shown as a band on both rulers, so the user can see how
    // wide a block of objects really is without measuring anything.
    const QVector<CardObject *> selected = m_canvas->selection();
    if (selected.isEmpty()) {
        m_hRuler->clearHighlight();
        m_vRuler->clearHighlight();
        return;
    }
    QRectF bounds;
    bool first = true;
    for (const CardObject *object : selected) {
        if (!object)
            continue;
        const QRectF box = object->boundingRectMm();
        bounds = first ? box : bounds.united(box);
        first = false;
    }
    if (!first) {
        m_hRuler->setHighlight(bounds.left(), bounds.right());
        m_vRuler->setHighlight(bounds.top(), bounds.bottom());
    }
}

void CanvasHost::onCanvasViewChanged()
{
    updateRulerState();
}

void CanvasHost::onCanvasZoomChanged(double percent)
{
    updateRulerState();
    emit zoomChanged(percent);
}

void CanvasHost::onCanvasSelectionChanged()
{
    updateRulerState();
    emit selectionChanged();
}

void CanvasHost::onHorizontalScroll(int value)
{
    m_hRuler->setScrollOffset(double(value));
    m_canvas->setScrollOffsets(
        QPoint(m_scrollArea->horizontalScrollBar()->value(),
               m_scrollArea->verticalScrollBar()->value()));
}

void CanvasHost::onVerticalScroll(int value)
{
    m_vRuler->setScrollOffset(double(value));
    m_canvas->setScrollOffsets(
        QPoint(m_scrollArea->horizontalScrollBar()->value(),
               m_scrollArea->verticalScrollBar()->value()));
}

void CanvasHost::onGuideDragStarted(bool horizontal, double posMm)
{
    m_canvas->startGuideDrag(horizontal, posMm);
}

void CanvasHost::onGuideDragMoved(bool horizontal, double posMm)
{
    m_canvas->moveGuideDrag(horizontal, posMm);
}

void CanvasHost::onGuideDragFinished(bool horizontal, double posMm)
{
    m_canvas->finishGuideDrag(horizontal, posMm);
}

void CanvasHost::onGuideCreated(bool horizontal, double posMm)
{
    m_canvas->addGuide(horizontal, posMm);
}

void CanvasHost::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateRulerGeometry();
    updateRulerState();
}

void CanvasHost::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
}

void CanvasHost::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        updateRulerGeometry();
    QWidget::changeEvent(event);
}

bool CanvasHost::eventFilter(QObject *watched, QEvent *event)
{
    return QWidget::eventFilter(watched, event);
}

} // namespace occ
