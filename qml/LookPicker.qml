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

                    Text {
                        anchors { left: parent.left; leftMargin: 8; right: del.left; rightMargin: 6; verticalCenter: parent.verticalCenter }
                        text: row.modelData.name
                        color: row.modelData.active ? Theme.accent : Theme.ink
                        font.pixelSize: 12
                        elide: Text.ElideRight
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

                    // Active marker when there is no delete button to sit there.
                    Text {
                        anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        visible: !row.modelData.saved && row.modelData.active
                        text: "●"
                        color: Theme.accent
                        font.pixelSize: 8
                    }

                    HoverHandler { id: rowHover }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        // Do not steal clicks from the delete button.
                        onClicked: {
                            if (row.modelData.saved) studio.loadProfile(row.modelData.name)
                            else studio.applyPreset(row.modelData.name)
                            menu.close()
                        }
                    }
                }
            }
        }
    }
}
