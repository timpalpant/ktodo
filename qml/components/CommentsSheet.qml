import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components

import io.github.timpalpant.ktodo

/// Comment thread for a task, the main collaboration surface on shared projects.
Kirigami.Dialog {
    id: root

    property string taskId: ""
    property bool canComment: true

    title: i18nc("@title:window", "Comments")

    preferredWidth: Kirigami.Units.gridUnit * 28
    // Hug the content up to a cap.
    maximumHeight: Kirigami.Units.gridUnit * 34

    standardButtons: Kirigami.Dialog.NoButton
    showCloseButton: true

    // Kirigami.Dialog is edge-to-edge by default; free-standing controls need
    // breathing room from the frame.
    leftPadding: Kirigami.Units.largeSpacing * 2
    rightPadding: Kirigami.Units.largeSpacing * 2
    topPadding: Kirigami.Units.largeSpacing
    bottomPadding: Kirigami.Units.largeSpacing * 2


    NotesModel {
        id: notesModel
        taskId: root.taskId
    }

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC2.ScrollView {
            Layout.fillWidth: true
            // Measured from the thread, not the parent: the dialog is sized
            // from its content, so filling height here would be circular.
            Layout.preferredHeight: notesModel.count === 0
                ? 0
                : Math.min(notesView.contentHeight, Kirigami.Units.gridUnit * 18)
            visible: notesModel.count > 0
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            ListView {
                id: notesView

                clip: true
                model: notesModel
                spacing: Kirigami.Units.largeSpacing

                // New comments appear at the bottom, so follow them.
                onCountChanged: positionViewAtEnd()

                delegate: RowLayout {
                    id: noteDelegate

                    required property string noteId
                    required property string content
                    required property string authorName
                    required property string authorAvatar
                    required property string postedText
                    required property bool isMine
                    required property string attachmentName
                    required property string attachmentUrl
                    required property bool isPending

                    width: ListView.view.width
                    spacing: Kirigami.Units.smallSpacing
                    opacity: isPending ? 0.6 : 1.0

                    Components.Avatar {
                        name: noteDelegate.authorName
                        source: noteDelegate.authorAvatar
                        implicitWidth: Kirigami.Units.iconSizes.medium
                        implicitHeight: Kirigami.Units.iconSizes.medium
                        Layout.alignment: Qt.AlignTop
                    }

                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true

                        RowLayout {
                            Layout.fillWidth: true

                            QQC2.Label {
                                text: noteDelegate.authorName
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            QQC2.Label {
                                text: noteDelegate.postedText
                                opacity: 0.6
                                font: Kirigami.Theme.smallFont
                                Layout.fillWidth: true
                            }
                            QQC2.ToolButton {
                                icon.name: "edit-delete"
                                display: QQC2.AbstractButton.IconOnly
                                text: i18nc("@action:button", "Delete comment")
                                visible: noteDelegate.isMine
                                onClicked: App.deleteComment(noteDelegate.noteId)
                            }
                        }

                        QQC2.Label {
                            text: noteDelegate.content
                            wrapMode: Text.WordWrap
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                        }

                        QQC2.Label {
                            visible: noteDelegate.attachmentName !== ""
                            text: i18nc("@info file attachment", "📎 %1", noteDelegate.attachmentName)
                            font: Kirigami.Theme.smallFont
                            opacity: 0.7
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }

        Kirigami.PlaceholderMessage {
            text: i18n("No comments yet")
            explanation: i18n("Start the conversation below.")
            icon.name: "comment"
            visible: notesModel.count === 0
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing

            QQC2.TextArea {
                id: composer

                placeholderText: i18nc("@info:placeholder", "Write a comment…")
                wrapMode: TextEdit.Wrap
                enabled: root.canComment
                Layout.fillWidth: true
                Layout.maximumHeight: Kirigami.Units.gridUnit * 6

                Keys.onReturnPressed: event => {
                    // Enter sends; Shift+Enter inserts a newline.
                    if (event.modifiers & Qt.ShiftModifier) {
                        event.accepted = false;
                        return;
                    }
                    send();
                }
            }

            QQC2.Button {
                text: i18nc("@action:button", "Send")
                icon.name: "document-send"
                enabled: root.canComment && composer.text.trim() !== ""
                onClicked: send()
                Layout.alignment: Qt.AlignBottom
            }
        }
    }

    function send() {
        const text = composer.text.trim();
        if (text === "") {
            return;
        }
        App.addComment(root.taskId, text);
        composer.text = "";
    }
}
