// ---------------------------------------------------------------------------
// Unit tests: CSV import (occ::CsvImporter).
//
// These cover the ways real HR exports actually differ from the ideal: quoted
// delimiters, embedded newlines, doubled quotes, a UTF-8 BOM, CRLF endings,
// semicolon delimiters, ragged rows and duplicated column names.
// ---------------------------------------------------------------------------
#include "personalization/CsvImporter.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace occ;

class TestCsv : public QObject
{
    Q_OBJECT
private slots:
    void parsesSimpleTable();
    void handlesQuotedDelimitersAndNewlines();
    void stripsByteOrderMark();
    void detectsDelimiters();
    void headerlessModeGeneratesNames();
    void disambiguatesDuplicateHeaders();
    void padsRaggedRows();
    void rejectsEmptyInput();
    void headerOnlyIsValidButEmpty();
    void writeThenParseRoundTrips();
    void loadFileMissingPathFailsCleanly();
    void trimsFields();
};

void TestCsv::parsesSimpleTable()
{
    const QByteArray data =
        "name,employee_id,department\n"
        "John Doe,EMP001,IT\n"
        "Jane Roe,EMP002,HR\n";

    QString error;
    const CsvTable t = CsvImporter::parse(data, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(t.rowCount(), 2);
    QCOMPARE(t.columnCount(), 3);
    QCOMPARE(t.headers, QStringList({ QStringLiteral("name"), QStringLiteral("employee_id"),
                                      QStringLiteral("department") }));
    QCOMPARE(t.value(0, QStringLiteral("name")), QStringLiteral("John Doe"));
    QCOMPARE(t.value(1, QStringLiteral("department")), QStringLiteral("HR"));
    QCOMPARE(t.value(0, 0), QStringLiteral("John Doe"));
    QVERIFY(t.hasHeader(QStringLiteral("employee_id")));
    QCOMPARE(t.columnIndex(QStringLiteral("employee_id")), 1);
    QCOMPARE(t.columnIndex(QStringLiteral("nope")), -1);
}

void TestCsv::handlesQuotedDelimitersAndNewlines()
{
    const QByteArray data =
        "name,notes\r\n"
        "\"Doe, John\",\"He said \"\"hi\"\"\"\r\n"
        "\"Line,break\",\"first\nsecond\"\r\n";

    QString error;
    const CsvTable t = CsvImporter::parse(data, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(t.rowCount(), 2);
    QCOMPARE(t.value(0, QStringLiteral("name")), QStringLiteral("Doe, John"));
    QCOMPARE(t.value(0, QStringLiteral("notes")), QStringLiteral("He said \"hi\""));
    QCOMPARE(t.value(1, QStringLiteral("name")), QStringLiteral("Line,break"));
    // The embedded newline must stay inside the field, not split the row.
    QCOMPARE(t.value(1, QStringLiteral("notes")), QStringLiteral("first\nsecond"));
}

void TestCsv::stripsByteOrderMark()
{
    QByteArray data("\xEF\xBB\xBF", 3);
    data += "name,id\nA,1\n";

    QString error;
    const CsvTable t = CsvImporter::parse(data, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(t.hadByteOrderMark);
    // The first header must not carry the BOM into its name.
    QCOMPARE(t.headers.first(), QStringLiteral("name"));
    QCOMPARE(t.value(0, QStringLiteral("name")), QStringLiteral("A"));
}

void TestCsv::detectsDelimiters()
{
    QCOMPARE(CsvImporter::detectDelimiter("a,b,c\n1,2,3\n"), QLatin1Char(','));
    QCOMPARE(CsvImporter::detectDelimiter("a;b;c\n1;2;3\n"), QLatin1Char(';'));
    QCOMPARE(CsvImporter::detectDelimiter("a\tb\tc\n1\t2\t3\n"), QLatin1Char('\t'));
    QCOMPARE(CsvImporter::detectDelimiter("a|b|c\n1|2|3\n"), QLatin1Char('|'));
    // Ambiguous input falls back to a comma rather than guessing wildly.
    QCOMPARE(CsvImporter::detectDelimiter("single\n"), QLatin1Char(','));

    // A semicolon file parsed with automatic detection must give three columns.
    QString error;
    const CsvTable t = CsvImporter::parse("a;b;c\n1;2;3\n", {}, &error);
    QCOMPARE(t.columnCount(), 3);
    QCOMPARE(t.delimiter, QLatin1Char(';'));
}

void TestCsv::headerlessModeGeneratesNames()
{
    CsvImporter::Options options;
    options.firstRowIsHeader = false;
    QString error;
    const CsvTable t = CsvImporter::parse("1,2\n3,4\n", options, &error);
    QCOMPARE(t.headers, QStringList({ QStringLiteral("Column 1"), QStringLiteral("Column 2") }));
    QCOMPARE(t.rowCount(), 2);
    QCOMPARE(t.value(1, QStringLiteral("Column 2")), QStringLiteral("4"));
}

void TestCsv::disambiguatesDuplicateHeaders()
{
    QString error;
    const CsvTable t = CsvImporter::parse("name,name,name\nA,B,C\n", {}, &error);
    QCOMPARE(t.headers.size(), 3);
    QCOMPARE(t.headers.at(0), QStringLiteral("name"));
    QCOMPARE(t.headers.at(1), QStringLiteral("name (2)"));
    QCOMPARE(t.headers.at(2), QStringLiteral("name (3)"));
    QCOMPARE(t.value(0, 2), QStringLiteral("C"));
}


void TestCsv::padsRaggedRows()
{
    QString error;
    // The second record is short and the third is long; neither may be dropped.
    const CsvTable t = CsvImporter::parse("a,b,c\n1,2,3\n4,5\n6,7,8,9\n", {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(t.rowCount(), 3);
    QCOMPARE(t.value(1, QStringLiteral("c")), QString());
    QCOMPARE(t.value(2, QStringLiteral("c")), QStringLiteral("8"));
}

void TestCsv::rejectsEmptyInput()
{
    QString error;
    CsvImporter::parse(QByteArray(), {}, &error);
    QVERIFY(!error.isEmpty());

    error.clear();
    CsvImporter::loadFile(QStringLiteral("does-not-exist-anywhere.csv"), {}, &error);
    QVERIFY(!error.isEmpty());
}

void TestCsv::headerOnlyIsValidButEmpty()
{
    QString error;
    const CsvTable t = CsvImporter::parse("name,id\n", {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(t.columnCount(), 2);
    QCOMPARE(t.rowCount(), 0);
    QVERIFY(t.isEmpty());
}

void TestCsv::writeThenParseRoundTrips()
{
    CsvTable original;
    original.headers = { QStringLiteral("name"), QStringLiteral("notes"), QStringLiteral("id") };
    original.rows = {
        { QStringLiteral("Doe, John"), QStringLiteral("he said \"hi\""), QStringLiteral("1") },
        { QStringLiteral("Multi\nline"), QStringLiteral("plain"), QStringLiteral("2") },
    };

    const QByteArray written = CsvImporter::write(original);
    QVERIFY(!written.isEmpty());
    QVERIFY(written.contains("\r\n"));   // records end with CRLF

    QString error;
    const CsvTable parsed = CsvImporter::parse(written, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(parsed.headers, original.headers);
    QCOMPARE(parsed.rowCount(), original.rows.size());
    for (int r = 0; r < original.rows.size(); ++r) {
        for (int c = 0; c < original.headers.size(); ++c) {
            QCOMPARE(parsed.value(r, c), original.rows.at(r).at(c));
        }
    }
}

void TestCsv::loadFileMissingPathFailsCleanly()
{
    QString error;
    const CsvTable t = CsvImporter::loadFile(
        QStringLiteral("C:/definitely/not/here/nope.csv"), {}, &error);
    QVERIFY(!error.isEmpty());
    QCOMPARE(t.rowCount(), 0);

    // A real file on disk must load and be remembered.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("data.csv"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("name,id\nAlice,7\n");
    f.close();

    error.clear();
    const CsvTable loaded = CsvImporter::loadFile(path, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(loaded.rowCount(), 1);
    QCOMPARE(loaded.value(0, QStringLiteral("name")), QStringLiteral("Alice"));
    QCOMPARE(loaded.sourcePath, path);
}

void TestCsv::trimsFields()
{
    QString error;
    const CsvTable t = CsvImporter::parse("name , id\n  Alice  , 7 \n", {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // Headers are trimmed so "name " and "name" are the same column.
    QCOMPARE(t.headers.at(0), QStringLiteral("name"));
    QCOMPARE(t.value(0, QStringLiteral("id")), QStringLiteral("7"));
}

QTEST_MAIN(TestCsv)
#include "test_csv.moc"
