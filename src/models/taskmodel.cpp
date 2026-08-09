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

int TaskModel::rowForDate(const QDate &date) const
{
    if (m_mode != Upcoming || !date.isValid()) {
        return -1;
    }

    // Empty days are represented by the next agenda group, which makes a
    // calendar tap useful without filling the list with placeholder rows.
    for (int row = 0; row < m_rows.size(); ++row) {
        const Row &candidate = m_rows.at(row);
        if (!candidate.isHeader) {
            continue;
        }
        const QDate candidateDate = QDate::fromString(candidate.headerId, Qt::ISODate);
        if (candidateDate >= date) {
            return row;
        }
    }
    return -1;
}

int TaskModel::taskCountForDate(const QDate &date) const
{
    if (m_mode != Upcoming || !date.isValid()) {
        return 0;
    }
    return m_taskCountsByDate.value(date);
}

QDate TaskModel::dateForRow(int row) const
{
    if (m_mode != Upcoming || row < 0 || row >= m_rows.size()) {
        return {};
    }
    for (int i = row; i >= 0; --i) {
        if (m_rows.at(i).isHeader) {
            return QDate::fromString(m_rows.at(i).headerId, Qt::ISODate);
        }
    }
    return {};
}

void TaskModel::toggleOverdueCollapsed()
{
    if (m_mode != Upcoming) {
        return;
    }
    m_overdueCollapsed = !m_overdueCollapsed;
    rebuild();
}

int TaskModel::heightOf(const QString &id, Build &build)
{
    const auto cached = build.heights.constFind(id);
    if (cached != build.heights.constEnd()) {
        return *cached;
    }
    // Written before recursing so a cyclic parent chain terminates instead of
    // running the stack out.
    build.heights.insert(id, 0);

    int height = 0;
    const auto children = build.childrenByParent.value(id);
    for (const Item &child : children) {
        height = qMax(height, 1 + heightOf(child.id, build));
    }
    build.heights.insert(id, height);
    return height;
}

TaskModel::Row TaskModel::makeRow(const Item &item, Build &build, int depth, bool nested)
{
    Row row;
    row.item = item;
    row.depth = depth;
    row.hasChildren = build.childrenByParent.contains(item.id);
    // Only a nested list has anything to fold away: the flat views list
    // sub-tasks as rows of their own.
    row.canCollapse = nested && row.hasChildren;
    row.subtasks = build.subtaskCounts.value(item.id);
    row.subtreeHeight = heightOf(item.id, build);
    return row;
}

void TaskModel::appendWithChildren(QVector<Row> &rows, const Item &item, Build &build, int depth)
{
    rows.append(makeRow(item, build, depth, true));

    // Guard against a pathological chain; real data nests only a few levels.
    if (depth >= 4 || item.isCollapsed) {
        return;
    }
    const auto children = build.childrenByParent.value(item.id);
    for (const Item &child : children) {
        if (!child.checked || m_showCompleted) {
            appendWithChildren(rows, child, build, depth + 1);
        }
    }
}

