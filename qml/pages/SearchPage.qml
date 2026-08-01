import QtQuick
import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami

import io.github.timpalpant.ktodo

/// Full-text search across task titles and descriptions.
Kirigami.ScrollablePage {
    id: root

    title: i18nc("@title:window", "Search")

    header: QQC2.Control {
        padding: Kirigami.Units.largeSpacing

        contentItem: Kirigami.SearchField {
            id: searchField

            placeholderText: i18nc("@info:placeholder", "Search tasks")
            focus: true

            // Searching on every keystroke would re-query the cache far more
            // than needed; a short idle is imperceptible and much cheaper.
            onTextChanged: debounce.restart()

            Timer {
                id: debounce
                interval: 200
                onTriggered: taskModel.searchText = searchField.text
            }
        }
    }

    Component.onCompleted: searchField.forceActiveFocus()

    TaskModel {
        id: taskModel
        mode: TaskModel.Search
    }

    ListView {
        model: taskModel
        currentIndex: -1
        reuseItems: true

        // TaskDelegate already declares the required properties, so the view
        // fills them straight from the model's roles.
        delegate: TaskDelegate {
            id: delegateItem

            width: ListView.view.width

            onEditRequested: applicationWindow().openTask(delegateItem.taskId)
            onCompleteRequested: App.completeTask(delegateItem.taskId)
            onDeleteRequested: App.deleteTask(delegateItem.taskId)
            onScheduleRequested: {
                scheduler.taskId = delegateItem.taskId;
                scheduler.open();
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: taskModel.empty
            icon.name: "search"
            text: searchField.text === ""
                ? i18n("Search your tasks")
                : i18n("No tasks match “%1”", searchField.text)
        }
    }


    DueDatePicker {
        id: scheduler

        property string taskId: ""

        onPicked: dueString => App.setTaskDue(taskId, dueString)
    }
}
