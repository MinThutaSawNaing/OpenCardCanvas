#include "ui/MainWindow.h"

#include "canvas/CanvasHost.h"
#include "canvas/CardCanvas.h"
#include "commands/UndoCommands.h"
#include "core/CardSide.h"
#include "core/ImageObject.h"
#include "printing/PrinterManager.h"
#include "project/AssetStore.h"
#include "project/AutoSave.h"
#include "project/ProjectSerializer.h"
#include "project/ProjectValidator.h"
#include "project/TemplateStore.h"
#include "rendering/ExportRenderer.h"
#include "personalization/BatchRenderer.h"
#include "ui/AboutDialog.h"
#include "ui/AlignPanel.h"
#include "ui/CardSizeDialog.h"
#include "ui/DiagnosticsDialog.h"
#include "ui/IconFactory.h"
#include "ui/LayersPanel.h"
#include "ui/NewCardDialog.h"
#include "ui/PersonalizationPanel.h"
#include "ui/PreferencesDialog.h"
#include "ui/PrinterSettingsDialog.h"
#include "ui/PrintPreviewDialog.h"
#include "ui/PropertyPanel.h"
#include "ui/ToolsPanel.h"
#include "utils/AppPaths.h"
#include "utils/Logger.h"
#include "utils/Settings.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QUuid>
#include <QUrl>
#include <QVBoxLayout>

#include <atomic>
#include <mutex>
#include <thread>

namespace occ {

namespace {

// ---------------------------------------------------------------------------
// Batch print state, shared between the worker thread and the progress dialog.
//
// Plain atomics and one small mutex rather than signals: the printer API is
// blocking and callback based, and a std::thread keeps the worker free of any
// object-model threading subtleties.
// ---------------------------------------------------------------------------
struct BatchPrintState
{
    std::atomic<bool> cancel{ false };
    std::atomic<bool> done{ false };
    std::atomic<int>  total{ 0 };
    std::atomic<int>  processed{ 0 };
    std::atomic<int>  printed{ 0 };
    std::atomic<int>  simulated{ 0 };
    std::atomic<int>  failed{ 0 };

    std::mutex  mutex;
    QStringList messages;      // one line per problem, guarded by mutex

    void note(const QString &message)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        messages << message;
    }
};

} // namespace

// Renders and prints the requested records. Runs off the GUI thread, touches no
// widget, and works on its own copy of the document.
static void batchPrintWorker(BatchPrintState *state,
                             const QByteArray &documentBytes,
                             const QString &printerName,
                             int dpi,
                             const PrintPreviewDialog::Request &request,
                             CsvTable table,
                             MappingSet mapping)
{
    CardDocument localDocument;
    const ProjectSerializer::LoadResult loaded =
        ProjectSerializer::loadFromData(&localDocument, documentBytes);
    if (!loaded.ok) {
        state->note(QObject::tr("The document could not be prepared for printing: %1")
                        .arg(loaded.error));
        state->failed = state->total.load();
        state->done = true;
        return;
    }

    // A printer manager of its own: the GUI thread's manager is never touched
    // from here, so there is no shared state to protect.
    PrinterManager manager;
    CardPrinterPtr printer = printerName.isEmpty() ? manager.simulator()
                                                   : manager.printer(printerName);
    if (!printer) {
        state->note(manager.lastError().isEmpty()
                        ? QObject::tr("The printer \"%1\" is not available.").arg(printerName)
                        : manager.lastError());
        state->failed = state->total.load();
        state->done = true;
        return;
    }
    if (!printer->connect()) {
        state->note(QObject::tr("The printer \"%1\" could not be opened: %2")
                        .arg(printer->name(), printer->lastError()));
        state->failed = state->total.load();
        state->done = true;
        return;
    }

    const int jobDpi = qBound(kMinDpi, dpi, kMaxDpi);
    const double pxPerMm = jobDpi / 25.4;
    const CardSideId sides[] = { CardSideId::Front, CardSideId::Back };

    const auto printOne = [&](QVector<RenderContext::Warning> *warnings,
                             const QVector<int> &recordList, int copies) {
        PrintJob job;
        job.printerName = printerName;
        job.documentName = QObject::tr("OpenCardCanvas card");
        job.copies = copies;
        job.frontEnabled = request.frontEnabled;
        job.backEnabled = request.backEnabled;
        job.duplex = request.duplex;
        // The card's own orientation decides how it is laid on the page, so a
        // portrait design prints upright instead of sideways.
        job.landscape = localDocument.geometry().isLandscape();
        job.frontDpi = job.backDpi = jobDpi;
        job.simulatorOutputDirectory = AppPaths::defaultExportsDir()
                                       + QStringLiteral("/simulator");

        AssetStore photos;
        for (CardSideId sideId : sides) {
            if (sideId == CardSideId::Front && !request.frontEnabled)
                continue;
            if (sideId == CardSideId::Back && !request.backEnabled)
                continue;

            QImage image;
            if (recordList.isEmpty()) {
                image = BatchRenderer::renderTemplate(localDocument, sideId, pxPerMm, warnings);
            } else {
                const int record = recordList.first();
                const QString photoPath = DataMapper::photoPathForRecord(table, record,
                                                                        mapping, nullptr);
                if (!photoPath.isEmpty())
                    photos.addImageFile(photoPath, nullptr);
                image = BatchRenderer::renderRecord(localDocument, sideId, record, table,
                                                    mapping, photos, pxPerMm, warnings);
            }
            if (sideId == CardSideId::Front)
                job.frontImage = image;
            else
                job.backImage = image;
        }

        if (!printer->print(job)) {
            state->failed.fetch_add(1);
            state->note(printer->lastError().isEmpty()
                            ? QObject::tr("The printer rejected a card.")
                            : printer->lastError());
            return;
        }
        // The distinction that must never be lost: a simulated job did not put
        // ink on a card.
        if (printer->backend() == PrinterBackendKind::Simulator)
            state->simulated.fetch_add(1);
        else
            state->printed.fetch_add(1);
    };

    if (request.records.isEmpty()) {
        // The design as it is, in one job of `copies` cards.
        QVector<RenderContext::Warning> warnings;
        printOne(&warnings, {}, qMax(1, request.copies));
        for (const RenderContext::Warning &warning : warnings)
            state->note(warning.message);
        state->processed = state->total.load();
    } else {
        for (int record : request.records) {
            if (state->cancel.load())
                break;
            QVector<RenderContext::Warning> warnings;
            printOne(&warnings, QVector<int>{ record }, 1);
            for (const RenderContext::Warning &warning : warnings)
                state->note(warning.message);
            state->processed.fetch_add(1);
        }
    }

    state->done = true;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowIcon(IconFactory::icon(QStringLiteral("logo")));
    setObjectName(QStringLiteral("MainWindow"));
    setDockNestingEnabled(true);
    setMinimumSize(900, 620);

    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_undoStack = new QUndoStack(this);
    m_undoStack->setUndoLimit(200);

    createDocument();
    createActions();
    // Docks are created before the menus because the View menu inserts their
    // toggle actions; creating the menus first left those pointers null and
    // crashed at start-up.
    createDocks();
    createMenus();
    createToolBars();
    createStatusBar();
    wirePanels();
    wireActions();

    m_printers = new PrinterManager(this);
    m_autoSave = new AutoSave(this);
    connect(m_autoSave, &AutoSave::autoSaveFailed, this, [this](const QString &error) {
        setStatus(tr("Auto-save failed: %1").arg(error));
        qCWarning(lcProject) << "Auto-save failed:" << error;
    });
    connect(m_autoSave, &AutoSave::autoSaved, this, [this](const QString &path) {
        qCDebug(lcProject) << "Auto-saved to" << path;
    });

    applySettingsToViews();
    restoreSession();
    refreshRecentMenu();
    updateStatusBar();
    updateTitle();

    // Recovery is offered after the window is on screen: a modal prompt before
    // the application has painted looks like a crash.
    QTimer::singleShot(0, this, &MainWindow::checkRecovery);
}

MainWindow::~MainWindow()
{
    if (m_autoSave)
        m_autoSave->detach();
}

void MainWindow::createDocument()
{
    m_canvasHost = new CanvasHost(this);
    m_canvas = m_canvasHost->canvas();
    setCentralWidget(m_canvasHost);

    m_canvasHost->setDocument(&m_document);
    m_canvasHost->setUndoStack(m_undoStack);
    m_canvasHost->setGuideModel(m_canvas->guides());

    connect(&m_document, &CardDocument::contentsChanged, this, &MainWindow::onDocumentChanged);
    connect(&m_document, &CardDocument::dirtyChanged, this, [this](bool) { updateTitle(); });
    connect(&m_document, &CardDocument::geometryChanged, this, [this] {
        updateStatusBar();
        updateWarningIndicator();
    });
    connect(&m_document, &CardDocument::filePathChanged, this, [this](const QString &) {
        updateTitle();
    });
}

void MainWindow::applyDefaultDockLayout()
{
    // addDockWidget() STACKS a second dock on top of the first in the same area,
    // which is what made the Align panel overlay the Properties panel. Splitting
    // is what puts them side by side (or, here, one above the other).
    addDockWidget(Qt::LeftDockWidgetArea, m_toolsDock);

    addDockWidget(Qt::RightDockWidgetArea, m_propertyDock);
    splitDockWidget(m_propertyDock, m_alignDock, Qt::Vertical);

    addDockWidget(Qt::BottomDockWidgetArea, m_layersDock);
    tabifyDockWidget(m_layersDock, m_personalizationDock);
    m_layersDock->raise();

    resizeDocks({ m_propertyDock, m_alignDock }, { 460, 240 }, Qt::Vertical);
    resizeDocks({ m_propertyDock }, { 340 }, Qt::Horizontal);
    resizeDocks({ m_layersDock }, { 210 }, Qt::Vertical);
}

void MainWindow::createDocks()
{
    m_toolsPanel = new ToolsPanel(this);
    m_toolsDock = new QDockWidget(tr("Tools"), this);
    m_toolsDock->setObjectName(QStringLiteral("ToolsDock"));
    m_toolsDock->setWidget(m_toolsPanel);
    m_toolsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_propertyPanel = new PropertyPanel(this);
    m_propertyDock = new QDockWidget(tr("Properties"), this);
    m_propertyDock->setObjectName(QStringLiteral("PropertyDock"));
    m_propertyDock->setWidget(m_propertyPanel);
    m_propertyDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_alignPanel = new AlignPanel(this);
    m_alignDock = new QDockWidget(tr("Align and arrange"), this);
    m_alignDock->setObjectName(QStringLiteral("AlignDock"));
    m_alignDock->setWidget(m_alignPanel);
    m_alignDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_layersPanel = new LayersPanel(this);
    m_layersDock = new QDockWidget(tr("Layers"), this);
    m_layersDock->setObjectName(QStringLiteral("LayersDock"));
    m_layersDock->setWidget(m_layersPanel);

    m_personalizationPanel = new PersonalizationPanel(this);
    m_personalizationDock = new QDockWidget(tr("Personalization"), this);
    m_personalizationDock->setObjectName(QStringLiteral("PersonalizationDock"));
    m_personalizationDock->setWidget(m_personalizationPanel);

    applyDefaultDockLayout();

    for (QDockWidget *dock : { m_toolsDock, m_propertyDock, m_alignDock, m_layersDock,
                               m_personalizationDock }) {
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                          | QDockWidget::DockWidgetClosable);
        dock->setMinimumWidth(180);
    }
}

