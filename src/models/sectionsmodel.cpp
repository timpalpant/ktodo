#include "sectionsmodel.h"

#include "data/repository.h"

using namespace Todoist;

SectionsModel::SectionsModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(Repository::instance(), &Repository::changed, this, &SectionsModel::refresh);
    connect(this, &SectionsModel::projectIdChanged, this, &SectionsModel::refresh);
}

void SectionsModel::setProjectId(const QString &id)
{
    if (m_projectId != id) {
        m_projectId = id;
        Q_EMIT projectIdChanged();
    }
}

void SectionsModel::refresh()
{
    beginResetModel();
    m_sections = m_projectId.isEmpty() ? QVector<Section>() : Repository::instance()->sections(m_projectId);
    endResetModel();
    Q_EMIT countChanged();
}

int SectionsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_sections.size();
}

QVariant SectionsModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_sections.size()) {
        return {};
    }
    const Section &s = m_sections.at(index.row());

    switch (role) {
    case IdRole:
        return s.id;
    case NameRole:
        return s.name;
    case TaskCountRole: {
        TaskQuery q;
        q.kind = TaskQuery::Project;
        q.projectId = s.projectId;
        int count = 0;
        for (const Item &i : Repository::instance()->items(q)) {
            if (i.sectionId == s.id) {
                ++count;
            }
        }
        return count;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> SectionsModel::roleNames() const
{
    return {
        {IdRole, "sectionId"},
        {NameRole, "name"},
        {TaskCountRole, "taskCount"},
    };
}
