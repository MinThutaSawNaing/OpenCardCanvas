#include "utils/Settings.h"

#include "utils/AppPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace occ {

namespace {

constexpr int    kMaxRecentProjects      = 12;
constexpr double kDefaultGridSpacingMm   = 5.0;
constexpr double kDefaultSnapTolerancePx = 8.0;
constexpr int    kDefaultDpi             = 300;
constexpr int    kDefaultAutoSaveMinutes = 3;

// One QSettings instance for the whole run. Qt caches the file internally, and
// keeping a single object guarantees that a value written by one panel is
// immediately visible to the next.
QSettings *settingsFor()
{
    static QSettings *s = nullptr;
    if (!s) {
        AppPaths::ensureDir(AppPaths::configDir());
        // INI files are UTF-8 in Qt 6, so no explicit codec is needed.
        s = new QSettings(AppPaths::configFilePath(), QSettings::IniFormat);
    }
    return s;
}

QString unitKey(AppSettings::UnitSystem u)
{
    return u == AppSettings::UnitSystem::Inches ? QStringLiteral("in") : QStringLiteral("mm");
}

AppSettings::UnitSystem unitFromKey(const QString &k)
{
    return k == QStringLiteral("in") ? AppSettings::UnitSystem::Inches
                                     : AppSettings::UnitSystem::Millimeters;
}

} // namespace

AppSettings &AppSettings::instance()
{
    static AppSettings s;
    return s;
}

AppSettings::AppSettings(QObject *parent) : QObject(parent)
{
    m_verbose = verboseLogging();
}

AppSettings::~AppSettings() = default;

void AppSettings::sync()
{
    settingsFor()->sync();
}

void AppSettings::resetToDefaults()
{
    QSettings *s = settingsFor();
    s->clear();
    s->sync();
    m_verbose = false;
    emit changed();
}

// --- Display units ----------------------------------------------------------

AppSettings::UnitSystem AppSettings::unitSystem() const
{
    return unitFromKey(
        settingsFor()->value(QStringLiteral("ui/units"), QStringLiteral("mm")).toString());
}

void AppSettings::setUnitSystem(UnitSystem unit)
{
    settingsFor()->setValue(QStringLiteral("ui/units"), unitKey(unit));
    emit changed();
}

// --- Canvas aids ------------------------------------------------------------

bool AppSettings::showRulers() const
{ return settingsFor()->value(QStringLiteral("canvas/rulers"), true).toBool(); }
void AppSettings::setShowRulers(bool on)
{ settingsFor()->setValue(QStringLiteral("canvas/rulers"), on); emit changed(); }

bool AppSettings::showGuides() const
{ return settingsFor()->value(QStringLiteral("canvas/guides"), true).toBool(); }
void AppSettings::setShowGuides(bool on)
{ settingsFor()->setValue(QStringLiteral("canvas/guides"), on); emit changed(); }

bool AppSettings::showGrid() const
{ return settingsFor()->value(QStringLiteral("canvas/grid"), false).toBool(); }
void AppSettings::setShowGrid(bool on)
{ settingsFor()->setValue(QStringLiteral("canvas/grid"), on); emit changed(); }

double AppSettings::gridSpacingMm() const
{
    return settingsFor()->value(QStringLiteral("canvas/gridSpacingMm"), kDefaultGridSpacingMm)
        .toDouble();
}
void AppSettings::setGridSpacingMm(double mm)
{ settingsFor()->setValue(QStringLiteral("canvas/gridSpacingMm"), mm); emit changed(); }

bool AppSettings::snapToGrid() const
{ return settingsFor()->value(QStringLiteral("canvas/snapGrid"), false).toBool(); }
void AppSettings::setSnapToGrid(bool on)
{ settingsFor()->setValue(QStringLiteral("canvas/snapGrid"), on); emit changed(); }

bool AppSettings::snapToGuides() const
{ return settingsFor()->value(QStringLiteral("canvas/snapGuides"), true).toBool(); }
void AppSettings::setSnapToGuides(bool on)
{ settingsFor()->setValue(QStringLiteral("canvas/snapGuides"), on); emit changed(); }

bool AppSettings::snapToObjects() const
{ return settingsFor()->value(QStringLiteral("canvas/snapObjects"), true).toBool(); }
void AppSettings::setSnapToObjects(bool on)
{ settingsFor()->setValue(QStringLiteral("canvas/snapObjects"), on); emit changed(); }

bool AppSettings::snapToCard() const
{ return settingsFor()->value(QStringLiteral("canvas/snapCard"), true).toBool(); }
void AppSettings::setSnapToCard(bool on)
{ settingsFor()->setValue(QStringLiteral("canvas/snapCard"), on); emit changed(); }

double AppSettings::snapTolerancePx() const
{
    return settingsFor()->value(QStringLiteral("canvas/snapTolerance"), kDefaultSnapTolerancePx)
        .toDouble();
}
void AppSettings::setSnapTolerancePx(double px)
{ settingsFor()->setValue(QStringLiteral("canvas/snapTolerance"), px); emit changed(); }

// --- New document defaults --------------------------------------------------

QString AppSettings::defaultCardPreset() const
{
    return settingsFor()->value(QStringLiteral("new/preset"), QStringLiteral("iso-id1")).toString();
}
void AppSettings::setDefaultCardPreset(const QString &id)
{ settingsFor()->setValue(QStringLiteral("new/preset"), id); emit changed(); }

