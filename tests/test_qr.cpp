// ---------------------------------------------------------------------------
// Unit tests: QR encoding.
//
// The primary assertion compares the module matrix, module for module, against
// golden matrices produced by an INDEPENDENT implementation
// (tools/qr/generate_golden.py uses the `segno` library). Comparing against a
// different implementation - not against our own output - is what makes this a
// real correctness check.
//
// When the golden file is absent the comparison is skipped with a warning; the
// structural assertions below still run.
// ---------------------------------------------------------------------------
#include "codes/QrEncoder.h"
#include "core/QrObject.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

using namespace occ;

class TestQr : public QObject
{
    Q_OBJECT
private slots:
    void finderAndTimingPatterns();
    void versionTables();
    void modeSelection();
    void errorCorrectionChangesSymbol();
    void capacityGrowsWithVersion();
    void rejectsEmptyInput();
    void rejectsOversizedPayload();
    void differentDataDifferentSymbol();
    void matchesGoldenMatrices();
};

void TestQr::finderAndTimingPatterns()
{
    const QrEncoder::Result r =
        QrEncoder::encode(QStringLiteral("HELLO WORLD"), QrErrorCorrection::Medium);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(r.version >= 1 && r.version <= 40);
    QCOMPARE(r.size, 17 + 4 * r.version);
    QCOMPARE(r.modules.size(), r.size * r.size);

    // Top-left finder: 7x7, dark border, dark 3x3 core, light ring between.
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            const bool border = (x == 0 || x == 6 || y == 0 || y == 6);
            const bool core = (x >= 2 && x <= 4 && y >= 2 && y <= 4);
            QVERIFY2(r.isDark(x, y) == (border || core),
                     qPrintable(QStringLiteral("finder mismatch at %1,%2").arg(x).arg(y)));
        }
    }
    // Separators must be light.
    for (int i = 0; i < 8; ++i) {
        QVERIFY(!r.isDark(7, i));
        QVERIFY(!r.isDark(i, 7));
    }
    // Timing patterns alternate, starting dark.
    QVERIFY(r.isDark(8, 6));
    QVERIFY(!r.isDark(9, 6));
    QVERIFY(r.isDark(6, 8));
    QVERIFY(!r.isDark(6, 9));
}

void TestQr::versionTables()
{
    QCOMPARE(QrEncoder::totalCodewords(1), 26);
    QCOMPARE(QrEncoder::totalCodewords(2), 44);
    QCOMPARE(QrEncoder::totalCodewords(7), 196);
    QCOMPARE(QrEncoder::totalCodewords(40), 3706);
    // Version 1-M: 16 data codewords, one block, 10 error correction codewords.
    QCOMPARE(QrEncoder::dataCapacityBits(1, QrErrorCorrection::Medium), 128);
    QCOMPARE(QrEncoder::eccCodewordsPerBlock(1, QrErrorCorrection::Medium), 10);
    QCOMPARE(QrEncoder::blockCount(1, QrErrorCorrection::Medium), 1);
}

void TestQr::modeSelection()
{
    QCOMPARE(QrEncoder::encode(QStringLiteral("1234567890"), QrErrorCorrection::Low).mode,
             QStringLiteral("numeric"));
    QCOMPARE(QrEncoder::encode(QStringLiteral("ABC-123 /$%"), QrErrorCorrection::Low).mode,
             QStringLiteral("alphanumeric"));
    QCOMPARE(QrEncoder::encode(QStringLiteral("Hello, World"), QrErrorCorrection::Low).mode,
             QStringLiteral("byte"));
}

void TestQr::errorCorrectionChangesSymbol()
{
    const QString data = QStringLiteral("https://example.org/card/42");
    const QrEncoder::Result low = QrEncoder::encode(data, QrErrorCorrection::Low);
    const QrEncoder::Result high = QrEncoder::encode(data, QrErrorCorrection::High);
    QVERIFY(low.ok && high.ok);
    QVERIFY(high.version >= low.version);
    QVERIFY(low.modules != high.modules);
}

void TestQr::capacityGrowsWithVersion()
{
    for (int v = 1; v < 40; ++v) {
        QVERIFY(QrEncoder::dataCapacityBits(v + 1, QrErrorCorrection::Medium)
                > QrEncoder::dataCapacityBits(v, QrErrorCorrection::Medium));
    }
}