void MainWindow::createStatusBar()
{
    auto *bar = statusBar();

    auto *sideGroup = new QButtonGroup(this);
    sideGroup->setExclusive(true);
    m_frontButton = new QToolButton(bar);
    m_frontButton->setText(tr("FRONT"));
    m_frontButton->setIcon(IconFactory::icon(QStringLiteral("front")));
    m_frontButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_frontButton->setCheckable(true);
    m_frontButton->setChecked(true);
    m_frontButton->setToolTip(tr("Edit the front of the card"));
    m_backButton = new QToolButton(bar);
    m_backButton->setText(tr("BACK"));
    m_backButton->setIcon(IconFactory::icon(QStringLiteral("back")));
    m_backButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_backButton->setCheckable(true);
    m_backButton->setToolTip(tr("Edit the back of the card"));
    sideGroup->addButton(m_frontButton);
    sideGroup->addButton(m_backButton);
    bar->addWidget(m_frontButton);
    bar->addWidget(m_backButton);
    connect(m_frontButton, &QToolButton::clicked, this,
            [this] { setSide(CardSideId::Front); });
    connect(m_backButton, &QToolButton::clicked, this, [this] { setSide(CardSideId::Back); });

    m_zoomCombo = new QComboBox(bar);
    m_zoomCombo->setEditable(true);
    m_zoomCombo->setInsertPolicy(QComboBox::NoInsert);
    m_zoomCombo->setMinimumWidth(90);
    m_zoomCombo->setToolTip(tr("Zoom. Type a percentage, or pick one of the presets."));
    for (int percent : { 25, 50, 75, 100, 150, 200, 400 })
        m_zoomCombo->addItem(tr("%1 %").arg(percent), percent);
    m_zoomCombo->addItem(tr("Fit"), -1);
    bar->addPermanentWidget(m_zoomCombo);
    connect(m_zoomCombo, &QComboBox::activated, this, [this](int index) {
        const int percent = m_zoomCombo->itemData(index).toInt();
        if (percent < 0)
            m_canvasHost->fitToWindow();
        else
            applyZoomPercent(percent);
    });
    connect(m_zoomCombo->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        const QString text = m_zoomCombo->currentText().remove(QLatin1Char('%')).trimmed();
        bool ok = false;
        const double percent = text.toDouble(&ok);
        if (ok && percent > 0.0)
            applyZoomPercent(percent);
        else
            updateStatusBar();
    });

    m_cardSizeLabel = new QLabel(bar);
    m_cardSizeLabel->setToolTip(tr("Card size and render resolution"));
    bar->addPermanentWidget(m_cardSizeLabel);

    m_selectionLabel = new QLabel(bar);
    bar->addPermanentWidget(m_selectionLabel);

    m_warningButton = new QToolButton(bar);
    m_warningButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_warningButton->setAutoRaise(true);
    m_warningButton->setToolTip(tr("Design problems found by the validator. Click for the "
                                   "details."));
    bar->addPermanentWidget(m_warningButton);
    connect(m_warningButton, &QToolButton::clicked, this, &MainWindow::showDiagnostics);

    // Validation walks every object, so it is debounced rather than run on every
    // keystroke of a text edit.
    m_warningTimer = new QTimer(this);
    m_warningTimer->setSingleShot(true);
    m_warningTimer->setInterval(400);
    connect(m_warningTimer, &QTimer::timeout, this, &MainWindow::updateWarningIndicator);
}

void MainWindow::createActions()
{
    const auto make = [this](const QString &iconName, const QString &text,
                             const QKeySequence &shortcut, const QString &tip = QString()) {
        auto *action = new QAction(text, this);
        // An empty icon name means "no icon": an icon factory lookup with an
        // unknown name would draw the generic fallback, which would look like a
        // deliberately meaningless symbol in the menu.
        if (!iconName.isEmpty())
            action->setIcon(IconFactory::icon(iconName));
        if (!shortcut.isEmpty())
            action->setShortcut(shortcut);
        action->setToolTip(tip.isEmpty() ? text : tip);
        action->setStatusTip(action->toolTip());
        return action;
    };

    m_actNew = make(QStringLiteral("new"), tr("&New card..."), QKeySequence::New,
                    tr("Create a new card"));
    m_actNewTemplate = make(QStringLiteral("template"), tr("New from &template..."),
                            QKeySequence(QStringLiteral("Ctrl+Shift+N")),
                            tr("Create a card from one of your templates"));
    m_actOpen = make(QStringLiteral("open"), tr("&Open..."), QKeySequence::Open,
                     tr("Open a project"));
    m_actSave = make(QStringLiteral("save"), tr("&Save"), QKeySequence::Save,
                     tr("Save the project"));
    m_actSaveAs = make(QStringLiteral("save_as"), tr("Save &As..."), QKeySequence::SaveAs,
                       tr("Save the project under a new name"));
    // Card size and orientation belong next to New/Open/Save: they describe the
    // sheet being designed, not its contents, and until this existed a card made
    // in the wrong orientation could never be corrected.
    m_actCardSize = make(QStringLiteral("front"), tr("Card si&ze and orientation..."),
                         QKeySequence(QStringLiteral("Ctrl+Alt+C")),
                         tr("Change the card size, orientation, resolution or bleed"));
    m_actImportImage = make(QStringLiteral("image"), tr("&Import image..."),
                            QKeySequence(QStringLiteral("Ctrl+Shift+I")),
                            tr("Add an image to the card"));
    m_actImportData = make(QStringLiteral("data"), tr("Import &data (CSV)..."),
                           QKeySequence(QStringLiteral("Ctrl+Shift+D")),
                           tr("Load a data set for personalization"));
    m_actSaveTemplate = make(QStringLiteral("template"), tr("Save as Te&mplate..."),
                             QKeySequence(), tr("Store this design in the template library"));
    m_actPrint = make(QStringLiteral("print"), tr("&Print..."), QKeySequence::Print,
                      tr("Print cards"));
    m_actPrintPreview = make(QStringLiteral("preview"), tr("Print Pre&view..."),
                             QKeySequence(QStringLiteral("Ctrl+Shift+P")),
                             tr("See exactly what will be printed"));
    m_actPrinterSettings = make(QStringLiteral("printer"), tr("Printer &settings..."),
                                QKeySequence(), tr("Choose the printer and check its driver"));
    m_actPrinterDiagnostics = make(QStringLiteral("log"), tr("Printer &diagnostics..."),
                                   QKeySequence(), tr("What the printer driver reports"));
    m_actTestCard = make(QStringLiteral("print"), tr("Print &test card"), QKeySequence(),
                         tr("Send a generated test page to the printer"));
    m_actPreferences = make(QStringLiteral("settings"), tr("Pre&ferences..."),
                            QKeySequence::Preferences);
    m_actExit = make(QString(), tr("E&xit"), QKeySequence::Quit);

    m_actUndo = m_undoStack->createUndoAction(this, tr("&Undo"));
    m_actUndo->setShortcut(QKeySequence::Undo);
    m_actUndo->setIcon(IconFactory::icon(QStringLiteral("undo")));
    m_actRedo = m_undoStack->createRedoAction(this, tr("&Redo"));
    m_actRedo->setShortcut(QKeySequence::Redo);
    m_actRedo->setIcon(IconFactory::icon(QStringLiteral("redo")));

    m_actCut = make(QStringLiteral("cut"), tr("Cu&t"), QKeySequence::Cut);
    m_actCopy = make(QStringLiteral("copy"), tr("&Copy"), QKeySequence::Copy);
    m_actPaste = make(QStringLiteral("paste"), tr("&Paste"), QKeySequence::Paste);
    m_actDuplicate = make(QStringLiteral("duplicate"), tr("&Duplicate"),
                          QKeySequence(QStringLiteral("Ctrl+D")));
    m_actDelete = make(QStringLiteral("delete"), tr("De&lete"),
                       QKeySequence(QStringLiteral("Delete")));
    m_actSelectAll = make(QStringLiteral("select"), tr("Select &All"),
                          QKeySequence::SelectAll);

    m_actGroup = make(QStringLiteral("group"), tr("&Group"),
                      QKeySequence(QStringLiteral("Ctrl+G")));
    m_actUngroup = make(QStringLiteral("ungroup"), tr("&Ungroup"),
                        QKeySequence(QStringLiteral("Ctrl+Shift+G")));
    m_actLock = make(QStringLiteral("lock"), tr("&Lock"), QKeySequence());
    m_actUnlock = make(QStringLiteral("unlock"), tr("&Unlock"), QKeySequence());
    m_actHide = make(QStringLiteral("hidden"), tr("&Hide"), QKeySequence());
    m_actShow = make(QStringLiteral("visible"), tr("&Show"), QKeySequence());
    m_actEditText = make(QStringLiteral("text"), tr("&Edit text"), QKeySequence());
    m_actReplaceImage = make(QStringLiteral("image"), tr("&Replace image..."), QKeySequence());
    m_actCrop = make(QString(), tr("&Crop..."), QKeySequence());

    m_actZoomIn = make(QStringLiteral("zoom_in"), tr("Zoom &in"),
                       QKeySequence(QStringLiteral("Ctrl+=")));
    m_actZoomOut = make(QStringLiteral("zoom_out"), tr("Zoom &out"),
                        QKeySequence(QStringLiteral("Ctrl+-")));
    m_actZoomFit = make(QStringLiteral("zoom_fit"), tr("&Fit card"),
                        QKeySequence(QStringLiteral("Ctrl+0")));
    m_actZoomActual = make(QStringLiteral("zoom_fit"), tr("&Actual size"),
                           QKeySequence(QStringLiteral("Ctrl+1")));
    m_actRulers = make(QStringLiteral("guide"), tr("&Rulers"), QKeySequence());
    m_actRulers->setCheckable(true);
    m_actGuides = make(QStringLiteral("guide"), tr("&Guides"), QKeySequence());
    m_actGuides->setCheckable(true);
    m_actGrid = make(QStringLiteral("grid"), tr("&Grid"), QKeySequence());
    m_actGrid->setCheckable(true);
    m_actSnapCard = make(QStringLiteral("snap"), tr("Snap to card edges"), QKeySequence());
    m_actSnapCard->setCheckable(true);
    m_actSnapGuides = make(QStringLiteral("snap"), tr("Snap to guides"), QKeySequence());
    m_actSnapGuides->setCheckable(true);
    m_actSnapObjects = make(QStringLiteral("snap"), tr("Snap to other objects"), QKeySequence());
    m_actSnapObjects->setCheckable(true);
    m_actSnapGrid = make(QStringLiteral("snap"), tr("Snap to the grid"), QKeySequence());
    m_actSnapGrid->setCheckable(true);
    m_actResetLayout = make(QStringLiteral("refresh"), tr("&Reset layout"), QKeySequence());

    m_actBringFront = make(QStringLiteral("bring_front"), tr("Bring to &Front"),
                           QKeySequence(QStringLiteral("Ctrl+Shift+]")));
    m_actBringForward = make(QStringLiteral("bring_forward"), tr("Bring F&orward"),
                             QKeySequence(QStringLiteral("Ctrl+]")));
    m_actSendBackward = make(QStringLiteral("send_backward"), tr("Send &Backward"),
                             QKeySequence(QStringLiteral("Ctrl+[")));
    m_actSendBack = make(QStringLiteral("send_back"), tr("Send to B&ack"),
                         QKeySequence(QStringLiteral("Ctrl+Shift+[")));

    // Everything that only means something with a selection. The list is used
    // to enable/disable them together, so an action can never look available
    // while having nothing to act on.
    m_objectActions = { m_actCut,        m_actCopy,       m_actDuplicate, m_actDelete,
                        m_actLock,       m_actUnlock,     m_actHide,      m_actShow,
                        m_actEditText,   m_actReplaceImage, m_actCrop,
                        m_actBringFront, m_actBringForward, m_actSendBackward, m_actSendBack };
}

