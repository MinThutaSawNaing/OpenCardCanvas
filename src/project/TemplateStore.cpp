#include "project/TemplateStore.h"

#include "core/CardDocument.h"
#include "project/ProjectSerializer.h"
#include "utils/AppPaths.h"
#include "utils/TextUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace occ {

namespace {

QString templateTr(const char *text)
{
    return QCoreApplication::translate("TemplateStore", text);
}

const char *const kTemplateExtension = ".occard";

QString slugFor(const QString &name)
{
    QString slug = text::sanitiseFileName(name, QStringLiteral("template"));
    slug.replace(QLatin1Char(' '), QLatin1Char('_'));
    slug.replace(QLatin1Char('.'), QLatin1Char('_'));
    if (slug.size() > 100)
        slug = slug.left(100);
    return slug;
}

// Refuses to act on a file outside the templates directory, so a mistake can
// never delete or rename one of the user's projects.
bool isInsideTemplatesDir(const QString &filePath)
{
    const QString canonicalDir =
        QFileInfo(QDir(AppPaths::templatesDir()).absolutePath()).canonicalFilePath();
    if (canonicalDir.isEmpty())
        return false;
    const QFileInfo info(filePath);
    const QString canonicalFile = info.canonicalFilePath();
    if (canonicalFile.isEmpty())
        return false;
    return canonicalFile.startsWith(canonicalDir + QLatin1Char('/'), Qt::CaseInsensitive);
}

} // namespace

QString TemplateStore::filePathForName(const QString &name)
{
    const QString fileName = slugFor(name) + QString::fromLatin1(kTemplateExtension);
    return QDir(AppPaths::templatesDir()).filePath(fileName);
}

bool TemplateStore::exists(const QString &name)
{
    if (name.trimmed().isEmpty())
        return false;
    return QFileInfo::exists(filePathForName(name));
}

QVector<TemplateStore::TemplateInfo> TemplateStore::list(QString *error)
{
    QVector<TemplateInfo> templates;

    QDir directory(AppPaths::templatesDir());
    if (!directory.exists()) {
        // An empty library is not an error: a fresh installation has none yet.
        if (error)
            error->clear();
        return templates;
    }

    const QStringList files = directory.entryList(
        { QStringLiteral("*") + QString::fromLatin1(kTemplateExtension) }, QDir::Files,
        QDir::Name);

    QStringList problems;
    for (const QString &fileName : files) {
        const QString filePath = directory.filePath(fileName);
        const ProjectSerializer::PeekResult peeked = ProjectSerializer::peek(filePath);
        if (!peeked.ok) {
            problems.append(templateTr("%1: %2").arg(fileName, peeked.error));
            continue;
        }

        TemplateInfo info;
        info.filePath = filePath;
        // The name inside project.json is authoritative; the file name is only a
        // fallback for a file that was renamed in Explorer.
        info.name = peeked.title.trimmed();
        if (info.name.isEmpty())
            info.name = QFileInfo(fileName).completeBaseName();
        info.description = peeked.description;
        info.placeholders = peeked.placeholders;
        const QFileInfo fileInfo(filePath);
        info.modified = peeked.modified.isValid() ? peeked.modified
                                                  : fileInfo.lastModified();
        templates.append(info);
    }

    std::stable_sort(templates.begin(), templates.end(),
                     [](const TemplateInfo &a, const TemplateInfo &b) {
                         return text::naturalLessThan(a.name, b.name);
                     });

    if (error) {
        if (problems.isEmpty())
            error->clear();
        else
            *error = templateTr("%1 template file(s) could not be read: %2")
                         .arg(problems.size())
                         .arg(text::joinedForHumans(problems));
    }
    return templates;
}

bool TemplateStore::saveAsTemplate(const CardDocument &doc, const QString &name,
                                   const QString &description, QString *error)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        if (error)
            *error = templateTr("A template needs a name.");
        return false;
    }

    if (!AppPaths::ensureDir(AppPaths::templatesDir())) {
        if (error) {
            *error = templateTr("The templates folder could not be created: %1")
                         .arg(AppPaths::templatesDir());
        }
        return false;
    }

    ProjectSerializer::ExtraMetadata extra;
    extra.templateName = trimmedName;
    extra.templateDescription = description;
    extra.asTemplate = true;

    // A template is an ordinary project file, so saving one uses exactly the same
    // writer - including the atomic temp-file-and-rename step.
    const ProjectSerializer::SaveResult result =
        ProjectSerializer::save(doc, filePathForName(trimmedName), &extra);
    if (!result.ok) {
        if (error)
            *error = result.error;
        return false;
    }
    if (error)
        error->clear();
    return true;
}

