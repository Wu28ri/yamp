import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridView {
    id: artistsGrid
    anchors.fill: parent
    clip: true
    reuseItems: true
    cellWidth: {
        const cols = Math.max(1, Math.floor(width / 200))
        return Math.floor(width / cols)
    }
    cellHeight: 280
    cacheBuffer: 600
    model: playerBackend.artistModel

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        onWheel: (wheel) => {
            const multiplier = 3.0
            const step = (wheel.angleDelta.y / 120) * 120 * multiplier
            const minY = artistsGrid.originY
            const maxY = Math.max(minY, artistsGrid.contentHeight - artistsGrid.height + artistsGrid.originY)
            artistsGrid.contentY = Math.max(minY, Math.min(artistsGrid.contentY - step, maxY))
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
            opacity: (vBar.hovered || vBar.pressed || artistsGrid.moving) ? 1.0 : 0.4
        }
    }

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
                    id: avatar
                    anchors.centerIn: parent
                    width: parent.width
                    height: parent.height
                    radius: width / 2
                    color: root.colorForName(tile.artist)

                    transformOrigin: Item.Center
                    scale: artistClickArea.containsMouse ? 1.03 : 1.0
                    Behavior on scale { NumberAnimation { duration: 150 } }
                    opacity: artistClickArea.pressed ? 0.85 : 1.0

                    Text {
                        anchors.centerIn: parent
                        text: root.initialsForName(tile.artist)
                        color: "white"
                        font.pixelSize: Math.round(parent.width * 0.32)
                        font.bold: true
                        renderType: Text.NativeRendering
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