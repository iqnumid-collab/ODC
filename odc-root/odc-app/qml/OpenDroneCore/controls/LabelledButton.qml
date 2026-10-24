import QtQuick
import QtQuick.Layouts

import OpenDroneCore

RowLayout {
    property alias label:                   _label.text
    property alias buttonText:              _button.text
    property real  buttonPreferredWidth:    -1

    signal clicked

    id:         _root
    spacing:    ScreenTools.defaultFontPixelWidth * 2

    ODCLabel {
        id:                  _label
        Layout.fillWidth:    true
        Layout.minimumWidth: implicitWidth
    }

    ODCButton {
        id:                     _button
        Layout.preferredWidth:  buttonPreferredWidth
        onClicked:              _root.clicked()
    }
}
