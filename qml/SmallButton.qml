import QtQuick
import QtQuick.Controls
import Vibes 1.0

// The app's button: a small rounded slab with a label. Hover and press are the
// only animation it needs, and both are short enough to feel like feedback
// rather than an effect.
Rectangle {
    id: btn
    property alias text: label.text
    property bool accent: false
    // Flat buttons are for secondary actions that should not compete for space.
    property bool flat: false
    signal clicked()

    implicitWidth: label.implicitWidth + (flat ? 14 : 20)
    implicitHeight: flat ? 22 : 26
    radius: 6
    scale: mouse.pressed && enabled ? 0.97 : 1
    color: {
        if (flat) return mouse.containsMouse ? Theme.panelHi : "transparent"
        if (accent) return mouse.containsMouse ? (mouse.pressed ? "#e0972c" : "#ffbe5c")
                                               : Theme.accent
        return mouse.pressed ? Theme.bg : (mouse.containsMouse ? Theme.panelHi : Theme.panel)
    }
    border.color: accent && !flat ? "transparent" : flat ? "transparent" : Theme.line
    opacity: enabled ? 1 : 0.42

    Behavior on color { ColorAnimation { duration: Theme.durFast } }
    Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }

    Text {
        id: label
        anchors.centerIn: parent
        color: btn.accent ? "#141516" : btn.flat ? Theme.dim : Theme.ink
        font.pixelSize: btn.flat ? 11 : 12
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: btn.clicked()
    }
}
