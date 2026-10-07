#pragma once

#include "core/CardGeometry.h"
#include "core/CardTypes.h"
#include "core/Units.h"

#include <QPointF>
#include <QWidget>

class QPainter;

// ---------------------------------------------------------------------------
// RulerWidget - the millimetre (or inch) ruler along the top and left edge of
// the canvas.
//
// Besides being a ruler it is an input device: dragging out of it creates a
// new guide on the side currently being edited, and dragging an existing guide
// back onto the ruler deletes it. The widget therefore emits a drag in
// progress and lets the canvas own the interaction - the ruler knows nothing
// about guides, documents or undo.
//
// Ticks follow a 1-2-5-10 progression in millimetres so the labels never
// collide however far out the user zooms: at 100 % every millimetre is labelled
// where there is room, at 20 % maybe only every fifth, at 800 % every half.
// ---------------------------------------------------------------------------
namespace occ {

class RulerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RulerWidget(Qt::Orientation orientation, QWidget *parent = nullptr);
    ~RulerWidget() override;

    Qt::Orientation orientation() const { return m_orientation; }

    // --- state --------------------------------------------------------------
    // Size of the widget in the "thin" direction, in logical pixels. Follows the
    // application font so it stays readable at 150 % scaling.
    int rulerThickness() const;

    // --- configuration ------------------------------------------------------
    void setCardGeometry(const CardGeometry &geom);
    CardGeometry cardGeometry() const { return m_geometry; }
    void setUnitDisplay(DisplayUnit unit);
    DisplayUnit unitDisplay() const { return m_unit; }
    // Logical pixels per millimetre of the canvas, and the canvas' scroll
    // offset. Together they let the ruler draw exactly the same scale as the
    // card underneath it.
    void setPxPerMm(double pxPerMm);
    double pxPerMm() const { return m_pxPerMm; }
    void setScrollOffset(double offsetPx);
    double scrollOffset() const { return m_scrollOffset; }
    // Half the width of the canvas viewport; the ruler draws a U-shaped frame
    // around the card when the viewport is wider than the content.
    void setOriginOffset(double offsetPx);
    double originOffset() const { return m_originOffset; }

    // --- indicators ---------------------------------------------------------
    // Position (mm) of the mouse cursor along this ruler's axis; negative hides
    // the indicator.
    void setCursorPosition(double mm);
    double cursorPosition() const { return m_cursorMm; }
    // Highlighted range (mm) - the current selection - drawn as a tinted band.
    void setHighlight(double fromMm, double toMm);
    void clearHighlight();

    // --- appearance ---------------------------------------------------------
    void setGuideEnabled(bool enabled);
    bool guideEnabled() const { return m_guideEnabled; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void guideDragStarted(bool horizontal, double posMm);
    void guideDragMoved(bool horizontal, double posMm);
    void guideDragFinished(bool horizontal, double posMm);
    // The user double-clicked a ruler position; the canvas creates a guide.
    void guideCreated(bool horizontal, double posMm);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    // Position along the ruler axis, in millimetres, from a widget coordinate.
    double mmFromWidgetPos(double posPx) const;
    // Widget coordinate along the ruler axis for a millimetre value.
    double widgetPosFromMm(double mm) const;
    double positionFromEvent(const QPointF &pos) const;
    void   drawTicks(QPainter &painter);
    void   drawLabels(QPainter &painter, double tickStepMm, double labelStepMm);
    void   drawChrome(QPainter &painter);

    Qt::Orientation m_orientation = Qt::Horizontal;
    CardGeometry    m_geometry;
    DisplayUnit     m_unit = DisplayUnit::Millimeters;
    double          m_pxPerMm = 4.0;
    double          m_scrollOffset = 0.0;
    double          m_originOffset = 0.0;
    double          m_cursorMm = -1.0;
    double          m_highlightFromMm = 0.0;
    double          m_highlightToMm = 0.0;
    bool            m_hasHighlight = false;
    bool            m_guideEnabled = true;
    bool            m_dragging = false;
    bool            m_dragBecameGuide = false;
    double          m_guidePosMm = 0.0;
    QPointF         m_pressPos;
};

} // namespace occ
