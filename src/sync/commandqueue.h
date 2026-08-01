#pragma once

#include <QHash>
#include <QSet>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

struct PendingCommand {
    QString uuid;
    qint64 seq = 0;
    QString type;
    QJsonObject args;
    QString tempId;
    int attempts = 0;
};

/**
 * Durable, ordered queue of mutations waiting to reach the server.
 *
 * Commands are persisted before the UI reports success, so an edit made
 * offline (or during a crash window) is replayed on the next sync.
 */
class CommandQueue : public QObject
{
    Q_OBJECT

public:
    static CommandQueue *instance();

    /// Persists a command and returns its uuid.
    QString enqueue(const QString &type, const QJsonObject &args, const QString &tempId = {});

    /// Oldest-first batch, with any known temp ids already substituted.
    QVector<PendingCommand> take(int limit = 100) const;

    void remove(const QString &uuid);
    void recordFailure(const QString &uuid, const QString &error, bool permanent);

    int pendingCount() const;
    bool isEmpty() const;

    /**
     * Ids of objects that still have unsent commands against them.
     *
     * A sync response reflects server state as of when the request was built,
     * so anything edited since must not be overwritten by it.
     */
    QSet<QString> pendingObjectIds() const;

    /// Stores a temp id -> real id mapping and rewrites queued commands.
    void resolveTempId(const QString &tempId, const QString &realId);
    QString realIdFor(const QString &tempId) const;

    void clear();

Q_SIGNALS:
    void pendingCountChanged();

private:
    explicit CommandQueue(QObject *parent = nullptr);

    /// Replaces any temp ids appearing in argument values with real ids.
    QJsonObject substituteIds(const QJsonObject &args) const;

    mutable qint64 m_nextSeq = -1;
};
