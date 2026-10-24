import QtQuick
import QtQuick.Layouts

import OpenDroneCore

RowLayout {
    property alias label:                   label.text
    property alias from:                    slider.from
    property alias to:                      slider.to
    property real  sliderPreferredWidth:    -1

    spacing: ScreenTools.defaultFontPixelWidth * 2

    ODCLabel {
        id:                  label
        Layout.fillWidth:    true
        Layout.minimumWidth: implicitWidth
    }

    ODCSlider {
        id:                     slider
        Layout.preferredWidth:  sliderPreferredWidth
    }
}
