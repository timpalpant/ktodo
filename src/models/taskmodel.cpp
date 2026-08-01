#include "taskmodel.h"

#include "util/colors.h"
#include "util/duedate.h"
#include "util/richtext.h"

#include <KLocalizedString>

#include <QSet>

#include <utility>
#include <QTimer>

using namespace Todoist;

namespace {

/// Two-letter initials used when a collaborator has no avatar image.
QString initialsFor(const QString &fullName)
{
    const QStringList parts = fullName.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return QStringLiteral("?");
    }
    QString out = parts.first().left(1);
    if (parts.size() > 1) {
        out += parts.last().left(1);
    }
    return out.toUpper();
}

} // namespace

TaskModel::TaskModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(Repository::instance(), &Repository::itemsChanged, this, &TaskModel::refresh);
    connect(Repository::instance(), &Repository::projectsChanged, this, &TaskModel::refresh);
    connect(this, &TaskModel::queryChanged, this, &TaskModel::refresh);
    rebuild();
}

void TaskModel::setReordering(bool reordering)
{
    if (m_reordering == reordering) {
        return;
    }
    m_reordering = reordering;
    if (!reordering && m_rebuildDeferred) {
        m_rebuildDeferred = false;
        refresh();
    }
}

void TaskModel::refresh()
{
    // Rebuilding under the user's finger would cancel the drag.
    if (m_reordering) {
        m_rebuildDeferred = true;
        return;
    }
    // A sync can emit several change signals in a row; collapse them into one
    // rebuild on the next event-loop turn.
    if (m_rebuildQueued) {
        return;
    }
    m_rebuildQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_rebuildQueued = false;
        rebuild();
    });
}

TaskQuery TaskModel::buildQuery() const
{
    TaskQuery q;
    q.includeCompleted = m_showCompleted;
    switch (m_mode) {
    case ProjectTasks:
        q.kind = TaskQuery::Project;
        q.projectId = m_projectId;
        break;
    case Inbox:
        q.kind = TaskQuery::Inbox;
        q.projectId = m_projectId.isEmpty() ? Repository::instance()->currentUser().inboxProjectId : m_projectId;
        break;
    case Today:
        q.kind = TaskQuery::Today;
        break;
    case Upcoming:
        q.kind = TaskQuery::Upcoming;
        q.rangeStart = QDate::currentDate();
        q.rangeEnd = QDate::currentDate().addDays(30);
        break;
    case LabelTasks:
        q.kind = TaskQuery::Label;
        q.labelName = m_labelName;
        break;
    case SavedFilter:
        q.kind = TaskQuery::SavedFilter;
        q.filterQuery = m_filterQuery;
        break;
    case Search:
        q.kind = TaskQuery::Search;
        q.searchText = m_searchText;
        break;
    case AssignedToMe:
        q.kind = TaskQuery::AssignedToMe;
        break;
    case Completed:
        q.kind = TaskQuery::Completed;
        break;
    }
    return q;
}

void TaskModel::appendWithChildren(QVector<Row> &rows, const Item &item, const QHash<QString, QVector<Item>> &childrenByParent, int depth)
{
    Row row;
    row.item = item;
    row.depth = depth;
    row.hasChildren = childrenByParent.contains(item.id);
    rows.append(row);

    // Guard against a pathological chain; real data nests only a few levels.
    if (depth >= 4) {
        return;
    }
    const auto children = childrenByParent.value(item.id);
    for (const Item &child : children) {
        if (!child.checked || m_showCompleted) {
            appendWithChildren(rows, child, childrenByParent, depth + 1);
        }
    }
}

