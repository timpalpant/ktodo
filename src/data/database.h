#pragma once

#include <QSqlDatabase>
#include <QString>

/**
 * Owns the on-disk SQLite cache and its schema migrations.
 *
 * The cache is a mirror of server state plus a durable outbound command
 * queue, so the UI can render and accept edits with no network at all.
 */
class Database
{
public:
    static Database &instance();

    bool open(const QString &path = {});
    void close();

    QSqlDatabase db() const;
    bool isOpen() const;

    /// Wipes cached resources and the sync token, but not the command queue.
    void resetCache();
    /// Wipes everything, used on sign-out.
    void wipe();

    QString lastError() const { return m_lastError; }

private:
    Database() = default;
    bool applyMigrations();
    int schemaVersion();
    void setSchemaVersion(int version);

    QString m_connectionName = QStringLiteral("ktodo");
    QString m_lastError;
    bool m_open = false;
};
