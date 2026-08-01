import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import org.kde.kirigamiaddons.delegates as Delegates

import io.github.timpalpant.ktodo

/**
 * Picks who a task belongs to.
 *
 * Only collaborators on the task's own project are listed, which is what the
 * API accepts — assigning to someone outside the project is rejected.
 */
QQC2.Popup {
    id: root

    property string projectId: ""
    property string currentAssignee: ""

    signal picked(string userId)

    modal: true
    focus: true
    padding: Kirigami.Units.smallSpacing
    anchors.centerIn: QQC2.Overlay.overlay

    background: Kirigami.ShadowedRectangle {
        color: Kirigami.Theme.backgroundColor
        radius: Kirigami.Units.cornerRadius
        shadow.size: Kirigami.Units.gridUnit
        shadow.color: Qt.rgba(0, 0, 0, 0.3)
        border.width: 1
        border.color: Kirigami.ColorUtils.linearInterpolation(
            Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.15)
    }

    CollaboratorsModel {
        id: collaborators
        projectId: root.projectId
    }

    function choose(userId) {
        root.picked(userId);
        root.close();
    }

    contentItem: ColumnLayout {
        spacing: 0

        Kirigami.Heading {
            text: i18nc("@title", "Assign to")
            level: 4
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
        }

        Delegates.RoundedItemDelegate {
            text: i18n("Nobody")
            icon.name: "edit-clear"
            Layout.fillWidth: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 16
            onClicked: root.choose("")
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            // Sized from the rows themselves rather than a guessed row height,
            // so a short list never needs scrolling to see everyone.
            Layout.preferredHeight: Math.min(collaboratorList.contentHeight,
                                             Kirigami.Units.gridUnit * 20)
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            ListView {
                id: collaboratorList

                clip: true
                model: collaborators

                delegate: Delegates.RoundedItemDelegate {
                    id: collaboratorDelegate

                    required property string userId
                    required property string name
                    required property string email
                    required property string avatar
                    required property bool isMe

                    width: ListView.view.width
                    onClicked: root.choose(userId)

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Components.Avatar {
                            name: collaboratorDelegate.name
                            source: collaboratorDelegate.avatar
                            implicitWidth: Kirigami.Units.iconSizes.medium
                            implicitHeight: Kirigami.Units.iconSizes.medium
                        }

                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true

                            QQC2.Label {
                                text: collaboratorDelegate.isMe
                                    ? i18nc("the signed-in user", "%1 (me)", collaboratorDelegate.name)
                                    : collaboratorDelegate.name
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            QQC2.Label {
                                text: collaboratorDelegate.email
                                elide: Text.ElideRight
                                opacity: 0.6
                                font: Kirigami.Theme.smallFont
                                Layout.fillWidth: true
                            }
                        }

                        Kirigami.Icon {
                            source: "checkmark"
                            visible: collaboratorDelegate.userId === root.currentAssignee
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                        }
                    }
                }
            }
        }

        Kirigami.PlaceholderMessage {
            text: i18n("This project has no other members")
            visible: collaborators.count === 0
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
        }
    }
}
