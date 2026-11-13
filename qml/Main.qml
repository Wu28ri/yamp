import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Window {
    id: root
    width: appSettings.windowWidth
    height: appSettings.windowHeight
    minimumWidth: 640
    minimumHeight: 420
    visible: true
    title: "YAMP"
    color: sysPalette.window

    signal locateCurrentTrackRequested()

    Component.onCompleted: {
        if (Screen.devicePixelRatio <= 1.0)
            Qt.application.font.hintingPreference = Font.PreferFullHinting
        if (appSettings.windowMaximized)
            root.showMaximized()
    }

    onClosing: settingsWindow.close()

    Timer {
        id: windowGeomTimer
        interval: 400
        onTriggered: {
            if (root.visibility === Window.Windowed) {
                appSettings.windowWidth = root.width
                appSettings.windowHeight = root.height
            }
        }
    }

    onWidthChanged: if (visibility === Window.Windowed) windowGeomTimer.restart()
    onHeightChanged: if (visibility === Window.Windowed) windowGeomTimer.restart()
    onVisibilityChanged: (vis) => {
        if (vis === Window.Maximized)
            appSettings.windowMaximized = true
        else if (vis === Window.Windowed)
            appSettings.windowMaximized = false
    }

    property string currentView: "tracks"
    property string selectedAlbum: ""
    property string selectedAlbumArtist: ""
    property string selectedAlbumPath: ""
    property string selectedArtist: ""
    property string viewBeforeLyrics: "tracks"
    property var    navBackStack: []
    property var    navForwardStack: []
    property real   savedTracklistScrollOffset: 0
    property real   savedAlbumsScrollOffset: 0
    property real   savedArtistsScrollOffset: 0
    property bool   sidebarSearchExpanded: false
    property real   sidebarWidthBeforeSearch: 56
    property string toastMessage: ""
    property bool   toastVisible: false
    readonly property bool searchActive: sideBar.searchOpen
    readonly property bool textInputFocused: sideBar.searchFocused
        || (activeFocusItem !== null
            && (activeFocusItem.selectByMouse !== undefined
                || activeFocusItem.cursorPosition !== undefined))

    Timer {
        id: toastTimer
        interval: 2400
        onTriggered: root.toastVisible = false
    }

    function showToast(msg) {
        if (!msg) return
        toastMessage = msg
        toastVisible = true
        toastTimer.restart()
    }

    function snapshotNavState() {
        return {
            view: currentView,
            album: selectedAlbum,
            albumArtist: selectedAlbumArtist,
            albumPath: selectedAlbumPath,
            artist: selectedArtist
        }
    }

    function sameNavState(a, b) {
        if (!a || !b) return false
        if (a.view !== b.view) return false
        if (a.view === "albumDetail")
            return a.album === b.album && a.albumArtist === b.albumArtist
        if (a.view === "artistDetail")
            return a.artist === b.artist
        return true
    }

    function pushNavState() {
        const snap = snapshotNavState()
        if (navBackStack.length > 0 && sameNavState(navBackStack[navBackStack.length - 1], snap))
            return
        const next = navBackStack.slice(Math.max(0, navBackStack.length - 31))
        next.push(snap)
        navBackStack = next
        navForwardStack = []
    }

    function applyNavState(s) {
        if (!s) return
        selectedAlbum = s.album || ""
        selectedAlbumArtist = s.albumArtist || ""
        selectedAlbumPath = s.albumPath || ""
        selectedArtist = s.artist || ""
        if (s.view === "albumDetail") {
            playerBackend.filterByAlbum(selectedAlbum, selectedAlbumArtist)
        } else if (s.view === "artistDetail") {
            playerBackend.filterByArtist(selectedArtist)
        } else if (s.view !== "lyrics") {
            playerBackend.clearCategoryFilter()
        }
        currentView = s.view
    }

    function goBack() {
        if (sideBar.searchOpen) {
            sideBar.clearSearch()
            return
        }
        const cur = snapshotNavState()
        const backCopy = navBackStack.slice()
        let target = null
        while (backCopy.length > 0) {
            const candidate = backCopy.pop()
            if (!sameNavState(candidate, cur)) {
                target = candidate
                break
            }
        }
        if (target) {
            navBackStack = backCopy
            const fwdCopy = navForwardStack.slice(Math.max(0, navForwardStack.length - 31))
            if (fwdCopy.length === 0 || !sameNavState(fwdCopy[fwdCopy.length - 1], cur))
                fwdCopy.push(cur)
            navForwardStack = fwdCopy
            applyNavState(target)
            return
        }
        if (currentView === "albumDetail") {
            playerBackend.clearCategoryFilter()
            currentView = "albums"
        } else if (currentView === "artistDetail") {
            playerBackend.clearCategoryFilter()
            currentView = "artists"
        } else if (currentView === "lyrics") {
            currentView = viewBeforeLyrics || "tracks"
        }
    }

    function goForward() {
        if (navForwardStack.length === 0) return
        if (sideBar.searchOpen) sideBar.clearSearch()
        const cur = snapshotNavState()
        const fwdCopy = navForwardStack.slice()
        let target = null
        while (fwdCopy.length > 0) {
            const candidate = fwdCopy.pop()
            if (!sameNavState(candidate, cur)) {
                target = candidate
                break
            }
        }
        if (!target) {
            navForwardStack = fwdCopy
            return
        }
        navForwardStack = fwdCopy
        const backCopy = navBackStack.slice(Math.max(0, navBackStack.length - 31))
        if (backCopy.length === 0 || !sameNavState(backCopy[backCopy.length - 1], cur))
            backCopy.push(cur)
        navBackStack = backCopy
        applyNavState(target)
    }

    function navigateToSection(section) {
        if (currentView === section && !sideBar.searchOpen) {
            playerBackend.clearCategoryFilter()
            return
        }
        pushNavState()
        if (sideBar.searchOpen) sideBar.clearSearch()
        playerBackend.clearCategoryFilter()
        currentView = section
    }

    function toggleLyrics() {
        if (currentView === "lyrics") {
            goBack()
        } else {
            if (!playerBackend.currentPath) return
            viewBeforeLyrics = currentView
            pushNavState()
            currentView = "lyrics"
        }
    }

    function openArtist(artistName) {
        const targetArtist = artistName || "Unknown Artist"
        if (currentView === "artistDetail" && selectedArtist === targetArtist && !sideBar.searchOpen)
            return
        pushNavState()
        if (sideBar.searchOpen) sideBar.clearSearch()
        selectedArtist = targetArtist
        playerBackend.filterByArtist(targetArtist)
        currentView = "artistDetail"
    }

    function openAlbum(albumName, albumArtist, trackPath) {
        const targetAlbum = albumName || "Unknown Album"
        const targetArtist = albumArtist || ""
        if (currentView === "albumDetail" && selectedAlbum === targetAlbum
                && selectedAlbumArtist === targetArtist && !sideBar.searchOpen)
            return
        pushNavState()
        if (sideBar.searchOpen) sideBar.clearSearch()
        selectedAlbum = targetAlbum
        selectedAlbumArtist = targetArtist
        selectedArtist = targetArtist
        selectedAlbumPath = trackPath || ""
        playerBackend.filterByAlbum(targetAlbum, targetArtist)
        currentView = "albumDetail"
    }

    function jumpToCurrentTrack() {
        const path = playerBackend.currentPath
        if (!path) return
        if (sideBar.searchOpen) sideBar.clearSearch()

        const ctx = playerBackend.trackContextForPath(path)
        if (!ctx || !ctx.inLibrary) {
            queuePanel.opened = true
            return
        }

        if (currentView === "lyrics") {
            const prev = viewBeforeLyrics || "tracks"
            if (prev === "albumDetail" || prev === "artistDetail" || prev === "tracks") {
                currentView = prev
            } else {
                playerBackend.clearCategoryFilter()
                currentView = "tracks"
            }
        } else if (currentView === "albums" || currentView === "artists") {
            pushNavState()
            playerBackend.clearCategoryFilter()
            currentView = "tracks"
        }

        if ((currentView === "albumDetail" || currentView === "artistDetail")
                && playerBackend.getRowForPath(path) < 0) {
            pushNavState()
            playerBackend.clearCategoryFilter()
            currentView = "tracks"
        }

        Qt.callLater(() => root.locateCurrentTrackRequested())
    }

    function openAddFolderSettings() {
        settingsWindow.openTab(2)
    }

    function hasValidImage(img) {
        return img !== null && img !== undefined
            && img.status === Image.Ready && img.implicitWidth > 1
    }

    function formatDurationTotal(totalSec) {
        const sec = Math.max(0, Math.floor(totalSec || 0))
        if (sec <= 0) return ""
        const h = Math.floor(sec / 3600)
        const m = Math.floor((sec % 3600) / 60)
        if (h > 0) return h + " hr " + m + " min"
        if (m > 0) return m + " min"
        return sec + " sec"
    }

    function deepestItemAt(item, scenePosition) {
        const local = item.mapFromItem(null, scenePosition.x, scenePosition.y)
        const child = item.childAt(local.x, local.y)
        return child ? deepestItemAt(child, scenePosition) : item
    }

    function keepsSearchOpen(item) {
        while (item) {
            if (item.keepsSearchOpen === true)
                return true
            if (item.down !== undefined && item.checkable !== undefined
                    && item.clicked !== undefined)
                return true
            if (item.from !== undefined && item.to !== undefined
                    && item.visualPosition !== undefined)
                return true
            if (item.selectByMouse !== undefined)
                return true
            item = item.parent
        }
        return false
    }

    function coverSource(path) {
        return path ? "image://cover/" + encodeURIComponent(path) : ""
    }

    function colorForName(name) {
        if (!name) return Qt.hsla(0.6, 0.35, 0.5, 1.0)
        let h = 0
        for (let i = 0; i < name.length; ++i) {
            h = (h * 31 + name.charCodeAt(i)) >>> 0
        }
        const hue = (h % 360) / 360.0
        return Qt.hsla(hue, 0.45, 0.5, 1.0)
    }

    function initialsForName(name) {
        if (!name) return "?"
        const parts = name.trim().split(/\s+/)
        if (parts.length === 1) return parts[0].substring(0, 2).toUpperCase()
        return (parts[0][0] + parts[parts.length - 1][0]).toUpperCase()
    }

    SystemPalette { id: sysPalette; colorGroup: SystemPalette.Active }

    Connections {
        target: playerBackend
        function onRaiseRequested() {
            root.show()
            root.raise()
            root.requestActivate()
        }
        function onLibraryCleared() {
            root.savedTracklistScrollOffset = 0
            root.savedAlbumsScrollOffset = 0
            root.savedArtistsScrollOffset = 0
            root.navBackStack = []
            root.navForwardStack = []
            root.selectedAlbum = ""
            root.selectedAlbumArtist = ""
            root.selectedAlbumPath = ""
            root.selectedArtist = ""
            if (root.currentView === "albumDetail"
                    || root.currentView === "artistDetail"
                    || root.currentView === "lyrics") {
                root.currentView = "tracks"
            }
        }
        function onMetadataChanged() {
            if (root.currentView === "lyrics" && playerBackend.currentPath === "") {
                root.goBack()
            }
        }
    }

    Shortcut {
        sequence: "Space"
        context: Qt.WindowShortcut
        enabled: !root.textInputFocused
        onActivated: playerBackend.togglePlayback()
    }
    Shortcut {
        sequence: "Left"
        context: Qt.WindowShortcut
        enabled: !root.textInputFocused
        onActivated: playerBackend.position = Math.max(0, playerBackend.position - 5000)
    }
    Shortcut {
        sequence: "Right"
        context: Qt.WindowShortcut
        enabled: !root.textInputFocused
        onActivated: {
            if (playerBackend.duration > 0)
                playerBackend.position = Math.min(playerBackend.duration, playerBackend.position + 5000)
        }
    }
    Shortcut {
        sequence: "Up"
        context: Qt.WindowShortcut
        enabled: !root.textInputFocused
        onActivated: playerBackend.volume = Math.min(1.0, playerBackend.volume + 0.05)
    }
    Shortcut {
        sequence: "Down"
        context: Qt.WindowShortcut
        enabled: !root.textInputFocused
        onActivated: playerBackend.volume = Math.max(0.0, playerBackend.volume - 0.05)
    }
    Shortcut {
        sequence: StandardKey.Find
        context: Qt.WindowShortcut
        onActivated: sideBar.openSearch()
    }
    Shortcut {
        sequence: "Ctrl+L"
        context: Qt.WindowShortcut
        onActivated: root.jumpToCurrentTrack()
    }
    Shortcut {
        sequence: StandardKey.Back
        context: Qt.WindowShortcut
        enabled: !root.textInputFocused
        onActivated: root.goBack()
    }
    Shortcut {
        sequence: StandardKey.Forward
        context: Qt.WindowShortcut
        enabled: !root.textInputFocused
        onActivated: root.goForward()
    }
    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: !sideBar.searchOpen
                 && (root.currentView === "albumDetail"
                     || root.currentView === "artistDetail"
                     || root.currentView === "lyrics")
        onActivated: root.goBack()
    }

    TapHandler {
        id: closeSearchTap
        acceptedButtons: Qt.LeftButton
        onPressedChanged: {
            if (!pressed || !sideBar.searchOpen)
                return
            const item = root.deepestItemAt(root.contentItem, closeSearchTap.point.scenePosition)
            if (!root.keepsSearchOpen(item))
                sideBar.clearSearch()
        }
    }

    SettingsWindow {
        id: settingsWindow
        transientParent: root
        visible: false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Rectangle {
                readonly property bool keepsSearchOpen: true
                implicitWidth: 2
                color: SplitHandle.hovered || SplitHandle.pressed ? sysPalette.highlight : "transparent"
                opacity: SplitHandle.pressed ? 0.55 : (SplitHandle.hovered ? 0.35 : 0.0)
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }

            SideBar {
                id: sideBar
                SplitView.preferredWidth: root.sidebarSearchExpanded ? 180 : appSettings.sidebarWidth
                SplitView.minimumWidth: 56
                SplitView.maximumWidth: 400
                onSettingsClicked: {
                    settingsWindow.show()
                    settingsWindow.raise()
                    settingsWindow.requestActivate()
                }
                onExpandRequested: {
                    if (!root.sidebarSearchExpanded) {
                        sidebarSaveTimer.stop()
                        root.sidebarWidthBeforeSearch = sideBar.width
                        root.sidebarSearchExpanded = true
                    }
                }
                onRestoreRequested: {
                    if (root.sidebarSearchExpanded) {
                        appSettings.sidebarWidth = root.sidebarWidthBeforeSearch
                        root.sidebarSearchExpanded = false
                    }
                }

                onWidthChanged: sidebarSaveTimer.restart()
            }

            Timer {
                id: sidebarSaveTimer
                interval: 400
                onTriggered: {
                    if (!root.sidebarSearchExpanded && sideBar.width >= 50)
                        appSettings.sidebarWidth = sideBar.width
                }
            }

            Loader {
                SplitView.fillWidth: true
                source: {
                    if (root.currentView === "albums") return "Albums.qml"
                    if (root.currentView === "albumDetail") return "AlbumDetail.qml"
                     if (root.currentView === "artists") return "Artists.qml"
                     if (root.currentView === "artistDetail") return "ArtistDetail.qml"
                     if (root.currentView === "lyrics") return "Lyrics.qml"
                     return "Tracklist.qml"
                }
            }

            QueuePanel {
                id: queuePanel
                SplitView.preferredWidth: opened ? Math.min(appSettings.queuePanelWidth, Math.max(200, Math.floor(root.width * 0.4))) : 0
                SplitView.minimumWidth: opened ? 200 : 0
                SplitView.maximumWidth: Math.max(200, Math.min(600, Math.floor(root.width * 0.5)))
                opened: appSettings.queuePanelOpen

                onWidthChanged: if (opened) queueSaveTimer.restart()
                onOpenedChanged: appSettings.queuePanelOpen = opened
            }

            Timer {
                id: queueSaveTimer
                interval: 400
                onTriggered: {
                    if (queuePanel.opened && queuePanel.width >= 100) {
                        appSettings.queuePanelWidth = queuePanel.width
                    }
                }
            }
        }

        BottomBar {
            id: bottomBar
            Layout.fillWidth: true
            Layout.preferredHeight: 105
        }
    }

    Rectangle {
        id: toastBanner
        z: 90
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: bottomBar.height + 16
        width: Math.min(root.width - 40, toastLabel.implicitWidth + 32)
        height: 36
        radius: 18
        color: Qt.rgba(sysPalette.base.r, sysPalette.base.g, sysPalette.base.b, 0.96)
        border.width: 1
        border.color: Qt.rgba(sysPalette.highlight.r, sysPalette.highlight.g, sysPalette.highlight.b, 0.45)
        opacity: root.toastVisible ? 1.0 : 0.0
        visible: opacity > 0.01

        Behavior on opacity {
            NumberAnimation { duration: 160; easing.type: Easing.OutQuad }
        }

        Text {
            id: toastLabel
            anchors.centerIn: parent
            width: Math.min(implicitWidth, toastBanner.width - 32)
            elide: Text.ElideRight
            text: root.toastMessage
            color: sysPalette.text
            font.pixelSize: 13
            font.bold: true
        }
    }

    DropArea {
        id: externalDropArea
        z: 100
        anchors.fill: parent
        keys: ["text/uri-list"]

        readonly property real splitX: (queuePanel.opened && queuePanel.width >= 200)
            ? (root.width - queuePanel.width)
            : (root.width * 0.52)
        readonly property bool appendMode: drag.x >= splitX

        onEntered: (drag) => {
            if (!drag.hasUrls) {
                drag.accepted = false
            }
        }

        onDropped: (drop) => {
            if (!drop.hasUrls || drop.urls.length === 0)
                return
            const appendOnly = (drop.x >= externalDropArea.splitX)
            drop.acceptProposedAction()
            const added = playerBackend.enqueueExternalUrls(drop.urls, !appendOnly, !appendOnly)
            if (added > 0) {
                if (!appSettings.queuePanelOpen)
                    appSettings.queuePanelOpen = true
                const noun = added === 1 ? " track" : " tracks"
                root.showToast(appendOnly
                               ? ("Added " + added + noun + " to queue")
                               : ("Playing " + added + noun + " in queue"))
            } else {
                root.showToast("No supported audio files found")
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.bottomMargin: bottomBar.height
            visible: externalDropArea.containsDrag
            color: Qt.rgba(sysPalette.window.r, sysPalette.window.g, sysPalette.window.b, 0.88)

            RowLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 18

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 14
                    color: !externalDropArea.appendMode
                        ? Qt.rgba(sysPalette.highlight.r, sysPalette.highlight.g, sysPalette.highlight.b, 0.16)
                        : Qt.rgba(sysPalette.base.r, sysPalette.base.g, sysPalette.base.b, 0.65)
                    border.width: !externalDropArea.appendMode ? 2 : 1
                    border.color: !externalDropArea.appendMode
                        ? sysPalette.highlight
                        : Qt.rgba(sysPalette.windowText.r, sysPalette.windowText.g, sysPalette.windowText.b, 0.18)

                    Column {
                        anchors.centerIn: parent
                        width: Math.max(120, parent.width - 32)
                        spacing: 8

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "▶"
                            font.pixelSize: 26
                            color: !externalDropArea.appendMode ? sysPalette.highlight : sysPalette.windowText
                            opacity: !externalDropArea.appendMode ? 1.0 : 0.45
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: "Play in Queue"
                            font.pixelSize: 17
                            font.bold: true
                            color: sysPalette.text
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: "Plays immediately · queue only, not added to library"
                            font.pixelSize: 12
                            color: sysPalette.windowText
                            opacity: 0.6
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: !(queuePanel.opened && queuePanel.width >= 200)
                    Layout.preferredWidth: (queuePanel.opened && queuePanel.width >= 200)
                        ? Math.max(200, queuePanel.width - 24)
                        : -1
                    Layout.fillHeight: true
                    radius: 14
                    color: externalDropArea.appendMode
                        ? Qt.rgba(sysPalette.highlight.r, sysPalette.highlight.g, sysPalette.highlight.b, 0.16)
                        : Qt.rgba(sysPalette.base.r, sysPalette.base.g, sysPalette.base.b, 0.65)
                    border.width: externalDropArea.appendMode ? 2 : 1
                    border.color: externalDropArea.appendMode
                        ? sysPalette.highlight
                        : Qt.rgba(sysPalette.windowText.r, sysPalette.windowText.g, sysPalette.windowText.b, 0.18)

                    Column {
                        anchors.centerIn: parent
                        width: Math.max(120, parent.width - 32)
                        spacing: 8

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: "+"
                            font.pixelSize: 28
                            font.bold: true
                            color: externalDropArea.appendMode ? sysPalette.highlight : sysPalette.windowText
                            opacity: externalDropArea.appendMode ? 1.0 : 0.45
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: "Add to End of Queue"
                            font.pixelSize: 17
                            font.bold: true
                            color: sysPalette.text
                        }

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: "Appends to queue · not added to library"
                            font.pixelSize: 12
                            color: sysPalette.windowText
                            opacity: 0.6
                        }
                    }
                }
            }
        }
    }
}