void MainWindow::createMenus()
{
    // --- File ---------------------------------------------------------------
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_actNew);
    fileMenu->addAction(m_actNewTemplate);
    fileMenu->addAction(m_actOpen);

    m_recentMenu = fileMenu->addMenu(IconFactory::icon(QStringLiteral("open")),
                                     tr("Open &Recent"));
    fileMenu->addSeparator();
    fileMenu->addAction(m_actSave);
    fileMenu->addAction(m_actSaveAs);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actCardSize);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actImportImage);
    fileMenu->addAction(m_actImportData);

    QMenu *exportMenu = fileMenu->addMenu(IconFactory::icon(QStringLiteral("export")),
                                          tr("&Export"));
    QAction *exportPng = exportMenu->addAction(tr("PNG at the export resolution"));
    QAction *exportPngHigh = exportMenu->addAction(tr("High-resolution PNG (600 dpi)"));
    QAction *exportPdf = exportMenu->addAction(tr("PDF at the card's physical size"));
    connect(exportPng, &QAction::triggered, this,
            [this] { exportDocument(QStringLiteral("png")); });
    connect(exportPngHigh, &QAction::triggered, this,
            [this] { exportDocument(QStringLiteral("png600")); });
    connect(exportPdf, &QAction::triggered, this,
            [this] { exportDocument(QStringLiteral("pdf")); });

    fileMenu->addSeparator();
    fileMenu->addAction(m_actSaveTemplate);
    QAction *manage = fileMenu->addAction(IconFactory::icon(QStringLiteral("template")),
                                          tr("&Manage templates..."));
    connect(manage, &QAction::triggered, this, &MainWindow::manageTemplates);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actPrint);
    fileMenu->addAction(m_actPrintPreview);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actExit);

    // --- Edit ---------------------------------------------------------------
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_actUndo);
    editMenu->addAction(m_actRedo);
    editMenu->addSeparator();
    editMenu->addAction(m_actCut);
    editMenu->addAction(m_actCopy);
    editMenu->addAction(m_actPaste);
    editMenu->addAction(m_actDuplicate);
    editMenu->addAction(m_actDelete);
    editMenu->addSeparator();
    editMenu->addAction(m_actSelectAll);
    editMenu->addSeparator();
    editMenu->addAction(m_actPreferences);

    // --- View ---------------------------------------------------------------
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_actZoomIn);
    viewMenu->addAction(m_actZoomOut);
    viewMenu->addAction(m_actZoomFit);
    viewMenu->addAction(m_actZoomActual);
    viewMenu->addSeparator();
    viewMenu->addAction(m_actRulers);
    viewMenu->addAction(m_actGuides);
    viewMenu->addAction(m_actGrid);

    QMenu *snapMenu = viewMenu->addMenu(IconFactory::icon(QStringLiteral("snap")),
                                        tr("&Snap to"));
    snapMenu->addAction(m_actSnapCard);
    snapMenu->addAction(m_actSnapGuides);
    snapMenu->addAction(m_actSnapObjects);
    snapMenu->addAction(m_actSnapGrid);

    viewMenu->addSeparator();
    viewMenu->addAction(m_propertyDock->toggleViewAction());
    viewMenu->addAction(m_alignDock->toggleViewAction());
    viewMenu->addAction(m_layersDock->toggleViewAction());
    viewMenu->addAction(m_personalizationDock->toggleViewAction());
    viewMenu->addAction(m_toolsDock->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(m_actResetLayout);

    // --- Object -------------------------------------------------------------
    QMenu *objectMenu = menuBar()->addMenu(tr("&Object"));
    objectMenu->addAction(m_actGroup);
    objectMenu->addAction(m_actUngroup);
    objectMenu->addSeparator();
    objectMenu->addAction(m_actLock);
    objectMenu->addAction(m_actUnlock);
    objectMenu->addAction(m_actHide);
    objectMenu->addAction(m_actShow);
    objectMenu->addSeparator();
    objectMenu->addAction(m_actEditText);
    objectMenu->addAction(m_actReplaceImage);
    objectMenu->addAction(m_actCrop);

    // --- Arrange ------------------------------------------------------------
    QMenu *arrangeMenu = menuBar()->addMenu(tr("&Arrange"));
    arrangeMenu->addAction(m_actBringFront);
    arrangeMenu->addAction(m_actBringForward);
    arrangeMenu->addAction(m_actSendBackward);
    arrangeMenu->addAction(m_actSendBack);
    arrangeMenu->addSeparator();

    QMenu *alignMenu = arrangeMenu->addMenu(IconFactory::icon(QStringLiteral("align_left")),
                                            tr("&Align"));
    addAlignActions(alignMenu);
    QMenu *distributeMenu = arrangeMenu->addMenu(
        IconFactory::icon(QStringLiteral("distribute_h")), tr("&Distribute"));
    auto *distH = distributeMenu->addAction(IconFactory::icon(QStringLiteral("distribute_h")),
                                            tr("Horizontally"));
    auto *distV = distributeMenu->addAction(IconFactory::icon(QStringLiteral("distribute_v")),
                                            tr("Vertically"));
    connect(distH, &QAction::triggered, this, [this] {
        m_canvas->distributeSelectionHorizontally();
        setStatus(tr("Selection distributed horizontally."));
    });
    connect(distV, &QAction::triggered, this, [this] {
        m_canvas->distributeSelectionVertically();
        setStatus(tr("Selection distributed vertically."));
    });
    // These are the same operations as the Align panel, so their enabled state
    // has to follow the same rule or one of the two would lie.
    connect(m_canvas, &CardCanvas::selectionChanged, this, [this, alignMenu, distH, distV] {
        const int count = m_canvas->selectionCount();
        const QList<QAction *> alignActions = alignMenu->actions();
        for (QAction *action : alignActions)
            action->setEnabled(count >= 2);
        distH->setEnabled(count >= 3);
        distV->setEnabled(count >= 3);
    });

    // --- Print --------------------------------------------------------------
    QMenu *printMenu = menuBar()->addMenu(tr("&Print"));
    printMenu->addAction(m_actPrint);
    printMenu->addAction(m_actPrintPreview);
    printMenu->addSeparator();
    printMenu->addAction(m_actPrinterSettings);
    printMenu->addAction(m_actPrinterDiagnostics);
    printMenu->addSeparator();
    printMenu->addAction(m_actTestCard);

    // --- Help ---------------------------------------------------------------
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *about = helpMenu->addAction(IconFactory::icon(QStringLiteral("logo")),
                                      tr("&About OpenCardCanvas"));
    auto *diagnostics = helpMenu->addAction(IconFactory::icon(QStringLiteral("log")),
                                            tr("&Diagnostics..."));
    auto *printerSupport = helpMenu->addAction(IconFactory::icon(QStringLiteral("printer")),
                                               tr("Printer &support"));
    auto *logFolder = helpMenu->addAction(tr("&Open log folder"));
    auto *shortcuts = helpMenu->addAction(IconFactory::icon(QStringLiteral("help")),
                                          tr("&Keyboard shortcuts"));
    shortcuts->setShortcut(QKeySequence(QKeySequence::HelpContents));
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);
    connect(diagnostics, &QAction::triggered, this, &MainWindow::showDiagnostics);
    connect(printerSupport, &QAction::triggered, this, &MainWindow::showPrinterSupport);
    connect(shortcuts, &QAction::triggered, this, &MainWindow::showShortcuts);
    connect(logFolder, &QAction::triggered, this, [this] {
        const QString dir = AppPaths::logsDir();
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir)))
            QMessageBox::information(this, tr("Log folder"), tr("The log folder is:\n%1").arg(dir));
    });
}

void MainWindow::addAlignActions(QMenu *menu)
{
    struct Entry
    {
        const char *icon;
        const char *text;
        Qt::Alignment flag;
    };
    const Entry entries[] = {
        { "align_left",     QT_TRANSLATE_NOOP("occ::MainWindow", "Left edges"), Qt::AlignLeft },
        { "align_center_h", QT_TRANSLATE_NOOP("occ::MainWindow", "Horizontal centres"),
          Qt::AlignHCenter },
        { "align_right",    QT_TRANSLATE_NOOP("occ::MainWindow", "Right edges"), Qt::AlignRight },
        { "align_top",      QT_TRANSLATE_NOOP("occ::MainWindow", "Top edges"), Qt::AlignTop },
        { "align_middle_v", QT_TRANSLATE_NOOP("occ::MainWindow", "Vertical centres"),
          Qt::AlignVCenter },
        { "align_bottom",   QT_TRANSLATE_NOOP("occ::MainWindow", "Bottom edges"),
          Qt::AlignBottom },
    };
    for (const Entry &entry : entries) {
        // The action belongs to the window, not to the menu: the same actions
        // are put on the toolbar, and the toolbar must survive the menu being
        // deleted.
        auto *action = new QAction(IconFactory::icon(QString::fromLatin1(entry.icon)),
                                   tr(entry.text), this);
        const Qt::Alignment flag = entry.flag;
        connect(action, &QAction::triggered, this, [this, flag] {
            m_canvas->alignSelection(flag);
            setStatus(tr("Selection aligned."));
        });
        menu->addAction(action);
    }
}

void MainWindow::createToolBars()
{
    m_mainToolBar = addToolBar(tr("Main"));
    m_mainToolBar->setObjectName(QStringLiteral("MainToolBar"));
    m_mainToolBar->setMovable(false);
    m_mainToolBar->addAction(m_actNew);
    m_mainToolBar->addAction(m_actOpen);
    m_mainToolBar->addAction(m_actSave);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_actUndo);
    m_mainToolBar->addAction(m_actRedo);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_actCut);
    m_mainToolBar->addAction(m_actCopy);
    m_mainToolBar->addAction(m_actPaste);
    m_mainToolBar->addAction(m_actDuplicate);
    m_mainToolBar->addAction(m_actDelete);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_actZoomFit);
    m_mainToolBar->addAction(m_actPrint);
    m_mainToolBar->addAction(m_actPrintPreview);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_actPreferences);

    m_arrangeToolBar = addToolBar(tr("Arrange"));
    m_arrangeToolBar->setObjectName(QStringLiteral("ArrangeToolBar"));
    m_arrangeToolBar->setMovable(false);
    m_arrangeToolBar->addAction(m_actBringFront);
    m_arrangeToolBar->addAction(m_actBringForward);
    m_arrangeToolBar->addAction(m_actSendBackward);
    m_arrangeToolBar->addAction(m_actSendBack);
    m_arrangeToolBar->addSeparator();
    m_arrangeToolBar->addAction(m_actGroup);
    m_arrangeToolBar->addAction(m_actUngroup);

    // The alignment buttons in the toolbar come from the same helper as the
    // Arrange menu, so a button can never exist without a working action.
    QMenu *alignMenu = new QMenu(this);
    addAlignActions(alignMenu);
    m_arrangeToolBar->addSeparator();
    const QList<QAction *> alignActions = alignMenu->actions();
    for (QAction *action : alignActions)
        m_arrangeToolBar->addAction(action);
    alignMenu->deleteLater();
}

