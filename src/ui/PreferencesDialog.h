#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QRadioButton;
class QSpinBox;
class QStackedWidget;

// ---------------------------------------------------------------------------
// PreferencesDialog - every persisted setting, in one place, grouped by what
// the setting is about rather than by which code reads it.
//
// Pages: General, Canvas, New card defaults, Auto-save, Export, Printing,
// Advanced.
//
// The settings object is written on Apply/OK only, and changed() is emitted
// once per apply, so a dialog the user cancels leaves the application exactly
// as it was. "Reset to defaults" clears the stored values and reloads the page.
//
// The Printing page talks to PrinterManager, so the default printer can only be
// chosen from printers that actually exist on this machine.
// ---------------------------------------------------------------------------
namespace occ {

class PrinterManager;

class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(PrinterManager *printers, QWidget *parent = nullptr);

signals:
    // Emitted after settings were written, so open views can update at once.
    void changed();

private:
    void buildUi();
    QWidget *buildGeneralPage();
    QWidget *buildCanvasPage();
    QWidget *buildNewCardPage();
    QWidget *buildAutoSavePage();
    QWidget *buildExportPage();
    QWidget *buildPrintingPage();
    QWidget *buildAdvancedPage();

    void loadFromSettings();
    void applyChanges();
    void resetToDefaults();

    PrinterManager *m_printers = nullptr;

    QListWidget    *m_pages = nullptr;
    QStackedWidget *m_stack = nullptr;

    // General
    QComboBox *m_unitSystem = nullptr;
    QCheckBox *m_verbose = nullptr;

    // Canvas
    QCheckBox      *m_showRulers = nullptr;
    QCheckBox      *m_showGuides = nullptr;
    QCheckBox      *m_showGrid = nullptr;
    QDoubleSpinBox *m_gridSpacing = nullptr;
    QCheckBox      *m_snapCard = nullptr;
    QCheckBox      *m_snapGuides = nullptr;
    QCheckBox      *m_snapObjects = nullptr;
    QCheckBox      *m_snapGrid = nullptr;
    QDoubleSpinBox *m_snapTolerance = nullptr;

    // New card defaults
    QComboBox      *m_defaultPreset = nullptr;
    QSpinBox       *m_defaultDpi = nullptr;
    QDoubleSpinBox *m_defaultBleed = nullptr;

    // Auto-save
    QCheckBox *m_autoSaveEnabled = nullptr;
    QSpinBox  *m_autoSaveInterval = nullptr;

    // Export
    QSpinBox       *m_exportDpi = nullptr;
    QComboBox      *m_exportFormat = nullptr;
    QLineEdit      *m_exportDirectory = nullptr;

    // Printing
    QComboBox *m_defaultPrinter = nullptr;
    QCheckBox *m_confirmBeforePrint = nullptr;
    QLabel    *m_printerHint = nullptr;

    // Advanced
    QLabel    *m_pathsLabel = nullptr;
};

} // namespace occ
