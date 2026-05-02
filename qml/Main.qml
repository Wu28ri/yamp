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
                SplitView.preferredWidth: 250
                SplitView.minimumWidth: 150
                SplitView.maximumWidth: 400
                onSettingsClicked: settingsWindow.show()
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
                SplitView.preferredWidth: opened ? 320 : 0
                SplitView.minimumWidth: opened ? 200 : 0
                SplitView.maximumWidth: 600
            }
        }

        BottomBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 105
        }
    }
}