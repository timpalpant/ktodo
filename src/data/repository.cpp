#include "repository.h"

#include "data/database.h"
#include "query/filterquery.h"
#include "sync/commandqueue.h"
#include "util/duedate.h"

#include <algorithm>

#include <QDebug>
#include <QJsonArray>
#include <QSet>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

using namespace Todoist;

namespace {

QString toText(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QJsonObject fromText(const QString &s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

QString labelsToText(const QStringList &labels)
{
    QJsonArray a;
    for (const QString &l : labels) {
        a.append(l);
    }
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

QStringList labelsFromText(const QString &s)
{
    QStringList out;
    const QJsonArray a = QJsonDocument::fromJson(s.toUtf8()).array();
    for (const QJsonValue &v : a) {
        out << v.toString();
    }
    return out;
}

QVariant nullable(const QString &s)
{
    return s.isEmpty() ? QVariant() : QVariant(s);
}

/// Sortable key that keeps all-day and timed dues comparable in SQL.
QString dueSortKey(const Due &d)
{
    if (!d.isValid()) {
        return {};
    }
    return d.hasTime() ? d.localDateTime().toString(Qt::ISODate) : d.date.left(10);
}

} // namespace

Repository *Repository::instance()
{
    static Repository self;
    return &self;
}

Repository::Repository(QObject *parent)
    : QObject(parent)
{}

QString Repository::newLocalId()
{
    return QStringLiteral("local-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool Repository::isLocalId(const QString &id) const
{
    return id.startsWith(QLatin1String("local-"));
}

QString Repository::metaValue(const QString &key) const
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT value FROM meta WHERE key = ?"));
    q.addBindValue(key);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return {};
}

void Repository::setMetaValue(const QString &key, const QString &value)
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)"));
    q.addBindValue(key);
    q.addBindValue(value);
    q.exec();
}

QString Repository::syncToken() const
{
    const QString t = metaValue(QStringLiteral("sync_token"));
    return t.isEmpty() ? QStringLiteral("*") : t;
}

void Repository::setSyncToken(const QString &token)
{
    setMetaValue(QStringLiteral("sync_token"), token);
}

User Repository::currentUser() const
{
    return User::fromJson(fromText(metaValue(QStringLiteral("user"))));
}

QString Repository::currentUserId() const
{
    if (m_cachedUserId.isEmpty()) {
        m_cachedUserId = currentUser().id;
    }
    return m_cachedUserId;
}

// ---------------------------------------------------------------------------
// Applying server payloads
// ---------------------------------------------------------------------------

void Repository::applySyncPayload(const QJsonObject &payload)
{
    QSqlDatabase database = Database::instance().db();
    database.transaction();

    if (payload.value(QStringLiteral("full_sync")).toBool()) {
        // A full sync is authoritative; drop stale rows so deletions that
        // happened while we were away do not linger.
        for (const QString &t : {QStringLiteral("projects"),
                                 QStringLiteral("sections"),
                                 QStringLiteral("items"),
                                 QStringLiteral("labels"),
                                 QStringLiteral("filters"),
                                 QStringLiteral("notes"),
                                 QStringLiteral("reminders"),
                                 QStringLiteral("collaborator_states")}) {
            QSqlQuery q(database);
            q.exec(QStringLiteral("DELETE FROM %1").arg(t));
        }
    }

    const auto each = [&payload](const char *key, auto fn) {
        const QJsonArray arr = payload.value(QLatin1String(key)).toArray();
        for (const QJsonValue &v : arr) {
            fn(v.toObject());
        }
    };

    each("projects", [this](const QJsonObject &o) {
        const Project p = Project::fromJson(o);
        p.isDeleted ? removeRow(QStringLiteral("projects"), p.id) : upsertProject(p);
    });
    each("sections", [this](const QJsonObject &o) {
        const Section s = Section::fromJson(o);
        s.isDeleted ? removeRow(QStringLiteral("sections"), s.id) : upsertSection(s);
    });
    // Items with unsent commands are newer locally than this payload, which
    // the server computed before those edits reached it.
    const QSet<QString> locallyEdited = CommandQueue::instance()->pendingObjectIds();

    each("items", [this, &locallyEdited](const QJsonObject &o) {
        const Item i = Item::fromJson(o);
        if (locallyEdited.contains(i.id)) {
            return;
        }
        i.isDeleted ? removeRow(QStringLiteral("items"), i.id) : upsertItem(i);
    });
    each("labels", [this](const QJsonObject &o) {
        const Label l = Label::fromJson(o);
        l.isDeleted ? removeRow(QStringLiteral("labels"), l.id) : upsertLabel(l);
    });
    each("filters", [this](const QJsonObject &o) {
        const Filter f = Filter::fromJson(o);
        f.isDeleted ? removeRow(QStringLiteral("filters"), f.id) : upsertFilter(f);
    });
    each("notes", [this](const QJsonObject &o) {
        const Note n = Note::fromJson(o);
        n.isDeleted ? removeRow(QStringLiteral("notes"), n.id) : upsertNote(n);
    });
    each("project_notes", [this](const QJsonObject &o) {
        const Note n = Note::fromJson(o);
        n.isDeleted ? removeRow(QStringLiteral("notes"), n.id) : upsertNote(n);
    });
    each("reminders", [this](const QJsonObject &o) {
        const Reminder r = Reminder::fromJson(o);
        r.isDeleted ? removeRow(QStringLiteral("reminders"), r.id) : upsertReminder(r);
    });
    each("collaborators", [this](const QJsonObject &o) { upsertCollaborator(Collaborator::fromJson(o)); });
    each("collaborator_states", [this](const QJsonObject &o) { upsertCollaboratorState(CollaboratorState::fromJson(o)); });

    if (payload.value(QStringLiteral("user")).isObject()) {
        setMetaValue(QStringLiteral("user"), toText(payload.value(QStringLiteral("user")).toObject()));
        m_cachedUserId.clear();
    }

    const QString token = payload.value(QStringLiteral("sync_token")).toString();
    if (!token.isEmpty()) {
        setSyncToken(token);
    }

    database.commit();

    Q_EMIT changed();
    Q_EMIT projectsChanged();
    Q_EMIT itemsChanged();
    if (payload.value(QStringLiteral("user")).isObject()) {
        Q_EMIT userChanged();
    }
}

void Repository::applyCompletedItems(const QJsonArray &items)
{
    if (items.isEmpty()) {
        return;
    }

    QSqlDatabase database = Database::instance().db();
    database.transaction();

    // A task the user just re-opened is newer locally than this payload, which
    // the server computed before that edit reached it.
    const QSet<QString> locallyEdited = CommandQueue::instance()->pendingObjectIds();

    for (const QJsonValue &v : items) {
        Item i = Item::fromJson(v.toObject());
        if (i.id.isEmpty() || locallyEdited.contains(i.id)) {
            continue;
        }
        // Everything this endpoint returns is complete by definition, but the
        // task objects it hands back do not always carry the flag.
        i.checked = true;
        upsertItem(i);
    }

    database.commit();

    Q_EMIT changed();
    Q_EMIT itemsChanged();
}

void Repository::removeRow(const QString &table, const QString &id)
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("DELETE FROM %1 WHERE id = ?").arg(table));
    q.addBindValue(id);
    q.exec();
}

