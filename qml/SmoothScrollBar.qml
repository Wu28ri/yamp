import QtQuick
import QtQuick.Templates as T

T.ScrollBar {
    id: bar
    z: 20
    policy: T.ScrollBar.AsNeeded
    hoverEnabled: true
    readonly property bool keepsSearchOpen: true

    implicitWidth: 14
    width: 14
    leftPadding: 3
    rightPadding: 3
    topPadding: 6
    bottomPadding: 6

    property Flickable flickable: null
    property real minThumbHeight: 38
    property bool scrollActive: false
    property bool ready: false

    readonly property bool needed: (flickable ? flickable.contentHeight > 0 : true)
        && bar.size > 0 && bar.size < 0.999
    readonly property bool expanded: bar.hovered || bar.pressed
    readonly property bool activeVisual: needed && (
        expanded
        || scrollActive
        || (flickable !== null && (flickable.moving || flickable.dragging))
    )

    visible: needed
    interactive: needed
    minimumSize: needed
        ? Math.min(0.35, minThumbHeight / Math.max(1, availableHeight))
        : 0

    Component.onCompleted: Qt.callLater(() => { bar.ready = true })

    onPressedChanged: {
        if (!pressed && needed) {
            scrollActive = true
            hideTimer.restart()
        }
    }

    Timer {
        id: hideTimer
        interval: 850
        onTriggered: bar.scrollActive = false
    }

    Connections {
        target: bar.flickable
        enabled: bar.ready && bar.needed
        function onContentYChanged() {
            bar.scrollActive = true
            hideTimer.restart()
        }
    }

    background: Item {
        implicitWidth: 14

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: bar.leftPadding
            anchors.rightMargin: bar.rightPadding
            anchors.topMargin: bar.topPadding
            anchors.bottomMargin: bar.bottomPadding
            radius: width / 2
            color: sysPalette.windowText
            opacity: bar.expanded ? 0.06 : 0.0

            Behavior on opacity {
                NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
            }
        }
    }

    contentItem: Item {
        implicitWidth: 8

        Rectangle {
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: bar.expanded ? 8 : 4
            radius: width / 2
            color: sysPalette.highlight
            opacity: {
                if (!bar.activeVisual) return 0.0
                if (bar.pressed) return 1.0
                if (bar.hovered) return 0.85
                return 0.55
            }

            Behavior on width {
                NumberAnimation { duration: 140; easing.type: Easing.OutQuad }
            }
            Behavior on opacity {
                NumberAnimation {
                    duration: bar.activeVisual ? 120 : 240
                    easing.type: Easing.OutQuad
                }
            }
        }
    }
}
