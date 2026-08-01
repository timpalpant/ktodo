#include "notesmodel.h"

#include "data/repository.h"

#include <KLocalizedString>

#include <QLocale>

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

/// Relative time for recent comments, absolute for older ones.
QString postedText(const QDateTime &when)
{
    if (!when.isValid()) {
        return {};
    }
    const qint64 secs = when.secsTo(QDateTime::currentDateTime());
    if (secs < 60) {
        return i18n("just now");
    }
    if (secs < 3600) {
        return i18np("%1 minute ago", "%1 minutes ago", secs / 60);
    }
    if (secs < 86400) {
        return i18np("%1 hour ago", "%1 hours ago", secs / 3600);
    }
    if (secs < 7 * 86400) {
        return i18np("%1 day ago", "%1 days ago", secs / 86400);
    }
    return QLocale().toString(when, QLocale::ShortFormat);
}

} // namespace

NotesModel::NotesModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(Repository::instance(), &Repository::notesChanged, this, [this](const QString &itemId) {
        if (itemId == m_taskId) {
            refresh();
        }
    });
    connect(Repository::instance(), &Repository::changed, this, &NotesModel::refresh);
    connect(this, &NotesModel::taskIdChanged, this, &NotesModel::refresh);
}

void NotesModel::setTaskId(const QString &id)
{
    if (m_taskId != id) {
        m_taskId = id;
        Q_EMIT taskIdChanged();
    }
}

void NotesModel::refresh()
{
    beginResetModel();
    m_notes = m_taskId.isEmpty() ? QVector<Note>() : Repository::instance()->notes(m_taskId);
    endResetModel();
    Q_EMIT countChanged();
}

int NotesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_notes.size();
}

QVariant NotesModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_notes.size()) {
        return {};
    }
    const Note &n = m_notes.at(index.row());
    Repository *repo = Repository::instance();
    const Collaborator author = repo->collaborator(n.postedUid);

    switch (role) {
    case IdRole:
        return n.id;
    case ContentRole:
        return n.content;
    case AuthorNameRole:
        return author.fullName.isEmpty() ? i18n("Unknown") : author.fullName;
    case AuthorAvatarRole:
        return author.avatarUrl();
    case AuthorInitialsRole:
        return initialsFor(author.fullName);
    case PostedAtRole:
        return n.postedAt;
    case PostedTextRole:
        return postedText(n.postedAt);
    case IsMineRole:
        return n.postedUid == repo->currentUserId();
    case AttachmentNameRole:
        return n.fileAttachment.value(QStringLiteral("file_name")).toString();
    case AttachmentUrlRole:
        return n.fileAttachment.value(QStringLiteral("file_url")).toString();
    case IsPendingRole:
        return repo->isLocalId(n.id);
    default:
        return {};
    }
}

QHash<int, QByteArray> NotesModel::roleNames() const
{
    return {
        {IdRole, "noteId"},
        {ContentRole, "content"},
        {AuthorNameRole, "authorName"},
        {AuthorAvatarRole, "authorAvatar"},
        {AuthorInitialsRole, "authorInitials"},
        {PostedAtRole, "postedAt"},
        {PostedTextRole, "postedText"},
        {IsMineRole, "isMine"},
        {AttachmentNameRole, "attachmentName"},
        {AttachmentUrlRole, "attachmentUrl"},
        {IsPendingRole, "isPending"},
    };
}
