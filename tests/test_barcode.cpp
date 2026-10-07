// ---------------------------------------------------------------------------
// Unit tests: linear barcode encoding and data validation.
// ---------------------------------------------------------------------------
#include "codes/BarcodeEncoder.h"

#include <QtTest/QtTest>

using namespace occ;

class TestBarcode : public QObject
{
    Q_OBJECT
private slots:
    void code128ProducesBars();
    void code128AutoSelectsCodeSetC();
    void code39WrapsWithStartStop();
    void ean13CheckDigits();
    void ean8CheckDigit();
    void itf14CheckDigit();
    void rejectsInvalidData_data();
    void rejectsInvalidData();
    void validateMatchesEncode();
    void humanReadableText();
};

void TestBarcode::code128ProducesBars()
{
    const BarcodeEncoder::Result r =
        BarcodeEncoder::encode(QStringLiteral("ABC123"), BarcodeSymbology::Code128);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(r.moduleCount() > 30);

    // A Code 128 symbol is: start (11) + one 11-module pattern per data symbol
    // + check symbol (11) + the 13-module stop pattern. So the total is
    // 11n + 13, not a multiple of 11.
    QCOMPARE((r.moduleCount() - 13) % 11, 0);
    QVERIFY(r.moduleCount() >= 11 + 11 + 11 + 13);

    // The stop pattern 2331112 ends with a 2-module bar, so the last two modules
    // are dark and the third from the end is light.
    QVERIFY(r.isDark(r.moduleCount() - 1));
    QVERIFY(r.isDark(r.moduleCount() - 2));
    QVERIFY(!r.isDark(r.moduleCount() - 3));
}

void TestBarcode::code128AutoSelectsCodeSetC()
{
    // An even run of digits is more compact in code set C than B.
    const BarcodeEncoder::Result digits =
        BarcodeEncoder::encode(QStringLiteral("1234567890"), BarcodeSymbology::Code128);
    const BarcodeEncoder::Result letters =
        BarcodeEncoder::encode(QStringLiteral("ABCDEFGHIJ"), BarcodeSymbology::Code128);
    QVERIFY(digits.ok && letters.ok);
    QVERIFY2(digits.moduleCount() < letters.moduleCount(),
             "code set C should encode digit pairs more compactly");
}

void TestBarcode::code39WrapsWithStartStop()
{
    const BarcodeEncoder::Result without =
        BarcodeEncoder::encode(QStringLiteral("AB"), BarcodeSymbology::Code39, false);
    const BarcodeEncoder::Result with =
        BarcodeEncoder::encode(QStringLiteral("AB"), BarcodeSymbology::Code39, true);
    QVERIFY(without.ok && with.ok);
    // The check character adds one symbol (9 elements + 1 gap) plus its own gap.
    QVERIFY(with.moduleCount() > without.moduleCount());
    QVERIFY(without.moduleCount() > 0);
}

void TestBarcode::ean13CheckDigits()
{
    QCOMPARE(BarcodeEncoder::eanCheckDigit(QStringLiteral("590123412345")), 7);
    QCOMPARE(BarcodeEncoder::eanCheckDigit(QStringLiteral("400638133393")), 1);
    // Both the 12 digit form and the full 13 digit form must be accepted.
    QVERIFY(BarcodeEncoder::validate(QStringLiteral("590123412345"), BarcodeSymbology::Ean13, nullptr));
    QVERIFY(BarcodeEncoder::validate(QStringLiteral("5901234123457"), BarcodeSymbology::Ean13, nullptr));
    // A wrong check digit must be rejected.
    QString error;
    QVERIFY(!BarcodeEncoder::validate(QStringLiteral("5901234123458"), BarcodeSymbology::Ean13, &error));
    QVERIFY(!error.isEmpty());
}

void TestBarcode::ean8CheckDigit()
{
    QCOMPARE(BarcodeEncoder::eanCheckDigit(QStringLiteral("9638507")), 4);
    QVERIFY(BarcodeEncoder::validate(QStringLiteral("96385074"), BarcodeSymbology::Ean8, nullptr));
}

void TestBarcode::itf14CheckDigit()
{
    // ITF-14 uses the same modulo 10 weighting as EAN.
    const QString base = QStringLiteral("1234567890123");
    const int check = BarcodeEncoder::eanCheckDigit(base);
    const QString full = base + QString::number(check);
    QVERIFY(BarcodeEncoder::validate(full, BarcodeSymbology::Itf14, nullptr));
    QVERIFY(BarcodeEncoder::validate(base, BarcodeSymbology::Itf14, nullptr));
}

void TestBarcode::rejectsInvalidData_data()
{
    QTest::addColumn<QString>("data");
    QTest::addColumn<int>("symbology");

    QTest::newRow("ean13 letters")  << QStringLiteral("59012341ABCD") << int(BarcodeSymbology::Ean13);
    QTest::newRow("ean13 short")    << QStringLiteral("12345")        << int(BarcodeSymbology::Ean13);
    QTest::newRow("ean8 long")      << QStringLiteral("1234567890")   << int(BarcodeSymbology::Ean8);
    QTest::newRow("code39 lower ok but bad char") << QStringLiteral("AB*C") << int(BarcodeSymbology::Code39);
    QTest::newRow("itf odd digits") << QStringLiteral("123")          << int(BarcodeSymbology::Itf14);
    QTest::newRow("empty code128")  << QString()                      << int(BarcodeSymbology::Code128);
    QTest::newRow("empty ean13")    << QString()                      << int(BarcodeSymbology::Ean13);
}

void TestBarcode::rejectsInvalidData()
{
    QFETCH(QString, data);
    QFETCH(int, symbology);
    const auto sym = static_cast<BarcodeSymbology>(symbology);

    QString error;
    QVERIFY2(!BarcodeEncoder::validate(data, sym, &error), qPrintable(data));
    QVERIFY2(!error.isEmpty(), qPrintable(QStringLiteral("no message for '%1'").arg(data)));

    // encode() must agree with validate().
    const BarcodeEncoder::Result r = BarcodeEncoder::encode(data, sym);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
    QVERIFY(r.moduleCount() == 0);
}

void TestBarcode::validateMatchesEncode()
{
    const QStringList valid = { QStringLiteral("ABC123"), QStringLiteral("EMP-00042"),
                                QStringLiteral("hello world") };
    for (const QString &data : valid) {
        QVERIFY(BarcodeEncoder::validate(data, BarcodeSymbology::Code128, nullptr));
        QVERIFY(BarcodeEncoder::encode(data, BarcodeSymbology::Code128).ok);
    }
}

void TestBarcode::humanReadableText()
{
    QVERIFY(BarcodeEncoder::supportsHumanText(BarcodeSymbology::Code128));
    QVERIFY(BarcodeEncoder::supportsHumanText(BarcodeSymbology::Ean13));
    QCOMPARE(BarcodeEncoder::humanReadableText(QStringLiteral("5901234123457"),
                                               BarcodeSymbology::Ean13),
             QStringLiteral("5 901234 123457"));
}

QTEST_MAIN(TestBarcode)
#include "test_barcode.moc"
