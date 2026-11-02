import QtQuick
import QtQuick.Controls
import Vibes 1.0

// Toolbar button: a glyph with a tooltip, so the editing row costs one line of
// height instead of seven button labels.
Rectangle {
    id: btn
    property string glyph: ""
    property string tip: ""
    signal clicked()

    width: 26
    height: 24
    radius: 5
    color: mouse.containsMouse && enabled ? Theme.panelHi : "transparent"
    opacity: enabled ? 1 : 0.35

    Behavior on color { ColorAnimation { duration: Theme.durFast } }

    Text {
        anchors.centerIn: parent
        text: btn.glyph
        color: mouse.pressed && btn.enabled ? Theme.accent : Theme.ink
        font.pixelSize: 14
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: btn.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: btn.clicked()
    }
    ToolTip.visible: mouse.containsMouse && btn.tip.length > 0 && btn.enabled
    ToolTip.text: btn.tip
    ToolTip.delay: 400
}
