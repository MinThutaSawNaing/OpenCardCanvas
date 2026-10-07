// ---------------------------------------------------------------------------
// Unit tests: units, card geometry and physical accuracy.
// ---------------------------------------------------------------------------
#include "core/CardGeometry.h"
#include "core/Units.h"

#include <QtTest/QtTest>

using namespace occ;

class TestUnits : public QObject
{
    Q_OBJECT
private slots:
    void conversionConstants();
    void mmInchRoundTrip();
    void pointsArePhysical();
    void pxPerMmFromDpi();
    void parseLengthAcceptsUnits();
    void parseLengthRejectsGarbage();
    void presetMatchesIsoId1();
    void orientationSwapsDimensions();
    void portraitIsAFullySupportedSize();
    void pixelSizeAt300Dpi();
    void geometryRejectsImpossibleSizes();
    void geometryJsonRoundTrip();
    void canonicalMmIsStable();
};

void TestUnits::conversionConstants()
{
    QCOMPARE(units::kMmPerInch, 25.4);
    QVERIFY(qAbs(units::pointsToMm(72.0) - 25.4) < 1e-9);
    QVERIFY(qAbs(units::mmToPoints(25.4) - 72.0) < 1e-9);
}

void TestUnits::mmInchRoundTrip()
{
    for (double mm : { 0.0, 1.0, 85.6, 53.98, 1000.0 }) {
        QVERIFY(qAbs(units::inchToMm(units::mmToInch(mm)) - mm) < 1e-9);
    }
}

void TestUnits::pointsArePhysical()
{
    // A 10 pt glyph must be 10/72 inch tall, i.e. about 3.528 mm.
    const double mm = units::pointsToMm(10.0);
    QVERIFY(qAbs(mm - 3.5277777) < 1e-6);
}

void TestUnits::pxPerMmFromDpi()
{
    QVERIFY(qAbs(units::pxPerMmFromDpi(300.0) - 11.8110236) < 1e-6);
    QVERIFY(qAbs(units::dpiFromPxPerMm(units::pxPerMmFromDpi(600.0)) - 600.0) < 1e-9);
}

void TestUnits::parseLengthAcceptsUnits()
{
    bool ok = false;
    QCOMPARE(units::parseLength(QStringLiteral("85.6"), units::DisplayUnit::Millimeters, &ok), 85.6);
    QVERIFY(ok);
    QVERIFY(qAbs(units::parseLength(QStringLiteral("85.6mm"), units::DisplayUnit::Inches, &ok) - 85.6) < 1e-9);
    QVERIFY(ok);
    QVERIFY(qAbs(units::parseLength(QStringLiteral("3.37in"), units::DisplayUnit::Millimeters, &ok) - 85.598) < 0.01);
    QVERIFY(ok);
    QVERIFY(qAbs(units::parseLength(QStringLiteral("1in"), units::DisplayUnit::Millimeters, &ok) - 25.4) < 1e-9);
    QVERIFY(ok);
}

void TestUnits::parseLengthRejectsGarbage()
{
    bool ok = true;
    units::parseLength(QStringLiteral("abc"), units::DisplayUnit::Millimeters, &ok);
    QVERIFY(!ok);
    units::parseLength(QStringLiteral(""), units::DisplayUnit::Millimeters, &ok);
    QVERIFY(!ok);
    units::parseLength(QStringLiteral("12 parsecs"), units::DisplayUnit::Millimeters, &ok);
    QVERIFY(!ok);
}

void TestUnits::presetMatchesIsoId1()
{
    const CardPreset id1 = CardGeometry::preset(QStringLiteral("iso-id1"));
    QCOMPARE(id1.widthMm, 85.60);
    QCOMPARE(id1.heightMm, 53.98);
    QCOMPARE(CardGeometry::matchingPresetId(85.60, 53.98), QStringLiteral("iso-id1"));
    QCOMPARE(CardGeometry::matchingPresetId(12.0, 34.0), QStringLiteral("custom"));
}

