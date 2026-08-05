#include "syncengine.h"

#include <QQmlEngine>

#include "api/apiclient.h"
#include "auth/authmanager.h"
#include "data/repository.h"
#include "sync/commandqueue.h"

#include <KLocalizedString>

#include <QDebug>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>

namespace {
// Todoist offers webhooks (server-to-server) but nothing a desktop client can
// subscribe to, so staying current means polling. An incremental sync with no
// changes is a few hundred bytes, which is cheap enough to do often while the
// user is actually looking at the window.
constexpr int ActiveIntervalMs = 30 * 1000;
constexpr int IdleIntervalMs = 5 * 60 * 1000;
// Long enough to batch a burst of edits, short enough to feel immediate.
constexpr int FlushDelayMs = 1200;
constexpr int MaxRetryDelayMs = 5 * 60 * 1000;
constexpr int CommandBatchSize = 100;
// The completed-tasks endpoint caps the window at three months and a page at
// 50 tasks. Ten pages is more history than a task list can usefully show, and
// bounds how long the toggle spends fetching on a busy account.
constexpr int CompletedWindowDays = 90;
constexpr int CompletedPageSize = 50;
constexpr int MaxCompletedPages = 10;
} // namespace

SyncEngine::SyncEngine(ApiClient *api, Repository *repo, AuthManager *auth, QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_repo(repo)
    , m_auth(auth)
{
    m_periodicTimer.setInterval(ActiveIntervalMs);
    connect(&m_periodicTimer, &QTimer::timeout, this, &SyncEngine::syncNow);

    // Back off while the user is elsewhere, and catch up the moment they come
    // back, so a change made on the website is there when they look.
    if (auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(gui, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            const bool active = state == Qt::ApplicationActive;
            m_periodicTimer.setInterval(active ? ActiveIntervalMs : IdleIntervalMs);
            if (active && m_periodicTimer.isActive()) {
                syncNow();
            }
        });
    }

    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(FlushDelayMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &SyncEngine::syncNow);

    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, &SyncEngine::syncNow);

    connect(CommandQueue::instance(), &CommandQueue::pendingCountChanged, this, [this] {
        Q_EMIT pendingCountChanged();
        // Flush promptly whichever layer queued the command; reordering
        // writes to the repository directly rather than through Application.
        if (!CommandQueue::instance()->isEmpty()) {
            scheduleFlush();
        }
    });
}

void SyncEngine::start()
{
    m_periodicTimer.start();
    syncNow();
}

void SyncEngine::stop()
{
    m_periodicTimer.stop();
    m_flushTimer.stop();
    m_retryTimer.stop();
}

int SyncEngine::pendingCount() const
{
    return CommandQueue::instance()->pendingCount();
}

QString SyncEngine::statusText() const
{
    switch (m_status) {
    case Syncing:
        return i18n("Syncing…");
    case Offline:
        return pendingCount() > 0 ? i18np("Offline · %1 change waiting", "Offline · %1 changes waiting", pendingCount()) : i18n("Offline");
    case Error:
        return m_lastError.isEmpty() ? i18n("Sync error") : m_lastError;
    case Idle:
        break;
    }

    if (pendingCount() > 0) {
        return i18np("%1 change waiting", "%1 changes waiting", pendingCount());
    }
    if (!m_lastSyncedAt.isValid()) {
        return i18n("Not synced yet");
    }

    const qint64 secs = m_lastSyncedAt.secsTo(QDateTime::currentDateTime());
    if (secs < 60) {
        return i18n("Synced just now");
    }
    if (secs < 3600) {
        return i18np("Synced %1 minute ago", "Synced %1 minutes ago", secs / 60);
    }
    if (secs < 86400) {
        return i18np("Synced %1 hour ago", "Synced %1 hours ago", secs / 3600);
    }
    return i18n("Synced %1", QLocale().toString(m_lastSyncedAt, QLocale::ShortFormat));
}

void SyncEngine::setStatus(Status status, const QString &error)
{
    m_status = status;
    m_lastError = error;
    Q_EMIT statusChanged();
}

void SyncEngine::scheduleFlush()
{
    m_flushTimer.start();
}

void SyncEngine::syncNow()
{
    if (m_running) {
        // Remember the request so changes made mid-flight are not stranded.
        m_syncRequestedWhileRunning = true;
        return;
    }
    performSync();
}

void SyncEngine::resync()
{
    m_repo->setSyncToken(QStringLiteral("*"));
    syncNow();
}

void SyncEngine::fetchCompleted(const QString &projectId)
{
    // Toggling the view repeatedly must not stack up overlapping page walks.
    if (!m_auth || !m_auth->isAuthenticated() || m_fetchingCompleted) {
        return;
    }

    m_fetchingCompleted = true;
    const QDateTime until = QDateTime::currentDateTimeUtc();
    fetchCompletedPage(projectId, until.addDays(-CompletedWindowDays), until, QString(), 0);
}

