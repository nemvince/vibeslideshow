import QtQuick
import QtQuick.Controls
import Vibes 1.0

// The clip menu: every edit operation on the shot under the pointer, with the
// glyph it wears in the toolbar and the key that does the same thing. The rows
// come from the command table, so this file knows nothing about what the
// operations are — adding one to TimelineModel puts it here for free.
//
// A plain Popup rather than a Controls Menu: the menu is hand-styled like every
// other surface in the app, and Menu's internal content item makes per-group
// separators and custom rows awkward.
Popup {
    id: menu
    objectName: "clipMenu"
    // Set by the owner before open(): which shot the menu acts on.
    property int segment: -1
    // The id of the command the user chose. The owner decides what to do with it.
    signal picked(string id)
    // Row index under the keyboard, -1 when nothing is highlighted.
    property int active: -1

    width: 268
    implicitHeight: col.implicitHeight + 12
    padding: 6
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Theme.panelHi
        radius: 10
        border.color: Theme.line
    }

    // What a command needs before it applies, as declared by its `needs` field.
    function enabledFor(cmd) {
        if (cmd.needs === "selection") return timeline.selected >= 0
        if (cmd.needs === "timeline") return timeline.count > 0
        return true
    }
    function firstEnabled() {
        for (let i = 0; i < timeline.commands.length; i++)
            if (enabledFor(timeline.commands[i])) return i
        return -1
    }
    // Move the highlight to the next row that can actually run, wrapping around.
    // A disabled row is never a landing spot, so the keyboard cannot run one by
    // pressing return twice.
    function step(delta) {
        const n = timeline.commands.length
        if (n === 0) return
        let i = active
        for (let k = 0; k < n; k++) {
            i = (i + delta + n) % n
            if (enabledFor(timeline.commands[i])) { active = i; return }
        }
    }
    function run(id) {
        if (!id) return
        for (let i = 0; i < timeline.commands.length; i++)
            if (timeline.commands[i].id === id && !enabledFor(timeline.commands[i])) return
        close()
        picked(id)
    }

    onOpened: {
        active = firstEnabled()
        col.forceActiveFocus()
    }
    onClosed: active = -1

    contentItem: Column {
        id: col
        focus: true

        Keys.onUpPressed: menu.step(-1)
        Keys.onDownPressed: menu.step(1)
        Keys.onReturnPressed: menu.run(menu.active >= 0 ? timeline.commands[menu.active].id : "")
        Keys.onEnterPressed: menu.run(menu.active >= 0 ? timeline.commands[menu.active].id : "")

        Repeater {
            model: timeline.commands
            delegate: Item {
                id: row
                required property int index
                required property var modelData
                // The harness (and anything else looking for a specific row) finds
                // rows by this rather than by position.
                property string rowId: modelData.id

                height: 30
                width: col.width
                opacity: menu.enabledFor(modelData) ? 1 : 0.4

                // A group change is a separator: timing, clip, destructive, all.
                Rectangle {
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                    }
                    anchors.topMargin: -3
                    height: 1
                    color: Theme.line
                    visible: row.index > 0 &&
                             timeline.commands[row.index - 1].group !== row.modelData.group
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: 1
                    radius: 6
                    color: menu.active === row.index ? Theme.bg
                                                     : (rowHover.hovered ? Theme.bg : "transparent")
                }

                Text {
                    id: glyph
                    x: 10
                    width: 20
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignHCenter
                    text: row.modelData.glyph
                    font.pixelSize: 13
                    color: row.modelData.group === "destructive" ? Theme.bad : Theme.ink
                }

                Text {
                    anchors {
                        left: glyph.right
                        leftMargin: 8
                        right: keysText.left
                        rightMargin: 12
                        verticalCenter: parent.verticalCenter
                    }
                    text: row.modelData.label
                    font.pixelSize: 11
                    color: Theme.ink
                    elide: Text.ElideRight
                }

                Text {
                    id: keysText
                    anchors {
                        right: parent.right
                        rightMargin: 10
                        verticalCenter: parent.verticalCenter
                    }
                    text: row.modelData.keys
                    font.pixelSize: 9
                    font.family: "monospace"
                    color: Theme.faint
                }

                HoverHandler { id: rowHover }

                MouseArea {
                    anchors.fill: parent
                    enabled: menu.enabledFor(row.modelData)
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onEntered: if (enabled) menu.active = row.index
                    onClicked: menu.run(row.modelData.id)
                }
            }
        }
    }
}
