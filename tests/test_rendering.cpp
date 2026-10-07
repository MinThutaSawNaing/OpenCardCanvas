// ---------------------------------------------------------------------------
// Unit tests: rendering.
//
// These assert real pixels, not merely "no crash". Rendering is where a subtle
// mistake (a wrong scale, dropped text, a skipped object) still produces a
// plausible looking result, so the checks are deliberately concrete: exact pixel
// sizes, changed pixel counts, and the warnings raised for what could not be
// drawn.
// ---------------------------------------------------------------------------
#include "core/CardDocument.h"
#include "core/ImageObject.h"
#include "core/ShapeObject.h"
#include "core/TextObject.h"
#include "project/AssetStore.h"
#include "rendering/CardRenderer.h"
#include "rendering/RenderContext.h"

#include <QtTest/QtTest>

using namespace occ;

namespace {

// Counts pixels that are not fully transparent: "did anything get drawn?".
int opaquePixels(const QImage &image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(row[x]) > 8)
                ++count;
        }
    }
    return count;
}

} // namespace

class TestRendering : public QObject
{
    Q_OBJECT
private slots:
    void rendersAtExactPhysicalSize();
    void resolutionScalesOutput();
    void backgroundIsPaintedFirst();
    void textActuallyDrawsPixels();
    void shapeActuallyDrawsPixels();
    void hiddenObjectIsNotDrawn();
    void missingAssetRaisesAWarningAndStillRenders();
    void bleedEnlargesTheOutput();
};

void TestRendering::rendersAtExactPhysicalSize()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());

    CardRenderer::Options options;
    options.dpi = 300;

    // 85.60 mm at 300 dpi is 1011 px and 53.98 mm is 638 px. If this ever
    // changes, a printed card would no longer be the right physical size.
    const QSize expected = CardRenderer::targetSizePx(doc.geometry(), options);
    QCOMPARE(expected.width(), 1011);
    QCOMPARE(expected.height(), 638);

    const QImage image = CardRenderer::renderSide(doc, CardSideId::Front, options);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), expected);
}

void TestRendering::resolutionScalesOutput()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());

    CardRenderer::Options low;
    low.dpi = 300;
    CardRenderer::Options high;
    high.dpi = 600;

    const QImage at300 = CardRenderer::renderSide(doc, CardSideId::Front, low);
    const QImage at600 = CardRenderer::renderSide(doc, CardSideId::Front, high);
    QVERIFY(!at300.isNull() && !at600.isNull());
    // Each dimension is rounded to whole pixels independently, so doubling the
    // resolution can land one pixel away from exactly twice the size.
    QVERIFY2(qAbs(at600.width() - at300.width() * 2) <= 1,
             qPrintable(QStringLiteral("300dpi %1 px -> 600dpi %2 px")
                            .arg(at300.width()).arg(at600.width())));
    QVERIFY2(qAbs(at600.height() - at300.height() * 2) <= 1,
             qPrintable(QStringLiteral("300dpi %1 px -> 600dpi %2 px")
                            .arg(at300.height()).arg(at600.height())));
}

void TestRendering::backgroundIsPaintedFirst()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());

    CardSide::Background background;
    background.kind = CardSide::Background::Kind::Solid;
    background.color = QColor(10, 80, 160);
    doc.front().setBackground(background);

    CardRenderer::Options options;
    options.dpi = 150;
    const QImage image = CardRenderer::renderSide(doc, CardSideId::Front, options);
    QVERIFY(!image.isNull());
    QVERIFY(opaquePixels(image) > 0);

    // The whole card must be covered by the background.
    const QColor centre(image.pixel(image.width() / 2, image.height() / 2));
    QVERIFY2(qAbs(centre.red() - 10) <= 2 && qAbs(centre.green() - 80) <= 2
                 && qAbs(centre.blue() - 160) <= 2,
             qPrintable(QStringLiteral("centre pixel was %1").arg(centre.name())));
}

void TestRendering::textActuallyDrawsPixels()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());

    // A transparent background, so "opaque pixels" measures the objects only.
    // With the default white background every pixel is already opaque and the
    // comparison could not detect any text at all.
    CardSide::Background none;
    none.kind = CardSide::Background::Kind::None;
    doc.front().setBackground(none);

    CardRenderer::Options options;
    options.dpi = 300;

    const QImage blank = CardRenderer::renderSide(doc, CardSideId::Front, options);
    const int blankPixels = opaquePixels(blank);
    QCOMPARE(blankPixels, 0);

    auto text = std::make_unique<TextObject>();
    text->setText(QStringLiteral("EMPLOYEE NAME"));
    text->setRectMm(QRectF(5.0, 5.0, 60.0, 10.0));
    text->setFontSizePt(12.0);
    doc.front().insertObject(std::move(text));

    const QImage withText = CardRenderer::renderSide(doc, CardSideId::Front, options);
    QVERIFY2(opaquePixels(withText) > blankPixels,
             "rendering a text object produced no additional pixels");
}