void TaskModel::rebuild()
{
    Repository *repo = Repository::instance();
    const QVector<Item> items = repo->items(buildQuery());

    QVector<Row> rows;
    QHash<QDate, int> taskCountsByDate;
    int taskCount = 0;

    if (m_mode == Upcoming) {
        for (const Item &i : items) {
            ++taskCountsByDate[i.due.localDate()];
        }
    }

    // Index children so sub-tasks can be attached to their parent in one pass.
    Build build;
    build.subtaskCounts = repo->subtaskCounts();
    QSet<QString> presentIds;
    for (const Item &i : items) {
        presentIds.insert(i.id);
    }
    for (const Item &i : items) {
        if (!i.parentId.isEmpty()) {
            build.childrenByParent[i.parentId].append(i);
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
            appendWithChildren(rows, i, build, 0);
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
                appendWithChildren(rows, i, build, 0);
            }
        }
    } else if (m_mode == Upcoming) {
        QVector<Item> overdue;
        for (const Item &i : items) {
            if (!i.checked && DueDate::isOverdue(i.due)) {
                overdue.append(i);
            }
        }

        if (!overdue.isEmpty()) {
            Row header;
            header.isHeader = true;
            header.headerText = i18n("Overdue");
            header.headerId = QStringLiteral("overdue");
            header.canCollapse = true;
            header.headerCollapsed = m_overdueCollapsed;
            rows.append(header);

            if (!m_overdueCollapsed) {
                for (const Item &i : overdue) {
                    rows.append(makeRow(i, build, 0, false));
                }
            }
        }

        // One group per calendar day.
        QString currentDay;
        for (const Item &i : items) {
            if (!i.checked && DueDate::isOverdue(i.due)) {
                continue;
            }
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
            rows.append(makeRow(i, build, 0, false));
        }
    } else {
        for (const Item &i : items) {
            if (!i.parentId.isEmpty() && presentIds.contains(i.parentId) && (m_mode == ProjectTasks || m_mode == Inbox)) {
                continue;
            }
            rows.append(makeRow(i, build, 0, false));
        }
    }

    bool collapsible = false;
    for (const Row &r : rows) {
        if (!r.isHeader) {
            ++taskCount;
            collapsible = collapsible || r.canCollapse;
        }
    }
    // A collapsed Overdue section still contains tasks and must not trigger
    // the empty-list placeholder over its header.
    if (m_mode == Upcoming) {
        taskCount = items.size();
    }

    beginResetModel();
    m_rows = rows;
    m_taskCountsByDate = taskCountsByDate;
    endResetModel();

    if (m_taskCount != taskCount) {
        m_taskCount = taskCount;
        Q_EMIT countsChanged();
    }
    if (m_hasCollapsibleRows != collapsible) {
        m_hasCollapsibleRows = collapsible;
        Q_EMIT hasCollapsibleRowsChanged();
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
    return !row.isHeader && !Repository::instance()->isLocalId(row.item.id);
}

