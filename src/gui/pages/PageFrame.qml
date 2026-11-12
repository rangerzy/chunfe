import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Flickable {
    id: root
    default property alias pageContent: contentColumn.data
    property real maximumContentWidth: 940
    property real pagePadding: 24
    property bool centerContent: false
    readonly property real bodyWidth: contentColumn.width

    clip: true
    contentWidth: width
    contentHeight: contentColumn.implicitHeight + 2 * pagePadding
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        id: contentColumn
        x: (root.width - width) / 2
        y: root.centerContent ? Math.max(root.pagePadding, (root.height - implicitHeight) / 2 - 12) : root.pagePadding
        width: Math.max(0, Math.min(root.maximumContentWidth, root.width - 2 * root.pagePadding))
        spacing: 20
    }
}
