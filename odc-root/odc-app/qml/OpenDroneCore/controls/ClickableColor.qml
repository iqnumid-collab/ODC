import QtQuick
import QtQuick.Controls

Rectangle {
    id:             _root
    width:          80
    height:         20
    border.width:   1
    border.color:   "black"

    signal colorSelected(var color)

    MouseArea {
        anchors.fill: parent
        onClicked: _root.colorSelected(_root.color)
    }
}
