import QtQuick

import io.github.timpalpant.ktodo

/// Upcoming adds a compact calendar navigator to the date-grouped task list.
TaskListPage {
    mode: TaskModel.Upcoming
    pageTitle: i18n("Upcoming")
    pageIcon: "view-calendar-upcoming-days"
    showUpcomingCalendar: true
}
