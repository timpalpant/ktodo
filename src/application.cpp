#include "application.h"

#include <QQmlEngine>

#include "auth/authmanager.h"
#include "data/repository.h"
#include "sync/syncengine.h"
#include "util/colors.h"
#include "util/duedate.h"
#include "util/richtext.h"

#include <KLocalizedString>

#include <QJsonArray>
#include <QJsonObject>

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

QJsonValue toJson(const QVariant &v)
{
    return QJsonValue::fromVariant(v);
}

} // namespace

Application::Application(Repository *repo, SyncEngine *sync, AuthManager *auth, QObject *parent)
    : QObject(parent)
    , m_repo(repo)
    , m_sync(sync)
    , m_auth(auth)
{
    connect(m_repo, &Repository::userChanged, this, &Application::userChanged);
    connect(m_sync, &SyncEngine::commandRejected, this, [this](const QString &type, const QString &error) {
        Q_UNUSED(type);
        Q_EMIT errorOccurred(error.isEmpty() ? i18n("Todoist rejected a change.") : i18n("Todoist rejected a change: %1", error));
    });
}

void Application::touch()
{
    if (m_sync) {
        m_sync->scheduleFlush();
    }
}

QString Application::userName() const
{
    return m_repo->currentUser().fullName;
}

QString Application::userEmail() const
{
    return m_repo->currentUser().email;
}

QString Application::userAvatar() const
{
    return m_repo->currentUser().avatarUrl();
}

QString Application::userInitials() const
{
    return initialsFor(m_repo->currentUser().fullName);
}

QString Application::inboxProjectId() const
{
    return m_repo->currentUser().inboxProjectId;
}

bool Application::isTeamAccount() const
{
    return !m_repo->currentUser().businessAccountId.isEmpty();
}

// -- Tasks ------------------------------------------------------------------

QString Application::addTask(const QString &content,
                             const QString &projectId,
                             const QString &sectionId,
                             const QString &parentId,
                             const QString &dueString,
                             int priority,
                             const QStringList &labels,
                             const QString &description,
                             const QString &responsibleUid)
{
    if (content.trimmed().isEmpty()) {
        return {};
    }
    const QString target = projectId.isEmpty() ? inboxProjectId() : projectId;
    const QString id = m_repo->addItem(content.trimmed(), target, sectionId, parentId, dueString, priority, labels, description, responsibleUid);
    touch();
    return id;
}

void Application::updateTask(const QString &id, const QVariantMap &changes)
{
    if (id.isEmpty() || changes.isEmpty()) {
        return;
    }

    QJsonObject json;
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        const QString key = it.key();
        if (key == QLatin1String("due")) {
            const QString s = it.value().toString();
            json[key] = s.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(QJsonObject{{QStringLiteral("string"), s}});
        } else if (key == QLatin1String("labels")) {
            QJsonArray a;
            for (const QString &l : it.value().toStringList()) {
                a.append(l);
            }
            json[key] = a;
        } else if (key == QLatin1String("priority")) {
            // QML speaks Todoist's UI numbering (p1 = urgent).
            json[key] = 5 - it.value().toInt();
        } else {
            json[key] = toJson(it.value());
        }
    }

    m_repo->updateItem(id, json);
    touch();
}

void Application::completeTask(const QString &id)
{
    m_repo->completeItem(id);
    touch();
}

void Application::uncompleteTask(const QString &id)
{
    m_repo->uncompleteItem(id);
    touch();
}

void Application::deleteTask(const QString &id)
{
    m_repo->deleteItem(id);
    touch();
}

void Application::setTaskDue(const QString &id, const QString &dueString)
{
    if (dueString.isEmpty()) {
        m_repo->clearItemDue(id);
    } else {
        m_repo->setItemDue(id, dueString);
    }
    touch();
}

void Application::clearTaskDue(const QString &id)
{
    m_repo->clearItemDue(id);
    touch();
}

void Application::setTaskPriority(const QString &id, int uiPriority)
{
    m_repo->setItemPriority(id, 5 - qBound(1, uiPriority, 4));
    touch();
}

void Application::setTaskAssignee(const QString &id, const QString &userId)
{
    m_repo->setItemAssignee(id, userId);
    touch();
}

