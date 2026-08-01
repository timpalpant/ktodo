#include "labelsmodel.h"

#include "data/repository.h"
#include "util/colors.h"

using namespace Todoist;

LabelsModel::LabelsModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(Repository::instance(), &Repository::changed, this, &LabelsModel::refresh);
    refresh();
}

void LabelsModel::refresh()
{
    Repository *repo = Repository::instance();

    beginResetModel();
    m_labels = repo->labels();

    m_counts.clear();
    for (const Label &l : m_labels) {
        TaskQuery q;
        q.kind = TaskQuery::Label;
        q.labelName = l.name;
        m_counts.insert(l.name, repo->items(q).size());
    }
    endResetModel();

    Q_EMIT countChanged();
}

int LabelsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_labels.size();
}

QVariant LabelsModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_labels.size()) {
        return {};
    }
    const Label &l = m_labels.at(index.row());

    switch (role) {
    case IdRole:
        return l.id;
    case NameRole:
        return l.name;
    case ColorRole:
        return Colors::fromTodoistName(l.color);
    case IsFavoriteRole:
        return l.isFavorite;
    case TaskCountRole:
        return m_counts.value(l.name);
    default:
        return {};
    }
}

QHash<int, QByteArray> LabelsModel::roleNames() const
{
    return {
        {IdRole, "labelId"},
        {NameRole, "name"},
        {ColorRole, "color"},
        {IsFavoriteRole, "isFavorite"},
        {TaskCountRole, "taskCount"},
    };
}

QStringList LabelsModel::allNames() const
{
    QStringList names;
    names.reserve(m_labels.size());
    for (const Label &l : m_labels) {
        names << l.name;
    }
    return names;
}
