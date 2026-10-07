#pragma once

#include "core/CardGeometry.h"
#include "core/CardObject.h"
#include "core/CardTypes.h"

#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QVector>

// ---------------------------------------------------------------------------
// CanvasGeometry - all of the canvas arithmetic, with no widget state.
//
// Every function is pure. That is a deliberate testability decision: handle
// placement, hit testing and the mm <-> pixel transform are the parts of a
// canvas that are easy to get subtly wrong (one pixel off, only at 150 %
// zoom, only on a HiDPI screen) and are impossible to test when they are
// entangled with mouse events and widget members.
//
// The zoom convention: 100 % means ONE PHYSICAL MILLIMETRE. Because Qt reports
// sizes in logical pixels, a 100 % card is devicePixelRatio times as many
// logical pixels as there are millimetres, which is what makes the on-screen
// card actually card sized on a calibrated display.
// ---------------------------------------------------------------------------
namespace occ {

struct Handle
{
    enum Id {
        None = 0,
        TopLeft,
        Top,
        TopRight,
        Right,
        BottomRight,
        Bottom,
        BottomLeft,
        Left,
        Rotate
    };

    Id      id = None;
    QPointF posMm;      // handle centre, in card millimetres
};

class CanvasGeometry
{
public:
    // --- screen resolution --------------------------------------------------
    // Physical pixels per millimetre of the primary screen, derived from its
    // logical DPI. Falls back to 96 dpi when there is no screen (tests).
    static double screenDpiX();
    static double screenDpiY();

    // --- zoom ---------------------------------------------------------------
    // pxPerMm (in logical pixels) for a requested zoom percentage.
    static double pxPerMmForZoom(double zoomPercent, double devicePixelRatio);
    static double zoomForPxPerMm(double pxPerMm, double devicePixelRatio);

    static constexpr double kMinZoomPercent = 5.0;
    static constexpr double kMaxZoomPercent = 3200.0;
    static double clampZoom(double zoomPercent);

    // --- transforms ---------------------------------------------------------
    // `originPx` is the position of the card's top-left corner inside the
    // viewport, in logical pixels. It is the only piece of state the transform
    // needs, which is why panning is just a different origin.
    static QPointF mmToView(const QPointF &mm, double pxPerMm, const QPointF &originPx);
    static QPointF viewToMm(const QPointF &px, double pxPerMm, const QPointF &originPx);
    static QRectF  viewRectForMm(const QRectF &mm, double pxPerMm, const QPointF &originPx);

    // --- handles ------------------------------------------------------------
    // The eight resize handles plus the rotate handle, in millimetres so they
    // follow rotation exactly. `handleSizePx` is the on-screen square size and
    // is converted with pxPerMm, which keeps the handles the same size on
    // screen at every zoom level.
    static QVector<Handle> handlesFor(const CardObject &obj, double pxPerMm,
                                      const QPointF &originPx, double handleSizePx);
    // Which handle, if any, is under `viewPos`. Rotate wins over the corner
    // handles when they overlap, because rotate is the outer one.
    static Handle::Id handleAt(const CardObject &obj, const QPointF &viewPos,
                              double pxPerMm, const QPointF &originPx, double handleSizePx);
    // Position of the rotate handle for an object, in millimetres.
    static QPointF rotateHandlePosMm(const CardObject &obj);
    // Distance of the rotate handle above the object's top edge, in millimetres.
    static double rotateHandleOffsetMm(double pxPerMm, double handleSizePx);

    // --- fitting ------------------------------------------------------------
    // Size of the scrollable content for a card at the zoom that best fits
    // `viewportPx` with `marginPx` of breathing space. The caller derives the
    // zoom back with pxPerMm = sizePx / widthMm and zoomForPxPerMm().
    static QSizeF fitZoomSize(const CardGeometry &geom, const QSize &viewportPx,
                              double devicePixelRatio, double marginPx);

    // --- hit testing --------------------------------------------------------
    // True when `viewPos` is inside the object, honouring rotation.
    static bool objectAt(const CardObject &obj, const QPointF &viewPos,
                         double pxPerMm, const QPointF &originPx);

    // --- helpers ------------------------------------------------------------
    // Converts a Qt alignment value's meaning into the two independent booleans
    // used by the resize code.
    static double handleCursorAngleDeg(Handle::Id id);
    // Human readable handle name, used in the status bar while dragging.
    static QString handleName(Handle::Id id);
};

} // namespace occ
