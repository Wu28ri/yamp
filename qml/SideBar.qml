import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: sideBar
    implicitWidth: 250
    color: sysPalette.base

    signal settingsClicked()
    signal expandRequested()
    signal restoreRequested()

    readonly property bool compact: width < 140
    readonly property bool searchOpen: searchField.searchOpen

    function clearSearch() {
        searchField.text = ""
        searchField.searchOpen = false
        playerBackend.searchTracks("")
        playerBackend.searchAlbums("")
        playerBackend.searchArtists("")
        sideBar.restoreRequested()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: sideBar.compact ? 6 : 10
        spacing: 15

        RowLayout {
            Layout.fillWidth: true
            spacing: 5

            Item {
                visible: sideBar.compact
                Layout.fillWidth: true
            }

            ToolButton {
                icon.name: "system-search"
                onClicked: {
                    if (sideBar.compact) {
                        searchField.searchOpen = true
                        sideBar.expandRequested()
                        Qt.callLater(() => searchField.forceActiveFocus())
                    } else {
                        searchField.searchOpen = !searchField.searchOpen
                    }
                    if (searchField.searchOpen) {
                        searchField.forceActiveFocus()
                    } else {
                        clearSearch()
                    }
                }

                ToolTip.visible: sideBar.compact && hovered
                ToolTip.text: "Search"
            }

            Item {
                visible: sideBar.compact
                Layout.fillWidth: true
            }

            TextField {
                id: searchField
                property bool searchOpen: false
                property bool keepsSearchOpen: true
                Layout.fillWidth: true
                placeholderText: {
                    if (root.currentView === "albums") return "Find album..."
                    if (root.currentView === "artists") return "Find artist..."
                    return "Find track..."
                }
                visible: searchOpen && !sideBar.compact
                color: sysPalette.text

                background: Rectangle {
                    color: sysPalette.mid
                    radius: 4
                }

                onTextEdited: {
                    if (root.currentView === "lyrics")
                        root.currentView = "tracks"
                    if (root.currentView === "albums") {
                        playerBackend.searchAlbums(text)
                    } else if (root.currentView === "artists") {
                        playerBackend.searchArtists(text)
                    } else {
                        playerBackend.searchTracks(text)
                    }
                }

                Keys.onEscapePressed: clearSearch()
            }
        }

        ToolButton {
            Layout.fillWidth: true
            text: "All tracks"
            icon.name: "go-home"
            display: sideBar.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
            onClicked: {
                clearSearch()
                playerBackend.filterByAlbum("")
                root.currentView = "tracks"
            }
            ToolTip.visible: sideBar.compact && hovered
            ToolTip.text: text
        }

        ToolButton {
            Layout.fillWidth: true
            text: "Albums"
            icon.name: "media-optical-audio"
            display: sideBar.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
            onClicked: {
                clearSearch()
                root.currentView = "albums"
            }
            ToolTip.visible: sideBar.compact && hovered
            ToolTip.text: text
        }

        ToolButton {
            Layout.fillWidth: true
            text: "Artists"
            icon.name: "system-users"
            display: sideBar.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
            onClicked: {
                clearSearch()
                root.currentView = "artists"
            }
            ToolTip.visible: sideBar.compact && hovered
            ToolTip.text: text
        }

        Item { Layout.fillHeight: true }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: playerBackend.scanInProgress && !sideBar.compact

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
            Layout.alignment: sideBar.compact ? Qt.AlignHCenter : Qt.AlignLeft
            icon.name: "settings-configure"
            onClicked: settingsClicked()
            ToolTip.visible: sideBar.compact && hovered
            ToolTip.text: "Settings"
        }
    }
}
