#include "project/AutoSave.h"

#include "core/CardDocument.h"
#include "occ/Version.h"
#include "project/ProjectSerializer.h"
#include "utils/AppPaths.h"
#include "utils/Logger.h"
#include "utils/SimpleZip.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTimer>

namespace occ {

namespace {

QString autoSaveTr(const char *text)
{
    return QCoreApplication::translate("AutoSave", text);
}

constexpr int kMinIntervalMinutes = 1;
constexpr int kMaxIntervalMinutes = 120;
// How long after the first edit of a session the early snapshot is taken.
constexpr int kFirstSnapshotDelayMs = 30 * 1000;

QString lockFileName(const QString &sessionId)
{
    return QStringLiteral("session_%1.lock").arg(sessionId);
}

QString autosaveFileName(const QString &sessionId)
{
    return QStringLiteral("autosave_%1.occard").arg(sessionId);
}

QString sidecarFileName(const QString &sessionId)
{
    return QStringLiteral("autosave_%1.json").arg(sessionId);
}

// Extracts the session id from "autosave_<id>.occard" / "session_<id>.lock".
QString sessionIdFromFileName(const QString &fileName, const QString &prefix,
                             const QString &suffix)
{
    if (!fileName.startsWith(prefix) || !fileName.endsWith(suffix))
        return QString();
    return fileName.mid(prefix.size(), fileName.size() - prefix.size() - suffix.size());
}

bool sidecarLookup(const QString &sessionId, QJsonObject *out)
{
    QFile file(QDir(AppPaths::autosaveDir()).filePath(sidecarFileName(sessionId)));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;
    if (out)
        *out = document.object();
    return true;
}

} // namespace

AutoSave::AutoSave(QObject *parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(m_intervalMinutes * 60 * 1000);
    connect(m_timer, &QTimer::timeout, this, &AutoSave::onTimer);

    m_idleCoalesce = new QTimer(this);
    m_idleCoalesce->setSingleShot(true);
    m_idleCoalesce->setInterval(kFirstSnapshotDelayMs);
    connect(m_idleCoalesce, &QTimer::timeout, this, &AutoSave::onTimer);
}

AutoSave::~AutoSave()
{
    detach();
}

QString AutoSave::sessionId() const
{
    return m_sessionId;
}

QString AutoSave::autosavePath() const
{
    if (m_sessionId.isEmpty())
        return QString();
    return QDir(AppPaths::autosaveDir()).filePath(autosaveFileName(m_sessionId));
}

QString AutoSave::lockFilePath() const
{
    if (m_sessionId.isEmpty())
        return QString();
    return QDir(AppPaths::crashDir()).filePath(lockFileName(m_sessionId));
}

void AutoSave::writeLockFile()
{
    const QString path = lockFilePath();
    if (path.isEmpty())
        return;
    if (!AppPaths::ensureDir(AppPaths::crashDir())) {
        qCWarning(lcProject, "AutoSave: could not create the crash-recovery directory");
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcProject, "AutoSave: could not write the session lock file");
        return;
    }
    QJsonObject lock;
    lock.insert(QStringLiteral("sessionId"), m_sessionId);
    lock.insert(QStringLiteral("applicationVersion"),
                QString::fromLatin1(OCC_VERSION_STRING));
    lock.insert(QStringLiteral("started"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (m_doc)
        lock.insert(QStringLiteral("projectPath"), m_doc->filePath());
    file.write(QJsonDocument(lock).toJson(QJsonDocument::Compact));
    file.close();
}

void AutoSave::removeLockFile()
{
    const QString path = lockFilePath();
    if (!path.isEmpty())
        QFile::remove(path);
}

void AutoSave::attach(CardDocument *doc, const QString &sessionId)
{
    detach();

    if (!doc || sessionId.trimmed().isEmpty()) {
        // Nothing to watch: a detached AutoSave is a valid state and must not be
        // mistaken for an active session.
        return;
    }

    m_doc = doc;
    m_sessionId = sessionId;
    writeLockFile();

    connect(m_doc, &CardDocument::contentsChanged, this, &AutoSave::onDocumentChanged);

    if (m_enabled)
        start();
}

void AutoSave::detach()
{
    stop();
    if (m_idleCoalesce)
        m_idleCoalesce->stop();

    if (m_doc) {
        disconnect(m_doc, &CardDocument::contentsChanged, this,
                   &AutoSave::onDocumentChanged);
    }

    // A clean detach removes the lock file, which is exactly what tells the next
    // start that this session ended properly and has nothing to recover.
    removeLockFile();

    m_doc = nullptr;
    m_sessionId.clear();
    m_pending = false;
}

int AutoSave::intervalMinutes() const
{
    return m_intervalMinutes;
}

void AutoSave::setInterval(int minutes)
{
    const int clamped = qBound(kMinIntervalMinutes, minutes, kMaxIntervalMinutes);
    if (clamped == m_intervalMinutes)
        return;
    m_intervalMinutes = clamped;
    if (m_timer) {
        m_timer->setInterval(m_intervalMinutes * 60 * 1000);
        if (m_timer->isActive())
            m_timer->start();
    }
}

void AutoSave::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    if (m_enabled)
        start();
    else
        stop();
}

bool AutoSave::isEnabled() const
{
    return m_enabled;
}

void AutoSave::start()
{
    if (!m_doc || !m_enabled || m_sessionId.isEmpty())
        return;
    if (m_timer && !m_timer->isActive())
        m_timer->start();
    m_running = true;
}

void AutoSave::stop()
{
    if (m_timer)
        m_timer->stop();
    m_running = false;
}

void AutoSave::onDocumentChanged()
{
    if (!m_doc || !m_enabled || m_sessionId.isEmpty())
        return;
    m_pending = true;
    scheduleFirstSnapshot();
}

void AutoSave::scheduleFirstSnapshot()
{
    // A session that has never produced a snapshot gets one early, so a crash in
    // the first interval still leaves something to recover. Once a snapshot
    // exists the periodic timer does the work, which keeps writes predictable.
    if (m_idleCoalesce && !QFileInfo::exists(autosavePath()))
        m_idleCoalesce->start();
}

void AutoSave::onTimer()
{
    if (!m_doc || !m_enabled || m_sessionId.isEmpty())
        return;

    const bool fileExists = QFileInfo::exists(autosavePath());
    if (!m_pending && fileExists && m_doc->isDirty())
        m_pending = true;   // a change arrived without a contentsChanged()
    if (!m_pending && fileExists)
        return;             // nothing has changed since the previous snapshot

    saveNow();
}

void AutoSave::writeSidecar()
{
    if (m_sessionId.isEmpty())
        return;

    QJsonObject sidecar;
    sidecar.insert(QStringLiteral("projectPath"), m_doc ? m_doc->filePath() : QString());
    sidecar.insert(QStringLiteral("timestamp"),
                   QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    sidecar.insert(QStringLiteral("sessionId"), m_sessionId);
    sidecar.insert(QStringLiteral("applicationVersion"),
                   QString::fromLatin1(OCC_VERSION_STRING));
    sidecar.insert(QStringLiteral("dirty"), m_doc ? m_doc->isDirty() : true);

    const QString path =
        QDir(AppPaths::autosaveDir()).filePath(sidecarFileName(m_sessionId));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcProject, "AutoSave: could not write the recovery sidecar file");
        return;
    }
    file.write(QJsonDocument(sidecar).toJson(QJsonDocument::Compact));
    file.close();
}

