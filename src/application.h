#pragma once

#include <QColor>
#include <QObject>
#include <QVariantMap>
#include <qqmlintegration.h>

class AuthManager;
class QJSEngine;
class QQmlEngine;
class Repository;
class SyncEngine;

/**
 * The action surface QML talks to.
 *
 * Models handle reading; this handles writing and the odd lookup that does
 * not justify a model of its own. Everything here is safe to call offline.
 */
class Application : public QObject
{
    Q_OBJECT
    // Exposed as a typed QML singleton rather than a context property, so
    // qmllint and the QML compiler can check every use site.
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

    Q_PROPERTY(QString userName READ userName NOTIFY userChanged)
    Q_PROPERTY(QString userEmail READ userEmail NOTIFY userChanged)
    Q_PROPERTY(QString userAvatar READ userAvatar NOTIFY userChanged)
    Q_PROPERTY(QString userInitials READ userInitials NOTIFY userChanged)
    Q_PROPERTY(QString inboxProjectId READ inboxProjectId NOTIFY userChanged)
    Q_PROPERTY(bool isTeamAccount READ isTeamAccount NOTIFY userChanged)

public:
    Application(Repository *repo, SyncEngine *sync, AuthManager *auth, QObject *parent = nullptr);

    /// Registers the instance main() built; must run before the QML engine loads.
    static void setInstance(Application *instance);
    static Application *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    QString userName() const;
    QString userEmail() const;
    QString userAvatar() const;
    QString userInitials() const;
    QString inboxProjectId() const;
    bool isTeamAccount() const;

    // -- Tasks ------------------------------------------------------------
    Q_INVOKABLE QString addTask(const QString &content,
                                const QString &projectId,
                                const QString &sectionId = {},
                                const QString &parentId = {},
                                const QString &dueString = {},
                                int priority = 1,
                                const QStringList &labels = {},
                                const QString &description = {},
                                const QString &responsibleUid = {});

    Q_INVOKABLE void updateTask(const QString &id, const QVariantMap &changes);
    Q_INVOKABLE void completeTask(const QString &id);
    Q_INVOKABLE void uncompleteTask(const QString &id);
    Q_INVOKABLE void deleteTask(const QString &id);
    Q_INVOKABLE void setTaskDue(const QString &id, const QString &dueString);
    Q_INVOKABLE void clearTaskDue(const QString &id);
    Q_INVOKABLE void rescheduleOverdueTasks(const QString &dueString);
    Q_INVOKABLE void setTaskPriority(const QString &id, int uiPriority);
    Q_INVOKABLE void setTaskAssignee(const QString &id, const QString &userId);
    Q_INVOKABLE void moveTask(const QString &id, const QString &projectId, const QString &sectionId = {});

    /// Everything the task editor needs, as a plain map.
    Q_INVOKABLE QVariantMap taskDetails(const QString &id) const;

    /// Direct children of @p parentId, as {id, content, checked} maps.
    Q_INVOKABLE QVariantList subtasks(const QString &parentId) const;

    /// Adds a child of @p parentId, inheriting its project and section.
    Q_INVOKABLE QString addSubtask(const QString &parentId, const QString &content);

    // -- Projects ---------------------------------------------------------
    Q_INVOKABLE QString addProject(const QString &name, const QString &color = {}, const QString &parentId = {}, bool favorite = false,
                                   const QString &description = {});
    Q_INVOKABLE void renameProject(const QString &id, const QString &name);
    Q_INVOKABLE void setProjectDescription(const QString &id, const QString &description);
    Q_INVOKABLE void setProjectColor(const QString &id, const QString &color);
    Q_INVOKABLE void setProjectFavorite(const QString &id, bool favorite);
    Q_INVOKABLE void deleteProject(const QString &id);
    Q_INVOKABLE void archiveProject(const QString &id);
    Q_INVOKABLE QVariantMap projectDetails(const QString &id) const;

    // -- Sections ---------------------------------------------------------
    Q_INVOKABLE QString addSection(const QString &name, const QString &projectId);
    Q_INVOKABLE void renameSection(const QString &id, const QString &name);
    Q_INVOKABLE void deleteSection(const QString &id);

    // -- Comments ---------------------------------------------------------
    Q_INVOKABLE void addComment(const QString &taskId, const QString &content);
    Q_INVOKABLE void deleteComment(const QString &id);

    // -- Labels -----------------------------------------------------------
    Q_INVOKABLE QString addLabel(const QString &name, const QString &color = {});
    Q_INVOKABLE void deleteLabel(const QString &id);

    // -- Misc -------------------------------------------------------------
    /// Escapes @p text and turns Markdown links and bare URLs into anchors.
    Q_INVOKABLE QString richText(const QString &text) const;
    Q_INVOKABLE bool hasLinks(const QString &text) const;

    Q_INVOKABLE QStringList colorNames() const;
    Q_INVOKABLE QColor colorFor(const QString &name) const;
    Q_INVOKABLE void signOut();

Q_SIGNALS:
    void userChanged();
    /// Surfaced as an inline message in the UI.
    void errorOccurred(const QString &message);

private:
    /// Nudges the sync engine after a local edit.
    void touch();

    Repository *m_repo = nullptr;
    SyncEngine *m_sync = nullptr;
    AuthManager *m_auth = nullptr;
};