void Repository::upsertProject(const Project &p)
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO projects (id, name, description, color, parent_id, workspace_id, "
                             "folder_id, role, status, view_style, child_order, is_inbox, is_shared, is_favorite, "
                             "is_archived, is_collapsed, can_assign, can_comment) "
                             "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(p.id);
    q.addBindValue(p.name);
    q.addBindValue(p.description);
    q.addBindValue(p.color);
    q.addBindValue(nullable(p.parentId));
    q.addBindValue(nullable(p.workspaceId));
    q.addBindValue(nullable(p.folderId));
    q.addBindValue(p.role);
    q.addBindValue(p.status);
    q.addBindValue(p.viewStyle);
    q.addBindValue(p.childOrder);
    q.addBindValue(p.isInbox);
    q.addBindValue(p.isShared);
    q.addBindValue(p.isFavorite);
    q.addBindValue(p.isArchived);
    q.addBindValue(p.isCollapsed);
    q.addBindValue(p.canAssignTasks);
    q.addBindValue(p.canComment);
    q.exec();
}

void Repository::upsertSection(const Section &s)
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO sections "
                             "(id, name, project_id, section_order, is_collapsed, is_archived) "
                             "VALUES (?,?,?,?,?,?)"));
    q.addBindValue(s.id);
    q.addBindValue(s.name);
    q.addBindValue(s.projectId);
    q.addBindValue(s.sectionOrder);
    q.addBindValue(s.isCollapsed);
    q.addBindValue(s.isArchived);
    q.exec();
}

void Repository::upsertItem(const Item &i)
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO items (id, content, description, project_id, section_id, parent_id, "
                             "responsible_uid, assigned_by_uid, added_by_uid, user_id, labels, due_json, due_date, "
                             "due_has_time, due_recurring, deadline_json, deadline_date, priority, child_order, "
                             "day_order, note_count, checked, is_collapsed, added_at, completed_at, updated_at) "
                             "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(i.id);
    q.addBindValue(i.content);
    q.addBindValue(i.description);
    q.addBindValue(nullable(i.projectId));
    q.addBindValue(nullable(i.sectionId));
    q.addBindValue(nullable(i.parentId));
    q.addBindValue(nullable(i.responsibleUid));
    q.addBindValue(nullable(i.assignedByUid));
    q.addBindValue(nullable(i.addedByUid));
    q.addBindValue(nullable(i.userId));
    q.addBindValue(labelsToText(i.labels));
    q.addBindValue(i.due.isValid() ? toText(i.due.toJson()) : QVariant());
    q.addBindValue(nullable(dueSortKey(i.due)));
    q.addBindValue(i.due.hasTime());
    q.addBindValue(i.due.isRecurring);
    q.addBindValue(i.deadline.isValid() ? toText(i.deadline.toJson()) : QVariant());
    q.addBindValue(nullable(i.deadline.date.left(10)));
    q.addBindValue(i.priority);
    q.addBindValue(i.childOrder);
    q.addBindValue(i.dayOrder);
    q.addBindValue(i.noteCount);
    q.addBindValue(i.checked);
    q.addBindValue(i.isCollapsed);
    q.addBindValue(i.addedAt.toString(Qt::ISODate));
    q.addBindValue(i.completedAt.isValid() ? i.completedAt.toString(Qt::ISODate) : QVariant());
    q.addBindValue(i.updatedAt.toString(Qt::ISODate));
    if (!q.exec()) {
        qWarning() << "ktodo: upsert item failed" << q.lastError().text();
    }
}

void Repository::upsertLabel(const Label &l)
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO labels "
                             "(id, name, color, item_order, is_favorite) VALUES (?,?,?,?,?)"));
    q.addBindValue(l.id);
    q.addBindValue(l.name);
    q.addBindValue(l.color);
    q.addBindValue(l.itemOrder);
    q.addBindValue(l.isFavorite);
    q.exec();
}

void Repository::upsertFilter(const Filter &f)
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO filters "
                             "(id, name, query, color, item_order, is_favorite) VALUES (?,?,?,?,?,?)"));
    q.addBindValue(f.id);
    q.addBindValue(f.name);
    q.addBindValue(f.query);
    q.addBindValue(f.color);
    q.addBindValue(f.itemOrder);
    q.addBindValue(f.isFavorite);
    q.exec();
}

