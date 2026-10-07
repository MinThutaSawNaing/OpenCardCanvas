#include "project/ProjectSerializer.h"

#include "core/CardDocument.h"
#include "core/CardGeometry.h"
#include "core/ObjectFactory.h"
#include "core/Units.h"
#include "project/AssetStore.h"
#include "rendering/CardRenderer.h"
#include "utils/Logger.h"
#include "utils/SimpleZip.h"
#include "utils/TextUtils.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTimeZone>

namespace occ {

namespace {

QString projectTr(const char *text)
{
    return QCoreApplication::translate("ProjectSerializer", text);
}

const char *const kProjectEntry = "project.json";
const char *const kThumbnailEntry = "thumbnail.png";
const char *const kAssetsPrefix = "assets/";

// Every key the reader understands. Anything else is reported, which is how a
// project written by a newer build can be opened with a clear "these fields were
// ignored" note instead of silently losing them.
const QStringList &knownTopLevelKeys()
{
    static const QStringList keys = {
        QStringLiteral("formatVersion"), QStringLiteral("application"),
        QStringLiteral("metadata"),      QStringLiteral("geometry"),
        QStringLiteral("front"),         QStringLiteral("back"),
        QStringLiteral("printSettings"), QStringLiteral("guides"),
    };
    return keys;
}

const QStringList &knownMetadataKeys()
{
    static const QStringList keys = {
        QStringLiteral("title"),        QStringLiteral("author"),
        QStringLiteral("notes"),        QStringLiteral("created"),
        QStringLiteral("modified"),     QStringLiteral("isTemplate"),
        QStringLiteral("templateName"), QStringLiteral("templateDescription"),
        QStringLiteral("templatePlaceholders"),
    };
    return keys;
}

const QStringList &knownSideKeys()
{
    static const QStringList keys = {
        QStringLiteral("background"), QStringLiteral("objects"),
        QStringLiteral("guides"),
    };
    return keys;
}

const QStringList &knownBackgroundKeys()
{
    static const QStringList keys = {
        QStringLiteral("kind"),     QStringLiteral("color"),
        QStringLiteral("color2"),   QStringLiteral("angleDeg"),
        QStringLiteral("assetId"),  QStringLiteral("stretchImage"),
    };
    return keys;
}

const QStringList &knownObjectKeys()
{
    static const QStringList keys = {
        QStringLiteral("type"),     QStringLiteral("id"),
        QStringLiteral("name"),     QStringLiteral("rectMm"),
        QStringLiteral("rotation"), QStringLiteral("opacity"),
        QStringLiteral("visible"),  QStringLiteral("locked"),
        QStringLiteral("z"),        QStringLiteral("properties"),
    };
    return keys;
}

const QStringList &knownPrintKeys()
{
    static const QStringList keys = {
        QStringLiteral("printer"),     QStringLiteral("copies"),
        QStringLiteral("duplex"),      QStringLiteral("frontEnabled"),
        QStringLiteral("backEnabled"), QStringLiteral("frontTopcoat"),
        QStringLiteral("backTopcoat"), QStringLiteral("hopper"),
        QStringLiteral("cardEjectSide"),
    };
    return keys;
}

void reportUnknownKeys(const QJsonObject &json, const QStringList &known,
                       const QString &where, QStringList *warnings)
{
    if (!warnings)
        return;
    QStringList unknown;
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        if (!known.contains(it.key()))
            unknown.append(it.key());
    }
    if (unknown.isEmpty())
        return;
    warnings->append(projectTr("%1 contains %2 field(s) this version of "
                               "OpenCardCanvas does not understand (%3). They were "
                               "ignored; the rest of the project loaded normally.")
                         .arg(where, QString::number(unknown.size()),
                              text::joinedForHumans(unknown)));
}

QDateTime parseIso(const QJsonValue &value);
QString isoForUtc(const QDateTime &value);

} // namespace

// ---------------------------------------------------------------------------
// Print settings
// ---------------------------------------------------------------------------
namespace {

ProjectSerializer::PrintSettings g_printSettings;

} // namespace

void ProjectSerializer::PrintSettings::reset()
{
    printer.clear();
    copies = 1;
    duplex = false;
    frontEnabled = true;
    backEnabled = false;
    frontTopcoat = QStringLiteral("DriverDefault");
    backTopcoat = QStringLiteral("DriverDefault");
    hopper.clear();
    cardEjectSide.clear();
}