bool TemplateStore::load(const QString &filePath, CardDocument *doc, QString *error)
{
    if (!doc) {
        if (error)
            *error = templateTr("The template could not be opened.");
        return false;
    }

    const ProjectSerializer::LoadResult result = ProjectSerializer::load(doc, filePath);
    if (!result.ok) {
        if (error)
            *error = result.error;
        return false;
    }

    // Opening a template must start a NEW document: if the template file stayed as
    // the document's path, the first Save would silently overwrite the template
    // with the personalized card.
    doc->setFilePath(QString());
    doc->setIsTemplate(true);
    doc->setDirty(false);

    if (error)
        error->clear();
    return true;
}

bool TemplateStore::duplicate(const QString &filePath, const QString &newName,
                              QString *error)
{
    if (newName.trimmed().isEmpty()) {
        if (error)
            *error = templateTr("A template needs a name.");
        return false;
    }
    if (QFileInfo::exists(filePathForName(newName))) {
        if (error) {
            *error = templateTr("A template called \"%1\" already exists.")
                         .arg(newName.trimmed());
        }
        return false;
    }

    // Loading and re-saving keeps the copy faithful: objects, assets, geometry and
    // placeholders all travel through the same reader and writer as a normal save.
    CardDocument copy;
    const ProjectSerializer::LoadResult loaded = ProjectSerializer::load(&copy, filePath);
    if (!loaded.ok) {
        if (error)
            *error = loaded.error;
        return false;
    }

    const ProjectSerializer::PeekResult peeked = ProjectSerializer::peek(filePath);
    const QString description = peeked.ok ? peeked.description : QString();
    return saveAsTemplate(copy, newName, description, error);
}

bool TemplateStore::remove(const QString &filePath, QString *error)
{
    if (!QFileInfo::exists(filePath)) {
        if (error)
            *error = templateTr("\"%1\" does not exist.").arg(QFileInfo(filePath).fileName());
        return false;
    }
    if (!isInsideTemplatesDir(filePath)) {
        if (error) {
            *error = templateTr("\"%1\" is not part of the template library, so it was "
                                "not deleted.")
                         .arg(QFileInfo(filePath).fileName());
        }
        return false;
    }
    if (!QFile::remove(filePath)) {
        if (error) {
            *error = templateTr("\"%1\" could not be deleted. It may be open in another "
                                "application.")
                         .arg(QFileInfo(filePath).fileName());
        }
        return false;
    }
    if (error)
        error->clear();
    return true;
}

bool TemplateStore::rename(const QString &filePath, const QString &newName,
                           QString *error)
{
    const QString trimmedName = newName.trimmed();
    if (trimmedName.isEmpty()) {
        if (error)
            *error = templateTr("A template needs a name.");
        return false;
    }
    if (!QFileInfo::exists(filePath)) {
        if (error)
            *error = templateTr("\"%1\" does not exist.").arg(QFileInfo(filePath).fileName());
        return false;
    }
    if (!isInsideTemplatesDir(filePath)) {
        if (error) {
            *error = templateTr("\"%1\" is not part of the template library, so it was "
                                "not renamed.")
                         .arg(QFileInfo(filePath).fileName());
        }
        return false;
    }

    const QString target = filePathForName(trimmedName);
    const bool sameFile =
        QFileInfo(filePath).canonicalFilePath() == QFileInfo(target).canonicalFilePath();
    if (!sameFile && QFileInfo::exists(target)) {
        if (error) {
            *error = templateTr("A template called \"%1\" already exists.")
                         .arg(trimmedName);
        }
        return false;
    }

    CardDocument document;
    const ProjectSerializer::LoadResult loaded = ProjectSerializer::load(&document, filePath);
    if (!loaded.ok) {
        if (error)
            *error = loaded.error;
        return false;
    }
    const ProjectSerializer::PeekResult peeked = ProjectSerializer::peek(filePath);
    const QString description = peeked.ok ? peeked.description : QString();

    if (!saveAsTemplate(document, trimmedName, description, error))
        return false;

    if (sameFile) {
        // The name inside the document changed; the file name is unchanged too,
        // so there is nothing to delete.
        return true;
    }

    // The old file is only removed after the new one exists, so a failure in the
    // middle cannot lose the template.
    if (!QFile::remove(filePath)) {
        if (error) {
            *error = templateTr("The template was saved as \"%1\", but the old file "
                                "\"%2\" could not be deleted.")
                         .arg(trimmedName, QFileInfo(filePath).fileName());
        }
        return false;
    }
    if (error)
        error->clear();
    return true;
}

} // namespace occ
