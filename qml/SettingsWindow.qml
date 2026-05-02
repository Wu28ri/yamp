import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Window {
    id: settingsWindow
    width: 600
    height: 450
    title: "Settings — YAMP"
    color: sysPalette.window

    SystemPalette { id: sysPalette; colorGroup: SystemPalette.Active }

    FolderDialog {
        id: folderDialog
        title: "Choose Music Folder"
        onAccepted: {
            appSettings.addFolder(selectedFolder)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: settingsTabBar
            Layout.fillWidth: true

            TabButton {
                text: "General"
            }
            TabButton {
                text: "Library"
            }
        }

        StackLayout {
            id: settingsStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 20
            currentIndex: settingsTabBar.currentIndex

            Item {
                id: generalTab

                Label {
                    anchors.centerIn: parent
                    text: "General Settings (Work in Progress)"
                    color: sysPalette.text
                    opacity: 0.5
                }
            }

            ColumnLayout {
                id: libraryTab
                spacing: 15

                Label {
                    text: "Music Folders"
                    font.pixelSize: 16
                    font.bold: true
                    color: sysPalette.text
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: sysPalette.base
                    border.color: sysPalette.mid
                    border.width: 1
                    radius: 4
                    clip: true

                    ListView {
                        id: folderList
                        anchors.fill: parent
                        anchors.margins: 5
                        model: appSettings.musicFolders

                        delegate: RowLayout {
                            width: ListView.view.width
                            height: 40

                            Label {
                                text: modelData
                                color: sysPalette.text
                                Layout.fillWidth: true
                                Layout.leftMargin: 5
                                elide: Text.ElideMiddle
                            }

                            Button {
                                icon.name: "edit-delete"
                                text: "Remove"
                                display: AbstractButton.TextBesideIcon
                                onClicked: appSettings.removeFolder(index)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Button {
                        icon.name: "folder-add"
                        text: "Add Folder"
                        display: AbstractButton.TextBesideIcon
                        onClicked: folderDialog.open()
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        icon.name: "edit-clear-history"
                        text: "Clear Database"
                        display: AbstractButton.TextBesideIcon
                        onClicked: appSettings.clearDatabase()
                    }

                    Button {
                        icon.name: "view-refresh"
                        text: "Rescan Library"
                        display: AbstractButton.TextBesideIcon
                        onClicked: appSettings.rescanDatabase()
                    }
                }
            }
        }
    }
}