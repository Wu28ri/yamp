import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: bottomBar
    implicitHeight: 105
    color: sysPalette.mid

    function formatTime(ms) {
        if (!ms || ms < 0 || isNaN(ms)) return "0:00"
        const totalSec = Math.floor(ms / 1000)
        const h = Math.floor(totalSec / 3600)
        const m = Math.floor((totalSec % 3600) / 60)
        const s = totalSec % 60
        const pad = (n) => (n < 10 ? "0" + n : "" + n)
        if (h > 0) return h + ":" + pad(m) + ":" + pad(s)
        return m + ":" + pad(s)
    }

    Slider {
        id: progressSlider
        anchors {
            top:   parent.top
            left:  parent.left
            right: parent.right
        }
        height: 20
        z: 10
        from: 0
        to: playerBackend.duration > 0 ? playerBackend.duration : 1
        live: true
        onMoved: playerBackend.position = value
        hoverEnabled: true

        Binding {
            progressSlider.value: playerBackend.position
            when: !progressSlider.pressed
            restoreMode: Binding.RestoreBindingOrValue
        }

        background: Rectangle {
            implicitHeight: 4
            width: progressSlider.availableWidth
            y: 0
            color: sysPalette.base

            Rectangle {
                width: progressSlider.visualPosition * parent.width
                height: parent.height
                color: sysPalette.highlight
            }
        }

        handle: Rectangle {
            x: progressSlider.leftPadding + progressSlider.visualPosition * (progressSlider.availableWidth - width)
            y: -4
            width: 12; height: 12
            radius: 6
            color: sysPalette.highlight
            visible: progressSlider.hovered || progressSlider.pressed
        }
    }

    Text {
        id: positionLabel
        anchors {
            left:           parent.left
            verticalCenter: progressSlider.verticalCenter
            leftMargin:     10
        }
        text: bottomBar.formatTime(progressSlider.pressed ? progressSlider.value : playerBackend.position)
        color: sysPalette.windowText
        opacity: (progressSlider.hovered || progressSlider.pressed) ? 0.85 : 0.0
        font.pixelSize: 10
        font.family: "Monospace"
        z: 11
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    Text {
        id: durationLabel
        anchors {
            right:          parent.right
            verticalCenter: progressSlider.verticalCenter
            rightMargin:    10
        }
        text: bottomBar.formatTime(playerBackend.duration)
        color: sysPalette.windowText
        opacity: (progressSlider.hovered || progressSlider.pressed) ? 0.85 : 0.0
        font.pixelSize: 10
        font.family: "Monospace"
        z: 11
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    RowLayout {
        anchors {
            left:        parent.left
            right:       centerControls.left
            top:         progressSlider.bottom
            bottom:      parent.bottom
            leftMargin:  15
            rightMargin: 15
        }
        spacing: 10

        Rectangle {
            width: 60; height: 60
            color: sysPalette.base
            radius: 4
            clip: true
            Image {
                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                source: root.coverSource(playerBackend.currentPath)
                sourceSize: Qt.size(60, 60)
                asynchronous: true
            }
        }

        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Text {
                text: playerBackend.currentTitle
                color: sysPalette.text
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                text: playerBackend.currentArtist + (playerBackend.currentAlbum ? " — " + playerBackend.currentAlbum : "")
                color: sysPalette.windowText
                font.pixelSize: 12
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
    }

    RowLayout {
        id: centerControls
        anchors {
            horizontalCenter: parent.horizontalCenter
            top:    progressSlider.bottom
            bottom: parent.bottom
        }
        spacing: 15

        ToolButton {
            id: shuffleButton
            icon.name: "media-playlist-shuffle"
            icon.color: playerBackend.shuffle ? sysPalette.highlight : sysPalette.windowText
            opacity: playerBackend.shuffle ? 1.0 : 0.5
            onClicked: playerBackend.shuffle = !playerBackend.shuffle

            ToolTip.visible: hovered
            ToolTip.text: "Shuffle"
        }

        ToolButton {
            icon.name: "media-skip-backward"
            onClicked: playerBackend.playPrevious()
        }

        ToolButton {
            icon.name: playerBackend.isPlaying ? "media-playback-pause" : "media-playback-start"
            icon.width: 32; icon.height: 32
            onClicked: playerBackend.togglePlayback()
        }

        ToolButton {
            icon.name: "media-skip-forward"
            onClicked: playerBackend.playNext()
        }
    }

    RowLayout {
        anchors {
            left:        centerControls.right
            right:       parent.right
            top:         progressSlider.bottom
            bottom:      parent.bottom
            rightMargin: 15
        }
        spacing: 15

        Item { Layout.fillWidth: true }

        Text {
            text: playerBackend.currentTechInfo
            color: sysPalette.windowText
            opacity: 0.35
            font.pixelSize: 10
            font.family: "Monospace"
            Layout.alignment: Qt.AlignVCenter
        }

        ToolButton {
            icon.name: "view-list-details"
            icon.color: queuePanel.opened ? sysPalette.highlight : sysPalette.windowText
            opacity: queuePanel.opened ? 1.0 : 0.6
            onClicked: queuePanel.opened = !queuePanel.opened
            ToolTip.visible: hovered
            ToolTip.text: "Play Queue"
        }

        ToolButton {
            id: muteButton
            readonly property bool effectivelyMuted: playerBackend.isMuted || playerBackend.volume <= 0
            property real volumeBeforeMute: 1.0

            icon.name: effectivelyMuted ? "audio-volume-muted" : "audio-volume-high"
            opacity: enabled ? 0.8 : 0.35
            enabled: playerBackend.volumeControllable
            ToolTip.visible: hovered && !enabled
            ToolTip.text: "Volume is locked in bit-perfect mode\n(enable Software volume in Audio settings)"
            onClicked: {
                if (effectivelyMuted) {
                    playerBackend.isMuted = false
                    if (playerBackend.volume <= 0) {
                        playerBackend.volume = volumeBeforeMute > 0 ? volumeBeforeMute : 1.0
                    }
                } else {
                    volumeBeforeMute = playerBackend.volume
                    playerBackend.isMuted = true
                }
            }
        }

        Slider {
            id: volumeSlider
            Layout.preferredWidth: 150
            from: 0
            to: 100
            enabled: playerBackend.volumeControllable
            opacity: enabled ? 1.0 : 0.4
            onMoved: playerBackend.volume = value / 100

            Binding {
                volumeSlider.value: playerBackend.volume * 100
                when: !volumeSlider.pressed
                restoreMode: Binding.RestoreBindingOrValue
            }

            ToolTip {
                parent: volumeSlider.handle
                visible: volumeSlider.pressed
                text: Math.round(volumeSlider.value) + "%"
            }
        }
    }
}

