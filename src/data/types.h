#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace Todoist {

/**
 * A Todoist due date. The API returns @c date either as a plain "2026-08-02"
 * (a floating, all-day date) or as an RFC3339 timestamp, and the distinction
 * matters for both display and reminders, so it is preserved here rather than
 * normalised to QDateTime.
 */
struct Due {
    QString string;   ///< Human text as entered, e.g. "every Sun, Wed".
    QString date;     ///< "YYYY-MM-DD" or RFC3339.
    QString timezone; ///< Null for floating dates.
    QString lang = QStringLiteral("en");
    bool isRecurring = false;

    bool isValid() const { return !date.isEmpty(); }
    bool hasTime() const { return date.contains(QLatin1Char('T')); }

    QDate localDate() const;
    QDateTime localDateTime() const;

    static Due fromJson(const QJsonObject &o);
    QJsonObject toJson() const;

    bool operator==(const Due &) const = default;
};

struct Project {
    QString id;
    QString name;
    QString description;
    QString color = QStringLiteral("charcoal");
    QString parentId;
    QString workspaceId; ///< Only present on team/workspace projects.
    QString folderId;
    QString role; ///< CREATOR / ADMIN / READ_WRITE / READ_ONLY ...
    QString viewStyle = QStringLiteral("list");
    QString status;
    int childOrder = 0;
    bool isInbox = false;
    bool isShared = false;
    bool isFavorite = false;
    bool isArchived = false;
    bool isCollapsed = false;
    bool isDeleted = false;
    bool canAssignTasks = false;
    bool canComment = true;

    bool isTeamProject() const { return !workspaceId.isEmpty(); }
    /// Read-only projects must not offer edit affordances in the UI.
    bool isReadOnly() const { return role == QLatin1String("READ_ONLY"); }

    static Project fromJson(const QJsonObject &o);
};

struct Section {
    QString id;
    QString name;
    QString projectId;
    int sectionOrder = 0;
    bool isCollapsed = false;
    bool isArchived = false;
    bool isDeleted = false;

    static Section fromJson(const QJsonObject &o);
};

struct Item {
    QString id;
    QString content;
    QString description;
    QString projectId;
    QString sectionId;
    QString parentId;
    QString responsibleUid; ///< Assignee on shared projects.
    QString assignedByUid;
    QString addedByUid;
    QString userId;
    QStringList labels;
    Due due;
    Due deadline;     ///< Separate from due; date-only in practice.
    int priority = 1; ///< API priority: 1 = none .. 4 = urgent (p1).
    int childOrder = 0;
    int dayOrder = -1;
    int noteCount = 0;
    bool checked = false;
    bool isCollapsed = false;
    bool isDeleted = false;
    QDateTime addedAt;
    QDateTime completedAt;
    QDateTime updatedAt;

    /// Todoist's UI numbering is inverted relative to the API's.
    int uiPriority() const { return 5 - priority; }

    static Item fromJson(const QJsonObject &o);

    bool operator==(const Item &) const = default;
};

struct Label {
    QString id;
    QString name;
    QString color = QStringLiteral("charcoal");
    int itemOrder = 0;
    bool isFavorite = false;
    bool isDeleted = false;

    static Label fromJson(const QJsonObject &o);
};

struct Collaborator {
    QString id;
    QString fullName;
    QString email;
    QString imageId;
    QString timezone;

    QString avatarUrl() const;

    static Collaborator fromJson(const QJsonObject &o);
};

struct CollaboratorState {
    QString projectId;
    QString userId;
    QString role;
    QString state; ///< "active" or "invited"
    bool isDeleted = false;

    static CollaboratorState fromJson(const QJsonObject &o);
};

struct Filter {
    QString id;
    QString name;
    QString query;
    QString color = QStringLiteral("charcoal");
    int itemOrder = 0;
    bool isFavorite = false;
    bool isDeleted = false;

    static Filter fromJson(const QJsonObject &o);
};

struct Note {
    QString id;
    QString itemId;
    QString projectId;
    QString postedUid;
    QString content;
    QDateTime postedAt;
    QJsonObject fileAttachment;
    bool isDeleted = false;

    static Note fromJson(const QJsonObject &o);
};

struct Reminder {
    QString id;
    QString itemId;
    QString notifyUid;
    QString type; ///< "absolute" | "relative" | "location"
    Due due;
    int minuteOffset = 0;
    bool isDeleted = false;

    static Reminder fromJson(const QJsonObject &o);
};

struct User {
    QString id;
    QString fullName;
    QString email;
    QString inboxProjectId;
    QString teamInboxId;
    QString imageId;
    QString timezone;
    QString businessAccountId;
    bool isPremium = false;

    QString avatarUrl() const;

    static User fromJson(const QJsonObject &o);
};

} // namespace Todoist
