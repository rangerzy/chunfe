import QtQuick
import Chunfe.Ui

Column {
    id: root

    property string title: ""
    property string subtitle: ""

    spacing: 6

    Text {
        width: root.width
        text: root.title
        color: Theme.text
        font.pixelSize: 24
        font.weight: Font.DemiBold
        wrapMode: Text.WordWrap
    }
    Text {
        visible: root.subtitle !== ""
        width: root.width
        text: root.subtitle
        color: Theme.muted
        font.pixelSize: 14
        wrapMode: Text.WordWrap
        lineHeight: 1.35
    }
}
