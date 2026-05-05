import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    implicitWidth: 250
    color: sysPalette.base

    signal settingsClicked()

    function clearSearch() {
        searchField.text = ""
        playerBackend.searchTracks("")
        playerBackend.searchAlbums("")
        playerBackend.searchArtists("")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 15

        RowLayout {
            Layout.fillWidth: true
            spacing: 5

            ToolButton {
                icon.name: "system-search"
                onClicked: {
                    searchField.visible = !searchField.visible
                    if (searchField.visible) {
                        searchField.forceActiveFocus()
                    } else {
                        clearSearch()
                    }
                }
            }

            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: {
                    if (root.currentView === "albums") return "Find album..."
                    if (root.currentView === "artists") return "Find artist..."
                    return "Find track..."
                }
                visible: false
                color: sysPalette.text

                background: Rectangle {
                    color: sysPalette.mid
                    radius: 4
                }

                onTextEdited: {
                    if (root.currentView === "albums") {
                        playerBackend.searchAlbums(text)
                    } else if (root.currentView === "artists") {
                        playerBackend.searchArtists(text)
                    } else {
                        playerBackend.searchTracks(text)
                    }
                }
            }
        }

        ToolButton {
            Layout.fillWidth: true
            text: "All tracks"
            icon.name: "go-home"
            display: AbstractButton.TextBesideIcon
            onClicked: {
                clearSearch()
                playerBackend.filterByAlbum("")
                root.currentView = "tracks"
            }
        }

        ToolButton {
            Layout.fillWidth: true
            text: "Albums"
            icon.name: "media-optical-audio"
            display: AbstractButton.TextBesideIcon
            onClicked: {
                clearSearch()
                root.currentView = "albums"
            }
        }

        ToolButton {
            Layout.fillWidth: true
            text: "Artists"
            icon.name: "system-users"
            display: AbstractButton.TextBesideIcon
            onClicked: {
                clearSearch()
                root.currentView = "artists"
            }
        }

        Item { Layout.fillHeight: true }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: playerBackend.scanInProgress

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: "Scanning library…"
                    color: sysPalette.windowText
                    font.pixelSize: 11
                    opacity: 0.7
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Text {
                    visible: playerBackend.scanTotal > 0
                    text: playerBackend.scanProgress + " / " + playerBackend.scanTotal
                    color: sysPalette.windowText
                    font.pixelSize: 11
                    font.family: "Monospace"
                    opacity: 0.6
                }
            }

            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, playerBackend.scanTotal)
                value: playerBackend.scanProgress
                indeterminate: playerBackend.scanTotal === 0
            }
        }

        ToolButton {
            Layout.alignment: Qt.AlignLeft
            icon.name: "settings-configure"
            onClicked: settingsClicked()
        }
    }
}