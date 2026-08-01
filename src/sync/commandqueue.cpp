#include "commandqueue.h"

#include "data/database.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {
// Commands that keep failing are dropped rather than blocking the queue head
// forever; the next full sync restores correct state either way.
constexpr int MaxAttempts = 5;

QString jsonToText(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QJsonObject textToJson(const QString &s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}
} // namespace

CommandQueue *CommandQueue::instance()
{
    static CommandQueue self;
    return &self;
}

CommandQueue::CommandQueue(QObject *parent)
    : QObject(parent)
{}

QString CommandQueue::enqueue(const QString &type, const QJsonObject &args, const QString &tempId)
{
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);

    if (m_nextSeq < 0) {
        QSqlQuery max(Database::instance().db());
        max.exec(QStringLiteral("SELECT COALESCE(MAX(seq), 0) FROM pending_commands"));
        m_nextSeq = max.next() ? max.value(0).toLongLong() + 1 : 1;
    }

    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("INSERT INTO pending_commands "
                             "(uuid, seq, type, args, temp_id, created_at, attempts) "
                             "VALUES (?, ?, ?, ?, ?, ?, 0)"));
    q.addBindValue(uuid);
    q.addBindValue(m_nextSeq++);
    q.addBindValue(type);
    q.addBindValue(jsonToText(args));
    q.addBindValue(tempId.isEmpty() ? QVariant() : QVariant(tempId));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    if (!q.exec()) {
        qWarning() << "ktodo: failed to queue" << type << q.lastError().text();
        return {};
    }

    Q_EMIT pendingCountChanged();
    return uuid;
}

QJsonObject CommandQueue::substituteIds(const QJsonObject &args) const
{
    QJsonObject out = args;
    for (auto it = out.begin(); it != out.end(); ++it) {
        if (!it.value().isString()) {
            continue;
        }
        const QString real = realIdFor(it.value().toString());
        if (!real.isEmpty()) {
            *it = real;
        }
    }
    return out;
}

QVector<PendingCommand> CommandQueue::take(int limit) const
{
    QVector<PendingCommand> out;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT uuid, seq, type, args, temp_id, attempts "
                             "FROM pending_commands ORDER BY seq ASC LIMIT ?"));
    q.addBindValue(limit);
    if (!q.exec()) {
        return out;
    }

    while (q.next()) {
        PendingCommand c;
        c.uuid = q.value(0).toString();
        c.seq = q.value(1).toLongLong();
        c.type = q.value(2).toString();
        c.args = substituteIds(textToJson(q.value(3).toString()));
        c.tempId = q.value(4).toString();
        c.attempts = q.value(5).toInt();

        // A command still referring to an unresolved temp id cannot succeed
        // yet. Stop here so ordering is preserved for the rest of the batch.
        bool unresolved = false;
        for (auto it = c.args.begin(); it != c.args.end(); ++it) {
            if (it.value().isString() && it.value().toString().startsWith(QLatin1String("local-"))) {
                unresolved = true;
                break;
            }
        }
        if (unresolved) {
            break;
        }

        out.append(c);
    }
    return out;
}

void CommandQueue::remove(const QString &uuid)
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("DELETE FROM pending_commands WHERE uuid = ?"));
    q.addBindValue(uuid);
    q.exec();
    Q_EMIT pendingCountChanged();
}

void CommandQueue::recordFailure(const QString &uuid, const QString &error, bool permanent)
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("UPDATE pending_commands SET attempts = attempts + 1, last_error = ? "
                             "WHERE uuid = ?"));
    q.addBindValue(error);
    q.addBindValue(uuid);
    q.exec();

    if (permanent) {
        remove(uuid);
        return;
    }

    QSqlQuery check(Database::instance().db());
    check.prepare(QStringLiteral("SELECT attempts FROM pending_commands WHERE uuid = ?"));
    check.addBindValue(uuid);
    if (check.exec() && check.next() && check.value(0).toInt() >= MaxAttempts) {
        qWarning() << "ktodo: dropping command after" << MaxAttempts << "attempts:" << error;
        remove(uuid);
    }
}

int CommandQueue::pendingCount() const
{
    QSqlQuery q(Database::instance().db());
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM pending_commands")) && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

QSet<QString> CommandQueue::pendingObjectIds() const
{
    QSet<QString> ids;
    QSqlQuery q(Database::instance().db());
    if (!q.exec(QStringLiteral("SELECT args, temp_id FROM pending_commands"))) {
        return ids;
    }
    while (q.next()) {
        const QJsonObject args = textToJson(q.value(0).toString());
        // "id" is the object a command acts on; item_add reports its temp id.
        const QString id = args.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            ids.insert(id);
        }
        const QString tempId = q.value(1).toString();
        if (!tempId.isEmpty()) {
            ids.insert(tempId);
            const QString real = realIdFor(tempId);
            if (!real.isEmpty()) {
                ids.insert(real);
            }
        }
    }
    return ids;
}

bool CommandQueue::isEmpty() const
{
    return pendingCount() == 0;
}

void CommandQueue::resolveTempId(const QString &tempId, const QString &realId)
{
    if (tempId.isEmpty() || realId.isEmpty()) {
        return;
    }
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO temp_id_map (temp_id, real_id) VALUES (?, ?)"));
    q.addBindValue(tempId);
    q.addBindValue(realId);
    q.exec();
}

QString CommandQueue::realIdFor(const QString &tempId) const
{
    if (tempId.isEmpty()) {
        return {};
    }
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT real_id FROM temp_id_map WHERE temp_id = ?"));
    q.addBindValue(tempId);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return {};
}

void CommandQueue::clear()
{
    QSqlQuery q(Database::instance().db());
    q.exec(QStringLiteral("DELETE FROM pending_commands"));
    m_nextSeq = -1;
    Q_EMIT pendingCountChanged();
}