void TestRendering::shapeActuallyDrawsPixels()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());

    auto shape = std::make_unique<ShapeObject>(ShapeKind::Rectangle);
    shape->setRectMm(QRectF(10.0, 10.0, 40.0, 20.0));
    shape->setFillColor(QColor(220, 30, 30));
    doc.front().insertObject(std::move(shape));

    CardRenderer::Options options;
    options.dpi = 300;
    const QImage image = CardRenderer::renderSide(doc, CardSideId::Front, options);
    QVERIFY(opaquePixels(image) > 10000);

    // A point inside the shape must carry the fill colour.
    const double pxPerMm = options.pxPerMm();
    const QColor inside(image.pixel(int(20.0 * pxPerMm), int(15.0 * pxPerMm)));
    QVERIFY2(inside.red() > 180 && inside.green() < 80,
             qPrintable(QStringLiteral("inside pixel was %1").arg(inside.name())));
}

void TestRendering::hiddenObjectIsNotDrawn()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());

    // Transparent background, so a non-transparent pixel can only come from the
    // object and "hidden draws nothing" is a meaningful assertion.
    CardSide::Background none;
    none.kind = CardSide::Background::Kind::None;
    doc.front().setBackground(none);

    auto shape = std::make_unique<ShapeObject>(ShapeKind::Rectangle);
    shape->setRectMm(QRectF(0.0, 0.0, 85.6, 53.98));
    shape->setFillColor(QColor(0, 200, 0));
    const ObjectId id = shape->id();
    doc.front().insertObject(std::move(shape));

    CardRenderer::Options options;
    options.dpi = 150;
    const QImage visible = CardRenderer::renderSide(doc, CardSideId::Front, options);
    QVERIFY(opaquePixels(visible) > 0);

    doc.front().object(id)->setVisible(false);
    const QImage hidden = CardRenderer::renderSide(doc, CardSideId::Front, options);
    QCOMPARE(opaquePixels(hidden), 0);
}

void TestRendering::missingAssetRaisesAWarningAndStillRenders()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());

    auto image = std::make_unique<ImageObject>();
    image->setAssetId(QStringLiteral("this-asset-does-not-exist"));
    image->setRectMm(QRectF(5.0, 5.0, 25.0, 30.0));
    doc.front().insertObject(std::move(image));

    CardRenderer::Options options;
    options.dpi = 150;
    QVector<RenderContext::Warning> warnings;
    const QImage rendered = CardRenderer::renderSide(doc, CardSideId::Front, options, &warnings);

    // The render must still produce an image, and it must say what was missing
    // rather than silently drawing nothing.
    QVERIFY(!rendered.isNull());
    QVERIFY2(!warnings.isEmpty(), "a missing asset produced no warning");

    bool explains = false;
    for (const RenderContext::Warning &w : warnings) {
        if (w.message.contains(QStringLiteral("missing"), Qt::CaseInsensitive)
            || w.message.contains(QStringLiteral("could not be drawn"), Qt::CaseInsensitive)) {
            explains = true;
        }
    }
    QVERIFY2(explains, "the warning did not explain that the image is missing");
}

void TestRendering::bleedEnlargesTheOutput()
{
    CardGeometry geometry = CardGeometry::isoId1();
    geometry.setBleedMm(0.5);

    CardDocument doc;
    doc.setGeometry(geometry);

    CardRenderer::Options trimmed;
    trimmed.dpi = 300;
    trimmed.includeBleed = false;
    CardRenderer::Options bled;
    bled.dpi = 300;
    bled.includeBleed = true;

    const QImage a = CardRenderer::renderSide(doc, CardSideId::Front, trimmed);
    const QImage b = CardRenderer::renderSide(doc, CardSideId::Front, bled);
    QVERIFY(b.width() > a.width());
    QVERIFY(b.height() > a.height());

    // 1 mm of extra width at 300 dpi is about 12 pixels.
    QVERIFY(qAbs((b.width() - a.width()) - 12) <= 2);

    // The bleed box starts at (-bleed, -bleed).
    const QRectF area = CardRenderer::paintAreaMm(geometry, bled);
    QCOMPARE(area.x(), -0.5);
    QCOMPARE(area.y(), -0.5);
}

QTEST_MAIN(TestRendering)
#include "test_rendering.moc"
