import QtQuick

import io.github.timpalpant.ktodo

/// Upcoming is the shared task list grouped by day; the model does the work.
TaskListPage {
    mode: TaskModel.Upcoming
    pageTitle: i18n("Upcoming")
    pageIcon: "view-calendar-upcoming-days"
}
