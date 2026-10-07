#pragma once

#include "printing/PrinterTypes.h"

#include <QDialog>
#include <QImage>
#include <QString>
#include <QStringList>

class QPlainTextEdit;
class QPushButton;
class QTableWidget;

// ---------------------------------------------------------------------------
// PrinterSettingsDialog - what printers exist, what they really are, and what
// they really say.
//
// Two modes share one window:
//
//   Settings     pick the default printer; the choice is written to
//                AppSettings::setDefaultPrinter()
//   Diagnostics  the same window with the diagnostics pane in front, used by
//                Print > Printer Diagnostics
//
// Nothing in this dialog guesses. Backendl detection asks the driver, status
// comes from the driver, and where a device does not report something the pane
// says so. The simulator is always listed, and it is labelled unmistakably as
// something that renders a file and does not print.
//
// The printer test page is generated here and is also used by the Print menu,
// so "Print Test Card" and "Printer test" produce the same sheet.
// ---------------------------------------------------------------------------
namespace occ {

class PrinterManager;

class PrinterSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Settings, Diagnostics };

    explicit PrinterSettingsDialog(PrinterManager *manager,
                                   Mode mode = Mode::Settings,
                                   QWidget *parent = nullptr);

    // Windows printer name of the selected row, or an empty string for the
    // simulator.
    QString selectedPrinter() const;
    void selectPrinter(const QString &printerName);
    QPlainTextEdit *diagnosticsPane() const { return m_diagnostics; }

    struct TestPageResult
    {
        bool    ok = false;
        bool    simulated = false;
        QString error;         // user facing, empty when ok
        QString detail;        // technical detail for the log
    };

    // Prints the generated test page through `printerName`; an empty name means
    // the simulator. Shared with the Print menu.
    static TestPageResult printTestPage(PrinterManager &manager,
                                        const QString &printerName,
                                        int dpi = 300);
    // The generated sheet, drawn at `dpi`. Exposed so the caller can log or
    // display exactly what was sent.
    static QImage buildTestPage(int dpi);

private:
    void buildUi();
    void refreshPrinters();
    void updateSelection();
    void appendDiagnostics(const QString &title, const QString &text);
    void showDriverInformation();
    void showSdkInformation();
    void testConnection();
    void runPrinterTest();
    QString backendLabel(PrinterBackendKind kind) const;

    PrinterManager *m_manager = nullptr;
    Mode            m_mode = Mode::Settings;

    QTableWidget   *m_table = nullptr;
    QPlainTextEdit *m_diagnostics = nullptr;
    QPushButton    *m_refresh = nullptr;
    QPushButton    *m_testConnection = nullptr;
    QPushButton    *m_testPage = nullptr;
    QPushButton    *m_driverInfo = nullptr;
    QPushButton    *m_sdkInfo = nullptr;
};

} // namespace occ
