import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: queuePanel
    color: sysPalette.window
    clip: true

    property bool opened: false

    Behavior on SplitView.preferredWidth {
        NumberAnimation { duration: 300; easing.type: Easing.OutQuint }
    }

    Rectangle {
        width: 1
        anchors {
            left:   parent.left
            top:    parent.top
            bottom: parent.bottom
        }
        color: sysPalette.windowText
        opacity: 0.1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16
        visible: queuePanel.width > 50

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            visible: playerBackend.currentPath !== ""

            Rectangle {
                width: 52; height: 52
                color: sysPalette.base; radius: 6; clip: true
                Image {
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    source: root.coverSource(playerBackend.currentPath)
                    sourceSize: Qt.size(52, 52)
                    asynchronous: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 18
                    text: playerBackend.currentTitle
                    color: sysPalette.text
                    font.bold: true; font.pixelSize: 14
                    renderType: Text.NativeRendering
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 16
                    text: playerBackend.currentArtist
                    color: sysPalette.windowText
                    opacity: 0.6; font.pixelSize: 12
                    renderType: Text.NativeRendering
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: sysPalette.windowText
            opacity: 0.1
            visible: playerBackend.currentPath !== ""
        }

        ListView {
            id: queueList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 0
            reuseItems: true
            model: playerBackend.queueModel

            property bool ignoreNextScroll: false

            Connections {
                target: playerBackend
                function onCurrentQueuePositionChanged() {
                    if (queueList.ignoreNextScroll) {
                        queueList.ignoreNextScroll = false
                        return
                    }
                    queueList.positionViewAtIndex(playerBackend.currentQueuePosition, ListView.Center)
                }
            }

            displaced: Transition {
                NumberAnimation { properties: "x,y"; duration: 200; easing.type: Easing.OutQuad }
            }

            delegate: Item {
                id: delegateRoot
                width: queueList.width
                height: 56
                z: mouseArea.drag.active ? 100 : 1

                required property int  index
                required property bool isCurrent
                required property string title
                required property string artist
                required property string path

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    drag.target: contentItem
                    drag.axis: Drag.YAxis
                    drag.smoothed: false

                    property bool wasDragged: false
                    property bool _moving: false

                    onPressed: wasDragged = false
                    onPositionChanged: {
                        if (drag.active) {
                            wasDragged = true
                            const mappedY = contentItem.mapToItem(queueList.contentItem, 0, contentItem.height / 2).y
                            const targetIndex = queueList.indexAt(0, mappedY)
                            if (targetIndex !== -1 && targetIndex !== delegateRoot.index && !_moving) {
                                _moving = true
                                playerBackend.queueModel.move(delegateRoot.index, targetIndex)
                                Qt.callLater(() => mouseArea._moving = false)
                            }
                        }
                    }
                    onReleased: {
                        if (drag.active) {
                            contentItem.Drag.drop()
                            contentItem.y = 0
                        }
                    }
                    onClicked: {
                        if (!wasDragged) {
                            queueList.ignoreNextScroll = true
                            playerBackend.playFromQueue(delegateRoot.index)
                        }
                    }

                    Item {
                        id: contentItem
                        width: delegateRoot.width
                        height: 56
                        Drag.active: mouseArea.drag.active

                        states: State {
                            when: mouseArea.drag.active
                            ParentChange    { target: contentItem; parent: queueList }
                            AnchorChanges   { target: contentItem; anchors.left: undefined; anchors.right: undefined }
                            PropertyChanges { target: contentItem; opacity: 0.95 }
                        }

                        transitions: Transition {
                            NumberAnimation { properties: "opacity"; duration: 150 }
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: 6
                            color: {
                                if (mouseArea.drag.active) return sysPalette.window
                                return delegateRoot.isCurrent ? sysPalette.highlight : "transparent"
                            }
                            opacity: {
                                if (mouseArea.drag.active) return 1.0
                                if (delegateRoot.isCurrent) return 0.15
                                return mouseArea.containsMouse ? 0.05 : 0.0
                            }
                            border.color: sysPalette.highlight
                            border.width: mouseArea.drag.active ? 1 : 0
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 4
                            spacing: 8

                            Text {
                                text: "⋮"
                                color: sysPalette.windowText
                                opacity: mouseArea.drag.active ? 0.6 : 0.2
                                font.pixelSize: 16
                            }

                            Rectangle {
                                width: 36; height: 36
                                color: sysPalette.base; radius: 4; clip: true
                                Image {
                                    anchors.fill: parent
                                    source: root.coverSource(delegateRoot.path)
                                    sourceSize: Qt.size(36, 36)
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Text {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 16
                                    text: delegateRoot.title
                                    color: delegateRoot.isCurrent ? sysPalette.highlight : sysPalette.text
                                    font.bold: true; font.pixelSize: 12
                                    renderType: Text.NativeRendering
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                                Text {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 14
                                    text: delegateRoot.artist
                                    color: sysPalette.windowText
                                    opacity: 0.6; font.pixelSize: 10
                                    renderType: Text.NativeRendering
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }

                            ToolButton {
                                icon.name: "list-remove"
                                opacity: mouseArea.containsMouse ? 1.0 : 0.0
                                Behavior on opacity { NumberAnimation { duration: 150 } }
                                onClicked: playerBackend.queueModel.remove(delegateRoot.index)
                            }
                        }
                    }
                }
            }
        }
    }
}
