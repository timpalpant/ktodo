import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import io.github.timpalpant.ktodo

/// Sign-in screen shown until a usable access token exists.
Kirigami.Page {
    id: root

    title: i18nc("@title:window", "Sign in")

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - Kirigami.Units.gridUnit * 4, Kirigami.Units.gridUnit * 26)
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Icon {
            source: "view-task"
            implicitWidth: Kirigami.Units.iconSizes.enormous
            implicitHeight: Kirigami.Units.iconSizes.enormous
            Layout.alignment: Qt.AlignHCenter
        }

        Kirigami.Heading {
            text: i18n("Connect to Todoist")
            level: 1
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        QQC2.Label {
            text: i18n("Your browser will open so you can authorise this application. "
                     + "Your password is never seen by it.")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            opacity: 0.7
            Layout.fillWidth: true
        }

        // Without client credentials the OAuth flow cannot even start, so the
        // problem is explained here rather than failing at the browser.
        Kirigami.InlineMessage {
            type: Kirigami.MessageType.Warning
            visible: !Auth.configured
            Layout.fillWidth: true

            text: i18n("No OAuth client credentials are configured. Add ClientId and "
                     + "ClientSecret under the [OAuth] group of ktodorc, or set "
                     + "KTODO_CLIENT_ID and KTODO_CLIENT_SECRET in the environment.")
        }

        Kirigami.InlineMessage {
            type: Kirigami.MessageType.Error
            visible: Auth.lastError !== ""
            text: Auth.lastError
            Layout.fillWidth: true
        }

        QQC2.Button {
            text: Auth.busy
                ? i18nc("@action:button", "Waiting for your browser…")
                : i18nc("@action:button", "Sign in with Todoist")
            icon.name: "globe"
            enabled: Auth.configured && !Auth.busy
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Kirigami.Units.gridUnit * 16

            onClicked: Auth.signIn()
        }

        QQC2.BusyIndicator {
            running: Auth.busy
            visible: Auth.busy
            Layout.alignment: Qt.AlignHCenter
        }

        QQC2.Label {
            text: i18n("Redirect address: %1", Auth.redirectUri)
            font: Kirigami.Theme.smallFont
            opacity: 0.6
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: Auth.configured
            Layout.fillWidth: true

            QQC2.ToolTip.text: i18nc("@info:tooltip",
                "This exact address must be registered as the OAuth redirect URL "
                + "for your Todoist app.")
            QQC2.ToolTip.visible: redirectHover.hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

            HoverHandler {
                id: redirectHover
            }
        }

        QQC2.Button {
            text: i18nc("@action:button", "Cancel")
            icon.name: "dialog-cancel"
            visible: Auth.busy
            flat: true
            Layout.alignment: Qt.AlignHCenter
            onClicked: Auth.cancel()
        }
    }
}
