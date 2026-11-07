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
    title: studio.documentPath.length ? studio.documentName + " — vibeslideshow" : "vibeslideshow"
    color: palette.window

    // Set while a native picker is open on top of one of the popups, so that popup
    // comes back afterwards instead of dropping the user out of the flow.
    property bool reopenFootage: false
    property bool reopenWelcome: false

    function reopenWhichever() {
        if (reopenFootage) {
            reopenFootage = false
            projectDialog.open()
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
        height: 52
        color: Theme.panel

        RowLayout {
            anchors { fill: parent; leftMargin: Theme.pad; rightMargin: Theme.pad }
            spacing: 12

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
                text: "File…"
                onClicked: fileMenu.open()
            }
            SmallButton {
                text: "Song…"
                onClicked: studio.openSongDialog()
            }
            SmallButton {
                text: "Project…"
                onClicked: projectDialog.open()
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

        // Right: the settings sidebar, collapsible.
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
            anchors { fill: parent; margins: 10 }
            spacing: 6

            // One control row: transport, edit actions, volume, Generate. Seeking
            // happens on the timeline itself.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    id: playBtn
                    implicitWidth: 30; implicitHeight: 30; radius: 15
                    // Locked while the library or the song is being (re)built:
                    // there is no valid edit to start or stop in that window, and
                    // nothing to play before a song has been read.
                    property bool usable: studio.workspaceEnabled && studio.songAnalysed
                    opacity: usable ? 1 : 0.45
                    color: showCtl.playing ? Theme.accent
                                           : (playHover.hovered ? "#262a2f" : Theme.panelHi)
                    border.color: showCtl.playing ? Theme.accent : Theme.line
                    Behavior on color { ColorAnimation { duration: Theme.durFast } }
                    HoverHandler { id: playHover }
                    Text {
                        anchors.centerIn: parent
                        text: showCtl.playing ? "\u275A\u275A" : "\u25B6"
                        color: Theme.ink
                        font.pixelSize: 11
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

                // History sits left of the edit actions: it is about the edit as a
                // whole, not about the selected shot.
                IconButton {
                    glyph: "\u21B6"
                    tip: "Undo (Ctrl+Z)"
                    enabled: studio.workspaceEnabled && timeline.canUndo
                    onClicked: timeline.undo()
                }
                IconButton {
                    glyph: "\u21B7"
                    tip: "Redo (Ctrl+Shift+Z)"
                    enabled: studio.workspaceEnabled && timeline.canRedo
                    onClicked: timeline.redo()
                }

                // Edit actions come from the command table, so the menu, these
                // buttons and the keys cannot disagree about what exists. They all
                // need a settled library: a rescan renumbers clip ids under the edit.
                Repeater {
                    model: timeline.commands.filter(function(c) { return c.bar })
                    delegate: IconButton {
                        required property var modelData
                        glyph: modelData.glyph
                        tip: modelData.label + (modelData.keys.length ? "  ·  " + modelData.keys : "")
                        enabled: studio.workspaceEnabled && root.canUse(modelData.needs)
                        onClicked: timeline.runCommand(modelData.id)
                    }
                }

                Text {
                    text: timeline.selected >= 0 ? timeline.selectionInfo : timeline.stats
                    color: Theme.faint
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                }

                Rectangle { Layout.preferredWidth: 1; implicitHeight: 18; color: Theme.line; Layout.leftMargin: 2; Layout.rightMargin: 2 }

                // Zoom: 1× fits the whole song, further in the strip scrolls and the
                // segments get wide enough to show their previews.
                IconButton {
                    glyph: "\u2212"
                    tip: "Zoom out (Ctrl+scroll over the timeline)"
                    enabled: tl.zoom > 1.001
                    onClicked: tl.zoomOut()
                }
                Text {
                    Layout.preferredWidth: 30
                    horizontalAlignment: Text.AlignHCenter
                    text: tl.zoom.toFixed(1) + "\u00D7"
                    color: tl.zoomed ? Theme.ink : Theme.faint
                    font.pixelSize: 10
                    font.family: "monospace"
                }
                IconButton {
                    glyph: "+"
                    tip: "Zoom in (Ctrl+scroll over the timeline)"
                    enabled: tl.zoom < tl.maxZoom
                    onClicked: tl.zoomIn()
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
                    accent: enabled
                    enabled: !studio.busy && studio.songAnalysed
                    onClicked: studio.generate()
                }
                // Rendering to a file needs an edit and a song: there is nothing to
                // encode without both.
                SmallButton {
                    text: "Export…"
                    flat: true
                    enabled: timeline.count > 0 && studio.songPath.length > 0 && !studio.exporting
                    onClicked: exportDialog.open()
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
            root.reopenWelcome = false   // the scan and the analysis are the show now
            studio.loadSong(selectedFile)
            root.reopenWhichever()
        }
        onRejected: root.reopenWhichever()
    }

    FolderDialog {
        id: folderDialog
        objectName: "folderDialog"
        title: "Import a footage folder"
        onAccepted: {
            root.reopenWelcome = false
            studio.importUrls([selectedFolder])
            root.reopenWhichever()
        }
        onRejected: root.reopenWhichever()
    }

    FileDialog {
        id: footageFilesDialog
        objectName: "footageFilesDialog"
        title: "Import clips"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Video (*.mp4 *.webm *.mov *.mkv *.m4v)", "All files (*)"]
        onAccepted: {
            root.reopenWelcome = false
            studio.importUrls(selectedFiles)
            root.reopenWhichever()
        }
        onRejected: root.reopenWhichever()
    }

    // One place to manage the project's footage: every folder and loose file the
    // library indexes, with a remove button next to it. Importing lives here too,
    // so there is never a second, staler copy of this list anywhere in the UI.
    //
    // Plain Popup + Column on purpose: a Dialog with a custom contentItem gets
    // no content height from the layout and opens as an empty sliver.
    Popup {
        id: projectDialog
        objectName: "projectDialog"
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width: 560
        padding: 18
        closePolicy: Popup.CloseOnEscape

        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.dur } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120 } }

        background: Rectangle {
            color: Theme.panel
            radius: Theme.radius
            border.color: Theme.line
        }

        contentItem: Column {
            id: footageCol
            spacing: 10

            Text {
                text: "Project footage"
                color: Theme.ink
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            Column {
                id: dirList
                width: footageCol.width
                spacing: 6

                Repeater {
                    model: studio.sources
                    delegate: Item {
                        id: srcRow
                        objectName: "sourceRow"
                        required property var modelData
                        width: dirList.width
                        height: 40

                        Rectangle {
                            anchors {
                                left: parent.left
                                right: srcRemove.left
                                rightMargin: 8
                                top: parent.top
                                bottom: parent.bottom
                            }
                            radius: 6
                            color: Theme.bg
                            border.color: Theme.line

                            Column {
                                anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                                topPadding: 5
                                spacing: 1
                                Text {
                                    width: parent.width
                                    text: (srcRow.modelData.isDir ? "Folder · " : "File · ")
                                          + srcRow.modelData.name
                                    color: Theme.ink
                                    font.pixelSize: 12
                                    elide: Text.ElideMiddle
                                }
                                Text {
                                    width: parent.width
                                    text: srcRow.modelData.path
                                    color: Theme.faint
                                    font.pixelSize: 10
                                    elide: Text.ElideMiddle
                                }
                            }
                        }
                        SmallButton {
                            id: srcRemove
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            text: "Remove"
                            onClicked: studio.removeSource(srcRow.modelData.path)
                        }
                    }
                }
            }

            Text {
                width: footageCol.width
                visible: studio.sources.length === 0
                text: "Nothing imported yet."
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
                        text: "Files…"
                        onClicked: {
                            root.reopenFootage = true
                            projectDialog.close()
                            footageFilesDialog.open()
                        }
                    }
                    SmallButton {
                        text: "Folder…"
                        onClicked: {
                            root.reopenFootage = true
                            projectDialog.close()
                            folderDialog.open()
                        }
                    }
                    SmallButton {
                        text: "Rescan"
                        enabled: studio.sources.length > 0
                        onClicked: studio.startIndexing()
                    }
                    SmallButton {
                        text: "Done"
                        accent: true
                        onClicked: {
                            root.reopenFootage = false
                            projectDialog.close()
                        }
                    }
                }
            }
        }
    }

    // Projects. A popup rather than a strip of its own: three commands do not
    // deserve permanent space, and the top bar stays two buttons wide.
    Popup {
        id: fileMenu
        objectName: "fileMenu"
        parent: topBar
        x: Math.max(Theme.pad, topBar.width - width - Theme.pad)
        y: topBar.height + 4
        // The rows size themselves to the popup, so the popup has to have a width:
        // a content item cannot measure itself from children that follow it.
        width: 190
        padding: 6
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durFast } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }

        background: Rectangle {
            color: Theme.panelHi
            radius: 10
            border.color: Theme.line
        }

        contentItem: Column {
            id: fileCol
            spacing: 2
            SmallButton {
                width: fileCol.width
                text: "New project"
                onClicked: {
                    fileMenu.close()
                    studio.newDocument()
                }
            }
            SmallButton {
                width: fileCol.width
                text: "Open…"
                onClicked: {
                    fileMenu.close()
                    studio.openDocumentDialog()
                }
            }
            SmallButton {
                width: fileCol.width
                text: "Save a copy…"
                onClicked: {
                    fileMenu.close()
                    studio.saveAsDialog()
                }
            }
        }
    }

    // The pickers hand Studio a QUrl, not a string: only toLocalFile() decodes
    // the percent-escapes, and every folder in a project's path may have spaces.
    FileDialog {
        id: documentOpenDialog
        objectName: "documentOpenDialog"
        title: "Open a project"
        fileMode: FileDialog.OpenFile
        nameFilters: ["vibeslideshow project (*.vibes)", "All files (*)"]
        onAccepted: studio.openDocument(selectedFile)
        onRejected: root.reopenWhichever()
    }

    FileDialog {
        id: documentSaveDialog
        objectName: "documentSaveDialog"
        title: "Save a copy"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "vibes"
        nameFilters: ["vibeslideshow project (*.vibes)"]
        onAccepted: studio.saveDocumentTo(selectedFile)
        onRejected: root.reopenWhichever()
    }

    // Rendering to a file. Same shape as the project popup: a path, the quality,
    // the progress of the run, and the two things the user can do about it.
    Popup {
        id: exportDialog
        objectName: "exportDialog"
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width: 540
        padding: 18
        closePolicy: Popup.CloseOnEscape
        // 0 draft, 1 balanced, 2 high — starts at the Render tool's setting and is
        // remembered there when this export runs.
        property int quality: studio.params.exportQuality

        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.dur } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120 } }

        background: Rectangle {
            color: Theme.panel
            radius: Theme.radius
            border.color: Theme.line
        }

        contentItem: Column {
            id: expCol
            spacing: 12

            Text {
                text: "Export the film"
                color: Theme.ink
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            Row {
                width: expCol.width
                spacing: 8

                Rectangle {
                    width: parent.width - exportChoose.width - parent.spacing
                    height: 26
                    radius: 6
                    color: Theme.bg
                    border.color: Theme.line

                    TextField {
                        id: exportPathField
                        objectName: "exportPath"
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        topPadding: 0
                        bottomPadding: 0
                        readOnly: true
                        placeholderText: "Choose where to save it…"
                        color: Theme.ink
                        placeholderTextColor: Theme.faint
                        font.pixelSize: 11
                        verticalAlignment: TextInput.AlignVCenter
                        background: null
                    }
                }
                SmallButton {
                    id: exportChoose
                    text: "Choose…"
                    onClicked: exportFileDialog.open()
                }
            }

            Segmented {
                objectName: "exportQuality"
                width: expCol.width
                options: [
                    { label: "Draft", value: 0 },
                    { label: "Balanced", value: 1 },
                    { label: "High", value: 2 }
                ]
                value: exportDialog.quality
                onSelected: (v) => exportDialog.quality = v
            }

            Text {
                width: expCol.width
                text: studio.params.exportW + " × " + studio.params.exportH + " · "
                      + Math.round(studio.params.exportFps) + " fps · "
                      + (studio.params.moveOn === true ? "shots move" : "still frames")
                color: Theme.faint
                font.pixelSize: 10
                elide: Text.ElideRight
            }

            ProgressLine {
                width: expCol.width
                visible: studio.exporting
                active: studio.exporting
                value: studio.exportProgress
            }

            Text {
                width: expCol.width
                text: studio.exportStatus
                visible: text.length > 0
                color: Theme.dim
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Item {
                width: expCol.width
                height: 30

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    SmallButton {
                        objectName: "exportCancel"
                        text: "Cancel export"
                        visible: studio.exporting
                        onClicked: studio.cancelExport()
                    }
                    SmallButton {
                        objectName: "exportGo"
                        text: "Export"
                        accent: enabled
                        enabled: exportPathField.text.length > 0 && !studio.exporting
                        onClicked: studio.exportVideo(exportPathField.text, exportDialog.quality)
                    }
                    SmallButton {
                        objectName: "exportClose"
                        text: "Close"
                        flat: true
                        onClicked: exportDialog.close()
                    }
                }
            }
        }
    }

    FileDialog {
        id: exportFileDialog
        objectName: "exportFileDialog"
        title: "Export the film"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mp4"
        nameFilters: ["Video (*.mp4)"]
        onAccepted: {
            exportPathField.text = selectedFile
            exportDialog.open()
        }
        onRejected: exportDialog.open()
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
        onFilesRequested: {
            root.reopenWelcome = true
            welcome.close()
            footageFilesDialog.open()
        }
    }

    Component.onCompleted: welcome.open()

    Connections {
        target: studio
        function onSongDialogRequested() { songDialog.open() }
        function onFolderDialogRequested() { projectDialog.open() }
        function onFilesDialogRequested() { footageFilesDialog.open() }
        function onDocumentOpenRequested() { documentOpenDialog.open() }
        function onDocumentSaveRequested() { documentSaveDialog.open() }
        function onExportDialogRequested() { exportDialog.open() }
    }

    // ---- keybindings -----------------------------------------------------
    // True while a text control owns the keyboard. Every Shortcut below is off
    // then: a Shortcut fires before the focused item sees the key, so without this
    // typing an "s" in the bin's search would swap a shot and a space would start
    // the show.
    readonly property bool typingText: {
        const f = root.activeFocusItem
        if (!f) return false
        if (f instanceof TextInput || f instanceof TextEdit || f instanceof TextField || f instanceof TextArea)
            return true
        // A control's content item holds the focus in some styles, so the field
        // around it is what says whether this is text entry.
        const p = f.parent
        return !!p && (p instanceof TextField || p instanceof TextArea)
    }

    // What a command needs before it applies, as declared by its `needs` field.
    function canUse(needs) {
        if (needs === "selection") return timeline.selected >= 0
        if (needs === "timeline") return timeline.count > 0
        return true
    }

    // The transport keys. A Shortcut is matched before the focused item sees the
    // key, so each of these is switched off while a text field has the keyboard:
    // Space types a space in the bin's search, the arrows move its caret, Escape
    // clears it, and none of them reach the timeline.
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
        sequence: "Escape"
        enabled: !root.typingText
        onActivated: studio.stopShow()
    }
    Shortcut {
        sequence: "Ctrl+G"
        // Same rule as the button: no new edit while the library is working, and
        // none before the song has been analysed.
        onActivated: if (!studio.busy && studio.songAnalysed) studio.generate()
    }
    // Clip operations. The sequence text lives in the command table; these only
    // bind it to a window shortcut. Every one of those operations no-ops without a
    // selection, so there is nothing to enable or disable here beyond typing.
    Shortcut { sequence: timeline.commandKey("shorten");   enabled: !root.typingText; onActivated: timeline.runCommand("shorten") }
    Shortcut { sequence: timeline.commandKey("lengthen");  enabled: !root.typingText; onActivated: timeline.runCommand("lengthen") }
    Shortcut { sequence: timeline.commandKey("chop");      enabled: !root.typingText; onActivated: timeline.runCommand("chop") }
    Shortcut { sequence: timeline.commandKey("rerollIn");  enabled: !root.typingText; onActivated: timeline.runCommand("rerollIn") }
    Shortcut { sequence: timeline.commandKey("swap");      enabled: !root.typingText; onActivated: timeline.runCommand("swap") }
    Shortcut { sequence: timeline.commandKey("remove");    enabled: !root.typingText; onActivated: timeline.runCommand("remove") }
    Shortcut { sequence: timeline.commandKey("rerollAll"); enabled: !root.typingText; onActivated: timeline.runCommand("rerollAll") }
    Shortcut { sequence: timeline.commandKey("cutHard");   enabled: !root.typingText; onActivated: timeline.runCommand("cutHard") }
    Shortcut { sequence: timeline.commandKey("cutSofter"); enabled: !root.typingText; onActivated: timeline.runCommand("cutSofter") }
    Shortcut { sequence: timeline.commandKey("cutHarder"); enabled: !root.typingText; onActivated: timeline.runCommand("cutHarder") }
    // History. Plain Ctrl+Z and Ctrl+Shift+Z, standard everywhere; the edit's own
    // undo stack is what they drive.
    Shortcut { sequence: "Ctrl+Z";       enabled: !root.typingText && timeline.canUndo; onActivated: timeline.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; enabled: !root.typingText && timeline.canRedo; onActivated: timeline.redo() }
}
