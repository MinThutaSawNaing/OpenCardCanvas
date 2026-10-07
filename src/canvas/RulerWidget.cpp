#include "canvas/RulerWidget.h"

#include "core/Units.h"

#include <QCoreApplication>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

namespace occ {

namespace {

QColor rulerBackground() { return QColor(246, 247, 249); }
QColor rulerBorder() { return QColor(190, 195, 202); }
QColor rulerTick() { return QColor(110, 118, 128); }
QColor rulerText() { return QColor(70, 76, 84); }
QColor rulerCursor() { return QColor(0, 120, 215); }
QColor rulerHighlight() { return QColor(0, 120, 215, 40); }

// A tick step in millimetres that keeps labels readable at the current zoom.
double chooseStepMm(double pxPerMm, double minimumSpacingPx, bool *useSubTicks)
{
    static const double candidates[] = { 0.1,  0.25, 0.5, 1.0,  2.0,  5.0,
                                         10.0, 20.0, 50.0, 100.0, 200.0, 500.0 };
    const int count = int(sizeof(candidates) / sizeof(candidates[0]));
    for (int i = 0; i < count; ++i) {
        if (candidates[i] * pxPerMm >= minimumSpacingPx) {
            if (useSubTicks) {
                *useSubTicks = candidates[i] * pxPerMm >= minimumSpacingPx * 2.0
                               && candidates[i] >= 1.0;
            }
            return candidates[i];
        }
    }
    if (useSubTicks)
        *useSubTicks = false;
    return candidates[count - 1];
}

} // namespace

RulerWidget::RulerWidget(Qt::Orientation orientation, QWidget *parent)
    : QWidget(parent), m_orientation(orientation)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setCursor(orientation == Qt::Horizontal ? Qt::SizeVerCursor : Qt::SizeHorCursor);
}

RulerWidget::~RulerWidget() = default;

int RulerWidget::rulerThickness() const
{
    // Follows the application font so the numbers stay readable at 150 % scaling.
    const QFontMetrics metrics(font());
    return qMax(18, metrics.height() + 6);
}

QSize RulerWidget::sizeHint() const
{
    return m_orientation == Qt::Horizontal ? QSize(600, rulerThickness())
                                           : QSize(rulerThickness(), 400);
}

QSize RulerWidget::minimumSizeHint() const
{
    return m_orientation == Qt::Horizontal ? QSize(40, rulerThickness())
                                           : QSize(rulerThickness(), 40);
}

void RulerWidget::setCardGeometry(const CardGeometry &geom)
{
    m_geometry = geom;
    update();
}

void RulerWidget::setUnitDisplay(DisplayUnit unit)
{
    if (m_unit == unit)
        return;
    m_unit = unit;
    update();
}

void RulerWidget::setPxPerMm(double pxPerMm)
{
    if (pxPerMm <= 0.0 || qFuzzyCompare(m_pxPerMm, pxPerMm))
        return;
    m_pxPerMm = pxPerMm;
    update();
}

void RulerWidget::setScrollOffset(double offsetPx)
{
    if (qFuzzyCompare(m_scrollOffset + 1.0, offsetPx + 1.0))
        return;
    m_scrollOffset = offsetPx;
    update();
}

void RulerWidget::setOriginOffset(double offsetPx)
{
    if (qFuzzyCompare(m_originOffset + 1.0, offsetPx + 1.0))
        return;
    m_originOffset = offsetPx;
    update();
}

void RulerWidget::setCursorPosition(double mm)
{
    if (qFuzzyCompare(m_cursorMm + 2.0, mm + 2.0))
        return;
    m_cursorMm = mm;
    update();
}

void RulerWidget::setHighlight(double fromMm, double toMm)
{
    m_highlightFromMm = qMin(fromMm, toMm);
    m_highlightToMm = qMax(fromMm, toMm);
    m_hasHighlight = true;
    update();
}

void RulerWidget::clearHighlight()
{
    if (!m_hasHighlight)
        return;
    m_hasHighlight = false;
    update();
}

void RulerWidget::setGuideEnabled(bool enabled)
{
    if (m_guideEnabled == enabled)
        return;
    m_guideEnabled = enabled;
    setCursor(enabled ? (m_orientation == Qt::Horizontal ? Qt::SizeVerCursor
                                                         : Qt::SizeHorCursor)
                      : Qt::ArrowCursor);
    update();
}

