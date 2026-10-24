import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import OpenDroneCore

// Provides the standard dialog mechanism for QGC. Works 99% like Qml _dialog.
//
// Example usage:
//      ODCPopupDialogFactory {
//          id: myDialogFactory
//          dialogComponent: myDialogComponent
//      }
//
//      Component {
//          id: myDialogComponent
//
//          ODCPopupDialog {
//              ...
//          }
//      }
//
//      onFoo: myDialogFactory.open()
//      onBar: myDialogFactory.open({ title: "My Title", myProp: someValue })
//
// Notes:
//  * Use ODCPopupDialogFactory to create and open dialogs. The factory handles correct parenting and cleanup of the dialog instances.
//  * The dialog automatically reparents itself to Overlay.overlay on creation, while tracking the original parent's lifetime to prevent orphaned dialogs.
// Differences from standard Qml Dialog:
//  * The ODCPopupDialog object will automatically be destroyed when it closed. You can override this
//      behaviour by setting destroyOnClose to false if it was not created dynamically.
//  * Dialog will automatically close after accepted/rejected signal processing. You can prevent this by setting
//      preventClose = true prior to returning from your signal handlers.
Popup {
    id:                 root

    QtObject {
        id: _dialog
        readonly property int ok: 0x00000400
        readonly property int openFile: 0x00002000
        readonly property int save: 0x00000800
        readonly property int cancel: 0x00400000
        readonly property int apply: 0x02000000
        readonly property int saveAll: 0x00001000
        readonly property int yes: 0x00004000
        readonly property int no: 0x00010000
        readonly property int close: 0x00200000
        readonly property int discard: 0x00800000
        readonly property int help: 0x01000000
    }

    width:  mainWindow.width
    height: mainWindow.height
    modal:              true
    focus:              true
    margins:            0

    default property alias dialogContent: dialogContentParent.data

    property string title
    property var    buttons:                _dialog.ok
    property alias  acceptButtonEnabled:    acceptButton.enabled
    property alias  rejectButtonEnabled:    rejectButton.enabled
    property var    dialogProperties
    property bool   destroyOnClose:         true
    property bool   preventClose:           false
    property bool   bypassNavigationCheck:  false

    property real maxContentAvailableWidth:    mainWindow.width - _contentMargin * 6
    property real maxContentAvailableHeight:   mainWindow.height - titleRowLayout.height - _contentMargin * 7

    readonly property real headerMinWidth: titleLabel.implicitWidth + rejectButton.width + acceptButton.width + titleRowLayout.spacing * 2

    signal accepted
    signal rejected

    property var    odcPalette:            ODCApp.theme
    property real   _frameSize:         ScreenTools.defaultFontPixelWidth
    property real   _contentMargin:     ScreenTools.defaultFontPixelHeight / 2
    property bool   _acceptAllowed:     acceptButton.visible
    property bool   _rejectAllowed:     rejectButton.visible
    property int    _previousValidationErrorCount: 0

    background: ODCMouseArea {
        width:  mainWindow.width
        height: mainWindow.height

        onClicked: {
            if (closePolicy & Popup.CloseOnPressOutside) {
                _reject()
            }
        }
    }

    // We use this to track when the original parent of the dialog is destroyed. This allows us to automatically close the dialog when that happens which prevents
    // orphaned dialogs which cause crashes.
    Connections {
        id: originalParentConnections
        ignoreUnknownSignals: true // Prevents warning from initial connection when parent is null
        onDestroyed: root.close()
    }

    Component.onCompleted: {
        originalParentConnections.target = parent
        parent = Overlay.overlay
    }

    onAboutToShow: {
        _previousValidationErrorCount = globals.validationErrorCount
        setupDialogButtons(buttons)
    }

    onClosed: {
        globals.validationErrorCount = _previousValidationErrorCount
        Qt.inputMethod.hide()
        if (destroyOnClose) {
            root.destroy()
        }
    }

    function _accept() {
        if (_acceptAllowed && (bypassNavigationCheck || mainWindow.allowViewSwitch(_previousValidationErrorCount))) {
            accepted()
            if (preventClose) {
                preventClose = false
            } else {
                close()
            }
        }
    }

    function _reject() {
        // Dialogs with cancel button are allowed to close with validation errors
        if (_rejectAllowed && ((buttons & _dialog.cancel) || bypassNavigationCheck || mainWindow.allowViewSwitch(_previousValidationErrorCount))) {
            rejected()
            if (preventClose) {
                preventClose = false
            } else {
                close()
            }
        }
    }

    ODCPalette { id: odcPalette; colorGroupEnabled: parent.enabled }

    function setupDialogButtons(buttons) {
        acceptButton.visible = false
        rejectButton.visible = false
        // Accept role buttons
        if (buttons & _dialog.ok) {
            acceptButton.text = qsTr("Ok")
            acceptButton.visible = true
        } else if (buttons & _dialog.openFile) {
            acceptButton.text = qsTr("Open")
            acceptButton.visible = true
        } else if (buttons & _dialog.save) {
            acceptButton.text = qsTr("Save")
            acceptButton.visible = true
        } else if (buttons & _dialog.apply) {
            acceptButton.text = qsTr("Apply")
            acceptButton.visible = true
        } else if (buttons & _dialog.openFile) {
            acceptButton.text = qsTr("Open")
            acceptButton.visible = true
        } else if (buttons & _dialog.saveAll) {
            acceptButton.text = qsTr("Save All")
            acceptButton.visible = true
        } else if (buttons & _dialog.yes) {
            acceptButton.text = qsTr("Yes")
            acceptButton.visible = true
        } else if (buttons & 0x00008000) {
            acceptButton.text = qsTr("Yes to All")
            acceptButton.visible = true
        } else if (buttons & 0x00080000) {
            acceptButton.text = qsTr("Retry")
            acceptButton.visible = true
        } else if (buttons & 0x00020000) {
            acceptButton.text = qsTr("Reset")
            acceptButton.visible = true
        } else if (buttons & 0x08000000) {
            acceptButton.text = qsTr("Restore to Defaults")
            acceptButton.visible = true
        } else if (buttons & 0x00100000) {
            acceptButton.text = qsTr("Ignore")
            acceptButton.visible = true
        }

        // Reject role buttons
        if (buttons & _dialog.cancel) {
            rejectButton.text = qsTr("Cancel")
            rejectButton.visible = true
        } else if (buttons & _dialog.close) {
            rejectButton.text = qsTr("Close")
            rejectButton.visible = true
        } else if (buttons & _dialog.no) {
            rejectButton.text = qsTr("No")
            rejectButton.visible = true
        } else if (buttons & 0x00040000) {
            rejectButton.text = qsTr("No to All")
            rejectButton.visible = true
        } else if (buttons & 0x00000200) {
            rejectButton.text = qsTr("Abort")
            rejectButton.visible = true
        }

        closePolicy = Popup.NoAutoClose
        if (buttons & _dialog.cancel) {
            closePolicy |= Popup.CloseOnEscape
        }
    }

    function disableAcceptButton() {
        acceptButton.enabled = false
    }

    Rectangle {
        x:              mainLayout.x - _contentMargin
        y:              mainLayout.y - _contentMargin
        width:          mainLayout.width + _contentMargin * 2
        height:         mainLayout.height + _contentMargin * 2
        color:          odcPalette.windowShade
        radius:         root.padding / 2
        border.width:   1
        border.color:   odcPalette.windowShadeLight
    }

    ColumnLayout {
        id:                 mainLayout
        anchors.centerIn:   parent
        x:                  _contentMargin
        y:                  _contentMargin
        spacing:            _contentMargin

        RowLayout {
            id:                     titleRowLayout
            Layout.fillWidth:       true
            spacing:                _contentMargin

            ODCLabel {
                id:                 titleLabel
                objectName:         "popupDialog_title"
                Layout.fillWidth:   true
                text:               root.title
                font.pointSize:     ScreenTools.mediumFontPointSize
                verticalAlignment:	Text.AlignVCenter
            }

            ODCButton {
                id:                     rejectButton
                objectName:             "popupDialog_rejectButton"
                onClicked:              _reject()
                Layout.minimumWidth:    height * 1.5
            }

            ODCButton {
                id:                     acceptButton
                objectName:             "popupDialog_acceptButton"
                primary:                true
                onClicked:              _accept()
                Layout.minimumWidth:    height * 1.5
            }
        }

        Rectangle {
            Layout.fillWidth:       true
            Layout.preferredWidth:  Math.min(maxAvailableWidth, totalContentWidth)
            Layout.preferredHeight: Math.min(maxAvailableHeight, totalContentHeight)
            color:                  odcPalette.window

            property real totalContentWidth:    dialogContentParent.childrenRect.width + _contentMargin * 2
            property real totalContentHeight:   dialogContentParent.childrenRect.height + _contentMargin * 2
            property real maxAvailableWidth:    mainWindow.width - _contentMargin * 4
            property real maxAvailableHeight:   mainWindow.height - titleRowLayout.height - _contentMargin * 5

            ODCFlickable {
                anchors.margins:    _contentMargin
                anchors.fill:       parent
                contentWidth:       dialogContentParent.childrenRect.width
                contentHeight:      dialogContentParent.childrenRect.height

                Item {
                    id:     dialogContentParent
                    focus:  true

                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_Escape && _rejectAllowed) {
                            _reject()
                            event.accepted = true
                        }
                    }
                }
            }
        }
    }
}
