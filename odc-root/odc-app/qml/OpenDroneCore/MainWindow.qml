import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import OpenDroneCore

ApplicationWindow {
    id: mainWindow
    visible: true
    flags: Qt.Window | (ScreenTools.isAndroid
           ? Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint : 0)

    MainWindowSavedState {
        window: mainWindow
    }

    readonly property real _topBottomMargins: ScreenTools.defaultFontPixelHeight * 0.5

    ODCPalette { id: odcPalette; colorGroupEnabled: true }

    QtObject {
        id: globals
        readonly property real defaultTextHeight: ScreenTools.defaultFontPixelHeight
        readonly property real defaultTextWidth: ScreenTools.defaultFontPixelWidth
        property int validationErrorCount: 0
    }

    // Event bus listener
    Connections {
        target: ODCEventBus
        function onEventPublished(event, data) {
            switch(event) {
                case "settingsChanged":
                    break
                case "alert":
                    if (data && data.title) {
                        showNotification(data.title, data.message || "")
                    }
                    break
                case "messageDialog":
                    if (data && data.title) {
                        showMessageDialog(data.title, data.message || "")
                    }
                    break
            }
        }
    }

    Connections {
        target: ODCApp
        function onMessageDialogRequested(title, message) {
            showMessageDialog(title, message)
        }
    }

    Connections {
        target: ODCNotificationHub
        function onAlertRaised(title, message, level) {
            showNotification(title, message)
        }
    }

    function allowViewSwitch() {
        return globals.validationErrorCount === 0
    }

    function showToolDrawer(title, source) {
        toolDrawer.toolTitle = title
        toolDrawer.toolSource = source
        toolDrawer.visible = true
    }

    function showSettingsTool() {
        showToolDrawer(qsTr("Application Settings"),
            "qrc:/qml/OpenDroneCore/controls/ODCAppSettings.qml")
    }

    function showNotification(title, message) {
        notificationToast.title = title
        notificationToast.text = message
        notificationToast.open()
    }

    function showMessageDialog(title, message) {
        let dialog = simpleMessageDialogComponent.createObject(mainWindow, {
            title: title, text: message,
            buttons: Dialog.Ok
        })
        dialog.open()
    }

    function finishCloseProcess() {
        _forceClose = true
        mainWindow.close()
    }

    property bool _forceClose: false
    onClosing: (close) => {
        if (!_forceClose) {
            close.accepted = true
            finishCloseProcess()
        }
    }

    Component {
        id: simpleMessageDialogComponent
        ODCSimpleMessageDialog {}
    }

    background: Rectangle {
        anchors.fill: parent
        color: odcPalette.window
    }

    // Main content area
    Rectangle {
        id: mainContent
        anchors.fill: parent
        color: odcPalette.window

        ColumnLayout {
            anchors.centerIn: parent
            spacing: ScreenTools.defaultFontPixelHeight

            ODCLabel {
                Layout.alignment: Qt.AlignHCenter
                text: "OpenDroneCore"
                font.pointSize: ScreenTools.largeFontPointSize * 2
                color: odcPalette.text
            }

            ODCLabel {
                Layout.alignment: Qt.AlignHCenter
                text: "Qt 6.3 Ground Control Application Framework"
                font.pointSize: ScreenTools.defaultFontPointSize
                color: odcPalette.colorGrey
            }

            Item { Layout.preferredHeight: ScreenTools.defaultFontPixelHeight }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: ScreenTools.defaultFontPixelWidth

                ODCButton {
                    text: qsTr("Settings")
                    onClicked: mainWindow.showSettingsTool()
                }

                ODCButton {
                    text: qsTr("Toggle Theme")
                    onClicked: ODCEventBus.sendCommand("toggleTheme", {})
                }

                ODCButton {
                    text: qsTr("Debug Window")
                    onClicked: {
                        mainWindow.showToolDrawer(qsTr("Debug"),
                            "qrc:/qml/OpenDroneCore/settings/DebugWindow.qml")
                    }
                }
            }
        }
    }

    // Tool Drawer Panel
    Rectangle {
        id: toolDrawer
        anchors.fill: parent
        visible: false
        color: odcPalette.window

        property string toolTitle: ""
        property string toolSource: ""

        onVisibleChanged: {
            if (!toolDrawer.visible) {
                toolDrawerLoader.source = ""
            }
        }

        DeadMouseArea { anchors.fill: parent }

        Rectangle {
            id: toolDrawerToolbar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: ScreenTools.toolbarHeight
            color: odcPalette.toolbarBackground

            RowLayout {
                anchors.leftMargin: ScreenTools.defaultFontPixelWidth
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                spacing: ScreenTools.defaultFontPixelWidth

                ODCButton {
                    text: "< Back"
                    onClicked: toolDrawer.visible = false
                }

                ODCLabel {
                    text: toolDrawer.toolTitle
                    font.pointSize: ScreenTools.largeFontPointSize
                    color: odcPalette.toolbarText
                }
            }
        }

        Loader {
            id: toolDrawerLoader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: toolDrawerToolbar.bottom
            anchors.bottom: parent.bottom
        }
    }

    // Toast notification
    ToolTip {
        id: notificationToast
        x: (mainWindow.width - width) / 2
        y: mainWindow.height - height - ScreenTools.defaultFontPixelHeight * 3
        timeout: 3000
        closePolicy: Popup.NoAutoClose

        background: Rectangle {
            color: odcPalette.alertBackground
            radius: ScreenTools.defaultFontPixelWidth / 2
            border.color: odcPalette.alertBorder
            border.width: 1
        }

        contentItem: ODCLabel {
            text: notificationToast.text
            color: odcPalette.alertText
        }
    }
}