// The ruler draws exactly the same scale as the canvas underneath it: a content
// pixel c appears at (c - scrollOffset), and card millimetre 0 sits at
// originOffset inside the content.
double RulerWidget::mmFromWidgetPos(double posPx) const
{
    if (m_pxPerMm <= 0.0)
        return 0.0;
    return (posPx + m_scrollOffset - m_originOffset) / m_pxPerMm;
}

double RulerWidget::widgetPosFromMm(double mm) const
{
    return m_originOffset + mm * m_pxPerMm - m_scrollOffset;
}

double RulerWidget::positionFromEvent(const QPointF &pos) const
{
    return m_orientation == Qt::Horizontal ? pos.x() : pos.y();
}

void RulerWidget::drawTicks(QPainter &painter)
{
    const bool horizontal = m_orientation == Qt::Horizontal;
    const int thickness = rulerThickness();
    const double length = horizontal ? width() : height();

    bool useSubTicks = false;
    const double stepMm = chooseStepMm(m_pxPerMm, 46.0, &useSubTicks);
    const double subStepMm = stepMm / 5.0;

    // Minor ticks first, so a major tick always draws over one of them.
    if (useSubTicks && subStepMm * m_pxPerMm >= 5.0) {
        painter.setPen(rulerTick());
        const double firstMm = std::floor(mmFromWidgetPos(0.0) / subStepMm) * subStepMm;
        for (double mm = firstMm; mm <= mmFromWidgetPos(length) + subStepMm;
             mm += subStepMm) {
            const double pos = widgetPosFromMm(mm);
            if (pos < -1.0 || pos > length + 1.0)
                continue;
            if (horizontal) {
                painter.drawLine(QPointF(pos, thickness - 5.0),
                                 QPointF(pos, thickness - 1.0));
            } else {
                painter.drawLine(QPointF(thickness - 5.0, pos),
                                 QPointF(thickness - 1.0, pos));
            }
        }
    }

    painter.setPen(rulerTick());
    const double firstMajor = std::floor(mmFromWidgetPos(0.0) / stepMm) * stepMm;
    for (double mm = firstMajor; mm <= mmFromWidgetPos(length) + stepMm; mm += stepMm) {
        const double pos = widgetPosFromMm(mm);
        if (pos < -1.0 || pos > length + 1.0)
            continue;
        if (horizontal) {
            painter.drawLine(QPointF(pos, thickness - 10.0), QPointF(pos, thickness - 1.0));
        } else {
            painter.drawLine(QPointF(thickness - 10.0, pos), QPointF(thickness - 1.0, pos));
        }
    }
}

void RulerWidget::drawLabels(QPainter &painter, double tickStepMm, double labelStepMm)
{
    Q_UNUSED(tickStepMm);
    const bool horizontal = m_orientation == Qt::Horizontal;
    const int thickness = rulerThickness();
    const double length = horizontal ? width() : height();

    painter.setPen(rulerText());
    const int decimals = m_unit == DisplayUnit::Millimeters
                             ? (labelStepMm < 1.0 ? 1 : 0)
                             : 2;
    const double firstMajor = std::floor(mmFromWidgetPos(0.0) / labelStepMm) * labelStepMm;
    for (double mm = firstMajor; mm <= mmFromWidgetPos(length) + labelStepMm;
         mm += labelStepMm) {
        const double pos = widgetPosFromMm(mm);
        if (pos < 0.0 || pos > length)
            continue;
        const QString text = units::formatLengthNumber(mm, m_unit, decimals);
        if (horizontal) {
            painter.drawText(QRectF(pos + 2.0, 1.0, 70.0, thickness - 12.0),
                             Qt::AlignLeft | Qt::AlignVCenter, text);
        } else {
            // Vertical labels read bottom-to-top, which is what a ruler along the
            // left edge has always done.
            painter.save();
            painter.translate(thickness - 12.0, pos - 2.0);
            painter.rotate(-90.0);
            painter.drawText(QRectF(0.0, 0.0, 70.0, thickness - 12.0),
                             Qt::AlignLeft | Qt::AlignVCenter, text);
            painter.restore();
        }
    }
}