QJsonObject ProjectSerializer::PrintSettings::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("printer"), printer);
    o.insert(QStringLiteral("copies"), qBound(1, copies, 10000));
    o.insert(QStringLiteral("duplex"), duplex);
    o.insert(QStringLiteral("frontEnabled"), frontEnabled);
    o.insert(QStringLiteral("backEnabled"), backEnabled);
    o.insert(QStringLiteral("frontTopcoat"), frontTopcoat);
    o.insert(QStringLiteral("backTopcoat"), backTopcoat);
    o.insert(QStringLiteral("hopper"), hopper);
    o.insert(QStringLiteral("cardEjectSide"), cardEjectSide);
    return o;
}

bool ProjectSerializer::PrintSettings::fromJson(const QJsonObject &json)
{
    if (json.isEmpty())
        return true;
    if (!json.contains(QStringLiteral("printer"))
        && !json.contains(QStringLiteral("copies"))
        && !json.contains(QStringLiteral("frontEnabled"))) {
        // A block that looks nothing like print settings is a real error: it
        // means the file is malformed rather than merely old.
        return false;
    }
    reset();
    printer = json.value(QStringLiteral("printer")).toString();
    copies = qBound(1, json.value(QStringLiteral("copies")).toInt(1), 10000);
    duplex = json.value(QStringLiteral("duplex")).toBool(false);
    frontEnabled = json.value(QStringLiteral("frontEnabled")).toBool(true);
    backEnabled = json.value(QStringLiteral("backEnabled")).toBool(false);

    const QString front = json.value(QStringLiteral("frontTopcoat")).toString();
    const QString back = json.value(QStringLiteral("backTopcoat")).toString();
    frontTopcoat = front.isEmpty() ? QStringLiteral("DriverDefault") : front;
    backTopcoat = back.isEmpty() ? QStringLiteral("DriverDefault") : back;
    hopper = json.value(QStringLiteral("hopper")).toString();
    cardEjectSide = json.value(QStringLiteral("cardEjectSide")).toString();
    return true;
}

void ProjectSerializer::setPrintSettings(const PrintSettings &settings)
{
    g_printSettings = settings;
}

ProjectSerializer::PrintSettings ProjectSerializer::printSettings()
{
    return g_printSettings;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------
namespace {

QJsonObject buildProjectJson(const CardDocument &doc,
                             const ProjectSerializer::ExtraMetadata *extra)
{
    QJsonObject project;
    project.insert(QStringLiteral("formatVersion"), ProjectSerializer::kFormatVersion);

    QJsonObject application;
    application.insert(QStringLiteral("name"), QStringLiteral("OpenCardCanvas"));
    application.insert(QStringLiteral("version"), QString::fromLatin1(OCC_VERSION_STRING));
    project.insert(QStringLiteral("application"), application);

    QJsonObject metadata;
    metadata.insert(QStringLiteral("title"), doc.title());
    metadata.insert(QStringLiteral("author"), doc.author());
    metadata.insert(QStringLiteral("notes"), doc.notes());

    const QDateTime created = doc.created().isValid() ? doc.created()
                                                      : QDateTime::currentDateTimeUtc();
    const QDateTime modified = doc.modified().isValid() ? doc.modified()
                                                        : QDateTime::currentDateTimeUtc();
    metadata.insert(QStringLiteral("created"), isoForUtc(created));
    metadata.insert(QStringLiteral("modified"), isoForUtc(modified));

    const bool isTemplate = extra ? (extra->asTemplate || doc.isTemplate())
                                  : doc.isTemplate();
    metadata.insert(QStringLiteral("isTemplate"), isTemplate);
    if (extra) {
        metadata.insert(QStringLiteral("templateName"), extra->templateName);
        metadata.insert(QStringLiteral("templateDescription"),
                        extra->templateDescription);
    }

    QJsonArray placeholders;
    for (const QString &key : doc.placeholders())
        placeholders.append(key);
    metadata.insert(QStringLiteral("templatePlaceholders"), placeholders);
    project.insert(QStringLiteral("metadata"), metadata);

    project.insert(QStringLiteral("geometry"), doc.geometry().toJson());
    project.insert(QStringLiteral("front"), doc.front().toJson());
    project.insert(QStringLiteral("back"), doc.back().toJson());
    project.insert(QStringLiteral("printSettings"),
                   ProjectSerializer::printSettings().toJson());
    return project;
}

QByteArray thumbnailFor(const CardDocument &doc)
{
    // A small front preview is what makes Open Recent and the template browser
    // useful instead of a list of file names. It is optional by design: a
    // failure here must never stop a project from being saved.
    QImage thumbnail;
    try {
        thumbnail = CardRenderer::renderPreviewThumbnail(doc, CardSideId::Front, 200);
    } catch (...) {
        return QByteArray();
    }
    if (thumbnail.isNull())
        return QByteArray();

    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly))
        return QByteArray();
    const bool saved = thumbnail.save(&buffer, "PNG");
    buffer.close();
    return saved ? bytes : QByteArray();
}

} // namespace

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------