void Repository::upsertNote(const Note &n)
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO notes "
                             "(id, item_id, project_id, posted_uid, content, posted_at, attachment) "
                             "VALUES (?,?,?,?,?,?,?)"));
    q.addBindValue(n.id);
    q.addBindValue(nullable(n.itemId));
    q.addBindValue(nullable(n.projectId));
    q.addBindValue(nullable(n.postedUid));
    q.addBindValue(n.content);
    q.addBindValue(n.postedAt.toString(Qt::ISODate));
    q.addBindValue(n.fileAttachment.isEmpty() ? QVariant() : QVariant(toText(n.fileAttachment)));
    q.exec();
}

void Repository::upsertReminder(const Reminder &r)
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO reminders "
                             "(id, item_id, notify_uid, type, due_json, due_date, minute_offset) "
                             "VALUES (?,?,?,?,?,?,?)"));
    q.addBindValue(r.id);
    q.addBindValue(nullable(r.itemId));
    q.addBindValue(nullable(r.notifyUid));
    q.addBindValue(r.type);
    q.addBindValue(r.due.isValid() ? toText(r.due.toJson()) : QVariant());
    q.addBindValue(nullable(dueSortKey(r.due)));
    q.addBindValue(r.minuteOffset);
    q.exec();
}

void Repository::upsertCollaborator(const Collaborator &c)
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO collaborators "
                             "(id, full_name, email, image_id, timezone) VALUES (?,?,?,?,?)"));
    q.addBindValue(c.id);
    q.addBindValue(c.fullName);
    q.addBindValue(c.email);
    q.addBindValue(nullable(c.imageId));
    q.addBindValue(c.timezone);
    q.exec();
}

void Repository::upsertCollaboratorState(const CollaboratorState &s)
{
    if (s.isDeleted) {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral("DELETE FROM collaborator_states WHERE project_id = ? AND user_id = ?"));
        q.addBindValue(s.projectId);
        q.addBindValue(s.userId);
        q.exec();
        return;
    }
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO collaborator_states "
                             "(project_id, user_id, role, state) VALUES (?,?,?,?)"));
    q.addBindValue(s.projectId);
    q.addBindValue(s.userId);
    q.addBindValue(s.role);
    q.addBindValue(s.state);
    q.exec();
}

void Repository::applyTempIdMapping(const QHash<QString, QString> &mapping)
{
    if (mapping.isEmpty()) {
        return;
    }
    QSqlDatabase database = Database::instance().db();
    database.transaction();
    for (auto it = mapping.constBegin(); it != mapping.constEnd(); ++it) {
        const QStringList updates = {
            QStringLiteral("UPDATE items SET id = ? WHERE id = ?"),
            QStringLiteral("UPDATE items SET project_id = ? WHERE project_id = ?"),
            QStringLiteral("UPDATE items SET section_id = ? WHERE section_id = ?"),
            QStringLiteral("UPDATE items SET parent_id = ? WHERE parent_id = ?"),
            QStringLiteral("UPDATE projects SET id = ? WHERE id = ?"),
            QStringLiteral("UPDATE projects SET parent_id = ? WHERE parent_id = ?"),
            QStringLiteral("UPDATE sections SET id = ? WHERE id = ?"),
            QStringLiteral("UPDATE sections SET project_id = ? WHERE project_id = ?"),
            QStringLiteral("UPDATE notes SET id = ? WHERE id = ?"),
            QStringLiteral("UPDATE notes SET item_id = ? WHERE item_id = ?"),
            QStringLiteral("UPDATE labels SET id = ? WHERE id = ?"),
            QStringLiteral("UPDATE filters SET id = ? WHERE id = ?"),
        };
        for (const QString &sql : updates) {
            QSqlQuery q(database);
            q.prepare(sql);
            q.addBindValue(it.value());
            q.addBindValue(it.key());
            q.exec();
        }
        CommandQueue::instance()->resolveTempId(it.key(), it.value());
    }
    database.commit();
    Q_EMIT changed();
    Q_EMIT itemsChanged();
    Q_EMIT projectsChanged();
}

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------

QVector<Project> Repository::projects(bool includeArchived) const
{
    QVector<Project> out;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT id, name, description, color, parent_id, workspace_id, "
                             "folder_id, role, status, view_style, child_order, is_inbox, "
                             "is_shared, is_favorite, is_archived, is_collapsed, can_assign, "
                             "can_comment FROM projects %1 ORDER BY child_order ASC, name ASC")
                  .arg(includeArchived ? QString() : QStringLiteral("WHERE is_archived = 0")));
    if (!q.exec()) {
        return out;
    }
    while (q.next()) {
        Project p;
        p.id = q.value(0).toString();
        p.name = q.value(1).toString();
        p.description = q.value(2).toString();
        p.color = q.value(3).toString();
        p.parentId = q.value(4).toString();
        p.workspaceId = q.value(5).toString();
        p.folderId = q.value(6).toString();
        p.role = q.value(7).toString();
        p.status = q.value(8).toString();
        p.viewStyle = q.value(9).toString();
        p.childOrder = q.value(10).toInt();
        p.isInbox = q.value(11).toBool();
        p.isShared = q.value(12).toBool();
        p.isFavorite = q.value(13).toBool();
        p.isArchived = q.value(14).toBool();
        p.isCollapsed = q.value(15).toBool();
        p.canAssignTasks = q.value(16).toBool();
        p.canComment = q.value(17).toBool();
        out.append(p);
    }
    return out;
}

Project Repository::project(const QString &id) const
{
    const QVector<Project> all = projects(true);
    for (const Project &p : all) {
        if (p.id == id) {
            return p;
        }
    }
    return {};
}

