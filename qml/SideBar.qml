import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: sideBar
    implicitWidth: 250
    color: sysPalette.window

    signal settingsClicked()
    signal expandRequested()
    signal restoreRequested()

    readonly property bool compact: width < 140
    readonly property bool searchOpen: searchField.searchOpen
    readonly property bool searchFocused: searchField.activeFocus

    Timer {
        id: searchDebounce
        interval: 120
        onTriggered: sideBar.applySearch(searchField.text)
    }

    function applySearch(query) {
        if (root.currentView === "lyrics") {
            playerBackend.clearCategoryFilter()
            root.currentView = "tracks"
        }
        if (root.currentView === "albums") {
            playerBackend.searchAlbums(query)
        } else if (root.currentView === "artists") {
            playerBackend.searchArtists(query)
        } else {
            playerBackend.searchTracks(query)
        }
    }

    Rectangle {
        width: 1
        anchors {
            right:  parent.right
            top:    parent.top
            bottom: parent.bottom
        }
        color: sysPalette.windowText
        opacity: 0.1
    }

    function clearSearch() {
        searchDebounce.stop()
        searchField.text = ""
        searchField.searchOpen = false
        playerBackend.searchTracks("")
        playerBackend.searchAlbums("")
        playerBackend.searchArtists("")
        sideBar.restoreRequested()
    }

    function openSearch() {
        searchField.searchOpen = true
        if (sideBar.compact) {
            sideBar.expandRequested()
            Qt.callLater(() => {
                searchField.forceActiveFocus()
                searchField.selectAll()
            })
        } else {
            searchField.forceActiveFocus()
            searchField.selectAll()
        }
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
                Layout.minimumWidth: 0
                implicitWidth: 100
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

                onTextEdited: searchDebounce.restart()

                Keys.onEscapePressed: clearSearch()
            }
        }

        ToolButton {
            readonly property bool activeSection: root.currentView === "tracks"
            Layout.fillWidth: true
            text: "All tracks"
            icon.name: "go-home"
            icon.color: activeSection ? sysPalette.highlight : sysPalette.windowText
            font.bold: activeSection
            display: sideBar.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
            onClicked: {
                root.currentView = "tracks"
                clearSearch()
                playerBackend.filterByAlbum("")
            }
            ToolTip.visible: sideBar.compact && hovered
            ToolTip.text: text
        }

        ToolButton {
            readonly property bool activeSection: root.currentView === "albums" || root.currentView === "albumDetail"
            Layout.fillWidth: true
            text: "Albums"
            icon.name: "media-optical-audio"
            icon.color: activeSection ? sysPalette.highlight : sysPalette.windowText
            font.bold: activeSection
            display: sideBar.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
            onClicked: {
                playerBackend.clearCategoryFilter()
                root.currentView = "albums"
                clearSearch()
            }
            ToolTip.visible: sideBar.compact && hovered
            ToolTip.text: text
        }

        ToolButton {
            readonly property bool activeSection: root.currentView === "artists" || root.currentView === "artistDetail"
            Layout.fillWidth: true
            text: "Artists"
            icon.name: "system-users"
            icon.color: activeSection ? sysPalette.highlight : sysPalette.windowText
            font.bold: activeSection
            display: sideBar.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
            onClicked: {
                playerBackend.clearCategoryFilter()
                root.currentView = "artists"
                clearSearch()
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
