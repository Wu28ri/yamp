import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ListView {
    id: listView
    model: playerBackend.trackModel
    spacing: 0
    clip: true
    reuseItems: true
    headerPositioning: ListView.OverlayHeader
    cacheBuffer: 2000

    property bool ignoreNextScroll: false

    readonly property real colNum:   50
    readonly property real colCover: 60
    readonly property real colAlbum: 200
    readonly property real colTime:  60

    Connections {
        target: playerBackend
        function onCurrentIndexChanged() {
            if (listView.ignoreNextScroll) {
                listView.ignoreNextScroll = false
                return
            }
            const realIdx = playerBackend.getRowForPath(playerBackend.currentPath)
            if (realIdx !== -1) {
                Qt.callLater(listView.positionViewAtIndex, realIdx, ListView.Center)
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        onWheel: (wheel) => {
            const multiplier = 3.0
            const step = (wheel.angleDelta.y / 120) * 120 * multiplier
            const minY = listView.originY
            const maxY = Math.max(minY, listView.contentHeight - listView.height + listView.originY)
            listView.contentY = Math.max(minY, Math.min(listView.contentY - step, maxY))
        }
    }

    ScrollBar.vertical: ScrollBar {
        id: vBar
        policy: ScrollBar.AlwaysOn
        topPadding: 40
        background: Item { implicitWidth: 8 }
        contentItem: Rectangle {
            implicitWidth: 8
            radius: 4
            color: sysPalette.highlight
            opacity: (vBar.hovered || vBar.pressed || listView.moving) ? 1.0 : 0.4
        }
    }

    header: Rectangle {
        id: headerRect
        z: 10
        width: listView.width; height: 40
        color: sysPalette.window

        readonly property int colTitleColumn  : 1
        readonly property int colAlbumColumn  : 3
        readonly property int colDurationCol  : 5
        readonly property int colTrackNoCol   : 7

        property int  sortCol: colTitleColumn
        property bool sortAsc: true

        function toggleSort(column) {
            sortAsc = sortCol === column ? !sortAsc : true
            sortCol = column
            playerBackend.sortTracks(column, sortAsc)
        }

        Row {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 10

            SortableHeader {
                width: listView.colNum
                height: parent.height
                label: "#"
                sortColumn: headerRect.colTrackNoCol
                activeColumn: headerRect.sortCol
                ascending: headerRect.sortAsc
                onToggled: column => headerRect.toggleSort(column)
            }

            Item { width: listView.colCover; height: parent.height }

            SortableHeader {
                width: parent.width - (listView.colNum + listView.colCover + listView.colAlbum + listView.colTime + 40)
                height: parent.height
                label: "Name"
                sortColumn: headerRect.colTitleColumn
                activeColumn: headerRect.sortCol
                ascending: headerRect.sortAsc
                onToggled: column => headerRect.toggleSort(column)
            }

            SortableHeader {
                width: listView.colAlbum
                height: parent.height
                label: "Album"
                sortColumn: headerRect.colAlbumColumn
                activeColumn: headerRect.sortCol
                ascending: headerRect.sortAsc
                onToggled: column => headerRect.toggleSort(column)
            }

            SortableHeader {
                width: listView.colTime
                height: parent.height
                label: "Time"
                sortColumn: headerRect.colDurationCol
                activeColumn: headerRect.sortCol
                ascending: headerRect.sortAsc
                alignment: Qt.AlignRight
                onToggled: column => headerRect.toggleSort(column)
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width; height: 1
            color: sysPalette.windowText
            opacity: 0.1
        }
    }

    Menu {
        id: globalTrackMenu
        property string trackPath: ""
        MenuItem {
            text: "Play"
            onTriggered: {
                listView.ignoreNextScroll = true
                playerBackend.playMusic(globalTrackMenu.trackPath)
            }
        }
        MenuItem {
            text: "Play next"
            onTriggered: playerBackend.addPlayNext(globalTrackMenu.trackPath)
        }
        MenuItem {
            text: "Show in file manager"
            icon.name: "folder-open"
            onTriggered: playerBackend.openInFileManager(globalTrackMenu.trackPath)
        }
    }

    delegate: ItemDelegate {
        id: trackItem
        width: listView.width
        height: 56

        required property int    index
        required property string title
        required property string artist
        required property string album
        required property string path
        required property int    duration

        readonly property bool isCurrent: playerBackend.currentPath === path

        onClicked: {
            listView.ignoreNextScroll = true
            playerBackend.playMusic(trackItem.path)
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    globalTrackMenu.trackPath = trackItem.path
                    globalTrackMenu.popup()
                }
            }
        }

        background: Rectangle {
            color: trackItem.isCurrent ? sysPalette.highlight : (trackItem.hovered ? sysPalette.highlight : "transparent")
            opacity: trackItem.isCurrent ? 0.15 : (trackItem.hovered ? 0.08 : 0)
        }

        contentItem: Item {
            anchors.fill: parent

            Text {
                id: idxTxt
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                width: listView.colNum
                text: trackItem.index + 1
                color: trackItem.isCurrent ? sysPalette.highlight : sysPalette.windowText
                opacity: 0.7
                font.pixelSize: 13
            }

            Rectangle {
                id: coverContainer
                anchors.left: idxTxt.right
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 40; height: 40
                radius: 4; clip: true
                color: sysPalette.mid

                Image {
                    anchors.fill: parent
                    source: root.coverSource(trackItem.path)
                    fillMode: Image.PreserveAspectCrop
                    sourceSize: Qt.size(40, 40)
                    asynchronous: true
                }
            }

            Column {
                anchors.left: coverContainer.right; anchors.leftMargin: 15
                anchors.right: albumTxt.left;       anchors.rightMargin: 15
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    width: parent.width
                    text: trackItem.title
                    elide: Text.ElideRight
                    color: trackItem.isCurrent ? sysPalette.highlight : sysPalette.text
                    font.bold: true; font.pixelSize: 14
                }
                Text {
                    width: parent.width
                    text: trackItem.artist
                    elide: Text.ElideRight
                    color: sysPalette.windowText
                    opacity: 0.6
                    font.pixelSize: 12
                }
            }

            Text {
                id: albumTxt
                anchors.right: timeTxt.left; anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: listView.colAlbum
                text: trackItem.album
                elide: Text.ElideRight
                color: sysPalette.windowText
                opacity: 0.5
                font.pixelSize: 13
            }

            Text {
                id: timeTxt
                anchors.right: parent.right; anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                width: listView.colTime
                horizontalAlignment: Text.AlignRight
                text: {
                    const s = trackItem.duration
                    const m = Math.floor(s / 60)
                    const sec = s % 60
                    return m + ":" + (sec < 10 ? "0" : "") + sec
                }
                color: sysPalette.windowText
                opacity: 0.6
                font.pixelSize: 13
            }
        }
    }
}
