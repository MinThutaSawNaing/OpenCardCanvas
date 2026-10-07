#pragma once

#include "core/CardTypes.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

// ---------------------------------------------------------------------------
// TemplateStore - the user's template library.
//
// A template is not a separate file format: it is an ordinary .occard project
// with isTemplate = true and a name plus a description in its metadata block.
// Keeping one format means a template can be opened, edited and saved with the
// normal project commands, and the templates directory is also a perfectly
// valid backup of the designs it holds.
//
// Templates live in AppPaths::templatesDir() as <slug>.occard. The file name is
// derived from the template name but is never trusted as the identity - the
// name inside project.json is authoritative, so renaming a file in Explorer
// cannot corrupt the library listing.
// ---------------------------------------------------------------------------
namespace occ {

class CardDocument;

class TemplateStore
{
public:
    struct TemplateInfo
    {
        QString     filePath;
        QString     name;
        QString     description;
        QStringList placeholders;
        QDateTime   modified;
    };

    // Lists every usable template, sorted by name (natural order). Files that
    // cannot be read are skipped and reported through `error`; an empty
    // directory is not an error.
    static QVector<TemplateInfo> list(QString *error = nullptr);

    // Writes `doc` as a template. `name` and `description` end up in the
    // metadata block. An existing template with the same name is replaced.
    static bool saveAsTemplate(const CardDocument &doc, const QString &name,
                               const QString &description, QString *error = nullptr);

    static bool load(const QString &filePath, CardDocument *doc, QString *error = nullptr);
    static bool duplicate(const QString &filePath, const QString &newName,
                          QString *error = nullptr);
    static bool remove(const QString &filePath, QString *error = nullptr);
    static bool rename(const QString &filePath, const QString &newName,
                       QString *error = nullptr);

    // Path a template with `name` would occupy. Exposed so the UI can ask
    // "replace this template?" before the fact.
    static QString filePathForName(const QString &name);
    // True when a template with that name already exists.
    static bool exists(const QString &name);
};

} // namespace occ
