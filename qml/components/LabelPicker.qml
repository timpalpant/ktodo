import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import io.github.timpalpant.ktodo

/// Multi-select over existing labels, with inline creation of new ones.
QQC2.Popup {
    id: root

    property var selected: []

    signal accepted(var labels)

    modal: true
    focus: true
    padding: Kirigami.Units.smallSpacing
    anchors.centerIn: QQC2.Overlay.overlay

    onOpened: {
        working = selected.slice();
        filterField.text = "";
        filterField.forceActiveFocus();
    }

    // Edits are staged so dismissing the popup discards them.
    property var working: []

    function toggle(name) {
        const index = working.indexOf(name);
        const next = working.slice();
        if (index >= 0) {
            next.splice(index, 1);
        } else {
            next.push(name);
        }
        working = next;
    }

    background: Kirigami.ShadowedRectangle {
        color: Kirigami.Theme.backgroundColor
        radius: Kirigami.Units.cornerRadius
        shadow.size: Kirigami.Units.gridUnit
        shadow.color: Qt.rgba(0, 0, 0, 0.3)
        border.width: 1
        border.color: Kirigami.ColorUtils.linearInterpolation(
            Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.15)
    }

    LabelsModel {
        id: labelsModel
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC2.TextField {
            id: filterField
            placeholderText: i18nc("@info:placeholder", "Filter or create a label")
            Layout.fillWidth: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 18

            onAccepted: {
                const name = text.trim();
                if (name === "") {
                    return;
                }
                // Creating and selecting in one step keeps the flow going.
                if (labelsModel.allNames().indexOf(name) < 0) {
                    App.addLabel(name);
                }
                root.toggle(name);
                text = "";
            }
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 12
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            ListView {
                clip: true
                model: labelsModel

                delegate: QQC2.CheckDelegate {
                    id: labelDelegate

                    required property string name
                    required property color color
                    required property int taskCount

                    width: ListView.view.width
                    visible: filterField.text === ""
                        || name.toLowerCase().includes(filterField.text.toLowerCase())
                    height: visible ? implicitHeight : 0

                    checked: root.working.indexOf(name) >= 0
                    onToggled: root.toggle(name)

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: "tag"
                            color: labelDelegate.color
                            isMask: true
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                        }
                        QQC2.Label {
                            text: labelDelegate.name
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        QQC2.Label {
                            text: labelDelegate.taskCount
                            visible: labelDelegate.taskCount > 0
                            opacity: 0.6
                            font: Kirigami.Theme.smallFont
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Item {
                Layout.fillWidth: true
            }
            QQC2.Button {
                text: i18nc("@action:button", "Cancel")
                icon.name: "dialog-cancel"
                onClicked: root.close()
            }
            QQC2.Button {
                text: i18nc("@action:button", "Apply")
                icon.name: "dialog-ok-apply"
                onClicked: {
                    root.accepted(root.working);
                    root.close();
                }
            }
        }
    }
}
