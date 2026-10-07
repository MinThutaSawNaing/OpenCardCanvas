#pragma once

#include <QByteArray>
#include <QChar>
#include <QString>
#include <QStringList>
#include <QVector>

// ---------------------------------------------------------------------------
// CsvImporter - CSV parsing for card personalization.
//
// Handles the ways real-world CSV files are actually written: RFC 4180 quoting,
// embedded commas, quotes and line breaks inside quoted fields, UTF-8 with or
// without a byte order mark, CRLF or LF endings, and the usual delimiter
// variants (comma, semicolon - typical for European Excel, tab, pipe).
//
// Parsing never silently discards data: short rows are padded, long rows are
// kept, and duplicate header names are disambiguated. Problems that make the
// file unusable are reported through `error`.
// ---------------------------------------------------------------------------
namespace occ {

struct CsvTable
{
    QStringList          headers;
    QVector<QStringList> rows;
    QString              sourcePath;
    QChar                delimiter = QLatin1Char(',');
    bool                 hadByteOrderMark = false;

    bool isEmpty() const { return rows.isEmpty(); }
    int  rowCount() const { return int(rows.size()); }
    int  columnCount() const { return int(headers.size()); }

    bool hasHeader(const QString &name) const;
    int  columnIndex(const QString &name) const;      // -1 when absent
    QString value(int row, int column) const;
    QString value(int row, const QString &header) const;
    // Every value in the table under `header`, used to build mapping previews.
    QStringList columnValues(const QString &header) const;
};

class CsvImporter
{
public:
    struct Options
    {
        // Null character means "detect automatically".
        QChar   delimiter = QChar();
        bool    firstRowIsHeader = true;
        bool    trimFields = true;
        // Cap on rows, so a runaway file cannot exhaust memory. 0 = no limit.
        int     maxRows = 500000;
    };

    // Parses CSV text. `error` receives a user facing explanation on failure.
    static CsvTable parse(const QByteArray &data, const Options &options,
                          QString *error = nullptr);

    static CsvTable loadFile(const QString &path, const Options &options,
                             QString *error = nullptr);

    // Picks the most likely delimiter by counting occurrences in the first
    // lines, ignoring quoted regions.
    static QChar detectDelimiter(const QByteArray &data);

    // Serialises a table back to CSV (used by "export the data set").
    static QByteArray write(const CsvTable &table, QChar delimiter = QLatin1Char(','));

    // Field names offered by the personalization dialog as ready-made mappings.
    static QStringList predefinedFields();      // name, employee_id, department, photo, ...
    static QString     photoFieldNames();       // "photo|photograph|picture|image|portrait"
};

} // namespace occ
