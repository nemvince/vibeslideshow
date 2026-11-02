import QtQuick
import QtQuick.Controls
import Vibes 1.0

// The stage: the mpv surfaces the show engine crossfades, plus the empty state
// that tells you what to do when there is nothing to play yet.
Item {
    id: stage
    clip: true

    // Which surface the engine is showing, and how long it takes to get there: the
    // only thing animating, so nothing else has to know about the crossfade.
    property int frontIndex: 0
    property int fadeMs: 120

    // The preview is a 16:9 window: as large as the space allows, centred, with
    // the app's own background around it. Everything inside — mpv, the frame, the
    // crossfade — is this rectangle, so a 4:3 or a vertical clip is letterboxed by
    // mpv inside a frame that never changes shape and never pushes the chrome
    // around when the window is resized.
    readonly property real frameW: Math.floor(Math.min(width, height * 16.0 / 9.0))
    readonly property real frameH: Math.floor(frameW * 9.0 / 16.0)

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    Item {
        id: frame
        width: stage.frameW
        height: stage.frameH
        anchors.centerIn: parent

        Rectangle {
            anchors.fill: parent
            color: "#000000"
        }

        // The surfaces. Only one is on screen at a time; the rest are lookahead
        // slots the engine keeps loaded with the upcoming shots, so a cut is a
        // swap rather than a file open. That is what lets a quarter-second shot
        // land on time, and it is why there are more of them than the eye needs.
        //
        // All of them are ALWAYS visible and always rendered, at 1 or at
        // `hiddenOpacity`. This is not an optimisation detail: Qt skips painting
        // an item at opacity 0 or with visible:false, and a skipped item never
        // calls mpv's render API. Under MPV_RENDER_PARAM_ADVANCED_CONTROL, mpv
        // then stalls the hidden surface, so "preloaded" would mean "loaded but
        // stuck on its first frame" and every cut would need a corrective seek
        // before it could show anything.
        Item {
            id: videoLayer
            anchors.fill: parent

            Repeater {
                model: stage.surfaceCount
                MpvItem {
                    required property int index
                    anchors.fill: parent
                    opacity: index === stage.frontIndex ? stage.visibleOpacity : stage.hiddenOpacity
                    // The engine says how long the fade is; a hard cut still takes
                    // a frame or two, because the scene graph will not composite a
                    // 0 ms property change.
                    Behavior on opacity {
                        NumberAnimation { duration: stage.fadeMs; easing.type: Easing.Linear }
                    }
                }
            }
        }

        // A hairline so the picture has an edge against the app background.
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: Theme.line
            border.width: 1
        }
    }

    // How many surfaces the engine gets: one on screen plus three lookahead slots,
    // which at quarter-second cuts is three quarters of a second of runway for a
    // file to open and seek before the shot it holds is needed.
    readonly property int surfaceCount: 4
    // Neither end of the range is 0 or 1, and that is deliberate. Qt does not
    // paint an item at opacity 0 (so its mpv surface stalls), and it treats an
    // opacity-1 item as opaque and may cull whatever is painted underneath it —
    // which would stall every lookahead surface the moment the front one was the
    // topmost of the stack. 0.0001 is invisible, 0.9999 is not opaque.
    readonly property real hiddenOpacity: 0.0001
    readonly property real visibleOpacity: 1.0 - hiddenOpacity

    Connections {
        target: showCtl
        function onCrossfade(frontIndex, fadeMs) {
            stage.fadeMs = Math.max(1, fadeMs)
            stage.frontIndex = frontIndex
        }
    }

    Component.onCompleted: showCtl.attachSurfaces(videoLayer.children)

    // Locked: the library is being rebuilt or the song is still being analysed, so
    // nothing on the stage can be trusted — say so instead of showing a dead frame.
    readonly property bool locked: studio.busy && !studio.workspaceEnabled

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
        visible: opacity > 0.01
        opacity: stage.locked ? 0.82 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.dur } }
    }
    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 60, 420)
        spacing: 8
        visible: opacity > 0.01
        opacity: stage.locked ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.dur } }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: studio.phase
            color: Theme.ink
            font.pixelSize: 17
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            // The numbers stay in the top bar; this only says why the workspace
            // is not taking input.
            text: "The workspace comes back as soon as this finishes."
            color: Theme.faint
            font.pixelSize: 12
        }
    }

    // Empty state.
    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 60, 460)
        spacing: 10
        visible: opacity > 0.01
        opacity: (!showCtl.ready && !stage.locked) ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.dur } }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: studio.songPath.length ? "Ready when you are" : "Load a song to begin"
            color: Theme.ink
            font.pixelSize: 20
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            // One next step, not three: the empty stage is where a new user is
            // most likely to stall.
            text: !studio.songPath.length
                  ? "Song… in the top bar takes a track; the app finds the beat from there."
                  : studio.libraryCount === 0
                    ? "Import some clips into the bin on the left, then press Generate."
                    : "Press Generate for a first cut, or play to preview what is already there."
            color: Theme.faint
            font.pixelSize: 13
        }
    }

    // Nothing playing: a quiet cue, so a black stage is not mistaken for a fault.
    Text {
        anchors.centerIn: parent
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        visible: opacity > 0.01
        text: "space to play"
        color: Theme.faint
        opacity: (showCtl.ready && !showCtl.playing && !stage.locked) ? 0.7 : 0
        font.pixelSize: 11
        font.letterSpacing: 1.5
        Behavior on opacity { NumberAnimation { duration: Theme.dur } }
    }

    // Bottom-left overlay: where the show is, or which segment is selected.
    // Nothing is on screen before the first play and after a stop (cutIndex resets
    // to 0), so the badge would otherwise claim "cut 1 / 0".
    Rectangle {
        anchors { left: parent.left; bottom: parent.bottom; margins: 14 }
        width: info.implicitWidth + 20
        height: 28
        radius: 6
        color: "#99000000"
        visible: opacity > 0.01
        opacity: (showCtl.ready && (timeline.selected >= 0 || showCtl.currentClip >= 0)) ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
        Text {
            id: info
            anchors.centerIn: parent
            text: timeline.selected >= 0 ? timeline.selectionInfo
                                         : ("cut " + (showCtl.cutIndex + 1) + " / " + timeline.count)
            color: Theme.ink
            font.pixelSize: 11
        }
    }

    // The song keeps running while the picture waits for the file: say so, or a
    // frozen frame reads as a broken app rather than a slow drive.
    Rectangle {
        anchors { right: parent.right; bottom: parent.bottom; margins: 14 }
        width: bufferRow.implicitWidth + 22
        height: 28
        radius: 6
        color: "#99000000"
        visible: opacity > 0.01
        opacity: showCtl.buffering ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
        Row {
            id: bufferRow
            anchors.centerIn: parent
            spacing: 7
            Rectangle {
                width: 7; height: 7; radius: 3.5
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.accent
                SequentialAnimation on opacity {
                    running: showCtl.buffering
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 0.25; duration: 560 }
                    NumberAnimation { from: 0.25; to: 1; duration: 560 }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Buffering…"
                color: Theme.ink
                font.pixelSize: 11
            }
        }
    }
}
