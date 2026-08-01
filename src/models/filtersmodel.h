#pragma once

#include "data/types.h"

#include <QAbstractListModel>
#include <qqmlintegration.h>

/// Saved filters, shown in the sidebar with live match counts.
class FiltersModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        QueryRole,
        ColorRole,
        IsFavoriteRole,
        TaskCountRole,
    };

    explicit FiltersModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void countChanged();

private:
    QVector<Todoist::Filter> m_filters;
    QVector<int> m_counts;
};
