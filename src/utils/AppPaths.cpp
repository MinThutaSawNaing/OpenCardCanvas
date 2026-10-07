#include "utils/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace occ {

namespace {
QString g_appDirOverride;
}

QString AppPaths::dataDir()
{
    return QDir::toNativeSeparators(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
}

QString AppPaths::configDir()
{
    return QDir::toNativeSeparators(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
}

QString AppPaths::logsDir()      { return dataDir() + QDir::separator() + QStringLiteral("logs"); }
QString AppPaths::autosaveDir()  { return dataDir() + QDir::separator() + QStringLiteral("autosave"); }
QString AppPaths::crashDir()     { return dataDir() + QDir::separator() + QStringLiteral("session"); }
QString AppPaths::templatesDir() { return configDir() + QDir::separator() + QStringLiteral("templates"); }

QString AppPaths::configFilePath()
{
    return configDir() + QDir::separator() + QStringLiteral("OpenCardCanvas.ini");
}

QString AppPaths::recentProjectsFilePath()
{
    return configDir() + QDir::separator() + QStringLiteral("recent-projects.json");
}

QString AppPaths::defaultExportsDir()
{
    const QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty())
        return QDir::toNativeSeparators(QDir::homePath());
    return QDir::toNativeSeparators(documents + QDir::separator() + QStringLiteral("OpenCardCanvas"));
}

bool AppPaths::ensureDir(const QString &path)
{
    if (path.isEmpty())
        return false;
    QDir dir(path);
    if (dir.exists())
        return true;
    return dir.mkpath(QStringLiteral("."));
}

bool AppPaths::ensureAllDirectories(QString *error)
{
    const QStringList required = {
        dataDir(), configDir(), logsDir(), autosaveDir(), crashDir(), templatesDir()
    };
    for (const QString &path : required) {
        if (!ensureDir(path)) {
            if (error) {
                //: %1 is a directory path the application could not create.
                *error = QCoreApplication::translate(
                             "AppPaths", "The application folder could not be created:\n%1\n\n"
                                         "Check that you have write permission for your user profile.")
                             .arg(QDir::toNativeSeparators(path));
            }
            return false;
        }
    }
    return true;
}

QString AppPaths::applicationDir()
{
    if (!g_appDirOverride.isEmpty())
        return g_appDirOverride;
    return QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
}

void AppPaths::setApplicationDirOverride(const QString &dir)
{
    g_appDirOverride = dir;
}

} // namespace occ
