import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import OpenDroneCore

Item {

    Text {
        id:             _textMeasure
        text:           "X"
        color:          odcPalette.window
        font.family:    ScreenTools.normalFontFamily
    }

    GridLayout {
        anchors.margins: 20
        anchors.top:     parent.top
        anchors.left:    parent.left
        columns: 3
        Text {
            text:   qsTr("Qt Platform:")
            color:  odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   Qt.platform.os
            color:  odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   qsTr("Font Point Size 10")
            color:  odcPalette.text
            font.pointSize: 10
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   qsTr("Default font width:")
            color:  odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   _textMeasure.contentWidth
            color:  odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   qsTr("Font Point Size 10.5")
            color:  odcPalette.text
            font.pointSize: 10.5
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   qsTr("Default font height:")
            color:  odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   _textMeasure.contentHeight
            color:  odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   qsTr("Font Point Size 11")
            color:  odcPalette.text
            font.pointSize: 11
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   qsTr("Default font pixel size:")
            color:  odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   _textMeasure.font.pointSize
            color:  odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   qsTr("Font Point Size 11.5")
            color:  odcPalette.text
            font.pointSize: 11.5
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   qsTr("Default font point size:")
            color:  odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   _textMeasure.font.pointSize
            color:  odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   qsTr("Font Point Size 12")
            color:  odcPalette.text
            font.pointSize: 12
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   qsTr("QML Screen Desktop:")
            color:  odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   Screen.desktopAvailableWidth + " x " + Screen.desktopAvailableHeight
            color:  odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   qsTr("Font Point Size 12.5")
            color:  odcPalette.text
            font.pointSize: 12.5
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   qsTr("QML Screen Size:")
            color:  odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   Screen.width + " x " + Screen.height
            color:  odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   qsTr("Font Point Size 13")
            color:  odcPalette.text
            font.pointSize: 13
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:   qsTr("QML Pixel Density:")
            color:  odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           Screen.pixelDensity.toFixed(4)
            color:          odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           qsTr("Font Point Size 13.5")
            color:          odcPalette.text
            font.pointSize: 13.5
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           qsTr("QML Pixel Ratio:")
            color:          odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           Screen.devicePixelRatio
            color:          odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           qsTr("Font Point Size 14")
            color:          odcPalette.text
            font.pointSize: 14
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           qsTr("Default Point:")
            color:          odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           ScreenTools.defaultFontPointSize
            color:          odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           qsTr("Font Point Size 14.5")
            color:          odcPalette.text
            font.pointSize: 14.5
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           qsTr("Computed Font Height:")
            color:          odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           ScreenTools.defaultFontPixelHeight
            color:          odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           qsTr("Font Point Size 15")
            color:          odcPalette.text
            font.pointSize: 15
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           qsTr("Computed Screen Height:")
            color:          odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           (Screen.height / Screen.pixelDensity * Screen.devicePixelRatio).toFixed(0)
            color:          odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           qsTr("Font Point Size 15.5")
            color:          odcPalette.text
            font.pointSize: 15.5
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           qsTr("Computed Screen Width:")
            color:          odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           (Screen.width / Screen.pixelDensity * Screen.devicePixelRatio).toFixed(0)
            color:          odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           qsTr("Font Point Size 16")
            color:          odcPalette.text
            font.pointSize: 16
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           qsTr("Desktop Available Width:")
            color:          odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           Screen.desktopAvailableWidth
            color:          odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           qsTr("Font Point Size 16.5")
            color:          odcPalette.text
            font.pointSize: 16.5
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           qsTr("Desktop Available Height:")
            color:          odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           Screen.desktopAvailableHeight
            color:          odcPalette.text
            font.family:    ScreenTools.normalFontFamily
        }
        Text {
            text:           qsTr("Font Point Size 17")
            color:          odcPalette.text
            font.pointSize: 17
            font.family:    ScreenTools.normalFontFamily
        }
    }

    Rectangle {
        id:                 square
        width:              100
        height:             100
        color:              odcPalette.text
        anchors.right:      parent.right
        anchors.bottom:     parent.bottom
        anchors.margins:    10
        Text {
            text: "100x100"
            anchors.centerIn: parent
            color:  odcPalette.window
        }
    }

    Component.onCompleted: {
        for (var i = 10; i < 360; i = i + 60) {
            var colorValue = Qt.hsla(i/360, 0.85, 0.5, 1);
            seriesColors.push(colorValue)
            colorListModel.append({"colorValue": colorValue.toString()})
        }
    }

    property var seriesColors: []

    ListModel {
        id: colorListModel
    }

    Column {
        width:              100
        spacing:            0
        anchors.right:      square.left
        anchors.bottom:     parent.bottom
        anchors.margins:    10
        Repeater {
            model: colorListModel
            delegate: Rectangle {
                width:      100
                height:     100 / 6
                color:      colorValue
                Text {
                    text:   colorValue
                    color:  "#202020"
                    font.pointSize:     _textMeasure.font.pointSize * 0.75
                    anchors.centerIn:   parent
                }
            }
        }
    }

}
