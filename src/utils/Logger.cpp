#include "utils/Logger.h"

#include "utils/AppPaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QTextStream>

#include <cstdio>
#include <cstdlib>

namespace occ {

Q_LOGGING_CATEGORY(lcApp, "occ.app")
Q_LOGGING_CATEGORY(lcProject, "occ.project")
Q_LOGGING_CATEGORY(lcRender, "occ.render")
Q_LOGGING_CATEGORY(lcPrint, "occ.print")
Q_LOGGING_CATEGORY(lcPrintSdk, "occ.print.sdk")
Q_LOGGING_CATEGORY(lcData, "occ.data")
Q_LOGGING_CATEGORY(lcUi, "occ.ui")

namespace {

QMutex           g_mutex;
QFile           *g_file = nullptr;
QTextStream     *g_stream = nullptr;
QtMessageHandler g_previousHandler = nullptr;
bool             g_initialised = false;
bool             g_verbose = false;

constexpr qint64 kMaxLogBytes = 4 * 1024 * 1024;   // rotate at 4 MiB

QString levelName(Logger::Level level)
{
    switch (level) {
    case Logger::Level::Debug:    return QStringLiteral("DEBUG");
    case Logger::Level::Info:     return QStringLiteral("INFO ");
    case Logger::Level::Warning:  return QStringLiteral("WARN ");
    case Logger::Level::Critical: return QStringLiteral("CRIT");
    }
    return QStringLiteral("INFO ");
}

bool openLogFileUnlocked()
{
    if (g_file)
        return true;

    const QString dir = AppPaths::logsDir();
    if (!AppPaths::ensureDir(dir))
        return false;

    const QString path = dir + QDir::separator() + QStringLiteral("OpenCardCanvas.log");
    const QString rotated = dir + QDir::separator() + QStringLiteral("OpenCardCanvas.1.log");

    // Simple size based rotation: keep exactly one previous log file.
    const QFileInfo info(path);
    if (info.exists() && info.size() > kMaxLogBytes) {
        QFile::remove(rotated);
        QFile::rename(path, rotated);
    }

    auto *file = new QFile(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete file;
        return false;
    }
    g_file = file;
    g_stream = new QTextStream(g_file);
    return true;
}

void writeBannerUnlocked()
{
    if (!g_stream)
        return;
    *g_stream << Qt::endl;
    *g_stream << "==================================================================" << Qt::endl;
    *g_stream << "OpenCardCanvas " << QCoreApplication::applicationVersion()
              << " session started "
              << QDateTime::currentDateTime().toString(Qt::ISODate)
              << " (pid " << QCoreApplication::applicationPid() << ")" << Qt::endl;
    *g_stream << "Qt " << QT_VERSION_STR << " | built " << __DATE__ << " " << __TIME__ << Qt::endl;
    *g_stream << "==================================================================" << Qt::endl;
    g_stream->flush();
}

Logger::Level levelFromQt(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return Logger::Level::Debug;
    case QtInfoMsg:     return Logger::Level::Info;
    case QtWarningMsg:  return Logger::Level::Warning;
    case QtCriticalMsg: return Logger::Level::Critical;
    case QtFatalMsg:    return Logger::Level::Critical;
    }
    return Logger::Level::Info;
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const char *category = context.category ? context.category : "default";
    Logger::write(levelFromQt(type), QString::fromLatin1(category), message);

    if (g_previousHandler)
        g_previousHandler(type, context, message);
    else if (type == QtFatalMsg)
        std::abort();
}

} // namespace

bool Logger::init()
{
    QMutexLocker locker(&g_mutex);
    if (g_initialised)
        return g_file != nullptr;

    g_initialised = true;
    const bool ok = openLogFileUnlocked();
    g_previousHandler = qInstallMessageHandler(messageHandler);
    writeBannerUnlocked();
    return ok;
}

void Logger::shutdown()
{
    QMutexLocker locker(&g_mutex);
    if (!g_initialised)
        return;
    if (g_stream)
        g_stream->flush();
    qInstallMessageHandler(g_previousHandler);
    g_previousHandler = nullptr;
    delete g_stream;
    g_stream = nullptr;
    if (g_file) {
        g_file->close();
        delete g_file;
        g_file = nullptr;
    }
    g_initialised = false;
}

bool Logger::isInitialised()
{
    return g_initialised;
}

QString Logger::logFilePath()
{
    return AppPaths::logsDir() + QDir::separator() + QStringLiteral("OpenCardCanvas.log");
}

QString Logger::logDirectory()
{
    return AppPaths::logsDir();
}

void Logger::write(Level level, const QString &category, const QString &message)
{
    const QString line = QStringLiteral("%1 [%2] [%3] %4")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             levelName(level), category, message);

    {
        QMutexLocker locker(&g_mutex);
        if (openLogFileUnlocked() && g_stream) {
            *g_stream << line << Qt::endl;
            g_stream->flush();
        }
    }

    // Mirror to stderr so developers see output live. Debug level stays out of
    // the console unless verbose mode was requested, to keep it readable.
    if (level != Level::Debug || g_verbose) {
        const QByteArray utf8 = line.toUtf8();
        std::fwrite(utf8.constData(), 1, size_t(utf8.size()), stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
    }
}

QString Logger::redact(const QString &text)
{
    static const QRegularExpression email(
        QStringLiteral(R"([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,})"));
    static const QRegularExpression longDigits(QStringLiteral(R"(\b\d{6,}\b)"));

    QString result = text;
    result.replace(email, QStringLiteral("<redacted-email>"));
    result.replace(longDigits, QStringLiteral("<redacted-number>"));
    return result;
}

void Logger::setVerbose(bool verbose)
{
    g_verbose = verbose;
    QLoggingCategory::setFilterRules(verbose ? QStringLiteral("occ.*=true")
                                             : QStringLiteral("occ.*.debug=false"));
}

bool Logger::isVerbose()
{
    return g_verbose;
}

} // namespace occ
