import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vibes 1.0

// One collapsible group in the settings panel: a title row you can fold away,
// and a body of controls. Keeps the panel readable when it holds looks, filters,
// saved looks and the raw parameters at once.
ColumnLayout {
    id: section
    property string title: ""
    property bool expanded: true

    default property alias content: body.data

    Layout.fillWidth: true
    spacing: 0

    Item {
        Layout.fillWidth: true
        implicitHeight: 32

        Text {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            text: section.title.toUpperCase()
            color: Theme.faint
            font.pixelSize: 11
            font.letterSpacing: 0.8
        }
        Text {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text: section.expanded ? "–" : "+"
            color: Theme.faint
            font.pixelSize: 14
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: section.expanded = !section.expanded
        }
    }

    ColumnLayout {
        id: body
        Layout.fillWidth: true
        visible: section.expanded
        spacing: 8
    }
}
