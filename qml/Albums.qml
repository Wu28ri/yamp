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
