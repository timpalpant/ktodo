#pragma once

#include "types.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QVector>

/**
 * Describes what a task list should show. Models hold one of these and ask
 * the repository to resolve it; the repository owns all SQL.
 */
struct TaskQuery {
    enum Kind {
        Project,
        Inbox,
        Today,
        Upcoming,
        Label,
        SavedFilter,
        Search,
        AssignedToMe,
        Completed,
    };

    Kind kind = Today;
    QString projectId;
    QString labelName;
    QString filterQuery;
    QString searchText;
    QDate rangeStart;
    QDate rangeEnd;
    bool includeCompleted = false;
};

/// How many sub-tasks a task has, and how many of them are done.
struct SubtaskCount {
    int total = 0;
    int completed = 0;
};

class Repository : public QObject
{
    Q_OBJECT

public:
    static Repository *instance();

    // -- Applying server state -------------------------------------------
    void applySyncPayload(const QJsonObject &payload);
    /**
     * Caches tasks read from the completed-tasks endpoint.
     *
     * /sync omits completed tasks, so they arrive through a separate read and
     * are merged in rather than treated as an authoritative snapshot.
     */
    void applyCompletedItems(const QJsonArray &items);
    void setSyncToken(const QString &token);
    QString syncToken() const;

    Todoist::User currentUser() const;
    QString currentUserId() const;

    // -- Reads ------------------------------------------------------------
    QVector<Todoist::Project> projects(bool includeArchived = false) const;
    Todoist::Project project(const QString &id) const;
    QVector<Todoist::Section> sections(const QString &projectId) const;
    Todoist::Section section(const QString &id) const;
    QVector<Todoist::Item> items(const TaskQuery &query) const;
    Todoist::Item item(const QString &id) const;
    QVector<Todoist::Item> subtasks(const QString &parentId) const;
    /**
     * Sub-task tallies for every parent that has any, keyed by parent id.
     *
     * Counted in one pass because the collapse indicator needs a number for
     * each row, and completed sub-tasks are usually filtered out of the list
     * the caller is showing.
     */
    QHash<QString, SubtaskCount> subtaskCounts() const;
    QVector<Todoist::Label> labels() const;
    QVector<Todoist::Filter> filters() const;
    QVector<Todoist::Note> notes(const QString &itemId) const;
    QVector<Todoist::Collaborator> collaborators(const QString &projectId = {}) const;
    Todoist::Collaborator collaborator(const QString &userId) const;
    QVector<Todoist::Reminder> reminders() const;

    // -- Local mutations (optimistic write + queued command) --------------
    QString addItem(const QString &content,
                    const QString &projectId,
                    const QString &sectionId,
                    const QString &parentId,
                    const QString &dueString,
                    int priority,
                    const QStringList &labels,
                    const QString &description,
                    const QString &responsibleUid);
    void updateItem(const QString &id, const QJsonObject &changes);
    void setItemContent(const QString &id, const QString &content, const QString &description);
    void setItemDue(const QString &id, const QString &dueString);
    void clearItemDue(const QString &id);
    void setItemPriority(const QString &id, int apiPriority);
    void setItemLabels(const QString &id, const QStringList &labels);
    void setItemAssignee(const QString &id, const QString &userId);
    /// Hides or reveals a task's sub-tasks; Todoist syncs this across clients.
    void setItemCollapsed(const QString &id, bool collapsed);
    void moveItem(const QString &id, const QString &projectId, const QString &sectionId, const QString &parentId);
    void reorderItems(const QVector<QPair<QString, int>> &idsAndOrders);
    void completeItem(const QString &id);
    void uncompleteItem(const QString &id);
    void deleteItem(const QString &id);

    QString addProject(const QString &name, const QString &color, const QString &parentId, bool isFavorite);
    void updateProject(const QString &id, const QJsonObject &changes);
    void deleteProject(const QString &id);
    void archiveProject(const QString &id);

    QString addSection(const QString &name, const QString &projectId);
    void updateSection(const QString &id, const QJsonObject &changes);
    void deleteSection(const QString &id);

    QString addNote(const QString &itemId, const QString &content);
    void deleteNote(const QString &id);

    QString addLabel(const QString &name, const QString &color);
    void deleteLabel(const QString &id);

    QString addFilter(const QString &name, const QString &query, const QString &color);
    void updateFilter(const QString &id, const QJsonObject &changes);
    void deleteFilter(const QString &id);

    /// True while a locally created object is still awaiting its server id.
    bool isLocalId(const QString &id) const;
    /// Rewrites cached rows after the server assigns real ids.
    void applyTempIdMapping(const QHash<QString, QString> &mapping);

    bool wasNotified(const QString &key) const;
    void markNotified(const QString &key);

    void clearAll();

Q_SIGNALS:
    /// Emitted once per applied change set; models reload on this.
    void changed();
    void projectsChanged();
    void itemsChanged();
    void notesChanged(const QString &itemId);
    void userChanged();

private:
    explicit Repository(QObject *parent = nullptr);

    void upsertProject(const Todoist::Project &p);
    void upsertSection(const Todoist::Section &s);
    void upsertItem(const Todoist::Item &i);
    void upsertLabel(const Todoist::Label &l);
    void upsertFilter(const Todoist::Filter &f);
    void upsertNote(const Todoist::Note &n);
    void upsertReminder(const Todoist::Reminder &r);
    void upsertCollaborator(const Todoist::Collaborator &c);
    void upsertCollaboratorState(const Todoist::CollaboratorState &s);

    void removeRow(const QString &table, const QString &id);
    QVector<Todoist::Item> itemsFromSql(const QString &where, const QVariantList &binds) const;

    QString metaValue(const QString &key) const;
    void setMetaValue(const QString &key, const QString &value);

    /// Mints a client-side id for optimistic inserts, swapped out on sync.
    static QString newLocalId();

    mutable QString m_cachedUserId;
};
