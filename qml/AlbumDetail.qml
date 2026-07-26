import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    anchors.fill: parent

    Component.onCompleted: playerBackend.filterByAlbum(root.selectedAlbum, root.selectedArtist)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        DetailHeader {
            title:    root.selectedAlbum
            subtitle: root.selectedArtist

            avatarContent: Rectangle {
                anchors.fill: parent
                radius: 12; clip: true
                color: sysPalette.mid
                Image {
                    anchors.fill: parent
                    source: root.coverSource(root.selectedAlbumPath)
                    sourceSize: Qt.size(220, 220)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                }
            }

            onBackRequested: {
                playerBackend.filterByAlbum("")
                root.currentView = "albums"
            }
            onPlayRequested: {
                playerBackend.shuffle = false
                const firstPath = playerBackend.trackModel.pathForRow(0)
                if (firstPath) playerBackend.playMusic(firstPath)
            }
        }

        Tracklist {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
