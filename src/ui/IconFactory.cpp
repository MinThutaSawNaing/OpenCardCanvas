#include "ui/IconFactory.h"

#include <QApplication>
#include <QFont>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPolygonF>
#include <QtMath>

// Explicit initialization keeps the resource object linked from occcore's
// static archive, including in tests that use IconFactory without main.cpp.
static void initializeBranding()
{
    Q_INIT_RESOURCE(branding);
}

namespace occ {
namespace {

// The design space every glyph is drawn in. Sizes are rasterised by scaling
// this space, so a glyph is defined once and stays proportionate.
constexpr double kDesign = 24.0;
constexpr double kPen    = 2.1;

// Sizes we rasterise. QIcon scales between them, and every one of these is a
// size Windows actually requests.
const int kSizes[] = { 16, 20, 24, 32, 48, 64 };

QColor glyphColor()
{
    if (qApp)
        return QApplication::palette().color(QPalette::WindowText);
    return QColor(40, 40, 40);
}

// ---------------------------------------------------------------------------
// Small drawing helpers, all in design coordinates.
// ---------------------------------------------------------------------------

QPen strokePen(const QPainter &painter, double width = kPen)
{
    QPen pen(painter.pen());
    pen.setWidthF(width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

QPainterPath roundedPath(double x, double y, double w, double h, double r)
{
    QPainterPath path;
    path.addRoundedRect(QRectF(x, y, w, h), r, r);
    return path;
}

// Arrow head at `tip`, pointing along `angleDeg` (0 = to the right).
void drawArrowHead(QPainter &p, const QPointF &tip, double angleDeg, double len = 3.6)
{
    const double rad = qDegreesToRadians(angleDeg);
    const QPointF back(qCos(rad), qSin(rad));
    const QPointF side(-back.y(), back.x());
    const QPointF base = tip - back * len;
    QPolygonF head;
    head << tip << (base + side * (len * 0.55)) << (base - side * (len * 0.55));
    p.setBrush(p.pen().color());
    p.drawPolygon(head);
    p.setBrush(Qt::NoBrush);
}

void drawArrow(QPainter &p, const QPointF &from, const QPointF &to, double len = 3.6)
{
    p.drawLine(from, to);
    const QPointF d = to - from;
    drawArrowHead(p, to, qRadiansToDegrees(qAtan2(d.y(), d.x())), len);
}

// Magnifier glass body: circle plus handle.
void drawMagnifier(QPainter &p, const QPointF &centre, double radius, double handleAngleDeg)
{
    p.drawEllipse(centre, radius, radius);
    const double rad = qDegreesToRadians(handleAngleDeg);
    const QPointF dir(qCos(rad), qSin(rad));
    p.drawLine(centre + dir * radius, centre + dir * (radius + 3.4));
}

// A card outline used by several icons.
QPainterPath cardPath(double x, double y, double w, double h)
{
    return roundedPath(x, y, w, h, 2.0);
}

void drawTextGlyph(QPainter &p, const QString &text, const QRectF &box, bool bold = true)
{
    QFont font = p.font();
    font.setPointSizeF(14.0);
    font.setBold(bold);
    p.setFont(font);
    p.drawText(box, Qt::AlignCenter, text);
}

} // namespace

// ---------------------------------------------------------------------------
// The glyph switch. Everything is drawn in the 0..24 design space with a pen
// whose colour is the current text colour.
// ---------------------------------------------------------------------------
namespace {

void drawGlyph(QPainter &p, const QString &name)
{
    const QColor fg = p.pen().color();
    QPainterPath path;

    if (name == QLatin1String("new")) {
        path.moveTo(5, 2.5);
        path.lineTo(14.5, 2.5);
        path.lineTo(19, 7);
        path.lineTo(19, 21.5);
        path.lineTo(5, 21.5);
        path.closeSubpath();
        p.drawPath(path);
        p.drawPolyline(QPolygonF() << QPointF(14.5, 2.5) << QPointF(14.5, 7) << QPointF(19, 7));
    } else if (name == QLatin1String("open")) {
        path.moveTo(2.5, 6.5);
        path.lineTo(9, 6.5);
        path.lineTo(11, 9);
        path.lineTo(21.5, 9);
        path.lineTo(21.5, 19.5);
        path.lineTo(2.5, 19.5);
        path.closeSubpath();
        p.drawPath(path);
    } else if (name == QLatin1String("save")) {
        p.drawPath(roundedPath(3.5, 3.5, 17, 17, 2.0));
        p.drawRect(QRectF(8, 3.5, 8, 5.5));
        p.drawRect(QRectF(6.5, 13, 11, 7.5));
        p.drawLine(QPointF(9.5, 15.5), QPointF(14.5, 15.5));
        p.drawLine(QPointF(9.5, 18), QPointF(14.5, 18));
    } else if (name == QLatin1String("save_as")) {
        p.drawPath(roundedPath(2.5, 3.5, 13, 13, 1.8));
        p.drawRect(QRectF(6, 3.5, 6, 4));
        p.drawRect(QRectF(4.5, 11, 9, 5.5));
        drawArrow(p, QPointF(19.5, 11), QPointF(19.5, 20.5), 3.4);
        p.drawLine(QPointF(15, 21.5), QPointF(22, 21.5));
    } else if (name == QLatin1String("undo")) {
        path.arcMoveTo(QRectF(5.5, 6.0, 13, 12), 160);
        path.arcTo(QRectF(5.5, 6.0, 13, 12), 160, -250);
        p.drawPath(path);
        p.setBrush(fg);
        p.drawPolygon(QPolygonF() << QPointF(5.6, 13.4) << QPointF(10.0, 8.2)
                                  << QPointF(10.4, 14.4));
        p.setBrush(Qt::NoBrush);
    } else if (name == QLatin1String("redo")) {
        path.arcMoveTo(QRectF(5.5, 6.0, 13, 12), 20);
        path.arcTo(QRectF(5.5, 6.0, 13, 12), 20, 250);
        p.drawPath(path);
        p.setBrush(fg);
        p.drawPolygon(QPolygonF() << QPointF(18.4, 13.4) << QPointF(13.6, 14.4)
                                  << QPointF(14.0, 8.2));
        p.setBrush(Qt::NoBrush);
    } else if (name == QLatin1String("cut")) {
        p.drawLine(QPointF(4.5, 3.5), QPointF(16.5, 15.5));
        p.drawLine(QPointF(19.5, 3.5), QPointF(7.5, 15.5));
        p.drawEllipse(QPointF(5.5, 19.5), 2.4, 2.4);
        p.drawEllipse(QPointF(18.5, 19.5), 2.4, 2.4);
    } else if (name == QLatin1String("copy")) {
        p.drawPath(roundedPath(3.5, 3.5, 12, 14, 1.8));
        p.drawPath(roundedPath(8.5, 7.5, 12, 14, 1.8));
    } else if (name == QLatin1String("paste")) {
        p.drawPath(roundedPath(4.5, 4.5, 15, 17, 2.0));
        p.drawPath(roundedPath(8.5, 2, 7, 4.5, 1.4));
        p.drawLine(QPointF(8, 12), QPointF(16, 12));
        p.drawLine(QPointF(8, 15.5), QPointF(16, 15.5));
    } else if (name == QLatin1String("duplicate")) {
        p.drawPath(roundedPath(3, 3, 11.5, 13.5, 1.8));
        p.drawPath(roundedPath(8, 7.5, 11.5, 13.5, 1.8));
        p.drawLine(QPointF(18.5, 15.5), QPointF(18.5, 21.5));
        p.drawLine(QPointF(15.5, 18.5), QPointF(21.5, 18.5));
    } else if (name == QLatin1String("delete")) {
        p.drawLine(QPointF(3.5, 6.5), QPointF(20.5, 6.5));
        p.drawPolyline(QPolygonF() << QPointF(9, 6.5) << QPointF(9.5, 3) << QPointF(14.5, 3)
                                   << QPointF(15, 6.5));
        path.moveTo(6, 6.5);
        path.lineTo(7.5, 21.5);
        path.lineTo(16.5, 21.5);
        path.lineTo(18, 6.5);
        p.drawPath(path);
        p.drawLine(QPointF(10.2, 10), QPointF(10.7, 18.5));
        p.drawLine(QPointF(14.2, 10), QPointF(13.7, 18.5));
    } else if (name == QLatin1String("select")) {
        QPolygonF arrow;
        arrow << QPointF(6, 3) << QPointF(6, 19.5) << QPointF(10.2, 15.6) << QPointF(13, 21.8)
              << QPointF(15.8, 20.5) << QPointF(13, 14.6) << QPointF(18.4, 14.2);
        p.setBrush(fg);
        p.drawPolygon(arrow);
        p.setBrush(Qt::NoBrush);
    } else if (name == QLatin1String("text")) {
        p.drawLine(QPointF(4.5, 5), QPointF(19.5, 5));
        p.drawLine(QPointF(12, 5), QPointF(12, 20));
        p.drawLine(QPointF(8, 20), QPointF(16, 20));
    } else if (name == QLatin1String("image")) {
        p.drawPath(roundedPath(2.5, 4, 19, 16, 2.0));
        path.moveTo(4.5, 17.5);
        path.lineTo(9.5, 11.5);
        path.lineTo(13.5, 16);
        path.lineTo(16.5, 13);
        path.lineTo(19.5, 17.5);
        p.drawPath(path);
        p.setBrush(fg);
        p.drawEllipse(QPointF(8, 8.5), 1.6, 1.6);
        p.setBrush(Qt::NoBrush);
    } else if (name == QLatin1String("photo")) {
        p.drawPath(roundedPath(2.5, 4.5, 19, 15, 2.0));
        p.drawLine(QPointF(2.5, 17.5), QPointF(21.5, 17.5));
        p.setBrush(fg);
        p.drawEllipse(QPointF(12, 9.5), 2.6, 2.6);
        p.setBrush(Qt::NoBrush);
        path.arcMoveTo(QRectF(6.5, 12.5, 11, 9.4), 180);
        path.arcTo(QRectF(6.5, 12.5, 11, 9.4), 180, -180);
        p.drawPath(path);
    } else if (name == QLatin1String("shape")) {
        p.drawPath(roundedPath(3, 3, 12.5, 12.5, 1.6));
        p.drawEllipse(QPointF(15.5, 15.5), 5.8, 5.8);
    } else if (name == QLatin1String("qr")) {
        const double s = 5.4;
        p.drawRect(QRectF(3, 3, s, s));
        p.drawRect(QRectF(15.6, 3, s, s));
        p.drawRect(QRectF(3, 15.6, s, s));
        p.setBrush(fg);
        p.drawRect(QRectF(5, 5, 1.4, 1.4));
        p.drawRect(QRectF(17.6, 5, 1.4, 1.4));
        p.drawRect(QRectF(5, 17.6, 1.4, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(14.5, 14.5, 2.2, 2.2));
        p.drawRect(QRectF(18.6, 15.6, 2.2, 2.2));
        p.drawRect(QRectF(15.6, 19, 2.2, 2.2));
    } else if (name == QLatin1String("barcode")) {
        p.drawLine(QPointF(3.5, 5), QPointF(3.5, 19));
        p.drawLine(QPointF(5.5, 5), QPointF(5.5, 19));
        p.drawLine(QPointF(8.5, 5), QPointF(8.5, 19));
        p.drawLine(QPointF(10.5, 5), QPointF(10.5, 19));
        p.drawLine(QPointF(13.5, 5), QPointF(13.5, 19));
        p.drawLine(QPointF(15, 5), QPointF(15, 19));
        p.drawLine(QPointF(18, 5), QPointF(18, 19));
        p.drawLine(QPointF(20.5, 5), QPointF(20.5, 19));
    } else if (name == QLatin1String("group")) {
        p.drawPath(roundedPath(3.5, 3.5, 8, 8, 1.2));
        p.drawPath(roundedPath(12.5, 12.5, 8, 8, 1.2));
        QPen dash = p.pen();
        dash.setStyle(Qt::DashLine);
        dash.setWidthF(1.2);
        p.setPen(dash);
        p.drawRect(QRectF(1.5, 1.5, 21, 21));
        p.setPen(strokePen(p));
    } else if (name == QLatin1String("ungroup")) {
        p.drawPath(roundedPath(2.5, 9.5, 7, 7, 1.2));
        p.drawPath(roundedPath(14.5, 9.5, 7, 7, 1.2));
        drawArrow(p, QPointF(11, 4), QPointF(5.5, 4), 2.6);
        drawArrow(p, QPointF(13, 4), QPointF(18.5, 4), 2.6);
    } else if (name == QLatin1String("lock")) {
        path.moveTo(7.5, 10.5);
        path.lineTo(7.5, 7.5);
        path.arcTo(QRectF(7.5, 2.5, 9, 9), 180, 180);
        path.lineTo(16.5, 10.5);
        p.drawPath(path);
        p.drawPath(roundedPath(4.5, 10.5, 15, 10.5, 2.0));
        p.setBrush(fg);
        p.drawEllipse(QPointF(12, 15.5), 1.5, 1.5);
        p.setBrush(Qt::NoBrush);
    } else if (name == QLatin1String("unlock")) {
        path.moveTo(7.5, 10.5);
        path.lineTo(7.5, 7.5);
        path.arcTo(QRectF(7.5, 2.5, 9, 9), 180, 130);
        p.drawPath(path);
        p.drawPath(roundedPath(4.5, 10.5, 15, 10.5, 2.0));
        p.setBrush(fg);
        p.drawEllipse(QPointF(12, 15.5), 1.5, 1.5);
        p.setBrush(Qt::NoBrush);
    } else if (name == QLatin1String("visible") || name == QLatin1String("hidden")) {
        path.moveTo(2.5, 12);
        path.quadTo(12, 3.5, 21.5, 12);
        path.quadTo(12, 20.5, 2.5, 12);
        p.drawPath(path);
        p.setBrush(fg);
        p.drawEllipse(QPointF(12, 12), 2.6, 2.6);
        p.setBrush(Qt::NoBrush);
        if (name == QLatin1String("hidden"))
            p.drawLine(QPointF(4, 20.5), QPointF(20, 3.5));
    } else if (name == QLatin1String("zoom_in") || name == QLatin1String("zoom_out")) {
        drawMagnifier(p, QPointF(10, 10), 6.2, 45.0);
        p.drawLine(QPointF(6.6, 10), QPointF(13.4, 10));
        if (name == QLatin1String("zoom_in"))
            p.drawLine(QPointF(10, 6.6), QPointF(10, 13.4));
    } else if (name == QLatin1String("zoom_fit")) {
        p.drawPath(roundedPath(7, 7, 10, 10, 1.4));
        drawArrow(p, QPointF(6, 8), QPointF(1.8, 3.8), 2.8);
        drawArrow(p, QPointF(18, 8), QPointF(22.2, 3.8), 2.8);
        drawArrow(p, QPointF(6, 16), QPointF(1.8, 20.2), 2.8);
        drawArrow(p, QPointF(18, 16), QPointF(22.2, 20.2), 2.8);
    } else if (name == QLatin1String("front")) {
        p.drawPath(cardPath(2.5, 5, 19, 14));
        p.drawPath(roundedPath(5, 7.5, 5.5, 7, 1.0));
        p.drawLine(QPointF(12.5, 8.5), QPointF(19, 8.5));
        p.drawLine(QPointF(12.5, 12), QPointF(19, 12));
        p.drawLine(QPointF(5, 16.5), QPointF(19, 16.5));
    } else if (name == QLatin1String("back")) {
        p.drawPath(cardPath(2.5, 5, 19, 14));
        p.drawLine(QPointF(5, 8.5), QPointF(19, 8.5));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        p.drawLine(QPointF(5, 15.5), QPointF(14, 15.5));
    } else if (name == QLatin1String("print")) {
        p.drawPath(roundedPath(3.5, 8.5, 17, 9, 1.6));
        p.drawPath(roundedPath(6.5, 3, 11, 5.5, 1.0));
        p.drawRect(QRectF(7.5, 15, 9, 6));
        p.setBrush(fg);
        p.drawEllipse(QPointF(17.5, 11.5), 1.2, 1.2);
        p.setBrush(Qt::NoBrush);
    } else if (name == QLatin1String("printer")) {
        p.drawPath(roundedPath(3.5, 8.5, 17, 9.5, 1.6));
        p.drawPath(roundedPath(6.5, 3, 11, 5.5, 1.0));
        p.drawRect(QRectF(6.5, 6, 11, 3));
        p.setBrush(fg);
        p.drawEllipse(QPointF(17.5, 12), 1.2, 1.2);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(6.5, 18), QPointF(17.5, 18));
    } else if (name == QLatin1String("preview")) {
        p.drawPath(roundedPath(3.5, 2.5, 12, 19, 1.6));
        p.drawLine(QPointF(6, 7), QPointF(13, 7));
        p.drawLine(QPointF(6, 10.5), QPointF(13, 10.5));
        drawMagnifier(p, QPointF(15.5, 15.5), 4.6, 45.0);
    } else if (name == QLatin1String("export")) {
        p.drawPath(roundedPath(2.5, 14.5, 19, 6.5, 1.2));
        drawArrow(p, QPointF(12, 13.5), QPointF(12, 3), 3.6);
    } else if (name == QLatin1String("data")) {
        p.drawEllipse(QRectF(3.5, 3, 17, 6));
        path.moveTo(3.5, 6);
        path.lineTo(3.5, 18);
        path.arcTo(QRectF(3.5, 15, 17, 6), 180, 180);
        path.lineTo(20.5, 6);
        p.drawPath(path);
        QPainterPath band;
        band.arcMoveTo(QRectF(3.5, 9, 17, 6), 180);
        band.arcTo(QRectF(3.5, 9, 17, 6), 180, 180);
        p.drawPath(band);
    } else if (name == QLatin1String("snap")) {
        QPen dash = p.pen();
        dash.setStyle(Qt::DashLine);
        p.setPen(dash);
        p.drawLine(QPointF(12, 2.5), QPointF(12, 21.5));
        p.drawLine(QPointF(2.5, 12), QPointF(21.5, 12));
        p.setPen(strokePen(p));
        p.setBrush(fg);
        p.drawEllipse(QPointF(12, 12), 2.4, 2.4);
        p.setBrush(Qt::NoBrush);
    } else if (name == QLatin1String("grid")) {
        p.drawRect(QRectF(3.5, 3.5, 17, 17));
        p.drawLine(QPointF(9.2, 3.5), QPointF(9.2, 20.5));
        p.drawLine(QPointF(14.8, 3.5), QPointF(14.8, 20.5));
        p.drawLine(QPointF(3.5, 9.2), QPointF(20.5, 9.2));
        p.drawLine(QPointF(3.5, 14.8), QPointF(20.5, 14.8));
    } else if (name == QLatin1String("guide")) {
        p.drawPath(cardPath(2.5, 5.5, 19, 13));
        QPen dash = p.pen();
        dash.setStyle(Qt::DashLine);
        p.setPen(dash);
        p.drawLine(QPointF(12, 2), QPointF(12, 22));
        p.setPen(strokePen(p));
        p.setBrush(fg);
        p.drawEllipse(QPointF(12, 2.6), 1.3, 1.3);
        p.drawEllipse(QPointF(12, 21.4), 1.3, 1.3);
        p.setBrush(Qt::NoBrush);
    } else if (name == QLatin1String("align_left") || name == QLatin1String("align_right")
               || name == QLatin1String("align_center_h")) {
        const bool left  = name == QLatin1String("align_left");
        const bool right = name == QLatin1String("align_right");
        const double ref = left ? 3.0 : (right ? 21.0 : 12.0);
        p.drawLine(QPointF(ref, 2.5), QPointF(ref, 21.5));
        const double lengths[2] = { 13.0, 8.0 };
        for (int i = 0; i < 2; ++i) {
            const double len = lengths[i];
            const double x = left ? ref : (right ? ref - len : ref - len / 2.0);
            p.setBrush(fg);
            p.drawRect(QRectF(x, 5.0 + i * 10.0, len, 4.0));
            p.setBrush(Qt::NoBrush);
        }
    } else if (name == QLatin1String("align_top") || name == QLatin1String("align_bottom")
               || name == QLatin1String("align_middle_v")) {
        const bool top    = name == QLatin1String("align_top");
        const bool bottom = name == QLatin1String("align_bottom");
        const double ref  = top ? 3.0 : (bottom ? 21.0 : 12.0);
        p.drawLine(QPointF(2.5, ref), QPointF(21.5, ref));
        const double lengths[2] = { 13.0, 8.0 };
        for (int i = 0; i < 2; ++i) {
            const double len = lengths[i];
            const double y = top ? ref : (bottom ? ref - len : ref - len / 2.0);
            p.setBrush(fg);
            p.drawRect(QRectF(5.0 + i * 10.0, y, 4.0, len));
            p.setBrush(Qt::NoBrush);
        }
    } else if (name == QLatin1String("distribute_h") || name == QLatin1String("distribute_v")) {
        const bool horizontal = name == QLatin1String("distribute_h");
        p.setBrush(fg);
        for (int i = 0; i < 3; ++i) {
            const double pos = 3.0 + i * 9.0;
            if (horizontal)
                p.drawRect(QRectF(pos - 1.5, 3.5, 3.0, 17.0));
            else
                p.drawRect(QRectF(3.5, pos - 1.5, 17.0, 3.0));
        }
        p.setBrush(Qt::NoBrush);
    } else if (name == QLatin1String("bring_front") || name == QLatin1String("bring_forward")
               || name == QLatin1String("send_backward") || name == QLatin1String("send_back")) {
        const bool up = name == QLatin1String("bring_front")
                        || name == QLatin1String("bring_forward");
        if (name == QLatin1String("bring_front"))
            p.drawPath(roundedPath(2.5, 2.5, 13, 13, 1.6));
        else if (name == QLatin1String("send_back"))
            p.drawPath(roundedPath(8.5, 8.5, 13, 13, 1.6));
        p.drawPath(roundedPath(6.5, 6.5, 13, 13, 1.6));
        if (up) {
            p.drawLine(QPointF(12, 20.5), QPointF(12, 3.5));
            drawArrowHead(p, QPointF(12, 3.5), -90, 3.4);
        } else {
            p.drawLine(QPointF(12, 3.5), QPointF(12, 20.5));
            drawArrowHead(p, QPointF(12, 20.5), 90, 3.4);
        }
    } else if (name == QLatin1String("template")) {
        p.drawPath(roundedPath(3.5, 2.5, 17, 19, 1.8));
        QPen dash = p.pen();
        dash.setStyle(Qt::DashLine);
        dash.setWidthF(1.1);
        p.setPen(dash);
        p.drawLine(QPointF(3.5, 8.5), QPointF(20.5, 8.5));
        p.drawLine(QPointF(11, 8.5), QPointF(11, 21.5));
        p.setPen(strokePen(p));
        p.drawRect(QRectF(6, 12, 3, 3));
        p.drawRect(QRectF(6, 17, 3, 3));
    } else if (name == QLatin1String("settings")) {
        const QPointF centre(12, 12);
        for (int i = 0; i < 8; ++i) {
            const double a = qDegreesToRadians(i * 45.0);
            const QPointF dir(qCos(a), qSin(a));
            p.drawLine(centre + dir * 6.2, centre + dir * 9.6);
        }
        p.drawEllipse(centre, 6.2, 6.2);
        p.drawEllipse(centre, 2.4, 2.4);
    } else if (name == QLatin1String("help")) {
        p.drawEllipse(QPointF(12, 12), 9.4, 9.4);
        drawTextGlyph(p, QStringLiteral("?"), QRectF(2, 1.5, 20, 21));
    } else if (name == QLatin1String("log")) {
        p.drawPath(roundedPath(3.5, 2.5, 17, 19, 1.8));
        p.drawLine(QPointF(7, 7.5), QPointF(17, 7.5));
        p.drawLine(QPointF(7, 11.5), QPointF(17, 11.5));
        p.drawLine(QPointF(7, 15.5), QPointF(14, 15.5));
        p.setBrush(fg);
        p.drawEllipse(QPointF(16.5, 18.5), 4.0, 4.0);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(glyphColor().lighter(180), 1.8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(16.5, 16.6), QPointF(16.5, 18.6));
        p.drawLine(QPointF(16.5, 18.6), QPointF(18.0, 19.6));
        p.setPen(strokePen(p));
    } else if (name == QLatin1String("refresh")) {
        path.arcMoveTo(QRectF(3, 3, 18, 18), 60);
        path.arcTo(QRectF(3, 3, 18, 18), 60, 300);
        p.drawPath(path);
        drawArrowHead(p, QPointF(19.4, 7.8), -40, 3.6);
    } else if (name == QLatin1String("ok")) {
        p.setPen(strokePen(p, 2.6));
        p.drawPolyline(QPolygonF() << QPointF(4.5, 12.5) << QPointF(9.5, 17.5)
                                   << QPointF(19.5, 6.5));
    } else if (name == QLatin1String("warning")) {
        p.drawPolygon(QPolygonF() << QPointF(12, 2.5) << QPointF(22, 20.5) << QPointF(2, 20.5));
        drawTextGlyph(p, QStringLiteral("!"), QRectF(2, 6, 20, 15));
    } else if (name == QLatin1String("error")) {
        p.drawEllipse(QPointF(12, 12), 9.4, 9.4);
        p.drawLine(QPointF(8, 8), QPointF(16, 16));
        p.drawLine(QPointF(16, 8), QPointF(8, 16));
    } else if (name == QLatin1String("logo")) {
        p.drawPath(cardPath(1.5, 5.5, 21, 13));
        p.setBrush(fg);
        p.drawPath(roundedPath(4, 8, 5, 8.5, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(11, 9.5), QPointF(19.5, 9.5));
        p.drawLine(QPointF(11, 12.2), QPointF(19.5, 12.2));
        p.drawLine(QPointF(11, 15), QPointF(17, 15));
    } else {
        // Generic fallback: an unknown name must never produce a null icon,
        // because a missing icon silently turns a toolbar entry into a blank
        // square and hides the fact that something is wrong.
        p.drawPath(roundedPath(3, 3, 18, 18, 3.5));
        p.setBrush(fg);
        p.drawEllipse(QPointF(12, 12), 3.0, 3.0);
        p.setBrush(Qt::NoBrush);
    }
}

// Every name the factory draws, in menu order. Kept next to the switch so the
// two cannot drift apart.
const char *const kNames[] = {
    "new", "open", "save", "save_as", "undo", "redo", "cut", "copy", "paste", "duplicate",
    "delete", "select", "text", "image", "photo", "shape", "qr", "barcode", "group",
    "ungroup", "lock", "unlock", "visible", "hidden", "zoom_in", "zoom_out", "zoom_fit",
    "front", "back", "print", "preview", "printer", "export", "data", "snap", "grid",
    "guide", "align_left", "align_center_h", "align_right", "align_top", "align_middle_v",
    "align_bottom", "distribute_h", "distribute_v", "bring_front", "bring_forward",
    "send_backward", "send_back", "template", "settings", "help", "log", "refresh", "ok",
    "warning", "error", "logo"
};

} // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

QPixmap IconFactory::pixmap(const QString &name, int sizePx)
{
    const int size = qBound(8, sizePx, 256);
    if (name == QLatin1String("logo")) {
        static const bool initialized = [] { initializeBranding(); return true; }();
        Q_UNUSED(initialized);
        return QPixmap(QStringLiteral(":/branding/logo.png"))
            .scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);

    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setPen(strokePen(painter));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(glyphColor(), kPen, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

    painter.scale(size / kDesign, size / kDesign);
    drawGlyph(painter, name);
    painter.end();
    return pm;
}

namespace {
QHash<QString, QIcon> *iconCache()
{
    static QHash<QString, QIcon> cache;
    return &cache;
}
QString *cacheKey()
{
    static QString key;
    return &key;
}
} // namespace

QIcon IconFactory::icon(const QString &name)
{
    if (name == QLatin1String("logo")) {
        static const bool initialized = [] { initializeBranding(); return true; }();
        Q_UNUSED(initialized);
        return QIcon(QStringLiteral(":/branding/app.ico"));
    }
    // The cached pixmaps carry the palette colour they were drawn with, so the
    // key includes it: switching to a dark theme must not leave black glyphs on
    // a dark toolbar.
    const QString key = name + QLatin1Char('|') + glyphColor().name(QColor::HexArgb);
    if (*cacheKey() != key) {
        iconCache()->clear();
        *cacheKey() = key;
    }

    auto it = iconCache()->constFind(key);
    if (it != iconCache()->constEnd())
        return it.value();

    QIcon icon;
    for (int size : kSizes)
        icon.addPixmap(pixmap(name, size));

    iconCache()->insert(key, icon);
    return icon;
}

QStringList IconFactory::knownNames()
{
    QStringList names;
    for (const char *name : kNames)
        names << QString::fromLatin1(name);
    return names;
}

void IconFactory::clearCache()
{
    iconCache()->clear();
    cacheKey()->clear();
}

} // namespace occ
