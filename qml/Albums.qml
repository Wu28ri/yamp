import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridView {
    id: albumsGrid
    anchors.fill: parent
    clip: true
    reuseItems: true
    cellWidth: {
        const cols = Math.max(1, Math.floor(width / 200))
        return Math.floor(width / cols)
    }
    cellHeight: 280
    cacheBuffer: 600
    model: playerBackend.albumModel

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        onWheel: (wheel) => {
            const multiplier = 3.0
            const step = (wheel.angleDelta.y / 120) * 120 * multiplier
            const minY = albumsGrid.originY
            const maxY = Math.max(minY, albumsGrid.contentHeight - albumsGrid.height + albumsGrid.originY)
            albumsGrid.contentY = Math.max(minY, Math.min(albumsGrid.contentY - step, maxY))
        }
    }

    ScrollBar.vertical: ScrollBar {
        id: vBar
        policy: ScrollBar.AlwaysOn
        background: Item { implicitWidth: 8 }
        contentItem: Rectangle {
            implicitWidth: 8
            radius: 4
            color: sysPalette.highlight
            opacity: (vBar.hovered || vBar.pressed || albumsGrid.moving) ? 1.0 : 0.4
        }
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
                Layout.preferredWidth: Math.floor((tile.width - 20) * 0.9)
                Layout.preferredHeight: Layout.preferredWidth
                Layout.alignment: Qt.AlignHCenter

                Rectangle {
                    id: coverContainer
                    anchors.centerIn: parent
                    width: parent.width
                    height: parent.height
                    color: sysPalette.mid
                    radius: 8
                    clip: true

                    transformOrigin: Item.Center
                    scale: albumClickArea.containsMouse ? 1.03 : 1.0
                    Behavior on scale { NumberAnimation { duration: 150 } }

                    Image {
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop
                        source: root.coverSource(tile.path)
                        sourceSize: Qt.size(250, 250)
                        asynchronous: true
                        opacity: albumClickArea.pressed ? 0.8 : 1.0
                    }

                    MouseArea {
                        id: albumClickArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.selectedAlbum     = tile.album
                            root.selectedAlbumPath = tile.path
                            root.selectedArtist    = tile.artist
                            root.currentView       = "albumDetail"
                        }
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
    }
}