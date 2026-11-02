import QtQuick
import QtQuick.Controls
import Vibes 1.0

// The capture-day picker: the days present in the library, as a themed dropdown.
Item {
    id: picker
    implicitHeight: 30
    property bool open: false
    signal chosen(string day)

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: hovered.hovered ? Theme.panelHi : Theme.bg
        border.color: picker.open ? Theme.accent : Theme.line

        Text {
            anchors { left: parent.left; leftMargin: 10; right: arrow.left; rightMargin: 6; verticalCenter: parent.verticalCenter }
            text: studio.filters.day.length ? studio.filters.day : (studio.days.length ? studio.days[0] : "no stamped days")
            color: Theme.ink
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        Text {
            id: arrow
            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
            text: "▾"
            color: Theme.dim
            font.pixelSize: 10
        }
        MouseArea {
            id: hovered
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                picker.open = !picker.open
                if (picker.open) menu.open()
                else menu.close()
            }
        }
    }

    Popup {
        id: menu
        // Same rule as the look picker: drop down, or up when there is no room.
        y: {
            const win = Overlay.overlay
            const below = picker.height + 4
            if (win) {
                const top = picker.mapToItem(null, 0, 0).y
                if (top + below + implicitHeight > win.height && top - implicitHeight - 4 > 0)
                    return -implicitHeight - 4
            }
            return below
        }
        width: picker.width
        padding: 4
        implicitHeight: Math.min(list.contentHeight + 8, 320)
        background: Rectangle { color: Theme.panelHi; radius: 8; border.color: Theme.line }
        onClosed: picker.open = false

        contentItem: ListView {
            id: list
            clip: true
            implicitHeight: contentHeight
            model: studio.days
            spacing: 1
            ScrollIndicator.vertical: ScrollIndicator { }

            delegate: Rectangle {
                id: row
                required property string modelData
                width: list.width
                height: 28
                radius: 5
                color: rowHover.hovered ? Theme.bg : "transparent"

                Text {
                    anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                    text: row.modelData
                    color: row.modelData === studio.filters.day ? Theme.accent : Theme.ink
                    font.pixelSize: 11
                }
                HoverHandler { id: rowHover }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        picker.chosen(row.modelData)
                        menu.close()
                    }
                }
            }
        }
    }
}
