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

    function openTab(index) {
        settingsTabBar.currentIndex = index
        show()
        raise()
        requestActivate()
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        onActivated: settingsWindow.close()
    }

    SystemPalette { id: sysPalette; colorGroup: SystemPalette.Active }

    FolderDialog {
        id: folderDialog
        title: "Choose Music Folder"
        onAccepted: {
            appSettings.addFolder(selectedFolder)
        }
    }

    Dialog {
        id: clearDbDialog
        title: "Clear Database"
        modal: true
        anchors.centerIn: parent
        width: 380
        standardButtons: Dialog.Yes | Dialog.Cancel
        onAccepted: appSettings.clearDatabase()

        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            color: sysPalette.text
            text: "Clear the music library database and remove watched folders?\n\nYour audio files on disk will not be modified."
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: settingsTabBar
            Layout.fillWidth: true
            implicitWidth: 0

            Repeater {
                model: ["General", "Audio", "Library", "Performance", "Last.fm", "About"]
                TabButton {
                    required property string modelData
                    text: modelData
                    width: settingsTabBar.width / settingsTabBar.count
                }
            }
        }

        ScrollView {
            id: settingsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            StackLayout {
                width: settingsScroll.availableWidth - 48
                height: Math.max(implicitHeight, settingsScroll.availableHeight - 48)
                x: 24
                y: 24
                currentIndex: settingsTabBar.currentIndex

            ColumnLayout {
                spacing: settingsWindow.pageSpacing

                Label {
                    text: "Playback"
                    font.pixelSize: 15
                    font.bold: true
                    color: sysPalette.text
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Switch {
                        text: "Gapless playback"
                        checked: appSettings.gaplessPlayback
                        onToggled: appSettings.gaplessPlayback = checked
                    }

                    Item { Layout.fillWidth: true }
                }

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
                        id: rgPreampSpin
                        Layout.preferredWidth: 120
                        from: -1500
                        to: 1500
                        stepSize: 25
                        value: Math.round(appSettings.replayGainPreampDb * 100)
                        onValueModified: appSettings.replayGainPreampDb = value / 100
                        validator: DoubleValidator {
                            bottom: -15.0
                            top: 15.0
                            decimals: 2
                            notation: DoubleValidator.StandardNotation
                        }
                        textFromValue: function(value) {
                            return (value / 100).toFixed(2)
                        }
                        valueFromText: function(text) {
                            const parsed = parseFloat(text.replace(",", "."))
                            if (isNaN(parsed)) return rgPreampSpin.value
                            return Math.max(-1500, Math.min(1500, Math.round(parsed * 100)))
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

                    Label {
                        text: "Fallback gain"
                        color: sysPalette.text
                        Layout.preferredWidth: settingsWindow.labelWidth
                    }

                    SpinBox {
                        id: rgFallbackSpin
                        Layout.preferredWidth: 120
                        from: -1500
                        to: 1500
                        stepSize: 25
                        value: Math.round(appSettings.replayGainFallbackDb * 100)
                        onValueModified: appSettings.replayGainFallbackDb = value / 100
                        validator: DoubleValidator {
                            bottom: -15.0
                            top: 15.0
                            decimals: 2
                            notation: DoubleValidator.StandardNotation
                        }
                        textFromValue: function(value) {
                            return (value / 100).toFixed(2)
                        }
                        valueFromText: function(text) {
                            const parsed = parseFloat(text.replace(",", "."))
                            if (isNaN(parsed)) return rgFallbackSpin.value
                            return Math.max(-1500, Math.min(1500, Math.round(parsed * 100)))
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "Applied to tracks without ReplayGain tags"
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

                        Binding {
                            bitPerfectSwitch.checked: appSettings.audioBitPerfect
                            restoreMode: Binding.RestoreBindingOrValue
                        }
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

                            function refresh() {
                                model = playerBackend.listHardwareDevices()
                                const index = indexOfValue(appSettings.audioDevice)
                                currentIndex = index >= 0 ? index : (count > 0 ? 0 : -1)
                                if (index < 0 && count > 0
                                        && bitPerfectSwitch.checked
                                        && (!appSettings.audioDevice || appSettings.audioDevice === "auto")) {
                                    appSettings.audioDevice = currentValue
                                }
                            }

                            onVisibleChanged: if (visible) refresh()
                            onActivated: {
                                if (currentIndex >= 0) appSettings.audioDevice = currentValue
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
                        onClicked: clearDbDialog.open()
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
                            currentIndex = idx >= 0 ? idx : 4
                        }
                        Component.onCompleted: syncFromSettings()
                        onActivated: appSettings.coverMaxEdge = model[currentIndex]
                    }

                    Label {
                        text: "px"
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

                Switch {
                    text: "Load artist profile pictures online"
                    checked: artistImages.enabled
                    onToggled: artistImages.enabled = checked
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        text: {
                            if (lastfm.authenticated)
                                return "Connected as " + lastfm.username
                            if (lastfm.awaitingAuth)
                                return "Waiting for browser authorization…"
                            return "Not connected"
                        }
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
                    text: "YAMP " + Qt.application.version
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
}