void MainWindow::wirePanels()
{
    // --- panels <-> document / canvas ---------------------------------------
    m_propertyPanel->setDocument(&m_document);
    m_propertyPanel->setCanvas(m_canvas);
    m_alignPanel->setDocument(&m_document);
    m_alignPanel->setCanvas(m_canvas);
    m_layersPanel->setDocument(&m_document);
    m_layersPanel->setCanvas(m_canvas);
    m_personalizationPanel->setDocument(&m_document);

    connect(m_layersPanel, &LayersPanel::statusMessage, this, &MainWindow::setStatus);
    connect(m_propertyPanel, &PropertyPanel::statusMessage, this, &MainWindow::setStatus);
    connect(m_alignPanel, &AlignPanel::statusMessage, this, &MainWindow::setStatus);
    connect(m_personalizationPanel, &PersonalizationPanel::statusMessage,
            this, &MainWindow::setStatus);
    connect(m_personalizationPanel, &PersonalizationPanel::printRequested,
            this, &MainWindow::runBatchPrint);

    connect(m_canvasHost, &CanvasHost::statusMessage, this, &MainWindow::setStatus);
    connect(m_canvasHost, &CanvasHost::zoomChanged, this, &MainWindow::onZoomChanged);
    connect(m_canvasHost, &CanvasHost::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_canvas, &CardCanvas::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_canvas, &CardCanvas::currentSideChanged, this, [this](CardSideId side) {
        m_frontButton->setChecked(side == CardSideId::Front);
        m_backButton->setChecked(side == CardSideId::Back);
        m_layersPanel->refresh();
        updateStatusBar();
    });
    connect(m_canvas, &CardCanvas::mousePositionChanged, this, [this](const QPointF &mm) {
        // A precise readout is what makes a millimetre-based canvas usable.
        setStatus(tr("%1, %2 mm")
                      .arg(QString::number(mm.x(), 'f', 2), QString::number(mm.y(), 'f', 2)));
    });

    // --- tools --------------------------------------------------------------
    connect(m_toolsPanel, &ToolsPanel::toolSelected, this, &MainWindow::onToolsToolSelected);
    connect(m_toolsPanel, &ToolsPanel::shapeRequested, this, &MainWindow::onShapeRequested);
    connect(m_toolsPanel, &ToolsPanel::imageRequested, this, &MainWindow::onImageRequested);
    connect(m_toolsPanel, &ToolsPanel::photoRequested, this, &MainWindow::onPhotoRequested);
}

void MainWindow::wireActions()
{
    // --- object / edit ------------------------------------------------------
    connect(m_actCut, &QAction::triggered, m_canvas, &CardCanvas::cutSelection);
    connect(m_actCopy, &QAction::triggered, m_canvas, &CardCanvas::copySelection);
    connect(m_actPaste, &QAction::triggered, m_canvas, &CardCanvas::pasteClipboard);
    connect(m_actDuplicate, &QAction::triggered, m_canvas, &CardCanvas::duplicateSelection);
    connect(m_actDelete, &QAction::triggered, m_canvas, &CardCanvas::deleteSelection);
    connect(m_actSelectAll, &QAction::triggered, m_canvas, &CardCanvas::selectAll);
    connect(m_actGroup, &QAction::triggered, m_canvas, &CardCanvas::groupSelection);
    connect(m_actUngroup, &QAction::triggered, m_canvas, &CardCanvas::ungroupSelection);
    connect(m_actLock, &QAction::triggered, this,
            [this] { m_canvas->setSelectionLocked(true); });
    connect(m_actUnlock, &QAction::triggered, this,
            [this] { m_canvas->setSelectionLocked(false); });
    connect(m_actHide, &QAction::triggered, this,
            [this] { m_canvas->setSelectionVisible(false); });
    connect(m_actShow, &QAction::triggered, this,
            [this] { m_canvas->setSelectionVisible(true); });
    connect(m_actEditText, &QAction::triggered, this, &MainWindow::editSelectedText);
    connect(m_actReplaceImage, &QAction::triggered, this, &MainWindow::replaceSelectedImage);
    connect(m_actCrop, &QAction::triggered, this, &MainWindow::cropSelection);
    connect(m_actBringFront, &QAction::triggered, m_canvas, &CardCanvas::bringToFront);
    connect(m_actBringForward, &QAction::triggered, m_canvas, &CardCanvas::bringForward);
    connect(m_actSendBackward, &QAction::triggered, m_canvas, &CardCanvas::sendBackward);
    connect(m_actSendBack, &QAction::triggered, m_canvas, &CardCanvas::sendToBack);

    // --- file ---------------------------------------------------------------
    connect(m_actNew, &QAction::triggered, this, &MainWindow::newDocument);
    connect(m_actNewTemplate, &QAction::triggered, this, &MainWindow::newFromTemplate);
    connect(m_actOpen, &QAction::triggered, this, &MainWindow::openProjectDialog);
    connect(m_actSave, &QAction::triggered, this, &MainWindow::saveProject);
    connect(m_actSaveAs, &QAction::triggered, this, &MainWindow::saveProjectAs);
    connect(m_actCardSize, &QAction::triggered, this, &MainWindow::changeCardGeometry);
    connect(m_actImportImage, &QAction::triggered, this, &MainWindow::importImage);
    connect(m_actImportData, &QAction::triggered, this, &MainWindow::importData);
    connect(m_actSaveTemplate, &QAction::triggered, this, &MainWindow::saveAsTemplate);
    connect(m_actExit, &QAction::triggered, this, &MainWindow::close);
    connect(m_actPreferences, &QAction::triggered, this, [this] {
        PreferencesDialog dialog(m_printers, this);
        connect(&dialog, &PreferencesDialog::changed, this, &MainWindow::applySettingsToViews);
        dialog.exec();
    });

    // --- view ---------------------------------------------------------------
    connect(m_actZoomIn, &QAction::triggered, this, [this] { m_canvas->zoomIn(); });
    connect(m_actZoomOut, &QAction::triggered, this, [this] { m_canvas->zoomOut(); });
    connect(m_actZoomFit, &QAction::triggered, m_canvasHost, &CanvasHost::fitToWindow);
    connect(m_actZoomActual, &QAction::triggered, this, [this] { applyZoomPercent(100.0); });
    connect(m_actResetLayout, &QAction::triggered, this, &MainWindow::resetLayout);
    connect(m_actRulers, &QAction::toggled, this, [this](bool on) {
        m_canvasHost->setShowRulers(on);
        AppSettings::instance().setShowRulers(on);
    });
    connect(m_actGuides, &QAction::toggled, this, [this](bool on) {
        m_canvasHost->setShowGuides(on);
        AppSettings::instance().setShowGuides(on);
        m_alignPanel->refresh();
    });
    connect(m_actGrid, &QAction::toggled, this, [this](bool on) {
        m_canvasHost->setShowGrid(on);
        AppSettings::instance().setShowGrid(on);
        m_alignPanel->refresh();
    });
    const auto snapToggled = [this] {
        SnapEngine::Options options = m_canvas->snapOptions();
        options.toCard = m_actSnapCard->isChecked();
        options.toGuides = m_actSnapGuides->isChecked();
        options.toObjects = m_actSnapObjects->isChecked();
        options.toGrid = m_actSnapGrid->isChecked();
        m_canvasHost->setSnapOptions(options);
        AppSettings &settings = AppSettings::instance();
        settings.setSnapToCard(options.toCard);
        settings.setSnapToGuides(options.toGuides);
        settings.setSnapToObjects(options.toObjects);
        settings.setSnapToGrid(options.toGrid);
        m_alignPanel->refresh();
    };
    connect(m_actSnapCard, &QAction::toggled, this, snapToggled);
    connect(m_actSnapGuides, &QAction::toggled, this, snapToggled);
    connect(m_actSnapObjects, &QAction::toggled, this, snapToggled);
    connect(m_actSnapGrid, &QAction::toggled, this, snapToggled);

    // --- printing -----------------------------------------------------------
    connect(m_actPrint, &QAction::triggered, this, &MainWindow::printCards);
    connect(m_actPrintPreview, &QAction::triggered, this, &MainWindow::printPreview);
    connect(m_actPrinterSettings, &QAction::triggered, this, &MainWindow::openPrinterSettings);
    connect(m_actPrinterDiagnostics, &QAction::triggered, this,
            &MainWindow::openPrinterDiagnostics);
    connect(m_actTestCard, &QAction::triggered, this, &MainWindow::printTestCard);
}

void MainWindow::applySettingsToViews()
{
    AppSettings &settings = AppSettings::instance();

    const DisplayUnit unit = settings.unitSystem() == AppSettings::UnitSystem::Inches
                                 ? DisplayUnit::Inches
                                 : DisplayUnit::Millimeters;
    m_canvasHost->setUnitDisplay(unit);
    m_canvasHost->setShowRulers(settings.showRulers());
    m_canvasHost->setShowGuides(settings.showGuides());
    m_canvasHost->setShowGrid(settings.showGrid());
    m_canvas->setShowPrintMargins(false);

    SnapEngine::Options options = m_canvas->snapOptions();
    options.toCard = settings.snapToCard();
    options.toGuides = settings.snapToGuides();
    options.toObjects = settings.snapToObjects();
    options.toGrid = settings.snapToGrid();
    options.gridSpacingMm = settings.gridSpacingMm();
    options.tolerancePx = settings.snapTolerancePx();
    m_canvasHost->setSnapOptions(options);

    // The menu check marks follow the settings rather than the other way round,
    // so a change made in Preferences is reflected everywhere at once.
    const QSignalBlocker blockRulers(m_actRulers);
    const QSignalBlocker blockGuides(m_actGuides);
    const QSignalBlocker blockGrid(m_actGrid);
    const QSignalBlocker blockCard(m_actSnapCard);
    const QSignalBlocker blockGuidesSnap(m_actSnapGuides);
    const QSignalBlocker blockObjects(m_actSnapObjects);
    const QSignalBlocker blockGridSnap(m_actSnapGrid);
    m_actRulers->setChecked(settings.showRulers());
    m_actGuides->setChecked(settings.showGuides());
    m_actGrid->setChecked(settings.showGrid());
    m_actSnapCard->setChecked(options.toCard);
    m_actSnapGuides->setChecked(options.toGuides);
    m_actSnapObjects->setChecked(options.toObjects);
    m_actSnapGrid->setChecked(options.toGrid);

    m_propertyPanel->refresh();
    m_alignPanel->refresh();
    m_layersPanel->refresh();

    if (m_autoSave) {
        m_autoSave->setEnabled(settings.autoSaveEnabled());
        m_autoSave->setInterval(settings.autoSaveIntervalMinutes());
        if (settings.autoSaveEnabled()) {
            m_autoSave->attach(&m_document, m_sessionId);
            m_autoSave->start();
        } else {
            m_autoSave->stop();
        }
    }
}

