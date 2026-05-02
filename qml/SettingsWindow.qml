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
            TabButton {
                text: "Performance"
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

            ColumnLayout {
                id: performanceTab
                spacing: 18

                Label {
                    text: "Cover Art Cache"
                    font.pixelSize: 16
                    font.bold: true
                    color: sysPalette.text
                }

                Label {
                    text: "Lower values reduce memory usage at the cost of more disk reads while scrolling. Changing the resolution clears the in-memory cache."
                    color: sysPalette.windowText
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        text: "Cover resolution"
                        color: sysPalette.text
                        Layout.preferredWidth: 180
                    }

                    ComboBox {
                        id: edgeBox
                        Layout.preferredWidth: 120
                        model: [128, 192, 256, 384, 512, 768, 1024]
                        function syncFromSettings() {
                            const idx = model.indexOf(appSettings.coverMaxEdge)
                            currentIndex = idx >= 0 ? idx : 3
                        }
                        Component.onCompleted: syncFromSettings()
                        onActivated: appSettings.coverMaxEdge = model[currentIndex]
                    }

                    Label {
                        text: edgeBox.model[edgeBox.currentIndex] + " px"
                        color: sysPalette.windowText
                        opacity: 0.6
                    }

                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        text: "Source cache"
                        color: sysPalette.text
                        Layout.preferredWidth: 180
                    }

                    SpinBox {
                        id: sourceBudgetBox
                        Layout.preferredWidth: 120
                        from: 4
                        to: 1024
                        stepSize: 4
                        value: appSettings.coverSourceBudgetMb
                        onValueModified: appSettings.coverSourceBudgetMb = value
                    }

                    Label {
                        text: "MB"
                        color: sysPalette.windowText
                        opacity: 0.6
                    }

                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        text: "Scaled cache"
                        color: sysPalette.text
                        Layout.preferredWidth: 180
                    }

                    SpinBox {
                        id: scaledBudgetBox
                        Layout.preferredWidth: 120
                        from: 2
                        to: 512
                        stepSize: 2
                        value: appSettings.coverScaledBudgetMb
                        onValueModified: appSettings.coverScaledBudgetMb = value
                    }

                    Label {
                        text: "MB"
                        color: sysPalette.windowText
                        opacity: 0.6
                    }

                    Item { Layout.fillWidth: true }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}