#pragma once

#include "core/CardDocument.h"
#include "personalization/DataMapper.h"
#include "ui/PrintPreviewDialog.h"

#include <QMainWindow>
#include <QVector>

class QAction;
class QComboBox;
class QDockWidget;
class QLabel;
class QMenu;
class QTimer;
class QToolBar;
class QToolButton;
class QUndoStack;

namespace occ {

class AlignPanel;
class AutoSave;
class CanvasHost;
class CardCanvas;
class LayersPanel;
class PersonalizationPanel;
class PrinterManager;
class PropertyPanel;
class ToolsPanel;

// ---------------------------------------------------------------------------
// MainWindow - the application shell.
//
// It owns exactly one document, one undo stack and one printer manager. The
// panels talk to the canvas and the undo stack, the canvas talks to the
// document, and nothing else holds a second copy of anything.
//
// Three responsibilities deserve naming:
//
//   * every menu entry, toolbar button and shortcut in the specification is
//     wired to real behaviour. There are no placeholder actions, and an action
//     that cannot apply is disabled rather than silently doing nothing.
//   * unsaved work is protected at every exit point (New, Open, Close), and a
//     crashed session is offered back without ever writing to the user's project
//     file.
//   * printing goes through BatchRenderer + CardRenderer + ICardPrinter on a
//     worker thread with real progress, and the completion message distinguishes
//     a printed card from a simulated one.
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Opens a project file; used by main.cpp for the command line argument.
    void openProjectFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // Dock arrangement version. Bump this whenever the default layout changes:
    // a saved state from an older version is then ignored instead of restoring
    // a layout that no longer matches (see restoreSession()).
    static constexpr int kLayoutVersion = 3;

    // --- construction -------------------------------------------------------
    void createDocument();
    void createActions();
    void createMenus();
    void addAlignActions(QMenu *menu);
    void createToolBars();
    void createDocks();
    // Adds and arranges the docks exactly as the product specifies. Used by both
    // createDocks() and resetLayout() so the two can never drift apart.
    void applyDefaultDockLayout();
    void createStatusBar();
    void wirePanels();
    void wireActions();
    void applySettingsToViews();
    void restoreSession();

    // --- file ---------------------------------------------------------------
    void newDocument();
    void newFromTemplate();
    void openProjectDialog();
    void openProjectFileInternal(const QString &path);
    void saveProject();
    void saveProjectAs();
    bool maybeSave();
    void importImage();
    void importData();
    void exportDocument(const QString &format);
    void saveAsTemplate();
    void manageTemplates();
    void refreshRecentMenu();
    // Card size and orientation for the open document, applied undoably.
    void changeCardGeometry();

    // --- editing ------------------------------------------------------------
    void replaceSelectedImage();
    void cropSelection();
    void editSelectedText();

    // --- view ---------------------------------------------------------------
    void resetLayout();
    void setSide(CardSideId side);
    void onZoomChanged(double percent);
    void applyZoomPercent(double percent);

    // --- printing -----------------------------------------------------------
    void printCards();
    void printPreview();
    void openPrinterSettings();
    void openPrinterDiagnostics();
    void printTestCard();
    void runPrintJob(const PrintPreviewDialog::Request &request);
    void runBatchPrint(const QVector<int> &records);

    // --- help ---------------------------------------------------------------
    void showAbout();
    void showDiagnostics();
    void showPrinterSupport();
    void showShortcuts();

    // --- status / housekeeping ---------------------------------------------
    void updateTitle();
    void updateStatusBar();
    void updateSelectionState();
    void updateWarningIndicator();
    void onDocumentChanged();
    void onSelectionChanged();
    void onToolsToolSelected(const QString &toolId);
    void onShapeRequested(ShapeKind kind);
    void onImageRequested();
    void onPhotoRequested();
    void checkRecovery();
    void logFailure(const QString &userMessage, const QString &technicalDetail);
    void setStatus(const QString &message);

private:
    CardDocument     m_document;
    AutoSave        *m_autoSave = nullptr;
    PrinterManager  *m_printers = nullptr;
    QUndoStack      *m_undoStack = nullptr;