bool AutoSave::loadSidecar(QJsonObject *out) const
{
    if (m_sessionId.isEmpty())
        return false;
    return sidecarLookup(m_sessionId, out);
}

bool AutoSave::saveNow()
{
    if (!m_doc || m_sessionId.isEmpty()) {
        emit autoSaveFailed(autoSaveTr("There is no open project to back up."));
        return false;
    }
    if (!AppPaths::ensureDir(AppPaths::autosaveDir())) {
        qCWarning(lcProject, "AutoSave: autosave directory unavailable");
        emit autoSaveFailed(
            autoSaveTr("The backup folder could not be created, so the project was not "
                       "backed up: %1")
                .arg(AppPaths::autosaveDir()));
        return false;
    }

    // ProjectSerializer::save() is atomic (temp file + rename), so a crash during
    // a backup can never damage the previous backup.
    const ProjectSerializer::SaveResult result =
        ProjectSerializer::save(*m_doc, autosavePath());
    if (!result.ok) {
        qCWarning(lcProject, "AutoSave failed: %s", qPrintable(result.technicalDetail));
        emit autoSaveFailed(result.error);
        return false;
    }

    writeSidecar();
    m_pending = false;
    emit autoSaved(autosavePath());
    return true;
}

QVector<AutoSave::RecoveryCandidate> AutoSave::findRecoverableSessions()
{
    QVector<RecoveryCandidate> candidates;

    QDir crashDir(AppPaths::crashDir());
    if (!crashDir.exists())
        return candidates;

    const QStringList locks =
        crashDir.entryList({ QStringLiteral("session_*.lock") }, QDir::Files, QDir::Name);
    for (const QString &lockName : locks) {
        const QString id = sessionIdFromFileName(lockName, QStringLiteral("session_"),
                                                QStringLiteral(".lock"));
        if (id.isEmpty())
            continue;

        const QString autosave =
            QDir(AppPaths::autosaveDir()).filePath(autosaveFileName(id));
        const QFileInfo autosaveInfo(autosave);
        if (!autosaveInfo.exists() || autosaveInfo.size() <= 0)
            continue;   // the lock is there but there is nothing to recover

        RecoveryCandidate candidate;
        candidate.sessionId = id;
        candidate.autosavePath = autosave;
        candidate.timestamp = autosaveInfo.lastModified();

        QJsonObject sidecar;
        if (sidecarLookup(id, &sidecar)) {
            candidate.projectPath =
                sidecar.value(QStringLiteral("projectPath")).toString();
            const QString stamp = sidecar.value(QStringLiteral("timestamp")).toString();
            if (!stamp.isEmpty()) {
                QDateTime parsed = QDateTime::fromString(stamp, Qt::ISODateWithMs);
                if (!parsed.isValid())
                    parsed = QDateTime::fromString(stamp, Qt::ISODate);
                if (parsed.isValid()) {
                    parsed.setTimeZone(QTimeZone::UTC);
                    candidate.timestamp = parsed;
                }
            }
        }

        // A session is only worth offering when the backup is newer than the
        // project on disk; otherwise the user already saved the work and the
        // leftover files are just noise.
        const QFileInfo projectInfo(candidate.projectPath);
        candidate.projectFileExists = !candidate.projectPath.isEmpty()
                                      && projectInfo.exists() && projectInfo.isFile();
        if (candidate.projectFileExists
            && projectInfo.lastModified() >= candidate.timestamp) {
            continue;
        }

        candidates.append(candidate);
    }

    return candidates;
}

