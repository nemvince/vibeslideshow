import QtQuick
import QtQuick.Controls
import Vibes 1.0

// The preset manager: one row tall when closed, and a list of every look when
// open — built-in presets, then saved looks with a delete button each.
Item {
    id: picker
    implicitHeight: 32
    implicitWidth: 200

    property bool open: false
    property var items: studio.looks

    function activeName() {
        for (let i = 0; i < items.length; i++)
            if (items[i].active) return items[i].name
        return "Custom"
    }

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: mouse.containsMouse ? Theme.panelHi : Theme.bg
        border.color: picker.open ? Theme.accent : Theme.line
        border.width: 1

        Behavior on color { ColorAnimation { duration: Theme.durFast } }
        Behavior on border.color { ColorAnimation { duration: Theme.durFast } }

        Text {
            objectName: "lookName"
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
        // Below the picker, unless the picker sits near the bottom of the window —
        // the settings panel scrolls, so it can — in which case the list opens
        // upwards instead of being clipped off the bottom edge.
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

        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durFast } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }
        implicitHeight: Math.min(list.contentHeight + 8, 360)
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

            delegate: Item {
                id: row
                required property int index
                required property var modelData
                width: list.width
                // Section label above the first saved look, else one row.
                readonly property bool firstSaved: modelData.saved && (index === 0 || !picker.items[index - 1].saved)
                height: row.firstSaved ? 48 : 30

                Rectangle {
                    visible: row.firstSaved
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    height: 18
                    color: "transparent"
                    Text {
                        anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                        text: "SAVED"
                        color: Theme.faint
                        font.pixelSize: 9
                        font.letterSpacing: 1
                    }
                }

                Rectangle {
                    id: hit
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 30
                    radius: 5
                    color: rowHover.hovered ? Theme.bg : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.durFast } }

                    Text {
                        anchors { left: parent.left; leftMargin: 8; right: del.left; rightMargin: 6; verticalCenter: parent.verticalCenter }
                        text: row.modelData.name
                        color: row.modelData.active ? Theme.accent : Theme.ink
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    // Active marker when there is no delete button to sit there.
                    Text {
                        anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        visible: !row.modelData.saved && row.modelData.active
                        text: "●"
                        color: Theme.accent
                        font.pixelSize: 8
                    }

                    HoverHandler { id: rowHover }

                    // Picking the look. Declared BEFORE the delete button: later
                    // siblings stack on top, so the ✕ keeps its own clicks instead of
                    // the row swallowing them and loading the look it just deleted.
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (row.modelData.saved) studio.loadProfile(row.modelData.name)
                            else studio.applyPreset(row.modelData.name)
                            menu.close()
                        }
                    }

                    // Delete for saved looks only; presets are built in.
                    Item {
                        id: del
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        width: row.modelData.saved ? 24 : 12
                        height: 24
                        visible: row.modelData.saved
                        Text {
                            anchors.centerIn: parent
                            text: "✕"
                            color: delHover.hovered ? Theme.ink : Theme.faint
                            font.pixelSize: 10
                        }
                        HoverHandler { id: delHover }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: studio.deleteProfile(row.modelData.name)
                        }
                    }
                }
            }
        }
    }
}
