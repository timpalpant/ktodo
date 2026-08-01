#include "filtersmodel.h"

#include "data/repository.h"
#include "util/colors.h"

using namespace Todoist;

FiltersModel::FiltersModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(Repository::instance(), &Repository::changed, this, &FiltersModel::refresh);
    refresh();
}

void FiltersModel::refresh()
{
    Repository *repo = Repository::instance();

    beginResetModel();
    m_filters = repo->filters();

    m_counts.clear();
    m_counts.reserve(m_filters.size());
    for (const Filter &f : m_filters) {
        TaskQuery q;
        q.kind = TaskQuery::SavedFilter;
        q.filterQuery = f.query;
        m_counts.append(repo->items(q).size());
    }
    endResetModel();

    Q_EMIT countChanged();
}

int FiltersModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_filters.size();
}

QVariant FiltersModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_filters.size()) {
        return {};
    }
    const Filter &f = m_filters.at(index.row());

    switch (role) {
    case IdRole:
        return f.id;
    case NameRole:
        return f.name;
    case QueryRole:
        return f.query;
    case ColorRole:
        return Colors::fromTodoistName(f.color);
    case IsFavoriteRole:
        return f.isFavorite;
    case TaskCountRole:
        return m_counts.value(index.row());
    default:
        return {};
    }
}

QHash<int, QByteArray> FiltersModel::roleNames() const
{
    return {
        {IdRole, "filterId"},
        {NameRole, "name"},
        {QueryRole, "query"},
        {ColorRole, "color"},
        {IsFavoriteRole, "isFavorite"},
        {TaskCountRole, "taskCount"},
    };
}
