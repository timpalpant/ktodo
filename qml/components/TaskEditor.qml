import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kirigami.dialogs as KDialogs
import org.kde.kirigamiaddons.components as Components
import org.kde.kirigamiaddons.formcard as FormCard

import io.github.timpalpant.ktodo

/**
 * Full editor for a single task.
 *
 * Metadata changes (due date, priority, labels, assignee) apply immediately,
 * matching how the official clients behave. The title and description are
 * committed on close or on Save so typing is not sent keystroke by keystroke.
 */
Kirigami.Dialog {
    id: root

    property string taskId: ""

    /// Snapshot of the task, refreshed whenever the cache changes.
    property var task: ({})

    readonly property bool readOnly: task.readOnly ?? false

    title: i18nc("@title:window", "Task")

    preferredWidth: Kirigami.Units.gridUnit * 32
    // Height follows the content: a task with no description should not open
    // a dialog two-thirds full of blank space.
    maximumHeight: Kirigami.Units.gridUnit * 40

    // Kirigami.Dialog is edge-to-edge by default, which suits list content but
    // leaves free-standing controls pressed against the frame.
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0

    standardButtons: Kirigami.Dialog.NoButton
    showCloseButton: true

    header: KDialogs.DialogHeader {
        dialog: root
        padding: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing
    }

    /// The description shows rendered links until you click into it.
    property bool editingDescription: false

    /// Direct children, refreshed alongside the task itself.
    property var subtasks: []

    onTaskIdChanged: reload()
    Component.onCompleted: reload()

    onClosed: commitText()

    function reload() {
        if (taskId === "") {
            return;
        }
        task = App.taskDetails(taskId);
        subtasks = App.subtasks(taskId);
        if (!contentField.activeFocus) {
            contentField.text = task.content ?? "";
        }
        if (!descriptionField.activeFocus) {
            descriptionField.text = task.description ?? "";
        }
    }

    function commitText() {
        if (taskId === "" || readOnly) {
            return;
        }
        const newContent = contentField.text.trim();
        const newDescription = descriptionField.text;
        if (newContent === "") {
            return;
        }
        if (newContent !== (task.content ?? "") || newDescription !== (task.description ?? "")) {
            App.updateTask(taskId, {
                content: newContent,
                description: newDescription
            });
        }
    }

    Connections {
        target: App
        function onUserChanged() {
            root.reload();
        }
    }

    // Metadata edits round-trip through the cache, so re-read after each.
    Connections {
        target: Sync
        function onSyncFinished(success) {
            root.reload();
        }
    }

    // Horizontal padding is zero on the dialog so its scroll view spans the
    // full frame and the scrollbar sits against the edge; the inset lives
    // here instead.
    QQC2.Control {
        // Matches the header, so title and content share a left edge.
        leftPadding: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing
        rightPadding: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing
        topPadding: 0
        bottomPadding: Kirigami.Units.largeSpacing

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.InlineMessage {
                text: i18n("You have read-only access to this project.")
                type: Kirigami.MessageType.Information
                visible: root.readOnly
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                PriorityBadge {
                    priority: 5 - (root.task.priority ?? 4)
                    checked: root.task.checked ?? false
                    accent: {
                        switch (root.task.priority ?? 4) {
                        case 1: return "#d1453b";
                        case 2: return "#eb8909";
                        case 3: return "#246fe0";
                        default: return Kirigami.Theme.textColor;
                        }
                    }
                    enabled: !root.readOnly
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: Kirigami.Units.smallSpacing

                    onToggled: {
                        if (root.task.checked) {
                            App.uncompleteTask(root.taskId);
                        } else {
                            App.completeTask(root.taskId);
                            root.close();
                        }
                    }
                }

                QQC2.TextArea {
                    id: contentField

                    placeholderText: i18nc("@info:placeholder", "Task name")
                    wrapMode: TextEdit.Wrap
                    readOnly: root.readOnly
                    background: null
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.1
                    Layout.fillWidth: true

                    Keys.onReturnPressed: event => {
                        if (!(event.modifiers & Qt.ShiftModifier)) {
                            root.commitText();
                            event.accepted = true;
                            return;
                        }
                        event.accepted = false;
                    }
                }
            }

            // Read mode: Markdown links and bare URLs are rendered and clickable.
            // Clicking anywhere that is not a link switches to editing the raw text.
            QQC2.Label {
                id: descriptionDisplay

                readonly property bool empty: (root.task.description ?? "") === ""

                visible: !root.editingDescription
                text: empty
                    ? i18n("Add a description…")
                    : App.richText(root.task.description)
                textFormat: Text.RichText
                wrapMode: Text.WordWrap
                opacity: empty ? 0.5 : 1.0
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.smallSpacing
                Layout.leftMargin: Kirigami.Units.iconSizes.smallMedium
                    + Kirigami.Units.smallSpacing


                onLinkActivated: link => Qt.openUrlExternally(link)

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: descriptionDisplay.linkAt(mouseX, mouseY) !== ""
                        ? Qt.PointingHandCursor
                        : (root.readOnly ? Qt.ArrowCursor : Qt.IBeamCursor)

                    onClicked: mouse => {
                        const link = descriptionDisplay.linkAt(mouse.x, mouse.y);
                        if (link !== "") {
                            Qt.openUrlExternally(link);
                            return;
                        }
                        if (!root.readOnly) {
                            root.editingDescription = true;
                            descriptionField.forceActiveFocus();
                        }
                    }
                }
            }

            QQC2.TextArea {
                id: descriptionField

                placeholderText: i18nc("@info:placeholder",
                                       "Description — [text](https://example.com) becomes a link")
                wrapMode: TextEdit.Wrap
                readOnly: root.readOnly
                visible: root.editingDescription
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.iconSizes.smallMedium
                    + Kirigami.Units.smallSpacing

                // One line, growing with the text. Uses contentHeight;
                // implicitHeight derives from the height the layout sets.
                Layout.preferredHeight: Math.min(
                    Math.max(contentHeight + topPadding + bottomPadding,
                             Kirigami.Units.gridUnit * 2),
                    Kirigami.Units.gridUnit * 10)

                onActiveFocusChanged: {
                    if (!activeFocus) {
                        root.commitText();
                        root.editingDescription = false;
                    }
                }
            }

            FormCard.FormCard {
                // FormCard caps itself at maximumWidth and centres within anything
                // wider, which would inset these rows relative to the dialog's
                // other controls. Keeping the cap just below the available width
                // lines them up while preserving the rounded-card look, which
                // FormCard only applies when it considers itself width-restricted.
                maximumWidth: width - 1
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.largeSpacing

                FormCard.FormButtonDelegate {
                    text: i18nc("@label", "Due date")
                    // A repeating task shows the next occurrence; the pattern
                    // itself lives on the Repeat row below.
                    description: root.task.hasDue
                        ? (root.task.dueText || root.task.dueString)
                        : i18n("No date")
                    icon.name: "view-calendar-day"
                    enabled: !root.readOnly
                    onClicked: duePicker.open()
                }

                FormCard.FormDelegateSeparator {}

                FormCard.FormButtonDelegate {
                    text: i18nc("@label", "Repeat")
                    description: root.task.isRecurring
                        ? (root.task.dueString || i18n("Repeats"))
                        : i18n("Does not repeat")
                    icon.name: root.task.isRecurring ? "view-refresh" : "view-calendar-day"
                    enabled: !root.readOnly
                    onClicked: recurrencePicker.open()
                }

                FormCard.FormDelegateSeparator {}

                FormCard.FormButtonDelegate {
                    text: i18nc("@label", "Project")
                    description: root.task.sectionName
                        ? i18nc("project / section", "%1 / %2", root.task.projectName, root.task.sectionName)
                        : (root.task.projectName ?? "")
                    icon.name: "folder"
                    enabled: !root.readOnly
                    onClicked: projectPicker.open()
                }

                FormCard.FormDelegateSeparator {}

                FormCard.FormButtonDelegate {
                    text: i18nc("@label", "Labels")
                    description: (root.task.labels && root.task.labels.length > 0)
                        ? root.task.labels.join(", ")
                        : i18n("None")
                    icon.name: "tag"
                    enabled: !root.readOnly
                    onClicked: labelPicker.open()
                }

                FormCard.FormDelegateSeparator {
                    visible: root.task.canAssign ?? false
                }

                FormCard.FormButtonDelegate {
                    text: i18nc("@label", "Assignee")
                    description: root.task.assigneeName || i18n("Nobody")
                    icon.name: "user-identity"
                    visible: root.task.canAssign ?? false
                    enabled: !root.readOnly
                    onClicked: assigneePicker.open()

                    trailing: Components.Avatar {
                        name: root.task.assigneeName ?? ""
                        source: root.task.assigneeAvatar ?? ""
                        visible: (root.task.assigneeName ?? "") !== ""
                        implicitWidth: Kirigami.Units.iconSizes.medium
                        implicitHeight: Kirigami.Units.iconSizes.medium
                    }
                }
            }

            FormCard.FormCard {
                maximumWidth: width - 1
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.largeSpacing

                FormCard.AbstractFormDelegate {
                    background: null
                    Layout.fillWidth: true

                    contentItem: RowLayout {
                        QQC2.Label {
                            text: i18nc("@label", "Priority")
                            Layout.fillWidth: true
                        }
                        PriorityPicker {
                            value: root.task.priority ?? 4
                            enabled: !root.readOnly
                            onPicked: level => App.setTaskPriority(root.taskId, level)
                        }
                    }
                }
            }

            FormCard.FormHeader {
                title: root.subtasks.length > 0
                    ? i18ncp("@title:group", "%1 Sub-task", "%1 Sub-tasks", root.subtasks.length)
                    : i18nc("@title:group", "Sub-tasks")
                // A sub-task cannot itself have children, so the section is hidden
                // when this task is already one.
                visible: (root.task.parentId ?? "") === ""
            }

            FormCard.FormCard {
                maximumWidth: width - 1
                visible: (root.task.parentId ?? "") === ""

                Repeater {
                    model: root.subtasks

                    delegate: FormCard.AbstractFormDelegate {
                        id: subtaskRow

                        required property var modelData

                        background: null
                        onClicked: applicationWindow().openTask(subtaskRow.modelData.id)

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.largeSpacing

                            QQC2.CheckBox {
                                checked: subtaskRow.modelData.checked
                                enabled: !root.readOnly
                                onToggled: {
                                    if (subtaskRow.modelData.checked) {
                                        App.uncompleteTask(subtaskRow.modelData.id);
                                    } else {
                                        App.completeTask(subtaskRow.modelData.id);
                                    }
                                    root.reload();
                                }
                            }

                            QQC2.Label {
                                text: subtaskRow.modelData.content
                                elide: Text.ElideRight
                                font.strikeout: subtaskRow.modelData.checked
                                opacity: subtaskRow.modelData.checked ? 0.6 : 1.0
                                Layout.fillWidth: true
                            }

                            // Revealed on hover, like the task rows in a list.
                            QQC2.ToolButton {
                                icon.name: "edit-delete"
                                display: QQC2.AbstractButton.IconOnly
                                text: i18nc("@action:button", "Delete sub-task")
                                visible: !root.readOnly
                                opacity: subtaskRow.hovered ? 1 : 0
                                enabled: subtaskRow.hovered

                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: Kirigami.Units.shortDuration
                                    }
                                }

                                onClicked: {
                                    subtaskDeletePrompt.subtaskId = subtaskRow.modelData.id;
                                    subtaskDeletePrompt.subtaskName = subtaskRow.modelData.content;
                                    subtaskDeletePrompt.open();
                                }

                                QQC2.ToolTip.text: text
                                QQC2.ToolTip.visible: hovered
                                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                            }
                        }
                    }
                }

                FormCard.AbstractFormDelegate {
                    background: null
                    visible: !root.readOnly

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: "list-add"
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                            opacity: 0.6
                        }

                        QQC2.TextField {
                            id: subtaskField

                            placeholderText: i18nc("@info:placeholder", "Add a sub-task")
                            background: null
                            Layout.fillWidth: true

                            onAccepted: {
                                if (text.trim() === "") {
                                    return;
                                }
                                App.addSubtask(root.taskId, text);
                                text = "";
                                root.reload();
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.largeSpacing

                QQC2.Button {
                    text: root.task.noteCount > 0
                        ? i18ncp("@action:button", "%1 Comment", "%1 Comments", root.task.noteCount)
                        : i18nc("@action:button", "Comments")
                    icon.name: "comment"
                    enabled: root.task.canComment ?? true
                    onClicked: applicationWindow().openComments(root.taskId,
                                                                root.task.canComment ?? true)
                }

                Item {
                    Layout.fillWidth: true
                }

                QQC2.Button {
                    text: i18nc("@action:button", "Delete")
                    icon.name: "edit-delete"
                    enabled: !root.readOnly
                    onClicked: deleteConfirm.open()
                }
            }
        }
    }

    // -- Sub-dialogs --------------------------------------------------------

    DueDatePicker {
        id: duePicker
        onPicked: dueString => {
            App.setTaskDue(root.taskId, dueString);
            root.reload();
        }
    }

    RecurrencePicker {
        id: recurrencePicker

        currentRule: root.task.isRecurring ? (root.task.dueString ?? "") : ""
        baseDate: (root.task.hasDue && root.task.dueDate) ? root.task.dueDate : new Date()
        // Switching repeating off keeps the next occurrence as a plain date.
        plainDate: (root.task.hasDue && root.task.dueDate)
            ? Qt.formatDate(root.task.dueDate, "yyyy-MM-dd")
            : ""

        onPicked: dueString => {
            App.setTaskDue(root.taskId, dueString);
            root.reload();
        }
    }

    LabelPicker {
        id: labelPicker
        selected: root.task.labels ?? []
        onAccepted: labels => {
            App.updateTask(root.taskId, { labels: labels });
            root.reload();
        }
    }

    AssigneePicker {
        id: assigneePicker
        projectId: root.task.projectId ?? ""
        currentAssignee: root.task.assigneeId ?? ""
        onPicked: userId => {
            App.setTaskAssignee(root.taskId, userId);
            root.reload();
        }
    }

    ProjectPicker {
        id: projectPicker
        currentProject: root.task.projectId ?? ""
        onPicked: (projectId, sectionId) => {
            App.moveTask(root.taskId, projectId, sectionId);
            projectPicker.close();
            root.reload();
        }
    }

    Kirigami.PromptDialog {
        id: subtaskDeletePrompt

        property string subtaskId: ""
        property string subtaskName: ""

        title: i18nc("@title:window", "Delete Sub-task")
        subtitle: i18n("“%1” will be deleted. This cannot be undone.", subtaskName)
        standardButtons: Kirigami.Dialog.Cancel

        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    App.deleteTask(subtaskDeletePrompt.subtaskId);
                    subtaskDeletePrompt.close();
                    root.reload();
                }
            }
        ]
    }

    Kirigami.PromptDialog {
        id: deleteConfirm

        title: i18nc("@title:window", "Delete Task")
        subtitle: i18n("“%1” will be deleted. This cannot be undone.", root.task.content ?? "")
        standardButtons: Kirigami.Dialog.Cancel

        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    App.deleteTask(root.taskId);
                    deleteConfirm.close();
                    root.close();
                }
            }
        ]
    }
}
