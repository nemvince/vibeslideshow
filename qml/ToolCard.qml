import QtQuick
import QtQuick.Layouts
import Vibes 1.0

// One tool in the settings panel: a named card with a sentence saying what it
// does to the video, and the controls that do it. The card is the unit of the
// panel now — every control belongs to the tool it serves, so nothing is a knob
// floating on its own.
Rectangle {
    id: card
    property string title: ""
    property string hint: ""
    default property alias content: body.data

    Layout.fillWidth: true
    implicitHeight: inner.implicitHeight + 24
    radius: Theme.radius
    color: Theme.bg
    border.color: Theme.line
    border.width: 1

    ColumnLayout {
        id: inner
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
        spacing: 6

        Text {
            Layout.fillWidth: true
            text: card.title
            color: Theme.ink
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            visible: card.hint.length > 0
            text: card.hint
            color: Theme.dim
            font.pixelSize: 11
            lineHeight: 1.25
            wrapMode: Text.WordWrap
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 9
        }
    }
}
