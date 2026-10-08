#include "ui/PreferencesDialog.h"

#include "core/CardGeometry.h"
#include "printing/PrinterManager.h"
#include "ui/IconFactory.h"
#include "utils/AppPaths.h"
#include "utils/Logger.h"
#include "utils/Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace occ {

PreferencesDialog::PreferencesDialog(PrinterManager *printers, QWidget *parent)
    : QDialog(parent),
      m_printers(printers)
{
    setWindowTitle(tr("Preferences"));
    setWindowIcon(IconFactory::icon(QStringLiteral("logo")));
    setModal(true);
    buildUi();
    loadFromSettings();
}

void PreferencesDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(8);

    auto *columns = new QHBoxLayout();
    columns->setSpacing(10);

    m_pages = new QListWidget(this);
    m_pages->setMinimumWidth(150);
    m_pages->setMaximumWidth(220);
    columns->addWidget(m_pages);

    m_stack = new QStackedWidget(this);
    m_stack->setMinimumWidth(420);
    columns->addWidget(m_stack, 1);

    const struct
    {
        const char *title;
        QWidget *page;
    } pages[] = {
        { QT_TRANSLATE_NOOP("occ::PreferencesDialog", "General"), buildGeneralPage() },
        { QT_TRANSLATE_NOOP("occ::PreferencesDialog", "Canvas"), buildCanvasPage() },
        { QT_TRANSLATE_NOOP("occ::PreferencesDialog", "New card defaults"),
          buildNewCardPage() },
        { QT_TRANSLATE_NOOP("occ::PreferencesDialog", "Auto-save"), buildAutoSavePage() },
        { QT_TRANSLATE_NOOP("occ::PreferencesDialog", "Export"), buildExportPage() },
        { QT_TRANSLATE_NOOP("occ::PreferencesDialog", "Printing"), buildPrintingPage() },
        { QT_TRANSLATE_NOOP("occ::PreferencesDialog", "Advanced"), buildAdvancedPage() },
    };
    for (const auto &page : pages) {
        auto *item = new QListWidgetItem(tr(page.title), m_pages);
        item->setSizeHint(QSize(0, 26));
        m_stack->addWidget(page.page);
    }
    connect(m_pages, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    m_pages->setCurrentRow(0);

    outer->addLayout(columns);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                         | QDialogButtonBox::Apply,
                                         this);
    outer->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked, this, [this] {
        applyChanges();
        accept();
    });
    connect(buttons->button(QDialogButtonBox::Cancel), &QPushButton::clicked,
            this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
        applyChanges();
        loadFromSettings();
    });
}

QWidget *PreferencesDialog::buildGeneralPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *group = new QGroupBox(tr("Display"), page);
    auto *form = new QFormLayout(group);

    m_unitSystem = new QComboBox(group);
    m_unitSystem->addItem(tr("Millimetres (mm)"), int(AppSettings::UnitSystem::Millimeters));
    m_unitSystem->addItem(tr("Inches (in)"), int(AppSettings::UnitSystem::Inches));
    m_unitSystem->setToolTip(tr("Unit used by the rulers, the property panel and every "
                                "dialog. The document itself is always stored in "
                                "millimetres."));
    form->addRow(tr("Unit system"), m_unitSystem);

    layout->addWidget(group);
    layout->addStretch(1);
    return page;
}

