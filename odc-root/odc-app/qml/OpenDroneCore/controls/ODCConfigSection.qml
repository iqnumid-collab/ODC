import QtQuick
import QtQuick.Layouts

import OpenDroneCore

ColumnLayout {
    id: control
    spacing: ScreenTools.defaultFontPixelHeight / 3

    default property alias contentItem: _controlsColumn.data

    property string heading
    property string iconSource

    property real _margins: ScreenTools.defaultFontPixelHeight / 2

    ODCPalette { id: odcPalette; colorGroupEnabled: true }

    ODCLabel {
        text: control.heading
        font.pointSize: ScreenTools.largeFontPointSize
        visible: control.heading !== ""
    }

    Rectangle {
        implicitWidth: _contentRow.implicitWidth + _margins * 2
        implicitHeight: _contentRow.implicitHeight + _margins * 2
        Layout.fillWidth: true
        color: odcPalette.windowShade
        radius: ScreenTools.defaultFontPixelHeight / 4

        RowLayout {
            id: _contentRow
            y: _margins
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: _margins
            anchors.rightMargin: _margins
            spacing: ScreenTools.defaultFontPixelWidth * 2

            ODCColoredImage {
                Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 3
                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 1.5
                source: control.iconSource
                color: odcPalette.text
                fillMode: Image.PreserveAspectFit
                visible: control.iconSource !== ""
            }

            ColumnLayout {
                id: _controlsColumn
                Layout.fillWidth: true
                spacing: ScreenTools.defaultFontPixelHeight / 2
            }
        }
    }
}
