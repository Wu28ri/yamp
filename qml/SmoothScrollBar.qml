import QtQuick
import QtQuick.Controls

ScrollBar {
    id: bar
    policy: ScrollBar.AlwaysOn

    property Item flickable: null

    background: Item { implicitWidth: 8 }
    contentItem: Rectangle {
        implicitWidth: 8
        radius: 4
        color: sysPalette.highlight
        opacity: (bar.hovered || bar.pressed || (bar.flickable && bar.flickable.moving)) ? 1.0 : 0.4
    }
}
