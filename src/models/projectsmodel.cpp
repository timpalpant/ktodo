#include "projectsmodel.h"

#include "data/repository.h"
#include "util/colors.h"

#include <QSet>
#include <QTimer>

#include <algorithm>
#include <functional>

using namespace Todoist;

ProjectsModel::ProjectsModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(Repository::instance(), &Repository::projectsChanged, this, &ProjectsModel::refresh);
    connect(Repository::instance(), &Repository::itemsChanged, this, &ProjectsModel::refresh);
    connect(this, &ProjectsModel::filterChanged, this, &ProjectsModel::refresh);
    rebuild();
}

void ProjectsModel::refresh()
{
    if (m_rebuildQueued) {
        return;
    }
    m_rebuildQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_rebuildQueued = false;
        rebuild();
    });
}

void ProjectsModel::rebuild()
{
    Repository *repo = Repository::instance();
    const QVector<Project> all = repo->projects();

    // Group by parent so the tree can be emitted depth-first in child order.
    QHash<QString, QVector<Project>> byParent;
    QSet<QString> known;
    for (const Project &p : all) {
        known.insert(p.id);
    }
    for (const Project &p : all) {
        // A project whose parent is missing (archived, or not synced) is
        // treated as top-level rather than being dropped.
        const QString parent = known.contains(p.parentId) ? p.parentId : QString();
        byParent[parent].append(p);
    }

    QVector<Row> rows;

    const std::function<void(const QString &, int)> emitLevel = [&](const QString &parentId, int depth) {
        QVector<Project> children = byParent.value(parentId);
        std::stable_sort(children.begin(), children.end(), [](const Project &a, const Project &b) { return a.childOrder < b.childOrder; });

        for (const Project &p : children) {
            const bool keep = [&] {
                if (m_excludeInbox && p.isInbox) {
                    return false;
                }
                if (m_favoritesOnly && !p.isFavorite) {
                    return false;
                }
                if (m_teamOnly && !p.isTeamProject()) {
                    return false;
                }
                if (m_personalOnly && p.isTeamProject()) {
                    return false;
                }
                return true;
            }();

            if (keep) {
                Row row;
                row.project = p;
                row.depth = depth;
                row.hasChildren = byParent.contains(p.id);

                TaskQuery q;
                q.kind = TaskQuery::Project;
                q.projectId = p.id;
                row.taskCount = repo->items(q).size();

                rows.append(row);
            }

            // Descend regardless, so a filtered-out parent does not hide a
            // matching child; depth only advances when the parent was kept.
            if (depth < 4) {
                emitLevel(p.id, keep ? depth + 1 : depth);
            }
        }
    };

    emitLevel(QString(), 0);

    beginResetModel();
    m_rows = rows;
    endResetModel();
    Q_EMIT countChanged();
}

int ProjectsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant ProjectsModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &row = m_rows.at(index.row());
    const Project &p = row.project;

    switch (role) {
    case IdRole:
        return p.id;
    case NameRole:
        return p.name;
    case ColorRole:
        return Colors::fromTodoistName(p.color);
    case DepthRole:
        return row.depth;
    case IsFavoriteRole:
        return p.isFavorite;
    case IsSharedRole:
        return p.isShared;
    case IsTeamRole:
        return p.isTeamProject();
    case IsInboxRole:
        return p.isInbox;
    case IsReadOnlyRole:
        return p.isReadOnly();
    case CanAssignRole:
        return p.canAssignTasks;
    case TaskCountRole:
        return row.taskCount;
    case HasChildrenRole:
        return row.hasChildren;
    default:
        return {};
    }
}

QHash<int, QByteArray> ProjectsModel::roleNames() const
{
    return {
        {IdRole, "projectId"},
        {NameRole, "name"},
        {ColorRole, "color"},
        {DepthRole, "depth"},
        {IsFavoriteRole, "isFavorite"},
        {IsSharedRole, "isShared"},
        {IsTeamRole, "isTeam"},
        {IsInboxRole, "isInbox"},
        {IsReadOnlyRole, "isReadOnly"},
        {CanAssignRole, "canAssign"},
        {TaskCountRole, "taskCount"},
        {HasChildrenRole, "hasChildren"},
    };
}

void ProjectsModel::setFavoritesOnly(bool value)
{
    if (m_favoritesOnly != value) {
        m_favoritesOnly = value;
        Q_EMIT filterChanged();
    }
}

void ProjectsModel::setTeamOnly(bool value)
{
    if (m_teamOnly != value) {
        m_teamOnly = value;
        Q_EMIT filterChanged();
    }
}

void ProjectsModel::setPersonalOnly(bool value)
{
    if (m_personalOnly != value) {
        m_personalOnly = value;
        Q_EMIT filterChanged();
    }
}

void ProjectsModel::setExcludeInbox(bool value)
{
    if (m_excludeInbox != value) {
        m_excludeInbox = value;
        Q_EMIT filterChanged();
    }
}
