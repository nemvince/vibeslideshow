import QtQuick
import QtQuick.Controls
import Vibes 1.0

// The one slider used everywhere: the Basic style's default is a light track with
// a white handle, which reads as a foreign object in this UI.
Slider {
    id: s
    property color fillColor: Theme.accent
    implicitHeight: 20

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
        }
    }

    handle: Rectangle {
        x: s.leftPadding + s.visualPosition * (s.availableWidth - width)
        y: s.topPadding + s.availableHeight / 2 - height / 2
        width: 12
        height: 12
        radius: 6
        color: s.enabled ? Theme.ink : Theme.faint
    }
}