QWidget *PreferencesDialog::buildCanvasPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *aidsGroup = new QGroupBox(tr("Aids"), page);
    auto *aidsLayout = new QVBoxLayout(aidsGroup);
    m_showRulers = new QCheckBox(tr("Show the rulers"), aidsGroup);
    m_showGuides = new QCheckBox(tr("Show guides"), aidsGroup);
    m_showGrid = new QCheckBox(tr("Show the grid"), aidsGroup);
    aidsLayout->addWidget(m_showRulers);
    aidsLayout->addWidget(m_showGuides);
    aidsLayout->addWidget(m_showGrid);

    auto *gridForm = new QFormLayout();
    m_gridSpacing = new QDoubleSpinBox(aidsGroup);
    m_gridSpacing->setRange(0.5, 50.0);
    m_gridSpacing->setDecimals(2);
    m_gridSpacing->setSingleStep(0.5);
    m_gridSpacing->setSuffix(tr(" mm"));
    gridForm->addRow(tr("Grid spacing"), m_gridSpacing);
    aidsLayout->addLayout(gridForm);
    layout->addWidget(aidsGroup);

    auto *snapGroup = new QGroupBox(tr("Snapping"), page);
    auto *snapLayout = new QVBoxLayout(snapGroup);
    m_snapCard = new QCheckBox(tr("Snap to the card edges and centre"), snapGroup);
    m_snapGuides = new QCheckBox(tr("Snap to guides"), snapGroup);
    m_snapObjects = new QCheckBox(tr("Snap to other objects"), snapGroup);
    m_snapGrid = new QCheckBox(tr("Snap to the grid"), snapGroup);
    snapLayout->addWidget(m_snapCard);
    snapLayout->addWidget(m_snapGuides);
    snapLayout->addWidget(m_snapObjects);
    snapLayout->addWidget(m_snapGrid);

    auto *snapForm = new QFormLayout();
    m_snapTolerance = new QDoubleSpinBox(snapGroup);
    m_snapTolerance->setRange(1.0, 40.0);
    m_snapTolerance->setDecimals(0);
    m_snapTolerance->setSingleStep(1.0);
    m_snapTolerance->setSuffix(tr(" px"));
    m_snapTolerance->setToolTip(tr("How close an edge has to be before it snaps. Measured "
                                   "in screen pixels, so it feels the same at every zoom."));
    snapForm->addRow(tr("Tolerance"), m_snapTolerance);
    snapLayout->addLayout(snapForm);

    layout->addWidget(snapGroup);
    layout->addStretch(1);
    return page;
}

QWidget *PreferencesDialog::buildNewCardPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *group = new QGroupBox(tr("Defaults for a new card"), page);
    auto *form = new QFormLayout(group);

    m_defaultPreset = new QComboBox(group);
    const QVector<CardPreset> presets = CardGeometry::presets();
    for (const CardPreset &preset : presets) {
        m_defaultPreset->addItem(tr("%1 (%2 x %3 mm)")
                                     .arg(preset.name,
                                          QString::number(preset.widthMm, 'f', 2),
                                          QString::number(preset.heightMm, 'f', 2)),
                                 preset.id);
    }
    form->addRow(tr("Card preset"), m_defaultPreset);

    m_defaultDpi = new QSpinBox(group);
    m_defaultDpi->setRange(kMinDpi, kMaxDpi);
    m_defaultDpi->setSingleStep(50);
    m_defaultDpi->setSuffix(tr(" dpi"));
    form->addRow(tr("Render resolution"), m_defaultDpi);

    m_defaultBleed = new QDoubleSpinBox(group);
    m_defaultBleed->setRange(0.0, 10.0);
    m_defaultBleed->setDecimals(2);
    m_defaultBleed->setSingleStep(0.5);
    m_defaultBleed->setSuffix(tr(" mm"));
    form->addRow(tr("Bleed"), m_defaultBleed);

    layout->addWidget(group);
    layout->addStretch(1);
    return page;
}

QWidget *PreferencesDialog::buildAutoSavePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *group = new QGroupBox(tr("Auto-save and crash recovery"), page);
    auto *form = new QFormLayout(group);

    m_autoSaveEnabled = new QCheckBox(tr("Save a recovery copy while I work"), group);
    m_autoSaveEnabled->setToolTip(tr("Recovery copies never touch your project file; they are "
                                     "written to the recovery folder and offered back after "
                                     "an unexpected shutdown."));
    form->addRow(m_autoSaveEnabled);

    m_autoSaveInterval = new QSpinBox(group);
    m_autoSaveInterval->setRange(1, 120);
    m_autoSaveInterval->setSuffix(tr(" min"));
    form->addRow(tr("Interval"), m_autoSaveInterval);

    auto *hint = new QLabel(tr("Recovery copies are kept for 30 days and then removed on "
                               "start-up."), group);
    hint->setWordWrap(true);
    form->addRow(hint);

    layout->addWidget(group);
    layout->addStretch(1);
    return page;
}