void Application::moveTask(const QString &id, const QString &projectId, const QString &sectionId)
{
    m_repo->moveItem(id, projectId, sectionId, {});
    touch();
}

QVariantMap Application::taskDetails(const QString &id) const
{
    const Item i = m_repo->item(id);
    if (i.id.isEmpty()) {
        return {};
    }
    const Project p = m_repo->project(i.projectId);

    QVariantMap map;
    map[QStringLiteral("id")] = i.id;
    map[QStringLiteral("content")] = i.content;
    map[QStringLiteral("description")] = i.description;
    map[QStringLiteral("projectId")] = i.projectId;
    map[QStringLiteral("projectName")] = p.name;
    map[QStringLiteral("projectColor")] = Colors::fromTodoistName(p.color);
    map[QStringLiteral("sectionId")] = i.sectionId;
    map[QStringLiteral("sectionName")] = i.sectionId.isEmpty() ? QString() : m_repo->section(i.sectionId).name;
    map[QStringLiteral("parentId")] = i.parentId;
    map[QStringLiteral("labels")] = i.labels;
    map[QStringLiteral("priority")] = i.uiPriority();
    map[QStringLiteral("checked")] = i.checked;
    map[QStringLiteral("noteCount")] = i.noteCount;

    map[QStringLiteral("dueString")] = i.due.string;
    map[QStringLiteral("dueText")] = DueDate::formatLong(i.due);
    map[QStringLiteral("hasDue")] = i.due.isValid() || !i.due.string.isEmpty();
    map[QStringLiteral("dueDate")] = i.due.localDate();
    map[QStringLiteral("dueIsOverdue")] = DueDate::isOverdue(i.due);
    map[QStringLiteral("isRecurring")] = i.due.isRecurring;
    map[QStringLiteral("deadlineText")] = i.deadline.isValid() ? DueDate::formatShort(i.deadline) : QString();

    map[QStringLiteral("assigneeId")] = i.responsibleUid;
    const Collaborator assignee = m_repo->collaborator(i.responsibleUid);
    map[QStringLiteral("assigneeName")] = assignee.fullName;
    map[QStringLiteral("assigneeAvatar")] = assignee.avatarUrl();
    map[QStringLiteral("assigneeInitials")] = initialsFor(assignee.fullName);

    // Assignment only exists on shared projects, and only where the user's
    // role allows it; the editor hides the control otherwise.
    map[QStringLiteral("canAssign")] = p.isShared && p.canAssignTasks;
    map[QStringLiteral("canComment")] = p.canComment;
    map[QStringLiteral("readOnly")] = p.isReadOnly();

    return map;
}

QVariantList Application::subtasks(const QString &parentId) const
{
    QVariantList out;
    for (const Item &child : m_repo->subtasks(parentId)) {
        out.append(QVariantMap{
            {QStringLiteral("id"), child.id},
            {QStringLiteral("content"), child.content},
            {QStringLiteral("checked"), child.checked},
            {QStringLiteral("isPending"), m_repo->isLocalId(child.id)},
        });
    }
    return out;
}

QString Application::addSubtask(const QString &parentId, const QString &content)
{
    if (content.trimmed().isEmpty() || parentId.isEmpty()) {
        return {};
    }
    // A sub-task lives wherever its parent does; Todoist rejects a child in a
    // different project.
    const Item parent = m_repo->item(parentId);
    if (parent.id.isEmpty()) {
        return {};
    }
    const QString id = m_repo->addItem(content.trimmed(), parent.projectId, parent.sectionId, parentId, {}, 1, {}, {}, {});
    touch();
    return id;
}

// -- Projects ---------------------------------------------------------------

QString Application::addProject(const QString &name, const QString &color, const QString &parentId, bool favorite)
{
    if (name.trimmed().isEmpty()) {
        return {};
    }
    const QString id = m_repo->addProject(name.trimmed(), color.isEmpty() ? QStringLiteral("charcoal") : color, parentId, favorite);
    touch();
    return id;
}

void Application::renameProject(const QString &id, const QString &name)
{
    m_repo->updateProject(id, QJsonObject{{QStringLiteral("name"), name}});
    touch();
}

