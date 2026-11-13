import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: header
    Layout.fillWidth: true
    Layout.margins: compact ? 18 : 30
    Layout.topMargin: compact ? 20 : 34
    Layout.bottomMargin: compact ? 14 : 22
    spacing: compact ? 18 : 30

    readonly property bool compact: (width > 0 && width < 560)
                                    || (Window.window !== null && Window.window.height < 560)
    readonly property real avatarSize: compact ? 128 : 204

    property alias avatarContent: avatarContainer.data
    property string title: ""
    property string subtitle: ""
    property bool   subtitleVisible: subtitle.length > 0
    property bool   subtitleClickable: false
    property string metaText: ""

    signal backRequested()
    signal playRequested()
    signal shuffleRequested()
    signal subtitleClicked()

    Item {
        id: avatarContainer
        Layout.preferredWidth: header.avatarSize
        Layout.preferredHeight: header.avatarSize
        Layout.minimumWidth: header.avatarSize
        Layout.maximumWidth: header.avatarSize
    }

    ColumnLayout {
        spacing: 6
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignBottom

        Text {
            Layout.fillWidth: true
            text: header.title
            font.pixelSize: header.compact ? 24 : 38
            font.bold: true
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.Wrap
            color: sysPalette.text
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: subtitleLabel.implicitHeight
            visible: header.subtitleVisible

            Text {
                id: subtitleLabel
                width: Math.min(implicitWidth, parent.width)
                text: header.subtitle
                font.pixelSize: header.compact ? 15 : 18
                font.underline: header.subtitleClickable && subtitleHover.hovered
                elide: Text.ElideRight
                opacity: header.subtitleClickable && subtitleHover.hovered ? 0.95 : 0.72
                color: header.subtitleClickable && subtitleHover.hovered
                       ? sysPalette.highlight
                       : sysPalette.windowText

                HoverHandler {
                    id: subtitleHover
                    enabled: header.subtitleClickable
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    enabled: header.subtitleClickable
                    onTapped: header.subtitleClicked()
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: header.metaText.length > 0
            text: header.metaText
            font.pixelSize: 13
            elide: Text.ElideRight
            opacity: 0.5
            color: sysPalette.windowText
        }

        RowLayout {
            spacing: 12
            Layout.topMargin: 8

            ToolButton {
                id: backButton
                icon.name: "go-previous"
                icon.width: 22; icon.height: 22
                Layout.preferredWidth: 44; Layout.preferredHeight: 44
                onClicked: header.backRequested()
                ToolTip.visible: hovered
                ToolTip.text: "Back"
            }

            ToolButton {
                id: playButton
                icon.name: "media-playback-start"
                icon.width: 28; icon.height: 28
                Layout.preferredWidth: 56; Layout.preferredHeight: 56
                onClicked: header.playRequested()
                ToolTip.visible: hovered
                ToolTip.text: "Play"
            }

            ToolButton {
                id: shuffleButton
                icon.name: "media-playlist-shuffle"
                icon.width: 22; icon.height: 22
                Layout.preferredWidth: 44; Layout.preferredHeight: 44
                onClicked: header.shuffleRequested()
                ToolTip.visible: hovered
                ToolTip.text: "Shuffle"
            }
        }
    }
}
