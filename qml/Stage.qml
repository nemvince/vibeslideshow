import QtQuick
import QtQuick.Controls
import Vibes 1.0

// The stage: two mpv surfaces crossfaded by the show engine, plus the empty
// state that tells you what to do when there is nothing to play yet.
Item {
    id: stage
    clip: true

    // 1.0 = surface A fully visible, 0.0 = surface B. The show engine says which
    // surface an incoming cut landed on and how long the fade is; this is the
    // only thing animating, so nothing else has to know about the crossfade.
    property real mix: 1.0

    Rectangle {
        anchors.fill: parent
        color: "#000000"
    }

    // Two surfaces so a cut never has to blank the screen: the incoming one is
    // already decoded and paused on its first frame when the fade starts.
    // Both surfaces are ALWAYS visible and always rendered. This is not an
    // optimisation detail: Qt skips painting an item at opacity 0 or with
    // visible:false, and a skipped item never calls mpv's render API. Under
    // MPV_RENDER_PARAM_ADVANCED_CONTROL, mpv then stalls the hidden surface, so
    // "preloaded" would mean "loaded but stuck on its first frame" and every cut
    // would show a stale frame and need a corrective seek. The epsilon keeps the
    // hidden surface painting while being invisible to the eye.
    readonly property real hiddenOpacity: 0.0001
    MpvItem {
        id: surfA
        anchors.fill: parent
        opacity: stage.hiddenOpacity + (1.0 - 2.0 * stage.hiddenOpacity) * stage.mix
    }
    MpvItem {
        id: surfB
        anchors.fill: parent
        opacity: stage.hiddenOpacity + (1.0 - 2.0 * stage.hiddenOpacity) * (1.0 - stage.mix)
    }

    NumberAnimation {
        id: fade
        target: stage
        property: "mix"
        duration: 120
        easing.type: Easing.Linear
    }

    Connections {
        target: showCtl
        function onCrossfade(incomingIsA, fadeMs) {
            fade.stop()
            // A hard cut still takes one refresh to swap; the scene graph will not
            // composite a 0 ms property change.
            fade.duration = Math.max(1, fadeMs)
            fade.to = incomingIsA ? 1.0 : 0.0
            fade.start()
        }
    }

    Component.onCompleted: showCtl.attachSurfaces(surfA, surfB)

    // Locked: the library is being rebuilt or the song is still being analysed, so
    // nothing on the stage can be trusted — say so instead of showing a dead frame.
    readonly property bool locked: studio.busy && !studio.workspaceEnabled

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
        opacity: 0.82
        visible: stage.locked
    }
    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 60, 420)
        spacing: 8
        visible: stage.locked
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
        spacing: 12
        visible: !showCtl.ready && !stage.locked
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: studio.songPath.length ? "Ready when you are" : "Load a song to begin"
            color: Theme.ink
            font.pixelSize: 20
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Pick a look on the right, then press play."
            color: Theme.faint
            font.pixelSize: 13
        }
    }

    // Nothing playing: a quiet cue, so a black stage is not mistaken for a fault.
    Text {
        anchors.centerIn: parent
        visible: showCtl.ready && !showCtl.playing && !stage.locked
        text: "space to play"
        color: Theme.faint
        opacity: 0.7
        font.pixelSize: 11
        font.letterSpacing: 1.5
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
        visible: showCtl.ready && (timeline.selected >= 0 || showCtl.currentClip >= 0)
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
        visible: showCtl.buffering
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
