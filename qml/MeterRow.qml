import QtQuick
import QtQuick.Layouts
import Vibes 1.0

// One measured property of the song: a bar, a percentage, and the word a person
// would use for it.
ColumnLayout {
    id: meter
    Layout.fillWidth: true
    spacing: 3

    property string label: ""
    property real value: 0
    property string word: ""
    property color fill: Theme.accent

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: meter.label
            color: Theme.ink
            font.pixelSize: 11
        }
        Text {
            text: meter.word + " · " + Math.round(Math.max(0, Math.min(1, meter.value)) * 100) + "%"
            color: Theme.dim
            font.pixelSize: 10
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 4
        radius: 2
        color: Theme.line

        Rectangle {
            width: parent.width * Math.max(0, Math.min(1, meter.value))
            height: parent.height
            radius: 2
            color: meter.fill
            Behavior on width { NumberAnimation { duration: Theme.dur; easing.type: Easing.OutCubic } }
        }
    }
}
