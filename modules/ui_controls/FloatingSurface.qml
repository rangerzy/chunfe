import QtQuick
import Chunfe.Ui

Rectangle {
    id: root

    radius: Theme.floatingRadius
    color: backdrop.available ? Theme.floatingNativeTint : Theme.floatingFallbackTint
    border.width: 1
    border.color: Theme.floatingBorder

    WindowBackdrop {
        id: backdrop
        objectName: "floatingBackdrop"
        anchors.fill: parent
        cornerRadius: root.radius
    }
}
