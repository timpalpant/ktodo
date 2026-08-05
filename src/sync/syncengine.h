#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <qqmlintegration.h>

class ApiClient;
class AuthManager;
class QJSEngine;
class QQmlEngine;
class Repository;

/**
 * Owns the sync loop: periodic incremental pulls, flushing the outbound
 * command queue, and reconciling the two.
 *
 * A single request carries both directions, so a user's edit and the server's
 * latest state are exchanged atomically.
 */
class SyncEngine : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Sync)
    QML_SINGLETON

    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)
    Q_PROPERTY(QDateTime lastSyncedAt READ lastSyncedAt NOTIFY statusChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY pendingCountChanged)

public:
    enum Status {
        Idle,
        Syncing,
        Offline,
        Error,
    };
    Q_ENUM(Status)

    SyncEngine(ApiClient *api, Repository *repo, AuthManager *auth, QObject *parent = nullptr);

    static void setInstance(SyncEngine *instance);
    static SyncEngine *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    Status status() const { return m_status; }
    QString statusText() const;
    QString lastError() const { return m_lastError; }
    QDateTime lastSyncedAt() const { return m_lastSyncedAt; }
    int pendingCount() const;

    void start();
    void stop();

public Q_SLOTS:
    /// Requests a sync now. Coalesces if one is already running.
    void syncNow();
    /// Discards the sync token and pulls everything again.
    void resync();
    /// Short-delay sync used after a local edit, so typing does not spam.
    void scheduleFlush();
    /**
     * Pulls recently completed tasks into the local cache.
     *
     * The regular sync payload never carries them, so nothing the user
     * finished before this session exists locally until this runs. Pass a
     * project id to read one list, or an empty string for every project.
     */
    void fetchCompleted(const QString &projectId = {});

Q_SIGNALS:
    void statusChanged();
    void pendingCountChanged();
    void syncFinished(bool success);
    /// A command was rejected outright; the UI surfaces this to the user.
    void commandRejected(const QString &commandType, const QString &error);
    /// Completed tasks could not be read; the list stays as it was.
    void completedFetchFailed(const QString &error);

private:
    void performSync();
    void fetchCompletedPage(const QString &projectId, const QDateTime &since, const QDateTime &until, const QString &cursor, int page);
    void setStatus(Status status, const QString &error = {});
    void scheduleRetry();

    ApiClient *m_api = nullptr;
    Repository *m_repo = nullptr;
    AuthManager *m_auth = nullptr;

    QTimer m_periodicTimer;
    QTimer m_flushTimer;
    QTimer m_retryTimer;

    Status m_status = Idle;
    QString m_lastError;
    QDateTime m_lastSyncedAt;

    bool m_running = false;
    bool m_syncRequestedWhileRunning = false;
    bool m_fetchingCompleted = false;
    int m_consecutiveFailures = 0;
    int m_consecutiveUnauthorized = 0;
};
