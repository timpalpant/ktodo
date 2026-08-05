import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import io.github.timpalpant.ktodo

/// The main task list, reused for projects, Today, labels and saved filters.
Kirigami.ScrollablePage {
    id: root

    property int mode: TaskModel.Today
    property string projectId: ""
    property string labelName: ""
    property string filterQuery: ""
    property string pageTitle: ""
    property string pageIcon: ""

    readonly property var projectInfo: projectId !== "" ? App.projectDetails(projectId) : ({})
    readonly property bool isProject: mode === TaskModel.ProjectTasks || mode === TaskModel.Inbox
    readonly property bool readOnly: projectInfo.readOnly ?? false

    title: pageTitle

    titleDelegate: RowLayout {
        // Match Kirigami's default title delegate: the header can shrink this
        // custom title before it overflows page actions into the menu.
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.maximumWidth: implicitWidth
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            source: root.pageIcon
            visible: root.pageIcon !== ""
            implicitWidth: Kirigami.Units.iconSizes.smallMedium
            implicitHeight: Kirigami.Units.iconSizes.smallMedium
        }

        // Reserve the same leading slot as an ordinary page icon.  Centering
        // the smaller project-color dot in it aligns the marker and title
        // with task rows without adding a conspicuous gap.
        Item {
            visible: root.pageIcon === "" && root.projectId !== ""
            implicitWidth: Kirigami.Units.iconSizes.smallMedium
            implicitHeight: implicitWidth

            Rectangle {
                anchors.centerIn: parent
                width: Math.round(Kirigami.Units.gridUnit * 0.7)
                height: width
                radius: width / 2
                color: root.projectInfo.colorValue ?? Kirigami.Theme.textColor
            }
        }

        Kirigami.Heading {
            text: root.title
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }

        Kirigami.Icon {
            source: "group"
            visible: root.projectInfo.isTeam ?? false
            opacity: 0.6
            implicitWidth: Kirigami.Units.iconSizes.small
            implicitHeight: Kirigami.Units.iconSizes.small

            QQC2.ToolTip.text: i18ncp("@info:tooltip", "Team project · %1 member",
                                      "Team project · %1 members",
                                      root.projectInfo.collaboratorCount ?? 0)
            QQC2.ToolTip.visible: teamHover.hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

            HoverHandler {
                id: teamHover
            }
        }
    }

    actions: [
        Kirigami.Action {
            text: i18nc("@action:button", "Add Task")
            icon.name: "list-add"
            // Prefer this primary action over secondary page actions. If
            // necessary, Kirigami reduces it to an icon button before moving
            // it into the overflow menu.
            displayHint: Kirigami.DisplayHint.KeepVisible
            enabled: !root.readOnly
            onTriggered: applicationWindow().quickAddTask(root.projectId)
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Add Section")
            icon.name: "list-add"
            visible: root.isProject && !root.readOnly
            onTriggered: sectionPrompt.open()
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Show Completed")
            icon.name: "checkmark"
            checkable: true
            checked: taskModel.showCompleted
            onTriggered: taskModel.showCompleted = !taskModel.showCompleted
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Edit Project")
            icon.name: "document-edit"
            visible: root.isProject && root.projectId !== "" && !(root.projectInfo.isInbox ?? false)
            onTriggered: applicationWindow().showPage(
                Qt.resolvedUrl("ProjectEditorPage.qml"), { projectId: root.projectId })
        }
    ]

    TaskModel {
        id: taskModel

        mode: root.mode
        projectId: root.projectId
        labelName: root.labelName
        filterQuery: root.filterQuery
    }

    QtObject {
        id: dragState

        property bool active: false
        property int fromIndex: -1
        /// "Insert before this row"; count means after the last row.
        property int insertIndex: -1
        /// A task's row under the pointer center, or -1 at a list boundary.
        property int targetIndex: -1
        property bool asSubtask: false
        property bool valid: false
    }

    ListView {
        id: taskListView

        model: taskModel

        // The model rebuilds by resetting, which drops the view back to the
        // top. Every sync does this, so without restoring the offset the list
        // jumps under the user whenever a reorder or a poll lands.
        property real restoreY: -1

        Connections {
            target: taskModel

            function onModelAboutToBeReset() {
                taskListView.restoreY = taskListView.contentY;
            }

            function onModelReset() {
                if (taskListView.restoreY < 0) {
                    return;
                }
                const wanted = taskListView.restoreY;
                taskListView.restoreY = -1;
                // Delegates are realised asynchronously, so contentHeight is
                // not final until after this turn of the event loop.
                Qt.callLater(function () {
                    const maxY = Math.max(0, taskListView.contentHeight
                                             - taskListView.height);
                    taskListView.contentY = Math.min(wanted, maxY);
                });
            }
        }
        currentIndex: -1
        reuseItems: false

        // Rows shifting during a sync is jarring, so movement is animated.
        displaced: Transition {
            NumberAnimation {
                properties: "y"
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.OutCubic
            }
        }

        // One delegate that carries both representations and toggles which is
        // shown. Swapping a Loader's sourceComponent instead would destroy and
        // rebuild the item whenever a row's header-ness changed — which happens
        // constantly while dragging, taking the in-flight grab down with it.
        delegate: Item {
            id: row

            required property int index
            required property bool isHeader
            required property string headerText
            required property string headerId
            required property string taskId
            required property string content
            required property string description
            required property string descriptionHtml
            required property bool descriptionHasLinks
            required property int priority
            required property color priorityColor
            required property string dueText
            required property bool dueIsOverdue
            required property bool dueIsToday
            required property bool hasDue
            required property bool isRecurring
            required property string deadlineText
            required property var labels
            required property string projectName
            required property color projectColor
            required property bool isChecked
            required property int noteCount
            required property bool canCollapse
            required property bool isCollapsed
            required property int subtaskCount
            required property int subtaskCompletedCount
            required property int depth
            required property string assigneeName
            required property string assigneeAvatar
            required property bool showProject
            required property bool isPending

            width: ListView.view.width
            implicitHeight: isHeader ? sectionHeader.implicitHeight : taskRow.implicitHeight
            height: implicitHeight

            Kirigami.ListSectionHeader {
                id: sectionHeader

                width: row.width
                visible: row.isHeader
                text: row.headerText

                readonly property bool deletable: root.isProject && !root.readOnly
                    && row.headerId !== ""

                HoverHandler {
                    id: headerHover
                }

                contentItem: RowLayout {
                    Kirigami.Heading {
                        text: row.headerText
                        level: 4
                        Layout.fillWidth: true
                    }

                    // Revealed on hover: a destructive action sitting
                    // permanently beside every section invites misclicks.
                    QQC2.ToolButton {
                        icon.name: "edit-delete"
                        display: QQC2.AbstractButton.IconOnly
                        text: i18nc("@action:button", "Delete section")
                        visible: sectionHeader.deletable
                        opacity: headerHover.hovered ? 1 : 0
                        enabled: opacity > 0

                        Behavior on opacity {
                            NumberAnimation {
                                duration: Kirigami.Units.shortDuration
                            }
                        }

                        onClicked: {
                            sectionDeletePrompt.sectionId = row.headerId;
                            sectionDeletePrompt.sectionName = row.headerText;
                            sectionDeletePrompt.open();
                        }
                    }
                }
            }

            TaskDelegate {
                id: taskRow

                width: row.width
                visible: !row.isHeader

                taskId: row.taskId
                content: row.content
                description: row.description
                descriptionHtml: row.descriptionHtml
                descriptionHasLinks: row.descriptionHasLinks
                priority: row.priority
                priorityColor: row.priorityColor
                dueText: row.dueText
                dueIsOverdue: row.dueIsOverdue
                dueIsToday: row.dueIsToday
                hasDue: row.hasDue
                isRecurring: row.isRecurring
                deadlineText: row.deadlineText
                labels: row.labels
                projectName: row.projectName
                projectColor: row.projectColor
                isChecked: row.isChecked
                noteCount: row.noteCount
                canCollapse: row.canCollapse
                isCollapsed: row.isCollapsed
                subtaskCount: row.subtaskCount
                subtaskCompletedCount: row.subtaskCompletedCount
                // Held open list-wide so every checkbox lines up, not just
                // those of the rows that happen to have sub-tasks.
                showCollapseGutter: taskModel.hasCollapsibleRows
                depth: row.depth
                assigneeName: row.assigneeName
                assigneeAvatar: row.assigneeAvatar
                showProject: row.showProject
                isPending: row.isPending

                dragItem: row
                dragView: taskListView
                rowIndex: row.index
                draggable: !row.isHeader && !root.readOnly && taskModel.isDraggable(row.index)

                onDragStarted: index => {
                    dragState.fromIndex = index;
                    dragState.insertIndex = index;
                    dragState.targetIndex = -1;
                    dragState.asSubtask = false;
                    dragState.valid = false;
                    dragState.active = true;
                    // Pin the model so a sync landing mid-gesture cannot reset
                    // it and destroy the row being dragged.
                    taskModel.setReordering(true);
                }
                onDragMoved: (insertIndex, targetIndex, asSubtask) => {
                    dragState.insertIndex = insertIndex;
                    dragState.targetIndex = targetIndex;
                    dragState.asSubtask = asSubtask;
                    dragState.valid = taskModel.canDrop(dragState.fromIndex, insertIndex, targetIndex, asSubtask);
                }
                onDragEnded: (fromIndex, insertIndex, targetIndex, asSubtask, moved, canceled) => {
                    dragState.active = false;
                    // The whole hierarchy change happens once, after release,
                    // so the view never rebuilds beneath the active pointer.
                    if (!canceled && moved
                            && taskModel.canDrop(fromIndex, insertIndex, targetIndex, asSubtask)) {
                        taskModel.commitDrop(fromIndex, insertIndex, targetIndex, asSubtask);
                    }
                    taskModel.setReordering(false);
                    dragState.fromIndex = -1;
                    dragState.insertIndex = -1;
                    dragState.targetIndex = -1;
                    dragState.asSubtask = false;
                    dragState.valid = false;
                }

                onCollapseToggled: taskModel.toggleCollapsed(row.index)

                onEditRequested: applicationWindow().openTask(row.taskId)
                onCompleteRequested: {
                    if (row.isChecked) {
                        App.uncompleteTask(row.taskId);
                    } else {
                        App.completeTask(row.taskId);
                    }
                }
                onDeleteRequested: {
                    taskDeletePrompt.taskId = row.taskId;
                    taskDeletePrompt.taskName = row.content;
                    taskDeletePrompt.open();
                }
                onScheduleRequested: {
                    scheduler.taskId = row.taskId;
                    scheduler.open();
                }
            }
        }

        // Shows where the dragged row will land. Declared inside the view so
        // it is parented to the content item and scrolls with the rows.
        Rectangle {
            id: dropIndicator

            z: 50
            visible: dragState.active && dragState.valid && y >= 0
            x: {
                if (!dragState.asSubtask || dragState.targetIndex < 0) {
                    return 0;
                }
                const item = taskListView.itemAtIndex(dragState.targetIndex);
                return item ? parent.mapFromItem(item, 0, 0).x : 0;
            }
            width: {
                if (!dragState.asSubtask || dragState.targetIndex < 0) {
                    return taskListView.width;
                }
                const item = taskListView.itemAtIndex(dragState.targetIndex);
                return item ? item.width : taskListView.width;
            }
            height: {
                if (dragState.asSubtask) {
                    const item = taskListView.itemAtIndex(dragState.targetIndex);
                    return item ? item.height : 0;
                }
                return Math.max(2, Math.round(Kirigami.Units.smallSpacing / 2));
            }
            radius: height / 2
            color: dragState.asSubtask ? "transparent" : Kirigami.Theme.highlightColor
            border.width: dragState.asSubtask ? 2 : 0
            border.color: Kirigami.Theme.highlightColor

            // Sits on the boundary the row will be inserted at, so it reads
            // the same whichever direction the drag came from.
            y: {
                if (!dragState.active || dragState.insertIndex < 0) {
                    return -1;
                }
                if (dragState.asSubtask && dragState.targetIndex >= 0) {
                    const target = taskListView.itemAtIndex(dragState.targetIndex);
                    return target ? parent.mapFromItem(target, 0, 0).y : -1;
                }
                if (dragState.insertIndex >= taskListView.count) {
                    const last = taskListView.itemAtIndex(taskListView.count - 1);
                    return last ? parent.mapFromItem(last, 0, last.height).y - height / 2 : -1;
                }
                const item = taskListView.itemAtIndex(dragState.insertIndex);
                return item ? parent.mapFromItem(item, 0, 0).y - height / 2 : -1;
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: taskModel.empty

            icon.name: root.mode === TaskModel.Today ? "checkmark" : "view-task"
            text: {
                switch (root.mode) {
                case TaskModel.Today:
                    return i18n("Nothing due today");
                case TaskModel.SavedFilter:
                    return i18n("No tasks match this filter");
                case TaskModel.AssignedToMe:
                    return i18n("Nothing is assigned to you");
                default:
                    return i18n("No tasks here yet");
                }
            }
            explanation: root.mode === TaskModel.Today
                ? i18n("Enjoy the quiet, or add something new.")
                : i18n("Press Ctrl+N to add one.")

            helpfulAction: Kirigami.Action {
                text: i18nc("@action:button", "Add Task")
                icon.name: "list-add"
                enabled: !root.readOnly
                onTriggered: applicationWindow().quickAddTask(root.projectId)
            }
        }
    }

    // -- Dialogs ------------------------------------------------------------


    DueDatePicker {
        id: scheduler

        property string taskId: ""

        onPicked: dueString => App.setTaskDue(taskId, dueString)
    }

    Kirigami.PromptDialog {
        id: sectionPrompt

        title: i18nc("@title:window", "New Section")
        standardButtons: Kirigami.Dialog.Cancel

        onOpened: sectionNameField.forceActiveFocus()

        QQC2.TextField {
            id: sectionNameField
            placeholderText: i18nc("@info:placeholder", "Section name")
            onAccepted: sectionPrompt.createSection()
        }

        function createSection() {
            if (sectionNameField.text.trim() !== "") {
                App.addSection(sectionNameField.text, root.projectId);
            }
            sectionNameField.text = "";
            sectionPrompt.close();
        }

        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Create")
                icon.name: "list-add"
                onTriggered: sectionPrompt.createSection()
            }
        ]
    }

    Kirigami.PromptDialog {
        id: sectionDeletePrompt

        property string sectionId: ""
        property string sectionName: ""

        title: i18nc("@title:window", "Delete Section")
        subtitle: i18n("“%1” and every task in it will be deleted.", sectionName)
        standardButtons: Kirigami.Dialog.Cancel

        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    App.deleteSection(sectionDeletePrompt.sectionId);
                    sectionDeletePrompt.close();
                }
            }
        ]
    }

    Kirigami.PromptDialog {
        id: taskDeletePrompt

        property string taskId: ""
        property string taskName: ""

        title: i18nc("@title:window", "Delete Task")
        subtitle: i18n("“%1” will be deleted. This cannot be undone.", taskName)
        standardButtons: Kirigami.Dialog.Cancel

        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    App.deleteTask(taskDeletePrompt.taskId);
                    taskDeletePrompt.close();
                }
            }
        ]
    }
}
