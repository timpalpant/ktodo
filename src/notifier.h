#pragma once

#include <QObject>
#include <QTimer>

class Repository;

/**
 * Raises desktop notifications for reminders that have come due.
 *
 * Todoist evaluates reminders server-side too, but a native client is
 * expected to notify locally so it works without the web app open. Each
 * reminder is recorded once fired, so a resync does not repeat it.
 */
class Notifier : public QObject
{
    Q_OBJECT

public:
    explicit Notifier(Repository *repo, QObject *parent = nullptr);

    void start();
    void stop();

Q_SIGNALS:
    /// The user activated a notification; the UI should open that task.
    void taskActivated(const QString &taskId);

private:
    void check();

    Repository *m_repo = nullptr;
    QTimer m_timer;
};