int AppSettings::defaultRenderDpi() const
{ return settingsFor()->value(QStringLiteral("new/dpi"), kDefaultDpi).toInt(); }
void AppSettings::setDefaultRenderDpi(int dpi)
{ settingsFor()->setValue(QStringLiteral("new/dpi"), dpi); emit changed(); }

double AppSettings::defaultBleedMm() const
{ return settingsFor()->value(QStringLiteral("new/bleedMm"), 0.0).toDouble(); }
void AppSettings::setDefaultBleedMm(double mm)
{ settingsFor()->setValue(QStringLiteral("new/bleedMm"), mm); emit changed(); }

// --- Autosave and recovery --------------------------------------------------

bool AppSettings::autoSaveEnabled() const
{ return settingsFor()->value(QStringLiteral("autosave/enabled"), true).toBool(); }
void AppSettings::setAutoSaveEnabled(bool on)
{ settingsFor()->setValue(QStringLiteral("autosave/enabled"), on); emit changed(); }

int AppSettings::autoSaveIntervalMinutes() const
{
    return settingsFor()->value(QStringLiteral("autosave/minutes"), kDefaultAutoSaveMinutes)
        .toInt();
}
void AppSettings::setAutoSaveIntervalMinutes(int minutes)
{ settingsFor()->setValue(QStringLiteral("autosave/minutes"), qBound(1, minutes, 60)); emit changed(); }

// --- Export -----------------------------------------------------------------

int AppSettings::exportDpi() const
{ return settingsFor()->value(QStringLiteral("export/dpi"), kDefaultDpi).toInt(); }
void AppSettings::setExportDpi(int dpi)
{ settingsFor()->setValue(QStringLiteral("export/dpi"), qBound(72, dpi, 1200)); emit changed(); }

QString AppSettings::exportDirectory() const
{
    return settingsFor()->value(QStringLiteral("export/dir"), AppPaths::defaultExportsDir())
        .toString();
}
void AppSettings::setExportDirectory(const QString &dir)
{ settingsFor()->setValue(QStringLiteral("export/dir"), dir); emit changed(); }

QString AppSettings::exportFormat() const
{
    return settingsFor()->value(QStringLiteral("export/format"), QStringLiteral("png")).toString();
}
void AppSettings::setExportFormat(const QString &format)
{ settingsFor()->setValue(QStringLiteral("export/format"), format); emit changed(); }

// --- Printing ---------------------------------------------------------------

QString AppSettings::defaultPrinter() const
{ return settingsFor()->value(QStringLiteral("print/printer")).toString(); }
void AppSettings::setDefaultPrinter(const QString &name)
{ settingsFor()->setValue(QStringLiteral("print/printer"), name); emit changed(); }

bool AppSettings::confirmBeforePrint() const
{ return settingsFor()->value(QStringLiteral("print/confirm"), true).toBool(); }
void AppSettings::setConfirmBeforePrint(bool on)
{ settingsFor()->setValue(QStringLiteral("print/confirm"), on); emit changed(); }

// --- Recent projects --------------------------------------------------------

QString AppSettings::lastOpenDirectory() const
{ return settingsFor()->value(QStringLiteral("paths/lastOpen")).toString(); }
void AppSettings::setLastOpenDirectory(const QString &dir)
{ settingsFor()->setValue(QStringLiteral("paths/lastOpen"), dir); }

QStringList AppSettings::recentProjects() const
{ return settingsFor()->value(QStringLiteral("paths/recent")).toStringList(); }

void AppSettings::addRecentProject(const QString &filePath)
{
    if (filePath.isEmpty())
        return;
    const QString normalized = QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath());
    QStringList list = recentProjects();
    list.removeAll(normalized);
    list.prepend(normalized);
    while (list.size() > kMaxRecentProjects)
        list.removeLast();
    settingsFor()->setValue(QStringLiteral("paths/recent"), list);
    emit changed();
}

void AppSettings::removeRecentProject(const QString &filePath)
{
    QStringList list = recentProjects();
    if (list.removeAll(QDir::toNativeSeparators(filePath)) > 0) {
        settingsFor()->setValue(QStringLiteral("paths/recent"), list);
        emit changed();
    }
}

void AppSettings::clearRecentProjects()
{
    settingsFor()->remove(QStringLiteral("paths/recent"));
    emit changed();
}

// --- Window state -----------------------------------------------------------

QByteArray AppSettings::windowGeometry() const
{ return settingsFor()->value(QStringLiteral("ui/geometry")).toByteArray(); }
void AppSettings::setWindowGeometry(const QByteArray &state)
{ settingsFor()->setValue(QStringLiteral("ui/geometry"), state); }

QByteArray AppSettings::windowState() const
{ return settingsFor()->value(QStringLiteral("ui/windowState")).toByteArray(); }
void AppSettings::setWindowState(const QByteArray &state)
{ settingsFor()->setValue(QStringLiteral("ui/windowState"), state); }

// --- Diagnostics ------------------------------------------------------------

bool AppSettings::verboseLogging() const
{ return settingsFor()->value(QStringLiteral("diag/verbose"), false).toBool(); }
void AppSettings::setVerboseLogging(bool on)
{ settingsFor()->setValue(QStringLiteral("diag/verbose"), on); m_verbose = on; emit changed(); }

} // namespace occ
