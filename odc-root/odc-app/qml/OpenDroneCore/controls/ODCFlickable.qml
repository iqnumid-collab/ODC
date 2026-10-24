import QtQuick
import QtQuick.Controls

import OpenDroneCore

/// QGC version of Flickable control that shows horizontal/vertial scroll indicators
Flickable {
    id:                     root
    boundsBehavior:         Flickable.StopAtBounds
    clip:                   true
    maximumFlickVelocity:   (ScreenTools.realPixelDensity * 25.4) * 8   // About two inches per second

    property color indicatorColor: odcPalette.text

    Component.onCompleted: {
        var indicatorComponent = Qt.createComponent("ODCFlickableScrollIndicator.qml")
        indicatorComponent.createObject(root, { orientation: ODCFlickableScrollIndicator.Horizontal })
        indicatorComponent = Qt.createComponent("ODCFlickableScrollIndicator.qml")
        indicatorComponent.createObject(root, { orientation: ODCFlickableScrollIndicator.Vertical })
    }
}
