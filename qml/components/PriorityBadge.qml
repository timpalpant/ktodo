import QtQuick

import org.kde.kirigami as Kirigami

/**
 * The task checkbox, tinted by priority.
 *
 * Todoist encodes priority in the checkbox rather than in a separate badge,
 * so this doubles as the completion control.
 */
Item {
    id: root

    /// API priority: 1 = none, 4 = urgent.
    property int priority: 1
    property bool checked: false
    property color accent: Kirigami.Theme.textColor

    signal toggled

    implicitWidth: Kirigami.Units.iconSizes.smallMedium
    implicitHeight: Kirigami.Units.iconSizes.smallMedium

    readonly property color effectiveColor: priority > 1
        ? accent
        : Kirigami.Theme.disabledTextColor

    scale: hover.hovered ? 1.1 : 1.0

    Behavior on scale {
        NumberAnimation {
            duration: Kirigami.Units.shortDuration
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.round(Kirigami.Units.iconSizes.small * 1.1)
        height: width
        radius: width / 2

        color: root.checked ? root.effectiveColor : "transparent"
        border.width: 1
        border.color: root.effectiveColor

        // A faint wash keeps higher priorities legible at a glance without
        // shouting the way a solid fill would.
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            visible: !root.checked && root.priority > 1
            color: root.effectiveColor
            opacity: 0.15
        }

        Kirigami.Icon {
            anchors.centerIn: parent
            width: Math.round(parent.width * 0.7)
            height: width
            source: "checkmark"
            isMask: true
            color: root.checked ? Kirigami.Theme.backgroundColor : root.effectiveColor
            visible: root.checked || hover.hovered
            opacity: root.checked ? 1.0 : 0.5
        }
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: root.toggled()
    }
}
