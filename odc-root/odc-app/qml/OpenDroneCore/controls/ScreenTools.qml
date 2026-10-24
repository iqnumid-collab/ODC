pragma Singleton

import QtQuick
import QtQuick.Controls
import QtQuick.Window

import OpenDroneCore

Item {
    id: _screenTools

    property real defaultFontPointSize:     10
    property real platformFontPointSize:    10

    readonly property real smallFontPointRatio:      0.75
    readonly property real mediumFontPointRatio:     1.25
    readonly property real largeFontPointRatio:      1.5

    property real defaultFontPixelHeight:   10
    property real largeFontPixelHeight:     defaultFontPixelHeight * largeFontPointRatio
    property real mediumFontPixelHeight:    defaultFontPixelHeight * mediumFontPointRatio
    property real smallFontPixelHeight:     defaultFontPixelHeight * smallFontPointRatio

    property real defaultFontPixelWidth:    10
    property real largeFontPixelWidth:      defaultFontPixelWidth * largeFontPointRatio
    property real mediumFontPixelWidth:     defaultFontPixelWidth * mediumFontPointRatio
    property real smallFontPixelWidth:      defaultFontPixelWidth * smallFontPointRatio

    property real defaultFontDescent:       0

    property real defaultDialogControlSpacing: defaultFontPixelHeight / 2

    property real smallFontPointSize:       10
    property real mediumFontPointSize:      10
    property real largeFontPointSize:       10

    property real toolbarHeight:            0

    property real realPixelDensity: {
        if (ODCScreenToolsController.devicePixelDensity > 0)
            return ODCScreenToolsController.devicePixelDensity
        return Screen.pixelDensity
    }

    property real screenWidth:  ODCScreenToolsController.isFakeMobile ? 731 : Screen.width
    property real screenHeight: ODCScreenToolsController.isFakeMobile ? 411 : Screen.height

    property bool isAndroid:    ODCScreenToolsController.isAndroid
    property bool isiOS:        ODCScreenToolsController.isIOS
    property bool isMobile:     ODCScreenToolsController.isMobile
    property bool isFakeMobile: ODCScreenToolsController.isFakeMobile
    property bool isWindows:    ODCScreenToolsController.isWindows
    property bool isDebug:      ODCScreenToolsController.isDebug
    property bool isMac:        ODCScreenToolsController.isMacOS
    property bool isLinux:      ODCScreenToolsController.isLinux
    property bool isTinyScreen: (Screen.width / realPixelDensity) < 120
    property bool isShortScreen: ((Screen.height / realPixelDensity) < 120)

    readonly property real minTouchMillimeters:     5
    property real minTouchPixels:                   0

    property real implicitButtonWidth:              Math.round(defaultFontPixelWidth *  5.0)
    property real implicitButtonHeight:             Math.round(defaultFontPixelHeight * 1.6)
    property real implicitCheckBoxHeight:           Math.round(defaultFontPixelHeight * 1.0)
    property real implicitRadioButtonHeight:        implicitCheckBoxHeight
    property real implicitTextFieldWidth:           defaultFontPixelWidth * 10
    property real implicitTextFieldHeight:          implicitButtonHeight
    property real implicitComboBoxHeight:           implicitButtonHeight
    property real implicitComboBoxWidth:            implicitButtonWidth
    property real comboBoxPadding:                  defaultFontPixelWidth
    property real implicitSliderHeight:             defaultFontPixelHeight
    property real defaultBorderRadius:              defaultFontPixelWidth / 2
    property real checkBoxIndicatorSize:            2 * Math.floor(defaultFontPixelHeight / 2) + 1
    property real radioButtonIndicatorSize:         checkBoxIndicatorSize

    readonly property string normalFontFamily:      ODCScreenToolsController.normalFontFamily
    readonly property string fixedFontFamily:       ODCScreenToolsController.fixedFontFamily

    onRealPixelDensityChanged: {
        _setBasePointSize(defaultFontPointSize)
    }

    function printScreenStats() {
        console.log('ScreenTools: Screen.width: ' + Screen.width + ' Screen.height: ' + Screen.height + ' Screen.pixelDensity: ' + Screen.pixelDensity)
    }

    function mouseX() {
        return ODCScreenToolsController.mouseX()
    }

    function mouseY() {
        return ODCScreenToolsController.mouseY()
    }

    function _setBasePointSize(pointSize) {
        _textMeasure.font.pointSize = pointSize
        defaultFontPointSize    = pointSize
        defaultFontPixelHeight  = Math.round(_textMeasure.fontHeight / 2.0) * 2
        defaultFontPixelWidth   = Math.round(_textMeasure.fontWidth  / 2.0) * 2
        defaultFontDescent      = ODCScreenToolsController.defaultFontDescent(defaultFontPointSize)
        smallFontPointSize      = defaultFontPointSize  * _screenTools.smallFontPointRatio
        mediumFontPointSize     = defaultFontPointSize  * _screenTools.mediumFontPointRatio
        largeFontPointSize      = defaultFontPointSize  * _screenTools.largeFontPointRatio
        minTouchPixels          = Math.round(minTouchMillimeters * realPixelDensity)
        if (minTouchPixels / Screen.height > 0.15) {
            minTouchPixels      = defaultFontPixelHeight * 3
        }
        toolbarHeight           = defaultFontPixelHeight * 3
    }

    Text {
        id:     _defaultFont
        text:   "X"
    }

    Text {
        id:     _textMeasure
        text:   "X"
        font.family:    normalFontFamily
        property real   fontWidth:    contentWidth
        property real   fontHeight:   contentHeight
        Component.onCompleted: {
            if (ODCScreenToolsController.isMobile) {
                if (ODCScreenToolsController.isiOS) {
                    platformFontPointSize = 14
                } else if ((Screen.width / realPixelDensity) < 120) {
                    platformFontPointSize = 11
                } else {
                    platformFontPointSize = 14
                }
            } else {
                platformFontPointSize = _defaultFont.font.pointSize
            }
            _screenTools._setBasePointSize(platformFontPointSize)
        }
    }
}
