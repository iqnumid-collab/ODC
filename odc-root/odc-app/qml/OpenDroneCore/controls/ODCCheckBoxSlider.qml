import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import OpenDroneCore

AbstractButton   {
    id:         control
    checkable:  true
    padding:    0

    property bool _showBorder:      odcPalette.globalTheme === ODCPalette.Light
    property int  _sliderInset:     2
    property bool _showHighlight:   enabled && (pressed || checked)

    ODCPalette { id: odcPalette; colorGroupEnabled: control.enabled }

    contentItem: Item {
        implicitWidth:  (label.visible ? label.contentWidth + ScreenTools.defaultFontPixelWidth : 0) + indicator.width
        implicitHeight: label.contentHeight

        ODCLabel {
            id:             label
            anchors.left:   parent.left
            text:           visible ? control.text : "X"
            visible:        control.text !== ""
        }

        Rectangle {
            id:                     indicator
            anchors.right:          parent.right
            anchors.verticalCenter: parent.verticalCenter
            height:                 ScreenTools.defaultFontPixelHeight
            width:                  height * 2
            radius:                 height / 2
            color:                  checked ? odcPalette.buttonHighlight : odcPalette.button
            border.width:           _showBorder ? 1 : 0
            border.color:           odcPalette.buttonBorder

            Rectangle {
                anchors.fill:   parent
                color:          odcPalette.buttonHighlight
                opacity:        _showHighlight ? 1 : control.enabled && control.hovered ? .2 : 0
                radius:         parent.radius
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                x:                      checked ? indicator.width - width - _sliderInset : _sliderInset
                height:                 parent.height - (_sliderInset * 2)
                width:                  height
                radius:                 height / 2
                color:                  odcPalette.buttonText
            }
        }
    }
}
