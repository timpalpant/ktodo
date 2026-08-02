import QtQuick
import QtTest

import "../../qml/components" as Components

Item {
    id: root

    width: 320
    height: 240

    property int reportedInsert: -1
    property int reportedTarget: -1
    property bool reportedSubtask: false

    ListModel {
        id: tasks

        Component.onCompleted: {
            for (let index = 0; index < 30; ++index) {
                append({ taskNumber: index });
            }
        }
    }

    ListView {
        id: list

        width: parent.width
        height: 200
        clip: true
        model: tasks

        delegate: Item {
            id: row

            required property int index

            width: list.width
            height: 40
            property real dragOffsetY: 0
            transform: Translate {
                y: row.dragOffsetY
            }

            Rectangle {
                anchors.fill: parent
                color: row.index % 2 === 0 ? "lightsteelblue" : "lightgray"
            }

            Components.TaskDragHandle {
                anchors.fill: parent
                listItem: row
                listView: list
                stackingItem: row
                rowIndex: row.index

                onDragMoved: (insertIndex, targetIndex, asSubtask) => {
                    root.reportedInsert = insertIndex;
                    root.reportedTarget = targetIndex;
                    root.reportedSubtask = asSubtask;
                }
                onDragOffsetChanged: offset => row.dragOffsetY = offset
            }
        }

        // ListView may parent decorative children to its viewport rather than
        // its scrolling content item. The production drop indicator therefore
        // maps the target through its actual parent.
        Rectangle {
            id: dropIndicator

            property int targetIndex: 12

            width: list.width
            height: 2
            color: "purple"
            y: {
                // Retain contentY as a binding dependency while delegates are
                // realised for the new scroll position.
                const scrollY = list.contentY
                const target = list.itemAtIndex(targetIndex)
                return target ? parent.mapFromItem(target, 0, 0).y : -1
            }
        }
    }

    TestCase {
        name: "TaskDragHandle"
        when: windowShown

        function init() {
            list.contentY = 400
            root.reportedInsert = -1
            root.reportedTarget = -1
            root.reportedSubtask = false
            wait(50)
        }

        function test_dragUsesContentCoordinatesAfterScrolling() {
            const source = list.itemAtIndex(10)
            verify(source !== null)
            const start = list.mapFromItem(source, list.width / 2, source.height / 2)

            mousePress(list, start.x, start.y, Qt.LeftButton)
            mouseMove(list, start.x, start.y + 80, 20)

            // The transformed row remains under the pointer even though its
            // layout y stays in ListView's scrolling content coordinates.
            const draggedCentre = list.mapFromItem(source, source.width / 2,
                                                    source.height / 2)
            verify(Math.abs(draggedCentre.y - (start.y + 80)) < 1)
            mouseRelease(list, start.x, start.y + 80, Qt.LeftButton)

            compare(root.reportedTarget, 12)
            compare(root.reportedInsert, 13)
            verify(root.reportedSubtask)
        }

        function test_dropIndicatorTracksVisibleTargetAfterScrolling() {
            const target = list.itemAtIndex(dropIndicator.targetIndex)
            verify(target !== null)

            const targetTop = list.mapFromItem(target, 0, 0).y
            const indicatorTop = list.mapFromItem(dropIndicator, 0, 0).y
            verify(Math.abs(indicatorTop - targetTop) < 1)
        }
    }
}
