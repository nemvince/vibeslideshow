import QtQuick
import QtQuick.Controls
import Vibes 1.0

// The one switch in the app: small, low contrast when off, amber with a light
// knob when on. Used by the filter list and by Advanced so both read the same.
Switch {
    id: sw
    implicitHeight: 12
    implicitWidth: 22

    indicator: Rectangle {
        implicitWidth: 22
        implicitHeight: 12
        radius: 6
        color: sw.checked ? Theme.accent : Theme.panelHi
        border.color: sw.checked ? Theme.accent : Theme.line

        Rectangle {
            width: 8; height: 8; radius: 4
            y: 2
            x: sw.checked ? parent.width - width - 2 : 2
            color: sw.checked ? Theme.ink : Theme.faint
            Behavior on x { NumberAnimation { duration: 110 } }
        }
    }
    contentItem: Item {}
}
