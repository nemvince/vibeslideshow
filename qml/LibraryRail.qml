import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vibes 1.0

// The left pane: the project's bin. Every clip the imported folders and files
// hold, in one flat list with a name search, and the three ways to fill it —
// the Import menu, a drop from the file manager, or the project sheet.
// Filtered-out clips stay visible but dimmed, so a filter never silently hides
// the library — you can always see what you excluded. The filters themselves
// live in the settings panel on the right.
Rectangle {
    id: rail
    color: Theme.panel

    property int hovered: -1
    property bool dropHover: false

    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.line }

    ColumnLayout {
        anchors { fill: parent; margins: Theme.gap }
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            SectionLabel { text: "Bin" }
            Item { Layout.fillWidth: true }
            Text {
                text: clips.visibleCount + " / " + clips.count
                color: Theme.faint
                font.pixelSize: 11
            }
            SmallButton {
                id: importBtn
                text: "Import…"
                onClicked: importMenu.opened ? importMenu.close() : importMenu.open()
            }
        }

        TextField {
            id: search
            objectName: "binSearch"
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            placeholderText: "Search names"
            placeholderTextColor: Theme.faint
            color: Theme.ink
            font.pixelSize: 11
            leftPadding: 8
            rightPadding: 8
            background: Rectangle { color: Theme.bg; radius: 5; border.color: Theme.line }
            onTextChanged: clips.search = text
        }

        // The clip list.
        ListView {
            id: list
            objectName: "binList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: clips
            spacing: 4
            cacheBuffer: 800
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                width: 6
                background: Item {}
                contentItem: Rectangle {
                    implicitWidth: 4
                    radius: 2
                    color: Theme.faint
                    opacity: parent.pressed ? 0.9 : 0.3
                }
            }

            delegate: Item {
                id: row
                required property int index
                required property var model

                width: list.width
                height: 54
                opacity: model.inPool ? 1 : 0.32

                Rectangle {
                    anchors.fill: parent
                    radius: 7
                    color: hover.hovered ? Theme.panelHi : "transparent"
                }

                Rectangle {
                    id: thumbFrame
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 5 }
                    width: (height * 16) / 9
                    radius: 5
                    color: Theme.bg
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: model.thumb ? "file://" + model.thumb : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                    }
                }

                Column {
                    anchors { left: thumbFrame.right; leftMargin: 9; right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                    spacing: 3
                    Text {
                        width: parent.width
                        text: model.name
                        color: Theme.ink
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }
                    Row {
                        spacing: 6
                        ScoreBadge { score: model.score; motion: model.mv }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: model.day.length ? model.day : "no stamp"
                            color: Theme.faint
                            font.pixelSize: 10
                        }
                        Text {
                            visible: model.proxy
                            anchors.verticalCenter: parent.verticalCenter
                            text: "proxy"
                            color: Theme.accent
                            font.pixelSize: 10
                        }
                    }
                }

                HoverHandler { id: hover }
                MouseArea {
                    anchors.fill: parent
                    // Dropping a clip in needs a settled edit to drop it into.
                    enabled: studio.workspaceEnabled
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    // Click a clip to drop it into the show at the playhead.
                    onClicked: studio.newInPointAt(model.clipId, showCtl.position)
                }

                // Declared after the row MouseArea: later siblings stack on top,
                // so the ✕ gets the click instead of the row underneath it.
                Item {
                    id: removeBtn
                    anchors { right: parent.right; rightMargin: 4; verticalCenter: parent.verticalCenter }
                    width: 22
                    height: 22
                    visible: hover.hovered

                    Rectangle {
                        anchors.fill: parent
                        radius: 4
                        color: removeHover.hovered ? Theme.panelHi : "transparent"
                    }
                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: removeHover.hovered ? Theme.ink : Theme.faint
                        font.pixelSize: 10
                    }
                    HoverHandler { id: removeHover }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: studio.removeClip(model.clipId)
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: clips.count === 0
                text: studio.busy ? "Looking for footage…" : "No footage yet — import files or a folder"
                color: Theme.faint
                font.pixelSize: 12
            }
        }
    }

    Popup {
        id: importMenu
        objectName: "importMenu"
        x: importBtn.x
        y: importBtn.y + importBtn.height + 4
        width: 160
        padding: 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.panelHi
            radius: 7
            border.color: Theme.line
        }

        contentItem: Column {
            spacing: 2
            SmallButton {
                width: importMenu.width - 8
                text: "Files…"
                flat: true
                onClicked: {
                    importMenu.close()
                    studio.openFilesDialog()
                }
            }
            SmallButton {
                width: importMenu.width - 8
                text: "Folder…"
                flat: true
                onClicked: {
                    importMenu.close()
                    studio.openFolderDialog()
                }
            }
        }
    }

    // The whole pane is the drop target, so a file dragged anywhere onto the rail
    // lands in the bin. Anchored last so it covers the list while a drag is over.
    DropArea {
        anchors.fill: parent
        onEntered: (drag) => {
            rail.dropHover = drag.hasUrls
            drag.acceptProposedAction()
        }
        onExited: rail.dropHover = false
        onDropped: (drop) => {
            rail.dropHover = false
            if (drop.hasUrls) {
                studio.importUrls(drop.urls)
                drop.acceptProposedAction()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: rail.dropHover
        color: Theme.bg
        opacity: 0.88

        Text {
            anchors.centerIn: parent
            text: "Drop to import"
            color: Theme.accent
            font.pixelSize: 13
        }
    }
}