ProjectSerializer::SaveResult ProjectSerializer::saveToData(const CardDocument &doc,
                                                           QByteArray *out)
{
    SaveResult result;
    if (!out) {
        result.error = projectTr("The project could not be prepared for saving.");
        result.technicalDetail = QStringLiteral("saveToData: out == nullptr");
        return result;
    }

    QString geometryError;
    if (!doc.geometry().isValid(&geometryError)) {
        result.error = projectTr("This project cannot be saved because its card size "
                                 "is not valid:\n%1")
                           .arg(geometryError);
        result.technicalDetail = QStringLiteral("geometry invalid: ") + geometryError;
        return result;
    }

    const QJsonObject project = buildProjectJson(doc, nullptr);
    const QByteArray json = QJsonDocument(project).toJson(QJsonDocument::Indented);

    SimpleZip zip;
    QString error;
    // Indented JSON on purpose: a .occard is a documented, diffable format and a
    // human being is expected to be able to read project.json.
    if (!zip.addFile(QString::fromLatin1(kProjectEntry), json, &error)) {
        result.error = projectTr("The project could not be saved:\n%1").arg(error);
        result.technicalDetail = QStringLiteral("zip.addFile(project.json): ") + error;
        return result;
    }

    const AssetStore *assets = doc.assets();
    if (assets) {
        const auto entries = assets->serialise();
        for (const auto &entry : entries) {
            const QString name = QString::fromLatin1(kAssetsPrefix) + entry.first;
            if (!zip.addFile(name, entry.second, &error)) {
                result.error = projectTr("The project could not be saved because one of "
                                         "its images is invalid:\n%1")
                                   .arg(error);
                result.technicalDetail = QStringLiteral("zip.addFile(") + name
                                         + QStringLiteral("): ") + error;
                return result;
            }
        }
    }

    const QByteArray thumbnail = thumbnailFor(doc);
    if (!thumbnail.isEmpty())
        zip.addFile(QString::fromLatin1(kThumbnailEntry), thumbnail, nullptr);

    const QByteArray data = zip.toByteArray(&error);
    if (data.isEmpty()) {
        result.error = projectTr("The project could not be saved:\n%1").arg(error);
        result.technicalDetail = QStringLiteral("zip.toByteArray: ") + error;
        return result;
    }
    *out = data;
    result.ok = true;
    return result;
}

ProjectSerializer::SaveResult ProjectSerializer::save(const CardDocument &doc,
                                                     const QString &path,
                                                     const ExtraMetadata *extra)
{
    SaveResult result;
    if (path.trimmed().isEmpty()) {
        result.error = projectTr("No file name was given, so the project was not saved.");
        result.technicalDetail = QStringLiteral("save: empty path");
        return result;
    }

    QString geometryError;
    if (!doc.geometry().isValid(&geometryError)) {
        result.error = projectTr("This project cannot be saved because its card size "
                                 "is not valid:\n%1")
                           .arg(geometryError);
        result.technicalDetail = QStringLiteral("geometry invalid: ") + geometryError;
        return result;
    }

    const QJsonObject project = buildProjectJson(doc, extra);
    const QByteArray json = QJsonDocument(project).toJson(QJsonDocument::Indented);

    SimpleZip zip;
    QString error;
    if (!zip.addFile(QString::fromLatin1(kProjectEntry), json, &error)) {
        result.error = projectTr("The project could not be saved:\n%1").arg(error);
        result.technicalDetail = QStringLiteral("zip.addFile(project.json): ") + error;
        return result;
    }

    const AssetStore *assets = doc.assets();
    if (assets) {
        const auto entries = assets->serialise();
        for (const auto &entry : entries) {
            const QString name = QString::fromLatin1(kAssetsPrefix) + entry.first;
            if (!zip.addFile(name, entry.second, &error)) {
                result.error = projectTr("The project could not be saved because one of "
                                         "its images is invalid:\n%1")
                                   .arg(error);
                result.technicalDetail = QStringLiteral("zip.addFile(") + name
                                         + QStringLiteral("): ") + error;
                return result;
            }
        }
    }

    const QByteArray thumbnail = thumbnailFor(doc);
    if (!thumbnail.isEmpty())
        zip.addFile(QString::fromLatin1(kThumbnailEntry), thumbnail, nullptr);

    // writeToFile() writes "<path>.tmp" and renames over the target, so a crash
    // or a full disk can never leave a half written project behind.
    if (!zip.writeToFile(path, &error)) {
        result.error = projectTr("The project could not be saved to \"%1\".\n%2")
                           .arg(QFileInfo(path).fileName(), error);
        result.technicalDetail = QStringLiteral("zip.writeToFile(") + path
                                 + QStringLiteral("): ") + error;
        return result;
    }

    result.ok = true;
    return result;
}

