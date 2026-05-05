import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridView {
    id: artistsGrid
    anchors.fill: parent
    clip: true
    reuseItems: true
    cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 200)))
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

            Rectangle {
                id: avatar
                Layout.preferredWidth: Math.floor(parent.width * 0.9)
                Layout.preferredHeight: width
                Layout.alignment: Qt.AlignHCenter
                radius: width / 2
                color: root.colorForName(tile.artist)

                scale: artistClickArea.containsMouse ? 1.03 : 1.0
                Behavior on scale { NumberAnimation { duration: 150 } }
                opacity: artistClickArea.pressed ? 0.85 : 1.0

                Text {
                    anchors.centerIn: parent
                    text: root.initialsForName(tile.artist)
                    color: "white"
                    font.pixelSize: Math.round(parent.width * 0.32)
                    font.bold: true
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

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 5
                text: tile.artist || "Unknown Artist"
                color: sysPalette.text
                font.bold: true
                font.pixelSize: 14
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 5
                text: {
                    const a = tile.albumCount
                    const t = tile.trackCount
                    return a + (a === 1 ? " album · " : " albums · ")
                         + t + (t === 1 ? " track"   : " tracks")
                }
                color: sysPalette.windowText
                font.pixelSize: 12
                opacity: 0.7
                elide: Text.ElideRight
            }

            Item { Layout.fillHeight: true }
        }
    }
}