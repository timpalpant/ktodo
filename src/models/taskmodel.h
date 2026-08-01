#pragma once

#include "data/repository.h"

#include <QAbstractListModel>
#include <qqmlintegration.h>

/**
 * Flattened, grouped view of a task list.
 *
 * Rows are either group headers (a section, or a day in Upcoming) or tasks.
 * Sub-tasks follow their parent with an increased depth, which lets the QML
 * delegate stay a plain ListView row rather than a nested view.
 */
class TaskModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY queryChanged)
    Q_PROPERTY(QString projectId READ projectId WRITE setProjectId NOTIFY queryChanged)
    Q_PROPERTY(QString labelName READ labelName WRITE setLabelName NOTIFY queryChanged)
    Q_PROPERTY(QString filterQuery READ filterQuery WRITE setFilterQuery NOTIFY queryChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY queryChanged)
    Q_PROPERTY(bool showCompleted READ showCompleted WRITE setShowCompleted NOTIFY queryChanged)
    Q_PROPERTY(int taskCount READ taskCount NOTIFY countsChanged)
    Q_PROPERTY(bool empty READ isEmpty NOTIFY countsChanged)

public:
    enum Mode {
        ProjectTasks,
        Inbox,
        Today,
        Upcoming,
        LabelTasks,
        SavedFilter,
        Search,
        AssignedToMe,
        Completed,
    };
    Q_ENUM(Mode)

    enum Roles {
        IsHeaderRole = Qt::UserRole + 1,
        HeaderTextRole,
        HeaderIdRole,
        TaskIdRole,
        ContentRole,
        DescriptionRole,
        DescriptionHtmlRole,
        DescriptionHasLinksRole,
        PriorityRole,
        PriorityColorRole,
        DueTextRole,
        DueIsOverdueRole,
        DueIsTodayRole,
        HasDueRole,
        IsRecurringRole,
        DeadlineTextRole,
        LabelsRole,
        ProjectIdRole,
        ProjectNameRole,
        ProjectColorRole,
        SectionIdRole,
        CheckedRole,
        NoteCountRole,
        HasChildrenRole,
        DepthRole,
        AssigneeIdRole,
        AssigneeNameRole,
        AssigneeAvatarRole,
        AssigneeInitialsRole,
        IsAssignedToMeRole,
        ShowProjectRole,
        IsPendingRole,
    };

    explicit TaskModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    QString projectId() const { return m_projectId; }
    void setProjectId(const QString &id);

    QString labelName() const { return m_labelName; }
    void setLabelName(const QString &name);

    QString filterQuery() const { return m_filterQuery; }
    void setFilterQuery(const QString &query);

    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);

    bool showCompleted() const { return m_showCompleted; }
    void setShowCompleted(bool show);

    int taskCount() const { return m_taskCount; }
    bool isEmpty() const { return m_taskCount == 0; }

    Q_INVOKABLE void refresh();

    /**
     * Moves a row for the duration of a drag. This only reorders the model's
     * own rows; nothing is persisted until commitMove().
     */
    Q_INVOKABLE void moveRow(int from, int to);

    /**
     * Persists the result of a drag: re-homes the task into whichever section
     * it was dropped under and renumbers that section.
     */
    Q_INVOKABLE void commitMove(int index);

    /// Only top-level tasks reorder; headers and sub-tasks stay put.
    Q_INVOKABLE bool isDraggable(int index) const;

    /**
     * Suppresses rebuilds while a drag is in progress, so a sync landing
     * mid-gesture cannot reset the model out from under the dragged row.
     */
    Q_INVOKABLE void setReordering(bool reordering);

Q_SIGNALS:
    void queryChanged();
    void countsChanged();

private:
    struct Row {
        bool isHeader = false;
        QString headerText;
        QString headerId; ///< Section id, or ISO date for day groups.
        Todoist::Item item;
        int depth = 0;
        bool hasChildren = false;
    };

    void rebuild();
    TaskQuery buildQuery() const;
    /// Appends an item and its sub-tasks depth-first.
    void appendWithChildren(QVector<Row> &rows, const Todoist::Item &item, const QHash<QString, QVector<Todoist::Item>> &childrenByParent, int depth);

    QVector<Row> m_rows;
    Mode m_mode = Today;
    QString m_projectId;
    QString m_labelName;
    QString m_filterQuery;
    QString m_searchText;
    bool m_showCompleted = false;
    int m_taskCount = 0;
    bool m_rebuildQueued = false;
    bool m_reordering = false;
    bool m_rebuildDeferred = false;
};
