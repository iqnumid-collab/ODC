import QtQuick
import QtQuick.Window

import OpenDroneCore

QtObject {
    id: _root

    property Window window

    property bool _enabled: !ScreenTools.isMobile && !ScreenTools.isFakeMobile

    function loadSavedState() {
        if (!_enabled || !window) return

        let group = "MainWindow"
        let x = ODCSettings.value(group, "x", -1)
        let y = ODCSettings.value(group, "y", -1)
        let w = ODCSettings.value(group, "width", -1)
        let h = ODCSettings.value(group, "height", -1)

        if (x >= 0 && y >= 0) {
            window.x = x
            window.y = y
        }
        if (w > 0 && h > 0) {
            window.width = w
            window.height = h
        }
    }

    function saveState() {
        if (!_enabled || !window) return

        let group = "MainWindow"
        ODCSettings.setValue(group, "x", window.x)
        ODCSettings.setValue(group, "y", window.y)
        ODCSettings.setValue(group, "width", window.width)
        ODCSettings.setValue(group, "height", window.height)
    }

    Component.onCompleted: {
        loadSavedState()
        if (_root.window) {
            _root.window.closing.connect(saveState)
        }
    }
}
