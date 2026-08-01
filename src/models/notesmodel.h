#pragma once

#include "data/types.h"

#include <QAbstractListModel>
#include <qqmlintegration.h>

/// Comments on a task, oldest first.
class NotesModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString taskId READ taskId WRITE setTaskId NOTIFY taskIdChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        ContentRole,
        AuthorNameRole,
        AuthorAvatarRole,
        AuthorInitialsRole,
        PostedAtRole,
        PostedTextRole,
        IsMineRole,
        AttachmentNameRole,
        AttachmentUrlRole,
        IsPendingRole,
    };

    explicit NotesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString taskId() const { return m_taskId; }
    void setTaskId(const QString &id);

    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void taskIdChanged();
    void countChanged();

private:
    QVector<Todoist::Note> m_notes;
    QString m_taskId;
};
