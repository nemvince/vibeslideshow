import QtQuick
import QtQuick.Controls
import Vibes 1.0

// A preset picker that stays one row tall. A themed popup list of the built-in
// looks, with the active one marked; the prose stays as a caption, not as cards.
Item {
    id: picker
    implicitHeight: 32
    implicitWidth: 200

    property bool open: false
    property var items: studio.presets

    function activeName() {
        for (let i = 0; i < items.length; i++)
            if (items[i].active) return items[i].name
        return studio.activePreset.length ? studio.activePreset : "Custom"
    }

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: mouse.containsMouse ? Theme.panelHi : Theme.bg
        border.color: picker.open ? Theme.accent : Theme.line
        border.width: 1

        Text {
            anchors { left: parent.left; leftMargin: 10; right: chevron.left; rightMargin: 6; verticalCenter: parent.verticalCenter }
            text: picker.activeName()
            color: Theme.ink
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        Text {
            id: chevron
            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
            text: "▾"
            color: Theme.dim
            font.pixelSize: 10
        }
        MouseArea {
            id: mouse
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
        y: picker.height + 4
        width: picker.width
        padding: 4
        // Never taller than the window; the list scrolls instead.
        implicitHeight: Math.min(list.contentHeight + 8, 340)
        background: Rectangle {
            color: Theme.panelHi
            radius: 8
            border.color: Theme.line
        }
        onClosed: picker.open = false

        contentItem: ListView {
            id: list
            clip: true
            implicitHeight: contentHeight
            model: picker.items
            spacing: 1
            ScrollIndicator.vertical: ScrollIndicator { }

            delegate: Rectangle {
                id: row
                required property var modelData
                width: list.width
                height: 30
                radius: 5
                color: rowHover.hovered ? Theme.bg : "transparent"

                Text {
                    anchors { left: parent.left; leftMargin: 8; right: tick.left; rightMargin: 6; verticalCenter: parent.verticalCenter }
                    text: row.modelData.name
                    color: row.modelData.active ? Theme.accent : Theme.ink
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Text {
                    id: tick
                    anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                    text: row.modelData.active ? "●" : ""
                    color: Theme.accent
                    font.pixelSize: 8
                }
                HoverHandler { id: rowHover }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        studio.applyPreset(row.modelData.name)
                        menu.close()
                    }
                }
            }
        }
    }
}
