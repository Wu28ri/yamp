import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: albumDetailRoot
    anchors.fill: parent

    property var details: ({})

    function refreshDetails() {
        details = playerBackend.albumDetails(root.selectedAlbum, root.selectedAlbumArtist)
    }

    Component.onCompleted: {
        playerBackend.filterByAlbum(root.selectedAlbum, root.selectedAlbumArtist)
        refreshDetails()
    }

    Connections {
        target: root
        function onSelectedAlbumChanged()       { albumDetailRoot.refreshDetails() }
        function onSelectedAlbumArtistChanged() { albumDetailRoot.refreshDetails() }
    }

    Connections {
        target: playerBackend.trackModel
        function onModelReset() { albumDetailRoot.refreshDetails() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        DetailHeader {
            title:    root.selectedAlbum || "Unknown Album"
            subtitle: root.selectedAlbumArtist
            subtitleClickable: root.selectedAlbumArtist.length > 0
                               && root.selectedAlbumArtist !== "Unknown Artist"
            metaText: {
                const d = albumDetailRoot.details || {}
                const parts = []
                if (d.year && d.year > 0) parts.push(String(d.year))
                if (d.genre && d.genre.length > 0) parts.push(d.genre)
                const tc = d.trackCount || 0
                if (tc > 0) parts.push(tc + (tc === 1 ? " track" : " tracks"))
                const dur = root.formatDurationTotal(d.totalDuration || 0)
                if (dur.length > 0) parts.push(dur)
                return parts.join(" · ")
            }

            avatarContent: Rectangle {
                anchors.fill: parent
                radius: 12
                color: root.hasValidImage(detailCoverImage) ? "transparent" : sysPalette.mid

                Text {
                    anchors.centerIn: parent
                    visible: !root.hasValidImage(detailCoverImage)
                    text: "♪"
                    font.pixelSize: Math.round(parent.width * 0.34)
                    color: sysPalette.windowText
                    opacity: 0.3
                }

                Image {
                    id: detailCoverImage
                    anchors.fill: parent
                    visible: root.hasValidImage(detailCoverImage)
                    source: root.coverSource(root.selectedAlbumPath)
                    sourceSize: Qt.size(Math.ceil(220 * Screen.devicePixelRatio), Math.ceil(220 * Screen.devicePixelRatio))
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                }
            }

            onBackRequested: root.goBack()
            onSubtitleClicked: root.openArtist(root.selectedAlbumArtist)
            onPlayRequested: playerBackend.playAlbum(root.selectedAlbum, root.selectedAlbumArtist, playerBackend.shuffle)
            onShuffleRequested: playerBackend.playAlbum(root.selectedAlbum, root.selectedAlbumArtist, true)
        }

        Tracklist {
            Layout.fillWidth: true
            Layout.fillHeight: true
            discCount: (albumDetailRoot.details && albumDetailRoot.details.discCount) ? albumDetailRoot.details.discCount : 1
        }
    }
}
