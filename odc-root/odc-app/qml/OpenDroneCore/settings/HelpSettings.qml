import QtQuick
import QtQuick.Layouts

import OpenDroneCore

Rectangle {
    objectName: "settingsPage_Help"
    color:          odcPalette.window
    anchors.fill:   parent

    readonly property real _margins: ScreenTools.defaultFontPixelHeight

    ODCPalette { id: odcPalette; colorGroupEnabled: true }

    ODCFlickable {
        anchors.margins:    _margins
        anchors.fill:       parent
        contentWidth:       grid.width
        contentHeight:      grid.height
        clip:               true

        GridLayout {
            id:         grid
            columns:    2

            ODCLabel { text: qsTr("OpenDroneCore User Guide") }
            ODCLabel {
                linkColor:          odcPalette.text
                text:               "<a href=\"https://docs.qgroundcontrol.com\">https://docs.qgroundcontrol.com</a>"
                onLinkActivated:    (link) => Qt.openUrlExternally(link)
            }

            ODCLabel { text: qsTr("PX4 Users Discussion Forum") }
            ODCLabel {
                linkColor:          odcPalette.text
                text:               "<a href=\"http://discuss.px4.io/c/qgroundcontrol\">http://discuss.px4.io/c/qgroundcontrol</a>"
                onLinkActivated:    (link) => Qt.openUrlExternally(link)
            }

            ODCLabel { text: qsTr("ArduPilot Users Discussion Forum") }
            ODCLabel {
                linkColor:          odcPalette.text
                text:               "<a href=\"https://discuss.ardupilot.org/c/ground-control-software/qgroundcontrol\">https://discuss.ardupilot.org/c/ground-control-software/qgroundcontrol</a>"
                onLinkActivated:    (link) => Qt.openUrlExternally(link)
            }

            ODCLabel { text: qsTr("OpenDroneCore Discord Channel") }
            ODCLabel {
                linkColor:          odcPalette.text
                text:               "<a href=\"https://discord.com/channels/1022170275984457759/1022185820683255908\">https://discord.com/channels/1022170275984457759/1022185820683255908</a>"
                onLinkActivated:    (link) => Qt.openUrlExternally(link)
            }
        }
    }
}
