#pragma once

#include <QString>

// ---------------------------------------------------------------------------
// AppPaths - every directory OpenCardCanvas writes to, in one place.
//
// Nothing is ever written next to the executable (Program Files is normally
// read-only for standard users). All persistent state lives under the current
// user's profile, which also keeps the application fully portable-friendly:
// copying the install directory is enough because no per-machine state is
// stored there.
// ---------------------------------------------------------------------------
namespace occ {

class AppPaths
{
public:
    // %LOCALAPPDATA%/OpenCardCanvas            (cache, logs, autosave)
    static QString dataDir();
    // %APPDATA%/OpenCardCanvas                 (settings, templates, recents)
    static QString configDir();
    static QString logsDir();
    static QString autosaveDir();
    static QString crashDir();
    static QString templatesDir();
    static QString configFilePath();            // configDir()/OpenCardCanvas.ini
    static QString recentProjectsFilePath();
    static QString defaultExportsDir();

    // Creates a directory (and its parents) if missing.
    static bool ensureDir(const QString &path);
    // Creates all application directories. Returns false if a required
    // directory could not be created; `error` receives a user-facing reason.
    static bool ensureAllDirectories(QString *error = nullptr);

    // Directory of the running executable (used for docs/help lookups only).
    static QString applicationDir();

    static void setApplicationDirOverride(const QString &dir);
};

} // namespace occ