namespace {

bool validHAlign(const QString &s)
{
    HorizontalAlign a = HorizontalAlign::Left;
    return names::horizontalAlignFromString(s, &a);
}
bool validVAlign(const QString &s)
{
    VerticalAlign a = VerticalAlign::Top;
    return names::verticalAlignFromString(s, &a);
}
bool validFit(const QString &s)
{
    ImageFitMode m = ImageFitMode::Cover;
    return names::imageFitFromString(s, &m);
}
bool validCrop(const QString &s)
{
    CropShape c = CropShape::Rectangle;
    return names::cropShapeFromString(s, &c);
}
bool validShapeKind(const QString &s)
{
    ShapeKind k = ShapeKind::Rectangle;
    return names::shapeKindFromString(s, &k);
}
bool validSymbology(const QString &s)
{
    BarcodeSymbology b = BarcodeSymbology::Code128;
    return names::barcodeSymbologyFromString(s, &b);
}
bool validEcc(const QString &s)
{
    QrErrorCorrection e = QrErrorCorrection::Medium;
    return names::qrEccFromString(s, &e);
}
bool validContentKind(const QString &s)
{
    QrContentKind k = QrContentKind::PlainText;
    return names::qrContentKindFromString(s, &k);
}

void checkEnum(const QJsonValue &value, bool (*isValid)(const QString &),
               const QString &field, const QString &objectName, QStringList *warnings)
{
    if (!warnings || !value.isString())
        return;
    const QString text = value.toString();
    if (isValid(text))
        return;
    // A silently changed alignment or symbology is exactly the kind of damage a
    // user cannot see until the cards are already printed.
    warnings->append(projectTr("The %1 of \"%2\" is set to a value this version "
                               "does not know (%3); the default was used instead.")
                         .arg(field, objectName, text));
}

// Reports enum values in one object's properties that had to fall back.
void scanObjectEnums(const QJsonObject &object, QStringList *warnings)
{
    if (!warnings)
        return;
    const QString name =
        object.value(QStringLiteral("name")).toString(projectTr("an unnamed object"));
    const QString type = object.value(QStringLiteral("type")).toString();
    const QJsonObject props = object.value(QStringLiteral("properties")).toObject();
    if (props.isEmpty())
        return;

    if (type == QLatin1String("text")) {
        checkEnum(props.value(QStringLiteral("hAlign")), validHAlign,
                  projectTr("horizontal alignment"), name, warnings);
        checkEnum(props.value(QStringLiteral("vAlign")), validVAlign,
                  projectTr("vertical alignment"), name, warnings);
    } else if (type == QLatin1String("image") || type == QLatin1String("photo")) {
        checkEnum(props.value(QStringLiteral("fitMode")), validFit,
                  projectTr("fit mode"), name, warnings);
        checkEnum(props.value(QStringLiteral("cropShape")), validCrop,
                  projectTr("crop shape"), name, warnings);
    } else if (type == QLatin1String("shape")) {
        checkEnum(props.value(QStringLiteral("kind")), validShapeKind,
                  projectTr("shape"), name, warnings);
    } else if (type == QLatin1String("barcode")) {
        checkEnum(props.value(QStringLiteral("symbology")), validSymbology,
                  projectTr("barcode type"), name, warnings);
    } else if (type == QLatin1String("qr")) {
        checkEnum(props.value(QStringLiteral("errorCorrection")), validEcc,
                  projectTr("error correction level"), name, warnings);
        checkEnum(props.value(QStringLiteral("contentKind")), validContentKind,
                  projectTr("content type"), name, warnings);
    }
}

void scanSideEnums(const QJsonObject &sideJson, const QString &sideName,
                   QStringList *warnings)
{
    if (!warnings)
        return;
    const QJsonObject background = sideJson.value(QStringLiteral("background")).toObject();
    if (!background.isEmpty()) {
        static const QStringList kinds = { QStringLiteral("none"),
                                           QStringLiteral("solid"),
                                           QStringLiteral("linear"),
                                           QStringLiteral("radial"),
                                           QStringLiteral("image") };
        const QJsonValue kind = background.value(QStringLiteral("kind"));
        if (kind.isString() && !kinds.contains(kind.toString())) {
            warnings->append(projectTr("The background of the %1 side uses a fill "
                                       "type this version does not know (%2); a plain "
                                       "colour was used instead.")
                                 .arg(sideName, kind.toString()));
        }
    }

    const QJsonArray objects =
        sideJson.value(QStringLiteral("objects")).toArray();
    for (const QJsonValue &value : objects) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        reportUnknownKeys(object, knownObjectKeys(),
                          projectTr("An object on the %1 side").arg(sideName), warnings);
        scanObjectEnums(object, warnings);
    }
}

