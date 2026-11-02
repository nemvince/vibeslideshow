import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Vibes 1.0

ApplicationWindow {
    id: root
    width: 1440
    height: 900
    minimumWidth: 1080
    minimumHeight: 700
    visible: true
    title: "vibeslideshow"
    color: palette.window

    // Set while the folder picker is open on top of the footage dialog, so the
    // list comes back afterwards instead of dropping the user out of the flow.
    property bool reopenFootage: false

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

            Rectangle { width: 1; height: 20; color: Theme.line }

            // Song: name, then what the analyser made of it, then status. Each is
            // its own column so they never jam together.
            Column {
                Layout.alignment: Qt.AlignVCenter
                spacing: 2
                Text {
                    text: studio.songName.length ? studio.songName : "No song loaded"
                    color: studio.songName.length ? Theme.ink : Theme.faint
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, 280)
                }
                Text {
                    text: studio.songAnalysed ? studio.songSummary : " "
                    visible: text.trim().length > 0
                    color: Theme.faint
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, 280)
                    height: 12
                }
            }

            Rectangle { width: 1; height: 20; color: Theme.line; visible: studio.status.length > 0 }

            // While a pass runs this slot carries the live progress + ETA;
            // otherwise it carries whatever the app last said.
            Text {
                text: studio.busy ? studio.progressLabel : studio.status
                color: studio.busy ? Theme.dim : Theme.faint
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.maximumWidth: 300
                Layout.alignment: Qt.AlignVCenter
            }

            Item { Layout.fillWidth: true }

            Text {
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

        // Indexing progress: always drawn while the library is working, so a
        // slow scan has something moving on screen from the first second. It
        // sits on the top bar's own bottom edge; nothing reflows when it appears.
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
            width: 292
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
        height: 178
        color: Theme.panel

        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.line }

        ColumnLayout {
            anchors { fill: parent; margins: Theme.gap }
            spacing: 10

            RowLayout {
                spacing: 10

                Rectangle {
                    width: 38; height: 38; radius: 19
                    color: showCtl.playing ? Theme.accent : Theme.panelHi
                    border.color: showCtl.playing ? Theme.accent : Theme.line
                    Text {
                        anchors.centerIn: parent
                        text: showCtl.playing ? "❚❚" : "▶"
                        color: showCtl.playing ? "#141516" : Theme.ink
                        font.pixelSize: 14
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: studio.playPause() }
                }

                Text {
                    text: showCtl.positionText
                    color: Theme.ink
                    font.pixelSize: 12
                    font.family: "monospace"
                    Layout.leftMargin: 2
                }

                Slider {
                    id: scrub
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, showCtl.duration)
                    // Dragging sets `value` imperatively, which would kill a plain
                    // binding: disable the follow while held, restore it on release.
                    Binding {
                        target: scrub
                        property: "value"
                        value: showCtl.position
                        when: !scrub.pressed
                        restoreMode: Binding.RestoreBindingOrValue
                    }
                    onMoved: studio.seek(value)
                    background: Rectangle {
                        x: scrub.leftPadding
                        y: scrub.topPadding + scrub.availableHeight / 2 - height / 2
                        width: scrub.availableWidth
                        height: 3
                        radius: 1.5
                        color: Theme.line
                        Rectangle {
                            width: scrub.visualPosition * parent.width
                            height: parent.height
                            radius: 1.5
                            color: Theme.accent
                        }
                        // Cut marks: you can see where the edit lands.
                        Repeater {
                            model: timeline.count
                            delegate: Rectangle {
                                required property int index
                                // Cut marks read from the timeline model directly:
                                // the delegate has no implicit model role access here.
                                readonly property double cutAt: timeline.cutTime(index)
                                width: 1; height: 9
                                x: showCtl.duration > 0 ? cutAt / showCtl.duration * parent.width : 0
                                y: -3
                                color: "#30ffffff"
                            }
                        }
                    }
                    handle: Rectangle {
                        x: scrub.leftPadding + scrub.visualPosition * (scrub.availableWidth - width)
                        y: scrub.topPadding + scrub.availableHeight / 2 - height / 2
                        width: 12; height: 12; radius: 6
                        color: Theme.ink
                    }
                }

                Text {
                    text: showCtl.durationText
                    color: Theme.dim
                    font.pixelSize: 12
                    font.family: "monospace"
                }

                Rectangle { Layout.preferredWidth: 1; implicitHeight: 18; color: Theme.line; Layout.leftMargin: 2; Layout.rightMargin: 2 }

                // Volume: the song is the master clock, so its level is the one
                // control that matters while the show plays.
                Text {
                    text: "vol"
                    color: Theme.faint
                    font.pixelSize: 10
                    Layout.alignment: Qt.AlignVCenter
                }
                DarkSlider {
                    id: vol
                    Layout.preferredWidth: 76
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

                Rectangle { Layout.preferredWidth: 1; implicitHeight: 18; color: Theme.line; Layout.leftMargin: 2; Layout.rightMargin: 2 }

                SmallButton {
                    text: "Generate"
                    enabled: !studio.busy && studio.songAnalysed
                    onClicked: studio.generate()
                }
                SmallButton { text: "Stop"; flat: true; onClicked: studio.stopShow() }
            }

            // Editing toolbar: above the strip, icons only. The controls stay put
            // and disable themselves, so the strip never jumps.
            RowLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: timeline.selected >= 0 ? timeline.selectionInfo
                                                 : (timeline.hasTimeline ? timeline.stats : "No edit yet")
                    color: timeline.selected >= 0 ? Theme.dim : Theme.faint
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                IconButton { glyph: "\u2212"; tip: "Shorten"; enabled: timeline.selected >= 0; onClicked: timeline.shorten() }
                IconButton { glyph: "+"; tip: "Lengthen"; enabled: timeline.selected >= 0; onClicked: timeline.lengthen() }
                IconButton { glyph: "\u2702"; tip: "Chop in half"; enabled: timeline.selected >= 0; onClicked: timeline.chop() }
                IconButton { glyph: "\u25CE"; tip: "New in-point"; enabled: timeline.selected >= 0; onClicked: timeline.rerollIn() }
                IconButton { glyph: "\u21C4"; tip: "Swap clip"; enabled: timeline.selected >= 0; onClicked: timeline.swap() }
                IconButton { glyph: "\u2715"; tip: "Delete segment"; enabled: timeline.selected >= 0; onClicked: timeline.remove() }
                Rectangle { Layout.preferredWidth: 1; implicitHeight: 16; color: Theme.line; Layout.leftMargin: 6; Layout.rightMargin: 6 }
                IconButton { glyph: "\u27F3"; tip: "Re-roll every in-point"; enabled: timeline.count > 0; onClicked: timeline.rerollAll() }
            }

            // The strip: segments with real thumbnails, selectable for editing.
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.bg
                radius: Theme.radius
                border.color: Theme.line
                clip: true

                Flickable {
                    id: stripFlick
                    anchors.fill: parent
                    anchors.margins: 6
                    contentWidth: Math.max(width, stripRow.width)
                    contentHeight: height
                    flickableDirection: Flickable.HorizontalFlick
                    ScrollBar.horizontal: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        height: 6
                        background: Item {}
                        contentItem: Rectangle {
                            implicitHeight: 4
                            radius: 2
                            color: Theme.faint
                            opacity: parent.pressed ? 0.9 : 0.35
                        }
                    }

                    Row {
                        id: stripRow
                        height: stripFlick.height
                        spacing: 2

                        Repeater {
                            model: timeline
                            delegate: Item {
                                id: cell
                                required property int index
                                required property var model
                                height: stripRow.height
                                // Widths are proportional to segment length, min 8px.
                                width: showCtl.duration > 0
                                       ? Math.max(8, model.segLen / showCtl.duration * Math.max(600, stripFlick.width))
                                       : 20

                                readonly property bool showThumb: width >= 26

                                Rectangle {
                                    id: cellBg
                                    anchors.fill: parent
                                    anchors.rightMargin: 2
                                    radius: 4
                                    color: model.segSelected ? "#2c3138" : Theme.panelHi
                                    border.color: model.segSelected ? Theme.accent : "transparent"
                                    border.width: model.segSelected ? 2 : 1
                                    clip: true

                                    Image {
                                        anchors.fill: parent
                                        anchors.margins: 2
                                        visible: cell.showThumb
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: true
                                        source: model.segThumb && cell.showThumb ? "file://" + model.segThumb : ""
                                        opacity: 0.85
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: 2
                                        color: model.segColor
                                        opacity: model.segThumb && cell.showThumb ? 0 : 0.45
                                    }
                                    Rectangle {
                                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                        height: 3
                                        color: Theme.accent
                                        opacity: model.segSelected ? 1 : 0
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        timeline.selected = model.segIndex
                                        studio.cueSegment(model.segIndex)
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: timeline.count === 0
                    text: studio.busy ? "Building the pool…" : "Pick a song and a look, then press Generate"
                    color: Theme.faint
                    font.pixelSize: 13
                }
            }

        }
    }

    // ---- dialogs ---------------------------------------------------------
    // The pickers hand Studio a QUrl, not a string: only toLocalFile() decodes
    // the percent-escapes, and every DJI/song filename has spaces in it.
    FileDialog {
        id: songDialog
        title: "Choose a song"
        nameFilters: ["Audio (*.mp3 *.flac *.wav *.m4a *.ogg *.opus)", "All files (*)"]
        onAccepted: studio.loadSong(selectedFile)
    }

    FolderDialog {
        id: folderDialog
        title: "Add a footage folder"
        onAccepted: {
            studio.addFolder(selectedFolder)
            if (root.reopenFootage) footageDialog.open()
        }
        onRejected: if (root.reopenFootage) footageDialog.open()
    }

    // One place to manage footage: every folder the library scans, with the
    // remove button next to it. Adding lives here too, so there is never a
    // second, staler copy of this list anywhere in the UI.
    //
    // Plain Popup + Column on purpose: a Dialog with a custom contentItem gets
    // no content height from the layout and opens as an empty sliver.
    Popup {
        id: footageDialog
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

    Connections {
        target: studio
        function onSongDialogRequested() { songDialog.open() }
        function onFolderDialogRequested() { footageDialog.open() }
    }

    // ---- keybindings: space toggles, arrows nudge ------------------------
    Shortcut {
        sequence: "Space"
        onActivated: studio.playPause()
    }
    Shortcut {
        sequence: "Right"
        onActivated: studio.seek(Math.min(showCtl.duration, showCtl.position + 5))
    }
    Shortcut {
        sequence: "Left"
        onActivated: studio.seek(Math.max(0, showCtl.position - 5))
    }
    Shortcut {
        sequence: "Ctrl+G"
        // Same rule as the button: no new edit while the library is working.
        onActivated: if (!studio.busy) studio.generate()
    }
    Shortcut {
        sequence: "Escape"
        onActivated: studio.stopShow()
    }
}
