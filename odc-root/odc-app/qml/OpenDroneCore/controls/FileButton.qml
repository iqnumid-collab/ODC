import QtQuick
import QtQuick.Controls

import OpenDroneCore

/// File Button controls used by ODCFileDialog control
Rectangle {
    implicitWidth:  ScreenTools.implicitButtonWidth
    implicitHeight: ScreenTools.implicitButtonHeight
    color:          highlight ? odcPalette.buttonHighlight : odcPalette.button
    border.color:   highlight ? odcPalette.buttonHighlightText : odcPalette.buttonText

    property alias  text:       label.text
    property bool   highlight:  false

    signal clicked
    signal hamburgerClicked

    property real _margins: ScreenTools.defaultFontPixelWidth / 2

    ODCPalette { id: odcPalette; colorGroupEnabled: enabled }

    ODCLabel {
        id:                     label
        anchors.margins:         _margins
        anchors.left:           parent.left
        anchors.right:          hamburger.left
        anchors.top:            parent.top
        anchors.bottom:         parent.bottom
        verticalAlignment:      Text.AlignVCenter
        horizontalAlignment:    Text.AlignHCenter
        color:                  highlight ? odcPalette.buttonHighlightText : odcPalette.buttonText
        elide:                  Text.ElideRight
    }

    ODCColoredImage {
        id:                     hamburger
        anchors.rightMargin:    _margins
        anchors.right:          parent.right
        anchors.verticalCenter: parent.verticalCenter
        width:                  _hamburgerSize
        height:                 _hamburgerSize
        sourceSize.height:      _hamburgerSize
        source:                 "qrc:/qmlimages/Hamburger.svg"
        color:                  highlight ? odcPalette.buttonHighlightText : odcPalette.buttonText

        property real _hamburgerSize: parent.height * 0.75
    }

    ODCMouseArea {
        anchors.fill:   parent
        onClicked:      parent.clicked()
    }

    ODCMouseArea {
        anchors.leftMargin: -_margins * 2
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        anchors.right:      parent.right
        anchors.left:       hamburger.left
        onClicked:          parent.hamburgerClicked()
    }
}