QVector<Section> Repository::sections(const QString &projectId) const
{
    QVector<Section> out;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT id, name, project_id, section_order, is_collapsed, is_archived "
                             "FROM sections WHERE project_id = ? AND is_archived = 0 "
                             "ORDER BY section_order ASC"));
    q.addBindValue(projectId);
    if (!q.exec()) {
        return out;
    }
    while (q.next()) {
        Section s;
        s.id = q.value(0).toString();
        s.name = q.value(1).toString();
        s.projectId = q.value(2).toString();
        s.sectionOrder = q.value(3).toInt();
        s.isCollapsed = q.value(4).toBool();
        s.isArchived = q.value(5).toBool();
        out.append(s);
    }
    return out;
}

Section Repository::section(const QString &id) const
{
    Section s;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT id, name, project_id, section_order, is_collapsed, is_archived "
                             "FROM sections WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        s.id = q.value(0).toString();
        s.name = q.value(1).toString();
        s.projectId = q.value(2).toString();
        s.sectionOrder = q.value(3).toInt();
        s.isCollapsed = q.value(4).toBool();
        s.isArchived = q.value(5).toBool();
    }
    return s;
}

QVector<Item> Repository::itemsFromSql(const QString &where, const QVariantList &binds) const
{
    QVector<Item> out;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT id, content, description, project_id, section_id, parent_id, responsible_uid, "
                             "assigned_by_uid, added_by_uid, user_id, labels, due_json, deadline_json, priority, "
                             "child_order, day_order, note_count, checked, is_collapsed, added_at, completed_at, "
                             "updated_at, due_date FROM items %1")
                  .arg(where));
    for (const QVariant &b : binds) {
        q.addBindValue(b);
    }
    if (!q.exec()) {
        qWarning() << "ktodo: item query failed" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        Item i;
        i.id = q.value(0).toString();
        i.content = q.value(1).toString();
        i.description = q.value(2).toString();
        i.projectId = q.value(3).toString();
        i.sectionId = q.value(4).toString();
        i.parentId = q.value(5).toString();
        i.responsibleUid = q.value(6).toString();
        i.assignedByUid = q.value(7).toString();
        i.addedByUid = q.value(8).toString();
        i.userId = q.value(9).toString();
        i.labels = labelsFromText(q.value(10).toString());
        if (!q.value(11).isNull()) {
            i.due = Due::fromJson(fromText(q.value(11).toString()));
        }
        if (!q.value(12).isNull()) {
            i.deadline = Due::fromJson(fromText(q.value(12).toString()));
        }
        i.priority = q.value(13).toInt();
        i.childOrder = q.value(14).toInt();
        i.dayOrder = q.value(15).toInt();
        i.noteCount = q.value(16).toInt();
        i.checked = q.value(17).toBool();
        i.isCollapsed = q.value(18).toBool();
        i.addedAt = QDateTime::fromString(q.value(19).toString(), Qt::ISODate);
        i.completedAt = QDateTime::fromString(q.value(20).toString(), Qt::ISODate);
        i.updatedAt = QDateTime::fromString(q.value(21).toString(), Qt::ISODate);
        out.append(i);
    }
    return out;
}

QVector<Item> Repository::items(const TaskQuery &query) const
{
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    QString where;
    QVariantList binds;

    // "Show completed" widens a list rather than swapping it: completed tasks
    // join the active ones instead of replacing them, so the clause drops out
    // entirely instead of flipping to `checked = 1`.
    const QString active = query.includeCompleted ? QStringLiteral("1") : QStringLiteral("checked = 0");
    // Completed tasks keep whatever child_order they had, which is meaningless
    // once they leave the list, so they are grouped after the active ones and
    // shown most-recently-finished first.
    const QString manualOrder = query.includeCompleted ? QStringLiteral("ORDER BY checked ASC, child_order ASC, completed_at DESC")
                                                       : QStringLiteral("ORDER BY child_order ASC");

    switch (query.kind) {
    case TaskQuery::Project:
        where = QStringLiteral("WHERE project_id = ? AND %1 %2").arg(active, manualOrder);
        binds << query.projectId;
        break;

    case TaskQuery::Inbox:
        where = QStringLiteral("WHERE project_id = ? AND %1 %2").arg(active, manualOrder);
        binds << query.projectId;
        break;

    case TaskQuery::Today:
        // Overdue tasks belong in Today, matching Todoist's own behavior.
        where = QStringLiteral("WHERE %1 AND due_date IS NOT NULL AND due_date <= ? "
                               "ORDER BY due_date ASC, priority DESC, child_order ASC")
                    .arg(active);
        binds << today + QStringLiteral("T23:59:59");
        break;

    case TaskQuery::Upcoming:
        where = QStringLiteral("WHERE %1 AND due_date IS NOT NULL "
                               "AND due_date >= ? AND due_date <= ? "
                               "ORDER BY due_date ASC, priority DESC, child_order ASC")
                    .arg(active);
        binds << query.rangeStart.toString(Qt::ISODate) << query.rangeEnd.toString(Qt::ISODate) + QStringLiteral("T23:59:59");
        break;

    case TaskQuery::Label:
        where = QStringLiteral("WHERE %1 AND labels LIKE ? "
                               "ORDER BY due_date ASC, priority DESC")
                    .arg(active);
        binds << QStringLiteral("%\"") + query.labelName + QStringLiteral("\"%");
        break;

    case TaskQuery::Search:
        where = QStringLiteral("WHERE (content LIKE ? OR description LIKE ?) AND %1 "
                               "ORDER BY due_date ASC, priority DESC LIMIT 300")
                    .arg(active);
        binds << QStringLiteral("%") + query.searchText + QStringLiteral("%") << QStringLiteral("%") + query.searchText + QStringLiteral("%");
        break;

    case TaskQuery::AssignedToMe:
        where = QStringLiteral("WHERE %1 AND responsible_uid = ? "
                               "ORDER BY due_date ASC, priority DESC")
                    .arg(active);
        binds << currentUserId();
        break;

    case TaskQuery::Completed:
        where = QStringLiteral("WHERE checked = 1 ORDER BY completed_at DESC LIMIT 200");
        break;

    case TaskQuery::SavedFilter: {
        // The filter language is richer than SQL can express directly, so the
        // candidate set is narrowed here and evaluated in memory.
        const QVector<Item> all = itemsFromSql(QStringLiteral("WHERE %1").arg(active), {});
        FilterQuery fq(query.filterQuery, this);
        QVector<Item> matched;
        for (const Item &i : all) {
            if (fq.matches(i)) {
                matched.append(i);
            }
        }
        std::stable_sort(matched.begin(), matched.end(), [](const Item &a, const Item &b) {
            const QString ka = dueSortKey(a.due);
            const QString kb = dueSortKey(b.due);
            if (ka.isEmpty() != kb.isEmpty()) {
                return !ka.isEmpty();
            }
            if (ka != kb) {
                return ka < kb;
            }
            return a.priority > b.priority;
        });
        return matched;
    }
    }

    return itemsFromSql(where, binds);
}

