import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridView {
    id: albumsGrid
    anchors.fill: parent
    clip: true
    reuseItems: true
    cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 200)))
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

            Rectangle {
                id: coverContainer
                Layout.preferredWidth: Math.floor(parent.width * 0.9)
                Layout.preferredHeight: width
                Layout.alignment: Qt.AlignHCenter
                color: sysPalette.mid
                radius: 8
                clip: true

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

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 5
                text: tile.album || "Album"
                color: sysPalette.text
                font.bold: true
                font.pixelSize: 14
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 5
                text: tile.artist || "Artist"
                color: sysPalette.windowText
                font.pixelSize: 12
                opacity: 0.7
                elide: Text.ElideRight
            }

            Item { Layout.fillHeight: true }
        }
    }
}