void RulerWidget::drawChrome(QPainter &painter)
{
    const bool horizontal = m_orientation == Qt::Horizontal;
    const int thickness = rulerThickness();
    const double length = horizontal ? width() : height();

    painter.setPen(Qt::NoPen);
    painter.setBrush(rulerBackground());
    painter.drawRect(rect());

    // A tinted band shows where the current selection sits on the card.
    if (m_hasHighlight && m_highlightToMm > m_highlightFromMm) {
        const double from = widgetPosFromMm(m_highlightFromMm);
        const double to = widgetPosFromMm(m_highlightToMm);
        painter.setBrush(rulerHighlight());
        if (horizontal)
            painter.drawRect(QRectF(from, 0.0, to - from, thickness));
        else
            painter.drawRect(QRectF(0.0, from, thickness, to - from));
    }

    // The edge that meets the canvas.
    painter.setPen(rulerBorder());
    if (horizontal) {
        painter.drawLine(QPointF(0.0, thickness - 0.5), QPointF(length, thickness - 0.5));
    } else {
        painter.drawLine(QPointF(thickness - 0.5, 0.0), QPointF(thickness - 0.5, length));
    }

    // The card's own extent, so the zero point and the trim edge are obvious.
    const double cardStart = widgetPosFromMm(0.0);
    const double cardEnd = widgetPosFromMm(horizontal ? m_geometry.widthMm()
                                                      : m_geometry.heightMm());
    painter.setPen(QPen(rulerBorder(), 2.0));
    if (horizontal) {
        painter.drawLine(QPointF(cardStart, 0.0), QPointF(cardStart, 6.0));
        painter.drawLine(QPointF(cardEnd, 0.0), QPointF(cardEnd, 6.0));
    } else {
        painter.drawLine(QPointF(0.0, cardStart), QPointF(6.0, cardStart));
        painter.drawLine(QPointF(0.0, cardEnd), QPointF(6.0, cardEnd));
    }

    // The cursor indicator.
    if (m_cursorMm >= -1.0) {
        const double pos = widgetPosFromMm(m_cursorMm);
        painter.setPen(QPen(rulerCursor(), 1.0));
        if (horizontal)
            painter.drawLine(QPointF(pos, 0.0), QPointF(pos, thickness));
        else
            painter.drawLine(QPointF(0.0, pos), QPointF(thickness, pos));
    }
}

void RulerWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    drawChrome(painter);

    painter.setRenderHint(QPainter::TextAntialiasing, true);
    drawTicks(painter);

    bool useSubTicks = false;
    const double labelStepMm = chooseStepMm(m_pxPerMm, 46.0, &useSubTicks);
    drawLabels(painter, labelStepMm, labelStepMm);
}

void RulerWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_guideEnabled || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_pressPos = event->position();
    m_dragging = true;
    m_dragBecameGuide = false;
    event->accept();
}

void RulerWidget::mouseMoveEvent(QMouseEvent *event)
{
    setCursorPosition(mmFromWidgetPos(positionFromEvent(event->position())));

    if (!m_dragging || !m_guideEnabled) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const bool horizontal = m_orientation == Qt::Horizontal;
    const double current = positionFromEvent(event->position());
    if (!m_dragBecameGuide) {
        // The drag becomes a guide drag as soon as the pointer has moved far
        // enough that the user clearly means "drag a guide out of the ruler".
        if (qAbs(current - positionFromEvent(m_pressPos)) < 4.0)
            return;
        m_dragBecameGuide = true;
        m_guidePosMm = mmFromWidgetPos(current);
        emit guideDragStarted(horizontal, m_guidePosMm);
    }
    m_guidePosMm = mmFromWidgetPos(current);
    emit guideDragMoved(horizontal, m_guidePosMm);
    event->accept();
}

void RulerWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging) {
        const bool horizontal = m_orientation == Qt::Horizontal;
        const bool wasGuide = m_dragBecameGuide;
        m_dragging = false;
        m_dragBecameGuide = false;
        if (wasGuide) {
            const double pos = mmFromWidgetPos(positionFromEvent(event->position()));
            emit guideDragFinished(horizontal, pos);
            event->accept();
            return;
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void RulerWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_guideEnabled || event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    // Double-clicking the ruler is the quick way to place a guide exactly where
    // the pointer is, without dragging.
    emit guideCreated(m_orientation == Qt::Horizontal,
                      mmFromWidgetPos(positionFromEvent(event->position())));
    event->accept();
}

void RulerWidget::leaveEvent(QEvent *event)
{
    setCursorPosition(-1.0);
    QWidget::leaveEvent(event);
}

void RulerWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
        updateGeometry();
        update();
    }
    QWidget::changeEvent(event);
}

void RulerWidget::wheelEvent(QWheelEvent *event)
{
    // The wheel over a ruler scrolls the canvas exactly as it does over the canvas
    // itself, so the two never fight over it.
    event->ignore();
}

} // namespace occ
