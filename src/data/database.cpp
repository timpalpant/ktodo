#include "database.h"

#include <QDebug>
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

namespace {

// Bumping this triggers applyMigrations() on next launch.
constexpr int CurrentSchemaVersion = 1;

const char *const CreateStatements[] = {
    R"(CREATE TABLE IF NOT EXISTS meta (
        key   TEXT PRIMARY KEY,
        value TEXT
    ))",

    R"(CREATE TABLE IF NOT EXISTS projects (
        id            TEXT PRIMARY KEY,
        name          TEXT NOT NULL DEFAULT '',
        description   TEXT NOT NULL DEFAULT '',
        color         TEXT NOT NULL DEFAULT 'charcoal',
        parent_id     TEXT,
        workspace_id  TEXT,
        folder_id     TEXT,
        role          TEXT,
        status        TEXT,
        view_style    TEXT NOT NULL DEFAULT 'list',
        child_order   INTEGER NOT NULL DEFAULT 0,
        is_inbox      INTEGER NOT NULL DEFAULT 0,
        is_shared     INTEGER NOT NULL DEFAULT 0,
        is_favorite   INTEGER NOT NULL DEFAULT 0,
        is_archived   INTEGER NOT NULL DEFAULT 0,
        is_collapsed  INTEGER NOT NULL DEFAULT 0,
        can_assign    INTEGER NOT NULL DEFAULT 0,
        can_comment   INTEGER NOT NULL DEFAULT 1
    ))",

    R"(CREATE TABLE IF NOT EXISTS sections (
        id            TEXT PRIMARY KEY,
        name          TEXT NOT NULL DEFAULT '',
        project_id    TEXT NOT NULL,
        section_order INTEGER NOT NULL DEFAULT 0,
        is_collapsed  INTEGER NOT NULL DEFAULT 0,
        is_archived   INTEGER NOT NULL DEFAULT 0
    ))",
    "CREATE INDEX IF NOT EXISTS idx_sections_project ON sections(project_id)",

    R"(CREATE TABLE IF NOT EXISTS items (
        id              TEXT PRIMARY KEY,
        content         TEXT NOT NULL DEFAULT '',
        description     TEXT NOT NULL DEFAULT '',
        project_id      TEXT,
        section_id      TEXT,
        parent_id       TEXT,
        responsible_uid TEXT,
        assigned_by_uid TEXT,
        added_by_uid    TEXT,
        user_id         TEXT,
        labels          TEXT NOT NULL DEFAULT '[]',
        due_json        TEXT,
        due_date        TEXT,
        due_has_time    INTEGER NOT NULL DEFAULT 0,
        due_recurring   INTEGER NOT NULL DEFAULT 0,
        deadline_json   TEXT,
        deadline_date   TEXT,
        priority        INTEGER NOT NULL DEFAULT 1,
        child_order     INTEGER NOT NULL DEFAULT 0,
        day_order       INTEGER NOT NULL DEFAULT -1,
        note_count      INTEGER NOT NULL DEFAULT 0,
        checked         INTEGER NOT NULL DEFAULT 0,
        is_collapsed    INTEGER NOT NULL DEFAULT 0,
        added_at        TEXT,
        completed_at    TEXT,
        updated_at      TEXT
    ))",
    "CREATE INDEX IF NOT EXISTS idx_items_project ON items(project_id)",
    "CREATE INDEX IF NOT EXISTS idx_items_due ON items(due_date)",
    "CREATE INDEX IF NOT EXISTS idx_items_parent ON items(parent_id)",
    "CREATE INDEX IF NOT EXISTS idx_items_checked ON items(checked)",

    R"(CREATE TABLE IF NOT EXISTS labels (
        id          TEXT PRIMARY KEY,
        name        TEXT NOT NULL DEFAULT '',
        color       TEXT NOT NULL DEFAULT 'charcoal',
        item_order  INTEGER NOT NULL DEFAULT 0,
        is_favorite INTEGER NOT NULL DEFAULT 0
    ))",

    R"(CREATE TABLE IF NOT EXISTS collaborators (
        id        TEXT PRIMARY KEY,
        full_name TEXT NOT NULL DEFAULT '',
        email     TEXT NOT NULL DEFAULT '',
        image_id  TEXT,
        timezone  TEXT
    ))",

    R"(CREATE TABLE IF NOT EXISTS collaborator_states (
        project_id TEXT NOT NULL,
        user_id    TEXT NOT NULL,
        role       TEXT,
        state      TEXT,
        PRIMARY KEY (project_id, user_id)
    ))",

    R"(CREATE TABLE IF NOT EXISTS filters (
        id          TEXT PRIMARY KEY,
        name        TEXT NOT NULL DEFAULT '',
        query       TEXT NOT NULL DEFAULT '',
        color       TEXT NOT NULL DEFAULT 'charcoal',
        item_order  INTEGER NOT NULL DEFAULT 0,
        is_favorite INTEGER NOT NULL DEFAULT 0
    ))",

    R"(CREATE TABLE IF NOT EXISTS notes (
        id         TEXT PRIMARY KEY,
        item_id    TEXT,
        project_id TEXT,
        posted_uid TEXT,
        content    TEXT NOT NULL DEFAULT '',
        posted_at  TEXT,
        attachment TEXT
    ))",
    "CREATE INDEX IF NOT EXISTS idx_notes_item ON notes(item_id)",

    R"(CREATE TABLE IF NOT EXISTS reminders (
        id            TEXT PRIMARY KEY,
        item_id       TEXT,
        notify_uid    TEXT,
        type          TEXT,
        due_json      TEXT,
        due_date      TEXT,
        minute_offset INTEGER NOT NULL DEFAULT 0
    ))",

    // Durable outbound queue. `seq` preserves submission order across restarts,
    // which matters because commands frequently depend on earlier ones.
    R"(CREATE TABLE IF NOT EXISTS pending_commands (
        uuid       TEXT PRIMARY KEY,
        seq        INTEGER,
        type       TEXT NOT NULL,
        args       TEXT NOT NULL DEFAULT '{}',
        temp_id    TEXT,
        created_at TEXT,
        attempts   INTEGER NOT NULL DEFAULT 0,
        last_error TEXT
    ))",
    "CREATE INDEX IF NOT EXISTS idx_pending_seq ON pending_commands(seq)",

    // Maps a locally minted temp_id to the real server id once known, so
    // queued follow-up commands can be rewritten before they are sent.
    R"(CREATE TABLE IF NOT EXISTS temp_id_map (
        temp_id TEXT PRIMARY KEY,
        real_id TEXT NOT NULL
    ))",

    // Reminders already surfaced, so a resync does not re-notify.
    R"(CREATE TABLE IF NOT EXISTS notified (
        key        TEXT PRIMARY KEY,
        notified_at TEXT
    ))",
};

} // namespace