TaskModel::Drop TaskModel::describeDrop(int fromIndex, int insertIndex, int targetIndex, bool asSubtask) const
{
    Drop drop;
    if ((m_mode != ProjectTasks && m_mode != Inbox) || fromIndex < 0 || fromIndex >= m_rows.size()) {
        return drop;
    }

    const Row &sourceRow = m_rows.at(fromIndex);
    const Item &source = sourceRow.item;
    if (sourceRow.isHeader || source.id.isEmpty() || Repository::instance()->isLocalId(source.id)) {
        return drop;
    }

    auto setScopeFrom = [&drop](const Item &item) {
        drop.projectId = item.projectId;
        drop.sectionId = item.sectionId;
        drop.parentId = item.parentId;
    };

    const int sourceSubtreeHeight = subtreeHeight(fromIndex);

    const auto findNextSibling = [this, fromIndex, &drop](int start) {
        for (int i = start; i < m_rows.size(); ++i) {
            const Row &candidateRow = m_rows.at(i);
            const Item &candidate = candidateRow.item;
            if (candidateRow.isHeader || i == fromIndex || candidate.id.isEmpty() || Repository::instance()->isLocalId(candidate.id)
                || candidate.projectId != drop.projectId || candidate.parentId != drop.parentId
                || (drop.parentId.isEmpty() && candidate.sectionId != drop.sectionId)) {
                continue;
            }
            return candidate.id;
        }
        return QString{};
    };

    if (asSubtask) {
        if (targetIndex < 0 || targetIndex >= m_rows.size()) {
            return drop;
        }
        const Row &targetRow = m_rows.at(targetIndex);
        const Item &target = targetRow.item;
        if (targetRow.isHeader || target.id.isEmpty() || Repository::instance()->isLocalId(target.id) || target.id == source.id
            || target.projectId != source.projectId || targetRow.depth + 1 + sourceSubtreeHeight > 4 || isDescendantOf(target.id, source.id)) {
            return drop;
        }

        drop.projectId = target.projectId;
        drop.sectionId = target.sectionId;
        drop.parentId = target.id;
        drop.valid = true;
        return drop;
    }

    insertIndex = qBound(0, insertIndex, m_rows.size());

    // A section-header edge always creates a root task. Looking only at the
    // preceding flattened row would otherwise accidentally use the last
    // subtask's parent as the destination scope.
    if (targetIndex >= 0 && targetIndex < m_rows.size() && m_rows.at(targetIndex).isHeader) {
        const Row &targetRow = m_rows.at(targetIndex);
        drop.projectId = source.projectId;
        drop.parentId.clear();
        if (insertIndex <= targetIndex) {
            // The upper half of a header belongs to the end of the preceding
            // section. Before the first header is the unsectioned root list.
            for (int i = targetIndex - 1; i >= 0; --i) {
                if (m_rows.at(i).isHeader) {
                    drop.sectionId = m_rows.at(i).headerId;
                    break;
                }
            }
        } else {
            // The lower half is the beginning of this header's section.
            drop.sectionId = targetRow.headerId;
            drop.beforeId = findNextSibling(targetIndex + 1);
        }
        drop.valid = sourceSubtreeHeight <= 4;
        return drop;
    }

    // Edge zones on an actual task use that task's sibling scope rather than
    // whichever flattened child happens to follow it. This is what makes the
    // lower edge of a parent mean "after this whole task" instead of the
    // surprising "inside this task".
    if (targetIndex >= 0 && targetIndex < m_rows.size() && targetIndex != fromIndex) {
        const Row &targetRow = m_rows.at(targetIndex);
        const Item &target = targetRow.item;
        if (!targetRow.isHeader && !target.id.isEmpty() && !Repository::instance()->isLocalId(target.id) && target.projectId == source.projectId
            && targetRow.depth + sourceSubtreeHeight <= 4 && !isDescendantOf(target.id, source.id)) {
            setScopeFrom(target);
            if (insertIndex <= targetIndex) {
                drop.beforeId = target.id;
            } else {
                // Find the next direct sibling, skipping any of the target's
                // descendants in the flattened ListView.
                for (int i = targetIndex + 1; i < m_rows.size(); ++i) {
                    const Row &candidateRow = m_rows.at(i);
                    const Item &candidate = candidateRow.item;
                    if (candidateRow.isHeader || candidate.id == source.id || Repository::instance()->isLocalId(candidate.id)
                        || candidate.projectId != target.projectId || candidate.parentId != target.parentId
                        || (target.parentId.isEmpty() && candidate.sectionId != target.sectionId)) {
                        continue;
                    }
                    drop.beforeId = candidate.id;
                    break;
                }
            }
            drop.valid = true;
            return drop;
        }
        // A concrete task target that cannot accept the source must not fall
        // through to a guessed parent/section. That could turn a visibly
        // invalid drop into an unexpected re-parenting operation.
        return Drop{};
    }

    // Outside a realised row (above the first or below the last) keep the
    // task in its current sibling scope. Choosing the nearest flattened row
    // here is unsafe: at the bottom of a project that row can be somebody
    // else's child, which silently turns a root task into a subtask.
    setScopeFrom(source);
    drop.beforeId = findNextSibling(insertIndex);
    drop.valid = true;
    return drop;
}

bool TaskModel::isDescendantOf(const QString &candidateId, const QString &ancestorId) const
{
    if (candidateId.isEmpty() || ancestorId.isEmpty()) {
        return false;
    }

    QHash<QString, QString> parents;
    for (const Row &row : m_rows) {
        if (!row.isHeader) {
            parents.insert(row.item.id, row.item.parentId);
        }
    }

    QString current = candidateId;
    QSet<QString> seen;
    while (!current.isEmpty() && !seen.contains(current)) {
        if (current == ancestorId) {
            return true;
        }
        seen.insert(current);
        current = parents.value(current);
    }
    return false;
}

int TaskModel::subtreeHeight(int rowIndex) const
{
    if (rowIndex < 0 || rowIndex >= m_rows.size() || m_rows.at(rowIndex).isHeader) {
        return 0;
    }
    // Measured at rebuild time from the queried items rather than from the
    // rows on screen: a collapsed task still carries its descendants with it,
    // and they count against Todoist's nesting limit at the destination.
    return m_rows.at(rowIndex).subtreeHeight;
}

