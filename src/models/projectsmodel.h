#pragma once

#include "data/types.h"

#include <QAbstractListModel>
#include <qqmlintegration.h>

/**
 * Flattened project tree for the sidebar.
 *
 * Sub-projects follow their parent with a depth, and team (workspace)
 * projects are marked so the drawer can group them separately from personal
 * ones, the way the official clients do.
 */
class ProjectsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY filterChanged)
    Q_PROPERTY(bool teamOnly READ teamOnly WRITE setTeamOnly NOTIFY filterChanged)
    Q_PROPERTY(bool personalOnly READ personalOnly WRITE setPersonalOnly NOTIFY filterChanged)
    Q_PROPERTY(bool excludeInbox READ excludeInbox WRITE setExcludeInbox NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ColorRole,
        DepthRole,
        IsFavoriteRole,
        IsSharedRole,
        IsTeamRole,
        IsInboxRole,
        IsReadOnlyRole,
        CanAssignRole,
        TaskCountRole,
        HasChildrenRole,
    };

    explicit ProjectsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool favoritesOnly() const { return m_favoritesOnly; }
    void setFavoritesOnly(bool value);

    bool teamOnly() const { return m_teamOnly; }
    void setTeamOnly(bool value);

    bool personalOnly() const { return m_personalOnly; }
    void setPersonalOnly(bool value);

    bool excludeInbox() const { return m_excludeInbox; }
    void setExcludeInbox(bool value);

    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void filterChanged();
    void countChanged();

private:
    struct Row {
        Todoist::Project project;
        int depth = 0;
        bool hasChildren = false;
        int taskCount = 0;
    };

    void rebuild();

    QVector<Row> m_rows;
    bool m_favoritesOnly = false;
    bool m_teamOnly = false;
    bool m_personalOnly = false;
    bool m_excludeInbox = false;
    bool m_rebuildQueued = false;
};
