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
                text: "Audio"
            }
            TabButton {
                text: "Library"
            }
            TabButton {
                text: "Performance"
            }
            TabButton {
                text: "Last.fm"
            }
        }

        StackLayout {
            id: settingsStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 20
            currentIndex: settingsTabBar.currentIndex

            ColumnLayout {
                id: generalTab
                spacing: 18

                Label {
                    text: "ReplayGain"
                    font.pixelSize: 16
                    font.bold: true
                    color: sysPalette.text
                }

                Label {
                    text: "Normalize playback loudness using ReplayGain tags embedded in your files. Tracks without tags play at their original level."
                    color: sysPalette.windowText
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    CheckBox {
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

                    Label {
                        text: "Mode"
                        color: sysPalette.text
                        Layout.preferredWidth: 180
                    }

                    ComboBox {
                        id: rgModeBox
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
                        Layout.preferredWidth: 180
                    }

                    SpinBox {
                        id: rgPreampBox
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

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    enabled: rgEnabledBox.checked

                    CheckBox {
                        id: rgClipBox
                        text: "Prevent clipping (use peak tags to cap gain)"
                        checked: appSettings.replayGainClipProtect
                        onToggled: appSettings.replayGainClipProtect = checked
                    }
                }

                Item { Layout.fillHeight: true }
            }

            ColumnLayout {
                id: audioTab
                spacing: 18

                Label {
                    text: "Audio Output"
                    font.pixelSize: 16
                    font.bold: true
                    color: sysPalette.text
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        text: "Bit-perfect output (ALSA)"
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
                    id: bitPerfectBlock
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
                            Layout.preferredWidth: 180
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

                        CheckBox {
                            id: swVolumeBox
                            text: "Software volume"
                            checked: appSettings.audioSoftwareVolume
                            onToggled: appSettings.audioSoftwareVolume = checked
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "Lock device to 0 dB"
                            icon.name: "audio-volume-high"
                            onClicked: {
                                const ok = playerBackend.lockDeviceToZeroDb()
                                lockResultLabel.text = ok
                                    ? "Hardware mixer set to 0 dB."
                                    : "Could not set hardware mixer — device may have no software-controllable volume."
                                lockResultLabel.error = !ok
                                lockResultTimer.restart()
                            }
                        }
                    }

                    Label {
                        id: lockResultLabel
                        property bool error: false
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: error ? "#d35" : sysPalette.windowText
                        opacity: text.length > 0 ? 0.85 : 0.0
                        Behavior on opacity { NumberAnimation { duration: 150 } }
                        Timer {
                            id: lockResultTimer
                            interval: 4000
                            onTriggered: lockResultLabel.text = ""
                        }
                    }
                }

                Item { Layout.fillHeight: true }
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

            ColumnLayout {
                id: lastfmTab
                spacing: 18

                Label {
                    text: "Last.fm Scrobbling"
                    font.pixelSize: 16
                    font.bold: true
                    color: sysPalette.text
                }

                Label {
                    text: "Submit your listening history to Last.fm. Tracks shorter than 30 seconds are not scrobbled."
                    color: sysPalette.windowText
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                CheckBox {
                    id: lastfmEnabledBox
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

                Item { Layout.fillHeight: true }
            }
        }
    }
}
