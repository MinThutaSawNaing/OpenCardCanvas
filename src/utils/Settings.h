#pragma once

#include <QObject>
#include <QStringList>

// ---------------------------------------------------------------------------
// AppSettings - persisted application preferences.
//
// Backed by %APPDATA%/OpenCardCanvas/OpenCardCanvas.ini so that settings are
// human readable, easy to support and never hidden in the registry.
//
// Live change notification is provided through changed() so open editors can
// react immediately (e.g. switching the display unit updates the rulers).
// ---------------------------------------------------------------------------
namespace occ {

class AppSettings : public QObject
{
    Q_OBJECT
public:
    enum class UnitSystem { Millimeters, Inches };

    static AppSettings &instance();

    void sync();
    void resetToDefaults();

    // --- Display units -----------------------------------------------------
    UnitSystem unitSystem() const;
    void setUnitSystem(UnitSystem unit);

    // --- Canvas aids -------------------------------------------------------
    bool showRulers() const;          void setShowRulers(bool on);
    bool showGuides() const;          void setShowGuides(bool on);
    bool showGrid() const;            void setShowGrid(bool on);
    double gridSpacingMm() const;     void setGridSpacingMm(double mm);
    bool snapToGrid() const;          void setSnapToGrid(bool on);
    bool snapToGuides() const;        void setSnapToGuides(bool on);
    bool snapToObjects() const;       void setSnapToObjects(bool on);
    bool snapToCard() const;          void setSnapToCard(bool on);
    double snapTolerancePx() const;   void setSnapTolerancePx(double px);

    // --- New document defaults --------------------------------------------
    QString defaultCardPreset() const;    void setDefaultCardPreset(const QString &id);
    int defaultRenderDpi() const;         void setDefaultRenderDpi(int dpi);
    double defaultBleedMm() const;        void setDefaultBleedMm(double mm);

    // --- Autosave / recovery ----------------------------------------------
    bool autoSaveEnabled() const;         void setAutoSaveEnabled(bool on);
    int autoSaveIntervalMinutes() const;  void setAutoSaveIntervalMinutes(int minutes);

    // --- Export ------------------------------------------------------------
    int exportDpi() const;                void setExportDpi(int dpi);
    QString exportDirectory() const;      void setExportDirectory(const QString &dir);
    QString exportFormat() const;         void setExportFormat(const QString &format);

    // --- Printing ----------------------------------------------------------
    QString defaultPrinter() const;       void setDefaultPrinter(const QString &name);
    bool confirmBeforePrint() const;      void setConfirmBeforePrint(bool on);

    // --- Paths / history ---------------------------------------------------
    QString lastOpenDirectory() const;    void setLastOpenDirectory(const QString &dir);
    QStringList recentProjects() const;
    void addRecentProject(const QString &filePath);
    void clearRecentProjects();
    void removeRecentProject(const QString &filePath);

    // --- Session (main window geometry) ------------------------------------
    QByteArray windowGeometry() const;    void setWindowGeometry(const QByteArray &state);
    QByteArray windowState() const;       void setWindowState(const QByteArray &state);

    // --- Diagnostics -------------------------------------------------------
    bool verboseLogging() const;          void setVerboseLogging(bool on);

signals:
    void changed();

private:
    explicit AppSettings(QObject *parent = nullptr);
    ~AppSettings() override;
    AppSettings(const AppSettings &) = delete;
    AppSettings &operator=(const AppSettings &) = delete;

    bool m_verbose = false;
};

} // namespace occ
