import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: header
    Layout.fillWidth: true
    Layout.margins: 30
    Layout.topMargin: 40
    spacing: 30

    property alias avatarContent: avatarContainer.data
    property string title: ""
    property string subtitle: ""
    property bool   subtitleVisible: subtitle.length > 0

    signal backRequested()
    signal playRequested()

    Item {
        id: avatarContainer
        width: 220; height: 220
    }

    ColumnLayout {
        spacing: 10
        Layout.alignment: Qt.AlignBottom

        Text {
            text: header.title
            font.pixelSize: 42
            font.bold: true
            color: sysPalette.text
        }
        Text {
            visible: header.subtitleVisible
            text: header.subtitle
            font.pixelSize: 18
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
