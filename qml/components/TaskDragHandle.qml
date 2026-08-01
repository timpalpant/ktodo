import QtQuick

import org.kde.kirigami as Kirigami

/**
 * Grip for reordering rows in a ListView.
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

    /// The view's delegate item; this is what physically moves.
    property Item listItem: null
    property ListView listView: null
    /// Model row of listItem.
    property int rowIndex: -1

    signal dragStarted(int index)
    /**
     * Where the row would land, expressed as "insert before this row".
     * Ranges over 0..count, where count means "after the last row". This is
     * direction-independent, which is what the drop indicator wants.
     */
    signal dragMoved(int insertIndex)
    signal dragEnded(int fromIndex, int toIndex)

    readonly property alias dragActive: mouseArea.drag.active

    implicitWidth: Kirigami.Units.iconSizes.smallMedium
    implicitHeight: Kirigami.Units.iconSizes.smallMedium

    Kirigami.Icon {
        anchors.fill: parent
        source: "handle-sort"
        opacity: mouseArea.pressed ? 1 : 0.7
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        // The row is a button and the view is a Flickable; without this either
        // could steal the press and cancel the drag.
        preventStealing: true

        drag.target: root.listItem
        drag.axis: Drag.YAxis
        drag.minimumY: 0

        property Item originalParent: null
        property int startIndex: -1
        /// "Insert before this row"; see dragMoved.
        property int insertIndex: -1

        /**
         * First row whose midpoint lies below @p centre — i.e. the row the
         * dragged item would be inserted in front of.
         *
         * ListView.indexAt() is unusable here: the dragged row is reparented
         * out of the content item, so its slot holds no item and indexAt()
         * returns -1 just where the pointer starts.
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
                if (centre < item.y + item.height / 2) {
                    return i;
                }
            }
            return root.listView.count;
        }

        onPressed: {
            // Misbinding these is easy: see the note on TaskDelegate.dragView.
            if (!root.listItem || !root.listView) {
                console.warn("TaskDragHandle: listItem/listView unset; refusing to drag");
                return;
            }

            // Lift the row out of the content item so the view stops laying it
            // out while the pointer is carrying it.
            originalParent = root.listItem.parent;
            root.listItem.parent = root.listView;
            root.listItem.y = originalParent.mapToItem(root.listView, root.listItem.x,
                                                       root.listItem.y).y;
            root.listItem.z = 99;

            drag.maximumY = root.listView.height - root.listItem.height;

            startIndex = root.rowIndex;
            insertIndex = startIndex;
            root.dragStarted(startIndex);
        }

        onPositionChanged: {
            if (!pressed || !originalParent) {
                return;
            }

            const centre = root.listView.contentItem
                .mapFromItem(root.listItem, 0, root.listItem.height / 2).y;

            const index = insertIndexFor(centre);
            if (index !== insertIndex) {
                insertIndex = index;
                root.dragMoved(index);
            }
        }

        onReleased: finishDrag()
        onCanceled: finishDrag()

        function finishDrag() {
            if (!originalParent) {
                return;
            }
            root.listItem.y = originalParent.mapFromItem(root.listItem, 0, 0).y;
            root.listItem.parent = originalParent;
            root.listItem.z = 0;
            originalParent = null;

            // "Insert before row N" becomes a destination index: dragging
            // downwards, removing the row first shifts everything below it up
            // by one, so the destination is one less than the insert point.
            let destination = insertIndex;
            if (destination > startIndex) {
                destination -= 1;
            }
            root.dragEnded(startIndex, destination);

            startIndex = -1;
            insertIndex = -1;
        }
    }
}
