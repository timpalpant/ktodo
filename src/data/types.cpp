#include "types.h"

#include <QJsonArray>
#include <QTimeZone>

namespace Todoist {

namespace {

QString str(const QJsonObject &o, const char *key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? v.toString() : QString();
}

bool boolean(const QJsonObject &o, const char *key, bool fallback = false)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isBool() ? v.toBool() : fallback;
}

int integer(const QJsonObject &o, const char *key, int fallback = 0)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() ? v.toInt() : fallback;
}

QDateTime timestamp(const QJsonObject &o, const char *key)
{
    const QString s = str(o, key);
    return s.isEmpty() ? QDateTime() : QDateTime::fromString(s, Qt::ISODateWithMs).toLocalTime();
}

} // namespace

QDate Due::localDate() const
{
    if (date.isEmpty()) {
        return {};
    }
    if (!hasTime()) {
        return QDate::fromString(date.left(10), Qt::ISODate);
    }
    return localDateTime().date();
}

QDateTime Due::localDateTime() const
{
    if (date.isEmpty()) {
        return {};
    }
    if (!hasTime()) {
        return QDateTime(QDate::fromString(date.left(10), Qt::ISODate), QTime(0, 0));
    }

    QDateTime dt = QDateTime::fromString(date, Qt::ISODateWithMs);
    if (!dt.isValid()) {
        return {};
    }
    // A due with an explicit timezone is a fixed instant; a floating one is
    // already expressed in the user's wall-clock time and must not be shifted.
    if (!timezone.isEmpty() || date.endsWith(QLatin1Char('Z'))) {
        return dt.toLocalTime();
    }
    dt.setTimeZone(QTimeZone::LocalTime);
    return dt;
}

Due Due::fromJson(const QJsonObject &o)
{
    Due d;
    d.string = str(o, "string");
    d.date = str(o, "date");
    d.timezone = str(o, "timezone");
    d.lang = str(o, "lang");
    if (d.lang.isEmpty()) {
        d.lang = QStringLiteral("en");
    }
    d.isRecurring = boolean(o, "is_recurring");
    return d;
}

QJsonObject Due::toJson() const
{
    QJsonObject o;
    if (!string.isEmpty()) {
        o[QStringLiteral("string")] = string;
    }
    if (!date.isEmpty()) {
        o[QStringLiteral("date")] = date;
    }
    if (!timezone.isEmpty()) {
        o[QStringLiteral("timezone")] = timezone;
    }
    o[QStringLiteral("is_recurring")] = isRecurring;
    o[QStringLiteral("lang")] = lang;
    return o;
}

Project Project::fromJson(const QJsonObject &o)
{
    Project p;
    p.id = str(o, "id");
    p.name = str(o, "name");
    p.description = str(o, "description");
    p.color = str(o, "color");
    p.parentId = str(o, "parent_id");
    p.workspaceId = str(o, "workspace_id");
    p.folderId = str(o, "folder_id");
    p.role = str(o, "role");
    p.status = str(o, "status");
    p.viewStyle = str(o, "view_style");
    if (p.viewStyle.isEmpty()) {
        p.viewStyle = QStringLiteral("list");
    }
    if (p.color.isEmpty()) {
        p.color = QStringLiteral("charcoal");
    }
    p.childOrder = integer(o, "child_order");
    p.isInbox = boolean(o, "inbox_project");
    p.isShared = boolean(o, "is_shared");
    p.isFavorite = boolean(o, "is_favorite");
    p.isArchived = boolean(o, "is_archived");
    p.isCollapsed = boolean(o, "is_collapsed");
    p.isDeleted = boolean(o, "is_deleted");
    p.canAssignTasks = boolean(o, "can_assign_tasks");
    p.canComment = boolean(o, "can_comment", true);
    return p;
}

Section Section::fromJson(const QJsonObject &o)
{
    Section s;
    s.id = str(o, "id");
    s.name = str(o, "name");
    s.projectId = str(o, "project_id");
    s.sectionOrder = integer(o, "section_order");
    s.isCollapsed = boolean(o, "is_collapsed");
    s.isArchived = boolean(o, "is_archived");
    s.isDeleted = boolean(o, "is_deleted");
    return s;
}

