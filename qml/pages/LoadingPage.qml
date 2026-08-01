import QtQuick

import org.kde.kirigami as Kirigami

/**
 * Shown while the stored token is being looked up.
 *
 * The wallet opens in the background and may sit behind an unlock prompt, so
 * this stands in for the brief window where it is not yet known whether the
 * user is signed in. Showing the login page instead would ask people who are
 * already signed in to sign in again.
 */
Kirigami.Page {
    title: i18nc("@title", "KTodo")

    Kirigami.LoadingPlaceholder {
        anchors.centerIn: parent
        text: i18n("Unlocking your keychain…")
    }
}
