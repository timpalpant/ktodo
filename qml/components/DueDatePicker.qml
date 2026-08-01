import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.dateandtime as DateTime
import org.kde.kirigamiaddons.delegates as Delegates

/**
 * Scheduling popup.
 *
 * Alongside the usual shortcuts it accepts free text, because Todoist parses
 * natural language server-side ("every mon", "next fri 9am") far better than
 * a client could — that string is sent through untouched.
 */
QQC2.Popup {
    id: root

    /// Emitted with a due string Todoist will parse, or "" to clear the date.
    signal picked(string dueString)

    modal: true
    focus: true
    padding: Kirigami.Units.smallSpacing

    anchors.centerIn: QQC2.Overlay.overlay

    onOpened: freeText.forceActiveFocus()
    onClosed: freeText.text = ""

    /// Carried between the date and time steps of the combined picker.
    property date pendingDate: new Date()

    background: Kirigami.ShadowedRectangle {
        color: Kirigami.Theme.backgroundColor
        radius: Kirigami.Units.cornerRadius
        shadow.size: Kirigami.Units.gridUnit
        shadow.color: Qt.rgba(0, 0, 0, 0.3)
        border.width: 1
        border.color: Kirigami.ColorUtils.linearInterpolation(
            Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.15)
    }

    function choose(value) {
        root.picked(value);
        root.close();
    }

    contentItem: ColumnLayout {
        spacing: 0

        QQC2.TextField {
            id: freeText

            placeholderText: i18nc("@info:placeholder", "e.g. tomorrow at 9am, every Monday")
            Layout.fillWidth: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 20
            Layout.margins: Kirigami.Units.smallSpacing

            onAccepted: {
                if (text.trim() !== "") {
                    root.choose(text.trim());
                }
            }
        }

        QQC2.Label {
            text: i18n("Times work too — type them, or use “Pick date and time”.")
            font: Kirigami.Theme.smallFont
            opacity: 0.6
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        Delegates.RoundedItemDelegate {
            text: i18n("Today")
            icon.name: "view-calendar-day"
            Layout.fillWidth: true
            onClicked: root.choose("today")
        }
        Delegates.RoundedItemDelegate {
            text: i18n("This Evening")
            icon.name: "view-calendar-day"
            Layout.fillWidth: true
            onClicked: root.choose("today at 6pm")
        }
        Delegates.RoundedItemDelegate {
            text: i18n("Tomorrow")
            icon.name: "view-calendar-upcoming-days"
            Layout.fillWidth: true
            onClicked: root.choose("tomorrow")
        }
        Delegates.RoundedItemDelegate {
            text: i18n("Next Weekend")
            icon.name: "view-calendar-workweek"
            Layout.fillWidth: true
            onClicked: root.choose("saturday")
        }
        Delegates.RoundedItemDelegate {
            text: i18n("Next Week")
            icon.name: "view-calendar-week"
            Layout.fillWidth: true
            onClicked: root.choose("next week")
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        Delegates.RoundedItemDelegate {
            text: i18n("Pick a Date…")
            icon.name: "view-calendar"
            Layout.fillWidth: true
            onClicked: {
                // Dismiss first: leaving this popup up would cover the
                // calendar and trap the modal grab.
                root.close();
                dateOnly.open();
            }
        }
        Delegates.RoundedItemDelegate {
            text: i18n("Pick Date and Time…")
            icon.name: "clock"
            Layout.fillWidth: true
            onClicked: {
                root.close();
                dateThenTime.open();
            }
        }
        Delegates.RoundedItemDelegate {
            text: i18n("No Date")
            icon.name: "edit-clear"
            Layout.fillWidth: true
            onClicked: root.choose("")
        }
    }

    // Both calendars are parented to the window overlay rather than to this
    // popup, so dismissing the popup does not take them down with it.

    DateTime.DatePopup {
        id: dateOnly

        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent

        onAccepted: {
            // An ISO date is unambiguous for the server's parser.
            root.picked(Qt.formatDate(value, "yyyy-MM-dd"));
        }
    }

    DateTime.DatePopup {
        id: dateThenTime

        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent

        onAccepted: {
            root.pendingDate = value;
            timePicker.open();
        }
    }

    DateTime.TimePopup {
        id: timePicker

        parent: QQC2.Overlay.overlay
        anchors.centerIn: parent

        onAccepted: {
            const day = Qt.formatDate(root.pendingDate, "yyyy-MM-dd");
            const time = Qt.formatTime(value, "HH:mm");
            root.picked(day + " " + time);
        }
    }
}
