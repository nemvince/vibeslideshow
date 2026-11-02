import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vibes 1.0

// The left pane: the project's bin. Every clip the imported folders and files
// hold, in one flat list with a name search, and the three ways to fill it —
// the Import menu, a drop from the file manager, or the project sheet.
// Filtered-out clips stay visible but dimmed, so a filter never silently hides
// the library — you can always see what you excluded. The filters themselves
// live in the settings panel on the right.
Rectangle {
    id: rail
    color: Theme.panel

    property int hovered: -1
    property bool dropHover: false

    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.line }

    ColumnLayout {
        anchors { fill: parent; margins: Theme.gap }
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            SectionLabel { text: "Bin" }
            Item { Layout.fillWidth: true }
            Text {
                text: clips.visibleCount + " / " + clips.count
                color: Theme.faint
                font.pixelSize: 11
            }
            SmallButton {
                id: importBtn
                text: "Import…"
                onClicked: importMenu.opened ? importMenu.close() : importMenu.open()
            }
        }

        TextField {
            id: search
            objectName: "binSearch"
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            placeholderText: "Search names"
            placeholderTextColor: Theme.faint
            color: Theme.ink
            font.pixelSize: 11
            leftPadding: 8
            rightPadding: 8
            background: Rectangle { color: Theme.bg; radius: 5; border.color: Theme.line }
            onTextChanged: clips.search = text
        }

        // The clip list.
        ListView {
            id: list
            objectName: "binList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: clips
            spacing: 4
            cacheBuffer: 800
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                width: 6
                background: Item {}
                contentItem: Rectangle {
                    implicitWidth: 4
                    radius: 2
                    color: Theme.faint
                    opacity: parent.pressed ? 0.9 : 0.3
                }
            }

            delegate: Item {
                id: row
                required property int index
                required property var model

                width: list.width
                height: 54
                opacity: model.inPool ? 1 : 0.32

                Rectangle {
                    anchors.fill: parent
                    radius: 7
                    color: hover.hovered ? Theme.panelHi : "transparent"
                }

                Rectangle {
                    id: thumbFrame
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 5 }
                    width: (height * 16) / 9
                    radius: 5
                    color: Theme.bg
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: model.thumb ? "file://" + model.thumb : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                    }
                }

                Column {
                    anchors { left: thumbFrame.right; leftMargin: 9; right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                    spacing: 3
                    Text {
                        width: parent.width
                        text: model.name
                        color: Theme.ink
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }
                    Row {
                        spacing: 6
                        ScoreBadge { score: model.score; motion: model.mv }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: model.day.length ? model.day : "no stamp"
                            color: Theme.faint
                            font.pixelSize: 10
                        }
                        Text {
                            visible: model.proxy
                            anchors.verticalCenter: parent.verticalCenter
                            text: "proxy"
                            color: Theme.accent
                            font.pixelSize: 10
                        }
                    }
                }

                HoverHandler { id: hover }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    // Dropping a clip in needs a settled edit to drop it into;
                    // the menu itself works at any time.
                    cursorShape: studio.workspaceEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    // Click a clip to drop it into the show at the playhead,
                    // right-click it for what else can be done with it.
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton) {
                            const p = mapToItem(rail, mouse.x, mouse.y)
                            rail.openClipMenu(model.clipId, model.name, p.x, p.y)
                        } else if (studio.workspaceEnabled) {
                            studio.newInPointAt(model.clipId, showCtl.position)
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: clips.count === 0
                text: studio.busy ? "Looking for footage…" : "No footage yet — import files or a folder"
                color: Theme.faint
                font.pixelSize: 12
            }
        }
    }

    // What can be done with a clip in the bin. One table, so the menu's rows and
    // the actions they run cannot drift apart.
    readonly property var clipActions: [
        { id: "insert", label: "Insert at playhead", destructive: false },
        { id: "remove", label: "Remove from project", destructive: true }
    ]

    // Open the clip menu at (x, y), in this pane's coordinates, on `clipId`.
    function openClipMenu(clipId, name, x, y) {
        clipMenu.clipId = clipId
        clipMenu.clipName = name
        const h = clipMenu.implicitHeight
        clipMenu.x = Math.max(0, Math.min(rail.width - clipMenu.width, x))
        clipMenu.y = (y + h > rail.height) ? Math.max(0, y - h) : y
        clipMenu.active = clipMenu.firstEnabled()
        clipMenu.open()
    }

    // The clip menu: what a right-click on a bin row offers. Inserting needs a
    // settled edit to insert into; removing needs nothing but the clip.
    //
    // A plain Popup, styled like the timeline's ClipMenu: the app's menus are
    // hand-drawn, and Menu's internal content item makes custom rows awkward.
    Popup {
        id: clipMenu
        objectName: "binClipMenu"
        property int clipId: -1
        property string clipName: ""
        // Row under the keyboard, -1 when nothing is highlighted.
        property int active: -1

        width: 232
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

        function enabledFor(id) {
            if (id === "insert") return studio.workspaceEnabled
            return true
        }
        function firstEnabled() {
            for (let i = 0; i < rail.clipActions.length; i++)
                if (enabledFor(rail.clipActions[i].id)) return i
            return -1
        }
        // Move the highlight to the next row that can actually run, wrapping.
        function step(delta) {
            const n = rail.clipActions.length
            if (n === 0) return
            let i = active
            for (let k = 0; k < n; k++) {
                i = (i + delta + n) % n
                if (enabledFor(rail.clipActions[i].id)) { active = i; return }
            }
        }
        function run(id) {
            if (!id || !enabledFor(id)) return
            const target = clipMenu.clipId
            close()
            if (id === "insert") studio.newInPointAt(target, showCtl.position)
            else studio.removeClip(target)
        }
        onOpened: col.forceActiveFocus()
        onClosed: {
            active = -1
            clipId = -1
        }

        contentItem: Column {
            id: col
            spacing: 2
            focus: true

            Keys.onUpPressed: clipMenu.step(-1)
            Keys.onDownPressed: clipMenu.step(1)
            Keys.onReturnPressed: clipMenu.run(clipMenu.active >= 0 ? rail.clipActions[clipMenu.active].id : "")
            Keys.onEnterPressed: clipMenu.run(clipMenu.active >= 0 ? rail.clipActions[clipMenu.active].id : "")

            // Which clip the menu acts on: a menu on a row is meaningless without
            // the row's name in it.
            Text {
                width: col.width - 8
                leftPadding: 8
                rightPadding: 8
                topPadding: 2
                bottomPadding: 5
                text: clipMenu.clipName
                color: Theme.faint
                font.pixelSize: 10
                elide: Text.ElideMiddle
            }

            Repeater {
                model: rail.clipActions
                delegate: Item {
                    id: actionRow
                    required property int index
                    required property var modelData
                    property string rowId: modelData.id

                    width: col.width
                    height: 28
                    opacity: clipMenu.enabledFor(modelData.id) ? 1 : 0.4

                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 1
                        radius: 6
                        color: clipMenu.active === actionRow.index ? Theme.bg
                               : (actionHover.hovered ? Theme.bg : "transparent")
                    }
                    Text {
                        anchors {
                            left: parent.left
                            leftMargin: 10
                            right: parent.right
                            rightMargin: 10
                            verticalCenter: parent.verticalCenter
                        }
                        text: actionRow.modelData.label
                        font.pixelSize: 11
                        color: actionRow.modelData.destructive ? Theme.bad : Theme.ink
                        elide: Text.ElideRight
                    }

                    HoverHandler { id: actionHover }
                    MouseArea {
                        anchors.fill: parent
                        enabled: clipMenu.enabledFor(actionRow.modelData.id)
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onEntered: if (enabled) clipMenu.active = actionRow.index
                        onClicked: clipMenu.run(actionRow.modelData.id)
                    }
                }
            }
        }
    }

    Popup {
        id: importMenu
        objectName: "importMenu"
        x: importBtn.x
        y: importBtn.y + importBtn.height + 4
        width: 160
        padding: 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.panelHi
            radius: 7
            border.color: Theme.line
        }

        contentItem: Column {
            spacing: 2
            SmallButton {
                width: importMenu.width - 8
                text: "Files…"
                flat: true
                onClicked: {
                    importMenu.close()
                    studio.openFilesDialog()
                }
            }
            SmallButton {
                width: importMenu.width - 8
                text: "Folder…"
                flat: true
                onClicked: {
                    importMenu.close()
                    studio.openFolderDialog()
                }
            }
        }
    }

    // The whole pane is the drop target, so a file dragged anywhere onto the rail
    // lands in the bin. Anchored last so it covers the list while a drag is over.
    DropArea {
        anchors.fill: parent
        onEntered: (drag) => {
            rail.dropHover = drag.hasUrls
            drag.acceptProposedAction()
        }
        onExited: rail.dropHover = false
        onDropped: (drop) => {
            rail.dropHover = false
            if (drop.hasUrls) {
                studio.importUrls(drop.urls)
                drop.acceptProposedAction()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: rail.dropHover
        color: Theme.bg
        opacity: 0.88

        Text {
            anchors.centerIn: parent
            text: "Drop to import"
            color: Theme.accent
            font.pixelSize: 13
        }
    }
}
