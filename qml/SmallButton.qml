import QtQuick
import Vibes 1.0

Rectangle {
    id: btn
    property alias text: label.text
    property bool accent: false
    signal clicked()

    implicitWidth: label.implicitWidth + 22
    implicitHeight: 30
    radius: 7
    color: accent ? (mouse.containsMouse ? "#ffbe5c" : Theme.accent)
                  : (mouse.containsMouse ? Theme.panelHi : Theme.panel)
    border.color: accent ? "transparent" : Theme.line
    opacity: enabled ? 1 : 0.42

    Text {
        id: label
        anchors.centerIn: parent
        color: btn.accent ? "#141516" : Theme.ink
        font.pixelSize: 12
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: btn.clicked()
    }
}
