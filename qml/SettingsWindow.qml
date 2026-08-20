import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Window {
    id: settingsWindow
    width: 640
    height: 480
    minimumWidth: 560
    minimumHeight: 400
    title: "Settings — YAMP"
    color: sysPalette.window

    readonly property int labelWidth: 160
    readonly property int pageSpacing: 14

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
            implicitWidth: 0

            TabButton {
                text: "General"
                width: settingsTabBar.width / settingsTabBar.count
            }
            TabButton {
                text: "Audio"
                width: settingsTabBar.width / settingsTabBar.count
            }
            TabButton {
                text: "Library"
                width: settingsTabBar.width / settingsTabBar.count
            }
            TabButton {
                text: "Performance"
                width: settingsTabBar.width / settingsTabBar.count
            }
            TabButton {
                text: "Last.fm"
                width: settingsTabBar.width / settingsTabBar.count
            }
            TabButton {
                text: "About"
                width: settingsTabBar.width / settingsTabBar.count
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 24
            currentIndex: settingsTabBar.currentIndex

            ColumnLayout {
                spacing: settingsWindow.pageSpacing

                Label {
                    text: "ReplayGain"
                    font.pixelSize: 15
                    font.bold: true
                    color: sysPalette.text
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Switch {
                        id: rgEnabledBox
                        text: "Enable ReplayGain"
                        checked: appSettings.replayGainEnabled
                        onToggled: appSettings.replayGainEnabled = checked
                    }

                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    enabled: rgEnabledBox.checked

                    Switch {
                        text: "Prevent clipping"
                        enabled: rgEnabledBox.checked
                        checked: appSettings.replayGainClipProtect
                        onToggled: appSettings.replayGainClipProtect = checked
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    enabled: rgEnabledBox.checked

                    Label {
                        text: "Mode"
                        color: sysPalette.text
                        Layout.preferredWidth: settingsWindow.labelWidth
                    }

                    ComboBox {
                        Layout.preferredWidth: 160
                        model: ["Track gain", "Album gain"]
                        currentIndex: appSettings.replayGainMode
                        onActivated: appSettings.replayGainMode = currentIndex
                    }

                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    enabled: rgEnabledBox.checked

                    Label {
                        text: "Pre-amp"
                        color: sysPalette.text
                        Layout.preferredWidth: settingsWindow.labelWidth
                    }

                    SpinBox {
                        Layout.preferredWidth: 120
                        from: -1500
                        to: 1500
                        stepSize: 25
                        value: Math.round(appSettings.replayGainPreampDb * 100)
                        onValueModified: appSettings.replayGainPreampDb = value / 100
                        textFromValue: function(value) {
                            return (value / 100).toFixed(2)
                        }
                        valueFromText: function(text) {
                            return Math.round(parseFloat(text) * 100)
                        }
                    }

                    Label {
                        text: "dB"
                        color: sysPalette.windowText
                        opacity: 0.6
                    }

                    Item { Layout.fillWidth: true }
                }

                Item { Layout.fillHeight: true }
            }

            ColumnLayout {
                spacing: settingsWindow.pageSpacing

                Label {
                    text: "Audio Output"
                    font.pixelSize: 15
                    font.bold: true
                    color: sysPalette.text
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        text: "Direct ALSA"
                        color: sysPalette.text
                        Layout.fillWidth: true
                    }

                    Switch {
                        id: bitPerfectSwitch
                        checked: appSettings.audioBitPerfect
                        onToggled: appSettings.audioBitPerfect = checked
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    visible: bitPerfectSwitch.checked
                    enabled: bitPerfectSwitch.checked

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            text: "Hardware device"
                            color: sysPalette.text
                            Layout.preferredWidth: settingsWindow.labelWidth
                        }

                        ComboBox {
                            id: deviceBox
                            Layout.fillWidth: true
                            textRole: "description"
                            valueRole: "name"

                            property var deviceList: []

                            function refresh() {
                                const list = playerBackend.listHardwareDevices()
                                deviceList = list
                                model = list
                                const cur = appSettings.audioDevice
                                let idx = -1
                                for (let i = 0; i < list.length; ++i) {
                                    if (list[i].name === cur) { idx = i; break }
                                }
                                if (idx < 0 && list.length > 0) {
                                    idx = 0
                                    appSettings.audioDevice = list[0].name
                                }
                                currentIndex = idx
                            }

                            Component.onCompleted: refresh()
                            onVisibleChanged: if (visible) refresh()
                            onActivated: {
                                if (currentIndex >= 0 && currentIndex < deviceList.length) {
                                    appSettings.audioDevice = deviceList[currentIndex].name
                                }
                            }
                        }

                        ToolButton {
                            icon.name: "view-refresh"
                            ToolTip.visible: hovered
                            ToolTip.text: "Re-scan ALSA devices"
                            onClicked: deviceBox.refresh()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            text: "Software volume"
                            color: sysPalette.text
                            Layout.fillWidth: true
                        }

                        Switch {
                            checked: appSettings.audioSoftwareVolume
                            onToggled: appSettings.audioSoftwareVolume = checked
                        }
                    }

                }

                Item { Layout.fillHeight: true }
            }

            ColumnLayout {
                spacing: settingsWindow.pageSpacing

                Label {
                    text: "Music Folders"
                    font.pixelSize: 15
                    font.bold: true
                    color: sysPalette.text
                }

                Frame {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: 0

                    ListView {
                        anchors.fill: parent
                        anchors.margins: 5
                        model: appSettings.musicFolders
                        clip: true
                        ScrollBar.vertical: ScrollBar {}

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
                spacing: settingsWindow.pageSpacing

                Label {
                    text: "Cover Art Cache"
                    font.pixelSize: 15
                    font.bold: true
                    color: sysPalette.text
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        text: "Cover resolution"
                        color: sysPalette.text
                        Layout.preferredWidth: settingsWindow.labelWidth
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
                        Layout.preferredWidth: settingsWindow.labelWidth
                    }

                    SpinBox {
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
                        Layout.preferredWidth: settingsWindow.labelWidth
                    }

                    SpinBox {
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

            ColumnLayout {
                spacing: settingsWindow.pageSpacing

                Label {
                    text: "Last.fm Scrobbling"
                    font.pixelSize: 15
                    font.bold: true
                    color: sysPalette.text
                }

                Switch {
                    text: "Enable scrobbling"
                    checked: lastfm.enabled
                    onToggled: lastfm.enabled = checked
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        text: lastfm.authenticated
                              ? ("Connected as " + lastfm.username)
                              : "Not connected"
                        color: sysPalette.text
                        Layout.fillWidth: true
                    }

                    Button {
                        text: "Connect"
                        visible: !lastfm.authenticated && !lastfm.awaitingAuth
                        onClicked: lastfm.startAuth()
                    }

                    Button {
                        text: "Cancel"
                        visible: lastfm.awaitingAuth
                        onClicked: lastfm.cancelAuth()
                    }

                    Button {
                        text: "Disconnect"
                        visible: lastfm.authenticated
                        onClicked: lastfm.logout()
                    }
                }

                RowLayout {
                    Button {
                        text: "Scrobbling powered by AudioScrobbler / Last.fm"
                        onClicked: Qt.openUrlExternally("https://www.last.fm/")
                    }

                    Button {
                        text: "API Terms"
                        flat: true
                        onClicked: Qt.openUrlExternally("https://www.last.fm/api/tos")
                    }
                }

                Item { Layout.fillHeight: true }
            }

            ColumnLayout {
                spacing: settingsWindow.pageSpacing

                Label {
                    text: "YAMP 0.2"
                    font.pixelSize: 18
                    font.bold: true
                    color: sysPalette.text
                }

                Label {
                    text: "Copyright (C) 2026 Wu28ri"
                    color: sysPalette.text
                }

                Label {
                    Layout.fillWidth: true
                    text: "Licensed under GPL-3.0-only. This program comes with absolutely no warranty."
                    wrapMode: Text.WordWrap
                    color: sysPalette.windowText
                    opacity: 0.75
                }

                Button {
                    text: "Source code and license"
                    onClicked: Qt.openUrlExternally("https://github.com/Wu28ri/yamp")
                }

                Button {
                    text: "Third-party notices"
                    onClicked: Qt.openUrlExternally(
                        "https://github.com/Wu28ri/yamp/blob/main/THIRD_PARTY_NOTICES.md")
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
