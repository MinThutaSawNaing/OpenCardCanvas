#pragma once

#include "canvas/CardCanvas.h"
#include "canvas/RulerWidget.h"
#include "core/CardGeometry.h"
#include "core/CardTypes.h"
#include "core/Units.h"

#include <QPoint>
#include <QWidget>

// ---------------------------------------------------------------------------
// CanvasHost - the canvas plus its rulers, scroll bars and corner button.
//
// This is the widget that makes the design surface feel like a drawing
// application: a ruler along the top and the left, a small corner square that
// resets the view, scroll bars that keep the rulers in step with the card, and
// guides that can be dragged out of a ruler and dropped onto the canvas.
//
// Layout notes that matter:
//   * the corner button is the standard "reset view" affordance: clicking it
//     clears the selection and fits the card to the window
//   * the rulers keep their scale and their scroll offset in step with the
//     canvas, which is why the canvas reports both through viewChanged()
//   * the canvas is a child of the scroll area's viewport, so its size follows
//     the zoom and the scroll area does the panning
// ---------------------------------------------------------------------------
class QScrollArea;
class QToolButton;

namespace occ {

class CanvasHost : public QWidget
{
    Q_OBJECT
public:
    explicit CanvasHost(QWidget *parent = nullptr);
    ~CanvasHost() override;

    CardCanvas  *canvas() const { return m_canvas; }
    RulerWidget *horizontalRuler() const { return m_hRuler; }
    RulerWidget *verticalRuler() const { return m_vRuler; }

    // --- document -----------------------------------------------------------
    void setDocument(CardDocument *doc);
    CardDocument *document() const;

    // --- display ------------------------------------------------------------
    void setUnitDisplay(DisplayUnit unit);
    DisplayUnit unitDisplay() const;
    void setShowRulers(bool on);
    bool showRulers() const;
    void setShowGrid(bool on);
    void setShowGuides(bool on);
    void setShowPrintMargins(bool on);
    void setZoomPercent(double percent);
    double zoomPercent() const;
    void fitToWindow();

    // Convenience pass-throughs so the window only has to know about the host.
    void setSnapOptions(const SnapEngine::Options &options);
    void setGuideModel(GuideModel *model);
    void setUndoStack(QUndoStack *stack);

signals:
    void statusMessage(const QString &message);
    void selectionChanged();
    void zoomChanged(double percent);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onCanvasViewChanged();
    void onCanvasZoomChanged(double percent);
    void onCanvasSelectionChanged();
    void onHorizontalScroll(int value);
    void onVerticalScroll(int value);
    void onGuideDragStarted(bool horizontal, double posMm);
    void onGuideDragMoved(bool horizontal, double posMm);
    void onGuideDragFinished(bool horizontal, double posMm);
    void onGuideCreated(bool horizontal, double posMm);

private:
    void buildUi();
    void updateRulerGeometry();
    void updateRulerState();
    void updateCornerButton();

    QScrollArea *m_scrollArea = nullptr;
    CardCanvas  *m_canvas = nullptr;
    RulerWidget *m_hRuler = nullptr;
    RulerWidget *m_vRuler = nullptr;
    QToolButton *m_cornerButton = nullptr;
    QWidget     *m_cornerWidget = nullptr;

    int  m_rulerThickness = 22;
    int  m_cornerSize = 22;
    bool m_showRulersActive = true;
};

} // namespace occ
