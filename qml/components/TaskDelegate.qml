import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import org.kde.kirigamiaddons.delegates as Delegates

/**
 * One task row.
 *
 * Completion is animated locally before the model catches up, so ticking a
 * task feels instant even though the write goes through the sync queue.
 */
Delegates.RoundedItemDelegate {
    id: root

    required property string taskId
    required property string content
    required property string description
    required property string descriptionHtml
    required property bool descriptionHasLinks
    required property int priority
    required property color priorityColor
    required property string dueText
    required property bool dueIsOverdue
    required property bool dueIsToday
    required property bool hasDue
    required property bool isRecurring
    required property string deadlineText
    required property var labels
    required property string projectName
    required property color projectColor
    required property bool isChecked
    required property int noteCount
    required property bool canCollapse
    required property bool isCollapsed
    required property int subtaskCount
    required property int subtaskCompletedCount
    required property int depth
    required property string assigneeName
    required property string assigneeAvatar
    required property bool showProject
    required property bool isPending

    /// Set by the page to enable reordering.
    property Item dragItem: null
    /**
     * The view this row lives in.
     *
     * Bind it from an id that is *not* called `listView`: RoundedItemDelegate
     * already declares a `listView` property holding the view's attached
     * object, and an unqualified name resolves against the scope object before
     * outer ids — so `dragView: listView` silently binds the attached object.
     */
    property ListView dragView: null
    property int rowIndex: -1
    property bool draggable: false
    /// Visual-only displacement controlled by TaskDragHandle. ListView keeps
    /// the outer delegate row in its normal layout slot throughout the drag.
    property real dragOffsetY: 0

    /**
     * Whether to keep the disclosure gutter free on rows without sub-tasks.
     *
     * A list where nothing nests has nothing to disclose, so the page turns
     * this off and the rows keep their old alignment.
     */
    property bool showCollapseGutter: canCollapse

    signal collapseToggled
    signal editRequested
    signal completeRequested
    signal deleteRequested
    signal scheduleRequested
    signal addSubtaskRequested
    signal dragStarted(int index)
    signal dragMoved(int insertIndex, int targetIndex, bool asSubtask)
    signal dragEnded(int fromIndex, int insertIndex, int targetIndex, bool asSubtask, bool moved, bool canceled)

    // Guards against the row being re-tapped while the fade-out plays.
    property bool completing: false

    Layout.fillWidth: true
    // The delegate's own horizontal padding aligns rows with section
    // headers; the sub-task indent goes on top of it.
    leftPadding: horizontalPadding + depth * Kirigami.Units.gridUnit * 1.5
    transform: Translate {
        y: root.dragOffsetY
    }

    opacity: completing ? 0.35 : (isPending ? 0.7 : 1.0)

    Behavior on opacity {
        NumberAnimation {
            duration: Kirigami.Units.shortDuration
        }
    }

    // Project rows use TaskDragHandle for a no-movement click. Keeping the
    // delegate button passive there prevents its own click from opening an
    // editor after a completed drag.
    onClicked: {
        if (!root.draggable) {
            root.editRequested();
        }
    }

    // A plain Item, not a layout: the hover actions are overlaid on the row and
    // must not influence its height. Were they declared as ordinary children
    // they would be adopted into the layout via Control.contentData.
    contentItem: Item {
        // The ListView owns this delegate's width. Rich text can have a very
        // large implicit width (notably an unbroken URL), so never feed that
        // width back into PageRow and let it widen the current pane.
        implicitWidth: root.availableWidth
        implicitHeight: mainRow.implicitHeight
        clip: true

        // A mouse drag begins from the task's normal content, while the
        // controls above it (checkbox, links, schedule/delete) retain their
        // direct click behavior. A simple release still opens the editor.
        TaskDragHandle {
            id: taskDrag

            anchors.fill: parent
            z: 0
            enabled: root.draggable
            visible: root.draggable
            listItem: root
            listView: root.dragView
            stackingItem: root.dragItem
            rowIndex: root.rowIndex

            onDragStarted: index => root.dragStarted(index)
            onDragMoved: (insertIndex, targetIndex, asSubtask) => root.dragMoved(insertIndex, targetIndex, asSubtask)
            onDragEnded: (fromIndex, insertIndex, targetIndex, asSubtask, moved, canceled) => root.dragEnded(fromIndex, insertIndex, targetIndex, asSubtask, moved, canceled)
            onDragOffsetChanged: offset => root.dragOffsetY = offset
            onClicked: root.editRequested()
        }

        RowLayout {
            id: mainRow

            anchors.fill: parent
            z: 1
            spacing: Kirigami.Units.largeSpacing

            RowLayout {
                spacing: Kirigami.Units.smallSpacing

                // The title's line box is forced to this same height below, so
                // aligning both to the top puts them on a shared center line
                // whether or not the row carries a description.
                Layout.alignment: Qt.AlignTop

                // Disclosure triangle in the leading gutter, where KDE's tree
                // views put it. The gutter is held open across the whole list
                // so rows with and without sub-tasks stay aligned.
                Item {
                    visible: root.showCollapseGutter
                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                    implicitHeight: Kirigami.Units.iconSizes.smallMedium

                    QQC2.ToolButton {
                        anchors.fill: parent
                        visible: root.canCollapse
                        padding: 0
                        display: QQC2.AbstractButton.IconOnly
                        icon.name: root.isCollapsed ? "arrow-right" : "arrow-down"
                        icon.width: Kirigami.Units.iconSizes.small
                        icon.height: Kirigami.Units.iconSizes.small
                        text: root.isCollapsed
                            ? i18nc("@action:button", "Show sub-tasks")
                            : i18nc("@action:button", "Hide sub-tasks")

                        onClicked: root.collapseToggled()

                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    }
                }

                PriorityBadge {
                    priority: root.priority
                    checked: root.isChecked || root.completing
                    accent: root.priorityColor.a > 0 ? root.priorityColor : Kirigami.Theme.textColor

                    Layout.alignment: Qt.AlignTop

                    onToggled: {
                        if (root.completing) {
                            return;
                        }
                        root.completing = true;
                        completeTimer.start();
                    }
                }
            }

            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                // Let the RowLayout shrink this column to the width left by
                // the checkbox, avatar, and action overlay.
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignTop

                QQC2.Label {
                    id: titleLabel

                    text: root.content
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignVCenter
                    font.strikeout: root.isChecked
                    opacity: root.isChecked ? 0.6 : 1.0

                    // Matches the checkbox, so a single-line row reads as one line.
                    Layout.minimumHeight: Kirigami.Units.iconSizes.smallMedium
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }

                Item {
                    id: descriptionPreview

                    visible: root.description !== ""
                    implicitHeight: descriptionLabel.implicitHeight
                    clip: true
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0

                    QQC2.Label {
                        id: descriptionLabel

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.rightMargin: descriptionEllipsis.visible
                            ? descriptionEllipsis.implicitWidth
                            : 0
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        // Rich text keeps Markdown links and bare URLs
                        // clickable. Qt does not elide rich text itself, so
                        // the parent clips it and the label below supplies the
                        // visible ellipsis when it overflows.
                        text: root.descriptionHtml
                        textFormat: Text.RichText
                        maximumLineCount: 1
                        wrapMode: Text.NoWrap
                        opacity: 0.6
                        font: Kirigami.Theme.smallFont
                        clip: true

                        onLinkActivated: link => Qt.openUrlExternally(link)

                        // Only intercept the pointer over an actual link, so
                        // clicking anywhere else still opens the task.
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            hoverEnabled: root.descriptionHasLinks
                            enabled: root.descriptionHasLinks
                            cursorShape: descriptionLabel.linkAt(mouseX, mouseY) !== ""
                                ? Qt.PointingHandCursor
                                : Qt.ArrowCursor

                            onPressed: mouse => {
                                mouse.accepted = descriptionLabel.linkAt(mouse.x, mouse.y) !== "";
                            }
                            onClicked: mouse => {
                                const link = descriptionLabel.linkAt(mouse.x, mouse.y);
                                if (link !== "") {
                                    Qt.openUrlExternally(link);
                                }
                            }
                        }
                    }

                    QQC2.Label {
                        id: descriptionEllipsis

                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        z: 1
                        text: "…"
                        visible: descriptionLabel.implicitWidth > descriptionPreview.width
                        color: descriptionLabel.color
                        font: descriptionLabel.font
                    }
                }

                // Metadata: dates, project, labels, comment count.
                Flow {
                    id: metadataFlow

                    spacing: Kirigami.Units.largeSpacing
                    visible: root.hasDue || root.showProject || root.labels.length > 0
                        || root.noteCount > 0 || root.deadlineText !== ""
                        || root.subtaskCount > 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.topMargin: Kirigami.Units.smallSpacing * 0.5

                    DueChip {
                        text: root.dueText
                        overdue: root.dueIsOverdue
                        today: root.dueIsToday
                        recurring: root.isRecurring
                    }

                    DueChip {
                        text: root.deadlineText
                        iconName: "flag"
                        overdue: false
                        today: false
                    }

                    Repeater {
                        model: root.labels

                        delegate: RowLayout {
                            id: labelChip

                            required property string modelData

                            spacing: Kirigami.Units.smallSpacing * 0.5

                            Kirigami.Icon {
                                source: "tag"
                                isMask: true
                                color: Kirigami.Theme.disabledTextColor
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                            }
                            QQC2.Label {
                                text: labelChip.modelData
                                font: Kirigami.Theme.smallFont
                                opacity: 0.6
                            }
                        }
                    }

                    // Sub-task progress, the way Todoist reads it: done out of
                    // total, counting sub-tasks hidden by a collapse or by the
                    // completed filter just the same.
                    RowLayout {
                        spacing: Kirigami.Units.smallSpacing * 0.5
                        visible: root.subtaskCount > 0

                        Kirigami.Icon {
                            source: "view-list-tree"
                            isMask: true
                            color: Kirigami.Theme.disabledTextColor
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                        }
                        QQC2.Label {
                            text: i18nc("@label completed of total sub-tasks", "%1/%2",
                                        root.subtaskCompletedCount, root.subtaskCount)
                            font: Kirigami.Theme.smallFont
                            opacity: 0.6

                            // Pluralised on the total, which is what varies;
                            // "%1 of %2" cannot be, since KLocalizedString
                            // takes its plural from the first argument.
                            QQC2.ToolTip.text: i18ncp("@info:tooltip",
                                                      "%1 sub-task, %2 completed",
                                                      "%1 sub-tasks, %2 completed",
                                                      root.subtaskCount,
                                                      root.subtaskCompletedCount)
                            QQC2.ToolTip.visible: subtaskHover.hovered
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                            HoverHandler {
                                id: subtaskHover
                            }
                        }
                    }

                    RowLayout {
                        spacing: Kirigami.Units.smallSpacing * 0.5
                        visible: root.noteCount > 0

                        Kirigami.Icon {
                            source: "comment"
                            isMask: true
                            color: Kirigami.Theme.disabledTextColor
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                        }
                        QQC2.Label {
                            text: root.noteCount
                            font: Kirigami.Theme.smallFont
                            opacity: 0.6
                        }
                    }

                    RowLayout {
                        spacing: Kirigami.Units.smallSpacing * 0.5
                        visible: root.showProject && root.projectName !== ""

                        Rectangle {
                            implicitWidth: Math.round(Kirigami.Units.gridUnit * 0.45)
                            implicitHeight: implicitWidth
                            radius: width / 2
                            color: root.projectColor
                        }
                        QQC2.Label {
                            text: root.projectName
                            font: Kirigami.Theme.smallFont
                            opacity: 0.6
                        }
                    }
                }
            }

            Components.Avatar {
                name: root.assigneeName
                source: root.assigneeAvatar
                visible: root.assigneeName !== ""
                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
                Layout.alignment: Qt.AlignVCenter

                QQC2.ToolTip.text: i18nc("@info:tooltip", "Assigned to %1", root.assigneeName)
                QQC2.ToolTip.visible: avatarHover.hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                HoverHandler {
                    id: avatarHover
                }
            }

            // Holds a column open for the overlaid actions so long titles never
            // run underneath them. Zero height, so the row stays as tall as its
            // own content.
            Item {
                implicitWidth: actions.implicitWidth
                implicitHeight: 0
            }
        }

        RowLayout {
            id: actions

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            z: 2
            spacing: 0

            opacity: root.hovered || taskDrag.dragActive ? 1 : 0
            // Stays live while a drag is in flight: the pointer routinely
            // leaves the row it started on, and disabling here would drop the
            // grab mid-gesture.
            enabled: root.hovered || taskDrag.dragActive

            Behavior on opacity {
                NumberAnimation {
                    duration: Kirigami.Units.shortDuration
                }
            }

            QQC2.ToolButton {
                icon.name: "list-add"
                display: QQC2.AbstractButton.IconOnly
                text: i18nc("@action:button", "Add sub-task")
                onClicked: root.addSubtaskRequested()

                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
            QQC2.ToolButton {
                icon.name: "view-calendar-day"
                display: QQC2.AbstractButton.IconOnly
                text: i18nc("@action:button", "Schedule")
                onClicked: root.scheduleRequested()

                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
            QQC2.ToolButton {
                icon.name: "edit-delete"
                display: QQC2.AbstractButton.IconOnly
                text: i18nc("@action:button", "Delete")
                onClicked: root.deleteRequested()

                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
        }
    }

    // Lets the tick animation finish before the row leaves the model.
    Timer {
        id: completeTimer
        interval: Kirigami.Units.longDuration
        onTriggered: root.completeRequested()
    }
}
