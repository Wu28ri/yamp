import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridView {
    id: artistsGrid
    anchors.fill: parent
    clip: true
    reuseItems: false
    cellWidth: {
        const cols = Math.max(1, Math.floor(width / 200))
        return Math.floor(width / cols)
    }
    cellHeight: 280
    cacheBuffer: 600
    model: playerBackend.artistModel

    ScrollBar.vertical: SmoothScrollBar { flickable: artistsGrid }

    delegate: Item {
        id: tile
        width: artistsGrid.cellWidth
        height: artistsGrid.cellHeight

        required property string artist
        required property int    albumCount
        required property int    trackCount

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            Item {
                Layout.preferredWidth: Math.floor((tile.width - 20) * 0.9)
                Layout.preferredHeight: Layout.preferredWidth
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
                        renderType: Text.NativeRendering
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

                    MouseArea {
                        id: artistClickArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.selectedArtist = tile.artist
                            root.currentView    = "artistDetail"
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
    }
}
