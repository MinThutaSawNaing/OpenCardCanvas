// ---------------------------------------------------------------------------
// Unit tests: canvas arithmetic and snapping.
//
// CanvasGeometry and SnapEngine are deliberately pure functions with no widget
// state, which is what makes them testable here. They are also the parts of a
// canvas that are easy to get subtly wrong - one pixel out, only at 150 % zoom,
// only on a HiDPI screen - so they are checked directly rather than through the
// GUI.
// ---------------------------------------------------------------------------
#include "canvas/CanvasGeometry.h"
#include "canvas/GuideModel.h"
#include "canvas/SnapEngine.h"
#include "core/CardGeometry.h"
#include "core/ShapeObject.h"

#include <QtTest/QtTest>

using namespace occ;

class TestCanvas : public QObject
{
    Q_OBJECT
private slots:
    void zoomRoundTripsExactly();
    void zoomIsClamped();
    void viewTransformRoundTrips();
    void handlesCoverAllEightEdgesPlusRotate();
    void handleHitTestFindsTheRightHandle();
    void handleHitTestMissesOutsideObjects();
    void objectHitTestHonoursRotation();
    void fitZoomSizeKeepsAspectRatio();
    void toleranceConvertsPixelsToMillimetres();
    void snapToCardEdgeAndCentre();
    void disabledSnappingLeavesPositionAlone();
};

void TestCanvas::zoomRoundTripsExactly()
{
    const double ratios[] = { 1.0, 1.25, 1.5, 2.0 };
    const double zooms[] = { 25.0, 100.0, 250.0, 800.0 };
    for (double dpr : ratios) {
        for (double zoom : zooms) {
            const double pxPerMm = CanvasGeometry::pxPerMmForZoom(zoom, dpr);
            QVERIFY(pxPerMm > 0.0);
            const double back = CanvasGeometry::zoomForPxPerMm(pxPerMm, dpr);
            QVERIFY2(qAbs(back - zoom) < 1e-9,
                     qPrintable(QStringLiteral("zoom %1 at dpr %2 gave %3")
                                    .arg(zoom).arg(dpr).arg(back)));
        }
    }
}

void TestCanvas::zoomIsClamped()
{
    QCOMPARE(CanvasGeometry::clampZoom(0.0), CanvasGeometry::kMinZoomPercent);
    QCOMPARE(CanvasGeometry::clampZoom(100000.0), CanvasGeometry::kMaxZoomPercent);
    QCOMPARE(CanvasGeometry::clampZoom(100.0), 100.0);
}

void TestCanvas::viewTransformRoundTrips()
{
    const double pxPerMm = 6.5;
    const QPointF origin(37.0, 11.0);
    const QPointF samples[] = { QPointF(0, 0), QPointF(85.6, 53.98), QPointF(12.34, 5.67) };
    for (const QPointF &mm : samples) {
        const QPointF view = CanvasGeometry::mmToView(mm, pxPerMm, origin);
        const QPointF back = CanvasGeometry::viewToMm(view, pxPerMm, origin);
        QVERIFY(qAbs(back.x() - mm.x()) < 1e-9);
        QVERIFY(qAbs(back.y() - mm.y()) < 1e-9);
    }

    const QRectF viewRect =
        CanvasGeometry::viewRectForMm(QRectF(10, 10, 20, 10), pxPerMm, origin);
    QCOMPARE(viewRect.x(), 10.0 * pxPerMm + origin.x());
    QCOMPARE(viewRect.width(), 20.0 * pxPerMm);
}

void TestCanvas::handlesCoverAllEightEdgesPlusRotate()
{
    ShapeObject shape;
    shape.setRectMm(QRectF(10, 10, 30, 20));

    const QVector<Handle> handles = CanvasGeometry::handlesFor(shape, 4.0, QPointF(0, 0), 8.0);
    QCOMPARE(handles.size(), 9);

    int resizeHandles = 0;
    int rotateHandles = 0;
    for (const Handle &handle : handles) {
        if (handle.id == Handle::Rotate)
            ++rotateHandles;
        else if (handle.id != Handle::None)
            ++resizeHandles;
    }
    QCOMPARE(resizeHandles, 8);
    QCOMPARE(rotateHandles, 1);

    // rotateHandlePosMm() returns the ANCHOR the rotate handle hangs from: the
    // midpoint of the object's top edge (already turned with the object). The
    // visual displacement that keeps it clear of the corner handles depends on
    // the current zoom, so it is applied by handlesFor() through
    // rotateHandleOffsetMm().
    const QPointF anchorMm = CanvasGeometry::rotateHandlePosMm(shape);
    QVERIFY(qAbs(anchorMm.x() - shape.rectMm().center().x()) < 1e-6);
    QVERIFY(qAbs(anchorMm.y() - shape.rectMm().top()) < 1e-6);

    // The handle actually offered on screen must sit above that anchor.
    QPointF handleMm;
    bool found = false;
    for (const Handle &handle : handles) {
        if (handle.id == Handle::Rotate) {
            handleMm = handle.posMm;
            found = true;
        }
    }
    QVERIFY(found);
    QVERIFY2(handleMm.y() < anchorMm.y(),
             qPrintable(QStringLiteral("rotate handle y=%1 is not above the top edge %2")
                            .arg(handleMm.y()).arg(anchorMm.y())));
    QVERIFY(qAbs(handleMm.x() - anchorMm.x()) < 1e-6);
    QVERIFY(qAbs((anchorMm.y() - handleMm.y())
                 - CanvasGeometry::rotateHandleOffsetMm(4.0, 8.0)) < 1e-6);
}