QWidget *PreferencesDialog::buildExportPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *group = new QGroupBox(tr("Export defaults"), page);
    auto *form = new QFormLayout(group);

    m_exportDpi = new QSpinBox(group);
    m_exportDpi->setRange(kMinDpi, kMaxDpi);
    m_exportDpi->setSingleStep(50);
    m_exportDpi->setSuffix(tr(" dpi"));
    m_exportDpi->setToolTip(tr("300 dpi matches the card printer's colour resolution; use 600 "
                               "dpi only when you really need it."));
    form->addRow(tr("Resolution"), m_exportDpi);

    m_exportFormat = new QComboBox(group);
    m_exportFormat->addItem(tr("PNG (lossless)"), QStringLiteral("png"));
    m_exportFormat->addItem(tr("PDF (physical size)"), QStringLiteral("pdf"));
    m_exportFormat->addItem(tr("JPEG (smaller files)"), QStringLiteral("jpeg"));
    form->addRow(tr("Format"), m_exportFormat);

    auto *directoryRow = new QHBoxLayout();
    m_exportDirectory = new QLineEdit(group);
    m_exportDirectory->setPlaceholderText(AppPaths::defaultExportsDir());
    auto *browse = new QPushButton(tr("Browse..."), group);
    directoryRow->addWidget(m_exportDirectory, 1);
    directoryRow->addWidget(browse);
    form->addRow(tr("Directory"), directoryRow);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString start = m_exportDirectory->text().isEmpty()
                                  ? AppPaths::defaultExportsDir()
                                  : m_exportDirectory->text();
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Export directory"), start);
        if (!dir.isEmpty())
            m_exportDirectory->setText(dir);
    });

    auto *exportHint = new QLabel(tr("Leaving the directory empty asks for a location every "
                                     "time."), group);
    exportHint->setWordWrap(true);
    form->addRow(exportHint);

    layout->addWidget(group);
    layout->addStretch(1);
    return page;
}

QWidget *PreferencesDialog::buildPrintingPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *group = new QGroupBox(tr("Printing"), page);
    auto *form = new QFormLayout(group);

    m_defaultPrinter = new QComboBox(group);
    // Only printers this machine actually has: a default printer that does not
    // exist is worse than no default at all.
    if (m_printers) {
        const QVector<PrinterIdentity> printers = m_printers->printers();
        for (const PrinterIdentity &identity : printers)
            m_defaultPrinter->addItem(identity.name, identity.name);
        if (printers.isEmpty()) {
            m_defaultPrinter->addItem(tr("OpenCardCanvas Simulator"), QString());
            m_defaultPrinter->setEnabled(false);
        }
    }
    form->addRow(tr("Default printer"), m_defaultPrinter);

    m_confirmBeforePrint = new QCheckBox(tr("Show the preview and ask before printing"), group);
    m_confirmBeforePrint->setToolTip(tr("Strongly recommended: a card printer consumes a card "
                                        "and a length of ribbon per job."));
    form->addRow(m_confirmBeforePrint);

    m_printerHint = new QLabel(group);
    m_printerHint->setWordWrap(true);
    form->addRow(m_printerHint);

    layout->addWidget(group);
    layout->addStretch(1);
    return page;
}

QWidget *PreferencesDialog::buildAdvancedPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *group = new QGroupBox(tr("Diagnostics"), page);
    auto *groupLayout = new QVBoxLayout(group);

    m_verbose = new QCheckBox(tr("Write detailed diagnostics to the log file"), group);
    m_verbose->setToolTip(tr("Takes effect for messages written from now on."));
    groupLayout->addWidget(m_verbose);

    m_pathsLabel = new QLabel(group);
    m_pathsLabel->setWordWrap(true);
    m_pathsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    groupLayout->addWidget(m_pathsLabel);

    auto *buttonRow = new QHBoxLayout();
    auto *openLogs = new QPushButton(tr("Open log folder"), group);
    auto *openRecovery = new QPushButton(tr("Open recovery folder"), group);
    buttonRow->addWidget(openLogs);
    buttonRow->addWidget(openRecovery);
    buttonRow->addStretch(1);
    groupLayout->addLayout(buttonRow);
    connect(openLogs, &QPushButton::clicked, this, [this] {
        const QString dir = AppPaths::logsDir();
        if (dir.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(dir)))
            QMessageBox::information(this, tr("Log folder"), tr("The log folder is:\n%1").arg(dir));
    });
    connect(openRecovery, &QPushButton::clicked, this, [this] {
        const QString dir = AppPaths::autosaveDir();
        if (dir.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
            QMessageBox::information(this, tr("Recovery folder"),
                                     tr("The recovery folder is:\n%1").arg(dir));
        }
    });

    auto *resetRow = new QHBoxLayout();
    auto *reset = new QPushButton(tr("Reset all preferences to defaults"), group);
    resetRow->addWidget(reset);
    resetRow->addStretch(1);
    groupLayout->addLayout(resetRow);
    connect(reset, &QPushButton::clicked, this, [this] {
        const auto answer = QMessageBox::question(
            this, tr("Reset preferences"),
            tr("Reset every preference to its default value?\n\nThis does not touch your "
               "templates, recent projects or any card design."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes)
            resetToDefaults();
    });

    layout->addWidget(group);
    layout->addStretch(1);
    return page;
}