Database &Database::instance()
{
    static Database self;
    return self;
}

QSqlDatabase Database::db() const
{
    return QSqlDatabase::database(m_connectionName);
}

bool Database::isOpen() const
{
    return m_open;
}

bool Database::open(const QString &path)
{
    QString file = path;
    if (file.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        file = dir + QStringLiteral("/ktodo.sqlite");
    }

    QSqlDatabase database = QSqlDatabase::contains(m_connectionName) ? QSqlDatabase::database(m_connectionName)
                                                                     : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    database.setDatabaseName(file);

    if (!database.open()) {
        m_lastError = database.lastError().text();
        qWarning() << "ktodo: cannot open cache" << file << m_lastError;
        return false;
    }

    QSqlQuery pragma(database);
    // WAL keeps reads responsive while a sync writes, and NORMAL is the
    // right durability trade-off for a cache that can always be rebuilt.
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));

    m_open = true;
    return applyMigrations();
}

void Database::close()
{
    if (m_open) {
        QSqlDatabase::database(m_connectionName).close();
        m_open = false;
    }
}

int Database::schemaVersion()
{
    QSqlQuery q(db());
    if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

void Database::setSchemaVersion(int version)
{
    QSqlQuery q(db());
    q.exec(QStringLiteral("PRAGMA user_version=%1").arg(version));
}

bool Database::applyMigrations()
{
    QSqlDatabase database = db();
    database.transaction();

    for (const char *stmt : CreateStatements) {
        QSqlQuery q(database);
        if (!q.exec(QLatin1String(stmt))) {
            m_lastError = q.lastError().text();
            qWarning() << "ktodo: schema error" << m_lastError << QLatin1String(stmt).left(60);
            database.rollback();
            return false;
        }
    }

    database.commit();
    if (schemaVersion() != CurrentSchemaVersion) {
        setSchemaVersion(CurrentSchemaVersion);
    }
    return true;
}

void Database::resetCache()
{
    QSqlDatabase database = db();
    database.transaction();
    const QStringList tables = {QStringLiteral("projects"),
                                QStringLiteral("sections"),
                                QStringLiteral("items"),
                                QStringLiteral("labels"),
                                QStringLiteral("collaborators"),
                                QStringLiteral("collaborator_states"),
                                QStringLiteral("filters"),
                                QStringLiteral("notes"),
                                QStringLiteral("reminders")};
    for (const QString &t : tables) {
        QSqlQuery q(database);
        q.exec(QStringLiteral("DELETE FROM %1").arg(t));
    }
    QSqlQuery q(database);
    q.exec(QStringLiteral("DELETE FROM meta WHERE key='sync_token'"));
    database.commit();
}

void Database::wipe()
{
    resetCache();
    QSqlDatabase database = db();
    database.transaction();
    for (const QString &t : {QStringLiteral("pending_commands"), QStringLiteral("temp_id_map"), QStringLiteral("notified"), QStringLiteral("meta")}) {
        QSqlQuery q(database);
        q.exec(QStringLiteral("DELETE FROM %1").arg(t));
    }
    database.commit();
}
