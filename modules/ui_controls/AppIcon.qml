import QtQuick
import "IconParkData.js" as IconPark

Item {
    id: root

    property string name: ""
    property color color: "#18181b"
    property real size: 20

    implicitWidth: size
    implicitHeight: size

    Image {
        anchors.fill: parent
        source: IconPark.source(root.name, root.color.r, root.color.g, root.color.b)
        sourceSize.width: Math.max(1, Math.ceil(root.width * root.Screen.devicePixelRatio))
        sourceSize.height: Math.max(1, Math.ceil(root.height * root.Screen.devicePixelRatio))
        fillMode: Image.PreserveAspectFit
        opacity: root.color.a
        // SVG is rasterized at the actual display density; no shader tinting.
        smooth: false
    }
}
