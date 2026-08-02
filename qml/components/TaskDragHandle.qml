import QtQuick

import org.kde.kirigami as Kirigami

/**
 * Drag surface for reordering rows in a ListView.
 *
 * The gesture is purely visual: only the dragged row moves, and the model is
 * left alone until the drop. Reordering the model on every mouse move — which
 * is what Kirigami's ListItemDragHandle does — sets relayout, displacement
 * animations and delegate churn against the row being positioned by hand,
 * which makes the drag stutter and lose its grab.
 *
 * The owning page decides what a drop means; this only reports positions.
 */
Item {
    id: root

    /// The visual task item that follows the pointer during a drag.
    property Item listItem: null
    property ListView listView: null
    /// The ListView delegate whose stacking order should be raised while its
    /// visual task item overlaps neighbouring rows.
    property Item stackingItem: null
    /// Model row of listItem.
    property int rowIndex: -1
    /// True for an old-style visible grip; false for the full-row surface.
    property bool showIcon: false

    signal dragStarted(int index)
    /**
     * Where the row would land, expressed as "insert before this row".
     * Ranges over 0..count, where count means "after the last row". This is
     * direction-independent, which is what the drop indicator wants.
     */
    signal dragMoved(int insertIndex, int targetIndex, bool asSubtask)
    signal dragEnded(int fromIndex, int insertIndex, int targetIndex, bool asSubtask, bool moved, bool cancelled)
    /// Offset applied by the owner as a visual transform, never to ListView's
    /// layout-managed y property.
    signal dragOffsetChanged(real offset)
    signal clicked()

    readonly property alias dragActive: mouseArea.dragging

    // Kept on the outer item so it survives the visual transform applied to
    // the task while a drag is active.
    property int lastTargetIndex: -2
    property bool lastAsSubtask: false

    implicitWidth: showIcon ? Kirigami.Units.iconSizes.smallMedium : 0
    implicitHeight: showIcon ? Kirigami.Units.iconSizes.smallMedium : 0

    Kirigami.Icon {
        anchors.fill: parent
        source: "handle-sort"
        visible: root.showIcon
        opacity: mouseArea.pressed ? 1 : 0.7
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        // The row is a button and the view is a Flickable; without this either
        // could steal the press and cancel the drag.
        preventStealing: true

        property int startIndex: -1
        /// "Insert before this row"; see dragMoved.
        property int insertIndex: -1
        property int targetIndex: -1
        property bool asSubtask: false
        property bool moved: false
        property bool gestureActive: false
        property bool dragging: false
        property real pressContentY: 0
        property real baseItemY: 0
        property real grabOffset: 0
        property real viewportTop: 0
        property real viewportBottom: 0

        /**
         * First row whose midpoint lies below @p centre — i.e. the row the
         * dragged item would be inserted in front of.
         *
         * ListView.indexAt() does not reliably identify the visual row while
         * the source carries a transform, so compare against realised rows in
         * the ListView's content coordinate system instead.
         */
        function insertIndexFor(centre) {
            for (let i = 0; i < root.listView.count; ++i) {
                if (i === startIndex) {
                    continue;   // Its geometry is not in content space.
                }
                const item = root.listView.itemAtIndex(i);
                if (!item) {
                    continue;   // Not realised; it is off-screen either way.
                }
                const itemY = root.listView.contentItem.mapFromItem(item, 0, 0).y;
                if (centre < itemY + item.height / 2) {
                    return i;
                }
            }
            return root.listView.count;
        }

        function updateDropTarget(centre) {
            let hovered = -1;
            let nest = false;
            for (let i = 0; i < root.listView.count; ++i) {
                if (i === startIndex) {
                    continue;
                }
                const item = root.listView.itemAtIndex(i);
                if (!item) {
                    continue;
                }
                const top = root.listView.contentItem.mapFromItem(item, 0, 0).y;
                const bottom = top + item.height;
                if (centre >= top && centre <= bottom) {
                    hovered = i;
                    // The middle is an explicit, forgiving nesting target.
                    // Its top/bottom quarters remain sibling drop zones.
                    const relative = (centre - top) / item.height;
                    nest = relative >= 0.25 && relative <= 0.75;
                    break;
                }
            }
            targetIndex = hovered;
            asSubtask = nest;
        }

        onPressed: {
            // Misbinding these is easy: see the note on TaskDelegate.dragView.
            if (!root.listItem || !root.listView) {
                console.warn("TaskDragHandle: list item or view is unset; refusing to drag");
                return;
            }

            // ListView owns each delegate's y position. Moving it by
            // reparenting produces a viewport/content offset as soon as the
            // list is scrolled. Instead, leave that layout intact and have
            // the owner apply a visual transform in content coordinates.
            pressContentY = root.listView.contentItem.mapFromItem(mouseArea, mouseX, mouseY).y;
            baseItemY = root.listView.contentItem.mapFromItem(root.listItem, 0, 0).y;
            grabOffset = pressContentY - baseItemY;
            viewportTop = root.listView.contentItem.mapFromItem(root.listView, 0, 0).y;
            viewportBottom = root.listView.contentItem.mapFromItem(root.listView, 0,
                                                                     root.listView.height).y;

            startIndex = root.rowIndex;
            insertIndex = startIndex;
            targetIndex = -1;
            asSubtask = false;
            moved = false;
            gestureActive = true;
            dragging = false;
            root.lastTargetIndex = -2;
            root.lastAsSubtask = false;
            root.dragStarted(startIndex);
        }

        onPositionChanged: {
            if (!pressed || !gestureActive) {
                return;
            }

            const pointerY = root.listView.contentItem.mapFromItem(mouseArea, mouseX, mouseY).y;
            const movement = pointerY - pressContentY;
            if (!dragging && Math.abs(movement) >= Math.max(6, Kirigami.Units.smallSpacing)) {
                dragging = true;
                if (root.stackingItem) {
                    root.stackingItem.z = 99;
                }
            }

            const maximumTop = Math.max(viewportTop, viewportBottom - root.listItem.height);
            const visualTop = Math.max(viewportTop,
                                       Math.min(maximumTop, pointerY - grabOffset));
            root.dragOffsetChanged(visualTop - baseItemY);
            const centre = visualTop + root.listItem.height / 2;

            const index = insertIndexFor(centre);
            updateDropTarget(centre);
            moved = dragging;
            if (index !== insertIndex || targetIndex !== root.lastTargetIndex
                    || asSubtask !== root.lastAsSubtask) {
                insertIndex = index;
                root.lastTargetIndex = targetIndex;
                root.lastAsSubtask = asSubtask;
                root.dragMoved(index, targetIndex, asSubtask);
            }
        }

        onReleased: finishDrag(false)
        onCanceled: finishDrag(true)

        function finishDrag(cancelled) {
            if (!gestureActive) {
                return;
            }
            root.dragOffsetChanged(0);
            if (root.stackingItem) {
                root.stackingItem.z = 0;
            }
            gestureActive = false;

            root.dragEnded(startIndex, insertIndex, targetIndex, asSubtask, moved, cancelled);
            if (!cancelled && !moved) {
                root.clicked();
            }

            startIndex = -1;
            insertIndex = -1;
            targetIndex = -1;
            asSubtask = false;
            dragging = false;
            root.lastTargetIndex = -2;
            root.lastAsSubtask = false;
        }
    }
}
