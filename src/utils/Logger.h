// ---------------------------------------------------------------------------
// Logger - structured application logging.
//
// Writes to:
//   %LOCALAPPDATA%/OpenCardCanvas/logs/OpenCardCanvas.log   (current)
//   %LOCALAPPDATA%/OpenCardCanvas/logs/OpenCardCanvas.1.log (previous, rotated)
//
// Installs a Qt message handler so that qDebug/qWarning/qCritical/qFatal and
// Qt-internal messages all end up in the same file. Log files are diagnostic
// only: cardholder data must never be logged (see redact()).
// ---------------------------------------------------------------------------
#pragma once

#include <QLoggingCategory>
#include <QString>

namespace occ {

Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcProject)
Q_DECLARE_LOGGING_CATEGORY(lcRender)
Q_DECLARE_LOGGING_CATEGORY(lcPrint)
Q_DECLARE_LOGGING_CATEGORY(lcPrintSdk)
Q_DECLARE_LOGGING_CATEGORY(lcData)
Q_DECLARE_LOGGING_CATEGORY(lcUi)

class Logger
{
public:
    enum class Level { Debug, Info, Warning, Critical };

    // Initialises the log file and installs the Qt message handler.
    // Returns false when the log file could not be opened (the application
    // still runs: messages then go to the debugger/stdout only).
    static bool init();
    static void shutdown();

    static bool isInitialised();
    static QString logFilePath();
    static QString logDirectory();

    // Explicit logging entry point (also used by the Qt handler).
    static void write(Level level, const QString &category, const QString &message);

    // Removes obvious personal data from a string before it reaches the log.
    // Card designs may contain names, photographs and identifiers; those must
    // never be persisted to disk.
    static QString redact(const QString &text);

    // Enables verbose (debug-level) logging for this run.
    static void setVerbose(bool verbose);
    static bool isVerbose();
};

} // namespace occ
