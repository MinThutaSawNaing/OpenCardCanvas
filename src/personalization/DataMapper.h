#pragma once

#include "personalization/CsvImporter.h"
#include "personalization/TemplateEngine.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

// ---------------------------------------------------------------------------
// DataMapper - the bridge between a CSV export and a design's {{placeholders}}.
//
// The rule this class exists to enforce: nothing is guessed silently. A CSV
// column that no placeholder uses is reported, a placeholder that no column
// feeds is reported, and the user gets a mapping they can save next to the HR
// export so that next month is one click instead of ten minutes.
//
// Photo columns are handled separately because they are a file reference rather
// than a value: a column called "photo" holds "photos/jane.jpg" and is resolved
// relative to MappingSet::relativePhotoBaseDir.
// ---------------------------------------------------------------------------
namespace occ {

struct FieldMapping
{
    QString placeholder;    // "employee_id"
    QString csvColumn;      // "Employee ID"

    bool isValid() const { return !placeholder.isEmpty() && !csvColumn.isEmpty(); }
    bool operator==(const FieldMapping &other) const
    { return placeholder == other.placeholder && csvColumn == other.csvColumn; }
};

struct MappingSet
{
    QVector<FieldMapping> fields;
    // Placeholder that carries the photograph, e.g. "photo". Empty disables
    // photo handling entirely.
    QString photoAssetPlaceholder;
    // Directory the photo column's relative paths are resolved against. Empty
    // means "next to the CSV file".
    QString relativePhotoBaseDir;

    bool isEmpty() const { return fields.isEmpty() && photoAssetPlaceholder.isEmpty(); }
    QString columnFor(const QString &placeholder) const;
    QString placeholderFor(const QString &csvColumn) const;
    QStringList placeholders() const;
    QStringList csvColumns() const;
    bool contains(const QString &placeholder) const;
    void removePlaceholder(const QString &placeholder);
    bool operator==(const MappingSet &other) const;
};

class DataMapper
{
public:
    // `DataMapper::MappingSet` and `DataMapper::FieldMapping` are provided as
    // aliases of the namespace-scope types, so both spellings compile: the
    // mapping value types can be forward declared without pulling in this class,
    // and code that reads "the mapper's mapping set" still works.
    using MappingSet = occ::MappingSet;
    using FieldMapping = occ::FieldMapping;

    // --- analysis -----------------------------------------------------------
    static QStringList unmappedColumns(const CsvTable &table, const MappingSet &mapping);
    static QStringList unmappedPlaceholders(const QStringList &documentPlaceholders,
                                            const MappingSet &mapping);

    // Column headers that look like a photo reference, in table order.
    static QStringList photoColumns(const CsvTable &table);

    // Best effort mapping: exact (case insensitive) matches first, then a
    // normalised comparison (lower case, separators removed), then nothing.
    // Never invents a mapping for a photo column - that is the user's call.
    static MappingSet suggestMapping(const CsvTable &table,
                                     const QStringList &documentPlaceholders);

    // --- per record ---------------------------------------------------------
    // Values for one CSV row, keyed by placeholder. `error` is filled when the
    // row index is out of range or a mapped column no longer exists.
    static QHash<QString, QString> valuesForRecord(const CsvTable &table, int row,
                                                   const MappingSet &mapping,
                                                   QString *error = nullptr);

    // Absolute path of the photograph for one record, or an empty string when
    // the record has no photo, no photo column is mapped, or the file does not
    // exist. `missing` reports that a value was present but unusable, so the
    // caller can warn without treating "no photo" and "photo gone" alike.
    static QString photoPathForRecord(const CsvTable &table, int row,
                                      const MappingSet &mapping, bool *missing = nullptr);

    // --- persistence --------------------------------------------------------
    static bool saveMapping(const MappingSet &mapping, const QString &path,
                            QString *error = nullptr);
    static bool loadMapping(MappingSet *mapping, const QString &path,
                            QString *error = nullptr);

    // Canonical form of a placeholder key ("Employee ID" -> "employee_id"), used
    // by suggestMapping() and by the mapping editor's "normalise" action.
    static QString normaliseKey(const QString &key);
};

} // namespace occ
