import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Window {
    id: root
    width: 1280
    height: 720
    visible: true
    title: "YAMP"
    color: sysPalette.window

    property string currentView: "tracks"
    property string selectedAlbum: ""
    property string selectedAlbumPath: ""
    property string selectedArtist: ""

    function coverSource(path) {
        return path ? "image://cover/" + encodeURI(path) : ""
    }

    function colorForName(name) {
        if (!name) return Qt.hsla(0.6, 0.35, 0.5, 1.0)
        let h = 0
        for (let i = 0; i < name.length; ++i) {
            h = (h * 31 + name.charCodeAt(i)) >>> 0
        }
        const hue = (h % 360) / 360.0
        return Qt.hsla(hue, 0.45, 0.5, 1.0)
    }

    function initialsForName(name) {
        if (!name) return "?"
        const parts = name.trim().split(/\s+/)
        if (parts.length === 1) return parts[0].substring(0, 2).toUpperCase()
        return (parts[0][0] + parts[parts.length - 1][0]).toUpperCase()
    }

    SystemPalette { id: sysPalette; colorGroup: SystemPalette.Active }

    SettingsWindow {
        id: settingsWindow
        visible: false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        SplitView {
            id: mainSplitter
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 2
                color: "transparent"
            }

            SideBar {
                id: sideBar
                SplitView.preferredWidth: appSettings.sidebarWidth
                SplitView.minimumWidth: 150
                SplitView.maximumWidth: 400
                onSettingsClicked: settingsWindow.show()

                onWidthChanged: sidebarSaveTimer.restart()
            }

            Timer {
                id: sidebarSaveTimer
                interval: 400
                onTriggered: {
                    if (sideBar.width >= 50) appSettings.sidebarWidth = sideBar.width
                }
            }

            Loader {
                id: mainContentLoader
                SplitView.fillWidth: true
                source: {
                    if (root.currentView === "albums") return "Albums.qml"
                    if (root.currentView === "albumDetail") return "AlbumDetail.qml"
                    if (root.currentView === "artists") return "Artists.qml"
                    if (root.currentView === "artistDetail") return "ArtistDetail.qml"
                    return "Tracklist.qml"
                }
            }

            QueuePanel {
                id: queuePanel
                SplitView.preferredWidth: opened ? appSettings.queuePanelWidth : 0
                SplitView.minimumWidth: opened ? 200 : 0
                SplitView.maximumWidth: 600
                opened: appSettings.queuePanelOpen

                onWidthChanged: if (opened) queueSaveTimer.restart()
                onOpenedChanged: appSettings.queuePanelOpen = opened
            }

            Timer {
                id: queueSaveTimer
                interval: 400
                onTriggered: {
                    if (queuePanel.opened && queuePanel.width >= 100) {
                        appSettings.queuePanelWidth = queuePanel.width
                    }
                }
            }
        }

        BottomBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 105
        }
    }
}