void TestUnits::orientationSwapsDimensions()
{
    CardGeometry g = CardGeometry::isoId1();
    QVERIFY(g.isLandscape());
    g.setOrientation(false);
    QVERIFY(!g.isLandscape());
    QCOMPARE(g.widthMm(), 53.98);
    QCOMPARE(g.heightMm(), 85.60);

    // Turning it back must restore the original numbers exactly, so repeated
    // toggling cannot drift.
    g.setOrientation(true);
    QVERIFY(g.isLandscape());
    QCOMPARE(g.widthMm(), 85.60);
    QCOMPARE(g.heightMm(), 53.98);
}

void TestUnits::portraitIsAFullySupportedSize()
{
    // A portrait card must behave exactly like a landscape one everywhere the
    // geometry is used: pixel size, bleed box, JSON round trip and preset match.
    const CardGeometry portrait = CardGeometry::isoId1Portrait();
    QVERIFY(!portrait.isLandscape());
    QCOMPARE(portrait.widthMm(), 53.98);
    QCOMPARE(portrait.heightMm(), 85.60);

    // 53.98 mm at 300 dpi is 638 px and 85.60 mm is 1011 px - the transpose of
    // the landscape card, not a stretched version of it.
    const QSize px = portrait.pixelSize(300);
    QCOMPARE(px.width(), 638);
    QCOMPARE(px.height(), 1011);
    QVERIFY2(px.width() < px.height(), "a portrait card must be taller than it is wide");

    QString error;
    CardGeometry restored;
    QVERIFY2(restored.fromJson(portrait.toJson(), &error), qPrintable(error));
    QVERIFY(!restored.isLandscape());
    QCOMPARE(restored.widthMm(), 53.98);
    QCOMPARE(restored.heightMm(), 85.60);
    QCOMPARE(restored.presetId(), QStringLiteral("iso-id1-portrait"));

    // And a custom portrait size must not be mistaken for a landscape preset.
    CardGeometry custom;
    custom.setSize(60.0, 90.0);
    QVERIFY(!custom.isLandscape());
    QCOMPARE(custom.presetId(), QStringLiteral("custom"));
}

void TestUnits::pixelSizeAt300Dpi()
{
    // 85.60 mm at 300 dpi is 1011 px; 53.98 mm is 638 px. This is the number the
    // renderer must produce, so it is pinned here.
    const CardGeometry g = CardGeometry::isoId1();
    const QSize px = g.pixelSize(300);
    QCOMPARE(px.width(), 1011);
    QCOMPARE(px.height(), 638);
    QCOMPARE(g.aspectRatio() > 1.5, true);
}

void TestUnits::geometryRejectsImpossibleSizes()
{
    QString error;
    CardGeometry g;
    g.setSize(0.0, 0.0);          // clamped into range by the setter
    QVERIFY(g.widthMm() >= kMinCardMm);
    QVERIFY(g.isValid(&error));

    QJsonObject bad;
    bad.insert(QStringLiteral("widthMm"), -5.0);
    bad.insert(QStringLiteral("heightMm"), 53.98);
    CardGeometry loaded;
    QVERIFY(!loaded.fromJson(bad, &error));
    QVERIFY(!error.isEmpty());
}

void TestUnits::geometryJsonRoundTrip()
{
    CardGeometry original;
    original.setSize(74.0, 105.0);
    original.setRenderDpi(600);
    original.setBleedMm(0.5);

    CardGeometry restored;
    QString error;
    QVERIFY2(restored.fromJson(original.toJson(), &error), qPrintable(error));
    QCOMPARE(restored.widthMm(), original.widthMm());
    QCOMPARE(restored.heightMm(), original.heightMm());
    QCOMPARE(restored.renderDpi(), original.renderDpi());
    QCOMPARE(restored.bleedMm(), original.bleedMm());

    // Bleed must expand the print box on all four sides.
    QCOMPARE(restored.bleedBoundsMm().x(), -0.5);
    QCOMPARE(restored.bleedBoundsMm().width(), 74.0 + 1.0);
}

void TestUnits::canonicalMmIsStable()
{
    // A save/load cycle must not drift, so values are rounded to 1 micrometre.
    const double rounded = units::canonicalMm(85.60000049);
    QCOMPARE(rounded, 85.6);
    QCOMPARE(units::canonicalMm(rounded), rounded);
}

QTEST_MAIN(TestUnits)
#include "test_units.moc"