void MainWindow::restoreSession()
{
    AppSettings &settings = AppSettings::instance();
    const QByteArray geometry = settings.windowGeometry();
    if (!geometry.isEmpty()) {
        if (!restoreGeometry(geometry))
            resize(1280, 800);
    } else {
        resize(1280, 800);
    }

    // The saved dock arrangement is versioned. A state written by an older build
    // can describe a layout that no longer matches (an earlier version stacked
    // the Align panel on top of the Properties panel), and restoring it would
    // bring that defect back. A mismatch simply keeps the layout we just built,
    // and a failed restore is reported rather than leaving the window odd.
    const QByteArray state = settings.windowState();
    if (state.size() > 1 && int(static_cast<unsigned char>(state.at(0))) == kLayoutVersion) {
        if (!restoreState(state.mid(1))) {
            qCWarning(lcUi) << "The saved window layout was ignored because it no longer matches;"
                               " using the default layout.";
            applyDefaultDockLayout();
        }
    }

    // Old recovery files must not accumulate forever.
    AutoSave::purgeOlderThan(30);
}

void MainWindow::resetLayout()
{
    for (QDockWidget *dock : { m_toolsDock, m_propertyDock, m_alignDock, m_layersDock,
                               m_personalizationDock }) {
        dock->setFloating(false);
        dock->setVisible(true);
    }

    applyDefaultDockLayout();

    m_mainToolBar->setVisible(true);
    m_arrangeToolBar->setVisible(true);
    setStatus(tr("Window layout reset."));
}

void MainWindow::setSide(CardSideId side)
{
    m_canvas->setCurrentSide(side);
    m_layersPanel->refresh();
    m_propertyPanel->refresh();
    updateStatusBar();
}

void MainWindow::applyZoomPercent(double percent)
{
    m_canvas->setZoomPercent(percent);
    onZoomChanged(m_canvas->zoomPercent());
}

void MainWindow::onZoomChanged(double percent)
{
    if (!m_zoomCombo)
        return;
    const QSignalBlocker blocker(m_zoomCombo);
    m_zoomCombo->setCurrentText(tr("%1 %").arg(QString::number(percent, 'f', 0)));
}

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------

void MainWindow::newDocument()
{
    if (!maybeSave())
        return;

    NewCardDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_undoStack->clear();
    m_document.clearSide(CardSideId::Front);
    m_document.clearSide(CardSideId::Back);
    m_document.setGeometry(dialog.cardGeometry());
    m_document.setFilePath(QString());
    m_document.setIsTemplate(false);

    const QString templatePath = dialog.templatePath();
    if (!templatePath.isEmpty()) {
        QString error;
        if (!TemplateStore::load(templatePath, &m_document, &error)) {
            QMessageBox::warning(this, tr("New card"),
                                 tr("The template could not be loaded.\n\n%1").arg(error));
            logFailure(tr("The template could not be loaded."), error);
        }
        // A card created from a template is a new, unsaved document.
        m_document.setFilePath(QString());
        m_document.setIsTemplate(false);
    }

    m_document.notifyReloaded();
    m_document.setDirty(false);
    m_undoStack->clear();
    m_propertyPanel->refresh();
    m_alignPanel->refresh();
    m_layersPanel->refresh();
    m_canvasHost->fitToWindow();
    updateTitle();
    updateStatusBar();
    updateWarningIndicator();
    setStatus(tr("New card created."));
}

void MainWindow::newFromTemplate()
{
    QString error;
    const QVector<TemplateStore::TemplateInfo> templates = TemplateStore::list(&error);
    if (templates.isEmpty()) {
        QMessageBox::information(this, tr("New from template"),
                                 error.isEmpty()
                                     ? tr("There are no templates yet. Save one with "
                                          "File > Save as Template... first.")
                                     : error);
        return;
    }

    if (!maybeSave())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("New from template"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *list = new QListWidget(&dialog);
    for (const TemplateStore::TemplateInfo &info : templates) {
        auto *item = new QListWidgetItem(info.name, list);
        item->setData(Qt::UserRole, info.filePath);
        item->setToolTip(info.description);
    }
    list->setCurrentRow(0);
    layout->addWidget(list);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || !list->currentItem())
        return;

    const QString path = list->currentItem()->data(Qt::UserRole).toString();
    const QString name = list->currentItem()->text();
    m_undoStack->clear();
    QString loadError;
    if (!TemplateStore::load(path, &m_document, &loadError)) {
        QMessageBox::warning(this, tr("New from template"),
                             tr("The template could not be loaded.\n\n%1").arg(loadError));
        logFailure(tr("The template could not be loaded."), loadError);
        return;
    }

    m_document.setFilePath(QString());
    m_document.setIsTemplate(false);
    m_document.setDirty(false);
    m_undoStack->clear();
    m_propertyPanel->refresh();
    m_layersPanel->refresh();
    m_alignPanel->refresh();
    m_canvasHost->fitToWindow();
    updateTitle();
    updateStatusBar();
    setStatus(tr("New card created from the template \"%1\".").arg(name));
}

void MainWindow::openProjectDialog()
{
    if (!maybeSave())
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open project"), AppSettings::instance().lastOpenDirectory(),
        tr("OpenCardCanvas projects (*.occard);;All files (*)"));
    if (path.isEmpty())
        return;
    openProjectFileInternal(path);
}

void MainWindow::openProjectFile(const QString &path)
{
    openProjectFileInternal(path);
}

void MainWindow::openProjectFileInternal(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const ProjectSerializer::LoadResult result = ProjectSerializer::load(&m_document, absolute);
    if (!result.ok) {
        QMessageBox::warning(this, tr("Open project"),
                             tr("The project could not be opened.\n\n%1").arg(result.error));
        logFailure(tr("Opening \"%1\" failed.").arg(QFileInfo(absolute).fileName()),
                   result.technicalDetail);
        return;
    }

    AppSettings::instance().setLastOpenDirectory(QFileInfo(absolute).absolutePath());
    AppSettings::instance().addRecentProject(absolute);
    refreshRecentMenu();

    m_undoStack->clear();
    m_document.setFilePath(absolute);
    m_document.setDirty(false);
    m_canvasHost->setDocument(&m_document);
    m_canvasHost->fitToWindow();
    m_propertyPanel->setDocument(&m_document);
    m_alignPanel->setDocument(&m_document);
    m_layersPanel->setDocument(&m_document);
    m_personalizationPanel->setDocument(&m_document);
    updateTitle();
    updateStatusBar();
    updateWarningIndicator();

    if (m_autoSave && AppSettings::instance().autoSaveEnabled()) {
        m_autoSave->detach();
        m_autoSave->attach(&m_document, m_sessionId);
        m_autoSave->start();
    }

    if (result.warnings.isEmpty()) {
        setStatus(tr("Opened \"%1\".").arg(QFileInfo(absolute).fileName()));
    } else {
        // Warnings are never swallowed: the file loaded, but something in it was
        // not what the reader expected.
        setStatus(tr("Opened \"%1\" with %2 warning(s).")
                      .arg(QFileInfo(absolute).fileName())
                      .arg(result.warnings.size()));
        QMessageBox::information(this, tr("Open project"),
                                 tr("The project was opened, but:\n\n%1")
                                     .arg(result.warnings.join(QLatin1Char('\n'))));
    }
}

void MainWindow::refreshRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();

    AppSettings &settings = AppSettings::instance();
    const QStringList recent = settings.recentProjects();
    for (const QString &path : recent) {
        const QFileInfo info(path);
        auto *action = m_recentMenu->addAction(info.fileName());
        action->setToolTip(QDir::toNativeSeparators(path));
        // A recent entry whose file is gone is shown but disabled, so the list
        // does not silently shrink and the user can see what happened.
        action->setEnabled(info.exists());
        connect(action, &QAction::triggered, this, [this, path] {
            if (!maybeSave())
                return;
            openProjectFileInternal(path);
        });
    }
    if (!recent.isEmpty()) {
        m_recentMenu->addSeparator();
        auto *clear = m_recentMenu->addAction(tr("Clear the list"));
        connect(clear, &QAction::triggered, this, [this] {
            AppSettings::instance().clearRecentProjects();
            refreshRecentMenu();
            setStatus(tr("The recent project list was cleared."));
        });
    }
    m_recentMenu->setEnabled(true);
}

void MainWindow::saveProject()
{
    if (m_document.filePath().isEmpty()) {
        saveProjectAs();
        return;
    }

    const ProjectSerializer::SaveResult result =
        ProjectSerializer::save(m_document, m_document.filePath());
    if (!result.ok) {
        QMessageBox::warning(this, tr("Save project"),
                             tr("The project could not be saved.\n\n%1").arg(result.error));
        logFailure(tr("Saving \"%1\" failed.").arg(QFileInfo(m_document.filePath()).fileName()),
                   result.technicalDetail);
        return;
    }

    m_document.setDirty(false);
    AppSettings::instance().addRecentProject(m_document.filePath());
    refreshRecentMenu();
    updateTitle();
    setStatus(tr("Saved \"%1\".").arg(QFileInfo(m_document.filePath()).fileName()));
}

void MainWindow::saveProjectAs()
{
    QString suggested = m_document.filePath();
    if (suggested.isEmpty()) {
        const QString dir = AppSettings::instance().lastOpenDirectory();
        suggested = QDir(dir).filePath(m_document.title().isEmpty()
                                           ? tr("card.occard")
                                           : m_document.title() + QStringLiteral(".occard"));
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save project as"), suggested,
        tr("OpenCardCanvas projects (*.occard)"));
    if (path.isEmpty())
        return;

    const ProjectSerializer::SaveResult result = ProjectSerializer::save(m_document, path);
    if (!result.ok) {
        QMessageBox::warning(this, tr("Save project"),
                             tr("The project could not be saved.\n\n%1").arg(result.error));
        logFailure(tr("Saving \"%1\" failed.").arg(QFileInfo(path).fileName()),
                   result.technicalDetail);
        return;
    }

    m_document.setFilePath(path);
    m_document.setDirty(false);
    AppSettings::instance().addRecentProject(path);
    AppSettings::instance().setLastOpenDirectory(QFileInfo(path).absolutePath());
    refreshRecentMenu();
    updateTitle();

    // The document is now safely on disk, so a recovery copy of the previous
    // session can go.
    if (!m_sessionId.isEmpty())
        AutoSave::discardSession(m_sessionId);

    setStatus(tr("Saved \"%1\".").arg(QFileInfo(path).fileName()));
}

