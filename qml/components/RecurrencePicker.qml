import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.delegates as Delegates

/**
 * Chooses how a task repeats.
 *
 * Todoist has no separate recurrence field: the schedule lives inside the due
 * *string*, which is why repeating tasks are otherwise invisible in an editor
 * that only shows a date. Each option here is a phrase the server parses.
 *
 * The phrases are deliberately English regardless of the interface language,
 * because they are sent with the due date's `lang` of "en".
 */
QQC2.Popup {
    id: root

    /// Anchor for "weekly on …" / "monthly on …"; the task's due date if it
    /// has one, otherwise today.
    property date baseDate: new Date()
    /// Current recurrence phrase, empty when the task does not repeat.
    property string currentRule: ""
    /// Due date to fall back to when repeating is switched off.
    property string plainDate: ""

    signal picked(string dueString)

    modal: true
    focus: true
    padding: Kirigami.Units.smallSpacing
    anchors.centerIn: QQC2.Overlay.overlay

    onOpened: customField.text = ""

    background: Kirigami.ShadowedRectangle {
        color: Kirigami.Theme.backgroundColor
        radius: Kirigami.Units.cornerRadius
        shadow.size: Kirigami.Units.gridUnit
        shadow.color: Qt.rgba(0, 0, 0, 0.3)
        border.width: 1
        border.color: Kirigami.ColorUtils.linearInterpolation(
            Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.15)
    }

    readonly property var _weekdays: ["sunday", "monday", "tuesday", "wednesday",
                                      "thursday", "friday", "saturday"]

    function choose(rule) {
        root.picked(rule);
        root.close();
    }

    contentItem: ColumnLayout {
        spacing: 0

        Kirigami.Heading {
            text: i18nc("@title", "Repeat")
            level: 4
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
        }

        QQC2.Label {
            text: root.currentRule !== ""
                ? i18n("Currently: %1", root.currentRule)
                : i18n("This task does not repeat.")
            font: Kirigami.Theme.smallFont
            opacity: 0.7
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        Delegates.RoundedItemDelegate {
            text: i18n("Every day")
            icon.name: "view-refresh"
            Layout.fillWidth: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 20
            onClicked: root.choose("every day")
        }
        Delegates.RoundedItemDelegate {
            text: i18n("Every weekday (Mon–Fri)")
            icon.name: "view-calendar-workweek"
            Layout.fillWidth: true
            onClicked: root.choose("every weekday")
        }
        Delegates.RoundedItemDelegate {
            text: i18n("Every week on %1",
                       Qt.formatDate(root.baseDate, "dddd"))
            icon.name: "view-calendar-week"
            Layout.fillWidth: true
            onClicked: root.choose("every " + root._weekdays[root.baseDate.getDay()])
        }
        Delegates.RoundedItemDelegate {
            text: i18n("Every two weeks")
            icon.name: "view-calendar-week"
            Layout.fillWidth: true
            onClicked: root.choose("every 2 weeks")
        }
        Delegates.RoundedItemDelegate {
            text: i18n("Every month on the %1", root.baseDate.getDate())
            icon.name: "view-calendar-month"
            Layout.fillWidth: true
            onClicked: root.choose("every month")
        }
        Delegates.RoundedItemDelegate {
            text: i18n("Every year on %1", Qt.formatDate(root.baseDate, "d MMMM"))
            icon.name: "view-calendar-year"
            Layout.fillWidth: true
            onClicked: root.choose("every year")
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        QQC2.TextField {
            id: customField

            placeholderText: i18nc("@info:placeholder", "e.g. every 3 days, every Mon, Thu")
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing

            onAccepted: {
                if (text.trim() !== "") {
                    root.choose(text.trim());
                }
            }
        }

        Delegates.RoundedItemDelegate {
            text: i18n("Do not repeat")
            icon.name: "edit-clear"
            enabled: root.currentRule !== ""
            Layout.fillWidth: true
            // Drops the recurrence but keeps the next occurrence's date.
            onClicked: root.choose(root.plainDate)
        }
    }
}
