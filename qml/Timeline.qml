import QtQuick
import QtQuick.Controls
import Vibes 1.0

// The edit, as a timeline: a time ruler, segments proportional to their length, a
// playhead over them, and the whole surface is the seek control. This replaces
// the separate scrubber and segment strip — one thing you look at and scrub.
Item {
    id: tl
    implicitHeight: 108

    // Prefer the player's own duration, but never depend on it: while mpv is still
    // opening the file it reports 0, and every segment would be screen-wide.
    readonly property real duration: {
        if (showCtl.duration > 0) return showCtl.duration
        if (timeline.songDuration > 0) return timeline.songDuration
        return 1
    }
    readonly property real progressFrac: Math.max(0, Math.min(1, showCtl.position / duration))
    readonly property int rulerHeight: 18

    function seekTo(x) {
        const frac = Math.max(0, Math.min(1, x / Math.max(1, width)))
        studio.seek(frac * duration)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
        radius: Theme.radius
        border.color: Theme.line
        clip: true

        // ---- time ruler ----
        Item {
            id: ruler
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: tl.rulerHeight

            Repeater {
                model: 7
                delegate: Item {
                    required property int index
                    x: index / 6 * ruler.width
                    width: 1
                    height: ruler.height

                    Rectangle { width: 1; height: 5; y: ruler.height - height; color: Theme.line }
                    Text {
                        visible: index > 0 && index < 6
                        x: 4
                        text: studio.formatTime(index / 6 * tl.duration)
                        color: Theme.faint
                        font.pixelSize: 9
                        font.family: "monospace"
                    }
                }
            }

            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: Theme.line
            }
        }

        // ---- segments ----
        Row {
            id: track
            anchors {
                left: parent.left
                right: parent.right
                top: ruler.bottom
                bottom: parent.bottom
            }
            anchors.leftMargin: 1
            anchors.rightMargin: 1
            anchors.bottomMargin: 1
            spacing: 0

            Repeater {
                model: timeline
                delegate: Item {
                    id: cell
                    required property int index
                    required property var model
                    height: track.height
                    width: Math.max(1, Math.min(track.width, model.segLen / tl.duration * track.width))

                    readonly property bool showThumb: width >= 32

                    Rectangle {
                        anchors.fill: parent
                        color: cell.model.segSelected ? "#2c3138" : Theme.panelHi
                        clip: true

                        Image {
                            anchors.fill: parent
                            visible: cell.showThumb
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true
                            source: cell.model.segThumb && cell.showThumb ? "file://" + cell.model.segThumb : ""
                            opacity: 0.8
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: cell.model.segColor
                            opacity: cell.model.segThumb && cell.showThumb ? 0.15 : 0.5
                        }
                        // A hairline between cuts: a dense edit still reads as
                        // separate shots instead of a smear.
                        Rectangle {
                            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                            width: 1
                            color: Theme.bg
                        }
                    }

                    // Selection: top and bottom rules, so a thin segment cannot
                    // lose its highlight.
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; top: parent.top }
                        height: 2
                        color: Theme.accent
                        visible: cell.model.segSelected
                    }
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 2
                        color: Theme.accent
                        visible: cell.model.segSelected
                    }
                }
            }
        }

        // ---- playhead ----
        Item {
            x: Math.max(0, Math.min(tl.width - width, tl.progressFrac * tl.width - width / 2))
            width: 11
            height: parent.height
            visible: timeline.count > 0

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 2
                height: parent.height
                color: Theme.accent
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 9
                height: 7
                radius: 2
                color: Theme.accent
            }
        }

        // ---- scrubbing ----
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onPressed: (m) => tl.seekTo(m.x)
            onPositionChanged: (m) => { if (pressed) tl.seekTo(m.x) }
            onReleased: (m) => {
                const t = Math.max(0, Math.min(1, m.x / Math.max(1, tl.width))) * tl.duration
                const idx = timeline.indexAt(t)
                if (idx >= 0) timeline.selected = idx
            }
        }

        Text {
            anchors.centerIn: parent
            visible: timeline.count === 0
            text: studio.busy ? studio.progressLabel : "Pick a song and a look, then press Generate"
            color: Theme.faint
            font.pixelSize: 12
        }
    }
}