void PreferencesDialog::loadFromSettings()
{
    AppSettings &settings = AppSettings::instance();

    // General
    const int unitIndex = m_unitSystem->findData(int(settings.unitSystem()));
    m_unitSystem->setCurrentIndex(unitIndex < 0 ? 0 : unitIndex);

    // Canvas
    m_showRulers->setChecked(settings.showRulers());
    m_showGuides->setChecked(settings.showGuides());
    m_showGrid->setChecked(settings.showGrid());
    m_gridSpacing->setValue(settings.gridSpacingMm());
    m_snapTolerance->setValue(settings.snapTolerancePx());
    m_snapCard->setChecked(settings.snapToCard());
    m_snapGuides->setChecked(settings.snapToGuides());
    m_snapObjects->setChecked(settings.snapToObjects());
    m_snapGrid->setChecked(settings.snapToGrid());

    // New card defaults
    const int presetIndex = m_defaultPreset->findData(settings.defaultCardPreset());
    m_defaultPreset->setCurrentIndex(presetIndex < 0 ? 0 : presetIndex);
    m_defaultDpi->setValue(settings.defaultRenderDpi());
    m_defaultBleed->setValue(settings.defaultBleedMm());

    // Auto-save
    m_autoSaveEnabled->setChecked(settings.autoSaveEnabled());
    m_autoSaveInterval->setValue(settings.autoSaveIntervalMinutes());

    // Export
    m_exportDpi->setValue(settings.exportDpi());
    const int formatIndex = m_exportFormat->findData(settings.exportFormat());
    m_exportFormat->setCurrentIndex(formatIndex < 0 ? 0 : formatIndex);
    m_exportDirectory->setText(settings.exportDirectory());

    // Printing
    const int printerIndex = m_defaultPrinter->findData(settings.defaultPrinter());
    if (printerIndex >= 0)
        m_defaultPrinter->setCurrentIndex(printerIndex);
    m_confirmBeforePrint->setChecked(settings.confirmBeforePrint());
    if (m_printers && m_printers->printers().isEmpty()) {
        m_printerHint->setText(tr("No card printer was found on this computer. The simulator "
                                  "renders cards to a file so the workflow can still be "
                                  "tested; it does not print."));
    } else {
        m_printerHint->setText(tr("Card printers are detected by asking the driver, never by "
                                  "the printer's name."));
    }

    // Advanced
    m_verbose->setChecked(settings.verboseLogging());
    m_pathsLabel->setText(tr("Settings: %1\nLogs: %2\nRecovery: %3\nTemplates: %4")
                              .arg(QDir::toNativeSeparators(AppPaths::configFilePath()),
                                   QDir::toNativeSeparators(AppPaths::logsDir()),
                                   QDir::toNativeSeparators(AppPaths::autosaveDir()),
                                   QDir::toNativeSeparators(AppPaths::templatesDir())));
}

void PreferencesDialog::applyChanges()
{
    AppSettings &settings = AppSettings::instance();

    settings.setUnitSystem(static_cast<AppSettings::UnitSystem>(m_unitSystem->currentData().toInt()));

    settings.setShowRulers(m_showRulers->isChecked());
    settings.setShowGuides(m_showGuides->isChecked());
    settings.setShowGrid(m_showGrid->isChecked());
    settings.setGridSpacingMm(m_gridSpacing->value());
    settings.setSnapTolerancePx(m_snapTolerance->value());
    settings.setSnapToCard(m_snapCard->isChecked());
    settings.setSnapToGuides(m_snapGuides->isChecked());
    settings.setSnapToObjects(m_snapObjects->isChecked());
    settings.setSnapToGrid(m_snapGrid->isChecked());

    settings.setDefaultCardPreset(m_defaultPreset->currentData().toString());
    settings.setDefaultRenderDpi(m_defaultDpi->value());
    settings.setDefaultBleedMm(m_defaultBleed->value());

    settings.setAutoSaveEnabled(m_autoSaveEnabled->isChecked());
    settings.setAutoSaveIntervalMinutes(m_autoSaveInterval->value());

    settings.setExportDpi(m_exportDpi->value());
    settings.setExportFormat(m_exportFormat->currentData().toString());
    settings.setExportDirectory(m_exportDirectory->text());

    settings.setDefaultPrinter(m_defaultPrinter->currentData().toString());
    settings.setConfirmBeforePrint(m_confirmBeforePrint->isChecked());

    settings.setVerboseLogging(m_verbose->isChecked());
    Logger::setVerbose(m_verbose->isChecked());

    settings.sync();
    emit changed();
}

void PreferencesDialog::resetToDefaults()
{
    AppSettings::instance().resetToDefaults();
    loadFromSettings();
    emit changed();
}

} // namespace occ
