import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import OpenDroneCore

Item {
    id: _root
    visible: false

    property string title
    property string folder
    property var    nameFilters: []
    property string fileUrl: ""
    property bool   selectExisting: true
    property bool   selectFolder: false

    signal accepted()
    signal rejected()
    signal fileImportedNotify()

    function openForLoad() {}
    function openForSave() {}
    function close() {}
}