bool AutoSave::openRecovery(const RecoveryCandidate &candidate, CardDocument *doc,
                            QString *error)
{
    if (!doc) {
        if (error)
            *error = autoSaveTr("The recovered copy could not be opened.");
        return false;
    }
    if (candidate.autosavePath.isEmpty()
        || !QFileInfo::exists(candidate.autosavePath)) {
        if (error) {
            *error = autoSaveTr("The recovered copy of this project is no longer "
                                "available.");
        }
        return false;
    }

    // Reading only: the user's project file is never touched here.
    const ProjectSerializer::LoadResult result =
        ProjectSerializer::load(doc, candidate.autosavePath);
    if (!result.ok) {
        if (error)
            *error = result.error;
        return false;
    }

    // The recovered document belongs to the original project, so Save writes back
    // over the real file. It is marked as unsaved work on purpose: the recovered
    // content has not been confirmed by the user yet, and quietly treating it as
    // already saved is how work gets lost.
    if (!candidate.projectPath.isEmpty())
        doc->setFilePath(candidate.projectPath);
    doc->setDirty(true);

    if (error)
        error->clear();
    return true;
}

bool AutoSave::discardSession(const QString &sessionId)
{
    if (sessionId.trimmed().isEmpty())
        return false;

    bool removedSomething = false;
    const QStringList files = {
        QDir(AppPaths::autosaveDir()).filePath(autosaveFileName(sessionId)),
        QDir(AppPaths::autosaveDir()).filePath(sidecarFileName(sessionId)),
        QDir(AppPaths::crashDir()).filePath(lockFileName(sessionId)),
    };
    for (const QString &path : files) {
        if (!QFileInfo::exists(path))
            continue;
        if (QFile::remove(path))
            removedSomething = true;
        else
            qCWarning(lcProject, "AutoSave: could not remove %s", qPrintable(path));
    }
    return removedSomething;
}

int AutoSave::purgeOlderThan(int days)
{
    if (days < 0)
        days = 0;
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-days);
    int removed = 0;

    const QStringList directories = { AppPaths::autosaveDir(), AppPaths::crashDir() };
    for (const QString &directoryPath : directories) {
        QDir directory(directoryPath);
        if (!directory.exists())
            continue;
        const QStringList files = directory.entryList(
            { QStringLiteral("autosave_*"), QStringLiteral("session_*") }, QDir::Files,
            QDir::Name);
        for (const QString &name : files) {
            const QFileInfo info(directory.filePath(name));
            if (info.lastModified() >= cutoff)
                continue;
            if (QFile::remove(info.absoluteFilePath()))
                ++removed;
        }
    }
    return removed;
}

} // namespace occ
