import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: header
    Layout.fillWidth: true
    Layout.margins: compact ? 18 : 30
    Layout.topMargin: compact ? 24 : 40
    spacing: compact ? 18 : 30

    readonly property bool compact: width > 0 && width < 560
    readonly property real avatarSize: compact ? 140 : 220

    property alias avatarContent: avatarContainer.data
    property string title: ""
    property string subtitle: ""
    property bool   subtitleVisible: subtitle.length > 0

    signal backRequested()
    signal playRequested()

    Item {
        id: avatarContainer
        width: header.avatarSize; height: header.avatarSize
        Layout.preferredWidth: header.avatarSize
        Layout.preferredHeight: header.avatarSize
    }

    ColumnLayout {
        spacing: 10
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignBottom

        Text {
            Layout.fillWidth: true
            text: header.title
            font.pixelSize: header.compact ? 26 : 42
            font.bold: true
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.Wrap
            color: sysPalette.text
        }
        Text {
            Layout.fillWidth: true
            visible: header.subtitleVisible
            text: header.subtitle
            font.pixelSize: header.compact ? 15 : 18
            elide: Text.ElideRight
            opacity: 0.6
            color: sysPalette.windowText
        }

        RowLayout {
            spacing: 15
            Layout.topMargin: 10

            ToolButton {
                id: backButton
                icon.name: "go-previous"
                background: Rectangle {
                    implicitWidth: 48; implicitHeight: 48
                    radius: 24
                    color: backButton.hovered ? sysPalette.mid : "transparent"
                }
                onClicked: header.backRequested()
            }

            ToolButton {
                id: playButton
                icon.name: "media-playback-start"
                icon.width: 32; icon.height: 32
                background: Rectangle {
                    implicitWidth: 64; implicitHeight: 64
                    radius: 32
                    color: playButton.hovered ? sysPalette.mid : "transparent"
                }
                onClicked: header.playRequested()
            }
        }
    }
    Item { Layout.fillWidth: true }
}
