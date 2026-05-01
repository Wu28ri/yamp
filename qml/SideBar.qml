import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs

Rectangle {
    implicitWidth: 250
    color: sysPalette.base

    FolderDialog {
        id: folderDialog
        title: "Choose directory to scan"
        onAccepted: playerBackend.scanFolder(folderDialog.selectedFolder)
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
                        searchField.text = ""
                        playerBackend.searchTracks("")
                    }
                }
            }

            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: "Find track..."
                visible: false
                color: sysPalette.text

                background: Rectangle {
                    color: sysPalette.mid
                    radius: 4
                }

                onTextEdited: playerBackend.searchTracks(text)
            }
        }

        ToolButton {
            Layout.fillWidth: true
            text: "All tracks"
            icon.name: "go-home"
            display: AbstractButton.TextBesideIcon
            onClicked: {
                playerBackend.filterByAlbum("")
                root.currentView = "tracks"
            }
        }

        ToolButton {
            Layout.fillWidth: true
            text: "Albums"
            icon.name: "media-optical-audio"
            display: AbstractButton.TextBesideIcon
            onClicked: root.currentView = "albums"
        }

        ToolButton {
            Layout.fillWidth: true
            text: "Add music"
            icon.name: "folder-add"
            onClicked: folderDialog.open()
        }

        Item { Layout.fillHeight: true }
    }
}
