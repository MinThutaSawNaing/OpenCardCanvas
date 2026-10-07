#pragma once

#include "occ/Version.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// ---------------------------------------------------------------------------
// ProjectSerializer - reads and writes the .occard project container.
//
// The container is a plain ZIP archive (occ::SimpleZip, "store" method) so a
// project can always be opened with any ZIP tool:
//
//     project.json            the document (see below)
//     assets/<assetId>.<ext>  the original image bytes, one entry per asset
//     thumbnail.png           optional ~200 px front preview for Open Recent
//
// Layout of project.json (see also docs/PROJECT_FORMAT.md):
//
// { "formatVersion": 1,
//   "application": { "name": "OpenCardCanvas", "version": "1.0.0" },
//   "metadata": { "title": ..., "author": ..., "notes": ...,
//                 "created": "<ISO8601>", "modified": "<ISO8601>",
//                 "isTemplate": false, "templateDescription": "",
//                 "templateName": "", "templatePlaceholders": [ ... ] },
//   "geometry": { ...CardGeometry::toJson()... },   // "preset","widthMm","heightMm",
//                                                   // "renderDpi","bleedMm"
//   "front": { ...CardSide::toJson()... },          // "background", "objects"[], "guides"[]
//   "back":  { ...CardSide::toJson()... },
//   "printSettings": { "printer": "", "copies": 1, "duplex": false,
//                      "frontEnabled": true, "backEnabled": false,
//                      "frontTopcoat": "DriverDefault", "backTopcoat": "DriverDefault",
//                      "hopper": "", "cardEjectSide": "" } }
//
// Loading is deliberately tolerant: unknown keys are ignored and reported as
// warnings, a missing side is treated as an empty side, and a file whose bytes
// are not a ZIP is parsed as a bare project.json (which keeps hand written and
// script generated projects usable). The only hard failure besides "the bytes
// are not a project at all" is a formatVersion NEWER than this build.
//
// Nothing coming out of the file is trusted: geometry is validated by
// CardGeometry::fromJson and every object by CardObject::fromJson, so a hostile
// or damaged file cannot produce an unrenderable document.
//
// Writes are atomic (temp file + rename), so a crash or a full disk can never
// leave a half written project behind.
// ---------------------------------------------------------------------------
namespace occ {

class CardDocument;

class ProjectSerializer
{
public:
    static constexpr int kFormatVersion = OCC_PROJECT_FORMAT_VERSION;

    struct SaveResult
    {
        bool    ok = false;
        QString error;             // user facing
        QString technicalDetail;   // for the log, never shown as the only message
    };

    struct LoadResult
    {
        bool        ok = false;
        QString     error;
        QString     technicalDetail;
        QStringList warnings;
        int         sourceFormatVersion = 0;
    };

    // --- saving -------------------------------------------------------------
    struct ExtraMetadata
    {
        QString templateName;          // written to metadata.templateName
        QString templateDescription;   // written to metadata.templateDescription
        bool    asTemplate = false;    // forces metadata.isTemplate
    };

    static SaveResult save(const CardDocument &doc, const QString &path,
                           const ExtraMetadata *extra = nullptr);
    static SaveResult saveToData(const CardDocument &doc, QByteArray *out);

    // --- loading ------------------------------------------------------------
    static LoadResult load(CardDocument *doc, const QString &path);
    static LoadResult loadFromData(CardDocument *doc, const QByteArray &data,
                                   const QString &sourceName = QString());

    // Upgrades an older project.json in place and reports what was changed.
    // Version 0 (a bare project.json with no "formatVersion") becomes 1.
    static LoadResult migrate(QJsonObject *project, int fromVersion);

    // --- print defaults ------------------------------------------------------
    // CardDocument (which is frozen) deliberately knows nothing about printers:
    // a design and a printer are independent concerns. The print settings a
    // project remembers are therefore carried in this small value type. The
    // print dialogs read printSettings() when they open and call
    // setPrintSettings() when the user changes something; the serializer writes
    // the current values into every project and restores them on load, so the
    // "last used printer" survives a save/load cycle exactly as documented in
    // docs/PROJECT_FORMAT.md.
    struct PrintSettings
    {
        QString printer;             // Windows printer name, empty = not chosen
        int     copies = 1;
        bool    duplex = false;
        bool    frontEnabled = true;
        bool    backEnabled = false;
        QString frontTopcoat = QStringLiteral("DriverDefault");
        QString backTopcoat = QStringLiteral("DriverDefault");
        QString hopper;
        QString cardEjectSide;

        QJsonObject toJson() const;
        bool fromJson(const QJsonObject &json);
        void reset();
    };

    static void setPrintSettings(const PrintSettings &settings);
    static PrintSettings printSettings();

    // --- helpers used by AutoSave / TemplateStore / the recents list --------
    // Reads only the metadata block, without building a document. Cheap enough
    // to call for every entry of the recents list.
    struct PeekResult
    {
        bool        ok = false;
        QString     error;
        QString     title;
        QString     description;
        bool        isTemplate = false;
        QStringList placeholders;
        QDateTime   created;
        QDateTime   modified;
        int         formatVersion = 0;
    };
    static PeekResult peek(const QString &path);
    static PeekResult peekData(const QByteArray &data);
};

} // namespace occ
