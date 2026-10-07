#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

// ---------------------------------------------------------------------------
// AutoSave - periodic snapshots of the open document plus crash recovery.
//
// Two files are written per session, both under AppPaths::autosaveDir():
//
//   autosave_<sessionId>.occard  the document itself (a normal project file,
//                                written through ProjectSerializer, so it is
//                                atomic and can simply be opened by hand)
//   autosave_<sessionId>.json    a sidecar describing where the project lives,
//                                when it was snapshotted and whether it was
//                                dirty at the time
//
// A second file, AppPaths::crashDir()/session_<sessionId>.lock, is created on
// attach() and removed on detach(). If the application is killed - or the
// machine loses power - the lock file survives, which is precisely how a
// crash is detected on the next start:
//
//   lock file present AND the autosave is newer than the project on disk
//   (or the project no longer exists)  ->  the session is recoverable
//
// Recovery NEVER writes to the user's project. openRecovery() only reports
// what is available; the user decides whether to load it, and mergeRecovery()
// is the only function that touches disk - and then only to delete the
// autosave files after the user has saved the recovered document themselves.
// ---------------------------------------------------------------------------
namespace occ {

class CardDocument;

class AutoSave : public QObject
{
    Q_OBJECT
public:
    struct RecoveryCandidate
    {
        QString   sessionId;
        QString   autosavePath;
        QString   projectPath;
        QDateTime timestamp;
        bool      projectFileExists = false;
    };

    explicit AutoSave(QObject *parent = nullptr);
    ~AutoSave() override;

    // Starts tracking `doc` under `sessionId`. Writes the lock file and
    // registers this session as "in progress". Passing a null document or an
    // empty session id detaches instead.
    void attach(CardDocument *doc, const QString &sessionId);
    void detach();

    int  intervalMinutes() const;
    void setInterval(int minutes);
    void setEnabled(bool enabled);
    bool isEnabled() const;

    void start();
    void stop();
    // Writes a snapshot immediately (used by Save As, by the idle timer and by
    // the crash handler). Returns true when the snapshot was written.
    bool saveNow();

    QString sessionId() const;
    QString autosavePath() const;    // empty when not attached
    QString lockFilePath() const;

    // Sessions that look like they crashed. Reads (never writes) the autosave
    // directory.
    static QVector<RecoveryCandidate> findRecoverableSessions();

    // Loads a candidate into `doc` without touching the user's project file.
    static bool openRecovery(const RecoveryCandidate &candidate, CardDocument *doc,
                             QString *error = nullptr);

    // Deletes the autosave and lock files of a session. Called after the user
    // has either saved the recovered document or chosen to discard it.
    static bool discardSession(const QString &sessionId);

    // Removes every autosave / lock file whose timestamp is older than
    // `days`. Called once at start-up so the directory cannot grow forever.
    static int purgeOlderThan(int days);

signals:
    void autoSaved(const QString &path);
    void autoSaveFailed(const QString &error);

private slots:
    void onTimer();
    void onDocumentChanged();

private:
    void writeLockFile();
    bool loadSidecar(QJsonObject *out) const;
    void writeSidecar();
    // Schedules one early snapshot shortly after the first edit of a session, so
    // a crash inside the first interval still has something to recover.
    void scheduleFirstSnapshot();
    void removeLockFile();

    CardDocument *m_doc = nullptr;
    QString       m_sessionId;
    class QTimer *m_timer = nullptr;
    class QTimer *m_idleCoalesce = nullptr;
    int           m_intervalMinutes = 5;
    bool          m_enabled = true;
    bool          m_running = false;
    bool          m_pending = false;
};

} // namespace occ
