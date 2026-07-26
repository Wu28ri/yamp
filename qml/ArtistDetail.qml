import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    anchors.fill: parent

    Component.onCompleted: playerBackend.filterByArtist(root.selectedArtist)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        DetailHeader {
            title: root.selectedArtist || "Unknown Artist"

            avatarContent: Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: root.colorForName(root.selectedArtist)

                Text {
                    anchors.centerIn: parent
                    text: root.initialsForName(root.selectedArtist)
                    color: "white"
                    font.pixelSize: 80
                    font.bold: true
                }
            }

            onBackRequested: {
                playerBackend.filterByArtist("")
                root.currentView = "artists"
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
