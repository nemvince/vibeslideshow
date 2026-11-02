import QtQuick
import QtQuick.Layouts
import Vibes 1.0

// Two to four named choices, all visible at once: the plainest alternative to a
// dropdown when every option is worth reading.
Item {
    id: seg
    property var options: []          // [{ label: "...", value: ... }]
    property var value: undefined
    signal selected(var value)

    implicitHeight: 26
    Layout.fillWidth: true

    Row {
        id: strip
        anchors.fill: parent
        spacing: 4

        Repeater {
            model: seg.options
            delegate: Rectangle {
                required property var modelData
                readonly property bool on: modelData.value === seg.value
                width: Math.max(0, (strip.width - (seg.options.length - 1) * strip.spacing)
                                   / Math.max(1, seg.options.length))
                height: strip.height
                radius: 6
                color: on ? Theme.panelHi : (hover.hovered ? Theme.panel : "transparent")
                border.color: on ? Theme.accent : Theme.line
                border.width: 1

                Behavior on color { ColorAnimation { duration: Theme.durFast } }
                Behavior on border.color { ColorAnimation { duration: Theme.durFast } }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 8
                    horizontalAlignment: Text.AlignHCenter
                    text: modelData.label
                    color: on ? Theme.ink : Theme.dim
                    Behavior on color { ColorAnimation { duration: Theme.durFast } }
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                HoverHandler { id: hover }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: seg.selected(modelData.value)
                }
            }
        }
    }
}