void SyncEngine::fetchCompletedPage(const QString &projectId, const QDateTime &since, const QDateTime &until, const QString &cursor, int page)
{
    m_api->completedTasks(since, until, projectId, cursor, CompletedPageSize, [this, projectId, since, until, page](const ApiClient::Result &result) {
        if (!result.ok) {
            m_fetchingCompleted = false;
            qWarning() << "ktodo: could not read completed tasks" << result.error;
            Q_EMIT completedFetchFailed(result.error);
            return;
        }

        // Each page is applied as it lands, so a long history fills the list
        // progressively instead of after the last request.
        const QJsonArray items = result.payload.contains(QStringLiteral("items")) ? result.payload.value(QStringLiteral("items")).toArray()
                                                                                  : result.payload.value(QStringLiteral("results")).toArray();
        m_repo->applyCompletedItems(items);

        const QString next = result.payload.value(QStringLiteral("next_cursor")).toString();
        if (next.isEmpty() || items.isEmpty() || page + 1 >= MaxCompletedPages) {
            m_fetchingCompleted = false;
            return;
        }
        fetchCompletedPage(projectId, since, until, next, page + 1);
    });
}

void SyncEngine::scheduleRetry()
{
    // Exponential backoff, capped, so a long outage does not hammer the API.
    const int delay = qMin(MaxRetryDelayMs, 5000 * (1 << qMin(m_consecutiveFailures, 6)));
    m_retryTimer.start(delay);
}

void SyncEngine::performSync()
{
    if (!m_auth || !m_auth->isAuthenticated()) {
        setStatus(Idle);
        return;
    }

    m_running = true;
    m_syncRequestedWhileRunning = false;
    setStatus(Syncing);

    const QVector<PendingCommand> pending = CommandQueue::instance()->take(CommandBatchSize);

    QJsonArray commands;
    for (const PendingCommand &c : pending) {
        QJsonObject obj{{QStringLiteral("type"), c.type}, {QStringLiteral("uuid"), c.uuid}, {QStringLiteral("args"), c.args}};
        if (!c.tempId.isEmpty()) {
            obj[QStringLiteral("temp_id")] = c.tempId;
        }
        commands.append(obj);
    }

    const QString token = m_repo->syncToken();

    m_api->sync(token, {}, commands, [this, pending](const ApiClient::Result &result) {
        m_running = false;

        if (!result.ok) {
            ++m_consecutiveFailures;

            if (result.unauthorized) {
                // A single 401 is not proof the token died — a blip or a
                // momentarily unhappy endpoint looks identical. Only give up
                // once it happens twice in a row, so the user is not thrown
                // back to the sign-in page for a transient failure.
                ++m_consecutiveUnauthorized;
                if (m_consecutiveUnauthorized < 2) {
                    setStatus(Offline, result.error);
                    scheduleRetry();
                    Q_EMIT syncFinished(false);
                    return;
                }
                setStatus(Error, result.error);
                if (m_auth) {
                    m_auth->handleUnauthorized();
                }
                Q_EMIT syncFinished(false);
                return;
            }

            // Queued commands are deliberately left in place: a transient
            // failure must not lose the user's edits.
            setStatus(result.retryable ? Offline : Error, result.error);
            if (result.retryable) {
                scheduleRetry();
            }
            Q_EMIT syncFinished(false);
            return;
        }

        m_consecutiveFailures = 0;
        m_consecutiveUnauthorized = 0;

        // 1. Resolve command outcomes before applying state, so that a
        //    rejected command's optimistic write is corrected by the payload.
        const QJsonObject syncStatus = result.payload.value(QStringLiteral("sync_status")).toObject();
        for (const PendingCommand &c : pending) {
            const QJsonValue outcome = syncStatus.value(c.uuid);
            if (outcome.isUndefined()) {
                // Not reported means not processed; leave it queued.
                continue;
            }
            if (outcome.isString() && outcome.toString() == QLatin1String("ok")) {
                CommandQueue::instance()->remove(c.uuid);
                continue;
            }

            const QJsonObject err = outcome.toObject();
            const QString message = err.value(QStringLiteral("error")).toString();
            const int code = err.value(QStringLiteral("error_code")).toInt();

            // Anything the server actively rejected will be rejected again;
            // retrying only blocks the queue.
            qWarning() << "ktodo: command rejected" << c.type << code << message;
            CommandQueue::instance()->recordFailure(c.uuid, message, true);
            Q_EMIT commandRejected(c.type, message);
        }

        // 2. Map locally minted ids onto the server's before applying state,
        //    so incoming rows line up with what the UI already shows.
        const QJsonObject mapping = result.payload.value(QStringLiteral("temp_id_mapping")).toObject();
        if (!mapping.isEmpty()) {
            QHash<QString, QString> hash;
            for (auto it = mapping.constBegin(); it != mapping.constEnd(); ++it) {
                hash.insert(it.key(), it.value().toString());
            }
            m_repo->applyTempIdMapping(hash);
        }

        // 3. Apply server state.
        m_repo->applySyncPayload(result.payload);

        m_lastSyncedAt = QDateTime::currentDateTime();
        setStatus(Idle);
        Q_EMIT pendingCountChanged();
        Q_EMIT syncFinished(true);

        // More work may remain: a queue longer than one batch, or edits made
        // while this request was in flight.
        if (m_syncRequestedWhileRunning || !CommandQueue::instance()->isEmpty()) {
            scheduleFlush();
        }
    });
}

// ---------------------------------------------------------------------------
// QML singleton access
//
// The instance is built in main() with its dependencies wired up; create()
// hands that same object to the QML engine. Ownership stays in C++ so the
// engine does not delete an object main() still refers to.
// ---------------------------------------------------------------------------

namespace {
SyncEngine *s_instance = nullptr;
}

void SyncEngine::setInstance(SyncEngine *instance)
{
    s_instance = instance;
}

SyncEngine *SyncEngine::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine);
    Q_UNUSED(scriptEngine);
    Q_ASSERT(s_instance);
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}