void TestCanvas::handleHitTestFindsTheRightHandle()
{
    ShapeObject shape;
    shape.setRectMm(QRectF(10, 10, 30, 20));
    const double pxPerMm = 5.0;
    const QPointF origin(20.0, 20.0);

    const QVector<Handle> handles = CanvasGeometry::handlesFor(shape, pxPerMm, origin, 8.0);
    for (const Handle &handle : handles) {
        const QPointF view = CanvasGeometry::mmToView(handle.posMm, pxPerMm, origin);
        const Handle::Id hit = CanvasGeometry::handleAt(shape, view, pxPerMm, origin, 8.0);
        QCOMPARE(int(hit), int(handle.id));
    }
}

void TestCanvas::handleHitTestMissesOutsideObjects()
{
    ShapeObject shape;
    shape.setRectMm(QRectF(10, 10, 30, 20));
    // A point far from every handle must report None rather than a stray handle.
    const Handle::Id hit =
        CanvasGeometry::handleAt(shape, QPointF(5.0, 5.0), 5.0, QPointF(0, 0), 8.0);
    QCOMPARE(int(hit), int(Handle::None));
}

void TestCanvas::objectHitTestHonoursRotation()
{
    ShapeObject shape;
    shape.setRectMm(QRectF(0, 0, 20, 10));
    const double pxPerMm = 4.0;
    const QPointF origin(0, 0);

    const QPointF inside = CanvasGeometry::mmToView(QPointF(10, 5), pxPerMm, origin);
    QVERIFY(CanvasGeometry::objectAt(shape, inside, pxPerMm, origin));

    const QPointF outside = CanvasGeometry::mmToView(QPointF(35, 5), pxPerMm, origin);
    QVERIFY(!CanvasGeometry::objectAt(shape, outside, pxPerMm, origin));

    // After a 90 degree rotation the point below the centre is inside the
    // rotated frame. This is what makes selecting a rotated object feel right.
    shape.setRotationDeg(90.0);
    const QPointF belowCentre = CanvasGeometry::mmToView(QPointF(10, 13), pxPerMm, origin);
    QVERIFY(CanvasGeometry::objectAt(shape, belowCentre, pxPerMm, origin));
}

void TestCanvas::fitZoomSizeKeepsAspectRatio()
{
    const CardGeometry geometry = CardGeometry::isoId1();
    const QSizeF fitted = CanvasGeometry::fitZoomSize(geometry, QSize(800, 600), 1.0, 20.0);
    QVERIFY(fitted.width() > 0.0);
    QVERIFY(fitted.height() > 0.0);

    const double cardRatio = geometry.widthMm() / geometry.heightMm();
    const double fittedRatio = fitted.width() / fitted.height();
    QVERIFY2(qAbs(cardRatio - fittedRatio) < 0.01,
             qPrintable(QStringLiteral("card %1 vs fitted %2").arg(cardRatio).arg(fittedRatio)));
    QVERIFY(fitted.width() <= 800.0);
    QVERIFY(fitted.height() <= 600.0);
}

void TestCanvas::toleranceConvertsPixelsToMillimetres()
{
    // The snap tolerance is expressed in screen pixels, so snapping feels the
    // same at every zoom level.
    QVERIFY(qAbs(SnapEngine::toleranceMm(8.0, 4.0) - 2.0) < 1e-9);
    QVERIFY(qAbs(SnapEngine::toleranceMm(8.0, 16.0) - 0.5) < 1e-9);
    QCOMPARE(SnapEngine::toleranceMm(8.0, 0.0), 0.0);
}

void TestCanvas::snapToCardEdgeAndCentre()
{
    CardGeometry geometry = CardGeometry::isoId1();   // 85.60 x 53.98 mm
    CardSide side(CardSideId::Front);

    ShapeObject moving;
    moving.setRectMm(QRectF(0, 0, 10.0, 10.0));

    SnapEngine::Options options;
    options.toCard = true;
    options.toGuides = false;
    options.toObjects = false;
    options.toGrid = false;
    options.tolerancePx = 8.0;
    const double pxPerMm = 4.0;                        // 8 px == 2 mm
    const QVector<GuideModel::Guide> guides;

    // Just inside the top-left corner: must snap to exactly (0, 0).
    const SnapEngine::Result corner = SnapEngine::snapMove(
        moving, QPointF(0.05, 0.05), side, geometry, guides, options, pxPerMm);
    QVERIFY(corner.snappedX);
    QVERIFY(corner.snappedY);
    QCOMPARE(corner.adjustedMm, QPointF(0.0, 0.0));

    // Near the horizontal centre: a 10 mm wide object centres at x = 37.8 mm.
    const SnapEngine::Result centred = SnapEngine::snapMove(
        moving, QPointF(37.85, 0.05), side, geometry, guides, options, pxPerMm);
    QVERIFY(centred.snappedX);
    QVERIFY(qAbs(centred.adjustedMm.x() - 37.8) < 1e-6);
}

void TestCanvas::disabledSnappingLeavesPositionAlone()
{
    CardGeometry geometry = CardGeometry::isoId1();
    CardSide side(CardSideId::Front);

    ShapeObject moving;
    moving.setRectMm(QRectF(0, 0, 10.0, 10.0));

    SnapEngine::Options options;
    options.toCard = false;
    options.toGuides = false;
    options.toObjects = false;
    options.toGrid = false;

    const QPointF proposed(0.05, 0.05);
    const SnapEngine::Result result = SnapEngine::snapMove(
        moving, proposed, side, geometry, {}, options, 4.0);
    QVERIFY(!result.snappedX);
    QVERIFY(!result.snappedY);
    QCOMPARE(result.adjustedMm, proposed);
}

QTEST_MAIN(TestCanvas)
#include "test_canvas.moc"
