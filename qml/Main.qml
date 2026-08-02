import QtCore

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import org.kde.kirigamiaddons.delegates as Delegates

import io.github.timpalpant.ktodo

Kirigami.ApplicationWindow {
    id: root

    title: i18n("KTodo")

    minimumWidth: Kirigami.Units.gridUnit * 22
    minimumHeight: Kirigami.Units.gridUnit * 20
    width: Kirigami.Units.gridUnit * 55
    height: Kirigami.Units.gridUnit * 38

    readonly property bool signedIn: Auth.authenticated

    /// Sidebar width, dragged by the handle on its trailing edge and
    /// remembered between sessions the way Dolphin's places panel is.
    readonly property int minimumSidebarWidth: Kirigami.Units.gridUnit * 10
    readonly property int maximumSidebarWidth: Kirigami.Units.gridUnit * 30

    Settings {
        id: uiSettings
        category: "Sidebar"
        property int width: Kirigami.Units.gridUnit * 15
    }

    // Shared models, so the sidebar and its counters live in one place.
    ProjectsModel {
        id: personalProjects
        personalOnly: true
        excludeInbox: true
    }
    ProjectsModel {
        id: teamProjects
        teamOnly: true
    }
    ProjectsModel {
        id: favoriteProjects
        favoritesOnly: true
        excludeInbox: true
    }
    LabelsModel {
        id: labelsModel
    }
    FiltersModel {
        id: filtersModel
    }
    TaskModel {
        id: todayCounter
        mode: TaskModel.Today
    }

    // -- Navigation ---------------------------------------------------------

    /// Replaces the current view. The sidebar is a flat navigation surface,
    /// so pushing would build a back-stack that means nothing to the user.
    function showPage(page, props) {
        const properties = props ?? ({});
        // replace() rather than clear() + push(): clearing first empties the
        // stack, so the incoming page is built with nowhere to live.
        if (pageStack.depth === 0) {
            pageStack.push(page, properties);
        } else {
            pageStack.replace(page, properties);
        }
        if (!root.wideScreen) {
            globalDrawer.close();
        }
    }

    function showProject(projectId, name) {
        showPage(Qt.resolvedUrl("pages/TaskListPage.qml"), {
            mode: TaskModel.ProjectTasks,
            projectId: projectId,
            pageTitle: name
        });
    }

    function showToday() {
        showPage(Qt.resolvedUrl("pages/TaskListPage.qml"), {
            mode: TaskModel.Today,
            pageTitle: i18n("Today"),
            pageIcon: "view-calendar-day"
        });
    }

    function showInbox() {
        showPage(Qt.resolvedUrl("pages/TaskListPage.qml"), {
            mode: TaskModel.Inbox,
            projectId: App.inboxProjectId,
            pageTitle: i18n("Inbox"),
            pageIcon: "mail-folder-inbox"
        });
    }

    /// Only one task is ever edited at a time, so a single reused editor
    /// avoids churning objects on every row click.
    function openTask(taskId) {
        taskEditor.taskId = taskId;
        taskEditor.open();
    }

    /// Entry point for pages, which cannot see ids declared in this file.
    function quickAddTask(projectId) {
        quickAdd.openFor(projectId ?? "");
    }

    /// Pages route comment threads through here for the same reason.
    function openComments(taskId, canComment) {
        commentsSheet.taskId = taskId;
        commentsSheet.canComment = canComment ?? true;
        commentsSheet.open();
    }

    TaskEditor {
        id: taskEditor
    }

    CommentsSheet {
        id: commentsSheet
    }

    QuickAddSheet {
        id: quickAdd
    }

    // -- Global actions -----------------------------------------------------

    Kirigami.Action {
        id: newTaskAction
        text: i18nc("@action:button", "Add Task")
        icon.name: "list-add"
        shortcut: "Ctrl+N"
        enabled: root.signedIn
        onTriggered: quickAdd.openFor(pageStack.currentItem?.projectId ?? "")
    }

    Kirigami.Action {
        id: syncAction
        text: i18nc("@action:button", "Sync Now")
        icon.name: "view-refresh"
        shortcut: "Ctrl+R"
        enabled: root.signedIn
        onTriggered: Sync.syncNow()
    }

    Kirigami.Action {
        id: searchAction
        text: i18nc("@action:button", "Search")
        icon.name: "search"
        shortcut: "Ctrl+F"
        enabled: root.signedIn
        onTriggered: root.showPage(Qt.resolvedUrl("pages/SearchPage.qml"))
    }

    // -- Sidebar ------------------------------------------------------------

    globalDrawer: Kirigami.GlobalDrawer {
        id: globalDrawer

        // Chrome-coloured like Dolphin's places panel. GlobalDrawer otherwise
        // uses the View colour set when it is non-modal, which makes the
        // sidebar the same white as the task list beside it.
        Kirigami.Theme.colorSet: Kirigami.Theme.Window
        Kirigami.Theme.inherit: false

        enabled: root.signedIn
        isMenu: false
        modal: !root.wideScreen
        drawerOpen: root.wideScreen && root.signedIn
        width: Math.max(root.minimumSidebarWidth,
                        Math.min(root.maximumSidebarWidth, uiSettings.width))

        topPadding: 0
        bottomPadding: 0
        leftPadding: 0
        rightPadding: 0

        header: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Components.Avatar {
                name: App.userName
                source: App.userAvatar
                implicitWidth: Kirigami.Units.iconSizes.medium
                implicitHeight: Kirigami.Units.iconSizes.medium

                // Lines the avatar up with the icons in the rows below, which
                // sit at their delegate's left margin plus its own padding.
                Layout.leftMargin: Kirigami.Units.smallSpacing + Kirigami.Units.mediumSpacing
                    + Math.round(Kirigami.Units.smallSpacing / 2)
                Layout.topMargin: Kirigami.Units.largeSpacing
                Layout.bottomMargin: Kirigami.Units.largeSpacing
            }

            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true

                QQC2.Label {
                    text: App.userName || i18n("KTodo")
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    Layout.fillWidth: true
                }

                SyncIndicator {
                    Layout.fillWidth: true
                }
            }

            QQC2.ToolButton {
                icon.name: "application-menu"
                display: QQC2.AbstractButton.IconOnly
                text: i18nc("@action:button", "Application Menu")
                onClicked: appMenu.popup()

                Layout.rightMargin: Kirigami.Units.smallSpacing

                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                QQC2.Menu {
                    id: appMenu

                    QQC2.MenuItem {
                        text: syncAction.text
                        icon.name: syncAction.icon.name
                        onTriggered: Sync.syncNow()
                    }
                    QQC2.MenuItem {
                        text: i18nc("@action:inmenu", "Download Everything Again")
                        icon.name: "download"
                        onTriggered: Sync.resync()
                    }
                    QQC2.MenuSeparator {}
                    QQC2.MenuItem {
                        text: i18nc("@action:inmenu", "New Project…")
                        icon.name: "folder-new"
                        onTriggered: root.showPage(Qt.resolvedUrl("pages/ProjectEditorPage.qml"))
                    }
                    QQC2.MenuSeparator {}
                    QQC2.MenuItem {
                        text: i18nc("@action:inmenu", "Settings…")
                        icon.name: "settings-configure"
                        onTriggered: root.showPage(Qt.resolvedUrl("pages/SettingsPage.qml"))
                    }
                }
            }
        }

        // GlobalDrawer already has a scroll view around its content. Reporting
        // the sidebar's real minimum height lets that one standard scrollbar
        // appear only when there is actually something to scroll.
        Item {
            id: sidebarContentArea

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: sidebarContent.implicitHeight
            implicitHeight: sidebarContent.implicitHeight

            ColumnLayout {
                id: sidebarContent

                width: parent.width - resizeHandle.width
                height: implicitHeight
                spacing: 0

                SidebarItem {
                    text: i18n("Inbox")
                    iconName: "mail-folder-inbox"
                    onClicked: root.showInbox()
                }
                SidebarItem {
                    text: i18n("Today")
                    iconName: "view-calendar-day"
                    counter: todayCounter.taskCount
                    onClicked: root.showToday()
                }
                SidebarItem {
                    text: i18n("Upcoming")
                    iconName: "view-calendar-upcoming-days"
                    onClicked: root.showPage(Qt.resolvedUrl("pages/UpcomingPage.qml"))
                }
                SidebarItem {
                    text: i18n("Assigned to me")
                    iconName: "user-identity"
                    visible: App.isTeamAccount
                    onClicked: root.showPage(Qt.resolvedUrl("pages/TaskListPage.qml"), {
                        mode: TaskModel.AssignedToMe,
                        pageTitle: i18n("Assigned to me"),
                        pageIcon: "user-identity"
                    })
                }

                Kirigami.ListSectionHeader {
                    text: i18n("Favorites")
                    Layout.fillWidth: true
                    visible: favoriteProjects.count > 0
                }
                Repeater {
                    model: favoriteProjects
                    delegate: SidebarItem {
                        required property string projectId
                        required property string name
                        required property color color
                        required property int taskCount

                        text: name
                        dotColor: color
                        counter: taskCount
                        onClicked: root.showProject(projectId, name)
                    }
                }

                Kirigami.ListSectionHeader {
                    text: i18n("Projects")
                    Layout.fillWidth: true
                    visible: personalProjects.count > 0
                }
                Repeater {
                    model: personalProjects
                    delegate: SidebarItem {
                        required property string projectId
                        required property string name
                        required property color color
                        required property int taskCount
                        required property int depth

                        text: name
                        dotColor: color
                        counter: taskCount
                        indent: depth
                        onClicked: root.showProject(projectId, name)
                    }
                }

                Kirigami.ListSectionHeader {
                    text: i18n("Team Projects")
                    Layout.fillWidth: true
                    visible: teamProjects.count > 0
                }
                Repeater {
                    model: teamProjects
                    delegate: SidebarItem {
                        required property string projectId
                        required property string name
                        required property color color
                        required property int taskCount
                        required property int depth

                        text: name
                        dotColor: color
                        counter: taskCount
                        indent: depth
                        badgeIcon: "group"
                        onClicked: root.showProject(projectId, name)
                    }
                }

                Kirigami.ListSectionHeader {
                    text: i18n("Filters")
                    Layout.fillWidth: true
                    visible: filtersModel.count > 0
                }
                Repeater {
                    model: filtersModel
                    delegate: SidebarItem {
                        required property string name
                        required property string query
                        required property color color
                        required property int taskCount

                        text: name
                        iconName: "view-filter"
                        counter: taskCount
                        onClicked: root.showPage(Qt.resolvedUrl("pages/TaskListPage.qml"), {
                            mode: TaskModel.SavedFilter,
                            filterQuery: query,
                            pageTitle: name,
                            pageIcon: "view-filter"
                        })
                    }
                }

                Kirigami.ListSectionHeader {
                    text: i18n("Labels")
                    Layout.fillWidth: true
                    visible: labelsModel.count > 0
                }
                Repeater {
                    model: labelsModel
                    delegate: SidebarItem {
                        required property string name
                        required property color color
                        required property int taskCount

                        text: name
                        iconName: "tag"
                        tintIcon: true
                        dotColor: color
                        counter: taskCount
                        onClicked: root.showPage(Qt.resolvedUrl("pages/TaskListPage.qml"), {
                            mode: TaskModel.LabelTasks,
                            labelName: name,
                            pageTitle: name,
                            pageIcon: "tag"
                        })
                    }
                }

                Item {
                    Layout.preferredHeight: Kirigami.Units.gridUnit
                }
            }

            // Drag handle on the trailing edge, like Dolphin's panel.
            Item {
                id: resizeHandle

                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Kirigami.Units.smallSpacing * 2

                Kirigami.Separator {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                }

                // Keep the split cursor as the hover affordance. Painting
                // a full-height highlight on hover looks like a scrollbar;
                // reserve it for an active resize instead.
                Rectangle {
                    anchors.fill: parent
                    color: Kirigami.Theme.highlightColor
                    opacity: dragArea.pressed ? 0.4 : 0
                    Behavior on opacity {
                        NumberAnimation {
                            duration: Kirigami.Units.shortDuration
                        }
                    }
                }

                MouseArea {
                    id: dragArea

                    anchors.fill: parent
                    anchors.margins: -Kirigami.Units.smallSpacing
                    hoverEnabled: true
                    cursorShape: Qt.SplitHCursor
                    // The drawer swallows drags that reach it, and the
                    // press must not close a modal drawer either.
                    preventStealing: true

                    property real pressX: 0
                    property int pressWidth: 0

                    onPressed: mouse => {
                        pressX = mapToItem(null, mouse.x, 0).x;
                        pressWidth = globalDrawer.width;
                    }

                    onPositionChanged: mouse => {
                        if (!pressed) {
                            return;
                        }
                        const delta = mapToItem(null, mouse.x, 0).x - pressX;
                        uiSettings.width = Math.max(
                            root.minimumSidebarWidth,
                            Math.min(root.maximumSidebarWidth, pressWidth + delta));
                    }

                    onDoubleClicked: uiSettings.width = Kirigami.Units.gridUnit * 15
                }
            }
        }

        footer: QQC2.ToolBar {
            visible: root.signedIn

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.ToolButton {
                    text: newTaskAction.text
                    icon.name: newTaskAction.icon.name
                    onClicked: newTaskAction.trigger()
                    Layout.fillWidth: true
                }
                QQC2.ToolButton {
                    text: searchAction.text
                    icon.name: searchAction.icon.name
                    display: QQC2.AbstractButton.IconOnly
                    onClicked: searchAction.trigger()

                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                }
            }
        }
    }

    /// One row of the sidebar. Kept inline because it is navigation-specific
    /// and needs the enclosing window's helpers.
    component SidebarItem: Delegates.RoundedItemDelegate {
        id: sidebarItem

        property string iconName: ""
        property color dotColor: "transparent"
        property bool tintIcon: false
        property int counter: 0
        property int indent: 0
        property string badgeIcon: ""

        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.smallSpacing
        Layout.rightMargin: Kirigami.Units.smallSpacing

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Item {
                Layout.preferredWidth: sidebarItem.indent * Kirigami.Units.largeSpacing
                visible: sidebarItem.indent > 0
            }

            Kirigami.Icon {
                source: sidebarItem.iconName
                visible: sidebarItem.iconName !== ""
                color: sidebarItem.tintIcon ? sidebarItem.dotColor : Kirigami.Theme.textColor
                isMask: sidebarItem.tintIcon
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
            }

            Rectangle {
                visible: sidebarItem.iconName === ""
                radius: width / 2
                color: sidebarItem.dotColor
                implicitWidth: Math.round(Kirigami.Units.gridUnit * 0.6)
                implicitHeight: implicitWidth
            }

            QQC2.Label {
                text: sidebarItem.text
                elide: Text.ElideRight
                maximumLineCount: 1
                Layout.fillWidth: true
            }

            Kirigami.Icon {
                source: sidebarItem.badgeIcon
                visible: sidebarItem.badgeIcon !== ""
                opacity: 0.6
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
            }

            QQC2.Label {
                text: sidebarItem.counter
                visible: sidebarItem.counter > 0
                opacity: 0.6
                font: Kirigami.Theme.smallFont
            }
        }
    }

    // -- Lifecycle ----------------------------------------------------------

    /// Picks the landing view once it is known whether there is a token.
    function showAuthLanding() {
        if (Auth.resolving) {
            root.showPage(Qt.resolvedUrl("pages/LoadingPage.qml"));
        } else if (Auth.authenticated) {
            root.showToday();
        } else {
            root.showPage(Qt.resolvedUrl("pages/LoginPage.qml"));
        }
    }

    Component.onCompleted: root.showAuthLanding()

    Connections {
        target: Auth

        // The wallet resolves in the background, so the landing view is
        // decided again when it settles.
        function onResolvingChanged() {
            root.showAuthLanding();
        }

        function onSignedIn() {
            root.showToday();
        }

        function onSignedOut() {
            root.showPage(Qt.resolvedUrl("pages/LoginPage.qml"));
        }

        function onAuthorizationExpired() {
            root.showPage(Qt.resolvedUrl("pages/LoginPage.qml"));
        }
    }

    Connections {
        target: App

        function onErrorOccurred(message) {
            errorBanner.text = message;
            errorBanner.visible = true;
        }
    }

    // A rejected command is recoverable, so it is reported inline rather
    // than in a modal dialog that would interrupt what the user is doing.
    footer: Kirigami.InlineMessage {
        id: errorBanner
        type: Kirigami.MessageType.Warning
        showCloseButton: true
        visible: false
        position: Kirigami.InlineMessage.Position.Footer
    }
}
