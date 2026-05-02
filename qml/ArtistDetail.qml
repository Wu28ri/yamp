import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: artistDetailRoot
    anchors.fill: parent

    Component.onCompleted: playerBackend.filterByArtist(root.selectedArtist)

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

            ColumnLayout {
                spacing: 10
                Layout.alignment: Qt.AlignBottom

                Text {
                    text: root.selectedArtist || "Unknown Artist"
                    font.pixelSize: 42
                    font.bold: true
                    color: sysPalette.text
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
                            playerBackend.filterByArtist("")
                            root.currentView = "artists"
                        }
                    }

                    ToolButton {
                        id: playArtistButton
                        icon.name: "media-playback-start"
                        icon.width: 32; icon.height: 32
                        background: Rectangle {
                            implicitWidth: 64; implicitHeight: 64
                            radius: 32
                            color: playArtistButton.hovered ? sysPalette.mid : "transparent"
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
