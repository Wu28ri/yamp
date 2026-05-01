import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: albumsRoot
    anchors.fill: parent
    clip: true

    GridView {
        id: albumsGrid
        width: albumsRoot.availableWidth
        cellWidth: width / Math.max(1, Math.floor(width / 200))
        cellHeight: 280
        model: playerBackend.albumModel

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
                    Layout.preferredWidth: parent.width * 0.9
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
                        source: tile.path ? "image://cover/" + tile.path : ""
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

                Column {
                    Layout.fillWidth: true
                    Layout.leftMargin: 5

                    Text {
                        width: parent.width
                        text: tile.album || "Album"
                        color: sysPalette.text
                        font.bold: true
                        font.pixelSize: 14
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: tile.artist || "Artist"
                        color: sysPalette.windowText
                        font.pixelSize: 12
                        opacity: 0.7
                        elide: Text.ElideRight
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