    CanvasHost           *m_canvasHost = nullptr;
    CardCanvas           *m_canvas = nullptr;
    ToolsPanel           *m_toolsPanel = nullptr;
    PropertyPanel        *m_propertyPanel = nullptr;
    AlignPanel           *m_alignPanel = nullptr;
    LayersPanel          *m_layersPanel = nullptr;
    PersonalizationPanel *m_personalizationPanel = nullptr;

    QDockWidget *m_toolsDock = nullptr;
    QDockWidget *m_propertyDock = nullptr;
    QDockWidget *m_alignDock = nullptr;
    QDockWidget *m_layersDock = nullptr;
    QDockWidget *m_personalizationDock = nullptr;

    QToolBar *m_mainToolBar = nullptr;
    QToolBar *m_arrangeToolBar = nullptr;

    // Status bar
    QToolButton *m_frontButton = nullptr;
    QToolButton *m_backButton = nullptr;
    QComboBox   *m_zoomCombo = nullptr;
    QLabel      *m_cardSizeLabel = nullptr;
    QLabel      *m_selectionLabel = nullptr;
    QToolButton *m_warningButton = nullptr;
    QTimer      *m_warningTimer = nullptr;

    QMenu *m_recentMenu = nullptr;

    QAction *m_actNew = nullptr;
    QAction *m_actNewTemplate = nullptr;
    QAction *m_actOpen = nullptr;
    QAction *m_actSave = nullptr;
    QAction *m_actSaveAs = nullptr;
    QAction *m_actCardSize = nullptr;
    QAction *m_actImportImage = nullptr;
    QAction *m_actImportData = nullptr;
    QAction *m_actSaveTemplate = nullptr;
    QAction *m_actPrint = nullptr;
    QAction *m_actPrintPreview = nullptr;
    QAction *m_actPrinterSettings = nullptr;
    QAction *m_actPrinterDiagnostics = nullptr;
    QAction *m_actTestCard = nullptr;
    QAction *m_actPreferences = nullptr;
    QAction *m_actExit = nullptr;

    QAction *m_actUndo = nullptr;
    QAction *m_actRedo = nullptr;
    QAction *m_actCut = nullptr;
    QAction *m_actCopy = nullptr;
    QAction *m_actPaste = nullptr;
    QAction *m_actDuplicate = nullptr;
    QAction *m_actDelete = nullptr;
    QAction *m_actSelectAll = nullptr;

    QAction *m_actGroup = nullptr;
    QAction *m_actUngroup = nullptr;
    QAction *m_actLock = nullptr;
    QAction *m_actUnlock = nullptr;
    QAction *m_actHide = nullptr;
    QAction *m_actShow = nullptr;
    QAction *m_actEditText = nullptr;
    QAction *m_actReplaceImage = nullptr;
    QAction *m_actCrop = nullptr;

    QAction *m_actZoomIn = nullptr;
    QAction *m_actZoomOut = nullptr;
    QAction *m_actZoomFit = nullptr;
    QAction *m_actZoomActual = nullptr;
    QAction *m_actRulers = nullptr;
    QAction *m_actGuides = nullptr;
    QAction *m_actGrid = nullptr;
    QAction *m_actSnapCard = nullptr;
    QAction *m_actSnapGuides = nullptr;
    QAction *m_actSnapObjects = nullptr;
    QAction *m_actSnapGrid = nullptr;
    QAction *m_actResetLayout = nullptr;

    QAction *m_actBringFront = nullptr;
    QAction *m_actBringForward = nullptr;
    QAction *m_actSendBackward = nullptr;
    QAction *m_actSendBack = nullptr;

    // Everything that only makes sense with a selection.
    QVector<QAction *> m_objectActions;
    QString            m_sessionId;
};

} // namespace occ
