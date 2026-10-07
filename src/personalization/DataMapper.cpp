#include "personalization/DataMapper.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace occ {

namespace {

QString mapperTr(const char *text)
{
    return QCoreApplication::translate("DataMapper", text);
}

constexpr int kMappingFormatVersion = 1;

QStringList photoWords()
{
    return CsvImporter::photoFieldNames().split(QLatin1Char('|'), Qt::SkipEmptyParts);
}

} // namespace

// ---------------------------------------------------------------------------
// MappingSet
// ---------------------------------------------------------------------------

QString MappingSet::columnFor(const QString &placeholder) const
{
    for (const FieldMapping &field : fields) {
        if (field.placeholder == placeholder)
            return field.csvColumn;
    }
    return QString();
}

QString MappingSet::placeholderFor(const QString &csvColumn) const
{
    for (const FieldMapping &field : fields) {
        if (field.csvColumn == csvColumn)
            return field.placeholder;
    }
    return QString();
}

QStringList MappingSet::placeholders() const
{
    QStringList list;
    list.reserve(fields.size());
    for (const FieldMapping &field : fields)
        list.append(field.placeholder);
    return list;
}

QStringList MappingSet::csvColumns() const
{
    QStringList list;
    list.reserve(fields.size());
    for (const FieldMapping &field : fields)
        list.append(field.csvColumn);
    return list;
}

bool MappingSet::contains(const QString &placeholder) const
{
    for (const FieldMapping &field : fields) {
        if (field.placeholder == placeholder)
            return true;
    }
    return false;
}

void MappingSet::removePlaceholder(const QString &placeholder)
{
    for (int i = int(fields.size()) - 1; i >= 0; --i) {
        if (fields.at(i).placeholder == placeholder)
            fields.removeAt(i);
    }
    if (photoAssetPlaceholder == placeholder)
        photoAssetPlaceholder.clear();
}

bool MappingSet::operator==(const MappingSet &other) const
{
    if (fields.size() != other.fields.size())
        return false;
    for (int i = 0; i < fields.size(); ++i) {
        if (!(fields.at(i) == other.fields.at(i)))
            return false;
    }
    return photoAssetPlaceholder == other.photoAssetPlaceholder
           && relativePhotoBaseDir == other.relativePhotoBaseDir;
}

// ---------------------------------------------------------------------------
// Analysis
// ---------------------------------------------------------------------------

QStringList DataMapper::unmappedColumns(const CsvTable &table, const MappingSet &mapping)
{
    const QStringList mapped = mapping.csvColumns();
    QStringList result;
    for (const QString &header : table.headers) {
        if (!mapped.contains(header))
            result.append(header);
    }
    return result;
}

QStringList DataMapper::unmappedPlaceholders(const QStringList &documentPlaceholders,
                                             const MappingSet &mapping)
{
    QStringList result;
    for (const QString &placeholder : documentPlaceholders) {
        if (!mapping.contains(placeholder))
            result.append(placeholder);
    }
    return result;
}

QString DataMapper::normaliseKey(const QString &key)
{
    QString out;
    out.reserve(key.size());
    bool lastWasSeparator = true;    // drops leading separators
    for (const QChar c : key.trimmed()) {
        if (c.isLetterOrNumber()) {
            out.append(c.toLower());
            lastWasSeparator = false;
        } else if (c == QLatin1Char('_') || c == QLatin1Char('-')
                   || c == QLatin1Char(' ') || c == QLatin1Char('.')) {
            if (!lastWasSeparator) {
                out.append(QLatin1Char('_'));
                lastWasSeparator = true;
            }
        }
        // Any other punctuation is dropped outright.
    }
    while (out.endsWith(QLatin1Char('_')))
        out.chop(1);
    return out;
}

QStringList DataMapper::photoColumns(const CsvTable &table)
{
    const QStringList words = photoWords();
    QStringList exact;
    QStringList partial;
    for (const QString &header : table.headers) {
        const QString folded = header.trimmed().toCaseFolded();
        bool isExact = false;
        bool isPartial = false;
        for (const QString &word : words) {
            if (folded == word) {
                isExact = true;
                break;
            }
            if (folded.contains(word))
                isPartial = true;
        }
        if (isExact)
            exact.append(header);
        else if (isPartial)
            partial.append(header);
    }
    // Exact names first: a column literally called "photo" beats
    // "photo_last_updated".
    return exact + partial;
}

MappingSet DataMapper::suggestMapping(const CsvTable &table,
                                      const QStringList &documentPlaceholders)
{
    MappingSet mapping;
    const QStringList photoCandidates = photoColumns(table);

    // A case-insensitive exact header lookup first, then a normalised one.
    QHash<QString, QString> exactByFolded;
    QHash<QString, QString> normalisedByHeader;
    for (const QString &header : table.headers) {
        if (!exactByFolded.contains(header.toCaseFolded()))
            exactByFolded.insert(header.toCaseFolded(), header);
        const QString key = normaliseKey(header);
        if (!key.isEmpty() && !normalisedByHeader.contains(key))
            normalisedByHeader.insert(key, header);
    }

    for (const QString &placeholder : documentPlaceholders) {
        if (placeholder.isEmpty() || mapping.contains(placeholder))
            continue;

        // A placeholder that names a photo column is left to the user: guessing
        // which column holds the photograph is a decision with visible
        // consequences, so it is never made automatically.
        bool looksLikePhoto = false;
        for (const QString &word : photoWords()) {
            if (placeholder.toCaseFolded().contains(word)) {
                looksLikePhoto = true;
                break;
            }
        }

        QString column = exactByFolded.value(placeholder.toCaseFolded());
        if (column.isEmpty())
            column = normalisedByHeader.value(normaliseKey(placeholder));
        if (column.isEmpty())
            continue;
        if (looksLikePhoto && !photoCandidates.contains(column))
            continue;
        if (mapping.csvColumns().contains(column))
            continue;   // one column cannot feed two placeholders by accident

        FieldMapping field;
        field.placeholder = placeholder;
        field.csvColumn = column;
        if (field.isValid())
            mapping.fields.append(field);
    }

    return mapping;
}

