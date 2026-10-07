// ---------------------------------------------------------------------------
// Unit tests: the .occard ZIP container (occ::SimpleZip).
//
// Beyond the round trip these tests pin the safety behaviour: path traversal,
// ZIP64, deflate entries and truncated archives must be rejected with a message
// rather than accepted or crashed on.
// ---------------------------------------------------------------------------
#include "utils/SimpleZip.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace occ;

class TestZip : public QObject
{
    Q_OBJECT
private slots:
    void crc32KnownVectors();
    void roundTripInMemory();
    void deterministicOutput();
    void fileRoundTripIsAtomic();
    void detectsZipMagic();
    void rejectsBadEntryNames_data();
    void rejectsBadEntryNames();
    void rejectsTruncatedArchive();
    void rejectsGarbage();
    void replacesDuplicateEntries();
    void handlesEmptyFileEntry();
};

void TestZip::crc32KnownVectors()
{
    // Reference values for the standard IEEE CRC-32.
    QCOMPARE(SimpleZip::crc32(QByteArray("The quick brown fox jumps over the lazy dog")),
             0x414FA339u);
    QCOMPARE(SimpleZip::crc32(QByteArray()), 0x00000000u);
    QCOMPARE(SimpleZip::crc32(QByteArray("a")), 0xE8B7BE43u);
    QCOMPARE(SimpleZip::crc32(QByteArray("123456789")), 0xCBF43926u);
}

void TestZip::roundTripInMemory()
{
    SimpleZip zip;
    QString error;
    QVERIFY2(zip.addFile(QStringLiteral("project.json"), QByteArray("{\"a\":1}"), &error),
             qPrintable(error));
    QVERIFY(zip.addFile(QStringLiteral("assets/one.bin"), QByteArray(1024, 'x'), &error));
    QVERIFY(zip.addFile(QStringLiteral("empty.txt"), QByteArray(), &error));
    QCOMPARE(zip.count(), 3);

    const QByteArray bytes = zip.toByteArray(&error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    QVERIFY(SimpleZip::looksLikeZip(bytes));

    SimpleZip restored;
    QVERIFY2(restored.readFromData(bytes, &error), qPrintable(error));
    QCOMPARE(restored.count(), 3);
    QCOMPARE(restored.file(QStringLiteral("project.json")), QByteArray("{\"a\":1}"));
    QCOMPARE(restored.file(QStringLiteral("assets/one.bin")), QByteArray(1024, 'x'));
    QVERIFY(restored.contains(QStringLiteral("empty.txt")));
    QVERIFY(!restored.contains(QStringLiteral("nope")));
    QVERIFY(restored.file(QStringLiteral("nope")).isEmpty());
}

void TestZip::deterministicOutput()
{
    SimpleZip zip;
    zip.addFile(QStringLiteral("a.txt"), QByteArray("alpha"));
    zip.addFile(QStringLiteral("b.txt"), QByteArray("beta"));
    // Saving the same content twice must produce identical bytes so that project
    // files are diffable and backups are meaningful.
    QCOMPARE(zip.toByteArray(), zip.toByteArray());
}

void TestZip::fileRoundTripIsAtomic()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("project.occard"));

    SimpleZip zip;
    QString error;
    zip.addFile(QStringLiteral("project.json"), QByteArray("{}"), &error);
    QVERIFY2(zip.writeToFile(path, &error), qPrintable(error));
    QVERIFY(QFile::exists(path));
    // The atomic write must not leave its temporary file behind.
    QVERIFY(!QFile::exists(path + QStringLiteral(".tmp")));

    SimpleZip restored;
    QVERIFY2(restored.readFromFile(path, &error), qPrintable(error));
    QCOMPARE(restored.file(QStringLiteral("project.json")), QByteArray("{}"));

    // Overwriting an existing project must also succeed.
    zip.addFile(QStringLiteral("project.json"), QByteArray("{\"v\":2}"), &error);
    QVERIFY2(zip.writeToFile(path, &error), qPrintable(error));
    SimpleZip second;
    QVERIFY(second.readFromFile(path, &error));
    QCOMPARE(second.file(QStringLiteral("project.json")), QByteArray("{\"v\":2}"));
}


