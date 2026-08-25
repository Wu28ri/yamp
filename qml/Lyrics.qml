import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: lyricsView
    color: sysPalette.window
    clip: true

    function centerCurrentLine(animated) {
        const index = playerBackend.currentLyricIndex
        if (lyricsList.count === 0)
            return

        lyricsScroll.stop()
        const previousY = lyricsList.contentY
        if (index < 0) {
            lyricsList.positionViewAtBeginning()
            lyricsList.contentY = lyricsList.originY - lyricsList.topMargin
        } else {
            lyricsList.positionViewAtIndex(index, ListView.Center)
        }
        const targetY = lyricsList.contentY

        if (animated && Math.abs(targetY - previousY) > 1) {
            lyricsList.contentY = previousY
            lyricsScroll.from = previousY
            lyricsScroll.to = targetY
            lyricsScroll.start()
        }
    }

    Component.onCompleted: Qt.callLater(() => centerCurrentLine(false))

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 8
            Layout.bottomMargin: 8
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: "Lyrics"
                color: sysPalette.text
                font.pixelSize: 16
                font.bold: true
            }

            ToolButton {
                visible: !lyricsList.followPlayback && playerBackend.lyricsSynchronized
                text: "Follow"
                icon.name: "go-jump"
                display: AbstractButton.TextBesideIcon
                onClicked: {
                    lyricsList.followPlayback = true
                    lyricsView.centerCurrentLine(true)
                }
                ToolTip.visible: hovered
                ToolTip.text: "Return to current line"
            }

            Text {
                visible: playerBackend.lyricsSynchronized
                text: "SYNCED"
                color: sysPalette.highlight
                opacity: 0.8
                font.pixelSize: 9
                font.bold: true
                font.letterSpacing: 1.2
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: sysPalette.windowText
            opacity: 0.1
        }

        ListView {
            id: lyricsList
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 0
            Layout.rightMargin: 0
            clip: true
            model: playerBackend.lyricsLines
            spacing: 4
            topMargin: height / 2 - 32
            bottomMargin: height / 2 - 32
            boundsBehavior: Flickable.StopAtBounds
            property bool followPlayback: true

            onDraggingChanged: {
                if (dragging && playerBackend.lyricsSynchronized) {
                    lyricsScroll.stop()
                    followPlayback = false
                }
            }
            onMovementStarted: {
                if (!lyricsScroll.running && playerBackend.lyricsSynchronized)
                    followPlayback = false
            }

            NumberAnimation {
                id: lyricsScroll
                target: lyricsList
                property: "contentY"
                duration: 360
                easing.type: Easing.OutCubic
            }

            onCountChanged: {
                if (count > 0) Qt.callLater(() => lyricsView.centerCurrentLine(false))
            }
            onHeightChanged: {
                if (playerBackend.currentLyricIndex < 0 && count > 0)
                    Qt.callLater(() => lyricsView.centerCurrentLine(false))
            }

            Connections {
                target: playerBackend

                function onCurrentLyricIndexChanged() {
                    if (lyricsList.followPlayback)
                        lyricsView.centerCurrentLine(true)
                }

                function onLyricsChanged() {
                    lyricsList.followPlayback = true
                    Qt.callLater(() => lyricsView.centerCurrentLine(false))
                }
            }

            delegate: ItemDelegate {
                id: lyricLine
                required property int index
                required property var modelData
                readonly property bool active: index === playerBackend.currentLyricIndex

                width: lyricsList.width
                height: Math.max(48, lineText.implicitHeight + 18)
                padding: 0
                hoverEnabled: modelData.timeMs >= 0
                enabled: true
                onClicked: {
                    if (modelData.timeMs >= 0) {
                        lyricsList.followPlayback = true
                        playerBackend.position = modelData.timeMs
                    }
                }

                background: Rectangle {
                    radius: 8
                    color: lyricLine.hovered ? sysPalette.mid : "transparent"
                    opacity: lyricLine.hovered ? 0.45 : 0.0
                }

                contentItem: Text {
                    id: lineText
                    text: lyricLine.modelData.text
                    textFormat: Text.PlainText
                    color: lyricLine.active ? sysPalette.highlight : sysPalette.windowText
                    opacity: !playerBackend.lyricsSynchronized || lyricLine.active ? 1.0 : 0.42
                    font.pixelSize: lyricLine.active
                                    ? Math.min(42, Math.max(22, lyricsList.width / 30))
                                    : Math.min(34, Math.max(18, lyricsList.width / 40))
                    font.bold: lyricLine.active
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.Wrap
                    renderType: Text.NativeRendering

                    Behavior on color { ColorAnimation { duration: 180 } }
                    Behavior on opacity { NumberAnimation { duration: 180 } }
                }
            }

            Text {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 420)
                visible: lyricsList.count === 0
                text: "No lyrics found\nAdd an .lrc file next to the track or embed lyrics in its tags."
                color: sysPalette.windowText
                opacity: 0.45
                font.pixelSize: 15
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }

            ScrollBar.vertical: ScrollBar { }
        }
    }
}
