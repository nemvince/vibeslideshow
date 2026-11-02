import QtQuick
import Vibes 1.0

// A thin progress line for long jobs. `value` is 0..1 for real progress, or
// negative when the job has not reported a total yet — then the line sweeps so it
// is obvious the app is working rather than stuck. It is always drawn while
// `active`, so the bar never flickers in and out between phases.
Item {
    id: bar
    property real value: -1
    property bool active: true
    implicitHeight: 4
    clip: true

    // Track: visible even at zero, so an idle-looking bar still reads as a bar.
    Rectangle {
        anchors.fill: parent
        color: Theme.panelHi
    }

    // Real progress: fills and eases between updates.
    Rectangle {
        id: fill
        height: parent.height
        x: 0
        visible: bar.active && bar.value >= 0.005
        color: Theme.accent
        width: Math.max(2, bar.width * Math.max(0, Math.min(1, bar.value)))
        Behavior on width { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
    }

    // No total yet: a marker that travels, permanently moving so the window never
    // looks frozen while the file system is being walked.
    Rectangle {
        id: sweep
        width: Math.max(48, bar.width * 0.22)
        height: parent.height
        radius: height / 2
        visible: bar.active && bar.value < 0.005
        color: Theme.accent

        SequentialAnimation on x {
            running: sweep.visible
            loops: Animation.Infinite
            NumberAnimation {
                from: -sweep.width * 0.2
                to: Math.max(0, bar.width - sweep.width * 0.8)
                duration: 1100
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                from: Math.max(0, bar.width - sweep.width * 0.8)
                to: -sweep.width * 0.2
                duration: 1100
                easing.type: Easing.InOutSine
            }
        }
    }
}