bool MainWindow::maybeSave()
{
    if (!m_document.isDirty())
        return true;

    const auto answer = QMessageBox::warning(
        this, tr("OpenCardCanvas"),
        tr("\"%1\" has unsaved changes.\n\nSave them before continuing?")
            .arg(m_document.title().isEmpty() ? tr("Untitled card") : m_document.title()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Discard)
        return true;

    saveProject();
    // If the save was cancelled or failed, the document is still dirty and the
    // caller must not continue.
    return !m_document.isDirty();
}

void MainWindow::changeCardGeometry()
{
    CardSizeDialog dialog(m_document.geometry(), &m_document, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const CardGeometry before = m_document.geometry();
    const CardGeometry after = dialog.geometry();
    if (before.toJson() == after.toJson())
        return;

    // Applied as a single undoable step, so changing the card size - or flipping
    // it between landscape and portrait - can always be taken back.
    m_undoStack->push(new GeometryCommand(&m_document, before, after,
                                          tr("Change card size")));

    m_canvasHost->setDocument(&m_document);
    m_canvasHost->fitToWindow();
    updateStatusBar();
    setStatus(tr("Card is now %1 x %2 mm (%3).")
                  .arg(after.widthMm(), 0, 'f', 2)
                  .arg(after.heightMm(), 0, 'f', 2)
                  .arg(after.isLandscape() ? tr("landscape") : tr("portrait")));
}

void MainWindow::importImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import image"), AppSettings::instance().lastOpenDirectory(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp);;All files (*)"));
    if (path.isEmpty())
        return;
    AppSettings::instance().setLastOpenDirectory(QFileInfo(path).absolutePath());
    m_canvas->addImageFromFile(path);
    setStatus(tr("Imported \"%1\".").arg(QFileInfo(path).fileName()));
}

void MainWindow::importData()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import data (CSV)"), AppSettings::instance().lastOpenDirectory(),
        tr("Data files (*.csv *.txt *.tsv);;All files (*)"));
    if (path.isEmpty())
        return;
    AppSettings::instance().setLastOpenDirectory(QFileInfo(path).absolutePath());
    m_personalizationDock->show();
    m_personalizationDock->raise();
    m_personalizationPanel->loadDataFile(path);
}

void MainWindow::exportDocument(const QString &format)
{
    ExportRenderer::Options options;
    options.format = ExportRenderer::Format::Png;
    if (format == QLatin1String("pdf"))
        options.format = ExportRenderer::Format::Pdf;
    else if (format == QLatin1String("jpeg"))
        options.format = ExportRenderer::Format::Jpeg;

    options.dpi = (format == QLatin1String("png600")) ? 600
                                                      : AppSettings::instance().exportDpi();
    options.frontSide = true;
    // Both sides are exported when the back has content: exporting only the
    // front of a two-sided card is a silent loss of half the design.
    options.backSide = !m_document.back().isEmpty();
    options.includeBleed = m_document.geometry().bleedMm() > 0.0;

    const QString extension = ExportRenderer::extensionFor(options.format);
    QString directory = AppSettings::instance().exportDirectory();
    if (directory.isEmpty())
        directory = AppPaths::defaultExportsDir();
    const QString base = m_document.title().isEmpty() ? tr("card") : m_document.title();
    const QString suggested = QDir(directory).filePath(base + extension);

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export"), suggested,
        tr("%1 files (*%2);;All files (*)").arg(ExportRenderer::formatName(options.format),
                                                extension));
    if (path.isEmpty())
        return;

    QProgressDialog progress(tr("Exporting..."), QString(), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.show();
    QApplication::processEvents();

    ExportRenderer::Result result;
    bool hardwarePending = false;
    if (options.format == ExportRenderer::Format::Pdf) {
        QString error;
        result.ok = ExportRenderer::exportPdf(m_document, path, options, &error,
                                              &hardwarePending);
        result.error = error;
        if (result.ok)
            result.filesWritten << path;
    } else {
        result = ExportRenderer::exportBoth(m_document, path, options);
    }
    progress.close();

    if (!result.ok) {
        QMessageBox::warning(this, tr("Export"),
                             tr("The export failed.\n\n%1").arg(result.error));
        logFailure(tr("Export failed."), result.technicalDetail);
        return;
    }

    AppSettings::instance().setExportDirectory(QFileInfo(path).absolutePath());
    if (hardwarePending) {
        // The PDF is valid, but something in the design was unavailable. Saying
        // so is the difference between a report and a claim.
        QMessageBox::information(
            this, tr("Export"),
            tr("The files were exported, but at least one image asset was unavailable and "
               "that part of the card is empty."));
    } else {
        setStatus(tr("Exported %1 file(s).").arg(result.fileCount()));
        QMessageBox::information(
            this, tr("Export"),
            tr("%1 file(s) were written to:\n%2")
                .arg(result.fileCount())
                .arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath())));
    }
}

