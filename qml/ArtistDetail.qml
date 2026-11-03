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
                radius: 12
                clip: true
                color: detailArtistImage.status === Image.Ready
                       ? "transparent"
                       : root.colorForName(root.selectedArtist)

                Text {
                    anchors.centerIn: parent
                    visible: detailArtistImage.status !== Image.Ready
                    text: root.initialsForName(root.selectedArtist)
                    color: "white"
                    font.pixelSize: 80
                    font.bold: true
                }

                Image {
                    id: detailArtistImage
                    property string fetchedUrl: ""
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    sourceSize: Qt.size(256, 256)
                    asynchronous: true
                    visible: status === Image.Ready
                    source: artistImages.enabled
                            ? (fetchedUrl || artistImages.artistImageUrl(root.selectedArtist))
                            : ""

                    Connections {
                        target: artistImages
                        enabled: artistImages.enabled && detailArtistImage.status !== Image.Ready
                        function onArtistImageReady(artistName, imageUrl) {
                            if (artistName === root.selectedArtist)
                                detailArtistImage.fetchedUrl = imageUrl
                        }
                    }
                }
            }

            onBackRequested: {
                root.currentView = "artists"
                playerBackend.filterByArtist("")
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
