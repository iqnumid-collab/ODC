import QtQuick
import QtQuick.Layouts

import OpenDroneCore

RowLayout {
    property alias label:                   _labelLabel.text
    property alias labelText:              _label.text
    property real  labelPreferredWidth:    -1

    spacing: ScreenTools.defaultFontPixelWidth * 2

    ODCLabel {
        id:                  _labelLabel
        Layout.fillWidth:    true
        Layout.minimumWidth: implicitWidth
    }

    ODCLabel {
        id:                     _label
        Layout.preferredWidth:  labelPreferredWidth
    }
}