void MainWindow::saveAsTemplate()
{
    const QString suggestedName = m_document.title().isEmpty() ? tr("My card")
                                                               : m_document.title();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save as template"),
                                               tr("Template name"), QLineEdit::Normal,
                                               suggestedName, &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    if (TemplateStore::exists(name.trimmed())) {
        const auto answer = QMessageBox::question(
            this, tr("Save as template"),
            tr("A template called \"%1\" already exists. Replace it?").arg(name.trimmed()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    const QString description = QInputDialog::getText(
        this, tr("Save as template"), tr("Description (optional)"), QLineEdit::Normal,
        QString(), &ok);
    if (!ok)
        return;

    QString error;
    if (!TemplateStore::saveAsTemplate(m_document, name.trimmed(), description, &error)) {
        QMessageBox::warning(this, tr("Save as template"),
                             tr("The template could not be saved.\n\n%1").arg(error));
        logFailure(tr("Saving the template failed."), error);
        return;
    }
    setStatus(tr("Template \"%1\" saved.").arg(name.trimmed()));
}

void MainWindow::manageTemplates()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Templates"));
    dialog.resize(560, 420);
    auto *layout = new QVBoxLayout(&dialog);

    auto *list = new QListWidget(&dialog);
    list->setMinimumHeight(240);
    layout->addWidget(list);

    auto *info = new QLabel(&dialog);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto *buttons = new QHBoxLayout();
    auto *use = new QPushButton(tr("Use as a new card"), &dialog);
    auto *rename = new QPushButton(tr("Rename..."), &dialog);
    auto *duplicate = new QPushButton(tr("Duplicate..."), &dialog);
    auto *remove = new QPushButton(tr("Delete"), &dialog);
    buttons->addWidget(use);
    buttons->addWidget(rename);
    buttons->addWidget(duplicate);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    const auto reload = [&] {
        list->clear();
        QString error;
        const QVector<TemplateStore::TemplateInfo> templates = TemplateStore::list(&error);
        for (const TemplateStore::TemplateInfo &templateInfo : templates) {
            auto *item = new QListWidgetItem(templateInfo.name, list);
            item->setData(Qt::UserRole, templateInfo.filePath);
            item->setToolTip(templateInfo.description);
        }
        info->setText(error.isEmpty()
                          ? tr("%1 template(s) in %2")
                                .arg(templates.size())
                                .arg(QDir::toNativeSeparators(AppPaths::templatesDir()))
                          : error);
    };
    reload();

    const auto selectedPath = [&]() -> QString {
        return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString()
                                   : QString();
    };
    const auto selectedName = [&]() -> QString {
        return list->currentItem() ? list->currentItem()->text() : QString();
    };

    connect(use, &QPushButton::clicked, &dialog, [&] {
        const QString path = selectedPath();
        if (path.isEmpty())
            return;
        QString error;
        if (!TemplateStore::load(path, &m_document, &error)) {
            QMessageBox::warning(&dialog, tr("Templates"), error);
            return;
        }
        m_document.setFilePath(QString());
        m_document.setIsTemplate(false);
        m_document.setDirty(false);
        m_undoStack->clear();
        m_propertyPanel->refresh();
        m_layersPanel->refresh();
        m_canvasHost->fitToWindow();
        updateTitle();
        setStatus(tr("New card created from \"%1\".").arg(selectedName()));
        dialog.accept();
    });
    connect(rename, &QPushButton::clicked, &dialog, [&] {
        const QString path = selectedPath();
        if (path.isEmpty())
            return;
        bool ok = false;
        const QString newName = QInputDialog::getText(&dialog, tr("Rename template"),
                                                      tr("New name"), QLineEdit::Normal,
                                                      selectedName(), &ok);
        if (!ok || newName.trimmed().isEmpty())
            return;
        QString error;
        if (!TemplateStore::rename(path, newName.trimmed(), &error))
            QMessageBox::warning(&dialog, tr("Templates"), error);
        reload();
    });
    connect(duplicate, &QPushButton::clicked, &dialog, [&] {
        const QString path = selectedPath();
        if (path.isEmpty())
            return;
        bool ok = false;
        const QString newName = QInputDialog::getText(&dialog, tr("Duplicate template"),
                                                      tr("Name for the copy"), QLineEdit::Normal,
                                                      selectedName() + tr(" copy"), &ok);
        if (!ok || newName.trimmed().isEmpty())
            return;
        QString error;
        if (!TemplateStore::duplicate(path, newName.trimmed(), &error))
            QMessageBox::warning(&dialog, tr("Templates"), error);
        reload();
    });
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        const QString path = selectedPath();
        if (path.isEmpty())
            return;
        const auto answer = QMessageBox::question(
            &dialog, tr("Delete template"),
            tr("Delete the template \"%1\"?").arg(selectedName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        QString error;
        if (!TemplateStore::remove(path, &error))
            QMessageBox::warning(&dialog, tr("Templates"), error);
        reload();
    });

    auto *closeRow = new QHBoxLayout();
    closeRow->addStretch(1);
    auto *close = new QPushButton(tr("Close"), &dialog);
    closeRow->addWidget(close);
    layout->addLayout(closeRow);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.exec();
}

// ---------------------------------------------------------------------------
// Editing helpers
// ---------------------------------------------------------------------------

void MainWindow::editSelectedText()
{
    const QVector<ObjectId> ids = m_canvas->selectionIds();
    for (const ObjectId &id : ids) {
        if (m_document.side(m_canvas->currentSide()).object(id)
            && m_document.side(m_canvas->currentSide()).object(id)->type() == ObjectType::Text) {
            m_canvas->beginTextEditing(id);
            return;
        }
    }
    setStatus(tr("Select a text object first."));
}

void MainWindow::replaceSelectedImage()
{
    const QVector<CardObject *> selection = m_canvas->selection();
    bool hasImage = false;
    for (const CardObject *object : selection) {
        if (object->type() == ObjectType::Image || object->type() == ObjectType::Photo) {
            hasImage = true;
            break;
        }
    }
    if (!hasImage) {
        QMessageBox::information(this, tr("Replace image"),
                                 tr("Select an image or a photograph first."));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Replace image"), AppSettings::instance().lastOpenDirectory(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp);;All files (*)"));
    if (path.isEmpty())
        return;
    AppSettings::instance().setLastOpenDirectory(QFileInfo(path).absolutePath());

    QString error;
    const QString assetId = m_document.assets()->addImageFile(path, &error);
    if (assetId.isEmpty()) {
        QMessageBox::warning(this, tr("Replace image"),
                             tr("The image could not be imported.\n\n%1").arg(error));
        logFailure(tr("Replacing an image failed."), error);
        return;
    }

    // One undo step for the whole selection, exactly like the property panel.
    const CardSideId side = m_canvas->currentSide();
    CardSide &cardSide = m_document.side(side);
    const QVector<ObjectId> ids = m_canvas->selectionIds();
    const QVector<QPair<ObjectId, QJsonObject>> before =
        ModifyObjectsCommand::captureState(cardSide, ids);
    for (CardObject *object : selection) {
        if (auto *image = dynamic_cast<ImageObject *>(object))
            image->setAssetId(assetId);
    }
    const QVector<QPair<ObjectId, QJsonObject>> after =
        ModifyObjectsCommand::captureState(cardSide, ids);
    if (before != after) {
        m_undoStack->push(new ModifyObjectsCommand(&m_document, side, before, after,
                                                   tr("Replace image")));
        setStatus(tr("Image replaced with \"%1\".").arg(QFileInfo(path).fileName()));
    }
    m_propertyPanel->refresh();
}

void MainWindow::cropSelection()
{
    const QVector<CardObject *> selection = m_canvas->selection();
    if (selection.isEmpty()) {
        setStatus(tr("Select an image or a photograph first."));
        return;
    }
    auto *image = dynamic_cast<ImageObject *>(selection.first());
    if (!image) {
        QMessageBox::information(this, tr("Crop"),
                                 tr("Cropping applies to an image or a photograph. "
                                    "Select one first."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Crop"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(tr("Percent cut away from each edge. The source file is never "
                               "modified."), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *form = new QFormLayout();
    const QRectF source = image->sourceRect();
    auto *left = new QDoubleSpinBox(&dialog);
    auto *top = new QDoubleSpinBox(&dialog);
    auto *right = new QDoubleSpinBox(&dialog);
    auto *bottom = new QDoubleSpinBox(&dialog);
    const double values[] = { source.x() * 100.0, source.y() * 100.0,
                              (1.0 - source.x() - source.width()) * 100.0,
                              (1.0 - source.y() - source.height()) * 100.0 };
    QDoubleSpinBox *boxes[] = { left, top, right, bottom };
    for (int i = 0; i < 4; ++i) {
        boxes[i]->setRange(0.0, 49.0);
        boxes[i]->setDecimals(1);
        boxes[i]->setSuffix(tr(" %"));
        boxes[i]->setValue(values[i]);
    }
    form->addRow(tr("Left"), left);
    form->addRow(tr("Top"), top);
    form->addRow(tr("Right"), right);
    form->addRow(tr("Bottom"), bottom);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const double l = left->value() / 100.0;
    const double t = top->value() / 100.0;
    const double r = right->value() / 100.0;
    const double b = bottom->value() / 100.0;
    const QRectF rect(l, t, qMax(0.01, 1.0 - l - r), qMax(0.01, 1.0 - t - b));

    const QVector<ObjectId> ids = m_canvas->selectionIds();
    const CardSideId side = m_canvas->currentSide();
    CardSide &cardSide = m_document.side(side);
    const QVector<QPair<ObjectId, QJsonObject>> before =
        ModifyObjectsCommand::captureState(cardSide, ids);
    for (CardObject *object : selection) {
        if (auto *objImage = dynamic_cast<ImageObject *>(object))
            objImage->setSourceRect(rect);
    }
    const QVector<QPair<ObjectId, QJsonObject>> after =
        ModifyObjectsCommand::captureState(cardSide, ids);
    if (before != after) {
        m_undoStack->push(new ModifyObjectsCommand(&m_document, side, before, after,
                                                   tr("Crop image")));
        setStatus(tr("Crop applied."));
    }
    m_propertyPanel->refresh();
}

// ---------------------------------------------------------------------------
// Printing
// ---------------------------------------------------------------------------

void MainWindow::openPrinterSettings()
{
    PrinterSettingsDialog dialog(m_printers, PrinterSettingsDialog::Mode::Settings, this);
    dialog.selectPrinter(AppSettings::instance().defaultPrinter());
    dialog.exec();
    updateStatusBar();
}

void MainWindow::openPrinterDiagnostics()
{
    PrinterSettingsDialog dialog(m_printers, PrinterSettingsDialog::Mode::Diagnostics, this);
    dialog.selectPrinter(AppSettings::instance().defaultPrinter());
    dialog.exec();
}

void MainWindow::printTestCard()
{
    const QString printer = AppSettings::instance().defaultPrinter();
    const auto answer = QMessageBox::question(
        this, tr("Print test card"),
        tr("Send a generated test page to \"%1\"?\n\nA card printer will consume one card if "
           "the job is accepted.")
            .arg(printer.isEmpty() ? tr("the simulator (no hardware - a file is written)")
                                   : printer),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const PrinterSettingsDialog::TestPageResult result =
        PrinterSettingsDialog::printTestPage(*m_printers, printer, 300);
    QApplication::restoreOverrideCursor();

    if (result.ok && result.simulated) {
        QMessageBox::information(this, tr("Print test card"),
                                 tr("The card was simulated, not printed. No hardware was "
                                    "used."));
        setStatus(tr("Test page simulated."));
    } else if (result.ok) {
        QMessageBox::information(this, tr("Print test card"),
                                 tr("The printer accepted the test page."));
        setStatus(tr("Test page sent to the printer."));
    } else {
        QMessageBox::warning(this, tr("Print test card"),
                             tr("The test card was not printed.\n\n%1").arg(result.error));
        logFailure(tr("The test card could not be printed."), result.detail);
        setStatus(tr("Print test card failed."));
    }
}

void MainWindow::printCards()
{
    PrintPreviewDialog::Request request;
    request.printerName = AppSettings::instance().defaultPrinter();
    request.copies = 1;
    request.duplex = !m_document.back().isEmpty();
    request.frontEnabled = true;
    request.backEnabled = !m_document.back().isEmpty();

    if (AppSettings::instance().confirmBeforePrint()) {
        printPreview();
        return;
    }

    const auto answer = QMessageBox::question(
        this, tr("Print"),
        tr("Print %1 copy of the card to \"%2\"?")
            .arg(request.copies)
            .arg(request.printerName.isEmpty() ? tr("the simulator") : request.printerName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes)
        runPrintJob(request);
}

void MainWindow::printPreview()
{
    PrintPreviewDialog::Request request;
    request.printerName = AppSettings::instance().defaultPrinter();
    request.copies = 1;
    request.duplex = !m_document.back().isEmpty();
    request.frontEnabled = true;
    request.backEnabled = !m_document.back().isEmpty();
    // Records stay empty: this preview shows the DESIGN, which is what File >
    // Print Preview means. A personalised card is previewed per record in the
    // Personalization panel, which renders through the same BatchRenderer the
    // batch print uses.
    request.records.clear();

    PrintPreviewDialog dialog(m_document, request, m_printers, this);
    connect(&dialog, &PrintPreviewDialog::printConfirmed, this, &MainWindow::runPrintJob);
    dialog.exec();
}

void MainWindow::runBatchPrint(const QVector<int> &records)
{
    if (records.isEmpty())
        return;

    PrintPreviewDialog::Request request;
    request.printerName = AppSettings::instance().defaultPrinter();
    request.copies = 1;
    request.duplex = !m_document.back().isEmpty();
    request.frontEnabled = true;
    request.backEnabled = !m_document.back().isEmpty();
    request.records = records;

    if (AppSettings::instance().confirmBeforePrint()) {
        PrintPreviewDialog dialog(m_document, request, m_printers, this);
        connect(&dialog, &PrintPreviewDialog::printConfirmed, this, &MainWindow::runPrintJob);
        if (dialog.exec() != QDialog::Accepted)
            return;
        request = dialog.request();
    }

    runPrintJob(request);
}

void MainWindow::runPrintJob(const PrintPreviewDialog::Request &request)
{
    // The document is serialised and reloaded inside the worker, so the print
    // job cannot be affected by an edit made while it runs, and no document
    // object is ever read from two threads.
    QByteArray documentBytes;
    const ProjectSerializer::SaveResult snapshot =
        ProjectSerializer::saveToData(m_document, &documentBytes);
    if (!snapshot.ok) {
        QMessageBox::warning(this, tr("Print"),
                             tr("The card could not be prepared for printing.\n\n%1")
                                 .arg(snapshot.error));
        logFailure(tr("Printing could not start."), snapshot.technicalDetail);
        return;
    }

    BatchPrintState state;
    state.total = request.records.isEmpty() ? 1 : request.records.size();

    QProgressDialog progress(state.total > 1 ? tr("Printing %1 cards...").arg(state.total)
                                             : tr("Printing..."),
                             tr("Stop after this card"), 0, state.total, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setValue(0);

    std::thread worker(batchPrintWorker, &state, documentBytes, request.printerName,
                       m_document.geometry().renderDpi(), request,
                       m_personalizationPanel->data(), m_personalizationPanel->mapping());

    QTimer poll;
    connect(&poll, &QTimer::timeout, this, [&] {
        progress.setValue(qMin(state.processed.load(), state.total.load()));
        if (state.done.load())
            progress.accept();
    });
    poll.start(120);
    connect(&progress, &QProgressDialog::canceled, this, [&state, &progress] {
        state.cancel = true;
        progress.setLabelText(tr("Finishing the current card..."));
    });

    if (progress.exec() == QDialog::Rejected)
        state.cancel = true;
    worker.join();
    poll.stop();

    QStringList messages;
    {
        const std::lock_guard<std::mutex> lock(state.mutex);
        messages = state.messages;
    }

    const int printed = state.printed.load();
    const int simulated = state.simulated.load();
    const int failed = state.failed.load();

    QStringList summary;
    if (printed > 0)
        summary << tr("%1 card(s) printed.").arg(printed);
    if (simulated > 0)
        summary << tr("%1 card(s) SIMULATED - no card was printed and no hardware was used.")
                       .arg(simulated);
    if (failed > 0)
        summary << tr("%1 card(s) failed.").arg(failed);
    if (state.cancel.load())
        summary << tr("The remaining cards were not sent.");

    const QString text = summary.isEmpty() ? tr("Nothing was printed.") : summary.join(
                                                 QLatin1Char('\n'));

    if (failed > 0 || (printed == 0 && simulated == 0)) {
        QMessageBox::warning(this, tr("Print"), text + QStringLiteral("\n\n")
                                                   + messages.join(QLatin1Char('\n')));
        setStatus(tr("Printing finished with problems."));
    } else {
        QMessageBox::information(this, tr("Print"),
                                 text + (messages.isEmpty()
                                             ? QString()
                                             : QStringLiteral("\n\n")
                                                   + messages.join(QLatin1Char('\n'))));
        setStatus(printed > 0 ? tr("Printing finished.") : tr("Cards were simulated."));
    }

    for (const QString &message : messages)
        qCWarning(lcPrint) << "print:" << message;
}

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------

void MainWindow::showAbout()
{
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::showDiagnostics()
{
    DiagnosticsDialog dialog(&m_document, this);
    dialog.exec();
    updateWarningIndicator();
}

void MainWindow::showPrinterSupport()
{
    // The printer setup notes ship next to the executable; if they are not
    // there, the essential facts are still given rather than a dead link.
    const QStringList candidates = {
        AppPaths::applicationDir() + QStringLiteral("/docs/PRINTER_SETUP.md"),
        AppPaths::applicationDir() + QStringLiteral("/../docs/PRINTER_SETUP.md"),
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path)) {
            if (QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
                return;
        }
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("Printer support"));
    box.setIcon(QMessageBox::Information);
    box.setText(tr("Card printer support"));
    box.setInformativeText(
        tr("OpenCardCanvas prints through the Windows print spooler using the documented "
           "XPS Card Printer Driver interfaces (BidiSpl for status, options and supplies; "
           "GDI/XPS printing for card output).\n\n"
           "The card printer's driver must be installed by you or your IT department; no "
           "driver software is included with this application.\n\n"
           "Without a printer, the simulator renders cards to a file so the whole workflow "
           "can still be tested - it does not print."));
    auto *openSettings = box.addButton(tr("Open printer settings..."), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == openSettings)
        openPrinterSettings();
}

void MainWindow::showShortcuts()
{
    // Built from the real actions, so the list cannot document a shortcut that
    // does not exist.
    QStringList rows;
    const QList<QAction *> actions = findChildren<QAction *>();
    for (const QAction *action : actions) {
        const QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
        if (shortcut.isEmpty() || action->text().isEmpty())
            continue;
        QString text = action->text();
        text.remove(QLatin1Char('&'));
        rows << QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>")
                    .arg(shortcut.toHtmlEscaped(), text.toHtmlEscaped());
    }
    rows.sort(Qt::CaseInsensitive);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Keyboard shortcuts"));
    dialog.resize(460, 560);
    auto *layout = new QVBoxLayout(&dialog);
    auto *browser = new QTextBrowser(&dialog);
    browser->setHtml(tr("<h3>Keyboard shortcuts</h3><p>These are the shortcuts of the "
                        "current session, taken from the application's own actions.</p>"
                        "<table width=\"100%\" cellspacing=\"4\">%1</table>")
                         .arg(rows.join(QLatin1Char('\n'))));
    layout->addWidget(browser);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

// ---------------------------------------------------------------------------
// Status, selection, recovery and shutdown
// ---------------------------------------------------------------------------

void MainWindow::setStatus(const QString &message)
{
    statusBar()->showMessage(message, 6000);
}

void MainWindow::logFailure(const QString &userMessage, const QString &technicalDetail)
{
    // The user sees the explanation; the log gets the detail.
    qCWarning(lcUi) << userMessage.toUtf8().constData();
    if (!technicalDetail.isEmpty())
        qCWarning(lcUi) << "detail:" << technicalDetail.toUtf8().constData();
}

void MainWindow::updateTitle()
{
    const QString name = m_document.title().isEmpty() ? tr("Untitled card") : m_document.title();
    const QString modified = m_document.isDirty() ? QStringLiteral("*") : QString();
    const QString path = m_document.filePath().isEmpty()
                             ? QString()
                             : QStringLiteral(" [%1]")
                                   .arg(QDir::toNativeSeparators(
                                       QFileInfo(m_document.filePath()).absolutePath()));
    setWindowTitle(tr("%1%2 - OpenCardCanvas%3").arg(name, modified, path));
}

void MainWindow::updateStatusBar()
{
    if (!m_canvas)
        return;

    const CardGeometry &geometry = m_document.geometry();
    m_cardSizeLabel->setText(tr("%1 x %2 mm at %3 dpi")
                                 .arg(QString::number(geometry.widthMm(), 'f', 2),
                                      QString::number(geometry.heightMm(), 'f', 2))
                                 .arg(geometry.renderDpi()));

    const int count = m_canvas->selectionCount();
    if (count == 0)
        m_selectionLabel->setText(tr("No selection"));
    else if (count == 1)
        m_selectionLabel->setText(tr("1 object selected"));
    else
        m_selectionLabel->setText(tr("%1 objects selected").arg(count));

    onZoomChanged(m_canvas->zoomPercent());
}

void MainWindow::updateSelectionState()
{
    if (!m_canvas)
        return;

    const int count = m_canvas->selectionCount();
    const QVector<CardObject *> selection = m_canvas->selection();
    const CardObject *first = selection.isEmpty() ? nullptr : selection.first();

    // Actions that cannot apply are disabled: a disabled menu entry teaches the
    // user what the selection supports, a no-op entry teaches them nothing.
    for (QAction *action : m_objectActions)
        action->setEnabled(count > 0);

    m_actCopy->setEnabled(count > 0);
    m_actCut->setEnabled(count > 0);
    m_actPaste->setEnabled(true);
    m_actDuplicate->setEnabled(count > 0);
    m_actDelete->setEnabled(count > 0);
    m_actGroup->setEnabled(count >= 2);
    m_actUngroup->setEnabled(first && first->type() == ObjectType::Group);

    const bool isImage = first && (first->type() == ObjectType::Image
                                   || first->type() == ObjectType::Photo);
    m_actReplaceImage->setEnabled(isImage);
    m_actCrop->setEnabled(isImage);
    m_actEditText->setEnabled(first && first->type() == ObjectType::Text);

    if (first) {
        m_actLock->setEnabled(!first->isLocked());
        m_actUnlock->setEnabled(first->isLocked());
        m_actHide->setEnabled(first->isVisible());
        m_actShow->setEnabled(!first->isVisible());
    } else {
        m_actLock->setEnabled(false);
        m_actUnlock->setEnabled(false);
        m_actHide->setEnabled(false);
        m_actShow->setEnabled(false);
    }

    updateStatusBar();
}

void MainWindow::updateWarningIndicator()
{
    if (!m_warningButton)
        return;
    const Report report = ProjectValidator::validate(m_document);
    const int errors = report.errorCount();
    const int warnings = report.warningCount();

    if (errors > 0) {
        m_warningButton->setIcon(IconFactory::icon(QStringLiteral("error")));
        m_warningButton->setText(tr("%1 problem(s)").arg(errors));
        m_warningButton->setToolTip(tr("%1 problem(s) in this design. Click for the details.")
                                        .arg(errors));
    } else if (warnings > 0) {
        m_warningButton->setIcon(IconFactory::icon(QStringLiteral("warning")));
        m_warningButton->setText(tr("%1 warning(s)").arg(warnings));
        m_warningButton->setToolTip(tr("%1 warning(s) in this design. Click for the details.")
                                        .arg(warnings));
    } else {
        m_warningButton->setIcon(IconFactory::icon(QStringLiteral("ok")));
        m_warningButton->setText(QString());
        m_warningButton->setToolTip(tr("No problems were found in this design."));
    }
}

void MainWindow::onDocumentChanged()
{
    updateTitle();
    updateStatusBar();
    // Validation is debounced: it walks every object, and a keystroke in a text
    // box must not trigger a full audit.
    m_warningTimer->start();
}

void MainWindow::onSelectionChanged()
{
    updateSelectionState();
    m_propertyPanel->refresh();
    m_alignPanel->refresh();
}

void MainWindow::onToolsToolSelected(const QString &toolId)
{
    // The canvas has no modal tool state: a tool button performs its insertion
    // immediately and the selection tool comes back. Saying so in the status bar
    // is what keeps the palette from looking like it armed something.
    if (toolId == QLatin1String("select")) {
        setStatus(tr("Select tool."));
    } else if (toolId == QLatin1String("text")) {
        m_canvas->addTextObject();
        m_toolsPanel->setCurrentTool(QStringLiteral("select"));
        setStatus(tr("Text object added."));
    } else if (toolId == QLatin1String("qr")) {
        m_canvas->addQrObject();
        m_toolsPanel->setCurrentTool(QStringLiteral("select"));
        setStatus(tr("QR code added."));
    } else if (toolId == QLatin1String("barcode")) {
        m_canvas->addBarcodeObject();
        m_toolsPanel->setCurrentTool(QStringLiteral("select"));
        setStatus(tr("Barcode added."));
    }
}

void MainWindow::onShapeRequested(ShapeKind kind)
{
    m_canvas->addShape(kind);
    m_toolsPanel->setCurrentTool(QStringLiteral("select"));
    setStatus(tr("Shape added."));
}

void MainWindow::onImageRequested()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Add image"), AppSettings::instance().lastOpenDirectory(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp);;All files (*)"));
    m_toolsPanel->setCurrentTool(QStringLiteral("select"));
    if (path.isEmpty())
        return;
    AppSettings::instance().setLastOpenDirectory(QFileInfo(path).absolutePath());
    m_canvas->addImageFromFile(path);
    setStatus(tr("Image added."));
}

void MainWindow::onPhotoRequested()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Add photograph"), AppSettings::instance().lastOpenDirectory(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp);;All files (*)"));
    m_toolsPanel->setCurrentTool(QStringLiteral("select"));
    if (path.isEmpty())
        return;
    AppSettings::instance().setLastOpenDirectory(QFileInfo(path).absolutePath());
    m_canvas->addPhotoFromFile(path);
    setStatus(tr("Photograph added."));
}

void MainWindow::checkRecovery()
{
    const QVector<AutoSave::RecoveryCandidate> candidates = AutoSave::findRecoverableSessions();
    if (candidates.isEmpty())
        return;

    // The newest session is the interesting one.
    AutoSave::RecoveryCandidate candidate = candidates.first();
    for (const AutoSave::RecoveryCandidate &other : candidates) {
        if (other.timestamp > candidate.timestamp)
            candidate = other;
    }

    const QString project = candidate.projectPath.isEmpty()
                                ? tr("an unsaved card")
                                : QFileInfo(candidate.projectPath).fileName();
    const auto answer = QMessageBox::question(
        this, tr("Recover work"),
        tr("OpenCardCanvas did not shut down cleanly last time.\n\n"
           "A recovery copy of %1 from %2 is available.\n\n"
           "Open the recovery copy? Your original project file will not be touched.")
            .arg(project, QLocale::system().toString(candidate.timestamp,
                                                      QLocale::ShortFormat)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        AutoSave::discardSession(candidate.sessionId);
        return;
    }

    QString error;
    m_undoStack->clear();
    if (!AutoSave::openRecovery(candidate, &m_document, &error)) {
        QMessageBox::warning(this, tr("Recover work"),
                             tr("The recovery copy could not be opened.\n\n%1").arg(error));
        logFailure(tr("Opening the recovery copy failed."), error);
        return;
    }

    // Deliberately nameless: the recovered document must be saved under a new
    // name so the original project on disk cannot be overwritten by accident.
    m_document.setFilePath(QString());
    m_document.setDirty(true);
    m_canvasHost->setDocument(&m_document);
    m_canvasHost->fitToWindow();
    m_propertyPanel->setDocument(&m_document);
    m_alignPanel->setDocument(&m_document);
    m_layersPanel->setDocument(&m_document);
    m_personalizationPanel->setDocument(&m_document);
    updateTitle();
    updateStatusBar();
    updateWarningIndicator();

    setStatus(tr("Recovered the auto-saved copy. Use Save As to keep it; the original project "
                 "has not been modified."));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }

    AppSettings &settings = AppSettings::instance();
    settings.setWindowGeometry(saveGeometry());
    // The leading byte is the layout version, so a state written by an older
    // build is never restored (see restoreSession()).
    QByteArray state = saveState();
    state.prepend(char(kLayoutVersion));
    settings.setWindowState(state);
    settings.sync();

    if (m_autoSave) {
        m_autoSave->stop();
        m_autoSave->detach();
    }

    // A clean shutdown deletes the lock file, which is exactly how the next
    // start knows that nothing crashed.
    if (!m_sessionId.isEmpty())
        AutoSave::discardSession(m_sessionId);

    event->accept();
}

} // namespace occ
