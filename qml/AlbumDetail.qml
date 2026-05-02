import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: albumDetailRoot
    anchors.fill: parent

    Component.onCompleted: playerBackend.filterByAlbum(root.selectedAlbum, root.selectedArtist)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 30
            Layout.topMargin: 40
            spacing: 30

            Rectangle {
                width: 220; height: 220
                radius: 12; clip: true
                color: sysPalette.mid
                Image {
                    anchors.fill: parent
                    source: root.coverSource(root.selectedAlbumPath)
                    sourceSize: Qt.size(220, 220)
                    fillMode: Image.PreserveAspectCrop
                }
            }

            ColumnLayout {
                spacing: 10
                Layout.alignment: Qt.AlignBottom

                Text {
                    text: root.selectedAlbum
                    font.pixelSize: 42
                    font.bold: true
                    color: sysPalette.text
                }
                Text {
                    text: root.selectedArtist
                    font.pixelSize: 18
                    opacity: 0.6
                    color: sysPalette.windowText
                }

                RowLayout {
                    spacing: 15
                    Layout.topMargin: 10

                    ToolButton {
                        id: backButton
                        icon.name: "go-previous"
                        background: Rectangle {
                            implicitWidth: 48; implicitHeight: 48
                            radius: 24
                            color: backButton.hovered ? sysPalette.mid : "transparent"
                        }
                        onClicked: {
                            playerBackend.filterByAlbum("")
                            root.currentView = "albums"
                        }
                    }

                    ToolButton {
                        id: playAlbumButton
                        icon.name: "media-playback-start"
                        icon.width: 32; icon.height: 32
                        background: Rectangle {
                            implicitWidth: 64; implicitHeight: 64
                            radius: 32
                            color: playAlbumButton.hovered ? sysPalette.mid : "transparent"
                        }
                        onClicked: {
                            playerBackend.shuffle = false
                            const firstPath = playerBackend.trackModel.pathForRow(0)
                            if (firstPath) playerBackend.playMusic(firstPath)
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }

        Tracklist {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
