import QtQuick
import QtQuick.Controls

import OpenDroneCore

Text {
    font.pointSize: ScreenTools.defaultFontPointSize
    font.family:    ScreenTools.normalFontFamily
    color:          odcPalette.text
    antialiasing:   true

    ODCPalette { id: odcPalette; colorGroupEnabled: enabled }
}