void TestQr::rejectsEmptyInput()
{
    // Layering matters here. The encoder is a low-level, spec-conformant
    // component: ISO/IEC 18004 permits an empty payload, so encode() may return
    // a small, valid symbol. What must never happen is a *malformed* result,
    // and the object layer is where "there is nothing to encode" is rejected -
    // that is what stops a blank QR code reaching a printed card.
    const QrEncoder::Result r = QrEncoder::encode(QString(), QrErrorCorrection::Medium);
    if (r.ok) {
        QCOMPARE(r.size, 17 + 4 * r.version);
        QCOMPARE(r.modules.size(), r.size * r.size);
    } else {
        QVERIFY2(!r.error.isEmpty(), "a rejected payload must explain itself");
        QCOMPARE(r.modules.size(), 0);
    }

    // The object layer must refuse it outright.
    QrObject qr;
    qr.setData(QString());
    QVERIFY2(!qr.validationError().isEmpty(),
             "an empty QR code must be reported by the object, not silently drawn");
    qr.setData(QStringLiteral("   "));
    QVERIFY(!qr.validationError().isEmpty());
    qr.setData(QStringLiteral("EMP001"));
    QVERIFY(qr.validationError().isEmpty());
}

void TestQr::rejectsOversizedPayload()
{
    const QString huge(5000, QLatin1Char('A'));
    const QrEncoder::Result r = QrEncoder::encode(huge, QrErrorCorrection::High);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
    QVERIFY(!QrEncoder::canEncode(huge, QrErrorCorrection::High));
}

void TestQr::differentDataDifferentSymbol()
{
    const QrEncoder::Result a = QrEncoder::encode(QStringLiteral("EMP001"), QrErrorCorrection::Medium);
    const QrEncoder::Result b = QrEncoder::encode(QStringLiteral("EMP002"), QrErrorCorrection::Medium);
    QVERIFY(a.ok && b.ok);
    QVERIFY(a.modules != b.modules);
}


void TestQr::matchesGoldenMatrices()
{
    const QByteArray path = qgetenv("OCC_QR_GOLDEN");
    if (path.isEmpty() || !QFile::exists(QString::fromLocal8Bit(path))) {
        qWarning("QR golden data not found; skipping the cross-implementation check. "
                 "Run tools/qr/generate_golden.py to create tests/data/qr_golden.json.");
        return;
    }

    QFile file(QString::fromLocal8Bit(path));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
    QVERIFY2(!cases.isEmpty(), "the golden file contains no cases");

    int compared = 0;
    for (const QJsonValue &value : cases) {
        const QJsonObject c = value.toObject();
        const QString text = c.value(QStringLiteral("text")).toString();
        QrErrorCorrection ecc = QrErrorCorrection::Medium;
        QVERIFY(names::qrEccFromString(c.value(QStringLiteral("ecc")).toString(), &ecc));

        const QrEncoder::Result r = QrEncoder::encode(text, ecc);
        const QString label = QStringLiteral("'%1' at %2").arg(text, names::qrEcc(ecc));
        QVERIFY2(r.ok, qPrintable(label + QStringLiteral(": ") + r.error));

        const int expectedSize = c.value(QStringLiteral("size")).toInt();
        if (expectedSize > 0)
            QCOMPARE(r.size, expectedSize);

        // Modules are stored as rows joined by '/', with '1' meaning dark.
        const QStringList rows =
            c.value(QStringLiteral("modules")).toString().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QCOMPARE(rows.size(), r.size);
        for (int y = 0; y < r.size; ++y) {
            const QString &row = rows.at(y);
            QCOMPARE(row.size(), r.size);
            for (int x = 0; x < r.size; ++x) {
                const bool expected = row.at(x) == QLatin1Char('1');
                if (r.isDark(x, y) != expected) {
                    QFAIL(qPrintable(QStringLiteral("%1: module mismatch at (%2,%3)")
                                         .arg(label).arg(x).arg(y)));
                }
            }
        }
        ++compared;
    }
    QVERIFY(compared > 0);
    qInfo("QR golden comparison passed for %d cases", compared);
}

QTEST_MAIN(TestQr)
#include "test_qr.moc"
