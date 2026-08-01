import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

/// Four-way priority selector using Todoist's UI numbering (P1 = urgent).
RowLayout {
    id: root

    /// 1..4, where 1 is the most urgent.
    property int value: 4

    signal picked(int priority)

    spacing: Kirigami.Units.largeSpacing

    Repeater {
        model: [
            { level: 1, color: "#d1453b" },
            { level: 2, color: "#eb8909" },
            { level: 3, color: "#246fe0" },
            { level: 4, color: "" }
        ]

        // Selection is drawn here rather than left to the style: `checkable`
        // lets QQC2 assign `checked` imperatively and break the binding to
        // `value`, and `highlighted` is not rendered by every style.
        delegate: QQC2.AbstractButton {
            id: flagButton

            required property var modelData

            readonly property bool selected: root.value === modelData.level
            readonly property color flagColor: modelData.color === ""
                ? Kirigami.Theme.disabledTextColor
                : modelData.color

            // Square: the level is carried by colour and tooltip.
            implicitWidth: Kirigami.Units.gridUnit * 1.8
            implicitHeight: Kirigami.Units.gridUnit * 1.8

            onClicked: root.picked(modelData.level)

            background: Rectangle {
                radius: Kirigami.Units.cornerRadius
                color: {
                    if (flagButton.selected) {
                        return Qt.alpha(flagButton.flagColor, 0.2);
                    }
                    return flagButton.hovered ? Kirigami.Theme.hoverColor : "transparent";
                }
                border.width: flagButton.selected ? 1 : 0
                border.color: flagButton.flagColor
            }

            contentItem: Kirigami.Icon {
                source: "flag"
                color: flagButton.flagColor
                isMask: true
                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
            }

            // The only place the level is spelled out.
            QQC2.ToolTip.text: modelData.level === 4
                ? i18nc("@info:tooltip", "No priority (P4)")
                : i18nc("@info:tooltip", "Priority %1", modelData.level)
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }
    }
}