Item Item::fromJson(const QJsonObject &o)
{
    Item i;
    i.id = str(o, "id");
    i.content = str(o, "content");
    i.description = str(o, "description");
    i.projectId = str(o, "project_id");
    i.sectionId = str(o, "section_id");
    i.parentId = str(o, "parent_id");
    i.responsibleUid = str(o, "responsible_uid");
    i.assignedByUid = str(o, "assigned_by_uid");
    i.addedByUid = str(o, "added_by_uid");
    i.userId = str(o, "user_id");

    const QJsonArray labels = o.value(QStringLiteral("labels")).toArray();
    for (const QJsonValue &l : labels) {
        i.labels << l.toString();
    }

    if (o.value(QStringLiteral("due")).isObject()) {
        i.due = Due::fromJson(o.value(QStringLiteral("due")).toObject());
    }
    if (o.value(QStringLiteral("deadline")).isObject()) {
        i.deadline = Due::fromJson(o.value(QStringLiteral("deadline")).toObject());
    }

    i.priority = integer(o, "priority", 1);
    i.childOrder = integer(o, "child_order");
    i.dayOrder = integer(o, "day_order", -1);
    i.noteCount = integer(o, "note_count");
    i.checked = boolean(o, "checked");
    i.isCollapsed = boolean(o, "is_collapsed");
    i.isDeleted = boolean(o, "is_deleted");
    i.addedAt = timestamp(o, "added_at");
    i.completedAt = timestamp(o, "completed_at");
    i.updatedAt = timestamp(o, "updated_at");
    return i;
}

Label Label::fromJson(const QJsonObject &o)
{
    Label l;
    l.id = str(o, "id");
    l.name = str(o, "name");
    l.color = str(o, "color");
    if (l.color.isEmpty()) {
        l.color = QStringLiteral("charcoal");
    }
    l.itemOrder = integer(o, "item_order");
    l.isFavorite = boolean(o, "is_favorite");
    l.isDeleted = boolean(o, "is_deleted");
    return l;
}

QString Collaborator::avatarUrl() const
{
    if (imageId.isEmpty()) {
        return {};
    }
    return QStringLiteral("https://dcff1xvirvpfp.cloudfront.net/%1_medium.jpg").arg(imageId);
}

Collaborator Collaborator::fromJson(const QJsonObject &o)
{
    Collaborator c;
    c.id = str(o, "id");
    c.fullName = str(o, "full_name");
    c.email = str(o, "email");
    c.imageId = str(o, "image_id");
    c.timezone = str(o, "timezone");
    return c;
}

CollaboratorState CollaboratorState::fromJson(const QJsonObject &o)
{
    CollaboratorState s;
    s.projectId = str(o, "project_id");
    s.userId = str(o, "user_id");
    s.role = str(o, "role");
    s.state = str(o, "state");
    s.isDeleted = boolean(o, "is_deleted");
    return s;
}

Filter Filter::fromJson(const QJsonObject &o)
{
    Filter f;
    f.id = str(o, "id");
    f.name = str(o, "name");
    f.query = str(o, "query");
    f.color = str(o, "color");
    if (f.color.isEmpty()) {
        f.color = QStringLiteral("charcoal");
    }
    f.itemOrder = integer(o, "item_order");
    f.isFavorite = boolean(o, "is_favorite");
    f.isDeleted = boolean(o, "is_deleted");
    return f;
}

Note Note::fromJson(const QJsonObject &o)
{
    Note n;
    n.id = str(o, "id");
    n.itemId = str(o, "item_id");
    n.projectId = str(o, "project_id");
    n.postedUid = str(o, "posted_uid");
    n.content = str(o, "content");
    n.postedAt = timestamp(o, "posted_at");
    n.fileAttachment = o.value(QStringLiteral("file_attachment")).toObject();
    n.isDeleted = boolean(o, "is_deleted");
    return n;
}

Reminder Reminder::fromJson(const QJsonObject &o)
{
    Reminder r;
    r.id = str(o, "id");
    r.itemId = str(o, "item_id");
    r.notifyUid = str(o, "notify_uid");
    r.type = str(o, "type");
    if (o.value(QStringLiteral("due")).isObject()) {
        r.due = Due::fromJson(o.value(QStringLiteral("due")).toObject());
    }
    r.minuteOffset = integer(o, "minute_offset");
    r.isDeleted = boolean(o, "is_deleted");
    return r;
}

QString User::avatarUrl() const
{
    if (imageId.isEmpty()) {
        return {};
    }
    return QStringLiteral("https://dcff1xvirvpfp.cloudfront.net/%1_medium.jpg").arg(imageId);
}

User User::fromJson(const QJsonObject &o)
{
    User u;
    u.id = str(o, "id");
    u.fullName = str(o, "full_name");
    u.email = str(o, "email");
    u.inboxProjectId = str(o, "inbox_project_id");
    u.teamInboxId = str(o, "team_inbox_id");
    u.imageId = str(o, "image_id");
    u.businessAccountId = str(o, "business_account_id");
    u.timezone = o.value(QStringLiteral("tz_info")).toObject().value(QStringLiteral("timezone")).toString();
    u.isPremium = boolean(o, "is_premium");
    return u;
}

} // namespace Todoist
