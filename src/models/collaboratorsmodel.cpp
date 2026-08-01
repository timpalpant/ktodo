#include "collaboratorsmodel.h"

#include "data/repository.h"

using namespace Todoist;

namespace {
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

CollaboratorsModel::CollaboratorsModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(Repository::instance(), &Repository::changed, this, &CollaboratorsModel::refresh);
    connect(this, &CollaboratorsModel::projectIdChanged, this, &CollaboratorsModel::refresh);
    refresh();
}

void CollaboratorsModel::refresh()
{
    beginResetModel();
    m_collaborators = Repository::instance()->collaborators(m_projectId);
    endResetModel();
    Q_EMIT countChanged();
}

void CollaboratorsModel::setProjectId(const QString &id)
{
    if (m_projectId != id) {
        m_projectId = id;
        Q_EMIT projectIdChanged();
    }
}

int CollaboratorsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_collaborators.size();
}

QVariant CollaboratorsModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_collaborators.size()) {
        return {};
    }
    const Collaborator &c = m_collaborators.at(index.row());

    switch (role) {
    case IdRole:
        return c.id;
    case NameRole:
        return c.fullName;
    case EmailRole:
        return c.email;
    case AvatarRole:
        return c.avatarUrl();
    case InitialsRole:
        return initialsFor(c.fullName);
    case IsMeRole:
        return c.id == Repository::instance()->currentUserId();
    default:
        return {};
    }
}

QHash<int, QByteArray> CollaboratorsModel::roleNames() const
{
    return {
        {IdRole, "userId"},
        {NameRole, "name"},
        {EmailRole, "email"},
        {AvatarRole, "avatar"},
        {InitialsRole, "initials"},
        {IsMeRole, "isMe"},
    };
}
