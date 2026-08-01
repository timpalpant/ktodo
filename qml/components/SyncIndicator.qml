import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import io.github.timpalpant.ktodo

/// Compact, always-visible sync state: a spinner while syncing, a warning
/// glyph when offline with unsent work, and quiet text otherwise.
RowLayout {
    id: root

    spacing: Kirigami.Units.smallSpacing

    QQC2.BusyIndicator {
        visible: Sync.status === Sync.Syncing
        running: visible
        implicitWidth: Kirigami.Units.iconSizes.small
        implicitHeight: Kirigami.Units.iconSizes.small
    }

    Kirigami.Icon {
        visible: Sync.status === Sync.Offline || Sync.status === Sync.Error
        source: Sync.status === Sync.Offline ? "network-disconnect" : "dialog-warning"
        color: Sync.status === Sync.Error
            ? Kirigami.Theme.negativeTextColor
            : Kirigami.Theme.neutralTextColor
        isMask: true
        implicitWidth: Kirigami.Units.iconSizes.small
        implicitHeight: Kirigami.Units.iconSizes.small
    }

    QQC2.Label {
        text: Sync.statusText
        elide: Text.ElideRight
        maximumLineCount: 1
        opacity: 0.7
        font: Kirigami.Theme.smallFont
        Layout.fillWidth: true

        QQC2.ToolTip.text: Sync.lastError !== "" ? Sync.lastError : Sync.statusText
        QQC2.ToolTip.visible: hoverHandler.hovered && Sync.lastError !== ""
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

        HoverHandler {
            id: hoverHandler
        }
    }
}