QDateTime parseIso(const QJsonValue &value)
{
    if (!value.isString())
        return QDateTime();
    const QString text = value.toString();
    if (text.isEmpty())
        return QDateTime();
    QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!parsed.isValid())
        parsed = QDateTime::fromString(text, Qt::ISODate);
    if (parsed.isValid())
        parsed.setTimeZone(QTimeZone::UTC);
    return parsed;
}

QString isoForUtc(const QDateTime &value)
{
    return value.toUTC().toString(Qt::ISODate);
}

} // namespace

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

namespace {

// Pulls project.json (and the asset entries) out of a .occard container, or
// treats the bytes as a bare project.json. Returns false with a user facing
// message when the bytes are not a project at all.
bool extractProject(const QByteArray &data, const QString &sourceName,
                    QByteArray *projectBytes,
                    QVector<QPair<QString, QByteArray>> *assets,
                    QStringList *warnings, QString *error, QString *technical)
{
    const auto fail = [error, technical](const QString &message,
                                         const QString &detail) {
        if (error)
            *error = message;
        if (technical)
            *technical = detail;
        return false;
    };

    if (data.isEmpty())
        return fail(projectTr("This file is empty, so it is not an OpenCardCanvas "
                              "project."),
                    QStringLiteral("load: zero bytes"));

    if (SimpleZip::looksLikeZip(data)) {
        SimpleZip zip;
        QString zipError;
        if (!zip.readFromData(data, &zipError)) {
            return fail(projectTr("\"%1\" is not a readable OpenCardCanvas project. "
                                  "The file may be damaged or incomplete.")
                            .arg(sourceName.isEmpty() ? projectTr("This file")
                                                      : sourceName),
                        QStringLiteral("SimpleZip::readFromData: ") + zipError);
        }
        if (!zip.contains(QString::fromLatin1(kProjectEntry))) {
            return fail(projectTr("\"%1\" is not an OpenCardCanvas project: it does "
                                  "not contain a project.json entry.")
                            .arg(sourceName.isEmpty() ? projectTr("This file")
                                                      : sourceName),
                        QStringLiteral("zip entries: ")
                            + zip.fileNames().join(QLatin1Char(',')));
        }
        *projectBytes = zip.file(QString::fromLatin1(kProjectEntry));

        // Assets live under assets/. Anything else in the container is ignored
        // (thumbnail.png is regenerated on save).
        const QStringList names = zip.fileNames();
        for (const QString &name : names) {
            if (!name.startsWith(QLatin1String(kAssetsPrefix)))
                continue;
            const QByteArray payload = zip.file(name);
            if (payload.isEmpty())
                continue;
            assets->append({ name, payload });
        }
        return true;
    }

    // Not a container: the loader accepts a bare project.json, which keeps hand
    // written and script generated projects usable.
    *projectBytes = data;
    if (warnings) {
        warnings->append(projectTr("This file is a plain JSON project rather than a "
                                   "compressed .occard container. It was loaded, but "
                                   "saving it will write a normal .occard file."));
    }
    return true;
}

} // namespace


