import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import org.kde.kirigamiaddons.formcard as FormCard

import io.github.timpalpant.ktodo

/// Account and sync settings.
FormCard.FormCardPage {
    id: root

    title: i18nc("@title:window", "Settings")

    FormCard.FormHeader {
        title: i18nc("@title:group", "Account")
    }

    FormCard.FormCard {
        FormCard.AbstractFormDelegate {
            background: null
            Layout.fillWidth: true

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Components.Avatar {
                    name: App.userName
                    source: App.userAvatar
                    implicitWidth: Kirigami.Units.iconSizes.huge
                    implicitHeight: Kirigami.Units.iconSizes.huge
                }

                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true

                    Kirigami.Heading {
                        text: App.userName
                        level: 3
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    QQC2.Label {
                        text: App.userEmail
                        opacity: 0.7
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    QQC2.Label {
                        text: i18n("Team account")
                        visible: App.isTeamAccount
                        opacity: 0.7
                        font: Kirigami.Theme.smallFont
                    }
                }
            }
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormButtonDelegate {
            text: i18nc("@action:button", "Sign Out")
            description: i18n("Removes the stored token and clears the local cache")
            icon.name: "system-log-out"
            onClicked: signOutPrompt.open()
        }
    }

    FormCard.FormHeader {
        title: i18nc("@title:group", "Synchronisation")
    }

    FormCard.FormCard {
        FormCard.FormTextDelegate {
            text: i18nc("@label", "Status")
            description: Sync.statusText
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormTextDelegate {
            text: i18nc("@label", "Unsent changes")
            description: Sync.pendingCount > 0
                ? i18np("%1 change waiting to be sent", "%1 changes waiting to be sent",
                        Sync.pendingCount)
                : i18n("Everything is up to date")
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormButtonDelegate {
            text: i18nc("@action:button", "Sync Now")
            icon.name: "view-refresh"
            onClicked: Sync.syncNow()
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormButtonDelegate {
            text: i18nc("@action:button", "Download Everything Again")
            description: i18n("Rebuilds the local cache from scratch. Unsent changes are kept.")
            icon.name: "download"
            onClicked: Sync.resync()
        }
    }

    FormCard.FormHeader {
        title: i18nc("@title:group", "About")
    }

    FormCard.FormCard {
        FormCard.FormButtonDelegate {
            text: i18nc("@action:button", "About KTodo")
            icon.name: "help-about"
            // Replace this page instead of adding an inaccessible second pane
            // in a wide PageRow. The About page offers an explicit return
            // action for coming back here.
            onClicked: applicationWindow().showPage(Qt.resolvedUrl("AboutPage.qml"))
        }
    }

    Kirigami.PromptDialog {
        id: signOutPrompt

        title: i18nc("@title:window", "Sign Out")
        subtitle: Sync.pendingCount > 0
            ? i18np("You have %1 unsent change that will be lost. Sign out anyway?",
                    "You have %1 unsent changes that will be lost. Sign out anyway?",
                    Sync.pendingCount)
            : i18n("The local cache will be cleared. Your tasks stay safe on Todoist.")
        standardButtons: Kirigami.Dialog.Cancel

        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Sign Out")
                icon.name: "system-log-out"
                onTriggered: {
                    App.signOut();
                    signOutPrompt.close();
                }
            }
        ]
    }
}