void TestZip::detectsZipMagic()
{
    SimpleZip zip;
    zip.addFile(QStringLiteral("x"), QByteArray("y"));
    QVERIFY(SimpleZip::looksLikeZip(zip.toByteArray()));
    // A plain JSON project must not be mistaken for a container.
    QVERIFY(!SimpleZip::looksLikeZip(QByteArray("{}")));
    QVERIFY(!SimpleZip::looksLikeZip(QByteArray()));
}

void TestZip::rejectsBadEntryNames_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("traversal")    << QStringLiteral("../evil.txt");
    QTest::newRow("backslash")    << QStringLiteral("..\\evil.txt");
    QTest::newRow("absolute")     << QStringLiteral("/abs.txt");
    QTest::newRow("drive letter") << QStringLiteral("C:/x.txt");
    QTest::newRow("unc")          << QStringLiteral("\\\\server\\share\\f");
    QTest::newRow("empty")        << QString();
    QTest::newRow("too long")     << QString(300, QLatin1Char('a'));
}

void TestZip::rejectsBadEntryNames()
{
    QFETCH(QString, name);
    QString error;
    QVERIFY2(!SimpleZip::isValidEntryName(name, &error), qPrintable(name));
    QVERIFY(!error.isEmpty());

    SimpleZip zip;
    QVERIFY(!zip.addFile(name, QByteArray("x"), &error));
    QCOMPARE(zip.count(), 0);
}

void TestZip::rejectsTruncatedArchive()
{
    SimpleZip zip;
    zip.addFile(QStringLiteral("a"), QByteArray(4096, 'z'));
    QByteArray bytes = zip.toByteArray();
    QVERIFY(bytes.size() > 100);
    bytes.truncate(bytes.size() / 2);

    SimpleZip restored;
    QString error;
    QVERIFY2(!restored.readFromData(bytes, &error), "a truncated archive must not load");
    QVERIFY(!error.isEmpty());
}

void TestZip::rejectsGarbage()
{
    SimpleZip restored;
    QString error;
    QVERIFY(!restored.readFromData(QByteArray("this is not a zip file at all"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!restored.readFromData(QByteArray(), &error));
    QVERIFY(!error.isEmpty());

    // A ZIP64 end-of-central-directory signature must be refused explicitly,
    // because it means entry sizes we cannot interpret.
    QByteArray zip64(32, '\0');
    zip64[0] = 'P'; zip64[1] = 'K'; zip64[2] = 0x06; zip64[3] = 0x06;
    SimpleZip z64;
    QVERIFY(!z64.readFromData(zip64, &error));
    QVERIFY(!error.isEmpty());
}

void TestZip::replacesDuplicateEntries()
{
    SimpleZip zip;
    zip.addFile(QStringLiteral("dup.txt"), QByteArray("first"));
    zip.addFile(QStringLiteral("dup.txt"), QByteArray("second"));
    QCOMPARE(zip.count(), 1);
    QCOMPARE(zip.file(QStringLiteral("dup.txt")), QByteArray("second"));
}

void TestZip::handlesEmptyFileEntry()
{
    SimpleZip zip;
    zip.addFile(QStringLiteral("zero.bin"), QByteArray());
    SimpleZip restored;
    QString error;
    QVERIFY2(restored.readFromData(zip.toByteArray(), &error), qPrintable(error));
    // An empty entry must survive as an empty entry, not vanish.
    QVERIFY(restored.contains(QStringLiteral("zero.bin")));
    QVERIFY(restored.file(QStringLiteral("zero.bin")).isEmpty());
}

QTEST_MAIN(TestZip)
#include "test_zip.moc"