Item Repository::item(const QString &id) const
{
    const QVector<Item> found = itemsFromSql(QStringLiteral("WHERE id = ?"), {id});
    return found.isEmpty() ? Item() : found.first();
}

QVector<Item> Repository::subtasks(const QString &parentId) const
{
    return itemsFromSql(QStringLiteral("WHERE parent_id = ? ORDER BY child_order ASC"), {parentId});
}

QHash<QString, SubtaskCount> Repository::subtaskCounts() const
{
    QHash<QString, SubtaskCount> out;
    QSqlQuery q(Database::instance().db());
    if (!q.exec(QStringLiteral("SELECT parent_id, COUNT(*), SUM(checked) FROM items "
                               "WHERE parent_id IS NOT NULL AND parent_id != '' GROUP BY parent_id"))) {
        qWarning() << "ktodo: subtask count query failed" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        SubtaskCount count;
        count.total = q.value(1).toInt();
        count.completed = q.value(2).toInt();
        out.insert(q.value(0).toString(), count);
    }
    return out;
}

QVector<Label> Repository::labels() const
{
    QVector<Label> out;
    QSqlQuery q(Database::instance().db());
    if (!q.exec(QStringLiteral("SELECT id, name, color, item_order, is_favorite FROM labels "
                               "ORDER BY item_order ASC, name ASC"))) {
        return out;
    }
    while (q.next()) {
        Label l;
        l.id = q.value(0).toString();
        l.name = q.value(1).toString();
        l.color = q.value(2).toString();
        l.itemOrder = q.value(3).toInt();
        l.isFavorite = q.value(4).toBool();
        out.append(l);
    }
    return out;
}

QVector<Filter> Repository::filters() const
{
    QVector<Filter> out;
    QSqlQuery q(Database::instance().db());
    if (!q.exec(QStringLiteral("SELECT id, name, query, color, item_order, is_favorite FROM filters "
                               "ORDER BY item_order ASC, name ASC"))) {
        return out;
    }
    while (q.next()) {
        Filter f;
        f.id = q.value(0).toString();
        f.name = q.value(1).toString();
        f.query = q.value(2).toString();
        f.color = q.value(3).toString();
        f.itemOrder = q.value(4).toInt();
        f.isFavorite = q.value(5).toBool();
        out.append(f);
    }
    return out;
}

QVector<Note> Repository::notes(const QString &itemId) const
{
    QVector<Note> out;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT id, item_id, project_id, posted_uid, content, posted_at, "
                             "attachment FROM notes WHERE item_id = ? ORDER BY posted_at ASC"));
    q.addBindValue(itemId);
    if (!q.exec()) {
        return out;
    }
    while (q.next()) {
        Note n;
        n.id = q.value(0).toString();
        n.itemId = q.value(1).toString();
        n.projectId = q.value(2).toString();
        n.postedUid = q.value(3).toString();
        n.content = q.value(4).toString();
        n.postedAt = QDateTime::fromString(q.value(5).toString(), Qt::ISODate);
        n.fileAttachment = fromText(q.value(6).toString());
        out.append(n);
    }
    return out;
}

QVector<Collaborator> Repository::collaborators(const QString &projectId) const
{
    QVector<Collaborator> out;
    QSqlQuery q(Database::instance().db());
    if (projectId.isEmpty()) {
        q.prepare(QStringLiteral("SELECT id, full_name, email, image_id, timezone "
                                 "FROM collaborators ORDER BY full_name ASC"));
    } else {
        q.prepare(QStringLiteral("SELECT c.id, c.full_name, c.email, c.image_id, c.timezone FROM collaborators c "
                                 "JOIN collaborator_states s ON s.user_id = c.id "
                                 "WHERE s.project_id = ? ORDER BY c.full_name ASC"));
        q.addBindValue(projectId);
    }
    if (!q.exec()) {
        return out;
    }
    while (q.next()) {
        Collaborator c;
        c.id = q.value(0).toString();
        c.fullName = q.value(1).toString();
        c.email = q.value(2).toString();
        c.imageId = q.value(3).toString();
        c.timezone = q.value(4).toString();
        out.append(c);
    }
    return out;
}

Collaborator Repository::collaborator(const QString &userId) const
{
    Collaborator c;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT id, full_name, email, image_id, timezone FROM collaborators WHERE id = ?"));
    q.addBindValue(userId);
    if (q.exec() && q.next()) {
        c.id = q.value(0).toString();
        c.fullName = q.value(1).toString();
        c.email = q.value(2).toString();
        c.imageId = q.value(3).toString();
        c.timezone = q.value(4).toString();
    }
    return c;
}