void TaskModel::setCollapsed(int index, bool collapsed)
{
    if (index < 0 || index >= m_rows.size()) {
        return;
    }
    const Row &row = m_rows.at(index);
    if (row.isHeader || !row.canCollapse || row.item.isCollapsed == collapsed) {
        return;
    }
    Repository::instance()->setItemCollapsed(row.item.id, collapsed);
}

void TaskModel::toggleCollapsed(int index)
{
    if (index < 0 || index >= m_rows.size()) {
        return;
    }
    setCollapsed(index, !m_rows.at(index).item.isCollapsed);
}

QVector<QString> TaskModel::siblingIds(const Drop &drop, const QString &excludeId) const
{
    QVector<QString> ids;

    // Dropping into a collapsed task: its children are real but off screen, so
    // renumbering from the visible rows alone would hand out duplicate orders.
    if (!drop.parentId.isEmpty()) {
        const int parentRow = [this, &drop] {
            for (int i = 0; i < m_rows.size(); ++i) {
                if (!m_rows.at(i).isHeader && m_rows.at(i).item.id == drop.parentId) {
                    return i;
                }
            }
            return -1;
        }();
        if (parentRow >= 0 && m_rows.at(parentRow).item.isCollapsed) {
            const QVector<Todoist::Item> children = Repository::instance()->subtasks(drop.parentId);
            for (const Item &child : children) {
                if (child.id != excludeId && !Repository::instance()->isLocalId(child.id)) {
                    ids.append(child.id);
                }
            }
            return ids;
        }
    }

    for (const Row &row : m_rows) {
        if (row.isHeader || row.item.id == excludeId || Repository::instance()->isLocalId(row.item.id) || row.item.projectId != drop.projectId
            || row.item.parentId != drop.parentId) {
            continue;
        }
        // Root tasks are additionally scoped by their section. A child task
        // inherits its parent's section, so parent_id is sufficient there.
        if (drop.parentId.isEmpty() && row.item.sectionId != drop.sectionId) {
            continue;
        }
        ids.append(row.item.id);
    }
    return ids;
}

bool TaskModel::canDrop(int fromIndex, int insertIndex, int targetIndex, bool asSubtask) const
{
    return describeDrop(fromIndex, insertIndex, targetIndex, asSubtask).valid;
}

void TaskModel::commitDrop(int fromIndex, int insertIndex, int targetIndex, bool asSubtask)
{
    const Drop drop = describeDrop(fromIndex, insertIndex, targetIndex, asSubtask);
    if (!drop.valid || fromIndex < 0 || fromIndex >= m_rows.size()) {
        return;
    }

    const Item source = m_rows.at(fromIndex).item;
    QVector<QString> order = siblingIds(drop, source.id);
    int position = drop.beforeId.isEmpty() ? order.size() : order.indexOf(drop.beforeId);
    if (position < 0) {
        position = order.size();
    }
    order.insert(position, source.id);

    Repository *repo = Repository::instance();
    if (source.projectId != drop.projectId || source.sectionId != drop.sectionId || source.parentId != drop.parentId) {
        repo->moveItem(source.id, drop.projectId, drop.sectionId, drop.parentId);
    }

    QVector<QPair<QString, int>> orders;
    orders.reserve(order.size());
    for (qsizetype i = 0; i < order.size(); ++i) {
        orders.append({order.at(i), static_cast<int>(i)});
    }
    repo->reorderItems(orders);

    // Dropping a task into a collapsed parent would otherwise look like the
    // task vanished, so reveal where it landed.
    if (!drop.parentId.isEmpty() && repo->item(drop.parentId).isCollapsed) {
        repo->setItemCollapsed(drop.parentId, false);
    }
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
    case CanCollapseRole:
        return row.canCollapse;
    case IsCollapsedRole:
        return row.isHeader ? row.headerCollapsed : (row.canCollapse && item.isCollapsed);
    case SubtaskCountRole:
        return row.subtasks.total;
    case SubtaskCompletedCountRole:
        return row.subtasks.completed;
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
        {CanCollapseRole, "canCollapse"},
        {IsCollapsedRole, "isCollapsed"},
        {SubtaskCountRole, "subtaskCount"},
        {SubtaskCompletedCountRole, "subtaskCompletedCount"},
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
