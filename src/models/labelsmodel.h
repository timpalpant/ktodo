#pragma once

#include "data/types.h"

#include <QAbstractListModel>
#include <qqmlintegration.h>

/// Labels for the sidebar and the label picker.
class LabelsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ColorRole,
        IsFavoriteRole,
        TaskCountRole,
    };

    explicit LabelsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    /// Names only, for the label picker's completion.
    Q_INVOKABLE QStringList allNames() const;

Q_SIGNALS:
    void countChanged();

private:
    QVector<Todoist::Label> m_labels;
    QHash<QString, int> m_counts;
};
