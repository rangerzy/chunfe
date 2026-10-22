import QtQuick

Window {
    width: 480
    height: 320
    visible: true
    title: qsTr("Chunfe")
    color: "white"

    // Text {
    //     id: t
    //     // anchors.centerIn: parent
    //     text: qsTr("Hello111 Worl11")
    //     font.pixelSize: 32
    //     color: "#222222"
    // }
    Test {
        id: test
        // anchors.top: t.bottom
    }
}
