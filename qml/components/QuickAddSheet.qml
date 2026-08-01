import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import io.github.timpalpant.ktodo

/**
 * Fast task entry.
 *
 * The text field understands Todoist's inline syntax the same way the other
 * clients do: #project, @label, p1..p4 and a trailing date phrase are pulled
 * out locally so the pickers reflect what was typed, and the remaining date
 * text is handed to the server to parse.
 */
Kirigami.Dialog {
    id: root

    property string projectId: ""
    property string sectionId: ""
    property string dueString: ""
    property int priority: 4
    property var labels: []
    property string assigneeId: ""

    title: i18nc("@title:window", "Add Task")

    preferredWidth: Kirigami.Units.gridUnit * 32
    standardButtons: Kirigami.Dialog.NoButton

    // Kirigami.Dialog is edge-to-edge by default; free-standing controls need
    // breathing room from the frame.
    leftPadding: Kirigami.Units.largeSpacing * 2
    rightPadding: Kirigami.Units.largeSpacing * 2
    topPadding: Kirigami.Units.largeSpacing
    bottomPadding: Kirigami.Units.largeSpacing * 2


    function openFor(project) {
        projectId = project ?? "";
        sectionId = "";
        dueString = "";
        priority = 4;
        labels = [];
        assigneeId = "";
        contentField.text = "";
        descriptionField.text = "";
        open();
        contentField.forceActiveFocus();
    }

    /**
     * Strips inline tokens from the typed text.
     *
     * Only tokens we can resolve locally are consumed; anything else is left
     * in the content so the user never silently loses what they typed.
     */
    function parseTokens(text) {
        let content = text;
        const foundLabels = [];
        let foundPriority = 0;
        let foundProject = "";

        content = content.replace(/(^|\s)@([^\s]+)/g, (match, lead, name) => {
            foundLabels.push(name);
            return lead;
        });

        content = content.replace(/(^|\s)p([1-4])(?=\s|$)/gi, (match, lead, level) => {
            foundPriority = parseInt(level, 10);
            return lead;
        });

        content = content.replace(/(^|\s)#([^\s]+)/g, (match, lead, name) => {
            foundProject = name;
            return lead;
        });

        return {
            content: content.replace(/\s+/g, " ").trim(),
            labels: foundLabels,
            priority: foundPriority,
            project: foundProject
        };
    }

    function submit() {
        const parsed = parseTokens(contentField.text);
        if (parsed.content === "") {
            return;
        }

        const allLabels = root.labels.concat(
            parsed.labels.filter(l => root.labels.indexOf(l) < 0));

        App.addTask(parsed.content,
                    root.projectId,
                    root.sectionId,
                    "",
                    root.dueString,
                    5 - (parsed.priority > 0 ? parsed.priority : root.priority),
                    allLabels,
                    descriptionField.text,
                    root.assigneeId);

        close();
    }

    ProjectsModel {
        id: projectsModel
    }

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC2.TextField {
            id: contentField

            placeholderText: i18nc("@info:placeholder", "Task name — try #project, @label, p1")
            Layout.fillWidth: true
            onAccepted: root.submit()
        }

        QQC2.TextArea {
            id: descriptionField

            placeholderText: i18nc("@info:placeholder", "Description (optional)")
            wrapMode: TextEdit.Wrap
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 3
        }

        // Chips reflect the current selection and open the matching picker.
        Flow {
            spacing: Kirigami.Units.smallSpacing
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing

            QQC2.Button {
                text: root.dueString !== "" ? root.dueString : i18nc("@action:button", "Schedule")
                icon.name: "view-calendar-day"
                onClicked: duePicker.open()
            }

            QQC2.Button {
                text: root.priority < 4
                    ? i18nc("@action:button priority level", "P%1", root.priority)
                    : i18nc("@action:button", "Priority")
                icon.name: "flag"
                onClicked: priorityMenu.popup()

                QQC2.Menu {
                    id: priorityMenu

                    Repeater {
                        model: [1, 2, 3, 4]
                        delegate: QQC2.MenuItem {
                            required property int modelData
                            text: modelData === 4
                                ? i18nc("@item priority", "No priority")
                                : i18nc("@item priority", "Priority %1", modelData)
                            onTriggered: root.priority = modelData
                        }
                    }
                }
            }

            QQC2.Button {
                text: root.labels.length > 0 ? root.labels.join(", ") : i18nc("@action:button", "Labels")
                icon.name: "tag"
                onClicked: labelPicker.open()
            }

            QQC2.Button {
                text: {
                    if (root.projectId === "") {
                        return i18nc("@action:button", "Inbox");
                    }
                    const details = App.projectDetails(root.projectId);
                    return details.name ?? i18nc("@action:button", "Project");
                }
                icon.name: "folder"
                onClicked: projectPicker.open()
            }

            QQC2.Button {
                text: root.assigneeId !== ""
                    ? i18nc("@action:button", "Assigned")
                    : i18nc("@action:button", "Assign")
                icon.name: "user-identity"
                visible: {
                    if (root.projectId === "") {
                        return false;
                    }
                    const details = App.projectDetails(root.projectId);
                    return (details.isShared ?? false) && (details.canAssign ?? false);
                }
                onClicked: assigneePicker.open()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing

            Item {
                Layout.fillWidth: true
            }
            QQC2.Button {
                text: i18nc("@action:button", "Cancel")
                icon.name: "dialog-cancel"
                onClicked: root.close()
            }
            QQC2.Button {
                text: i18nc("@action:button", "Add Task")
                icon.name: "list-add"
                enabled: contentField.text.trim() !== ""
                onClicked: root.submit()
            }
        }
    }

    DueDatePicker {
        id: duePicker
        onPicked: value => root.dueString = value
    }

    LabelPicker {
        id: labelPicker
        selected: root.labels
        onAccepted: value => root.labels = value
    }

    ProjectPicker {
        id: projectPicker
        currentProject: root.projectId
        onPicked: (project, section) => {
            root.projectId = project;
            root.sectionId = section;
            projectPicker.close();
        }
    }

    AssigneePicker {
        id: assigneePicker
        projectId: root.projectId
        currentAssignee: root.assigneeId
        onPicked: userId => root.assigneeId = userId
    }
}