// ---------------------------------------------------------------------------
// Migration
// ---------------------------------------------------------------------------
ProjectSerializer::LoadResult ProjectSerializer::migrate(QJsonObject *project,
                                                        int fromVersion)
{
    LoadResult result;
    if (!project) {
        result.error = projectTr("The project could not be upgraded.");
        result.technicalDetail = QStringLiteral("migrate: project == nullptr");
        return result;
    }

    if (fromVersion >= kFormatVersion) {
        result.ok = true;
        result.sourceFormatVersion = fromVersion;
        return result;
    }

    // Version 0 is a project.json written before "formatVersion" existed: a bare
    // hand written or script generated file. It is upgraded by filling in the
    // defaults this build would have written, so the rest of the loader only
    // ever sees a complete document.
    if (fromVersion == 0) {
        QJsonObject metadata = project->value(QStringLiteral("metadata")).toObject();
        if (!metadata.contains(QStringLiteral("created")))
            metadata.insert(QStringLiteral("created"), isoForUtc(QDateTime::currentDateTimeUtc()));
        if (!metadata.contains(QStringLiteral("modified")))
            metadata.insert(QStringLiteral("modified"), isoForUtc(QDateTime::currentDateTimeUtc()));
        if (!metadata.contains(QStringLiteral("author")))
            metadata.insert(QStringLiteral("author"), QString());
        if (!metadata.contains(QStringLiteral("notes")))
            metadata.insert(QStringLiteral("notes"), QString());
        if (!metadata.contains(QStringLiteral("isTemplate")))
            metadata.insert(QStringLiteral("isTemplate"), false);
        if (!metadata.contains(QStringLiteral("templatePlaceholders")))
            metadata.insert(QStringLiteral("templatePlaceholders"), QJsonArray());
        project->insert(QStringLiteral("metadata"), metadata);
        if (!project->contains(QStringLiteral("application"))) {
            QJsonObject application;
            application.insert(QStringLiteral("name"), QStringLiteral("OpenCardCanvas"));
            application.insert(QStringLiteral("version"), QString::fromLatin1(OCC_VERSION_STRING));
            project->insert(QStringLiteral("application"), application);
        }
        project->insert(QStringLiteral("formatVersion"), 1);
        result.warnings.append(projectTr("This project was written before the format "
                                         "gained a version number and has been "
                                         "upgraded (format 0 -> 1)."));
    }

    result.ok = true;
    result.sourceFormatVersion = fromVersion;
    return result;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

namespace {

// Builds the document from an already version-checked project.json. Declared
// here so loadFromData() reads top to bottom; defined below.
ProjectSerializer::LoadResult applyProject(
    CardDocument *doc, const QJsonObject &project,
    const QVector<QPair<QString, QByteArray>> &assetEntries, const QString &sourceName,
    ProjectSerializer::LoadResult result);

} // namespace

ProjectSerializer::LoadResult ProjectSerializer::loadFromData(CardDocument *doc,
                                                             const QByteArray &data,
                                                             const QString &sourceName)
{
    LoadResult result;
    if (!doc) {
        result.error = projectTr("The project could not be opened.");
        result.technicalDetail = QStringLiteral("loadFromData: doc == nullptr");
        return result;
    }

    QByteArray projectBytes;
    QVector<QPair<QString, QByteArray>> assetEntries;
    if (!extractProject(data, QFileInfo(sourceName).fileName(), &projectBytes,
                        &assetEntries, &result.warnings, &result.error,
                        &result.technicalDetail)) {
        return result;
    }

    QJsonParseError parseError{};
    const QJsonDocument json = QJsonDocument::fromJson(projectBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        result.error = projectTr("\"%1\" is not a readable OpenCardCanvas project: its "
                                 "contents are not valid JSON.\n%2")
                           .arg(sourceName.isEmpty() ? projectTr("This file")
                                                     : sourceName,
                                parseError.errorString());
        result.technicalDetail = QStringLiteral("QJsonDocument::fromJson: ")
                                 + parseError.errorString();
        return result;
    }

    QJsonObject project = json.object();

    // --- version ------------------------------------------------------------
    int version = 0;
    bool versionPresent = false;
    const QJsonValue versionValue = project.value(QStringLiteral("formatVersion"));
    if (versionValue.isDouble()) {
        versionPresent = true;
        version = versionValue.toInt(0);
    } else if (versionValue.isString()) {
        bool numOk = false;
        const int parsed = versionValue.toString().toInt(&numOk);
        if (numOk) {
            versionPresent = true;
            version = parsed;
        }
    }
    result.sourceFormatVersion = versionPresent ? version : 0;

    if (version > kFormatVersion) {
        // The one case that is refused outright: an older build must not guess at
        // a format it does not know and risk saving the newer fields away.
        result.error = projectTr("This project was created by a newer version of "
                                 "OpenCardCanvas (format %1); please update the "
                                 "application.")
                           .arg(version);
        result.technicalDetail = QStringLiteral("formatVersion=%1 > supported=%2")
                                     .arg(version)
                                     .arg(kFormatVersion);
        return result;
    }
    if (version < kFormatVersion) {
        const LoadResult migration = migrate(&project, version);
        if (!migration.ok) {
            result.error = migration.error;
            result.technicalDetail = migration.technicalDetail;
            return result;
        }
        result.warnings += migration.warnings;
    }

    return applyProject(doc, project, assetEntries, sourceName, std::move(result));
}

ProjectSerializer::LoadResult ProjectSerializer::load(CardDocument *doc, const QString &path)
{
    LoadResult result;
    if (path.trimmed().isEmpty()) {
        result.error = projectTr("No file name was given, so nothing was opened.");
        result.technicalDetail = QStringLiteral("load: empty path");
        return result;
    }

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        result.error = projectTr("\"%1\" does not exist.").arg(info.fileName());
        result.technicalDetail = QStringLiteral("load: missing file ") + path;
        return result;
    }
    if (info.size() > 512LL * 1024 * 1024) {
        result.error = projectTr("\"%1\" is too large to be an OpenCardCanvas project.")
                           .arg(info.fileName());
        result.technicalDetail = QStringLiteral("load: file exceeds 512 MB");
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = projectTr("\"%1\" could not be opened for reading. It may be in "
                                 "use by another application.")
                           .arg(info.fileName());
        result.technicalDetail = QStringLiteral("load: cannot open ") + path;
        return result;
    }
    const QByteArray data = file.readAll();
    file.close();

    return loadFromData(doc, data, path);
}

