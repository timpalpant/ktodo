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
    /// True when some row nests, so the view can reserve a disclosure gutter.
    Q_PROPERTY(bool hasCollapsibleRows READ hasCollapsibleRows NOTIFY hasCollapsibleRowsChanged)

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
        CanCollapseRole,
        IsCollapsedRole,
        SubtaskCountRole,
        SubtaskCompletedCountRole,
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
    bool hasCollapsibleRows() const { return m_hasCollapsibleRows; }

    Q_INVOKABLE void refresh();

    /// Row of the requested day in Upcoming, or the next populated day.
    Q_INVOKABLE int rowForDate(const QDate &date) const;
    /// Number of tasks due on @p date in the current Upcoming result.
    Q_INVOKABLE int taskCountForDate(const QDate &date) const;
    /// Calendar day containing @p row, walking back over its day header.
    Q_INVOKABLE QDate dateForRow(int row) const;
    /// Hides or reveals Upcoming's special overdue group locally.
    Q_INVOKABLE void toggleOverdueCollapsed();

    /**
     * Hides or reveals the sub-tasks of the task at @p index.
     *
     * The state lives on the task itself rather than in the view, so it
     * survives a rebuild and follows the account to Todoist's other clients.
     */
    Q_INVOKABLE void setCollapsed(int index, bool collapsed);
    Q_INVOKABLE void toggleCollapsed(int index);

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

    /// Every persisted task in a project list can start a hierarchy drag.
    Q_INVOKABLE bool isDraggable(int index) const;

    /// Validates the current mouse drop proposal without mutating the model.
    Q_INVOKABLE bool canDrop(int fromIndex, int insertIndex, int targetIndex, bool asSubtask) const;

    /**
     * Applies a hierarchy drop. A middle-zone drop makes @p fromIndex the last
     * child of @p targetIndex; an edge-zone drop inserts it as a sibling at
     * @p insertIndex, which naturally lets a child be promoted back out.
     */
    Q_INVOKABLE void commitDrop(int fromIndex, int insertIndex, int targetIndex, bool asSubtask);

    /**
     * Suppresses rebuilds while a drag is in progress, so a sync landing
     * mid-gesture cannot reset the model out from under the dragged row.
     */
    Q_INVOKABLE void setReordering(bool reordering);

Q_SIGNALS:
    void queryChanged();
    void countsChanged();
    void hasCollapsibleRowsChanged();

private:
    struct Row {
        bool isHeader = false;
        QString headerText;
        QString headerId; ///< Section id, or ISO date for day groups.
        Todoist::Item item;
        int depth = 0;
        bool hasChildren = false;
        /// Nested here, so a disclosure control can hide something.
        bool canCollapse = false;
        SubtaskCount subtasks;
        /// Levels of descendants, counted whether or not they are shown.
        int subtreeHeight = 0;
        /// Collapse state for synthetic headers such as Upcoming's Overdue.
        bool headerCollapsed = false;

        bool operator==(const Row &) const = default;
    };

    struct Drop {
        bool valid = false;
        QString projectId;
        QString sectionId;
        QString parentId;
        QString beforeId;
    };

    /// Everything a rebuild derives from the queried items once, up front.
    struct Build {
        QHash<QString, QVector<Todoist::Item>> childrenByParent;
        QHash<QString, SubtaskCount> subtaskCounts;
        QHash<QString, int> heights; ///< Memoised subtree heights by item id.
    };

    void rebuild();
    /**
     * Replaces m_rows with @p newRows, preferring surgical insert/remove/
     * update signals over a full reset so the view doesn't tear down and
     * recreate delegates it doesn't need to. Falls back to a reset when the
     * rows that persist between the two states have been reordered, or when
     * the two states share nothing (a mode/query change, or the first build).
     */
    void applyRows(QVector<Row> newRows);
    static QString keyOf(const Row &row);
    TaskQuery buildQuery() const;
    /// Builds a task row, filling in what it takes to draw the collapse state.
    Row makeRow(const Todoist::Item &item, Build &build, int depth, bool nested);
    /// Appends an item and, unless it is collapsed, its sub-tasks depth-first.
    void appendWithChildren(QVector<Row> &rows, const Todoist::Item &item, Build &build, int depth);
    /// Levels below @p id in the queried set, regardless of what is shown.
    static int heightOf(const QString &id, Build &build);
    Drop describeDrop(int fromIndex, int insertIndex, int targetIndex, bool asSubtask) const;
    bool isDescendantOf(const QString &candidateId, const QString &ancestorId) const;
    /// Depth of the deepest descendant of @p rowIndex, collapsed or not.
    int subtreeHeight(int rowIndex) const;
    QVector<QString> siblingIds(const Drop &drop, const QString &excludeId = {}) const;

    QVector<Row> m_rows;
    QHash<QDate, int> m_taskCountsByDate;
    Mode m_mode = Today;
    QString m_projectId;
    QString m_labelName;
    QString m_filterQuery;
    QString m_searchText;
    bool m_showCompleted = false;
    int m_taskCount = 0;
    bool m_hasCollapsibleRows = false;
    bool m_rebuildQueued = false;
    bool m_reordering = false;
    bool m_rebuildDeferred = false;
    bool m_overdueCollapsed = false;
};
