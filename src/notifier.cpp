#include "notifier.h"

#include "data/repository.h"

#include <KLocalizedString>
#include <KNotification>

#include <QDateTime>

using namespace Todoist;

namespace {
constexpr int CheckIntervalMs = 60 * 1000;
// A reminder that came due while the app was closed is still worth showing,
// but only if it is recent enough to be actionable.
constexpr int MaxLatenessSecs = 60 * 60;
} // namespace

Notifier::Notifier(Repository *repo, QObject *parent)
    : QObject(parent)
    , m_repo(repo)
{
    m_timer.setInterval(CheckIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &Notifier::check);
}

void Notifier::start()
{
    m_timer.start();
    check();
}

void Notifier::stop()
{
    m_timer.stop();
}

void Notifier::check()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QString me = m_repo->currentUserId();

    for (const Reminder &r : m_repo->reminders()) {
        // Reminders can be addressed to a collaborator on a shared project.
        if (!r.notifyUid.isEmpty() && !me.isEmpty() && r.notifyUid != me) {
            continue;
        }
        if (!r.due.isValid()) {
            continue;
        }

        const QDateTime fireAt = r.due.localDateTime();
        if (!fireAt.isValid() || fireAt > now) {
            continue;
        }
        if (fireAt.secsTo(now) > MaxLatenessSecs) {
            continue;
        }

        // Keyed by fire time so a recurring reminder notifies once per
        // occurrence rather than once ever.
        const QString key = QStringLiteral("reminder:%1:%2").arg(r.id, fireAt.toString(Qt::ISODate));
        if (m_repo->wasNotified(key)) {
            continue;
        }

        const Item item = m_repo->item(r.itemId);
        if (item.id.isEmpty() || item.checked) {
            continue;
        }

        auto *notification = new KNotification(QStringLiteral("reminder"));
        notification->setTitle(i18n("Task reminder"));
        notification->setText(item.content);
        notification->setIconName(QStringLiteral("view-task"));

        const QString projectName = m_repo->project(item.projectId).name;
        if (!projectName.isEmpty()) {
            notification->setText(i18nc("task content and its project", "%1 — %2", item.content, projectName));
        }

        auto *openAction = notification->addAction(i18n("Open"));
        connect(openAction, &KNotificationAction::activated, this, [this, id = item.id] { Q_EMIT taskActivated(id); });

        auto *completeAction = notification->addAction(i18n("Complete"));
        connect(completeAction, &KNotificationAction::activated, this, [this, id = item.id] { m_repo->completeItem(id); });

        notification->sendEvent();
        m_repo->markNotified(key);
    }
}
