import QtQuick

import OpenDroneCore

/// QGC version of ListVIew control that shows horizontal/vertial scroll indicators
ListView {
    id:             root
    boundsBehavior: Flickable.StopAtBounds
    clip:           true

    property color indicatorColor: odcPalette.text

    ODCPalette { id: odcPalette; colorGroupEnabled: enabled }

    Component.onCompleted: {
        var indicatorComponent = Qt.createComponent("ODCFlickableScrollIndicator.qml")
        indicatorComponent.createObject(root, { orientation: ODCFlickableScrollIndicator.Horizontal })
        indicatorComponent = Qt.createComponent("ODCFlickableScrollIndicator.qml")
        indicatorComponent.createObject(root, { orientation: ODCFlickableScrollIndicator.Vertical })
    }
}
