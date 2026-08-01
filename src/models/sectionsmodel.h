#pragma once

#include "data/types.h"

#include <QAbstractListModel>
#include <qqmlintegration.h>

/// Sections of one project, used by the section picker in the task editor.
class SectionsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString projectId READ projectId WRITE setProjectId NOTIFY projectIdChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TaskCountRole,
    };

    explicit SectionsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString projectId() const { return m_projectId; }
    void setProjectId(const QString &id);

    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void projectIdChanged();
    void countChanged();

private:
    QVector<Todoist::Section> m_sections;
    QString m_projectId;
};
