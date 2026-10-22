import QtQuick

Item {

    Rectangle {
        color: "#000"
        width: 100
        height: 20
        Text {
            text: "112224"
            color: "#fff"
        }
        Text {
            id: name
            text: qsTr("text")
            color: "#fff"
        }
    }
}
