import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridView {
    id: albumsGrid
    anchors.fill: parent
    clip: true
    reuseItems: false
    readonly property real gridAvailableWidth: Math.max(1, width - vbar.implicitWidth)

    cellWidth: {
        const cols = Math.max(1, Math.floor(gridAvailableWidth / 200))
        return Math.floor(gridAvailableWidth / cols)
    }
    readonly property real coverSize: Math.max(80, Math.floor((cellWidth - 20) * 0.9))
    cellHeight: coverSize + 74
    cacheBuffer: 600
    model: playerBackend.albumModel

    property bool alive: true
    property bool persistScroll: false
    property bool suppressSave: true
    property bool needsInitialRestore: true

    Component.onDestruction: alive = false

    function restoreScroll(offset) {
        if (!alive || width <= 0 || height <= 0) return false
        forceLayout()
        const minY = originY
        const maxY = Math.max(minY, contentHeight - height + originY)
        contentY = Math.max(minY, Math.min(minY + offset, maxY))
        return true
    }

    Timer {
        id: suppressTimer
        interval: 50
        onTriggered: {
            albumsGrid.suppressSave = false
            if (albumsGrid.alive && albumsGrid.persistScroll && !root.searchActive)
                root.savedAlbumsScrollOffset = Math.max(0, albumsGrid.contentY - albumsGrid.originY)
        }
    }

    Connections {
        target: playerBackend.albumModel
        enabled: albumsGrid.alive && albumsGrid.parent !== null
        function onModelAboutToBeReset() {
            albumsGrid.suppressSave = true
        }
        function onModelReset() {
            const offset = root.searchActive ? 0 : root.savedAlbumsScrollOffset
            Qt.callLater(function() {
                if (!albumsGrid.alive || !albumsGrid.parent) return
                albumsGrid.restoreScroll(offset)
                suppressTimer.restart()
            })
        }
    }

    Component.onCompleted: {
        const offset = root.searchActive ? 0 : root.savedAlbumsScrollOffset
        if (restoreScroll(offset))
            needsInitialRestore = false
        Qt.callLater(function() {
            if (!albumsGrid.alive) return
            if (albumsGrid.restoreScroll(offset))
                albumsGrid.needsInitialRestore = false
            albumsGrid.persistScroll = true
            suppressTimer.restart()
        })
    }

    onWidthChanged: {
        if (needsInitialRestore && width > 0 && height > 0) {
            if (restoreScroll(root.searchActive ? 0 : root.savedAlbumsScrollOffset))
                needsInitialRestore = false
        }
    }

    onHeightChanged: {
        if (needsInitialRestore && width > 0 && height > 0) {
            if (restoreScroll(root.searchActive ? 0 : root.savedAlbumsScrollOffset))
                needsInitialRestore = false
        }
    }

    onContentYChanged: {
        if (persistScroll && !suppressSave && !root.searchActive)
            root.savedAlbumsScrollOffset = Math.max(0, contentY - originY)
    }

    ScrollBar.vertical: SmoothScrollBar {
        id: vbar
        flickable: albumsGrid
        topPadding: 8
        bottomPadding: 8
    }

    delegate: Item {
        id: tile
        width: albumsGrid.cellWidth
        height: albumsGrid.cellHeight

        required property string album
        required property string artist
        required property string path

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            Item {
                Layout.preferredWidth: albumsGrid.coverSize
                Layout.preferredHeight: albumsGrid.coverSize
                Layout.alignment: Qt.AlignHCenter

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width
                    height: parent.height
                    color: coverImage.status === Image.Ready ? "transparent" : sysPalette.mid
                    radius: 8
                    clip: true

                    transformOrigin: Item.Center
                    scale: albumClickArea.containsMouse ? 1.03 : 1.0
                    Behavior on scale { NumberAnimation { duration: 150 } }

                    Image {
                        id: coverImage
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop
                        source: root.coverSource(tile.path)
                        sourceSize: Qt.size(Math.ceil(250 * Screen.devicePixelRatio), Math.ceil(250 * Screen.devicePixelRatio))
                        asynchronous: true
                        opacity: albumClickArea.pressed ? 0.8 : 1.0
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 5
                Layout.preferredHeight: 18
                text: tile.album || "Album"
                color: sysPalette.text
                font.bold: true
                font.pixelSize: 14
                renderType: Text.NativeRendering
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 5
                Layout.preferredHeight: 16
                text: tile.artist || "Artist"
                color: sysPalette.windowText
                font.pixelSize: 12
                renderType: Text.NativeRendering
                opacity: 0.7
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }

            Item { Layout.fillHeight: true }
        }

        MouseArea {
            id: albumClickArea
            readonly property bool keepsSearchOpen: true
            anchors.fill: parent
            anchors.margins: 8
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.openAlbum(tile.album, tile.artist, tile.path)
        }
    }
}
