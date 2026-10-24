import QtQuick
import QtQuick.Controls

import OpenDroneCore

GroupBox {
    id: control

    ODCPalette { id: odcPalette; colorGroupEnabled: enabled }

    background: Rectangle {
        y:      control.topPadding - control.padding
        width:  parent.width
        height: parent.height - control.topPadding + control.padding
        color:  odcPalette.windowShade
    }

    label: ODCLabel {
        width:  control.availableWidth
        text:   control.title
    }
}
