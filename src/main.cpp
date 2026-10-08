// ---------------------------------------------------------------------------
// OpenCardCanvas - application entry point.
//
// In order: establish application identity (so QSettings/QStandardPaths resolve
// correctly), set up high DPI behaviour for Windows 10/11 at 100-200 % scaling,
// initialise logging, make sure the per-user directories exist, install a
// translation when one is available for the system locale, then run the window.
//
// Failures during start-up are reported through a message box that explains what
// happened and what to do, while the technical detail goes to the log file.
// ---------------------------------------------------------------------------
#include "occ/Version.h"

#include "printing/PrinterManager.h"
#include "ui/MainWindow.h"
#include "ui/IconFactory.h"
#include "utils/AppPaths.h"
#include "utils/Logger.h"
#include "utils/Settings.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QStyleFactory>
#include <QTextStream>
#include <QTranslator>

#include <cstdio>
#include <exception>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

using namespace occ;

namespace {

// Shows a message box instead of the usual silent abort when an unhandled
// exception escapes during start-up or shutdown.
void installTerminateHandler()
{
    std::set_terminate([] {
        QString detail;
        try {
            if (std::current_exception())
                std::rethrow_exception(std::current_exception());
        } catch (const std::exception &ex) {
            detail = QString::fromUtf8(ex.what());
        } catch (...) {
            detail = QStringLiteral("unknown exception");
        }
        qCritical("Unhandled exception, aborting. Detail: %s",
                  qPrintable(Logger::redact(detail)));
        QMessageBox::critical(
            nullptr, QCoreApplication::translate("main", "OpenCardCanvas"),
            QCoreApplication::translate(
                "main",
                "OpenCardCanvas had to stop because of an unexpected problem.\n\n"
                "If the project had unsaved changes it was auto-saved; the recovery "
                "folder is listed under Help > Diagnostics."));
        std::abort();
    });
}

// English is the source language. Other languages are dropped into a
// "translations" folder next to the executable and picked up for the system
// locale, so adding a language needs no code change.
void setupTranslations(QApplication &app)
{
    static QTranslator qtTranslator;
    static QTranslator appTranslator;

    const QString dir =
        AppPaths::applicationDir() + QDir::separator() + QStringLiteral("translations");
    const QString locale = QLocale::system().name();

    if (qtTranslator.load(QStringLiteral("qtbase_") + locale, dir))
        app.installTranslator(&qtTranslator);
    if (appTranslator.load(QStringLiteral("OpenCardCanvas_") + locale, dir))
        app.installTranslator(&appTranslator);
}

} // namespace

// ---------------------------------------------------------------------------
// Console printer diagnostics.
//
// These two modes exist so that printer hardware can be checked without going
// near the GUI: they enumerate the queues, report which backend each one will
// use, and print the full device/SDK report for one printer. That is the fastest
// way to find out whether a card printer is correctly installed and whether the
// XPS Card Printer driver is answering.
// ---------------------------------------------------------------------------
namespace {

QString backendName(PrinterBackendKind kind)
{
    switch (kind) {
    case PrinterBackendKind::EntrustXps: return QStringLiteral("Entrust XPS card printer");
    case PrinterBackendKind::Windows:    return QStringLiteral("Windows printer");
    case PrinterBackendKind::Simulator:  return QStringLiteral("Simulator (no hardware)");
    }
    return QStringLiteral("unknown");
}

// A GUI-subsystem executable has no console of its own, so the diagnostic modes
// attach to the terminal they were launched from. If that is not possible the
// report is still written to a file, which is what makes the output reachable
// even when the tool is started from a shortcut.
void attachToParentConsole()
{
#ifdef Q_OS_WIN
    if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *stream = nullptr;
        (void)freopen_s(&stream, "CONOUT$", "w", stdout);
        (void)freopen_s(&stream, "CONOUT$", "w", stderr);
    }
#endif
}

