import QtQuick

MouseArea {
    id: root
    anchors.fill: parent
    acceptedButtons: Qt.NoButton

    property Flickable target: null
    property real multiplier: 3.0

    onWheel: (wheel) => {
        if (!root.target) return
        const step = (wheel.angleDelta.y / 120) * 120 * root.multiplier
        const minY = root.target.originY
        const maxY = Math.max(minY,
            root.target.contentHeight - root.target.height + root.target.originY)
        root.target.contentY = Math.max(minY,
            Math.min(root.target.contentY - step, maxY))
    }
}
