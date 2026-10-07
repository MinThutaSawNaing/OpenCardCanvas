#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

// ---------------------------------------------------------------------------
// SimpleZip - a small, dependency free ZIP container used by the .occard
// project format.
//
// Design decision: entries are stored UNCOMPRESSED (ZIP "store" method). Card
// artwork is already compressed (PNG/JPEG), so deflating it again would gain
// almost nothing, while storing it verbatim keeps the reader/writer small,
// deterministic and free of any third party compression dependency. A .occard
// file is therefore a completely standard ZIP archive that any zip tool can
// open and inspect - which is exactly what a documented project format needs.
//
// Safety: entry names are validated. Absolute paths, drive letters and ".."
// components are rejected, so a hostile project file cannot write outside the
// intended directory when extracted.
// ---------------------------------------------------------------------------
namespace occ {

class SimpleZip
{
public:
    static constexpr int kMaxEntryCount = 20000;
    static constexpr qint64 kMaxEntryBytes = 256LL * 1024 * 1024;

    SimpleZip();
    ~SimpleZip();

    void clear();

    // Adds or replaces an entry. `name` uses '/' separators and is stored with
    // a 1980-01-01 timestamp so that repeated saves are byte identical.
    bool addFile(const QString &name, const QByteArray &data, QString *error = nullptr);

    bool contains(const QString &name) const;
    // Returns the entry contents, or an empty array when absent. Use
    // contains() when an empty file is a meaningful result.
    QByteArray file(const QString &name) const;
    QStringList fileNames() const;
    int count() const;

    // Serialises the archive.
    QByteArray toByteArray(QString *error = nullptr) const;

    // Atomic write: writes to "<path>.tmp" then renames over `path`, so a
    // failure can never leave a half written project behind.
    bool writeToFile(const QString &path, QString *error = nullptr) const;

    bool readFromData(const QByteArray &data, QString *error = nullptr);
    bool readFromFile(const QString &path, QString *error = nullptr);

    // Cheap magic number test ("PK\x03\x04"). Used to tell a .occard container
    // apart from a plain JSON project file.
    static bool looksLikeZip(const QByteArray &data);

    // Standard CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320).
    static quint32 crc32(const QByteArray &data);

    // Validates an entry name. Exposed so tests can exercise it directly.
    static bool isValidEntryName(const QString &name, QString *error = nullptr);

private:
    struct Entry
    {
        QString    name;
        QByteArray data;
    };
    QVector<Entry> m_entries;
};

} // namespace occ
