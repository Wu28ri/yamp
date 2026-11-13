import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridView {
    id: artistsGrid
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

    delegate: Item {
        id: tile
        width: artistsGrid.cellWidth
        height: artistsGrid.cellHeight

        required property string artist
        required property int    albumCount
        required property int    trackCount

        onArtistChanged: artistImage.fetchedUrl = ""

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            Item {
                Layout.preferredWidth: artistsGrid.coverSize
                Layout.preferredHeight: artistsGrid.coverSize
                Layout.alignment: Qt.AlignHCenter

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width
                    height: parent.height
                    radius: 8
                    clip: true
                    color: artistImage.status === Image.Ready
                           ? "transparent"
                           : root.colorForName(tile.artist)

                    transformOrigin: Item.Center
                    scale: artistClickArea.containsMouse ? 1.03 : 1.0
                    Behavior on scale { NumberAnimation { duration: 150 } }
                    opacity: artistClickArea.pressed ? 0.85 : 1.0

                    Text {
                        anchors.centerIn: parent
                        visible: artistImage.status !== Image.Ready
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
                        sourceSize: Qt.size(256, 256)
                        asynchronous: true
                        visible: status === Image.Ready
                        source: artistImages.enabled
                                ? (fetchedUrl || artistImages.artistImageUrl(tile.artist))
                                : ""

                        Connections {
                            target: artistImages
                            enabled: artistImages.enabled && artistImage.status !== Image.Ready
                            function onArtistImageReady(artistName, imageUrl) {
                                if (artistName === tile.artist)
                                    artistImage.fetchedUrl = imageUrl
                            }
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 5
                Layout.preferredHeight: 18
                text: tile.artist || "Unknown Artist"
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

            Item { Layout.fillHeight: true }
        }

        MouseArea {
            id: artistClickArea
            readonly property bool keepsSearchOpen: true
            anchors.fill: parent
            anchors.margins: 8
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.openArtist(tile.artist)
        }
    }
}
