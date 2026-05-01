import QtQuick

Item {
    id: header

    property string label: ""
    property int    sortColumn: 0
    property int    activeColumn: -1
    property bool   ascending: true
    property int    alignment: Qt.AlignLeft

    signal toggled(int column)

    implicitWidth: row.implicitWidth

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        anchors.left:  header.alignment === Qt.AlignLeft  ? parent.left  : undefined
        anchors.right: header.alignment === Qt.AlignRight ? parent.right : undefined
        spacing: 5

        Text {
            visible: header.alignment === Qt.AlignRight && header.activeColumn === header.sortColumn
            text: header.ascending ? "▲" : "▼"
            color: sysPalette.windowText
            opacity: 0.5
            font.pixelSize: 10
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: header.label
            color: sysPalette.windowText
            opacity: 0.5
        }
        Text {
            visible: header.alignment === Qt.AlignLeft && header.activeColumn === header.sortColumn
            text: header.ascending ? "▲" : "▼"
            color: sysPalette.windowText
            opacity: 0.5
            font.pixelSize: 10
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: header.toggled(header.sortColumn)
    }
}
