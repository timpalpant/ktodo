import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.dateandtime as DateTime

/**
 * Compact week navigator for Upcoming's agenda.
 *
 * The week stays pinned while the agenda moves. The month button opens the
 * full calendar for longer jumps, while the seven day strip keeps nearby
 * planning to one click.
 */
Item {
    id: root

    property var agendaModel
    property date selectedDate: new Date()
    property int modelRevision: 0
    readonly property real contentImplicitHeight: calendarColumn.implicitHeight
    readonly property date weekStart: startOfWeek(selectedDate)

    signal dateSelected(date date)

    implicitHeight: contentImplicitHeight
    z: 20

    function cleanDate(date) {
        return new Date(date.getFullYear(), date.getMonth(), date.getDate());
    }

    function startOfWeek(date) {
        const result = cleanDate(date);
        // Monday-first follows the locale used by Todoist's week planner.
        result.setDate(result.getDate() - ((result.getDay() + 6) % 7));
        return result;
    }

    function dateAt(offset) {
        const result = cleanDate(weekStart);
        result.setDate(result.getDate() + offset);
        return result;
    }

    function sameDay(left, right) {
        return left.getFullYear() === right.getFullYear()
            && left.getMonth() === right.getMonth()
            && left.getDate() === right.getDate();
    }

    function select(date) {
        const value = cleanDate(date);
        selectedDate = value;
        dateSelected(value);
    }

    function moveWeek(days) {
        const value = cleanDate(selectedDate);
        value.setDate(value.getDate() + days);
        select(value);
    }

    Connections {
        target: root.agendaModel
        function onModelReset() { root.modelRevision += 1; }
    }

    Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor

        Kirigami.Separator {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
        }
    }

    ColumnLayout {
        id: calendarColumn

        width: parent.width
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            Layout.topMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            QQC2.ToolButton {
                text: Qt.formatDate(root.selectedDate, "MMMM yyyy")
                icon.name: "view-calendar"
                display: QQC2.AbstractButton.TextBesideIcon
                onClicked: datePopup.open()

                QQC2.ToolTip.text: i18nc("@info:tooltip", "Choose a date")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            }

            Item { Layout.fillWidth: true }

            QQC2.Button {
                text: i18nc("@action:button", "Today")
                flat: true
                onClicked: root.select(new Date())
            }

            QQC2.ToolButton {
                text: i18nc("@action:button", "Previous week")
                icon.name: "go-previous"
                display: QQC2.AbstractButton.IconOnly
                onClicked: root.moveWeek(-7)

                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            }

            QQC2.ToolButton {
                text: i18nc("@action:button", "Next week")
                icon.name: "go-next"
                display: QQC2.AbstractButton.IconOnly
                onClicked: root.moveWeek(7)

                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                model: 7

                delegate: QQC2.AbstractButton {
                    id: dayButton

                    required property int index
                    readonly property date dayDate: root.dateAt(index)
                    readonly property bool selected: root.sameDay(dayDate, root.selectedDate)
                    readonly property bool today: root.sameDay(dayDate, new Date())
                    readonly property int dueCount: {
                        const revision = root.modelRevision;
                        return root.agendaModel
                            ? root.agendaModel.taskCountForDate(dayDate) : 0;
                    }

                    text: Qt.formatDate(dayDate, "dddd d MMMM")
                    Accessible.name: dueCount > 0
                        ? i18ncp("@info:accessible", "%1, %2 task", "%1, %2 tasks",
                                 dueCount, Qt.formatDate(dayDate, "dddd d MMMM"), dueCount)
                        : Qt.formatDate(dayDate, "dddd d MMMM")
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: Kirigami.Units.gridUnit * 3.4
                    hoverEnabled: true
                    onClicked: root.select(dayDate)

                    background: Rectangle {
                        radius: Kirigami.Units.cornerRadius
                        color: dayButton.selected
                            ? Kirigami.Theme.highlightColor
                            : (dayButton.hovered
                                ? Kirigami.Theme.alternateBackgroundColor : "transparent")
                    }

                    contentItem: ColumnLayout {
                        spacing: 1

                        QQC2.Label {
                            text: Qt.formatDate(dayButton.dayDate, "ddd")
                            horizontalAlignment: Text.AlignHCenter
                            color: dayButton.selected
                                ? Kirigami.Theme.highlightedTextColor
                                : Kirigami.Theme.textColor
                            opacity: dayButton.selected ? 1 : 0.65
                            font: Kirigami.Theme.smallFont
                            Layout.fillWidth: true
                        }

                        QQC2.Label {
                            text: dayButton.dayDate.getDate()
                            horizontalAlignment: Text.AlignHCenter
                            color: dayButton.selected
                                ? Kirigami.Theme.highlightedTextColor
                                : (dayButton.today
                                    ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor)
                            font.bold: dayButton.selected || dayButton.today
                            Layout.fillWidth: true
                        }

                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            implicitWidth: dayButton.dueCount > 0
                                ? Kirigami.Units.smallSpacing : 0
                            implicitHeight: implicitWidth
                            radius: width / 2
                            color: dayButton.selected
                                ? Kirigami.Theme.highlightedTextColor
                                : Kirigami.Theme.highlightColor
                            visible: dayButton.dueCount > 0
                        }
                    }
                }
            }
        }
    }

    DateTime.DatePopup {
        id: datePopup

        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent
        value: root.selectedDate
        onAccepted: root.select(value)
    }
}