QVector<Reminder> Repository::reminders() const
{
    QVector<Reminder> out;
    QSqlQuery q(Database::instance().db());
    if (!q.exec(QStringLiteral("SELECT id, item_id, notify_uid, type, due_json, minute_offset "
                               "FROM reminders"))) {
        return out;
    }
    while (q.next()) {
        Reminder r;
        r.id = q.value(0).toString();
        r.itemId = q.value(1).toString();
        r.notifyUid = q.value(2).toString();
        r.type = q.value(3).toString();
        if (!q.value(4).isNull()) {
            r.due = Due::fromJson(fromText(q.value(4).toString()));
        }
        r.minuteOffset = q.value(5).toInt();
        out.append(r);
    }
    return out;
}

bool Repository::wasNotified(const QString &key) const
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT 1 FROM notified WHERE key = ?"));
    q.addBindValue(key);
    return q.exec() && q.next();
}

void Repository::markNotified(const QString &key)
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO notified (key, notified_at) VALUES (?, ?)"));
    q.addBindValue(key);
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.exec();
}

void Repository::clearAll()
{
    Database::instance().wipe();
    m_cachedUserId.clear();
    Q_EMIT changed();
    Q_EMIT projectsChanged();
    Q_EMIT itemsChanged();
    Q_EMIT userChanged();
}

// ---------------------------------------------------------------------------
// Mutations
//
// Every mutation writes to the cache first and queues the matching command
// second, so the UI updates instantly and survives being offline. The server
// remains authoritative: the next sync overwrites whatever it disagrees with.
// ---------------------------------------------------------------------------

QString Repository::addItem(const QString &content,
                            const QString &projectId,
                            const QString &sectionId,
                            const QString &parentId,
                            const QString &dueString,
                            int priority,
                            const QStringList &labels,
                            const QString &description,
                            const QString &responsibleUid)
{
    const QString localId = newLocalId();

    Item i;
    i.id = localId;
    i.content = content;
    i.description = description;
    i.projectId = projectId;
    i.sectionId = sectionId;
    i.parentId = parentId;
    i.priority = priority;
    i.labels = labels;
    i.responsibleUid = responsibleUid;
    i.addedAt = QDateTime::currentDateTime();
    i.updatedAt = i.addedAt;
    i.userId = currentUserId();
    i.addedByUid = currentUserId();
    if (!dueString.isEmpty()) {
        // The server owns natural-language parsing; a local guess only exists
        // so the task lands in the right list before the first sync.
        i.due = DueDate::guessLocal(dueString);
        i.due.string = dueString;
    }
    upsertItem(i);

    QJsonObject args;
    args[QStringLiteral("content")] = content;
    if (!description.isEmpty()) {
        args[QStringLiteral("description")] = description;
    }
    if (!projectId.isEmpty()) {
        args[QStringLiteral("project_id")] = projectId;
    }
    if (!sectionId.isEmpty()) {
        args[QStringLiteral("section_id")] = sectionId;
    }
    if (!parentId.isEmpty()) {
        args[QStringLiteral("parent_id")] = parentId;
    }
    if (!dueString.isEmpty()) {
        args[QStringLiteral("due")] = QJsonObject{{QStringLiteral("string"), dueString}};
    }
    if (priority > 1) {
        args[QStringLiteral("priority")] = priority;
    }
    if (!labels.isEmpty()) {
        QJsonArray a;
        for (const QString &l : labels) {
            a.append(l);
        }
        args[QStringLiteral("labels")] = a;
    }
    if (!responsibleUid.isEmpty()) {
        args[QStringLiteral("responsible_uid")] = responsibleUid;
    }

    CommandQueue::instance()->enqueue(QStringLiteral("item_add"), args, localId);
    Q_EMIT changed();
    Q_EMIT itemsChanged();
    return localId;
}

void Repository::updateItem(const QString &id, const QJsonObject &changes)
{
    Item i = item(id);
    if (i.id.isEmpty()) {
        return;
    }

    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        const QString key = it.key();
        if (key == QLatin1String("content")) {
            i.content = it.value().toString();
        } else if (key == QLatin1String("description")) {
            i.description = it.value().toString();
        } else if (key == QLatin1String("priority")) {
            i.priority = it.value().toInt();
        } else if (key == QLatin1String("responsible_uid")) {
            i.responsibleUid = it.value().toString();
        } else if (key == QLatin1String("labels")) {
            i.labels.clear();
            for (const QJsonValue &v : it.value().toArray()) {
                i.labels << v.toString();
            }
        } else if (key == QLatin1String("due")) {
            if (it.value().isNull()) {
                i.due = Due();
            } else {
                const QJsonObject d = it.value().toObject();
                const QString s = d.value(QStringLiteral("string")).toString();
                i.due = d.contains(QStringLiteral("date")) ? Due::fromJson(d) : DueDate::guessLocal(s);
                i.due.string = s;
            }
        } else if (key == QLatin1String("is_collapsed")) {
            i.isCollapsed = it.value().toBool();
        } else if (key == QLatin1String("deadline")) {
            i.deadline = it.value().isNull() ? Due() : Due::fromJson(it.value().toObject());
        }
    }
    i.updatedAt = QDateTime::currentDateTime();
    upsertItem(i);

    QJsonObject args = changes;
    args[QStringLiteral("id")] = id;
    CommandQueue::instance()->enqueue(QStringLiteral("item_update"), args);

    Q_EMIT changed();
    Q_EMIT itemsChanged();
}

void Repository::setItemContent(const QString &id, const QString &content, const QString &description)
{
    updateItem(id, QJsonObject{{QStringLiteral("content"), content}, {QStringLiteral("description"), description}});
}

void Repository::setItemDue(const QString &id, const QString &dueString)
{
    updateItem(id, QJsonObject{{QStringLiteral("due"), QJsonObject{{QStringLiteral("string"), dueString}}}});
}

void Repository::clearItemDue(const QString &id)
{
    updateItem(id, QJsonObject{{QStringLiteral("due"), QJsonValue::Null}});
}

void Repository::setItemPriority(const QString &id, int apiPriority)
{
    updateItem(id, QJsonObject{{QStringLiteral("priority"), apiPriority}});
}

