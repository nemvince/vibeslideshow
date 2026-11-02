import QtQuick
import QtQuick.Controls
import Vibes 1.0

// The two things the app cannot work without, asked for at every launch in one
// place: a song and at least one folder. A remembered setup is already filled in,
// so a returning user just presses Continue; changing either recuts as it goes.
//
// Plain Popup + Column on purpose: a Dialog with a custom contentItem gets no
// content height from the layout and opens as an empty sliver.
Popup {
    id: intro
    signal songRequested()
    signal folderRequested()
    signal filesRequested()

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 520
    padding: 20
    closePolicy: Popup.CloseOnEscape

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.dur } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120 } }

    readonly property bool ready: studio.songPath.length > 0 && studio.sources.length > 0
    // A restored session: say so, and let one button carry the whole thing.
    readonly property bool remembered: studio.songPath.length > 0 || studio.sources.length > 0

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
            text: intro.remembered ? "Pick up where you left off" : "Make your first slideshow"
            color: Theme.ink
            font.pixelSize: 18
            font.weight: Font.DemiBold
        }
        Text {
            width: col.width
            wrapMode: Text.WordWrap
            text: intro.remembered
                  ? "This is what you had open last time. Change either and the app recuts as it goes."
                  : "Pick a song and import some clips. Then the app finds the beat, "
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
            Row {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                spacing: 6
                SmallButton {
                    text: "Import files…"
                    onClicked: intro.filesRequested()
                }
                SmallButton {
                    text: "Folder…"
                    onClicked: intro.folderRequested()
                }
            }
        }

        Column {
            id: dirList
            width: col.width
            spacing: 4

            Text {
                width: dirList.width
                visible: studio.sources.length === 0
                text: "Nothing imported yet."
                color: Theme.faint
                font.pixelSize: 11
            }
            Repeater {
                model: studio.sources
                delegate: Item {
                    required property var modelData
                    width: dirList.width
                    height: 26

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: -6
                        anchors.rightMargin: -4
                        radius: 5
                        color: srcHover.hovered ? Theme.panelHi : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.durFast } }
                    }
                    HoverHandler { id: srcHover }

                    Text {
                        anchors { left: parent.left; right: drop.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        text: modelData.name
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
                            onClicked: studio.removeSource(modelData.path)
                        }
                    }
                }
            }
        }

        // What the app is doing while this sheet is open. A first scan starts the
        // moment footage is imported — or at launch, for a remembered session — and
        // a modal sheet with nothing moving in it looks like a frozen app.
        Column {
            width: col.width
            spacing: 5
            visible: studio.busy || studio.progress > 0.001

            Text {
                width: parent.width
                text: studio.busy ? (studio.phase.length ? studio.phase : "Working") + " · " + studio.progressLabel
                                  : studio.status
                color: Theme.dim
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            ProgressLine {
                width: parent.width
                height: 4
                active: studio.busy
                value: studio.progress
            }
        }

        Item {
            width: col.width
            height: 34

            Text {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter; right: row.left; rightMargin: 12 }
                text: intro.ready ? "Ready."
                                 : (studio.sources.length === 0 ? "Import footage to begin."
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
                    text: intro.remembered ? "Continue" : "Start"
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
