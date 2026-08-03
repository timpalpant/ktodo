import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard

import io.github.timpalpant.ktodo

/// Creates a new project, or edits an existing one when projectId is set.
FormCard.FormCardPage {
    id: root

    property string projectId: ""

    readonly property bool isNew: projectId === ""
    readonly property var project: isNew ? ({}) : App.projectDetails(projectId)

    title: isNew ? i18nc("@title:window", "New Project") : i18nc("@title:window", "Edit Project")

    property string chosenColor: "charcoal"

    Component.onCompleted: {
        if (!isNew) {
            nameField.text = project.name ?? "";
            chosenColor = project.color ?? "charcoal";
            favoriteToggle.checked = project.isFavorite ?? false;
        }
    }

    FormCard.FormCard {
        Layout.topMargin: Kirigami.Units.largeSpacing

        FormCard.FormTextFieldDelegate {
            id: nameField
            label: i18nc("@label:textbox", "Name")
            onAccepted: root.save()
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormCheckDelegate {
            id: favoriteToggle
            text: i18nc("@option:check", "Show in Favorites")
        }
    }

    FormCard.FormHeader {
        title: i18nc("@title:group", "Color")
    }

    FormCard.FormCard {
        FormCard.AbstractFormDelegate {
            background: null
            Layout.fillWidth: true

            contentItem: Flow {
                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: App.colorNames()

                    delegate: QQC2.AbstractButton {
                        id: swatch

                        required property string modelData

                        implicitWidth: Kirigami.Units.gridUnit * 1.6
                        implicitHeight: Kirigami.Units.gridUnit * 1.6

                        onClicked: root.chosenColor = modelData

                        // Todoist's API spells this color identifier "grey";
                        // keep the user-facing label in American English.
                        QQC2.ToolTip.text: modelData === "grey"
                            ? i18nc("@info:tooltip", "Gray")
                            : modelData
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                        contentItem: Rectangle {
                            radius: width / 2
                            color: App.colorFor(swatch.modelData)

                            border.width: root.chosenColor === swatch.modelData ? 3 : 0
                            border.color: Kirigami.Theme.highlightColor
                        }
                    }
                }
            }
        }
    }

    FormCard.FormCard {
        Layout.topMargin: Kirigami.Units.largeSpacing

        FormCard.FormButtonDelegate {
            text: root.isNew ? i18nc("@action:button", "Create Project")
                             : i18nc("@action:button", "Save Changes")
            icon.name: root.isNew ? "list-add" : "document-save"
            enabled: nameField.text.trim() !== ""
            onClicked: root.save()
        }

        FormCard.FormDelegateSeparator {
            visible: !root.isNew
        }

        FormCard.FormButtonDelegate {
            text: i18nc("@action:button", "Archive Project")
            description: i18n("Hide it without deleting its tasks")
            icon.name: "archive-insert"
            visible: !root.isNew
            onClicked: {
                App.archiveProject(root.projectId);
                applicationWindow().showToday();
            }
        }

        FormCard.FormDelegateSeparator {
            visible: !root.isNew
        }

        FormCard.FormButtonDelegate {
            text: i18nc("@action:button", "Delete Project")
            description: i18n("Deletes the project and all of its tasks")
            icon.name: "edit-delete"
            visible: !root.isNew
            onClicked: deletePrompt.open()
        }
    }

    function save() {
        const name = nameField.text.trim();
        if (name === "") {
            return;
        }

        if (isNew) {
            App.addProject(name, chosenColor, "", favoriteToggle.checked);
        } else {
            App.renameProject(projectId, name);
            App.setProjectColor(projectId, chosenColor);
            App.setProjectFavorite(projectId, favoriteToggle.checked);
        }
        applicationWindow().showToday();
    }

    Kirigami.PromptDialog {
        id: deletePrompt

        title: i18nc("@title:window", "Delete Project")
        subtitle: i18n("“%1” and all of its tasks will be deleted. This cannot be undone.",
                       root.project.name ?? "")
        standardButtons: Kirigami.Dialog.Cancel

        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    App.deleteProject(root.projectId);
                    deletePrompt.close();
                    applicationWindow().showToday();
                }
            }
        ]
    }
}
