import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.delegates as Delegates

import io.github.timpalpant.ktodo

/// Chooses the project (and optionally the section) a task belongs to.
QQC2.Popup {
    id: root

    property string currentProject: ""
    property bool showSections: true

    signal picked(string projectId, string sectionId)

    modal: true
    focus: true
    padding: Kirigami.Units.smallSpacing
    anchors.centerIn: QQC2.Overlay.overlay

    onOpened: filterField.forceActiveFocus()

    background: Kirigami.ShadowedRectangle {
        color: Kirigami.Theme.backgroundColor
        radius: Kirigami.Units.cornerRadius
        shadow.size: Kirigami.Units.gridUnit
        shadow.color: Qt.rgba(0, 0, 0, 0.3)
        border.width: 1
        border.color: Kirigami.ColorUtils.linearInterpolation(
            Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.15)
    }

    ProjectsModel {
        id: projectsModel
    }

    SectionsModel {
        id: sectionsModel
        projectId: root.currentProject
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC2.TextField {
            id: filterField
            placeholderText: i18nc("@info:placeholder", "Filter projects")
            Layout.fillWidth: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 18
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 14
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            ListView {
                clip: true
                model: projectsModel

                delegate: Delegates.RoundedItemDelegate {
                    id: projectDelegate

                    required property string projectId
                    required property string name
                    required property color color
                    required property int depth
                    required property bool isReadOnly
                    required property bool isTeam

                    width: ListView.view.width
                    // Tasks cannot be created in a project the user can only read.
                    enabled: !isReadOnly
                    visible: filterField.text === ""
                        || name.toLowerCase().includes(filterField.text.toLowerCase())
                    height: visible ? implicitHeight : 0

                    onClicked: root.picked(projectId, "")

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Item {
                            Layout.preferredWidth: projectDelegate.depth * Kirigami.Units.largeSpacing
                            visible: projectDelegate.depth > 0
                        }

                        Rectangle {
                            implicitWidth: Math.round(Kirigami.Units.gridUnit * 0.6)
                            implicitHeight: implicitWidth
                            radius: width / 2
                            color: projectDelegate.color
                        }

                        QQC2.Label {
                            text: projectDelegate.name
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Kirigami.Icon {
                            source: "group"
                            visible: projectDelegate.isTeam
                            opacity: 0.6
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                        }

                        Kirigami.Icon {
                            source: "checkmark"
                            visible: projectDelegate.projectId === root.currentProject
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                        }
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: root.showSections && sectionsModel.count > 0
        }

        QQC2.Label {
            text: i18nc("@title:group", "Sections")
            visible: root.showSections && sectionsModel.count > 0
            font: Kirigami.Theme.smallFont
            opacity: 0.7
            Layout.leftMargin: Kirigami.Units.smallSpacing
        }

        Repeater {
            model: root.showSections ? sectionsModel : null

            delegate: Delegates.RoundedItemDelegate {
                required property string sectionId
                required property string name

                text: name
                icon.name: "view-list-details"
                Layout.fillWidth: true
                onClicked: root.picked(root.currentProject, sectionId)
            }
        }
    }
}
