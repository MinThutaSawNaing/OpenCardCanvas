#include "canvas/CanvasGeometry.h"

#include "core/Units.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QTransform>
#include <QtMath>

#include <cmath>

namespace occ {

namespace {

QString geometryTr(const char *text)
{
    return QCoreApplication::translate("CanvasGeometry", text);
}

// Fallback for the (test, headless) case where there is no screen to ask.
constexpr double kFallbackDpi = 96.0;

} // namespace

double CanvasGeometry::screenDpiX()
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return kFallbackDpi;
    const double dpi = screen->logicalDotsPerInchX();
    return dpi > 1.0 ? dpi : kFallbackDpi;
}

double CanvasGeometry::screenDpiY()
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return kFallbackDpi;
    const double dpi = screen->logicalDotsPerInchY();
    return dpi > 1.0 ? dpi : kFallbackDpi;
}

double CanvasGeometry::clampZoom(double zoomPercent)
{
    if (!std::isfinite(zoomPercent))
        return 100.0;
    return qBound(kMinZoomPercent, zoomPercent, kMaxZoomPercent);
}

double CanvasGeometry::pxPerMmForZoom(double zoomPercent, double devicePixelRatio)
{
    // 100 % means one PHYSICAL millimetre. A physical millimetre is
    // screenDpi/25.4 device pixels, and a device pixel is 1/dpr logical pixels,
    // which is the unit the widget works in.
    const double dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    const double oneHundredPercent = units::pxPerMmFromDpi(screenDpiX()) / dpr;
    return oneHundredPercent * clampZoom(zoomPercent) / 100.0;
}

double CanvasGeometry::zoomForPxPerMm(double pxPerMm, double devicePixelRatio)
{
    const double dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    const double oneHundredPercent = units::pxPerMmFromDpi(screenDpiX()) / dpr;
    if (oneHundredPercent <= 0.0 || !std::isfinite(pxPerMm))
        return 100.0;
    return clampZoom(pxPerMm / oneHundredPercent * 100.0);
}

QPointF CanvasGeometry::mmToView(const QPointF &mm, double pxPerMm, const QPointF &originPx)
{
    return QPointF(originPx.x() + mm.x() * pxPerMm, originPx.y() + mm.y() * pxPerMm);
}

QPointF CanvasGeometry::viewToMm(const QPointF &px, double pxPerMm, const QPointF &originPx)
{
    if (pxPerMm <= 0.0)
        return QPointF();
    return QPointF((px.x() - originPx.x()) / pxPerMm, (px.y() - originPx.y()) / pxPerMm);
}

QRectF CanvasGeometry::viewRectForMm(const QRectF &mm, double pxPerMm, const QPointF &originPx)
{
    return QRectF(originPx.x() + mm.x() * pxPerMm, originPx.y() + mm.y() * pxPerMm,
                  mm.width() * pxPerMm, mm.height() * pxPerMm);
}

QPointF CanvasGeometry::rotateHandlePosMm(const CardObject &obj)
{
    // The anchor the rotate handle hangs from: the midpoint of the object's top
    // edge, in card millimetres, already turned with the object. The visual
    // displacement that keeps the handle clear of the corner handles depends on
    // the current zoom, so it is added by handlesFor()/handleAt() through
    // rotateHandleOffsetMm().
    const QPointF centre(obj.centerXMm(), obj.centerYMm());
    QPointF offset(0.0, -(obj.heightMm() / 2.0));
    if (!qFuzzyIsNull(obj.rotationDeg())) {
        QTransform t;
        t.rotate(obj.rotationDeg());
        offset = t.map(offset);
    }
    return centre + offset;
}

double CanvasGeometry::rotateHandleOffsetMm(double pxPerMm, double handleSizePx)
{
    if (pxPerMm <= 0.0)
        return 3.0;
    // Far enough out that the rotate handle never sits on top of a corner handle,
    // and never so small that it becomes unclickable.
    return qMax(2.0, (handleSizePx * 1.4) / pxPerMm);
}

