import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: artistDetailRoot
    anchors.fill: parent

    property var details: ({})
    property var albumsList: []

    function refreshArtistMeta() {
        details = playerBackend.artistDetails(root.selectedArtist)
        albumsList = playerBackend.artistAlbums(root.selectedArtist)
    }

    Component.onCompleted: {
        playerBackend.filterByArtist(root.selectedArtist)
        refreshArtistMeta()
    }

    Connections {
        target: root
        function onSelectedArtistChanged() {
            detailArtistImage.fetchedUrl = ""
            artistDetailRoot.refreshArtistMeta()
        }
    }

    Connections {
        target: playerBackend.trackModel
        function onModelReset() { artistDetailRoot.refreshArtistMeta() }
    }

    Menu {
        id: artistAlbumMenu
        property string albumName: ""
        property string artistName: ""

        MenuItem {
            text: "Play"
            onTriggered: playerBackend.playAlbum(artistAlbumMenu.albumName, artistAlbumMenu.artistName, playerBackend.shuffle)
        }
        MenuItem {
            text: "Shuffle"
            onTriggered: playerBackend.playAlbum(artistAlbumMenu.albumName, artistAlbumMenu.artistName, true)
        }
        MenuItem {
            text: "Play next"
            onTriggered: playerBackend.addAlbumPlayNext(artistAlbumMenu.albumName, artistAlbumMenu.artistName)
        }
        MenuItem {
            text: "Add to queue"
            onTriggered: playerBackend.addAlbumToQueue(artistAlbumMenu.albumName, artistAlbumMenu.artistName)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        DetailHeader {
            id: detailHeader
            title: root.selectedArtist || "Unknown Artist"
            metaText: {
                const d = artistDetailRoot.details || {}
                const parts = []
                const ac = d.albumCount || 0
                const tc = d.trackCount || 0
                if (ac > 0) parts.push(ac + (ac === 1 ? " album" : " albums"))
                if (tc > 0) parts.push(tc + (tc === 1 ? " track" : " tracks"))
                const dur = root.formatDurationTotal(d.totalDuration || 0)
                if (dur.length > 0) parts.push(dur)
                return parts.join(" · ")
            }

            avatarContent: Rectangle {
                anchors.fill: parent
                radius: 12
                color: root.hasValidImage(detailArtistImage)
                       ? "transparent"
                       : root.colorForName(root.selectedArtist)

                Text {
                    anchors.centerIn: parent
                    visible: !root.hasValidImage(detailArtistImage)
                    text: root.initialsForName(root.selectedArtist)
                    color: "white"
                    font.pixelSize: Math.round(parent.width * 0.36)
                    font.bold: true
                    renderType: Text.QtRendering
                }

                Image {
                    id: detailArtistImage
                    property string fetchedUrl: ""
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    sourceSize: Qt.size(Math.min(384, Math.ceil(220 * Screen.devicePixelRatio)),
                                        Math.min(384, Math.ceil(220 * Screen.devicePixelRatio)))
                    asynchronous: true
                    visible: root.hasValidImage(detailArtistImage)
                    source: artistImages.enabled
                            ? (fetchedUrl || artistImages.artistImageUrl(root.selectedArtist))
                            : ""

                    Connections {
                        target: artistImages
                        enabled: artistImages.enabled && !root.hasValidImage(detailArtistImage)
                        function onArtistImageReady(artistName, imageUrl) {
                            if (artistName === root.selectedArtist)
                                detailArtistImage.fetchedUrl = imageUrl
                        }
                    }
                }
            }

            onBackRequested: root.goBack()
            onPlayRequested: playerBackend.playArtist(root.selectedArtist, playerBackend.shuffle)
            onShuffleRequested: playerBackend.playArtist(root.selectedArtist, true)
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: detailHeader.compact ? 114 : 138
            visible: artistDetailRoot.albumsList.length > 0 && artistDetailRoot.height >= 500

            readonly property real cardCover: detailHeader.compact ? 68 : 88

            ListView {
                id: artistAlbumsStrip
                anchors.fill: parent
                anchors.bottomMargin: 8
                leftMargin: detailHeader.compact ? 18 : 30
                rightMargin: detailHeader.compact ? 18 : 30
                orientation: ListView.Horizontal
                spacing: 14
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: artistDetailRoot.albumsList

                WheelHandler {
                    onWheel: event => {
                        const delta = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
                        if (delta === 0) return
                        const maxX = Math.max(artistAlbumsStrip.originX,
                                              artistAlbumsStrip.contentWidth - artistAlbumsStrip.width + artistAlbumsStrip.originX + artistAlbumsStrip.leftMargin + artistAlbumsStrip.rightMargin)
                        artistAlbumsStrip.contentX = Math.max(artistAlbumsStrip.originX - artistAlbumsStrip.leftMargin,
                                                              Math.min(maxX, artistAlbumsStrip.contentX - delta))
                        event.accepted = true
                    }
                }

                delegate: Item {
                    id: albumChip
                    readonly property bool keepsSearchOpen: true
                    width: detailHeader.compact ? 84 : 104
                    height: artistAlbumsStrip.height

                    required property var modelData

                    Rectangle {
                        id: chipCoverBox
                        anchors.top: parent.top
                        anchors.topMargin: 2
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: detailHeader.compact ? 68 : 88
                        height: width
                        radius: 8
                        color: root.hasValidImage(chipCover) ? "transparent" : sysPalette.mid
                        scale: chipMouse.containsMouse ? 1.03 : 1.0
                        Behavior on scale { NumberAnimation { duration: 140 } }

                        Text {
                            anchors.centerIn: parent
                            visible: !root.hasValidImage(chipCover)
                            text: "♪"
                            font.pixelSize: Math.round(parent.width * 0.34)
                            color: sysPalette.windowText
                            opacity: 0.3
                        }

                        Image {
                            id: chipCover
                            anchors.fill: parent
                            visible: root.hasValidImage(chipCover)
                            source: root.coverSource(albumChip.modelData.path || "")
                            sourceSize: Qt.size(Math.ceil(96 * Screen.devicePixelRatio),
                                                Math.ceil(96 * Screen.devicePixelRatio))
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            opacity: chipMouse.pressed ? 0.82 : 1.0
                        }
                    }

                    Text {
                        id: chipTitle
                        anchors.top: chipCoverBox.bottom
                        anchors.topMargin: 5
                        anchors.left: parent.left
                        anchors.right: parent.right
                        horizontalAlignment: Text.AlignHCenter
                        text: albumChip.modelData.album || "Unknown Album"
                        color: sysPalette.text
                        font.pixelSize: 12
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Text {
                        anchors.top: chipTitle.bottom
                        anchors.topMargin: 1
                        anchors.left: parent.left
                        anchors.right: parent.right
                        horizontalAlignment: Text.AlignHCenter
                        text: (albumChip.modelData.year && albumChip.modelData.year > 0)
                              ? String(albumChip.modelData.year)
                              : ((albumChip.modelData.trackCount || 0)
                                 + ((albumChip.modelData.trackCount || 0) === 1 ? " track" : " tracks"))
                        color: sysPalette.windowText
                        opacity: 0.55
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        id: chipMouse
                        readonly property bool keepsSearchOpen: true
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: mouse => {
                            if (mouse.button === Qt.RightButton) {
                                artistAlbumMenu.albumName = albumChip.modelData.album || "Unknown Album"
                                artistAlbumMenu.artistName = albumChip.modelData.artist || ""
                                artistAlbumMenu.popup()
                            } else {
                                root.openAlbum(albumChip.modelData.album || "Unknown Album",
                                               albumChip.modelData.artist || "",
                                               albumChip.modelData.path || "")
                            }
                        }
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: sysPalette.windowText
                opacity: 0.08
            }
        }

        Tracklist {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