void Repository::setItemLabels(const QString &id, const QStringList &labels)
{
    QJsonArray a;
    for (const QString &l : labels) {
        a.append(l);
    }
    updateItem(id, QJsonObject{{QStringLiteral("labels"), a}});
}

void Repository::setItemAssignee(const QString &id, const QString &userId)
{
    updateItem(id, QJsonObject{{QStringLiteral("responsible_uid"), userId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(userId)}});
}

void Repository::setItemCollapsed(const QString &id, bool collapsed)
{
    const Item existing = item(id);
    if (existing.id.isEmpty() || existing.isCollapsed == collapsed) {
        return;
    }

    // Folding is a write to the task, which a read-only collaborator may not
    // make. Keeping it on this device costs only cross-client agreement and
    // beats queueing a command the server would reject.
    if (project(existing.projectId).isReadOnly()) {
        Item updated = existing;
        updated.isCollapsed = collapsed;
        upsertItem(updated);
        Q_EMIT changed();
        Q_EMIT itemsChanged();
        return;
    }

    updateItem(id, QJsonObject{{QStringLiteral("is_collapsed"), collapsed}});
}

void Repository::moveItem(const QString &id, const QString &projectId, const QString &sectionId, const QString &parentId)
{
    Item i = item(id);
    if (i.id.isEmpty()) {
        return;
    }
    i.projectId = projectId;
    i.sectionId = sectionId;
    i.parentId = parentId;
    upsertItem(i);

    // item_move accepts exactly one destination; the most specific wins.
    QJsonObject args{{QStringLiteral("id"), id}};
    if (!parentId.isEmpty()) {
        args[QStringLiteral("parent_id")] = parentId;
    } else if (!sectionId.isEmpty()) {
        args[QStringLiteral("section_id")] = sectionId;
    } else {
        args[QStringLiteral("project_id")] = projectId;
    }
    CommandQueue::instance()->enqueue(QStringLiteral("item_move"), args);

    Q_EMIT changed();
    Q_EMIT itemsChanged();
}

void Repository::reorderItems(const QVector<QPair<QString, int>> &idsAndOrders)
{
    if (idsAndOrders.isEmpty()) {
        return;
    }
    QSqlDatabase database = Database::instance().db();
    database.transaction();
    QJsonArray entries;
    for (const auto &[id, order] : idsAndOrders) {
        QSqlQuery q(database);
        q.prepare(QStringLiteral("UPDATE items SET child_order = ? WHERE id = ?"));
        q.addBindValue(order);
        q.addBindValue(id);
        q.exec();
        entries.append(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("child_order"), order}});
    }
    database.commit();

    CommandQueue::instance()->enqueue(QStringLiteral("item_reorder"), QJsonObject{{QStringLiteral("items"), entries}});
    Q_EMIT changed();
    Q_EMIT itemsChanged();
}

void Repository::completeItem(const QString &id)
{
    Item i = item(id);
    if (i.id.isEmpty()) {
        return;
    }

    // For a recurring task the server advances the due date instead of
    // closing it. We cannot compute the next occurrence locally, so the task
    // is hidden optimistically and the next sync restores it with a new date.
    i.checked = true;
    i.completedAt = QDateTime::currentDateTime();
    upsertItem(i);

    CommandQueue::instance()->enqueue(QStringLiteral("item_close"), QJsonObject{{QStringLiteral("id"), id}});
    Q_EMIT changed();
    Q_EMIT itemsChanged();
}

void Repository::uncompleteItem(const QString &id)
{
    Item i = item(id);
    if (i.id.isEmpty()) {
        return;
    }
    i.checked = false;
    i.completedAt = QDateTime();
    upsertItem(i);

    CommandQueue::instance()->enqueue(QStringLiteral("item_uncomplete"), QJsonObject{{QStringLiteral("id"), id}});
    Q_EMIT changed();
    Q_EMIT itemsChanged();
}

void Repository::deleteItem(const QString &id)
{
    // Subtasks disappear with their parent server-side; mirror that locally
    // so the list does not briefly show orphans.
    for (const Item &child : subtasks(id)) {
        removeRow(QStringLiteral("items"), child.id);
    }
    removeRow(QStringLiteral("items"), id);

    CommandQueue::instance()->enqueue(QStringLiteral("item_delete"), QJsonObject{{QStringLiteral("id"), id}});
    Q_EMIT changed();
    Q_EMIT itemsChanged();
}

QString Repository::addProject(const QString &name, const QString &color, const QString &parentId, bool isFavorite)
{
    const QString localId = newLocalId();

    Project p;
    p.id = localId;
    p.name = name;
    p.color = color;
    p.parentId = parentId;
    p.isFavorite = isFavorite;
    p.canComment = true;
    p.role = QStringLiteral("CREATOR");
    upsertProject(p);

    QJsonObject args{{QStringLiteral("name"), name}, {QStringLiteral("color"), color}};
    if (!parentId.isEmpty()) {
        args[QStringLiteral("parent_id")] = parentId;
    }
    if (isFavorite) {
        args[QStringLiteral("is_favorite")] = true;
    }
    CommandQueue::instance()->enqueue(QStringLiteral("project_add"), args, localId);

    Q_EMIT changed();
    Q_EMIT projectsChanged();
    return localId;
}

void Repository::updateProject(const QString &id, const QJsonObject &changes)
{
    Project p = project(id);
    if (p.id.isEmpty()) {
        return;
    }
    if (changes.contains(QStringLiteral("name"))) {
        p.name = changes.value(QStringLiteral("name")).toString();
    }
    if (changes.contains(QStringLiteral("color"))) {
        p.color = changes.value(QStringLiteral("color")).toString();
    }
    if (changes.contains(QStringLiteral("is_favorite"))) {
        p.isFavorite = changes.value(QStringLiteral("is_favorite")).toBool();
    }
    if (changes.contains(QStringLiteral("description"))) {
        p.description = changes.value(QStringLiteral("description")).toString();
    }
    upsertProject(p);

    QJsonObject args = changes;
    args[QStringLiteral("id")] = id;
    CommandQueue::instance()->enqueue(QStringLiteral("project_update"), args);

    Q_EMIT changed();
    Q_EMIT projectsChanged();
}