QVector<Handle> CanvasGeometry::handlesFor(const CardObject &obj, double pxPerMm,
                                          const QPointF &originPx, double handleSizePx)
{
    Q_UNUSED(originPx);
    QVector<Handle> handles;
    handles.reserve(9);

    const QRectF r = obj.rectMm();
    const QPointF centre = r.center();
    const double rotation = obj.rotationDeg();

    const auto place = [&](Handle::Id id, const QPointF &localMm) {
        QPointF point = localMm;
        if (!qFuzzyIsNull(rotation)) {
            QTransform t;
            t.translate(centre.x(), centre.y());
            t.rotate(rotation);
            t.translate(-centre.x(), -centre.y());
            point = t.map(point);
        }
        Handle handle;
        handle.id = id;
        handle.posMm = point;
        handles.append(handle);
    };

    place(Handle::TopLeft, r.topLeft());
    place(Handle::Top, QPointF(r.center().x(), r.top()));
    place(Handle::TopRight, r.topRight());
    place(Handle::Right, QPointF(r.right(), r.center().y()));
    place(Handle::BottomRight, r.bottomRight());
    place(Handle::Bottom, QPointF(r.center().x(), r.bottom()));
    place(Handle::BottomLeft, r.bottomLeft());
    place(Handle::Left, QPointF(r.left(), r.center().y()));

    // The rotate handle sits just outside the top edge, along the object's own
    // "up" direction, so it follows the object however it is rotated.
    const double offsetMm = rotateHandleOffsetMm(pxPerMm, handleSizePx);
    QPointF offset(0.0, -offsetMm);
    if (!qFuzzyIsNull(rotation)) {
        QTransform t;
        t.rotate(rotation);
        offset = t.map(offset);
    }
    Handle rotateHandle;
    rotateHandle.id = Handle::Rotate;
    rotateHandle.posMm = rotateHandlePosMm(obj) + offset;
    handles.append(rotateHandle);

    return handles;
}

Handle::Id CanvasGeometry::handleAt(const CardObject &obj, const QPointF &viewPos,
                                   double pxPerMm, const QPointF &originPx,
                                   double handleSizePx)
{
    if (pxPerMm <= 0.0)
        return Handle::None;

    // Half the handle, plus a small slop so a handle is still easy to grab on a
    // touch screen or a trackpad.
    const double radiusPx = qMax(3.0, handleSizePx / 2.0 + 1.0);
    const QVector<Handle> handles = handlesFor(obj, pxPerMm, originPx, handleSizePx);

    // Rotate wins when it overlaps a corner: it is the outer handle, and a user
    // reaching for it means it.
    Handle::Id best = Handle::None;
    double bestDistance = radiusPx * radiusPx;
    for (const Handle &handle : handles) {
        const QPointF centrePx = mmToView(handle.posMm, pxPerMm, originPx);
        const double dx = centrePx.x() - viewPos.x();
        const double dy = centrePx.y() - viewPos.y();
        const double distance = dx * dx + dy * dy;
        if (distance > bestDistance)
            continue;
        if (handle.id == Handle::Rotate) {
            best = Handle::Rotate;
            return best;
        }
        if (best == Handle::None || distance < bestDistance) {
            best = handle.id;
            bestDistance = distance;
        }
    }
    return best;
}

QSizeF CanvasGeometry::fitZoomSize(const CardGeometry &geom, const QSize &viewportPx,
                                  double devicePixelRatio, double marginPx)
{
    const double cardW = qMax(0.001, geom.widthMm());
    const double cardH = qMax(0.001, geom.heightMm());
    const double availableW = qMax(16.0, double(viewportPx.width()) - 2.0 * marginPx);
    const double availableH = qMax(16.0, double(viewportPx.height()) - 2.0 * marginPx);

    // Fit means the whole card is visible, so the smaller of the two scales wins.
    const double pxPerMm = qMin(availableW / cardW, availableH / cardH);
    const double zoom = zoomForPxPerMm(pxPerMm, devicePixelRatio);
    const double applied = pxPerMmForZoom(zoom, devicePixelRatio);

    return QSizeF(cardW * applied, cardH * applied);
}

bool CanvasGeometry::objectAt(const CardObject &obj, const QPointF &viewPos,
                              double pxPerMm, const QPointF &originPx)
{
    if (pxPerMm <= 0.0)
        return false;
    return obj.containsMm(viewToMm(viewPos, pxPerMm, originPx));
}

double CanvasGeometry::handleCursorAngleDeg(Handle::Id id)
{
    // The cursor for a handle points outward along the handle's own direction, so
    // it stays correct for a rotated object.
    switch (id) {
    case Handle::TopLeft:     return 135.0;
    case Handle::Top:         return 90.0;
    case Handle::TopRight:    return 45.0;
    case Handle::Right:       return 0.0;
    case Handle::BottomRight: return -45.0;
    case Handle::Bottom:      return -90.0;
    case Handle::BottomLeft:  return -135.0;
    case Handle::Left:        return 180.0;
    case Handle::Rotate:      return 0.0;
    case Handle::None:        return 0.0;
    }
    return 0.0;
}

QString CanvasGeometry::handleName(Handle::Id id)
{
    switch (id) {
    case Handle::TopLeft:     return geometryTr("top left");
    case Handle::Top:         return geometryTr("top");
    case Handle::TopRight:    return geometryTr("top right");
    case Handle::Right:       return geometryTr("right");
    case Handle::BottomRight: return geometryTr("bottom right");
    case Handle::Bottom:      return geometryTr("bottom");
    case Handle::BottomLeft:  return geometryTr("bottom left");
    case Handle::Left:        return geometryTr("left");
    case Handle::Rotate:      return geometryTr("rotation");
    case Handle::None:        break;
    }
    return QString();
}

} // namespace occ
