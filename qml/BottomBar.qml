import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: bottomBar
    implicitHeight: 105
    color: sysPalette.mid

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
        to: Math.max(playerBackend.duration, 1)
        value: playerBackend.position
        onMoved: playerBackend.position = value

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

    RowLayout {
        anchors {
            left:       parent.left
            top:        progressSlider.bottom
            bottom:     parent.bottom
            leftMargin: 15
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
            Text {
                text: playerBackend.currentTitle
                color: sysPalette.text
                font.bold: true
            }
            Text {
                text: playerBackend.currentArtist + (playerBackend.currentAlbum ? " — " + playerBackend.currentAlbum : "")
                color: sysPalette.windowText
                font.pixelSize: 12
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
            icon.name: playerBackend.isMuted ? "audio-volume-muted" : "audio-volume-high"
            opacity: 0.8
            onClicked: playerBackend.isMuted = !playerBackend.isMuted
        }

        Slider {
            id: volumeSlider
            width: 150
            from: 0
            to: 100
            value: playerBackend.volume * 100
            onMoved: playerBackend.volume = value / 100

            ToolTip {
                parent: volumeSlider.handle
                visible: volumeSlider.pressed
                text: Math.round(volumeSlider.value) + "%"
            }
        }
    }
}
