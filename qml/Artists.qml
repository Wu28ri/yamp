import QtQuick
import QtQuick.Controls

GridView {
    id: artistsGrid
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
    model: playerBackend.artistModel

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
            artistsGrid.suppressSave = false
            if (artistsGrid.alive && artistsGrid.persistScroll && !root.searchActive)
                root.savedArtistsScrollOffset = Math.max(0, artistsGrid.contentY - artistsGrid.originY)
        }
    }

    Connections {
        target: playerBackend.artistModel
        enabled: artistsGrid.alive && artistsGrid.parent !== null
        function onModelAboutToBeReset() {
            artistsGrid.suppressSave = true
        }
        function onModelReset() {
            const offset = root.searchActive ? 0 : root.savedArtistsScrollOffset
            Qt.callLater(function() {
                if (!artistsGrid.alive || !artistsGrid.parent) return
                artistsGrid.restoreScroll(offset)
                suppressTimer.restart()
            })
        }
    }

    Component.onCompleted: {
        const offset = root.searchActive ? 0 : root.savedArtistsScrollOffset
        if (restoreScroll(offset))
            needsInitialRestore = false
        Qt.callLater(function() {
            if (!artistsGrid.alive) return
            if (artistsGrid.restoreScroll(offset))
                artistsGrid.needsInitialRestore = false
            artistsGrid.persistScroll = true
            suppressTimer.restart()
        })
    }

    onWidthChanged: {
        if (needsInitialRestore && width > 0 && height > 0) {
            if (restoreScroll(root.searchActive ? 0 : root.savedArtistsScrollOffset))
                needsInitialRestore = false
        }
    }

    onHeightChanged: {
        if (needsInitialRestore && width > 0 && height > 0) {
            if (restoreScroll(root.searchActive ? 0 : root.savedArtistsScrollOffset))
                needsInitialRestore = false
        }
    }

    onContentYChanged: {
        if (persistScroll && !suppressSave && !root.searchActive)
            root.savedArtistsScrollOffset = Math.max(0, contentY - originY)
    }

    ScrollBar.vertical: SmoothScrollBar {
        id: vbar
        flickable: artistsGrid
        topPadding: 8
        bottomPadding: 8
    }

    Menu {
        id: artistContextMenu
        property string artistName: ""

        MenuItem {
            text: "Play"
            onTriggered: playerBackend.playArtist(artistContextMenu.artistName, playerBackend.shuffle)
        }
        MenuItem {
            text: "Shuffle"
            onTriggered: playerBackend.playArtist(artistContextMenu.artistName, true)
        }
        MenuItem {
            text: "Play next"
            onTriggered: playerBackend.addArtistPlayNext(artistContextMenu.artistName)
        }
        MenuItem {
            text: "Add to queue"
            onTriggered: playerBackend.addArtistToQueue(artistContextMenu.artistName)
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 12
        visible: artistsGrid.count === 0

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: {
                if (appSettings.musicFolders.length === 0) return "No music folders added"
                if (playerBackend.scanInProgress) return "Scanning music library…"
                if (root.searchActive) return "No matching artists"
                return "No artists in library"
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
        width: artistsGrid.cellWidth
        height: artistsGrid.cellHeight

        required property string artist
        required property int    albumCount
        required property int    trackCount

        onArtistChanged: artistImage.fetchedUrl = ""

        Rectangle {
            id: avatarBox
            anchors.top: parent.top
            anchors.topMargin: 10
            anchors.horizontalCenter: parent.horizontalCenter
            width: artistsGrid.coverSize
            height: artistsGrid.coverSize
            radius: 8
            color: root.hasValidImage(artistImage)
                   ? "transparent"
                   : root.colorForName(tile.artist)

            transformOrigin: Item.Center
            scale: artistClickArea.containsMouse ? 1.03 : 1.0
            Behavior on scale { NumberAnimation { duration: 150 } }
            opacity: artistClickArea.pressed ? 0.85 : 1.0

            Text {
                anchors.centerIn: parent
                visible: !root.hasValidImage(artistImage)
                text: root.initialsForName(tile.artist)
                color: "white"
                font.pixelSize: Math.round(parent.width * 0.32)
                font.bold: true
                renderType: Text.QtRendering
            }

            Image {
                id: artistImage
                property string fetchedUrl: ""
                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(Math.min(384, Math.ceil(220 * Screen.devicePixelRatio)),
                                    Math.min(384, Math.ceil(220 * Screen.devicePixelRatio)))
                asynchronous: true
                visible: root.hasValidImage(artistImage)
                source: artistImages.enabled
                        ? (fetchedUrl || artistImages.artistImageUrl(tile.artist))
                        : ""

                Connections {
                    target: artistImages
                    enabled: artistImages.enabled && !root.hasValidImage(artistImage)
                    function onArtistImageReady(artistName, imageUrl) {
                        if (artistName === tile.artist)
                            artistImage.fetchedUrl = imageUrl
                    }
                }
            }
        }

        Text {
            id: artistLabel
            anchors.top: avatarBox.bottom
            anchors.topMargin: 8
            anchors.left: parent.left
            anchors.leftMargin: 15
            anchors.right: parent.right
            anchors.rightMargin: 10
            height: 18
            text: tile.artist || "Unknown Artist"
            color: sysPalette.text
            font.bold: true
            font.pixelSize: 14
            renderType: Text.NativeRendering
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            anchors.top: artistLabel.bottom
            anchors.topMargin: 4
            anchors.left: parent.left
            anchors.leftMargin: 15
            anchors.right: parent.right
            anchors.rightMargin: 10
            height: 16
            text: {
                const a = tile.albumCount
                const t = tile.trackCount
                return a + (a === 1 ? " album · " : " albums · ")
                     + t + (t === 1 ? " track"   : " tracks")
            }
            color: sysPalette.windowText
            font.pixelSize: 12
            renderType: Text.NativeRendering
            opacity: 0.7
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        MouseArea {
            id: artistClickArea
            readonly property bool keepsSearchOpen: true
            anchors.fill: parent
            anchors.margins: 8
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor
            onClicked: mouse => {
                const name = tile.artist || "Unknown Artist"
                if (mouse.button === Qt.RightButton) {
                    artistContextMenu.artistName = name
                    artistContextMenu.popup()
                } else {
                    root.openArtist(name)
                }
            }
        }
    }
}
