import QtQuick
import QtQuick.Controls
import Vibes 1.0

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
    color: flat ? (mouse.containsMouse ? Theme.panelHi : "transparent")
         : accent ? (mouse.containsMouse ? "#ffbe5c" : Theme.accent)
                  : (mouse.containsMouse ? Theme.panelHi : Theme.panel)
    border.color: accent && !flat ? "transparent" : flat ? "transparent" : Theme.line
    opacity: enabled ? 1 : 0.42

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
