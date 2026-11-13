import QtQuick
import QtQuick.Controls

GridView {
    id: albumsGrid
    anchors.fill: parent
    clip: true
    reuseItems: true
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

    Menu {
        id: albumContextMenu
        property string albumName: ""
        property string artistName: ""

        MenuItem {
            text: "Play"
            onTriggered: playerBackend.playAlbum(albumContextMenu.albumName, albumContextMenu.artistName, playerBackend.shuffle)
        }
        MenuItem {
            text: "Shuffle"
            onTriggered: playerBackend.playAlbum(albumContextMenu.albumName, albumContextMenu.artistName, true)
        }
        MenuItem {
            text: "Play next"
            onTriggered: playerBackend.addAlbumPlayNext(albumContextMenu.albumName, albumContextMenu.artistName)
        }
        MenuItem {
            text: "Add to queue"
            onTriggered: playerBackend.addAlbumToQueue(albumContextMenu.albumName, albumContextMenu.artistName)
        }
        MenuSeparator {}
        MenuItem {
            text: "Go to artist"
            enabled: albumContextMenu.artistName.length > 0
                     && albumContextMenu.artistName !== "Unknown Artist"
            onTriggered: root.openArtist(albumContextMenu.artistName)
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 12
        visible: albumsGrid.count === 0

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: {
                if (appSettings.musicFolders.length === 0) return "No music folders added"
                if (playerBackend.scanInProgress) return "Scanning music library…"
                if (root.searchActive) return "No matching albums"
                return "No albums in library"
            }
            color: sysPalette.windowText
            opacity: 0.55
            font.pixelSize: 15
        }

        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: appSettings.musicFolders.length === 0
            text: "Add Music Folder…"
            icon.name: "list-add"
            onClicked: root.openAddFolderSettings()
        }
    }

    delegate: Item {
        id: tile
        width: albumsGrid.cellWidth
        height: albumsGrid.cellHeight

        required property string album
        required property string artist
        required property string path

        Rectangle {
            id: coverBox
            anchors.top: parent.top
            anchors.topMargin: 10
            anchors.horizontalCenter: parent.horizontalCenter
            width: albumsGrid.coverSize
            height: albumsGrid.coverSize
            color: root.hasValidImage(coverImage) ? "transparent" : sysPalette.mid
            radius: 8

            transformOrigin: Item.Center
            scale: albumClickArea.containsMouse ? 1.03 : 1.0
            Behavior on scale { NumberAnimation { duration: 150 } }

            Text {
                anchors.centerIn: parent
                visible: !root.hasValidImage(coverImage)
                text: "♪"
                font.pixelSize: Math.round(parent.width * 0.32)
                color: sysPalette.windowText
                opacity: 0.3
            }

            Image {
                id: coverImage
                anchors.fill: parent
                visible: root.hasValidImage(coverImage)
                fillMode: Image.PreserveAspectCrop
                source: root.coverSource(tile.path)
                sourceSize: Qt.size(Math.ceil(250 * Screen.devicePixelRatio), Math.ceil(250 * Screen.devicePixelRatio))
                asynchronous: true
                opacity: albumClickArea.pressed ? 0.8 : 1.0
            }
        }

        Text {
            id: albumLabel
            anchors.top: coverBox.bottom
            anchors.topMargin: 8
            anchors.left: parent.left
            anchors.leftMargin: 15
            anchors.right: parent.right
            anchors.rightMargin: 10
            height: 18
            text: tile.album || "Unknown Album"
            color: sysPalette.text
            font.bold: true
            font.pixelSize: 14
            renderType: Text.NativeRendering
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            anchors.top: albumLabel.bottom
            anchors.topMargin: 4
            anchors.left: parent.left
            anchors.leftMargin: 15
            anchors.right: parent.right
            anchors.rightMargin: 10
            height: 16
            text: tile.artist || "Unknown Artist"
            color: sysPalette.windowText
            font.pixelSize: 12
            renderType: Text.NativeRendering
            opacity: 0.7
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        MouseArea {
            id: albumClickArea
            readonly property bool keepsSearchOpen: true
            anchors.fill: parent
            anchors.margins: 8
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor
            onClicked: mouse => {
                if (mouse.button === Qt.RightButton) {
                    albumContextMenu.albumName = tile.album || "Unknown Album"
                    albumContextMenu.artistName = tile.artist || "Unknown Artist"
                    albumContextMenu.popup()
                } else {
                    root.openAlbum(tile.album || "Unknown Album", tile.artist || "Unknown Artist", tile.path)
                }
            }
        }
    }
}
