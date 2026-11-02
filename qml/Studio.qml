import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Vibes 1.0

ApplicationWindow {
    id: root
    width: 1440
    height: 900
    // Small enough to fit a laptop screen next to the rest of the desktop, large
    // enough that every pane still has room. The layout below is proportional, so
    // nothing breaks between these and full screen.
    minimumWidth: 960
    minimumHeight: 620
    visible: true
    title: "vibeslideshow"
    color: palette.window

    // Set while a native picker is open on top of one of the popups, so that popup
    // comes back afterwards instead of dropping the user out of the flow.
    property bool reopenFootage: false
    property bool reopenWelcome: false

    function reopenWhichever() {
        if (reopenFootage) {
            reopenFootage = false
            footageDialog.open()
        } else if (reopenWelcome) {
            reopenWelcome = false
            welcome.open()
        }
    }


    PanelBackground { anchors.fill: parent }

    // ---- top bar ----------------------------------------------------------
    Rectangle {
        id: topBar
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 62
        color: Theme.panel

        RowLayout {
            anchors { fill: parent; leftMargin: Theme.pad; rightMargin: Theme.pad }
            spacing: 16

            Row {
                spacing: 9
                Rectangle {
                    width: 10; height: 10; radius: 5
                    anchors.verticalCenter: parent.verticalCenter
                    color: showCtl.playing ? Theme.accent : Theme.faint
                    Behavior on color { ColorAnimation { duration: 180 } }
                }
                Text {
                    text: "vibeslideshow"
                    color: Theme.ink
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Rectangle { implicitWidth: 1; implicitHeight: 20; color: Theme.line }

            // Song: name, then what the analyser made of it, then status. Each is
            // its own column so they never jam together; both narrow down before
            // the row has to give anything up.
            Column {
                Layout.alignment: Qt.AlignVCenter
                spacing: 2
                Text {
                    text: studio.songName.length ? studio.songName : "No song loaded"
                    color: studio.songName.length ? Theme.ink : Theme.faint
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, root.width < 1200 ? 170 : 280)
                }
                Text {
                    text: studio.songAnalysed ? studio.songSummary : " "
                    visible: text.trim().length > 0
                    color: Theme.faint
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, root.width < 1200 ? 170 : 280)
                    height: 12
                }
            }

            Rectangle { implicitWidth: 1; implicitHeight: 20; color: Theme.line; visible: statusText.visible && statusText.text.length > 0 }

            // While a pass runs this slot carries the live progress + ETA;
            // otherwise it carries whatever the app last said.
            Text {
                id: statusText
                text: studio.busy ? studio.progressLabel : studio.status
                color: studio.busy ? Theme.dim : Theme.faint
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.maximumWidth: root.width < 1200 ? 190 : 300
                Layout.alignment: Qt.AlignVCenter
            }

            Item { Layout.fillWidth: true }

            // The first thing to go when the window is narrow: it is a nicety, and
            // the rail already shows the same counts.
            Text {
                visible: root.width >= 1120
                text: studio.libraryCount + " clips · " + studio.poolCount + " in pool"
                color: Theme.dim
                font.pixelSize: 11
                Layout.alignment: Qt.AlignVCenter
                Layout.rightMargin: 4
            }

            SmallButton {
                text: "Song…"
                onClicked: studio.openSongDialog()
            }
            SmallButton {
                text: "Footage…"
                onClicked: studio.openFolderDialog()
            }
        }

        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }

        // Indexing progress rides on the top bar's own bottom edge: it appears
        // where the eye already is, and nothing reflows when it does.
        ProgressLine {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            active: studio.busy
            value: studio.progress
        }
    }

    // ---- main three-pane body --------------------------------------------
    Item {
        anchors { top: topBar.bottom; left: parent.left; right: parent.right; bottom: transport.top }

        // Left: library rail with real thumbnails and score badges.
        LibraryRail {
            id: rail
            anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
            width: Math.round(Math.max(220, Math.min(340, root.width * 0.20)))
        }

        // Center: the stage.
        Stage {
            id: stage
            anchors { top: parent.top; bottom: parent.bottom; left: rail.right; right: sidePanel.left }
        }

        // Right: the look (presets in prose) + Advanced, collapsible.
        SidePanel {
            id: sidePanel
            anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
        }
    }

    // ---- transport + timeline strip --------------------------------------
    Rectangle {
        id: transport
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        // The preview is 16:9 at the current stage width, so its height is decided
        // by the width of the window; the timeline gets everything that is left
        // over, within bounds. Making the window wider therefore makes the timeline
        // taller, which is the only part of the window that wants more height.
        readonly property real stageWidth: Math.max(0, root.width - rail.width - sidePanel.width)
        readonly property real previewHeight: stageWidth * 9.0 / 16.0
        height: Math.round(Math.max(170, Math.min(root.height * 0.42,
                                                  root.height - topBar.height - previewHeight)))
        color: Theme.panel

        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.line }

        ColumnLayout {
            anchors { fill: parent; margins: Theme.gap }
            spacing: 8

            // One control row: transport, edit actions, volume, Generate. Seeking
            // happens on the timeline itself.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    id: playBtn
                    implicitWidth: 36; implicitHeight: 36; radius: 18
                    // Locked while the library or the song is being (re)built:
                    // there is no valid edit to start or stop in that window, and
                    // nothing to play before a song has been read.
                    property bool usable: studio.workspaceEnabled && studio.songAnalysed
                    opacity: usable ? 1 : 0.45
                    color: showCtl.playing ? Theme.accent : Theme.panelHi
                    border.color: showCtl.playing ? Theme.accent : Theme.line
                    Text {
                        anchors.centerIn: parent
                        text: showCtl.playing ? "\u275A\u275A" : "\u25B6"
                        color: Theme.ink
                        font.pixelSize: 13
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: playBtn.usable
                        cursorShape: playBtn.usable ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: studio.playPause()
                    }
                }

                Text {
                    text: showCtl.positionText
                    color: Theme.ink
                    font.pixelSize: 12
                    font.family: "monospace"
                }
                Text {
                    text: "/ " + showCtl.durationText
                    color: Theme.faint
                    font.pixelSize: 12
                    font.family: "monospace"
                }

                Rectangle { Layout.preferredWidth: 1; implicitHeight: 18; color: Theme.line; Layout.leftMargin: 2; Layout.rightMargin: 2 }

                // Edit actions, in the order you reach for them. All of them need a
                // settled library: a rescan renumbers clip ids under the edit.
                IconButton { glyph: "\u2212"; tip: "Shorten"; enabled: studio.workspaceEnabled && timeline.selected >= 0; onClicked: timeline.shorten() }
                IconButton { glyph: "+"; tip: "Lengthen"; enabled: studio.workspaceEnabled && timeline.selected >= 0; onClicked: timeline.lengthen() }
                IconButton { glyph: "\u2702"; tip: "Chop in half"; enabled: studio.workspaceEnabled && timeline.selected >= 0; onClicked: timeline.chop() }
                IconButton { glyph: "\u25CE"; tip: "New in-point"; enabled: studio.workspaceEnabled && timeline.selected >= 0; onClicked: timeline.rerollIn() }
                IconButton { glyph: "\u21C4"; tip: "Swap clip"; enabled: studio.workspaceEnabled && timeline.selected >= 0; onClicked: timeline.swap() }
                IconButton { glyph: "\u2715"; tip: "Delete segment"; enabled: studio.workspaceEnabled && timeline.selected >= 0; onClicked: timeline.remove() }
                IconButton { glyph: "\u27F3"; tip: "Re-roll every in-point"; enabled: studio.workspaceEnabled && timeline.count > 0; onClicked: timeline.rerollAll() }

                Text {
                    text: timeline.selected >= 0 ? timeline.selectionInfo : timeline.stats
                    color: Theme.faint
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                }

                Rectangle { Layout.preferredWidth: 1; implicitHeight: 18; color: Theme.line; Layout.leftMargin: 2; Layout.rightMargin: 2 }

                Text {
                    text: "vol"
                    color: Theme.faint
                    font.pixelSize: 10
                    Layout.alignment: Qt.AlignVCenter
                }
                DarkSlider {
                    id: vol
                    Layout.preferredWidth: 70
                    Layout.alignment: Qt.AlignVCenter
                    from: 0
                    to: 100
                    fillColor: Theme.dim
                    Binding {
                        target: vol
                        property: "value"
                        value: studio.volume
                        when: !vol.pressed
                        restoreMode: Binding.RestoreBindingOrValue
                    }
                    onMoved: studio.volume = value
                }

                SmallButton {
                    text: "Generate"
                    enabled: !studio.busy && studio.songAnalysed
                    onClicked: studio.generate()
                }
                // Stop stays usable even mid-pass: a scan can start while a show
                // is already playing, and the music must always be stoppable.
                SmallButton { text: "Stop"; flat: true; enabled: showCtl.ready; onClicked: studio.stopShow() }
            }

            // The edit itself: segments, playhead and scrubbing in one surface.
            Timeline {
                id: tl
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }

    // ---- dialogs ---------------------------------------------------------
    // The pickers hand Studio a QUrl, not a string: only toLocalFile() decodes
    // the percent-escapes, and every DJI/song filename has spaces in it.
    FileDialog {
        id: songDialog
        objectName: "songDialog"
        title: "Choose a song"
        nameFilters: ["Audio (*.mp3 *.flac *.wav *.m4a *.ogg *.opus)", "All files (*)"]
        onAccepted: {
            studio.loadSong(selectedFile)
            root.reopenWhichever()
        }
        onRejected: root.reopenWhichever()
    }

    FolderDialog {
        id: folderDialog
        objectName: "folderDialog"
        title: "Add a footage folder"
        onAccepted: {
            studio.addFolder(selectedFolder)
            root.reopenWhichever()
        }
        onRejected: root.reopenWhichever()
    }

    // One place to manage footage: every folder the library scans, with the
    // remove button next to it. Adding lives here too, so there is never a
    // second, staler copy of this list anywhere in the UI.
    //
    // Plain Popup + Column on purpose: a Dialog with a custom contentItem gets
    // no content height from the layout and opens as an empty sliver.
    Popup {
        id: footageDialog
        objectName: "footageDialog"
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width: 560
        padding: 18
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: Theme.panel
            radius: Theme.radius
            border.color: Theme.line
        }

        contentItem: Column {
            id: footageCol
            spacing: 10

            Text {
                text: "Footage folders"
                color: Theme.ink
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            Column {
                id: dirList
                width: footageCol.width
                spacing: 6

                Repeater {
                    model: studio.videoDirs
                    delegate: Item {
                        required property string modelData
                        width: dirList.width
                        height: 34

                        Rectangle {
                            anchors {
                                left: parent.left
                                right: removeBtn.left
                                rightMargin: 8
                                top: parent.top
                                bottom: parent.bottom
                            }
                            radius: 6
                            color: Theme.bg
                            border.color: Theme.line
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                verticalAlignment: Text.AlignVCenter
                                text: modelData
                                color: Theme.ink
                                font.pixelSize: 12
                                elide: Text.ElideMiddle
                            }
                        }
                        SmallButton {
                            id: removeBtn
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            text: "Remove"
                            onClicked: studio.removeFolder(modelData)
                        }
                    }
                }
            }

            Text {
                width: footageCol.width
                visible: studio.videoDirs.length === 0
                text: "No folders yet — the library is empty."
                color: Theme.faint
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            Item {
                width: footageCol.width
                height: 40

                Column {
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    spacing: 5
                    Text {
                        text: studio.busy ? studio.progressLabel : studio.libraryCount + " clips indexed"
                        color: Theme.faint
                        font.pixelSize: 11
                    }
                    ProgressLine {
                        width: 170
                        height: 4
                        visible: studio.busy
                        active: studio.busy
                        value: studio.progress
                    }
                }
                Row {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    spacing: 8
                    SmallButton {
                        text: "Add folder…"
                        onClicked: {
                            root.reopenFootage = true
                            footageDialog.close()
                            folderDialog.open()
                        }
                    }
                    SmallButton {
                        text: "Done"
                        accent: true
                        onClicked: {
                            root.reopenFootage = false
                            footageDialog.close()
                        }
                    }
                }
            }
        }
    }

    // Asked at every launch: the two things the app needs, in one place. A
    // remembered song and folder are already filled in, so continuing is one
    // click; "Not now" leaves the restored session exactly as it was.
    FirstRun {
        id: welcome
        objectName: "firstRun"
        onSongRequested: {
            root.reopenWelcome = true
            welcome.close()
            songDialog.open()
        }
        onFolderRequested: {
            root.reopenWelcome = true
            welcome.close()
            folderDialog.open()
        }
    }

    Component.onCompleted: welcome.open()

    Connections {
        target: studio
        function onSongDialogRequested() { songDialog.open() }
        function onFolderDialogRequested() { footageDialog.open() }
    }

    // ---- keybindings: space toggles, arrows nudge ------------------------
    // A Shortcut fires before the focused item sees the key, so a bare Space would
    // toggle playback while the user types a look name, a date window or an
    // advanced value. Text entry keeps its keys.
    readonly property bool typingText: {
        const f = root.activeFocusItem
        return !!f && (f instanceof TextInput || f instanceof TextField || f instanceof TextArea)
    }

    Shortcut {
        sequence: "Space"
        enabled: !root.typingText && studio.workspaceEnabled && studio.songAnalysed
        onActivated: studio.playPause()
    }
    // Seeking clamps to the edit's own length: mpv reports 0 until it has opened
    // the file, and Math.min(0, …) would send every "forward" press to the start.
    Shortcut {
        sequence: "Right"
        enabled: !root.typingText && studio.workspaceEnabled
        onActivated: studio.seek(Math.min(tl.duration, showCtl.position + 5))
    }
    Shortcut {
        sequence: "Left"
        enabled: !root.typingText && studio.workspaceEnabled
        onActivated: studio.seek(Math.max(0, showCtl.position - 5))
    }
    Shortcut {
        sequence: "Ctrl+G"
        // Same rule as the button: no new edit while the library is working, and
        // none before the song has been analysed.
        onActivated: if (!studio.busy && studio.songAnalysed) studio.generate()
    }
    Shortcut {
        sequence: "Escape"
        onActivated: studio.stopShow()
    }
}