QHash<QString, QString> DataMapper::valuesForRecord(const CsvTable &table, int row,
                                                   const MappingSet &mapping,
                                                   QString *error)
{
    QHash<QString, QString> values;
    if (row < 0 || row >= table.rowCount()) {
        if (error) {
            *error = mapperTr("The data set has %1 records, so record %2 does not exist.")
                         .arg(table.rowCount())
                         .arg(row + 1);
        }
        return values;
    }

    for (const FieldMapping &field : mapping.fields) {
        if (!field.isValid())
            continue;
        if (!table.hasHeader(field.csvColumn)) {
            if (error) {
                *error = mapperTr("The column \"%1\", which feeds {{%2}}, is not in "
                                  "this data set.")
                             .arg(field.csvColumn, field.placeholder);
            }
            return QHash<QString, QString>();
        }
        values.insert(field.placeholder, table.value(row, field.csvColumn));
    }

    if (error)
        error->clear();
    return values;
}

QString DataMapper::photoPathForRecord(const CsvTable &table, int row,
                                       const MappingSet &mapping, bool *missing)
{
    if (missing)
        *missing = false;
    if (mapping.photoAssetPlaceholder.isEmpty())
        return QString();
    if (row < 0 || row >= table.rowCount())
        return QString();

    const QString column = mapping.columnFor(mapping.photoAssetPlaceholder);
    if (column.isEmpty())
        return QString();

    const QString value = table.value(row, column).trimmed();
    if (value.isEmpty())
        return QString();   // "no photograph" is not the same as "photograph gone"

    QString path = value;
    // A relative reference is resolved against the configured base directory,
    // or against the data set's own folder when none was configured.
    if (QFileInfo(path).isRelative()) {
        QString base = mapping.relativePhotoBaseDir;
        if (base.isEmpty())
            base = QFileInfo(table.sourcePath).absolutePath();
        if (base.isEmpty())
            base = QDir::currentPath();
        path = QDir(base).filePath(value);
    }

    if (!QFileInfo::exists(path)) {
        if (missing)
            *missing = true;
        return QString();
    }
    return path;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

bool DataMapper::saveMapping(const MappingSet &mapping, const QString &path,
                             QString *error)
{
    if (path.trimmed().isEmpty()) {
        if (error)
            *error = mapperTr("No file name was given, so the mapping was not saved.");
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("formatVersion"), kMappingFormatVersion);

    QJsonArray fields;
    for (const FieldMapping &field : mapping.fields) {
        if (!field.isValid())
            continue;
        QJsonObject entry;
        entry.insert(QStringLiteral("placeholder"), field.placeholder);
        entry.insert(QStringLiteral("column"), field.csvColumn);
        fields.append(entry);
    }
    root.insert(QStringLiteral("fields"), fields);
    root.insert(QStringLiteral("photoPlaceholder"), mapping.photoAssetPlaceholder);
    root.insert(QStringLiteral("photoBaseDir"), mapping.relativePhotoBaseDir);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = mapperTr("The mapping could not be written to \"%1\".")
                         .arg(QFileInfo(path).fileName());
        }
        return false;
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    const bool written = file.write(data) == data.size();
    file.close();
    if (!written) {
        if (error) {
            *error = mapperTr("The mapping could not be written to \"%1\".")
                         .arg(QFileInfo(path).fileName());
        }
        return false;
    }
    if (error)
        error->clear();
    return true;
}

bool DataMapper::loadMapping(MappingSet *mapping, const QString &path, QString *error)
{
    if (!mapping) {
        if (error)
            *error = mapperTr("The mapping could not be loaded.");
        return false;
    }

    QFile file(path);
    if (!file.exists()) {
        if (error) {
            *error = mapperTr("\"%1\" does not exist.").arg(QFileInfo(path).fileName());
        }
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = mapperTr("\"%1\" could not be opened for reading.")
                         .arg(QFileInfo(path).fileName());
        }
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = mapperTr("\"%1\" is not a saved field mapping.\n%2")
                         .arg(QFileInfo(path).fileName(), parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = document.object();
    if (!root.contains(QStringLiteral("fields"))
        && !root.contains(QStringLiteral("photoPlaceholder"))) {
        // Something else is in this file; refusing is better than loading an
        // empty mapping and letting the user believe it worked.
        if (error) {
            *error = mapperTr("\"%1\" is not a saved field mapping.")
                         .arg(QFileInfo(path).fileName());
        }
        return false;
    }

    MappingSet loaded;
    const QJsonArray fields = root.value(QStringLiteral("fields")).toArray();
    for (const QJsonValue &value : fields) {
        if (!value.isObject())
            continue;
        const QJsonObject entry = value.toObject();
        FieldMapping field;
        field.placeholder = entry.value(QStringLiteral("placeholder")).toString();
        field.csvColumn = entry.value(QStringLiteral("column")).toString();
        if (field.isValid())
            loaded.fields.append(field);
    }
    loaded.photoAssetPlaceholder =
        root.value(QStringLiteral("photoPlaceholder")).toString();
    loaded.relativePhotoBaseDir = root.value(QStringLiteral("photoBaseDir")).toString();

    *mapping = loaded;
    if (error)
        error->clear();
    return true;
}

} // namespace occ