void TaskModel::rebuild()
{
    Repository *repo = Repository::instance();
    const QVector<Item> items = repo->items(buildQuery());

    QVector<Row> rows;
    int taskCount = 0;

    // Index children so sub-tasks can be attached to their parent in one pass.
    QHash<QString, QVector<Item>> childrenByParent;
    QSet<QString> presentIds;
    for (const Item &i : items) {
        presentIds.insert(i.id);
    }
    for (const Item &i : items) {
        if (!i.parentId.isEmpty()) {
            childrenByParent[i.parentId].append(i);
        }
    }

    if (m_mode == ProjectTasks || m_mode == Inbox) {
        const QString projectId = m_mode == Inbox ? (m_projectId.isEmpty() ? repo->currentUser().inboxProjectId : m_projectId) : m_projectId;

        // Ungrouped tasks first, then one group per section, matching Todoist.
        QVector<Item> loose;
        QHash<QString, QVector<Item>> bySection;
        for (const Item &i : items) {
            if (!i.parentId.isEmpty() && presentIds.contains(i.parentId)) {
                continue; // Rendered under its parent instead.
            }
            if (i.sectionId.isEmpty()) {
                loose.append(i);
            } else {
                bySection[i.sectionId].append(i);
            }
        }

        for (const Item &i : loose) {
            appendWithChildren(rows, i, childrenByParent, 0);
        }

        const QVector<Section> sections = repo->sections(projectId);
        for (const Section &s : sections) {
            const QVector<Item> sectionItems = bySection.value(s.id);
            if (sectionItems.isEmpty() && s.name.isEmpty()) {
                continue;
            }
            Row header;
            header.isHeader = true;
            header.headerText = s.name;
            header.headerId = s.id;
            rows.append(header);
            for (const Item &i : sectionItems) {
                appendWithChildren(rows, i, childrenByParent, 0);
            }
        }
    } else if (m_mode == Upcoming) {
        // One group per calendar day.
        QString currentDay;
        for (const Item &i : items) {
            const QDate date = i.due.localDate();
            const QString key = date.toString(Qt::ISODate);
            if (key != currentDay) {
                currentDay = key;
                Row header;
                header.isHeader = true;
                header.headerText = DueDate::dayHeading(date);
                header.headerId = key;
                rows.append(header);
            }
            Row row;
            row.item = i;
            row.hasChildren = childrenByParent.contains(i.id);
            rows.append(row);
        }
    } else {
        for (const Item &i : items) {
            if (!i.parentId.isEmpty() && presentIds.contains(i.parentId) && (m_mode == ProjectTasks || m_mode == Inbox)) {
                continue;
            }
            Row row;
            row.item = i;
            row.hasChildren = childrenByParent.contains(i.id);
            rows.append(row);
        }
    }

    for (const Row &r : rows) {
        if (!r.isHeader) {
            ++taskCount;
        }
    }

    beginResetModel();
    m_rows = rows;
    endResetModel();

    if (m_taskCount != taskCount) {
        m_taskCount = taskCount;
        Q_EMIT countsChanged();
    }
}

bool TaskModel::isDraggable(int index) const
{
    if (index < 0 || index >= m_rows.size()) {
        return false;
    }
    // Reordering is only meaningful where the order is the project's own;
    // Today and filters are sorted by date, so a drag would not survive.
    if (m_mode != ProjectTasks && m_mode != Inbox) {
        return false;
    }
    const Row &row = m_rows.at(index);
    // Dragging a sub-task out of its parent would silently re-parent it.
    return !row.isHeader && row.depth == 0 && !Repository::instance()->isLocalId(row.item.id);
}

void TaskModel::moveRow(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= m_rows.size() || to >= m_rows.size()) {
        return;
    }
    if (m_rows.at(from).isHeader) {
        return;
    }

    // QAbstractItemModel expects the destination expressed in pre-move
    // coordinates, which is one further along when moving downwards.
    if (!beginMoveRows({}, from, from, {}, to > from ? to + 1 : to)) {
        return;
    }
    m_rows.move(from, to);
    endMoveRows();
}

void TaskModel::commitMove(int index)
{
    if (index < 0 || index >= m_rows.size()) {
        return;
    }
    const Row moved = m_rows.at(index);
    if (moved.isHeader) {
        return;
    }

    // The section a task now belongs to is the nearest header above it.
    QString targetSection;
    for (int i = index - 1; i >= 0; --i) {
        if (m_rows.at(i).isHeader) {
            targetSection = m_rows.at(i).headerId;
            break;
        }
    }

    // Renumber every top-level task of that section in its new visual order.
    QVector<QPair<QString, int>> orders;
    QString walkSection;
    int order = 0;
    for (const Row &row : std::as_const(m_rows)) {
        if (row.isHeader) {
            walkSection = row.headerId;
            continue;
        }
        if (row.depth != 0 || walkSection != targetSection) {
            continue;
        }
        orders.append({row.item.id, order++});
    }

    Repository *repo = Repository::instance();
    if (moved.item.sectionId != targetSection) {
        repo->moveItem(moved.item.id, moved.item.projectId, targetSection, {});
    }
    repo->reorderItems(orders);
}

int TaskModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant TaskModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &row = m_rows.at(index.row());
    const Item &item = row.item;
    Repository *repo = Repository::instance();

    switch (role) {
    case IsHeaderRole:
        return row.isHeader;
    case HeaderTextRole:
        return row.headerText;
    case HeaderIdRole:
        return row.headerId;
    case TaskIdRole:
        return item.id;
    case ContentRole:
        return item.content;
    case DescriptionRole:
        return item.description;
    case DescriptionHtmlRole:
        return RichText::toHtml(item.description);
    case DescriptionHasLinksRole:
        return RichText::hasLinks(item.description);
    case PriorityRole:
        return item.priority;
    case PriorityColorRole:
        return Colors::priorityColor(item.priority);
    case DueTextRole:
        return DueDate::formatShort(item.due);
    case DueIsOverdueRole:
        return DueDate::isOverdue(item.due);
    case DueIsTodayRole:
        return DueDate::isToday(item.due);
    case HasDueRole:
        return item.due.isValid() || !item.due.string.isEmpty();
    case IsRecurringRole:
        return item.due.isRecurring;
    case DeadlineTextRole:
        return item.deadline.isValid() ? DueDate::formatShort(item.deadline) : QString();
    case LabelsRole:
        return item.labels;
    case ProjectIdRole:
        return item.projectId;
    case ProjectNameRole:
        return repo->project(item.projectId).name;
    case ProjectColorRole:
        return Colors::fromTodoistName(repo->project(item.projectId).color);
    case SectionIdRole:
        return item.sectionId;
    case CheckedRole:
        return item.checked;
    case NoteCountRole:
        return item.noteCount;
    case HasChildrenRole:
        return row.hasChildren;
    case DepthRole:
        return row.depth;
    case AssigneeIdRole:
        return item.responsibleUid;
    case AssigneeNameRole:
        return item.responsibleUid.isEmpty() ? QString() : repo->collaborator(item.responsibleUid).fullName;
    case AssigneeAvatarRole:
        return item.responsibleUid.isEmpty() ? QString() : repo->collaborator(item.responsibleUid).avatarUrl();
    case AssigneeInitialsRole:
        return item.responsibleUid.isEmpty() ? QString() : initialsFor(repo->collaborator(item.responsibleUid).fullName);
    case IsAssignedToMeRole:
        return !item.responsibleUid.isEmpty() && item.responsibleUid == repo->currentUserId();
    case ShowProjectRole:
        // The project badge is redundant inside a project view.
        return m_mode != ProjectTasks && m_mode != Inbox;
    case IsPendingRole:
        return repo->isLocalId(item.id);
    default:
        return {};
    }
}

QHash<int, QByteArray> TaskModel::roleNames() const
{
    return {
        {IsHeaderRole, "isHeader"},
        {HeaderTextRole, "headerText"},
        {HeaderIdRole, "headerId"},
        {TaskIdRole, "taskId"},
        {ContentRole, "content"},
        {DescriptionRole, "description"},
        {DescriptionHtmlRole, "descriptionHtml"},
        {DescriptionHasLinksRole, "descriptionHasLinks"},
        {PriorityRole, "priority"},
        {PriorityColorRole, "priorityColor"},
        {DueTextRole, "dueText"},
        {DueIsOverdueRole, "dueIsOverdue"},
        {DueIsTodayRole, "dueIsToday"},
        {HasDueRole, "hasDue"},
        {IsRecurringRole, "isRecurring"},
        {DeadlineTextRole, "deadlineText"},
        {LabelsRole, "labels"},
        {ProjectIdRole, "projectId"},
        {ProjectNameRole, "projectName"},
        {ProjectColorRole, "projectColor"},
        {SectionIdRole, "sectionId"},
        {CheckedRole, "isChecked"},
        {NoteCountRole, "noteCount"},
        {HasChildrenRole, "hasChildren"},
        {DepthRole, "depth"},
        {AssigneeIdRole, "assigneeId"},
        {AssigneeNameRole, "assigneeName"},
        {AssigneeAvatarRole, "assigneeAvatar"},
        {AssigneeInitialsRole, "assigneeInitials"},
        {IsAssignedToMeRole, "isAssignedToMe"},
        {ShowProjectRole, "showProject"},
        {IsPendingRole, "isPending"},
    };
}

void TaskModel::setMode(Mode mode)
{
    if (m_mode != mode) {
        m_mode = mode;
        Q_EMIT queryChanged();
    }
}

void TaskModel::setProjectId(const QString &id)
{
    if (m_projectId != id) {
        m_projectId = id;
        Q_EMIT queryChanged();
    }
}

void TaskModel::setLabelName(const QString &name)
{
    if (m_labelName != name) {
        m_labelName = name;
        Q_EMIT queryChanged();
    }
}

void TaskModel::setFilterQuery(const QString &query)
{
    if (m_filterQuery != query) {
        m_filterQuery = query;
        Q_EMIT queryChanged();
    }
}

void TaskModel::setSearchText(const QString &text)
{
    if (m_searchText != text) {
        m_searchText = text;
        Q_EMIT queryChanged();
    }
}

void TaskModel::setShowCompleted(bool show)
{
    if (m_showCompleted != show) {
        m_showCompleted = show;
        Q_EMIT queryChanged();
    }
}
