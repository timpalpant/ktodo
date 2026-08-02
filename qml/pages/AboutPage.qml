import QtQuick

import org.kde.kirigami as Kirigami

/// Standard application information page, entered from Settings.
Kirigami.AboutPage {
    id: root

    aboutData: AboutData

    // Kirigami points these at KDE's own pages for any app whose desktop file
    // id starts with "org.kde."; point interested contributors at this project
    // instead.
    getInvolvedUrl: "https://github.com/timpalpant/ktodo/pulls"
    donateUrl: ""

    // This is deliberately an action instead of PageRow's history button:
    // Settings is replaced rather than kept alive in a second pane.
    actions: [
        Kirigami.Action {
            text: i18nc("@action:button", "Back to Settings")
            tooltip: text
            icon.name: "go-previous"
            displayHint: Kirigami.DisplayHint.KeepVisible
            onTriggered: applicationWindow().showPage(Qt.resolvedUrl("SettingsPage.qml"))
        }
    ]
}
