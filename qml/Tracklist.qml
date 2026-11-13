import QtQuick
import QtQuick.Controls

ListView {
    id: listView
    model: playerBackend.trackModel
    spacing: 0
    clip: true
    reuseItems: true
    headerPositioning: ListView.OverlayHeader
    cacheBuffer: 1200

    property int  discCount: 1
    property bool alive: true
    property bool ignoreNextScroll: false
    property int  pendingScrollIdx: -1
    property bool persistScroll: false
    property bool suppressSave: false
    property bool needsInitialRestore: true
    property real scrollOffsetBeforeReset: 0

    readonly property real headerHeight: headerItem ? headerItem.height : 40
    readonly property real topRestY: originY - headerHeight

    Component.onDestruction: alive = false

    function restoreScroll(offset) {
        if (!alive || width <= 0 || height <= 0) return false
        const targetRow = Math.ceil(offset / 56) + 30
        playerBackend.trackModel.ensureFetchedTo(targetRow)
        forceLayout()
        const minY = topRestY
        const maxY = Math.max(minY, contentHeight - height + originY)
        contentY = Math.max(minY, Math.min(minY + offset, maxY))
        return true
    }

    Timer {
        id: suppressTimer
        interval: 80
        onTriggered: {
            listView.suppressSave = false
            if (listView.alive && listView.persistScroll && !root.searchActive)
                root.savedTracklistScrollOffset = Math.max(0, listView.contentY - listView.topRestY)
        }
    }

    Connections {
        target: playerBackend.trackModel
        enabled: listView.alive && listView.parent !== null
        function onModelAboutToBeReset() {
            listView.scrollOffsetBeforeReset = Math.max(0, listView.contentY - listView.topRestY)
            listView.suppressSave = true
        }
        function onModelReset() {
            const offset = listView.persistScroll
                           ? (root.searchActive ? 0 : root.savedTracklistScrollOffset)
                           : (root.searchActive ? 0 : listView.scrollOffsetBeforeReset)
            Qt.callLater(function() {
                if (!listView.alive || !listView.parent) return
                listView.restoreScroll(offset)
                suppressTimer.restart()
            })
        }
    }

    Component.onCompleted: {
        persistScroll = (root.currentView === "tracks")
        if (persistScroll) {
            const offset = root.searchActive ? 0 : root.savedTracklistScrollOffset
            if (restoreScroll(offset))
                needsInitialRestore = false
            Qt.callLater(function() {
                if (!listView.alive) return
                if (listView.restoreScroll(offset))
                    listView.needsInitialRestore = false
                suppressTimer.restart()
            })
        } else {
            needsInitialRestore = false
        }
    }

    onHeightChanged: {
        if (persistScroll && needsInitialRestore && width > 0 && height > 0) {
            const offset = root.searchActive ? 0 : root.savedTracklistScrollOffset
            if (restoreScroll(offset))
                needsInitialRestore = false
        }
    }

    onContentYChanged: {
        if (persistScroll && !suppressSave && !root.searchActive)
            root.savedTracklistScrollOffset = Math.max(0, contentY - topRestY)
    }

    Timer {
        id: scrollDebounce
        interval: 80
        onTriggered: {
            const realIdx = playerBackend.getRowForPath(playerBackend.currentPath)
            if (realIdx < 0) return
            playerBackend.trackModel.ensureFetchedTo(realIdx)
            listView.pendingScrollIdx = realIdx
            scrollApply.running = false
            scrollApply.running = true
        }
    }

    FrameAnimation {
        id: scrollApply
        running: false
        property real elapsed: 0
        readonly property real timeoutSec: 0.5

        onRunningChanged: if (running) elapsed = 0

        onTriggered: {
            const idx = listView.pendingScrollIdx
            if (idx < 0) { running = false; return }

            elapsed += frameTime

            if (idx >= listView.count) {
                playerBackend.trackModel.ensureFetchedTo(idx)
                if (elapsed > timeoutSec) { running = false; listView.pendingScrollIdx = -1 }
                return
            }

            listView.forceLayout()
            listView.positionViewAtIndex(idx, ListView.Center)
            listView.forceLayout()

            const hit = listView.indexAt(listView.width / 2,
                                         listView.contentY + listView.height / 2)
            if (hit === idx || elapsed > timeoutSec) {
                listView.pendingScrollIdx = -1
                running = false
            }
        }
    }

    readonly property bool showAlbumCol: width >= 540 && root.currentView !== "albumDetail"
    readonly property real colNum:   width < 460 ? 32 : 50
    readonly property real colCover: 45
    readonly property real colAlbum: showAlbumCol ? Math.min(200, Math.max(110, (width - 360) * 0.35)) : 0
    readonly property real colTime:  60

    Connections {
        target: root
        enabled: listView.alive && listView.parent !== null
        function onLocateCurrentTrackRequested() {
            scrollDebounce.restart()
        }
    }

    Connections {
        target: playerBackend
        function onCurrentIndexChanged() {
            if (listView.ignoreNextScroll) {
                listView.ignoreNextScroll = false
                return
            }
            scrollDebounce.restart()
        }
    }

    readonly property real rightGutter: vbar.needed ? vbar.width : 0

    ScrollBar.vertical: SmoothScrollBar {
        id: vbar
        flickable: listView
        topPadding: 44
        bottomPadding: 6
    }

    section.property: (root.currentView === "albumDetail" && listView.discCount > 1) ? "discNo" : ""
    section.criteria: ViewSection.FullString
    section.delegate: Item {
        id: discSection
        width: listView.width - listView.rightGutter
        height: 36

        required property string section

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            text: {
                const n = parseInt(discSection.section, 10)
                return "Disc " + (n > 0 ? n : 1)
            }
            color: sysPalette.windowText
            opacity: 0.65
            font.pixelSize: 12
            font.bold: true
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.right: parent.right
            anchors.rightMargin: 20
            height: 1
            color: sysPalette.windowText
            opacity: 0.08
        }
    }

    header: Rectangle {
        id: headerRect
        z: 10
        width: listView.width; height: 40
        color: sysPalette.window

        readonly property string sortCol: playerBackend.sortColumn
        readonly property bool   sortAsc: playerBackend.sortAscending

        function toggleSort(column) {
            const nextAsc = sortCol === column ? !sortAsc : true
            playerBackend.sortTracks(column, nextAsc)
        }

        Row {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 24
            spacing: 10

            SortableHeader {
                width: listView.colNum
                height: parent.height
                label: "#"
                sortColumn: "track_no"
                activeColumn: headerRect.sortCol
                ascending: headerRect.sortAsc
                onToggled: column => headerRect.toggleSort(column)
            }

            Item {
                width: listView.colCover
                height: parent.height
            }

            SortableHeader {
                width: Math.max(60, parent.width
                                - listView.colNum
                                - listView.colCover
                                - (listView.showAlbumCol ? listView.colAlbum + 10 : 0)
                                - listView.colTime
                                - 30)
                height: parent.height
                label: "Name"
                sortColumn: "title"
                activeColumn: headerRect.sortCol
                ascending: headerRect.sortAsc
                onToggled: column => headerRect.toggleSort(column)
            }

            SortableHeader {
                visible: listView.showAlbumCol
                width: listView.colAlbum
                height: parent.height
                label: "Album"
                sortColumn: "album"
                activeColumn: headerRect.sortCol
                ascending: headerRect.sortAsc
                onToggled: column => headerRect.toggleSort(column)
            }

            SortableHeader {
                width: listView.colTime
                height: parent.height
                label: "Time"
                sortColumn: "duration"
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
            text: "Add to queue"
            onTriggered: playerBackend.addToQueue(globalTrackMenu.trackPath)
        }
        MenuSeparator {}
        MenuItem {
            text: "Go to artist"
            onTriggered: {
                const ctx = playerBackend.trackContextForPath(globalTrackMenu.trackPath)
                if (!ctx || !ctx.artist) return
                root.openArtist(ctx.artist)
            }
        }
        MenuItem {
            text: "Go to album"
            onTriggered: {
                const ctx = playerBackend.trackContextForPath(globalTrackMenu.trackPath)
                if (!ctx || !ctx.album) return
                root.openAlbum(ctx.album, ctx.albumArtist, globalTrackMenu.trackPath)
            }
        }
        MenuSeparator {}
        MenuItem {
            text: "Show in file manager"
            icon.name: "folder-open"
            onTriggered: playerBackend.openInFileManager(globalTrackMenu.trackPath)
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 12
        visible: listView.count === 0

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: {
                if (appSettings.musicFolders.length === 0) return "No music folders added"
                if (playerBackend.scanInProgress) return "Scanning music library…"
                if (root.searchActive) return "No matching tracks"
                return "No tracks found"
            }
            color: sysPalette.windowText
            opacity: 0.55
            font.pixelSize: 15
        }

        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: appSettings.musicFolders.length === 0
            text: "Add Music Folder…"
            icon.name: "list-add"
            onClicked: root.openAddFolderSettings()
        }
    }

    delegate: Item {
        id: trackItem
        readonly property bool keepsSearchOpen: true
        width: listView.width - listView.rightGutter
        height: 56

        required property int    index
        required property string title
        required property string artist
        required property string album
        required property string path
        required property int    duration
        required property int    trackNo
        required property int    discNo

        readonly property bool isCurrent: playerBackend.currentPath === path
        readonly property bool rowHovered: rowHover.hovered

        HoverHandler {
            id: rowHover
        }

        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: {
                listView.ignoreNextScroll = true
                playerBackend.playMusic(trackItem.path)
            }
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                globalTrackMenu.trackPath = trackItem.path
                globalTrackMenu.popup()
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 14 - listView.rightGutter
            anchors.topMargin: 1
            anchors.bottomMargin: 1
            radius: 6
            color: trackItem.isCurrent
                   ? sysPalette.highlight
                   : (trackItem.rowHovered ? sysPalette.highlight : "transparent")
            opacity: trackItem.isCurrent ? 0.15 : (trackItem.rowHovered ? 0.08 : 0)
        }

        Text {
            id: idxTxt
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            width: listView.colNum
            text: {
                const showTrackNum = (root.currentView === "albumDetail" || playerBackend.sortColumn === "track_no")
                                     && trackItem.trackNo > 0
                if (!showTrackNum) return trackItem.index + 1
                if (root.currentView !== "albumDetail" && trackItem.discNo > 1) {
                    const tn = trackItem.trackNo < 10 ? ("0" + trackItem.trackNo) : String(trackItem.trackNo)
                    return trackItem.discNo + "-" + tn
                }
                return trackItem.trackNo
            }
            color: trackItem.isCurrent ? sysPalette.highlight : sysPalette.windowText
            opacity: 0.7
            font.pixelSize: 13
            renderType: Text.NativeRendering
        }

        Rectangle {
            id: coverContainer
            anchors.left: idxTxt.right
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 40; height: 40
            radius: 4
            color: root.hasValidImage(trackCoverImage) ? "transparent" : sysPalette.mid

            Text {
                anchors.centerIn: parent
                visible: !root.hasValidImage(trackCoverImage)
                text: "♪"
                font.pixelSize: 14
                color: sysPalette.windowText
                opacity: 0.3
            }

            Image {
                id: trackCoverImage
                anchors.fill: parent
                visible: root.hasValidImage(trackCoverImage)
                source: root.coverSource(trackItem.path)
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(Math.ceil(40 * Screen.devicePixelRatio), Math.ceil(40 * Screen.devicePixelRatio))
                asynchronous: true
            }
        }

        Column {
            anchors.left: coverContainer.right
            anchors.leftMargin: 15
            anchors.right: listView.showAlbumCol ? albumCell.left : timeTxt.left
            anchors.rightMargin: 15
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                width: parent.width
                height: 18
                text: trackItem.title
                elide: Text.ElideRight
                color: trackItem.isCurrent ? sysPalette.highlight : sysPalette.text
                font.bold: true; font.pixelSize: 14
                renderType: Text.NativeRendering
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                width: parent.width
                height: 16
                text: trackItem.artist
                elide: Text.ElideRight
                color: sysPalette.windowText
                opacity: 0.6
                font.pixelSize: 12
                renderType: Text.NativeRendering
                verticalAlignment: Text.AlignVCenter
            }
        }

        Item {
            id: albumCell
            visible: listView.showAlbumCol
            anchors.right: timeTxt.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: listView.colAlbum
            height: 28

            Text {
                id: albumTxt
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, parent.width)
                text: trackItem.album
                elide: Text.ElideRight
                color: albumLinkMouse.containsMouse ? sysPalette.highlight : sysPalette.windowText
                opacity: albumLinkMouse.containsMouse ? 0.9 : 0.55
                font.pixelSize: 13
                font.underline: albumLinkMouse.containsMouse
                renderType: Text.NativeRendering

                MouseArea {
                    id: albumLinkMouse
                    readonly property bool keepsSearchOpen: true
                    anchors.fill: parent
                    anchors.topMargin: -4
                    anchors.bottomMargin: -4
                    enabled: trackItem.album.length > 0
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        const ctx = playerBackend.trackContextForPath(trackItem.path)
                        root.openAlbum(ctx.album || trackItem.album, ctx.albumArtist || "", trackItem.path)
                    }
                }
            }
        }

        Text {
            id: timeTxt
            anchors.right: parent.right; anchors.rightMargin: 24 - listView.rightGutter
            anchors.verticalCenter: parent.verticalCenter
            width: listView.colTime
            horizontalAlignment: Text.AlignRight
            text: {
                const s = Math.max(0, trackItem.duration)
                const h = Math.floor(s / 3600)
                const m = Math.floor((s % 3600) / 60)
                const sec = s % 60
                const pad = (n) => (n < 10 ? "0" + n : "" + n)
                if (h > 0) return h + ":" + pad(m) + ":" + pad(sec)
                return m + ":" + pad(sec)
            }
            color: sysPalette.windowText
            opacity: 0.6
            font.pixelSize: 13
            renderType: Text.NativeRendering
        }
    }
}
