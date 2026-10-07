#include "personalization/CsvImporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringConverter>

namespace occ {

namespace {

constexpr int kMaxDetectLines = 20;
constexpr int kDelimiterCount = 4;
const char kDelimiterCandidates[kDelimiterCount] = {',', ';', '\t', '|'};

QString csvMessage(const char *text)
{
    return QCoreApplication::translate("CsvImporter", text);
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

// UTF-8 with a best-effort Latin-1 fallback so a legacy file still opens.
QString decodeText(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return QString();
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder(bytes);
    if (decoder.hasError()) {
        // Best effort only: bytes above 0x7F are interpreted as Latin-1.
        return QString::fromLatin1(bytes);
    }
    return text;
}

QByteArray encodeField(const QString &value, QChar delimiter)
{
    const bool needsQuotes = value.contains(delimiter) || value.contains(QLatin1Char('"'))
                             || value.contains(QLatin1Char('\r')) || value.contains(QLatin1Char('\n'));
    if (!needsQuotes)
        return value.toUtf8();

    QString quoted = value;
    quoted.replace(QLatin1Char('"'), QLatin1String("\"\""));
    QByteArray out;
    out.reserve(quoted.size() * 2 + 2);
    out.append('"');
    out.append(quoted.toUtf8());
    out.append('"');
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// CsvTable
// ---------------------------------------------------------------------------

bool CsvTable::hasHeader(const QString &name) const
{
    return columnIndex(name) >= 0;
}

int CsvTable::columnIndex(const QString &name) const
{
    const int exact = headers.indexOf(name);
    if (exact >= 0)
        return exact;
    for (int i = 0; i < headers.size(); ++i) {
        if (headers.at(i).compare(name, Qt::CaseInsensitive) == 0)
            return i;
    }
    return -1;
}

QString CsvTable::value(int row, int column) const
{
    if (row < 0 || column < 0 || row >= rows.size())
        return QString();
    const QStringList &values = rows.at(row);
    if (column >= values.size())
        return QString();
    return values.at(column);
}

QString CsvTable::value(int row, const QString &header) const
{
    return value(row, columnIndex(header));
}

QStringList CsvTable::columnValues(const QString &header) const
{
    QStringList values;
    const int column = columnIndex(header);
    if (column < 0)
        return values;
    values.reserve(rows.size());
    for (const QStringList &row : rows)
        values.append(column < row.size() ? row.at(column) : QString());
    return values;
}

CsvTable CsvImporter::parse(const QByteArray &data, const Options &options, QString *error)
{
    CsvTable table;

    // ---- bytes -> text ----------------------------------------------------
    QByteArray payload = data;
    if (payload.size() >= 3 && quint8(payload.at(0)) == 0xEF && quint8(payload.at(1)) == 0xBB
        && quint8(payload.at(2)) == 0xBF) {
        payload.remove(0, 3);
        table.hadByteOrderMark = true;
    }

    QString text = decodeText(payload);
    if (text.startsWith(QChar(0xFEFF))) {       // BOM that arrived as a character
        text.remove(0, 1);
        table.hadByteOrderMark = true;
    }

    if (payload.isEmpty()) {
        setError(error, csvMessage("The file is empty."));
        return CsvTable();
    }

    table.delimiter = options.delimiter.isNull() ? detectDelimiter(data) : options.delimiter;
    const QChar delimiter = table.delimiter;
    const int rowLimit = options.maxRows > 0 ? options.maxRows : -1;
    const int headerRows = options.firstRowIsHeader ? 1 : 0;

    // ---- split into records (RFC 4180) ------------------------------------
    QVector<QStringList> records;
    QStringList current;
    QString field;
    bool inQuotes = false;
    bool limitExceeded = false;

    auto appendRecord = [&]() -> bool {
        current.append(field);
        field.clear();
        records.append(current);
        current.clear();
        if (rowLimit >= 0 && int(records.size()) - headerRows > rowLimit) {
            limitExceeded = true;
            return false;
        }
        return true;
    };

    const qsizetype size = text.size();
    for (qsizetype i = 0; i < size;) {
        const QChar character = text.at(i);

        if (inQuotes) {
            if (character == QLatin1Char('"')) {
                if (i + 1 < size && text.at(i + 1) == QLatin1Char('"')) {
                    field.append(QLatin1Char('"'));     // "" inside a quoted field
                    i += 2;
                    continue;
                }
                inQuotes = false;                       // closing quote
                ++i;
                continue;
            }
            field.append(character);                    // quoting protects CR/LF
            ++i;
            continue;
        }

        if (character == QLatin1Char('"') && field.isEmpty()) {
            inQuotes = true;                            // a quote only opens at field start
            ++i;
            continue;
        }
        if (character == delimiter) {
            current.append(field);
            field.clear();
            ++i;
            continue;
        }
        if (character == QLatin1Char('\r')) {
            ++i;
            if (i < size && text.at(i) == QLatin1Char('\n'))
                ++i;                                    // CRLF
            if (!appendRecord())
                break;
            continue;
        }
        if (character == QLatin1Char('\n')) {
            ++i;
            if (!appendRecord())
                break;
            continue;
        }
        field.append(character);
        ++i;
    }

    // A trailing newline must not create a spurious empty record.
    if (!limitExceeded && (!field.isEmpty() || !current.isEmpty()))
        appendRecord();

    if (limitExceeded) {
        setError(error, csvMessage("The file has more than %1 data rows. Increase the row limit or "
                                   "split the file into smaller ones.").arg(rowLimit));
        return CsvTable();
    }

    if (options.trimFields) {
        for (QStringList &record : records) {
            for (QString &value : record)
                value = value.trimmed();
        }
    }

    if (records.isEmpty()) {
        setError(error, csvMessage("The file is empty."));
        return CsvTable();
    }

    // ---- headers ----------------------------------------------------------
    QStringList rawHeaders;
    int firstDataRecord = 0;
    if (options.firstRowIsHeader) {
        rawHeaders = records.at(0);
        firstDataRecord = 1;
    } else {
        int widest = 0;
        for (const QStringList &record : records)
            widest = qMax(widest, int(record.size()));
        for (int column = 0; column < widest; ++column)
            rawHeaders.append(QStringLiteral("Column %1").arg(column + 1));
    }

    QStringList headers;
    headers.reserve(rawHeaders.size());
    for (int index = 0; index < rawHeaders.size(); ++index) {
        QString name = rawHeaders.at(index);
        if (name.startsWith(QChar(0xFEFF))) {
            name.remove(0, 1);
            table.hadByteOrderMark = true;
        }
        name = name.trimmed();
        if (name.isEmpty())
            name = QStringLiteral("Column %1").arg(index + 1);
        if (headers.contains(name)) {
            int suffix = 2;
            QString candidate;
            do {
                candidate = QStringLiteral("%1 (%2)").arg(name).arg(suffix++);
            } while (headers.contains(candidate));
            name = candidate;
        }
        headers.append(name);
    }

    table.headers = headers;
    const int columns = headers.size();
    table.rows.reserve(int(records.size()) - firstDataRecord);
    for (int index = firstDataRecord; index < int(records.size()); ++index) {
        QStringList row = records.at(index);
        while (row.size() < columns)                 // short rows are padded ...
            row.append(QString());
        table.rows.append(row);                      // ... long rows are kept
    }

    if (error)
        error->clear();
    return table;
}

QChar CsvImporter::detectDelimiter(const QByteArray &data)
{
    qint64 counts[kDelimiterCount] = {0, 0, 0, 0};
    bool inQuotes = false;
    int lineCount = 0;
    qsizetype start = 0;
    if (data.size() >= 3 && quint8(data.at(0)) == 0xEF && quint8(data.at(1)) == 0xBB
        && quint8(data.at(2)) == 0xBF) {
        start = 3;
    }

    for (qsizetype i = start; i < data.size(); ++i) {
        const char byte = data.at(i);
        if (inQuotes) {
            if (byte == '"') {
                if (i + 1 < data.size() && data.at(i + 1) == '"')
                    ++i;                    // escaped quote, stay inside
                else
                    inQuotes = false;
            }
            continue;                       // delimiters inside quotes do not count
        }
        if (byte == '"') {
            inQuotes = true;
            continue;
        }
        if (byte == '\r' || byte == '\n') {
            if (byte == '\r' && i + 1 < data.size() && data.at(i + 1) == '\n')
                ++i;
            if (++lineCount >= kMaxDetectLines)
                break;
            continue;
        }
        for (int candidate = 0; candidate < kDelimiterCount; ++candidate) {
            if (byte == kDelimiterCandidates[candidate]) {
                ++counts[candidate];
                break;
            }
        }
    }

    // The strongest candidate wins; the comma is the tie break / default.
    int best = 0;
    for (int candidate = 1; candidate < kDelimiterCount; ++candidate) {
        if (counts[candidate] > counts[best])
            best = candidate;
    }
    return QLatin1Char(kDelimiterCandidates[best]);
}

QByteArray CsvImporter::write(const CsvTable &table, QChar delimiter)
{
    if (delimiter.isNull())
        delimiter = QLatin1Char(',');

    QByteArray out;
    const char separator = delimiter.toLatin1();

    auto appendRecord = [&](const QStringList &fields) {
        for (int index = 0; index < fields.size(); ++index) {
            if (index > 0)
                out.append(separator);
            out.append(encodeField(fields.at(index), delimiter));
        }
        out.append("\r\n");                 // every record ends with CRLF
    };

    if (!table.headers.isEmpty())
        appendRecord(table.headers);
    for (const QStringList &row : table.rows)
        appendRecord(row);
    return out;
}

QStringList CsvImporter::predefinedFields()
{
    return {
        QStringLiteral("name"),
        QStringLiteral("employee_id"),
        QStringLiteral("department"),
        QStringLiteral("job_title"),
        QStringLiteral("expiry_date"),
        QStringLiteral("issue_date"),
        QStringLiteral("qr_data"),
        QStringLiteral("barcode_data"),
        QStringLiteral("photo"),
        QStringLiteral("email"),
        QStringLiteral("company"),
        QStringLiteral("card_number"),
        QStringLiteral("blood_group"),
        QStringLiteral("nationality"),
        QStringLiteral("date_of_birth")
    };
}

QString CsvImporter::photoFieldNames()
{
    return QStringLiteral("photo|photograph|picture|image|portrait");
}

CsvTable CsvImporter::loadFile(const QString &path, const Options &options, QString *error)
{
    if (path.isEmpty()) {
        setError(error, csvMessage("No CSV file was given."));
        return CsvTable();
    }

    QFile file(path);
    if (!file.exists()) {
        setError(error, csvMessage("The file \"%1\" does not exist.").arg(QDir::toNativeSeparators(path)));
        return CsvTable();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, csvMessage("The file \"%1\" could not be opened: %2")
                            .arg(QDir::toNativeSeparators(path), file.errorString()));
        return CsvTable();
    }
    const qint64 declaredSize = file.size();
    const QByteArray data = file.readAll();
    const QString readError = file.errorString();
    file.close();
    if (data.size() != declaredSize && !readError.isEmpty()) {
        setError(error, csvMessage("The file \"%1\" could not be read: %2")
                            .arg(QDir::toNativeSeparators(path), readError));
        return CsvTable();
    }

    QString inner;
    CsvTable table = parse(data, options, &inner);
    if (!inner.isEmpty()) {
        setError(error, csvMessage("The file \"%1\" could not be imported: %2")
                            .arg(QDir::toNativeSeparators(path), inner));
        return CsvTable();
    }

    table.sourcePath = path;
    if (error)
        error->clear();
    return table;
}

} // namespace occ
