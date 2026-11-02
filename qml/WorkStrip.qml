import QtQuick
import QtQuick.Layouts
import Vibes 1.0

// The one place progress is reported. It sits between the top bar and the
// workspace, and takes no room at all when the app is idle, so nothing reflows
// when a pass starts: it grows, says what is happening, and folds away.
Item {
    id: strip
    property bool active: false
    property string title: ""
    property string detail: ""
    // 0..1, or negative while the pass has no total to measure against.
    property real value: -1

    implicitHeight: active ? 44 : 0
    clip: true
    visible: implicitHeight > 0.5

    Behavior on implicitHeight { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.fill: parent
        color: Theme.panelHi
    }

    // Hidden the instant the pass ends: the strip's own fold animation would
    // otherwise leave the dot hanging in a two-pixel sliver on the way out.
    RowLayout {
        visible: strip.active
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            leftMargin: Theme.pad
            rightMargin: Theme.pad
            topMargin: 10
        }
        spacing: 10

        // A slow pulse: the same "still working" cue as the bar, readable even
        // when no number is known yet.
        Rectangle {
            width: 7
            height: 7
            radius: 3.5
            color: Theme.accent
            SequentialAnimation on opacity {
                running: strip.active
                loops: Animation.Infinite
                NumberAnimation { from: 1; to: 0.3; duration: 700 }
                NumberAnimation { from: 0.3; to: 1; duration: 700 }
            }
        }
        Text {
            text: strip.title
            color: Theme.ink
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: strip.detail
            color: Theme.dim
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        Text {
            visible: strip.active && strip.value >= 0
            text: Math.round(Math.max(0, Math.min(1, strip.value)) * 100) + "%"
            color: Theme.faint
            font.pixelSize: 11
            font.family: "monospace"
        }
    }

    ProgressLine {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 3
        active: strip.active
        value: strip.value
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: Theme.line
    }
}