void Application::setProjectColor(const QString &id, const QString &color)
{
    m_repo->updateProject(id, QJsonObject{{QStringLiteral("color"), color}});
    touch();
}

void Application::setProjectFavorite(const QString &id, bool favorite)
{
    m_repo->updateProject(id, QJsonObject{{QStringLiteral("is_favorite"), favorite}});
    touch();
}

void Application::deleteProject(const QString &id)
{
    m_repo->deleteProject(id);
    touch();
}

void Application::archiveProject(const QString &id)
{
    m_repo->archiveProject(id);
    touch();
}

QVariantMap Application::projectDetails(const QString &id) const
{
    const Project p = m_repo->project(id);
    if (p.id.isEmpty()) {
        return {};
    }

    QVariantMap map;
    map[QStringLiteral("id")] = p.id;
    map[QStringLiteral("name")] = p.name;
    map[QStringLiteral("description")] = p.description;
    map[QStringLiteral("color")] = p.color;
    map[QStringLiteral("colorValue")] = Colors::fromTodoistName(p.color);
    map[QStringLiteral("isFavorite")] = p.isFavorite;
    map[QStringLiteral("isShared")] = p.isShared;
    map[QStringLiteral("isTeam")] = p.isTeamProject();
    map[QStringLiteral("isInbox")] = p.isInbox;
    map[QStringLiteral("canAssign")] = p.canAssignTasks;
    map[QStringLiteral("canComment")] = p.canComment;
    map[QStringLiteral("readOnly")] = p.isReadOnly();
    map[QStringLiteral("role")] = p.role;
    map[QStringLiteral("collaboratorCount")] = m_repo->collaborators(p.id).size();
    return map;
}

// -- Sections ---------------------------------------------------------------

QString Application::addSection(const QString &name, const QString &projectId)
{
    if (name.trimmed().isEmpty() || projectId.isEmpty()) {
        return {};
    }
    const QString id = m_repo->addSection(name.trimmed(), projectId);
    touch();
    return id;
}

void Application::renameSection(const QString &id, const QString &name)
{
    m_repo->updateSection(id, QJsonObject{{QStringLiteral("name"), name}});
    touch();
}

void Application::deleteSection(const QString &id)
{
    m_repo->deleteSection(id);
    touch();
}

// -- Comments ---------------------------------------------------------------

void Application::addComment(const QString &taskId, const QString &content)
{
    if (content.trimmed().isEmpty()) {
        return;
    }
    m_repo->addNote(taskId, content.trimmed());
    touch();
}

void Application::deleteComment(const QString &id)
{
    m_repo->deleteNote(id);
    touch();
}

// -- Labels -----------------------------------------------------------------

QString Application::addLabel(const QString &name, const QString &color)
{
    if (name.trimmed().isEmpty()) {
        return {};
    }
    const QString id = m_repo->addLabel(name.trimmed(), color.isEmpty() ? QStringLiteral("charcoal") : color);
    touch();
    return id;
}

void Application::deleteLabel(const QString &id)
{
    m_repo->deleteLabel(id);
    touch();
}

// -- Misc -------------------------------------------------------------------

QString Application::richText(const QString &text) const
{
    return RichText::toHtml(text);
}

bool Application::hasLinks(const QString &text) const
{
    return RichText::hasLinks(text);
}

QStringList Application::colorNames() const
{
    return Colors::paletteNames();
}

QColor Application::colorFor(const QString &name) const
{
    return Colors::fromTodoistName(name);
}

void Application::signOut()
{
    if (m_sync) {
        m_sync->stop();
    }
    m_repo->clearAll();
    if (m_auth) {
        m_auth->signOut();
    }
}

// ---------------------------------------------------------------------------
// QML singleton access
//
// The instance is built in main() with its dependencies wired up; create()
// hands that same object to the QML engine. Ownership stays in C++ so the
// engine does not delete an object main() still refers to.
// ---------------------------------------------------------------------------

namespace {
Application *s_instance = nullptr;
}

void Application::setInstance(Application *instance)
{
    s_instance = instance;
}

Application *Application::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine);
    Q_UNUSED(scriptEngine);
    Q_ASSERT(s_instance);
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}