void Repository::deleteProject(const QString &id)
{
    QSqlQuery items(Database::instance().db());
    items.prepare(QStringLiteral("DELETE FROM items WHERE project_id = ?"));
    items.addBindValue(id);
    items.exec();

    QSqlQuery secs(Database::instance().db());
    secs.prepare(QStringLiteral("DELETE FROM sections WHERE project_id = ?"));
    secs.addBindValue(id);
    secs.exec();

    removeRow(QStringLiteral("projects"), id);
    CommandQueue::instance()->enqueue(QStringLiteral("project_delete"), QJsonObject{{QStringLiteral("id"), id}});
    Q_EMIT changed();
    Q_EMIT projectsChanged();
    Q_EMIT itemsChanged();
}

void Repository::archiveProject(const QString &id)
{
    Project p = project(id);
    if (p.id.isEmpty()) {
        return;
    }
    p.isArchived = true;
    upsertProject(p);

    CommandQueue::instance()->enqueue(QStringLiteral("project_archive"), QJsonObject{{QStringLiteral("id"), id}});
    Q_EMIT changed();
    Q_EMIT projectsChanged();
}

QString Repository::addSection(const QString &name, const QString &projectId)
{
    const QString localId = newLocalId();

    Section s;
    s.id = localId;
    s.name = name;
    s.projectId = projectId;
    upsertSection(s);

    CommandQueue::instance()->enqueue(
        QStringLiteral("section_add"), QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("project_id"), projectId}}, localId);

    Q_EMIT changed();
    return localId;
}

void Repository::updateSection(const QString &id, const QJsonObject &changes)
{
    Section s = section(id);
    if (s.id.isEmpty()) {
        return;
    }
    if (changes.contains(QStringLiteral("name"))) {
        s.name = changes.value(QStringLiteral("name")).toString();
    }
    if (changes.contains(QStringLiteral("collapsed"))) {
        s.isCollapsed = changes.value(QStringLiteral("collapsed")).toBool();
    }
    upsertSection(s);

    QJsonObject args = changes;
    args[QStringLiteral("id")] = id;
    CommandQueue::instance()->enqueue(QStringLiteral("section_update"), args);
    Q_EMIT changed();
}

void Repository::deleteSection(const QString &id)
{
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("DELETE FROM items WHERE section_id = ?"));
    q.addBindValue(id);
    q.exec();

    removeRow(QStringLiteral("sections"), id);
    CommandQueue::instance()->enqueue(QStringLiteral("section_delete"), QJsonObject{{QStringLiteral("id"), id}});
    Q_EMIT changed();
    Q_EMIT itemsChanged();
}

QString Repository::addNote(const QString &itemId, const QString &content)
{
    const QString localId = newLocalId();

    Note n;
    n.id = localId;
    n.itemId = itemId;
    n.content = content;
    n.postedUid = currentUserId();
    n.postedAt = QDateTime::currentDateTime();
    upsertNote(n);

    CommandQueue::instance()->enqueue(
        QStringLiteral("note_add"), QJsonObject{{QStringLiteral("item_id"), itemId}, {QStringLiteral("content"), content}}, localId);

    Q_EMIT changed();
    Q_EMIT notesChanged(itemId);
    return localId;
}

void Repository::deleteNote(const QString &id)
{
    QSqlQuery lookup(Database::instance().db());
    lookup.prepare(QStringLiteral("SELECT item_id FROM notes WHERE id = ?"));
    lookup.addBindValue(id);
    const QString itemId = (lookup.exec() && lookup.next()) ? lookup.value(0).toString() : QString();

    removeRow(QStringLiteral("notes"), id);
    CommandQueue::instance()->enqueue(QStringLiteral("note_delete"), QJsonObject{{QStringLiteral("id"), id}});
    Q_EMIT changed();
    Q_EMIT notesChanged(itemId);
}

QString Repository::addLabel(const QString &name, const QString &color)
{
    const QString localId = newLocalId();

    Label l;
    l.id = localId;
    l.name = name;
    l.color = color;
    upsertLabel(l);

    CommandQueue::instance()->enqueue(
        QStringLiteral("label_add"), QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("color"), color}}, localId);

    Q_EMIT changed();
    return localId;
}

void Repository::deleteLabel(const QString &id)
{
    removeRow(QStringLiteral("labels"), id);
    CommandQueue::instance()->enqueue(QStringLiteral("label_delete"), QJsonObject{{QStringLiteral("id"), id}});
    Q_EMIT changed();
}

QString Repository::addFilter(const QString &name, const QString &query, const QString &color)
{
    const QString localId = newLocalId();

    Filter f;
    f.id = localId;
    f.name = name;
    f.query = query;
    f.color = color;
    upsertFilter(f);

    CommandQueue::instance()->enqueue(QStringLiteral("filter_add"),
                                      QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("query"), query}, {QStringLiteral("color"), color}},
                                      localId);
    Q_EMIT changed();
    return localId;
}

void Repository::updateFilter(const QString &id, const QJsonObject &changes)
{
    QJsonObject args = changes;
    args[QStringLiteral("id")] = id;
    CommandQueue::instance()->enqueue(QStringLiteral("filter_update"), args);
    Q_EMIT changed();
}

void Repository::deleteFilter(const QString &id)
{
    removeRow(QStringLiteral("filters"), id);
    CommandQueue::instance()->enqueue(QStringLiteral("filter_delete"), QJsonObject{{QStringLiteral("id"), id}});
    Q_EMIT changed();
}
