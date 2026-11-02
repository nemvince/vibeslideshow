import QtQuick
import QtQuick.Controls
import Vibes 1.0

// The one slider used everywhere: the Basic style's default is a light track with
// a white handle, which reads as a foreign object in this UI.
Slider {
    id: s
    property color fillColor: Theme.accent
    implicitHeight: 18

    background: Rectangle {
        x: s.leftPadding
        y: s.topPadding + s.availableHeight / 2 - height / 2
        width: s.availableWidth
        height: 3
        radius: 1.5
        color: Theme.line

        Rectangle {
            width: s.visualPosition * parent.width
            height: parent.height
            radius: 1.5
            color: s.fillColor
            // Eased when the model moves it (a preset, a reset); direct while the
            // user drags, where a lag would feel like a stuck handle.
            Behavior on width {
                enabled: !s.pressed
                NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic }
            }
        }
    }

    handle: Rectangle {
        x: s.leftPadding + s.visualPosition * (s.availableWidth - width)
        y: s.topPadding + s.availableHeight / 2 - height / 2
        width: s.pressed || s.hovered ? 14 : 12
        height: width
        radius: width / 2
        color: s.enabled ? Theme.ink : Theme.faint

        Behavior on width { NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic } }
    }
}