void emitReport(const QString &report)
{
    const QString path = AppPaths::logsDir() + QDir::separator()
                         + QStringLiteral("printer-report.txt");
    if (AppPaths::ensureDir(AppPaths::logsDir())) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream out(&file);
            out << report;
            file.close();
        }
    }

    std::fputs(qPrintable(report), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);

    std::fprintf(stderr, "\nA copy of this report was written to:\n  %s\n",
                 qPrintable(QDir::toNativeSeparators(path)));
    std::fflush(stderr);
}

int runPrinterDiagnostics(PrinterManager &manager, bool listOnly, const QString &reportFor)
{
    QStringList out;
    const auto line = [&out](const QString &text) { out << text; };

    const QVector<PrinterIdentity> printers = manager.refresh();
    const QString rule(78, QLatin1Char('-'));

    line(QStringLiteral("Printers found: %1").arg(printers.size()));
    line(rule);
    for (const PrinterIdentity &p : printers) {
        line(QStringLiteral("Name         : %1").arg(p.name));
        line(QStringLiteral("  Backend    : %1").arg(backendName(p.backend)));
        line(QStringLiteral("  Port       : %1").arg(p.port));
        line(QStringLiteral("  Driver     : %1").arg(p.driverName));
        if (!p.model.isEmpty())
            line(QStringLiteral("  Model      : %1").arg(p.model));
        if (!p.serialNumber.isEmpty())
            line(QStringLiteral("  Serial     : %1").arg(p.serialNumber));
        line(QStringLiteral("  Duplex     : %1")
                 .arg(p.duplexKnown ? (p.duplexSupported ? QStringLiteral("yes")
                                                         : QStringLiteral("no"))
                                    : QStringLiteral("not available")));
        line(QStringLiteral("  Card job   : %1")
                 .arg(p.backend == PrinterBackendKind::EntrustXps
                          ? QStringLiteral("yes (XPS Card Printer driver detected)")
                          : QStringLiteral("treated as a plain Windows print queue")));
    }
    line(rule);
    if (!manager.lastError().isEmpty())
        line(QStringLiteral("Note: %1").arg(manager.lastError()));

    // The simulator is always available, so say so rather than leaving the
    // impression that nothing can be done without hardware.
    line(QString());
    line(QStringLiteral("Simulator backend: %1")
             .arg(manager.simulator()
                      ? QStringLiteral("available (renders to a file; does NOT print)")
                      : QStringLiteral("unavailable")));

    if (listOnly) {
        emitReport(out.join(QLatin1Char('\n')));
        return printers.isEmpty() ? 3 : 0;
    }

    QString name = reportFor;
    if (name.isEmpty() && !printers.isEmpty())
        name = printers.first().name;
    if (name.isEmpty()) {
        line(QString());
        line(QStringLiteral("No printer available to report on. Install the printer, then run:"));
        line(QStringLiteral("  OpenCardCanvas.exe --printer-report \"XPS Card Printer\""));
        emitReport(out.join(QLatin1Char('\n')));
        return 3;
    }

    const CardPrinterPtr printer = manager.printer(name);
    if (!printer) {
        line(QString());
        line(QStringLiteral("The printer \"%1\" is not available: %2")
                 .arg(name, manager.lastError().isEmpty() ? QStringLiteral("not found")
                                                          : manager.lastError()));
        emitReport(out.join(QLatin1Char('\n')));
        return 3;
    }

    line(QString());
    line(QStringLiteral("===== Report for \"%1\" =====").arg(name));
    const bool connected = printer->connect();
    line(QStringLiteral("connect()      : %1").arg(connected ? QStringLiteral("ok")
                                                             : QStringLiteral("FAILED")));
    if (!connected)
        line(QStringLiteral("lastError()    : %1").arg(printer->lastError()));

    const PrinterStatus status = printer->status();
    QString statusLine = QStringLiteral("status         : %1")
                             .arg(status.stateText.isEmpty() ? QStringLiteral("(none)")
                                                             : status.stateText);
    if (!status.detail.isEmpty())
        statusLine += QStringLiteral("  ") + status.detail;
    line(statusLine);
    line(QStringLiteral("iBidi          : %1")
             .arg(status.iBidiAvailable ? QStringLiteral("available")
                                        : QStringLiteral("not available on this queue")));
    if (!status.sdkVersion.isEmpty())
        line(QStringLiteral("SDK version    : %1").arg(status.sdkVersion));

    line(QString());
    line(QStringLiteral("--- diagnostics report ---"));
    line(printer->diagnosticsReport());

    emitReport(out.join(QLatin1Char('\n')));
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    // High DPI policy must be set BEFORE the QGuiApplication instance exists
    // (Qt 6.8 warns and ignores it otherwise). Fractional scaling is honoured
    // as-is, so a design stays proportional at 125 % and 150 %, not only at
    // 100 % and 200 %.
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QApplication::setWindowIcon(IconFactory::icon(QStringLiteral("logo")));

    QApplication::setOrganizationName(QStringLiteral("OpenCardCanvas"));
    QApplication::setOrganizationDomain(QStringLiteral("opencardcanvas.local"));
    QApplication::setApplicationName(QStringLiteral("OpenCardCanvas"));
    QApplication::setApplicationVersion(QStringLiteral(OCC_VERSION_STRING));
    QApplication::setApplicationDisplayName(QStringLiteral("OpenCardCanvas"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate(
        "main", "Design, personalize, preview and print ID cards."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption verboseOption(
        QStringLiteral("verbose"),
        QCoreApplication::translate("main",
                                    "Write detailed diagnostics to the log file."));
    parser.addOption(verboseOption);
    const QCommandLineOption logsOption(
        QStringLiteral("show-logs"),
        QCoreApplication::translate("main", "Print the log file location and exit."));
    parser.addOption(logsOption);
    const QCommandLineOption listPrintersOption(
        QStringLiteral("list-printers"),
        QCoreApplication::translate(
            "main", "List the installed printers, the backend each will use, and exit."));
    parser.addOption(listPrintersOption);
    const QCommandLineOption printerReportOption(
        QStringLiteral("printer-report"),
        QCoreApplication::translate(
            "main",
            "Print the full device and SDK report for a printer (or the default one) and exit."),
        QStringLiteral("name"));
    parser.addOption(printerReportOption);
    parser.addPositionalArgument(
        QStringLiteral("project"),
        QCoreApplication::translate("main", "Optional .occard project to open."));
    parser.process(app);

    if (parser.isSet(logsOption)) {
        AppPaths::ensureAllDirectories();
        std::printf("%s\n", qPrintable(QDir::toNativeSeparators(Logger::logFilePath())));
        return 0;
    }

    Logger::setVerbose(parser.isSet(verboseOption)
                       || AppSettings::instance().verboseLogging());
    installTerminateHandler();

    QString dirError;
    if (!AppPaths::ensureAllDirectories(&dirError)) {
        QMessageBox::critical(nullptr,
                              QCoreApplication::translate("main", "OpenCardCanvas"), dirError);
        return 2;
    }
    Logger::init();
    qInfo("OpenCardCanvas %s starting", OCC_VERSION_STRING);

    // Console printer diagnostics: no window is created for these.
    if (parser.isSet(listPrintersOption) || parser.isSet(printerReportOption)) {
        attachToParentConsole();
        PrinterManager manager;
        const int code = runPrinterDiagnostics(manager, !parser.isSet(printerReportOption),
                                               parser.value(printerReportOption));
        qInfo("Printer diagnostics finished with code %d", code);
        Logger::shutdown();
        return code;
    }

    setupTranslations(app);

    int exitCode = 0;
    try {
        MainWindow window;
        window.show();

        const QStringList positional = parser.positionalArguments();
        if (!positional.isEmpty())
            window.openProjectFile(positional.first());

        exitCode = app.exec();
    } catch (const std::exception &ex) {
        const QString detail = QString::fromUtf8(ex.what());
        qCritical("Fatal error while running: %s", qPrintable(Logger::redact(detail)));
        QMessageBox::critical(
            nullptr, QCoreApplication::translate("main", "OpenCardCanvas"),
            QCoreApplication::translate(
                "main",
                "OpenCardCanvas could not continue.\n\n"
                "Technical details have been written to the log file."));
        exitCode = 3;
    }

    qInfo("OpenCardCanvas exiting with code %d", exitCode);
    Logger::shutdown();
    return exitCode;
}