namespace {

// Builds the document from an already version-checked project.json.
ProjectSerializer::LoadResult applyProject(
    CardDocument *doc, const QJsonObject &project,
    const QVector<QPair<QString, QByteArray>> &assetEntries, const QString &sourceName,
    ProjectSerializer::LoadResult result)
{
    // Anything this build does not understand is reported rather than ignored in
    // silence, so a project written by a newer build can still be opened safely.
    reportUnknownKeys(project, knownTopLevelKeys(), projectTr("This project"),
                      &result.warnings);
    reportUnknownKeys(project.value(QStringLiteral("metadata")).toObject(),
                      knownMetadataKeys(), projectTr("The details of this project"),
                      &result.warnings);
    reportUnknownKeys(project.value(QStringLiteral("printSettings")).toObject(),
                      knownPrintKeys(),
                      projectTr("The print settings of this project"), &result.warnings);
    reportUnknownKeys(project.value(QStringLiteral("front")).toObject(), knownSideKeys(),
                      projectTr("The front side"), &result.warnings);
    reportUnknownKeys(project.value(QStringLiteral("back")).toObject(), knownSideKeys(),
                      projectTr("The back side"), &result.warnings);
    reportUnknownKeys(
        project.value(QStringLiteral("front")).toObject()
            .value(QStringLiteral("background")).toObject(),
        knownBackgroundKeys(), projectTr("The front background"), &result.warnings);
    reportUnknownKeys(
        project.value(QStringLiteral("back")).toObject()
            .value(QStringLiteral("background")).toObject(),
        knownBackgroundKeys(), projectTr("The back background"), &result.warnings);
    scanSideEnums(project.value(QStringLiteral("front")).toObject(), projectTr("front"),
                  &result.warnings);
    scanSideEnums(project.value(QStringLiteral("back")).toObject(), projectTr("back"),
                  &result.warnings);

    // --- geometry -----------------------------------------------------------
    // Validated by CardGeometry itself, so a hostile or damaged file cannot
    // produce a card that cannot be rendered.
    CardGeometry geometry = CardGeometry::isoId1();
    QString geometryError;
    if (!geometry.fromJson(project.value(QStringLiteral("geometry")).toObject(),
                           &geometryError)) {
        result.error = projectTr("\"%1\" cannot be opened because its card size is "
                                 "not valid:\n%2")
                           .arg(sourceName.isEmpty() ? projectTr("This project")
                                                     : sourceName,
                                geometryError);
        result.technicalDetail = QStringLiteral("CardGeometry::fromJson: ")
                                 + geometryError;
        return result;
    }

    // --- sides --------------------------------------------------------------
    // Both sides are built into scratch objects first, so a project with a broken
    // back side cannot leave the front side half applied in the open document.
    const CardSide::ObjectReader reader = [](const QJsonObject &objectJson,
                                             QString *error) {
        return ObjectFactory::createAndLoad(objectJson, error);
    };
    CardSide frontScratch(CardSideId::Front);
    QString sideError;
    if (!frontScratch.fromJson(project.value(QStringLiteral("front")).toObject(), reader,
                               &sideError)) {
        result.error = projectTr("\"%1\" cannot be opened because its front side could "
                                 "not be read:\n%2")
                           .arg(sourceName.isEmpty() ? projectTr("This project")
                                                     : sourceName,
                                sideError);
        result.technicalDetail = QStringLiteral("front.fromJson: ") + sideError;
        return result;
    }

    CardSide backScratch(CardSideId::Back);
    if (!backScratch.fromJson(project.value(QStringLiteral("back")).toObject(), reader,
                              &sideError)) {
        result.error = projectTr("\"%1\" cannot be opened because its back side could "
                                 "not be read:\n%2")
                           .arg(sourceName.isEmpty() ? projectTr("This project")
                                                     : sourceName,
                                sideError);
        result.technicalDetail = QStringLiteral("back.fromJson: ") + sideError;
        return result;
    }

    // --- commit -------------------------------------------------------------
    // Everything has validated: the document may now be modified.
    doc->setGeometry(geometry);

    if (AssetStore *assets = doc->assets()) {
        assets->clear();
        assets->deserialise(assetEntries, &result.warnings);
    }

    std::vector<CardObjectPtr> frontObjects = frontScratch.takeAll();
    doc->front().takeAll();
    doc->front().setBackground(frontScratch.background());
    for (CardObjectPtr &object : frontObjects)
        doc->front().insertObject(std::move(object));

    std::vector<CardObjectPtr> backObjects = backScratch.takeAll();
    doc->back().takeAll();
    doc->back().setBackground(backScratch.background());
    for (CardObjectPtr &object : backObjects)
        doc->back().insertObject(std::move(object));

    const QJsonObject metadata = project.value(QStringLiteral("metadata")).toObject();
    doc->setAuthor(metadata.value(QStringLiteral("author")).toString());
    doc->setNotes(metadata.value(QStringLiteral("notes")).toString());
    doc->setIsTemplate(metadata.value(QStringLiteral("isTemplate")).toBool(false));

    ProjectSerializer::PrintSettings settings;
    if (settings.fromJson(project.value(QStringLiteral("printSettings")).toObject()))
        ProjectSerializer::setPrintSettings(settings);
    else
        result.warnings.append(projectTr("The print settings stored in this project "
                                         "could not be read, so the previous settings "
                                         "were kept."));

    if (!sourceName.isEmpty())
        doc->setFilePath(sourceName);

    doc->notifyReloaded();
    // A freshly loaded project has no unsaved changes.
    doc->setDirty(false);

    result.ok = true;
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Peeking (recents list, template browser)
// ---------------------------------------------------------------------------

ProjectSerializer::PeekResult ProjectSerializer::peekData(const QByteArray &data)
{
    PeekResult result;

    QByteArray projectBytes;
    QVector<QPair<QString, QByteArray>> assetEntries;
    QStringList warnings;
    QString error;
    QString technical;
    if (!extractProject(data, QString(), &projectBytes, &assetEntries, &warnings, &error,
                        &technical)) {
        result.error = error;
        return result;
    }

    QJsonParseError parseError{};
    const QJsonDocument json = QJsonDocument::fromJson(projectBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        result.error = projectTr("This file is not a readable OpenCardCanvas project.");
        return result;
    }

    const QJsonObject project = json.object();
    result.formatVersion = project.contains(QStringLiteral("formatVersion"))
                               ? project.value(QStringLiteral("formatVersion")).toInt(0)
                               : 0;
    if (result.formatVersion > kFormatVersion) {
        result.error = projectTr("This project was created by a newer version of "
                                 "OpenCardCanvas (format %1); please update the "
                                 "application.")
                           .arg(result.formatVersion);
        return result;
    }

    const QJsonObject metadata = project.value(QStringLiteral("metadata")).toObject();
    // The template name is authoritative for a template; the title is the fallback
    // for an ordinary project.
    result.title = metadata.value(QStringLiteral("templateName")).toString();
    if (result.title.isEmpty())
        result.title = metadata.value(QStringLiteral("title")).toString();
    result.description = metadata.value(QStringLiteral("templateDescription")).toString();
    if (result.description.isEmpty())
        result.description = metadata.value(QStringLiteral("notes")).toString();
    result.isTemplate = metadata.value(QStringLiteral("isTemplate")).toBool(false);
    result.created = parseIso(metadata.value(QStringLiteral("created")));
    result.modified = parseIso(metadata.value(QStringLiteral("modified")));

    const QJsonArray placeholders =
        metadata.value(QStringLiteral("templatePlaceholders")).toArray();
    for (const QJsonValue &value : placeholders) {
        if (value.isString() && !value.toString().isEmpty())
            result.placeholders.append(value.toString());
    }

    result.ok = true;
    return result;
}

ProjectSerializer::PeekResult ProjectSerializer::peek(const QString &path)
{
    PeekResult result;
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        result.error = projectTr("\"%1\" does not exist.").arg(info.fileName());
        return result;
    }
    if (info.size() <= 0 || info.size() > 512LL * 1024 * 1024) {
        result.error = projectTr("\"%1\" is not a readable size for a project file.")
                           .arg(info.fileName());
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = projectTr("\"%1\" could not be opened for reading.")
                           .arg(info.fileName());
        return result;
    }
    const QByteArray data = file.readAll();
    file.close();
    return peekData(data);
}

} // namespace occ

