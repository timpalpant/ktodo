import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

/// Small date label used on task rows, coloured by urgency.
RowLayout {
    id: root

    property string text: ""
    property bool overdue: false
    property bool today: false
    property bool recurring: false
    property string iconName: "view-calendar-day"

    spacing: Kirigami.Units.smallSpacing * 0.5
    visible: text !== ""

    readonly property color tint: overdue
        ? Kirigami.Theme.negativeTextColor
        : (today ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor)

    Kirigami.Icon {
        source: root.recurring ? "view-refresh" : root.iconName
        color: root.tint
        isMask: true
        implicitWidth: Kirigami.Units.iconSizes.small
        implicitHeight: Kirigami.Units.iconSizes.small
    }

    QQC2.Label {
        text: root.text
        color: root.tint
        font: Kirigami.Theme.smallFont
        elide: Text.ElideRight
        maximumLineCount: 1
    }
}
