import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: bottomBar
    implicitHeight: 105
    color: sysPalette.mid

    readonly property bool compact: width < 920
    readonly property bool veryCompact: width < 720

    Menu {
        id: metadataTextMenu
        property var editor: null

        MenuItem {
            text: "Copy"
            enabled: metadataTextMenu.editor && metadataTextMenu.editor.selectedText.length > 0
            onTriggered: metadataTextMenu.editor.copy()
        }
        MenuItem {
            text: "Select all"
            onTriggered: metadataTextMenu.editor.selectAll()
        }
    }

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

    readonly property bool hasActiveTrack: playerBackend.currentPath !== ""
    readonly property var currentTrackCtx: hasActiveTrack
        ? playerBackend.trackContextForPath(playerBackend.currentPath)
        : null
    readonly property bool canNavigateMetadata: currentTrackCtx !== null && currentTrackCtx.inLibrary === true

    function openMetadata(target) {
        const ctx = bottomBar.currentTrackCtx
        if (!ctx || !ctx.inLibrary) return
        if (target === "artist" && ctx.artist) {
            root.openArtist(ctx.artist)
        } else if (target === "album" && ctx.album) {
            root.openAlbum(ctx.album, ctx.albumArtist, playerBackend.currentPath)
        }
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
        enabled: playerBackend.duration > 0
        live: true
        onMoved: playerBackend.position = value
        hoverEnabled: true

        Binding {
            progressSlider.value: playerBackend.position
            when: !progressSlider.pressed
            restoreMode: Binding.RestoreBindingOrValue
        }

        background: Rectangle {
            x: progressSlider.leftPadding
            implicitHeight: 4
            width: progressSlider.availableWidth
            y: 0
            color: sysPalette.base

            Rectangle {
                width: playerBackend.duration > 0 ? progressSlider.visualPosition * parent.width : 0
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
            visible: playerBackend.duration > 0 && (progressSlider.hovered || progressSlider.pressed)
        }
    }

    Text {
        anchors {
            left:           parent.left
            verticalCenter: progressSlider.verticalCenter
            leftMargin:     10
        }
        text: bottomBar.formatTime(progressSlider.pressed ? progressSlider.value : playerBackend.position)
        color: sysPalette.windowText
        opacity: playerBackend.duration > 0 && (progressSlider.hovered || progressSlider.pressed) ? 0.5 : 0.0
        font.pixelSize: 10
        font.family: "Monospace"
        z: 11
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    Text {
        anchors {
            right:          parent.right
            verticalCenter: progressSlider.verticalCenter
            rightMargin:    10
        }
        text: bottomBar.formatTime(playerBackend.duration)
        color: sysPalette.windowText
        opacity: playerBackend.duration > 0 && (progressSlider.hovered || progressSlider.pressed) ? 0.5 : 0.0
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
            leftMargin:  bottomBar.compact ? 10 : 15
            rightMargin: bottomBar.compact ? 8 : 15
        }
        spacing: bottomBar.compact ? 8 : 10
        clip: true

        Rectangle {
            id: bottomCoverContainer
            Layout.preferredWidth: bottomBar.veryCompact ? 48 : 60
            Layout.preferredHeight: bottomBar.veryCompact ? 48 : 60
            color: root.hasValidImage(bottomCoverImage) ? "transparent" : sysPalette.base
            radius: 4

            Text {
                anchors.centerIn: parent
                visible: !root.hasValidImage(bottomCoverImage)
                text: "♪"
                color: sysPalette.windowText
                opacity: 0.35
                font.pixelSize: bottomBar.veryCompact ? 18 : 22
            }

            Image {
                id: bottomCoverImage
                anchors.fill: parent
                visible: root.hasValidImage(bottomCoverImage)
                fillMode: Image.PreserveAspectCrop
                source: root.coverSource(playerBackend.currentPath)
                sourceSize: Qt.size(Math.ceil(60 * Screen.devicePixelRatio), Math.ceil(60 * Screen.devicePixelRatio))
                asynchronous: true
                opacity: coverClickArea.pressed ? 0.8 : 1.0
            }

            MouseArea {
                id: coverClickArea
                readonly property bool keepsSearchOpen: true
                anchors.fill: parent
                enabled: bottomBar.hasActiveTrack
                hoverEnabled: true
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.jumpToCurrentTrack()
            }

            ToolTip.visible: coverClickArea.containsMouse && bottomBar.hasActiveTrack
            ToolTip.delay: 500
            ToolTip.text: "Jump to playing track (Ctrl+L)"
        }

        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 20

                TextMetrics {
                    id: titleMetrics
                    text: titleField.text
                    font: titleField.font
                }

                TextEdit {
                    id: titleField
                    width: Math.min(Math.ceil(titleMetrics.advanceWidth) + 6, parent.width)
                    height: parent.height
                    text: bottomBar.hasActiveTrack ? playerBackend.currentTitle : "Not playing"
                    color: sysPalette.text
                    opacity: bottomBar.hasActiveTrack ? 1.0 : 0.45
                    font.bold: true
                    textFormat: Text.PlainText
                    readOnly: true
                    selectByMouse: bottomBar.hasActiveTrack
                    persistentSelection: true
                    clip: true
                    verticalAlignment: TextEdit.AlignVCenter
                    renderType: Text.NativeRendering
                    onActiveFocusChanged: {
                        if (activeFocus) artistAlbumField.deselect()
                    }

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        enabled: bottomBar.hasActiveTrack
                        onTapped: {
                            if (titleField.selectedText.length === 0) titleField.selectAll()
                            metadataTextMenu.editor = titleField
                            metadataTextMenu.popup()
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 18
                visible: bottomBar.hasActiveTrack

                TextMetrics {
                    id: artistAlbumMetrics
                    text: artistAlbumField.text
                    font: artistAlbumField.font
                }

                TextEdit {
                    id: artistAlbumField
                    readonly property string hoveredMetadata: bottomBar.canNavigateMetadata && metadataHover.hovered
                        ? navigationTargetAt(metadataHover.point.position.x, metadataHover.point.position.y) : ""
                    width: Math.min(Math.ceil(artistAlbumMetrics.advanceWidth) + 6, parent.width)
                    height: parent.height
                    text: playerBackend.currentArtist + (playerBackend.currentAlbum ? " — " + playerBackend.currentAlbum : "")
                    color: sysPalette.windowText
                    font.pixelSize: 12
                    textFormat: Text.PlainText
                    readOnly: true
                    selectByMouse: true
                    persistentSelection: true
                    clip: true
                    verticalAlignment: TextEdit.AlignVCenter
                    renderType: Text.NativeRendering
                    onActiveFocusChanged: {
                        if (activeFocus) titleField.deselect()
                    }

                    function navigationTargetAt(x, y) {
                        let position = positionAt(x, y)
                        const caret = positionToRectangle(position)
                        if (position > 0) {
                            const previous = positionToRectangle(position - 1)
                            if (x >= Math.min(previous.x, caret.x) && x < Math.max(previous.x, caret.x))
                                --position
                        }
                        if (position < 0 || position >= length) return ""
                        const start = positionToRectangle(position)
                        const end = positionToRectangle(position + 1)
                        if (x < Math.min(start.x, end.x) || x >= Math.max(start.x, end.x)
                                || y < start.y || y >= start.y + start.height) return ""
                        const artistLength = playerBackend.currentArtist.length
                        if (position < artistLength) return "artist"
                        if (playerBackend.currentAlbum && position >= artistLength + 3) return "album"
                        return ""
                    }

                    function navigationUnderline(target) {
                        if (!target) return Qt.rect(0, 0, 0, 0)
                        const artistLength = playerBackend.currentArtist.length
                        const start = positionToRectangle(target === "artist" ? 0 : artistLength + 3)
                        const end = positionToRectangle(target === "artist" ? artistLength : text.length)
                        return Qt.rect(Math.min(start.x, end.x), start.y + start.height - 1,
                                       Math.abs(end.x - start.x), 1)
                    }

                    Rectangle {
                        readonly property rect underline: artistAlbumField.navigationUnderline(artistAlbumField.hoveredMetadata)
                        x: underline.x
                        y: underline.y
                        width: underline.width
                        height: underline.height
                        color: sysPalette.highlight
                    }

                    HoverHandler {
                        id: metadataHover
                        cursorShape: artistAlbumField.hoveredMetadata ? Qt.PointingHandCursor : Qt.IBeamCursor
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        acceptedModifiers: Qt.NoModifier
                        gesturePolicy: TapHandler.DragThreshold
                        onTapped: (eventPoint) => {
                            if (tapCount !== 1 || artistAlbumField.selectedText.length > 0) return
                            if (titleField.selectedText.length > 0)
                                titleField.deselect()
                            const destination = artistAlbumField.navigationTargetAt(eventPoint.position.x, eventPoint.position.y)
                            if (destination)
                                bottomBar.openMetadata(destination)
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: {
                            if (artistAlbumField.selectedText.length === 0) artistAlbumField.selectAll()
                            metadataTextMenu.editor = artistAlbumField
                            metadataTextMenu.popup()
                        }
                    }
                }
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
        spacing: bottomBar.compact ? 6 : 15

        ToolButton {
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

        ToolButton {
            id: repeatButton
            icon.name: "media-playlist-repeat"
            icon.color: playerBackend.repeatMode !== 0 ? sysPalette.highlight : sysPalette.windowText
            opacity: playerBackend.repeatMode !== 0 ? 1.0 : 0.5
            onClicked: playerBackend.cycleRepeatMode()

            Text {
                visible: playerBackend.repeatMode === 2
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 4
                anchors.bottomMargin: 3
                text: "1"
                font.pixelSize: 9
                font.bold: true
                color: sysPalette.highlight
            }

            ToolTip.visible: hovered
            ToolTip.text: {
                if (playerBackend.repeatMode === 1) return "Repeat: All"
                if (playerBackend.repeatMode === 2) return "Repeat: One"
                return "Repeat: Off"
            }
        }
    }

    RowLayout {
        anchors {
            left:        centerControls.right
            right:       parent.right
            top:         progressSlider.bottom
            bottom:      parent.bottom
            leftMargin:  bottomBar.compact ? 8 : 15
            rightMargin: bottomBar.compact ? 10 : 15
        }
        spacing: bottomBar.compact ? 6 : 15
        clip: true

        Item { Layout.fillWidth: true }

        Text {
            visible: !bottomBar.compact && playerBackend.currentTechInfo.length > 0
            text: playerBackend.currentTechInfo
            color: sysPalette.windowText
            opacity: 0.35
            font.pixelSize: 10
            font.family: "Monospace"
            elide: Text.ElideLeft
            Layout.fillWidth: true
            Layout.maximumWidth: implicitWidth
            Layout.alignment: Qt.AlignVCenter
        }

        ToolButton {
            icon.name: "format-justify-left"
            icon.color: root.currentView === "lyrics" ? sysPalette.highlight : sysPalette.windowText
            opacity: root.currentView === "lyrics" ? 1.0 : 0.6
            enabled: playerBackend.currentPath !== ""
            onClicked: root.toggleLyrics()
            ToolTip.visible: hovered
            ToolTip.text: "Lyrics"
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
            readonly property bool effectivelyMuted: playerBackend.isMuted || playerBackend.volume <= 0
            property real volumeBeforeMute: 1.0

            icon.name: {
                if (effectivelyMuted) return "audio-volume-muted"
                if (playerBackend.volume < 0.34) return "audio-volume-low"
                if (playerBackend.volume < 0.67) return "audio-volume-medium"
                return "audio-volume-high"
            }
            opacity: enabled ? 0.8 : 0.35
            enabled: playerBackend.volumeControllable
            ToolTip.visible: hovered
            ToolTip.text: enabled
                ? (effectivelyMuted ? "Unmute" : ("Mute (" + Math.round(playerBackend.volume * 100) + "%)"))
                : "Volume is locked in direct ALSA mode\n(enable Software volume in Audio settings)"
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
            Layout.preferredWidth: bottomBar.veryCompact ? 80 : (bottomBar.compact ? 105 : 150)
            Layout.minimumWidth: 60
            Layout.maximumWidth: 150
            Layout.fillWidth: true
            from: 0
            to: 100
            enabled: playerBackend.volumeControllable
            opacity: enabled ? 1.0 : 0.4
            onMoved: {
                if (playerBackend.isMuted && value > 0)
                    playerBackend.isMuted = false
                playerBackend.volume = value / 100
            }

            WheelHandler {
                enabled: volumeSlider.enabled
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => {
                    const step = (event.angleDelta.y / 120.0) * 0.05
                    if (step === 0) return
                    const next = Math.max(0.0, Math.min(1.0, playerBackend.volume + step))
                    if (playerBackend.isMuted && next > 0)
                        playerBackend.isMuted = false
                    playerBackend.volume = next
                }
            }

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
