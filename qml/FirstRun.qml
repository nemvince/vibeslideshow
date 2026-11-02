import QtQuick
import QtQuick.Controls
import Vibes 1.0

// First run: the two things the app cannot work without, asked for once, in one
// place. It closes as soon as both are there — the progress strip takes over from
// there, and the first edit is cut without further input.
//
// Plain Popup + Column on purpose: a Dialog with a custom contentItem gets no
// content height from the layout and opens as an empty sliver.
Popup {
    id: intro
    signal songRequested()
    signal folderRequested()

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 520
    padding: 20
    closePolicy: Popup.CloseOnEscape

    readonly property bool ready: studio.songPath.length > 0 && studio.videoDirs.length > 0

    background: Rectangle {
        color: Theme.panel
        radius: Theme.radius
        border.color: Theme.line
    }

    contentItem: Column {
        id: col
        spacing: 14

        Text {
            width: col.width
            text: "Make your first slideshow"
            color: Theme.ink
            font.pixelSize: 18
            font.weight: Font.DemiBold
        }
        Text {
            width: col.width
            wrapMode: Text.WordWrap
            text: "Pick a song and at least one folder of clips. Then the app finds the beat, "
                  + "scores the footage and cuts the first edit on its own — you can fix it shot by shot afterwards."
            color: Theme.dim
            font.pixelSize: 12
            lineHeight: 1.3
        }

        // ---- song -------------------------------------------------------
        Item {
            width: col.width
            height: 46

            Text {
                anchors { left: parent.left; top: parent.top }
                text: "SONG"
                color: Theme.faint
                font.pixelSize: 9
                font.letterSpacing: 1
            }
            Text {
                anchors { left: parent.left; bottom: parent.bottom; right: chooseSong.left; rightMargin: 10 }
                text: studio.songName.length ? studio.songName : "No song chosen"
                color: studio.songName.length ? Theme.ink : Theme.faint
                font.pixelSize: 12
                elide: Text.ElideMiddle
            }
            SmallButton {
                id: chooseSong
                anchors { right: parent.right; bottom: parent.bottom }
                text: studio.songPath.length ? "Change…" : "Choose a song…"
                onClicked: intro.songRequested()
            }
        }

        Rectangle { width: col.width; height: 1; color: Theme.line }

        // ---- footage ----------------------------------------------------
        Item {
            width: col.width
            height: 20

            Text {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                text: "FOOTAGE"
                color: Theme.faint
                font.pixelSize: 9
                font.letterSpacing: 1
            }
            SmallButton {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: "Add a folder…"
                onClicked: intro.folderRequested()
            }
        }

        Column {
            id: dirList
            width: col.width
            spacing: 4

            Text {
                width: dirList.width
                visible: studio.videoDirs.length === 0
                text: "No folders yet."
                color: Theme.faint
                font.pixelSize: 11
            }
            Repeater {
                model: studio.videoDirs
                delegate: Item {
                    required property string modelData
                    width: dirList.width
                    height: 24

                    Text {
                        anchors { left: parent.left; right: drop.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        text: modelData
                        color: Theme.ink
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }
                    Item {
                        id: drop
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        width: 22
                        height: 22
                        Text {
                            anchors.centerIn: parent
                            text: "✕"
                            color: dropHover.hovered ? Theme.ink : Theme.faint
                            font.pixelSize: 10
                        }
                        HoverHandler { id: dropHover }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: studio.removeFolder(modelData)
                        }
                    }
                }
            }
        }

        Item {
            width: col.width
            height: 34

            Text {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter; right: row.left; rightMargin: 12 }
                text: intro.ready ? "Ready."
                                 : (studio.videoDirs.length === 0 ? "Add a folder of clips to begin."
                                                                  : "Choose a song to begin.")
                color: intro.ready ? Theme.good : Theme.faint
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            Row {
                id: row
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                spacing: 8
                SmallButton {
                    text: "Not now"
                    flat: true
                    onClicked: intro.close()
                }
                SmallButton {
                    text: "Start"
                    accent: true
                    enabled: intro.ready
                    // Both pickers already started the work; there is nothing left
                    // to press. The progress strip takes over from here.
                    onClicked: intro.close()
                }
            }
        }
    }
}
