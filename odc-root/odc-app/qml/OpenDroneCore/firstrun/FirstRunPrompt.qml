import QtQuick
import QtQuick.Controls

import OpenDroneCore

// Base class for all first run prompt dialogs
ODCPopupDialog {
    buttons: _dialog.ok

    property int  promptId
    property bool markAsShownOnClose: true

    onClosed: {
        if (markAsShownOnClose) {
            {}
        }
    }
}
