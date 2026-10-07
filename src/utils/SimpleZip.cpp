#include "utils/SimpleZip.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringConverter>

#include <array>

namespace occ {

namespace {

// ---------------------------------------------------------------------------
// ZIP record signatures.
// ---------------------------------------------------------------------------
constexpr quint32 kLocalFileHeaderSignature = 0x04034b50u;
constexpr quint32 kCentralDirectorySignature = 0x02014b50u;
constexpr quint32 kEndOfCentralDirSignature = 0x06054b50u;
constexpr quint32 kZip64EndOfCentralDirSignature = 0x06064b50u;
constexpr quint32 kZip64EndOfCentralDirLocatorSignature = 0x07064b50u;

// ---------------------------------------------------------------------------
// Fixed field values. Everything here is constant, which is what makes the
// output byte identical from one save to the next.
// ---------------------------------------------------------------------------
constexpr quint16 kVersionNeeded = 20;        // "stored" needs no more than 1.0
constexpr quint16 kVersionMadeBy = 0x0014;    // high byte 0 = MS-DOS, low byte 20
constexpr quint16 kDosDate1980 = 0x0021;      // 1980-01-01
constexpr quint16 kDosTimeMidnight = 0x0000;  // 00:00:00
constexpr quint16 kFlagUtf8Names = 0x0800;    // bit 11: name bytes are UTF-8
constexpr quint16 kFlagEncrypted = 0x0001;    // bit 0: entry is encrypted
constexpr quint16 kMethodStored = 0;
constexpr int kLocalHeaderSize = 30;
constexpr int kCentralHeaderSize = 46;
constexpr int kEndOfCentralDirSize = 22;
constexpr int kMaxCommentBytes = 65535;

QString zipMessage(const char *text)
{
    return QCoreApplication::translate("SimpleZip", text);
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

void appendU16(QByteArray &out, quint16 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void appendU32(QByteArray &out, quint32 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
    out.append(char((value >> 16) & 0xFF));
    out.append(char((value >> 24) & 0xFF));
}

bool readU16(const QByteArray &data, qint64 offset, quint16 *value)
{
    if (!value || offset < 0 || offset + 2 > data.size())
        return false;
    const auto *p = reinterpret_cast<const unsigned char *>(data.constData() + offset);
    *value = quint16(quint16(p[0]) | (quint16(p[1]) << 8));
    return true;
}

bool readU32(const QByteArray &data, qint64 offset, quint32 *value)
{
    if (!value || offset < 0 || offset + 4 > data.size())
        return false;
    const auto *p = reinterpret_cast<const unsigned char *>(data.constData() + offset);
    *value = quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
    return true;
}

bool isPlainAscii(const QByteArray &bytes)
{
    for (const char c : bytes) {
        if (static_cast<unsigned char>(c) > 0x7F)
            return false;
    }
    return true;
}

} // namespace

SimpleZip::SimpleZip() = default;
SimpleZip::~SimpleZip() = default;

bool SimpleZip::addFile(const QString &name, const QByteArray &data, QString *error)
{
    if (!isValidEntryName(name, error))
        return false;

    if (qint64(data.size()) > kMaxEntryBytes) {
        setError(error, zipMessage("The file \"%1\" is too large to store (%2 bytes, the limit is %3 bytes).")
                            .arg(name)
                            .arg(data.size())
                            .arg(kMaxEntryBytes));
        return false;
    }

    for (Entry &entry : m_entries) {
        if (entry.name == name) {
            entry.data = data;      // duplicates replace, they never accumulate
            return true;
        }
    }

    if (m_entries.size() >= kMaxEntryCount) {
        setError(error, zipMessage("A project cannot hold more than %1 files.").arg(kMaxEntryCount));
        return false;
    }

    Entry entry;
    entry.name = name;
    entry.data = data;
    m_entries.append(entry);
    return true;
}

bool SimpleZip::contains(const QString &name) const
{
    for (const Entry &entry : m_entries) {
        if (entry.name == name)
            return true;
    }
    return false;
}

QByteArray SimpleZip::file(const QString &name) const
{
    for (const Entry &entry : m_entries) {
        if (entry.name == name)
            return entry.data;
    }
    return QByteArray();
}

QStringList SimpleZip::fileNames() const
{
    QStringList names;
    names.reserve(int(m_entries.size()));
    for (const Entry &entry : m_entries)
        names.append(entry.name);
    return names;
}

int SimpleZip::count() const
{
    return int(m_entries.size());
}

bool SimpleZip::looksLikeZip(const QByteArray &data)
{
    return data.size() >= 4
           && quint8(data.at(0)) == 0x50    // 'P'
           && quint8(data.at(1)) == 0x4B    // 'K'
           && quint8(data.at(2)) == 0x03
           && quint8(data.at(3)) == 0x04;
}

quint32 SimpleZip::crc32(const QByteArray &data)
{
    // Reflected CRC-32, polynomial 0xEDB88320, init 0xFFFFFFFF, final XOR.
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int bit = 0; bit < 8; ++bit)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();

    quint32 crc = 0xFFFFFFFFu;
    const auto *bytes = reinterpret_cast<const unsigned char *>(data.constData());
    for (qsizetype i = 0; i < data.size(); ++i)
        crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void SimpleZip::clear()
{
    m_entries.clear();
}

bool SimpleZip::isValidEntryName(const QString &name, QString *error)
{
    if (name.isEmpty()) {
        setError(error, zipMessage("The file name in the project is empty."));
        return false;
    }
    if (name.size() > 255) {
        setError(error, zipMessage("The file name \"%1...\" is too long (the limit is 255 characters).")
                            .arg(name.left(48)));
        return false;
    }
    if (name.contains(QLatin1Char('\\'))) {
        setError(error, zipMessage("The file name \"%1\" contains a backslash. Project paths use '/' as the separator.")
                            .arg(name));
        return false;
    }
    if (name.startsWith(QLatin1Char('/'))) {
        setError(error, zipMessage("The file name \"%1\" is an absolute path. Project paths must be relative.").arg(name));
        return false;
    }
    if (name.size() >= 2 && name.at(0).isLetter() && name.at(1) == QLatin1Char(':')) {
        setError(error, zipMessage("The file name \"%1\" contains a drive letter. Project paths must be relative.")
                            .arg(name));
        return false;
    }
    const QStringList components = name.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString &component : components) {
        if (component == QLatin1String("..")) {
            setError(error, zipMessage("The file name \"%1\" is not safe: it would write outside the project folder.")
                                .arg(name));
            return false;
        }
    }
    for (const QChar character : name) {
        const ushort code = character.unicode();
        if (code < 0x20 || code == 0x7F) {
            setError(error, zipMessage("The file name \"%1\" contains a character that cannot be stored.").arg(name));
            return false;
        }
    }
    return true;
}

QByteArray SimpleZip::toByteArray(QString *error) const
{
    if (m_entries.size() > kMaxEntryCount) {
        setError(error, zipMessage("A project cannot hold more than %1 files.").arg(kMaxEntryCount));
        return QByteArray();
    }

    QByteArray out;
    qint64 predicted = kEndOfCentralDirSize;
    for (const Entry &entry : m_entries) {
        predicted += kLocalHeaderSize + kCentralHeaderSize + entry.name.toUtf8().size()
                     + entry.data.size();
    }
    out.reserve(int(qBound<qint64>(0, predicted, 64LL * 1024 * 1024)));

    QVector<quint32> localOffsets;
    localOffsets.reserve(int(m_entries.size()));

    // ---- local file headers + stored payloads (insertion order) -----------
    for (const Entry &entry : m_entries) {
        if (qint64(entry.data.size()) > kMaxEntryBytes) {
            setError(error, zipMessage("The file \"%1\" is too large to store (%2 bytes).")
                                .arg(entry.name).arg(entry.data.size()));
            return QByteArray();
        }
        const QByteArray nameBytes = entry.name.toUtf8();
        if (nameBytes.size() > kMaxCommentBytes) {
            setError(error, zipMessage("The file name \"%1\" is too long to store.").arg(entry.name));
            return QByteArray();
        }
        if (qint64(out.size()) > qint64(0xFFFFFFFFu) - nameBytes.size() - kLocalHeaderSize) {
            setError(error, zipMessage("The project is larger than 4 GB and cannot be saved."));
            return QByteArray();
        }

        localOffsets.append(quint32(out.size()));
        const quint16 flags = isPlainAscii(nameBytes) ? quint16(0) : kFlagUtf8Names;
        const quint32 crc = crc32(entry.data);
        const quint32 size = quint32(entry.data.size());

        appendU32(out, kLocalFileHeaderSignature);
        appendU16(out, kVersionNeeded);
        appendU16(out, flags);
        appendU16(out, kMethodStored);
        appendU16(out, kDosTimeMidnight);
        appendU16(out, kDosDate1980);
        appendU32(out, crc);
        appendU32(out, size);       // compressed size == uncompressed size
        appendU32(out, size);       // (method 0 stores the bytes verbatim)
        appendU16(out, quint16(nameBytes.size()));
        appendU16(out, 0);          // no extra field
        out.append(nameBytes);
        out.append(entry.data);
    }

    // ---- central directory -------------------------------------------------
    const quint32 centralOffset = quint32(out.size());
    for (int i = 0; i < int(m_entries.size()); ++i) {
        const Entry &entry = m_entries.at(i);
        const QByteArray nameBytes = entry.name.toUtf8();
        const quint16 flags = isPlainAscii(nameBytes) ? quint16(0) : kFlagUtf8Names;
        const quint32 crc = crc32(entry.data);
        const quint32 size = quint32(entry.data.size());

        appendU32(out, kCentralDirectorySignature);
        appendU16(out, kVersionMadeBy);     // MS-DOS so Explorer shows a folder
        appendU16(out, kVersionNeeded);
        appendU16(out, flags);
        appendU16(out, kMethodStored);
        appendU16(out, kDosTimeMidnight);
        appendU16(out, kDosDate1980);
        appendU32(out, crc);
        appendU32(out, size);
        appendU32(out, size);
        appendU16(out, quint16(nameBytes.size()));
        appendU16(out, 0);                  // extra field length
        appendU16(out, 0);                  // file comment length
        appendU16(out, 0);                  // disk number start
        appendU16(out, 0);                  // internal attributes
        appendU32(out, 0);                  // external attributes: plain file
        appendU32(out, localOffsets.at(i)); // relative offset of local header
        out.append(nameBytes);
    }
    const quint32 centralSize = quint32(out.size()) - centralOffset;

    // ---- end of central directory -----------------------------------------
    appendU32(out, kEndOfCentralDirSignature);
    appendU16(out, 0);                      // this disk
    appendU16(out, 0);                      // disk with the central directory
    appendU16(out, quint16(m_entries.size()));
    appendU16(out, quint16(m_entries.size()));
    appendU32(out, centralSize);
    appendU32(out, centralOffset);
    appendU16(out, 0);                      // archive comment length
    return out;
}

bool SimpleZip::writeToFile(const QString &path, QString *error) const
{
    if (path.isEmpty()) {
        setError(error, zipMessage("No file name was given for the project."));
        return false;
    }

    const QByteArray blob = toByteArray(error);
    if (blob.isEmpty()) {
        if (error && error->isEmpty())
            *error = zipMessage("The project could not be serialised.");
        return false;
    }

    // Make sure the destination folder exists before touching anything.
    const QFileInfo info(path);
    const QString folder = info.absolutePath();
    if (!folder.isEmpty()) {
        QDir directory;
        if (!directory.exists(folder) && !directory.mkpath(folder)) {
            setError(error, zipMessage("The folder \"%1\" does not exist and could not be created.")
                                .arg(QDir::toNativeSeparators(folder)));
            return false;
        }
    }

    // 1. Write the complete archive to "<path>.tmp" ...
    const QString tempPath = path + QStringLiteral(".tmp");
    {
        QFile temp(tempPath);
        if (!temp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            setError(error, zipMessage("The project could not be saved to \"%1\": %2")
                                .arg(QDir::toNativeSeparators(tempPath), temp.errorString()));
            return false;
        }
        const qint64 written = temp.write(blob);
        const bool flushed = temp.flush();
        const QString writeError = temp.errorString();
        const bool ok = (written == qint64(blob.size())) && flushed;
        temp.close();
        if (!ok || temp.error() != QFileDevice::NoError) {
            QFile::remove(tempPath);
            setError(error, zipMessage("The project could not be saved to \"%1\": %2")
                                .arg(QDir::toNativeSeparators(tempPath),
                                     writeError.isEmpty() ? zipMessage("not enough free space?") : writeError));
            return false;
        }
    }

    // 2. ... then move it over the target, so a crash never leaves a partial
    //    project behind. Windows cannot rename onto an existing file, so the
    //    old file is staged first and restored if the move fails.
    QString staged;
    if (QFile::exists(path)) {
        staged = path + QStringLiteral(".old");
        QFile::remove(staged);
        if (!QFile::rename(path, staged)) {
            staged.clear();
            if (!QFile::remove(path)) {
                QFile::remove(tempPath);
                setError(error, zipMessage("The existing project \"%1\" could not be replaced.")
                                    .arg(QDir::toNativeSeparators(path)));
                return false;
            }
        }
    }

    if (!QFile::rename(tempPath, path)) {
        if (!staged.isEmpty())
            QFile::rename(staged, path);    // put the previous version back
        QFile::remove(tempPath);
        setError(error, zipMessage("The project could not be moved into place at \"%1\".")
                            .arg(QDir::toNativeSeparators(path)));
        return false;
    }
    if (!staged.isEmpty())
        QFile::remove(staged);

    if (error)
        error->clear();
    return true;
}


namespace {

// Names are UTF-8 when bit 11 is set. Otherwise they are supposed to be
// CP437; UTF-8 is tried first (what almost every modern tool writes) and
// Latin-1 is the last resort so a legacy archive still opens.
QString decodeEntryName(const QByteArray &bytes, bool utf8Flag)
{
    if (utf8Flag)
        return QString::fromUtf8(bytes);
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder(bytes);
    if (decoder.hasError())
        return QString::fromLatin1(bytes);
    return text;
}

} // namespace

bool SimpleZip::readFromData(const QByteArray &data, QString *error)
{
    const qint64 total = data.size();
    if (total < kEndOfCentralDirSize) {
        setError(error, zipMessage("The file is too small to be a project archive (%1 bytes).").arg(total));
        return false;
    }

    // Scan backwards so an archive comment (or trailing junk) is tolerated.
    const qint64 lowest = qMax<qint64>(0, total - (kEndOfCentralDirSize + kMaxCommentBytes));
    qint64 eocd = -1;
    for (qint64 pos = total - kEndOfCentralDirSize; pos >= lowest; --pos) {
        quint32 signature = 0;
        if (!readU32(data, pos, &signature) || signature != kEndOfCentralDirSignature)
            continue;
        quint16 commentLength = 0;
        if (!readU16(data, pos + 20, &commentLength))
            continue;
        if (pos + kEndOfCentralDirSize + commentLength == total) {
            eocd = pos;
            break;
        }
    }
    if (eocd < 0) {
        setError(error, zipMessage("This is not a valid project archive: the end of central directory "
                                   "record is missing."));
        return false;
    }

    quint16 entriesOnDisk = 0;
    quint16 entryCount = 0;
    quint32 centralSize = 0;
    quint32 centralOffset = 0;
    readU16(data, eocd + 8, &entriesOnDisk);
    readU16(data, eocd + 10, &entryCount);
    readU32(data, eocd + 12, &centralSize);
    readU32(data, eocd + 16, &centralOffset);

    quint32 probe = 0;
    const qint64 locatorPos = eocd - 20;
    const bool zip64Locator = locatorPos >= 0 && readU32(data, locatorPos, &probe)
                              && probe == kZip64EndOfCentralDirLocatorSignature;
    const bool zip64Record = readU32(data, qint64(centralOffset), &probe)
                             && probe == kZip64EndOfCentralDirSignature;
    if (zip64Locator || zip64Record || entriesOnDisk == 0xFFFFu || entryCount == 0xFFFFu
        || centralOffset == 0xFFFFFFFFu || centralSize == 0xFFFFFFFFu) {
        setError(error, zipMessage("ZIP64 archives are not supported"));
        return false;
    }

    if (int(entryCount) > kMaxEntryCount) {
        setError(error, zipMessage("The archive contains %1 files; the limit is %2.")
                            .arg(entryCount).arg(kMaxEntryCount));
        return false;
    }
    if (entriesOnDisk != entryCount) {
        setError(error, zipMessage("Split (multi disk) archives are not supported."));
        return false;
    }
    if (qint64(centralOffset) > total || qint64(centralOffset) + qint64(centralSize) > total) {
        setError(error, zipMessage("The archive is truncated: its central directory lies outside the file."));
        return false;
    }

    // Parse into a scratch list; m_entries is only touched when everything
    // succeeded, so a failed read leaves the object as it was.
    QVector<Entry> parsed;
    parsed.reserve(int(entryCount));
    qint64 position = centralOffset;



    for (int index = 0; index < int(entryCount); ++index) {
        quint32 signature = 0;
        if (!readU32(data, position, &signature) || signature != kCentralDirectorySignature) {
            setError(error, zipMessage("The archive is corrupt: it announces %1 files but only %2 "
                                       "central directory entries could be read.")
                                .arg(entryCount).arg(index));
            return false;
        }
        if (position + kCentralHeaderSize > total) {
            setError(error, zipMessage("The archive is truncated inside its central directory."));
            return false;
        }

        quint16 flags = 0;
        quint16 method = 0;
        quint16 nameLength = 0;
        quint16 extraLength = 0;
        quint16 entryCommentLength = 0;
        quint32 crc = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
        quint32 localOffset = 0;
        readU16(data, position + 8, &flags);
        readU16(data, position + 10, &method);
        readU32(data, position + 16, &crc);
        readU32(data, position + 20, &compressedSize);
        readU32(data, position + 24, &uncompressedSize);
        readU16(data, position + 28, &nameLength);
        readU16(data, position + 30, &extraLength);
        readU16(data, position + 32, &entryCommentLength);
        readU32(data, position + 42, &localOffset);

        const qint64 namePosition = position + kCentralHeaderSize;
        if (namePosition + nameLength + extraLength + entryCommentLength > total) {
            setError(error, zipMessage("The archive is truncated inside a central directory entry."));
            return false;
        }
        const QString name = decodeEntryName(data.mid(namePosition, nameLength),
                                             (flags & kFlagUtf8Names) != 0);

        if (flags & kFlagEncrypted) {
            setError(error, zipMessage("The archive entry \"%1\" is encrypted, which is not supported.").arg(name));
            return false;
        }
        if (method != kMethodStored) {
            setError(error, zipMessage("The archive entry \"%1\" uses compression method %2 (deflate). "
                                       "Only stored (uncompressed) entries are supported.")
                                .arg(name).arg(method));
            return false;
        }
        if (qint64(uncompressedSize) > kMaxEntryBytes || compressedSize != uncompressedSize) {
            setError(error, zipMessage("The archive entry \"%1\" declares an unusable size.").arg(name));
            return false;
        }
        if (!isValidEntryName(name, nullptr)) {
            setError(error, zipMessage("The archive contains an unsafe file name (\"%1\") and was rejected.")
                                .arg(name.left(64)));
            return false;
        }

        // Read the payload through the local header, so a mismatching central
        // directory cannot make us return the wrong bytes.
        if (qint64(localOffset) + kLocalHeaderSize > total) {
            setError(error, zipMessage("The archive is truncated: the local header of \"%1\" is missing.").arg(name));
            return false;
        }
        quint32 localSignature = 0;
        readU32(data, localOffset, &localSignature);
        if (localSignature != kLocalFileHeaderSignature) {
            setError(error, zipMessage("The archive is corrupt: \"%1\" has no local file header.").arg(name));
            return false;
        }
        quint16 localNameLength = 0;
        quint16 localExtraLength = 0;
        readU16(data, localOffset + 26, &localNameLength);
        readU16(data, localOffset + 28, &localExtraLength);
        const qint64 dataStart = qint64(localOffset) + kLocalHeaderSize + localNameLength + localExtraLength;
        if (dataStart < 0 || dataStart + qint64(compressedSize) > total) {
            setError(error, zipMessage("The archive is truncated: the contents of \"%1\" are incomplete.").arg(name));
            return false;
        }

        const QByteArray payload = data.mid(dataStart, compressedSize);
        if (crc32(payload) != crc) {
            setError(error, zipMessage("The archive entry \"%1\" is corrupt: its checksum does not match.").arg(name));
            return false;
        }

        Entry entry;
        entry.name = name;
        entry.data = payload;
        parsed.append(entry);

        position = namePosition + nameLength + extraLength + entryCommentLength;
    }

    // More central directory records than the EOCD announced?
    if (position < qint64(centralOffset) + qint64(centralSize)) {
        quint32 extra = 0;
        if (readU32(data, position, &extra) && extra == kCentralDirectorySignature) {
            setError(error, zipMessage("The archive is corrupt: the central directory holds more files "
                                       "than announced."));
            return false;
        }
    }

    m_entries = parsed;
    if (error)
        error->clear();
    return true;
}

bool SimpleZip::readFromFile(const QString &path, QString *error)
{
    if (path.isEmpty()) {
        setError(error, zipMessage("No project file was given."));
        return false;
    }

    QFile file(path);
    if (!file.exists()) {
        setError(error, zipMessage("The project file \"%1\" does not exist.")
                            .arg(QDir::toNativeSeparators(path)));
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, zipMessage("The project file \"%1\" could not be opened: %2")
                            .arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }
    const QByteArray data = file.readAll();
    const QString readError = file.errorString();
    file.close();
    if (data.isEmpty() && !readError.isEmpty()) {
        setError(error, zipMessage("The project file \"%1\" could not be read: %2")
                            .arg(QDir::toNativeSeparators(path), readError));
        return false;
    }

    QString inner;
    if (!readFromData(data, &inner)) {
        setError(error, zipMessage("\"%1\" could not be opened: %2")
                            .arg(QDir::toNativeSeparators(path), inner));
        return false;
    }
    if (error)
        error->clear();
    return true;
}

} // namespace occ

