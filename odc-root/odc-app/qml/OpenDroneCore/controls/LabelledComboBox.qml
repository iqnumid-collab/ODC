import QtQuick
import QtQuick.Layouts

import OpenDroneCore

RowLayout {
    property alias label:                   label.text
    property alias model:                   _comboBox.model
    property alias currentIndex:            _comboBox.currentIndex
    property alias currentText:             _comboBox.currentText
    property alias alternateText:           _comboBox.alternateText
    property var   comboBox:                _comboBox
    property real  comboBoxPreferredWidth:  -1

    spacing: ScreenTools.defaultFontPixelWidth

    signal activated(int index)

    ODCLabel {
        id:                  label
        Layout.fillWidth:    true
        Layout.minimumWidth: implicitWidth
    }

    ODCComboBox {
        id:                     _comboBox
        Layout.preferredWidth:  comboBoxPreferredWidth
        sizeToContents:         true
        onActivated: (index) => { parent.activated(index) }
    }
}
