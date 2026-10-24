import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import OpenDroneCore

Rectangle {
    id: _root
    anchors.fill:       parent
    anchors.margins:    ScreenTools.defaultFontPixelWidth
    color:              "white"

    ODCPalette { id: odcPalette }

    property var enabledPalette:    ODCPalette { colorGroupEnabled: true }
    property var disabledPalette:   ODCPalette { colorGroupEnabled: false }

    function exportPaletteColors(pal) {
        var objToExport = {}
        for(var clrName in pal) {
            if(pal[clrName].r !== undefined) {
                objToExport[clrName] = pal[clrName].toString();
            }
        }
        return objToExport;
    }

    function fillPalette(pal, colorsObj) {
        for(var clrName in colorsObj) {
            pal[clrName] = colorsObj[clrName];
        }
    }

    function exportTheme() {
        var themeObj = {"light": {}, "dark":{}}
        var oldTheme = odcPalette.globalTheme;

        odcPalette.globalTheme = ODCPalette.Light
        odcPalette.colorGroupEnabled = true
        themeObj.light["enabled"] = exportPaletteColors(odcPalette);
        odcPalette.colorGroupEnabled = false
        themeObj.light["disabled"] = exportPaletteColors(odcPalette);
        odcPalette.globalTheme = ODCPalette.Dark
        odcPalette.colorGroupEnabled = true
        themeObj.dark["enabled"] = exportPaletteColors(odcPalette);
        odcPalette.colorGroupEnabled = false
        themeObj.dark["disabled"] = exportPaletteColors(odcPalette);

        odcPalette.globalTheme = oldTheme;
        odcPalette.colorGroupEnabled = true;

        var jsonString = JSON.stringify(themeObj, null, 4);

        themeImportExportEdit.text = jsonString
    }

    function exportThemeCPP() {
        var palToExport = ""
        for(var i = 0; i < odcPalette.colors.length; i++) {
            var cs = odcPalette.colors[i]
            var csc = cs + 'Colors'
            palToExport += 'DECLARE_QGC_COLOR(' + cs + ', \"' + odcPalette[csc][1] + '\", \"' + odcPalette[csc][0] + '\", \"' + odcPalette[csc][3] + '\", \"' + odcPalette[csc][2] + '\")\n'
        }
        themeImportExportEdit.text = palToExport
    }

    function exportThemePlugin() {
        var palToExport = ""
        for(var i = 0; i < odcPalette.colors.length; i++) {
            var cs = odcPalette.colors[i]
            var csc = cs + 'Colors'
            if(i > 0) {
                palToExport += '\nelse '
            }
            palToExport +=
            'if (colorName == QStringLiteral(\"' + cs + '\")) {\n' +
            '    colorInfo[ODCPalette::Dark][ODCPalette::ColorGroupEnabled]   = QColor(\"' + odcPalette[csc][2] + '\");\n' +
            '    colorInfo[ODCPalette::Dark][ODCPalette::ColorGroupDisabled]  = QColor(\"' + odcPalette[csc][3] + '\");\n' +
            '    colorInfo[ODCPalette::Light][ODCPalette::ColorGroupEnabled]  = QColor(\"' + odcPalette[csc][0] + '\");\n' +
            '    colorInfo[ODCPalette::Light][ODCPalette::ColorGroupDisabled] = QColor(\"' + odcPalette[csc][1] + '\");\n' +
            '}'
        }
        themeImportExportEdit.text = palToExport
    }

    function importTheme(jsonStr) {
        var jsonObj = JSON.parse(jsonStr)
        var themeObj = {"light": {}, "dark":{}}
        var oldTheme = odcPalette.globalTheme;

        odcPalette.globalTheme = ODCPalette.Light
        odcPalette.colorGroupEnabled = true
        fillPalette(odcPalette, jsonObj.light.enabled)
        odcPalette.colorGroupEnabled = false
        fillPalette(odcPalette, jsonObj.light.disabled);
        odcPalette.globalTheme = ODCPalette.Dark
        odcPalette.colorGroupEnabled = true
        fillPalette(odcPalette, jsonObj.dark.enabled);
        odcPalette.colorGroupEnabled = false
        fillPalette(odcPalette, jsonObj.dark.disabled);

        odcPalette.globalTheme = oldTheme;
        odcPalette.colorGroupEnabled = true;

        paletteImportExportPopup.close()
    }

    //-------------------------------------------------------------------------
    //-- Export/Import
    Popup {
        id:             paletteImportExportPopup
        width:          impCol.width  + (ScreenTools.defaultFontPixelWidth  * 4)
        height:         impCol.height + (ScreenTools.defaultFontPixelHeight * 2)
        modal:          true
        focus:          true
        parent:         Overlay.overlay
        closePolicy:    Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x:              Math.round((mainWindow.width  - width)  * 0.5)
        y:              Math.round((mainWindow.height - height) * 0.5)
        onVisibleChanged: {
            if(visible) {
                exportTheme()
                _jsonButton.checked = true
            }
        }
        background: Rectangle {
            anchors.fill:   parent
            color:          odcPalette.window
            radius:         ScreenTools.defaultFontPixelHeight * 0.5
            border.width:   1
            border.color:   odcPalette.text
        }
        Column {
            id:             impCol
            spacing:        ScreenTools.defaultFontPixelHeight
            anchors.centerIn: parent
            Row {
                id:         exportFormats
                spacing:    ScreenTools.defaultFontPixelWidth  * 2
                anchors.horizontalCenter: parent.horizontalCenter
                ODCRadioButton {
                    id:     _jsonButton
                    text:   "Json"
                    onClicked: exportTheme()
                }
                ODCRadioButton {
                    text: "ODC"
                    onClicked: exportThemeCPP()
                }
                ODCRadioButton {
                    text: "Custom Plugin"
                    onClicked: exportThemePlugin()
                }
            }
            Rectangle {
                width:              flick.width  + (ScreenTools.defaultFontPixelWidth  * 2)
                height:             flick.height + (ScreenTools.defaultFontPixelHeight * 2)
                color:              "white"
                anchors.margins:    10
                Flickable {
                    id:             flick
                    clip:           true
                    width:          mainWindow.width  * 0.666
                    height:         mainWindow.height * 0.666
                    contentWidth:   themeImportExportEdit.paintedWidth
                    contentHeight:  themeImportExportEdit.paintedHeight
                    anchors.centerIn: parent
                    flickableDirection: Flickable.VerticalFlick

                    function ensureVisible(r)
                    {
                       if (contentX >= r.x)
                           contentX = r.x;
                       else if (contentX+width <= r.x+r.width)
                           contentX = r.x+r.width-width;
                       if (contentY >= r.y)
                           contentY = r.y;
                       else if (contentY+height <= r.y+r.height)
                           contentY = r.y+r.height-height;
                    }

                    TextEdit {
                       id:          themeImportExportEdit
                       width:       flick.width
                       focus:       true
                       font.family: ScreenTools.fixedFontFamily
                       font.pointSize: ScreenTools.defaultFontPointSize
                       onCursorRectangleChanged: flick.ensureVisible(cursorRectangle)
                    }
                }
            }
            Row {
                spacing:    ScreenTools.defaultFontPixelWidth  * 2
                anchors.horizontalCenter: parent.horizontalCenter
                ODCButton {
                    id:         importButton
                    text:       "Import (Json Only)"
                    enabled:    themeImportExportEdit.text[0] === "{" && _jsonButton.checked
                    onClicked: {
                        importTheme(themeImportExportEdit.text);
                    }
                }
                ODCButton {
                    text:       "Close"
                    onClicked: {
                        paletteImportExportPopup.close()
                    }
                }
            }
        }
    }

    //-------------------------------------------------------------------------
    //-- Header
    Rectangle {
        id:         _header
        width:      parent.width
        height:     themeChoice.height * 2
        color:      odcPalette.window
        anchors.top: parent.top
        Row {
            id:         themeChoice
            spacing:    20
            anchors.centerIn: parent
            ODCLabel {
                text:   qsTr("Window Color")
                anchors.verticalCenter: parent.verticalCenter
            }
            ODCButton {
                text:   qsTr("Import/Export")
                anchors.verticalCenter: parent.verticalCenter
                onClicked: paletteImportExportPopup.open()
            }
            Row {
                spacing:         20
                anchors.verticalCenter: parent.verticalCenter
                ODCRadioButton {
                    text:       qsTr("Light")
                    checked:    odcPalette.globalTheme === ODCPalette.Light
                    onClicked: {
                        odcPalette.globalTheme = ODCPalette.Light
                    }
                }
                ODCRadioButton {
                    text:       qsTr("Dark")
                    checked:    odcPalette.globalTheme === ODCPalette.Dark
                    onClicked: {
                        odcPalette.globalTheme = ODCPalette.Dark
                    }
                }
            }
        }
    }
    //-------------------------------------------------------------------------
    //-- Main Contents
    ODCFlickable {
        anchors.top:            _header.bottom
        anchors.bottom:         parent.bottom
        width:                  parent.width
        contentWidth:           _rootCol.width
        contentHeight:          _rootCol.height
        clip:                   true
        Column {
            id:         _rootCol
            Row {
                spacing: 30
                // Edit theme GroupBox
                GroupBox {
                    title: "Preview and edit theme"
                Column {
                    id: editRoot
                    spacing: 5
                    property size cellSize: "90x25"

                    // Header row
                    Row {
                        Text {
                            width: editRoot.cellSize.width * 2
                            height: editRoot.cellSize.height
                            text: ""
                        }
                        Text {
                            width: editRoot.cellSize.width; height: editRoot.cellSize.height
                            color: "black"
                            horizontalAlignment: Text.AlignLeft
                            text: qsTr("Enabled")
                        }
                        Text {
                            width: editRoot.cellSize.width; height: editRoot.cellSize.height
                            color: "black"
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("Value")
                        }
                        Text {
                            width: editRoot.cellSize.width; height: editRoot.cellSize.height
                            color: "black"
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("Disabled")
                        }
                        Text {
                            width: editRoot.cellSize.width; height: editRoot.cellSize.height
                            color: "black"
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("Value")
                        }
                    }

                    // Populate the model with all color names in the global palette
                    Component.onCompleted: {
                        for(var colorNameStr in enabledPalette) {
                            if(enabledPalette[colorNameStr].r !== undefined) {
                                paletteColorList.append({ colorName: colorNameStr });
                            }
                        }
                    }

                    ListModel {
                        id: paletteColorList
                    }

                    // Reproduce all the models
                    Repeater {
                        model: paletteColorList
                        delegate: Row {
                            spacing: 5
                            Text {
                                width: editRoot.cellSize.width * 2
                                height: editRoot.cellSize.height
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                                color: "black"
                                text: colorName
                            }
                            ClickableColor {
                                id: enabledColorPicker
                                color: enabledPalette[colorName]
                                onColorSelected: enabledPalette[colorName] = color
                            }
                            TextField {
                                id: enabledTextField
                                width: editRoot.cellSize.width; height: editRoot.cellSize.height
                                inputMask: "\\#>HHHHHHhh;"
                                horizontalAlignment: Text.AlignLeft
                                text: enabledPalette[colorName]
                                onEditingFinished: enabledPalette[colorName] = text
                            }
                            ClickableColor {
                                id: disabledColorPicker
                                color: disabledPalette[colorName]
                                onColorSelected: disabledPalette[colorName] = color
                            }
                            TextField {
                                width: editRoot.cellSize.width; height: editRoot.cellSize.height
                                inputMask: enabledTextField.inputMask
                                horizontalAlignment: Text.AlignLeft
                                text: disabledPalette[colorName]
                                onEditingFinished: disabledPalette[colorName] = text
                            }
                        }
                    }
                } // Column
                } // GroupBox { title: "Preview and edit theme"

                // QGC controls preview
                GroupBox { title: "Controls preview"
                Column {
                    id: ctlPrevColumn
                    property real _colWidth: ScreenTools.defaultFontPointSize * 18
                    property real _height: _colWidth*0.15
                    property color _bkColor: odcPalette.window
                    spacing: 10
                    width: previewGrid.width
                    Grid {
                        id: previewGrid
                        columns: 3
                        spacing: 10

                        // Header row
                        Text {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            color: "black"
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("QGC name")
                        }
                        Text {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            color: "black"
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("Enabled")
                        }
                        Text {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            color: "black"
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("Disabled")
                        }

                        // ODCLabel
                        Loader {
                            sourceComponent: ctlRowHeader
                            property string text: "ODCLabel"
                        }
                        Rectangle {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            color: ctlPrevColumn._bkColor
                            ODCLabel {
                                anchors.fill: parent
                                anchors.margins: 5
                                text: qsTr("Label")
                            }
                        }
                        Rectangle {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            color: ctlPrevColumn._bkColor
                            ODCLabel {
                                anchors.fill: parent
                                anchors.margins: 5
                                text: qsTr("Label")
                                enabled: false
                            }
                        }

                        // ODCButton
                        Loader {
                            sourceComponent: ctlRowHeader
                            property string text: "ODCButton"
                        }
                        ODCButton {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            text: qsTr("Button")
                        }
                        ODCButton {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            text: qsTr("Button")
                            enabled: false
                        }

                        // ODCButton - primary
                        Loader {
                            sourceComponent: ctlRowHeader
                            property string text: "ODCButton(primary)"
                        }
                        ODCButton {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            primary: true
                            text: qsTr("Button")
                        }
                        ODCButton {
                            width:  ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            text:   qsTr("Button")
                            primary: true
                            enabled: false
                        }

                        // ToolStripHoverButton
                        Loader {
                            sourceComponent: ctlRowHeader
                            property string text: "ToolStripHoverButton"
                        }
                        ToolStripHoverButton {
                            width:  ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height * 2
                            text:   qsTr("Hover Button")
                            radius: ScreenTools.defaultFontPointSize
                            imageSource: "/qmlimages/Gears.svg"
                        }
                        ToolStripHoverButton {
                            width:  ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height * 2
                            text:   qsTr("Hover Button")
                            radius: ScreenTools.defaultFontPointSize
                            imageSource: "/qmlimages/Gears.svg"
                            enabled: false
                        }

                        // ODCButton - menu
                        Loader {
                            sourceComponent: ctlRowHeader
                            property string text: "ODCButton(menu)"
                        }
                        Menu {
                            id: buttonMenu
                            ODCMenuItem {
                                text: qsTr("Item 1")
                            }
                            ODCMenuItem {
                                text: qsTr("Item 2")
                            }
                            ODCMenuItem {
                                text: qsTr("Item 3")
                            }
                        }
                        ODCButton {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            text: qsTr("Button")
                            onClicked: buttonMenu.popup()
                        }
                        ODCButton {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            text: qsTr("Button")
                            enabled: false
                            onClicked: buttonMenu.popup()
                        }

                        // ODCRadioButton
                        Loader {
                            sourceComponent: ctlRowHeader
                            property string text: "ODCRadioButton"
                        }
                        Rectangle {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            color: ctlPrevColumn._bkColor
                            ODCRadioButton {
                                anchors.fill: parent
                                anchors.margins: 5
                                text: qsTr("Radio")
                            }
                        }
                        Rectangle {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            color: ctlPrevColumn._bkColor
                            ODCRadioButton {
                                anchors.fill: parent
                                anchors.margins: 5
                                text: qsTr("Radio")
                                enabled: false
                            }
                        }

                        // ODCCheckBox
                        Loader {
                            sourceComponent: ctlRowHeader
                            property string text: "ODCCheckBox"
                        }
                        Rectangle {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            color: ctlPrevColumn._bkColor
                            ODCCheckBox {
                                anchors.fill: parent
                                anchors.margins: 5
                                text: qsTr("Check Box")
                            }
                        }
                        Rectangle {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            color: ctlPrevColumn._bkColor
                            ODCCheckBox {
                                anchors.fill: parent
                                anchors.margins: 5
                                text: qsTr("Check Box")
                                enabled: false
                            }
                        }

                        // ODCCheckBoxSlider
                        Loader {
                            sourceComponent: ctlRowHeader
                            property string text: "ODCCheckBoxSlider"
                        }
                        Rectangle {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            color: ctlPrevColumn._bkColor
                            ODCCheckBoxSlider {
                                anchors.fill: parent
                                anchors.margins: 5
                                text: qsTr("Check Box Slider")
                            }
                        }
                        Rectangle {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            color: ctlPrevColumn._bkColor
                            ODCCheckBoxSlider {
                                anchors.fill: parent
                                anchors.margins: 5
                                text: qsTr("Check Box Slider")
                                enabled: false
                            }
                        }

                        // ODCTextField
                        Loader {
                            sourceComponent: ctlRowHeader
                            property string text: "ODCTextField"
                        }
                        ODCTextField {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            text: "ODCTextField"
                        }
                        ODCTextField {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            text: "ODCTextField"
                            enabled: false
                        }

                        // ODCComboBox
                        Loader {
                            sourceComponent: ctlRowHeader
                            property string text: "ODCComboBox"
                        }
                        ODCComboBox {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            model: [ qsTr("Item 1"), qsTr("Item 2"), qsTr("Item 3") ]
                        }
                        ODCComboBox {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._height
                            model: [ qsTr("Item 1"), qsTr("Item 2"), qsTr("Item 3") ]
                            enabled: false
                        }

                        // SubMenuButton
                        Loader {
                            sourceComponent: ctlRowHeader
                            property string text: "SubMenuButton"
                        }
                        SubMenuButton {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._colWidth/3
                            text: qsTr("SUB MENU")
                        }
                        SubMenuButton {
                            width: ctlPrevColumn._colWidth
                            height: ctlPrevColumn._colWidth/3
                            text: qsTr("SUB MENU")
                            enabled: false
                        }
                    }
                    Rectangle {
                        width:  previewGrid.width
                        height: 60
                        radius: 3
                        color:  odcPalette.alertBackground
                        border.color: odcPalette.alertBorder
                        border.width: 1
                        anchors.horizontalCenter: parent.horizontalCenter
                        Label {
                            text: "Alert Message"
                            color: odcPalette.alertText
                            anchors.centerIn: parent
                        }
                    }
                } // Column
                } // GroupBox { title: "Controls preview"
            }

            Item{
                height: 10;
                width:  1;
            }

            Row {
                spacing: 10
                anchors.horizontalCenter: parent.horizontalCenter
                Loader {
                    property color backgroundColor: odcPalette.window
                    sourceComponent: arbBox
                }
                Loader {
                    property color backgroundColor: odcPalette.windowShade
                    sourceComponent: arbBox
                }
                Loader {
                    property color backgroundColor: odcPalette.windowShadeDark
                    sourceComponent: arbBox
                }
            }

            Item{
                height: 20;
                width:  1;
            }

            // SettingsGroupLayout Test
            GroupBox {
                title: "SettingsGroupLayout Test"
                anchors.horizontalCenter: parent.horizontalCenter

                background: Rectangle {
                    color: odcPalette.window
                    border.color: odcPalette.text
                    border.width: 1
                }

                Column {
                    spacing: ScreenTools.defaultFontPixelHeight

                    // Controls for testing properties
                    Row {
                        spacing: ScreenTools.defaultFontPixelWidth * 2

                        ODCCheckBox {
                            id: showBorderCheck
                            text: "showBorder"
                            checked: true
                        }

                        ODCCheckBox {
                            id: showDividersCheck
                            text: "showDividers"
                            checked: true
                        }

                        ODCCheckBox {
                            id: showHeadingCheck
                            text: "Show Heading"
                            checked: true
                        }

                        ODCCheckBox {
                            id: showDescriptionCheck
                            text: "Show Description"
                            checked: true
                        }
                    }

                    Row {
                        spacing: ScreenTools.defaultFontPixelWidth * 2

                        ODCLabel { text: "Visibility toggles:"; font.bold: true }

                        ODCCheckBox {
                            id: item1Visible
                            text: "Item 1"
                            checked: true
                        }

                        ODCCheckBox {
                            id: item2Visible
                            text: "Item 2"
                            checked: true
                        }

                        ODCCheckBox {
                            id: item3Visible
                            text: "Item 3"
                            checked: true
                        }

                        ODCCheckBox {
                            id: item4Visible
                            text: "Item 4"
                            checked: true
                        }
                    }

                    Row {
                        spacing: ScreenTools.defaultFontPixelWidth * 2

                        ODCLabel { text: "Repeater toggles:"; font.bold: true }

                        ODCCheckBox {
                            id: repeaterShowDividers
                            text: "Dividers"
                            checked: true
                        }

                        ODCCheckBox {
                            id: repeater1Visible
                            text: "Rep 1"
                            checked: true
                        }

                        ODCCheckBox {
                            id: repeater2Visible
                            text: "Rep 2"
                            checked: true
                        }

                        ODCCheckBox {
                            id: repeater3Visible
                            text: "Rep 3"
                            checked: true
                        }

                        ODCCheckBox {
                            id: repeater4Visible
                            text: "Rep 4"
                            checked: true
                        }

                        ODCCheckBox {
                            id: repeater5Visible
                            text: "Rep 5"
                            checked: true
                        }
                    }

                    // Test SettingsGroupLayout with various content
                    SettingsGroupLayout {
                        width: ScreenTools.defaultFontPixelWidth * 60
                        heading: showHeadingCheck.checked ? "Test Settings Group" : ""
                        headingDescription: showDescriptionCheck.checked ? "This is a description of the settings group that explains what these settings are for." : ""
                        showBorder: showBorderCheck.checked
                        showDividers: showDividersCheck.checked

                        RowLayout {
                            Layout.fillWidth: true
                            visible: item1Visible.checked

                            ODCLabel {
                                text: "Setting 1:"
                                Layout.fillWidth: true
                            }
                            ODCTextField {
                                text: "Value 1"
                                Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 15
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: item2Visible.checked

                            ODCLabel {
                                text: "Setting 2:"
                                Layout.fillWidth: true
                            }
                            ODCComboBox {
                                model: ["Option 1", "Option 2", "Option 3"]
                                Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 15
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: item3Visible.checked

                            ODCCheckBox {
                                text: "Enable feature"
                                Layout.fillWidth: true
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: item4Visible.checked

                            ODCLabel {
                                text: "Setting 4:"
                                Layout.fillWidth: true
                            }
                            ODCButton {
                                text: "Configure"
                            }
                        }
                    }

                    // Nested SettingsGroupLayout test
                    ODCLabel {
                        text: "Nested SettingsGroupLayout:"
                        font.bold: true
                    }

                    SettingsGroupLayout {
                        width: ScreenTools.defaultFontPixelWidth * 60
                        heading: "Outer Group"
                        showBorder: true
                        showDividers: true

                        RowLayout {
                            Layout.fillWidth: true
                            ODCLabel { text: "Outer setting"; Layout.fillWidth: true }
                            ODCTextField { text: "Value" }
                        }

                        SettingsGroupLayout {
                            Layout.fillWidth: true
                            heading: "Inner Group"
                            headingDescription: "Nested group inside outer group"
                            showBorder: true
                            showDividers: false

                            ODCCheckBox {
                                text: "Inner checkbox 1"
                                Layout.fillWidth: true
                            }

                            ODCCheckBox {
                                text: "Inner checkbox 2"
                                Layout.fillWidth: true
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            ODCLabel { text: "Another outer setting"; Layout.fillWidth: true }
                            ODCComboBox { model: ["A", "B", "C"] }
                        }
                    }

                    // Test with Repeater
                    ODCLabel {
                        text: "SettingsGroupLayout with Repeater:"
                        font.bold: true
                    }

                    SettingsGroupLayout {
                        width: ScreenTools.defaultFontPixelWidth * 60
                        heading: "Repeater Test"
                        showBorder: true
                        showDividers: repeaterShowDividers.checked

                        Repeater {
                            model: 5
                            delegate: RowLayout {
                                Layout.fillWidth: true
                                visible: {
                                    switch(index) {
                                        case 0: return repeater1Visible.checked
                                        case 1: return repeater2Visible.checked
                                        case 2: return repeater3Visible.checked
                                        case 3: return repeater4Visible.checked
                                        case 4: return repeater5Visible.checked
                                        default: return true
                                    }
                                }
                                ODCLabel {
                                    text: "Repeated Item " + (index + 1) + ":"
                                    Layout.fillWidth: true
                                }
                                ODCTextField {
                                    text: "Value " + (index + 1)
                                    Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 15
                                }
                            }
                        }
                    }
                }
            }

        }

    }

    Component {
        id: ctlRowHeader
        Rectangle {
            width:  ctlPrevColumn._colWidth
            height: ctlPrevColumn._height
            color:  "white"
            Text {
                color:  "black"
                text:   parent.parent.text
                anchors.fill: parent
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    Component {
        id: arbBox
        Rectangle {
            width:  arbGrid.width  * 1.5
            height: arbGrid.height * 1.5
            color:  backgroundColor
            border.color: odcPalette.text
            border.width: 1
            anchors.horizontalCenter: parent.horizontalCenter
            GridLayout {
                id: arbGrid
                columns: 4
                rowSpacing: 10
                anchors.centerIn: parent
                ODCColoredImage {
                    color:                      odcPalette.colorGreen
                    width:                      ScreenTools.defaultFontPixelWidth * 2
                    height:                     width
                    sourceSize.height:          width
                    mipmap:                     true
                    fillMode:                   Image.PreserveAspectFit
                    source:                     "/qmlimages/Gears.svg"
                }
                Label { text: "colorGreen"; color: odcPalette.colorGreen; }
                ODCColoredImage {
                    color:                      odcPalette.colorOrange
                    width:                      ScreenTools.defaultFontPixelWidth * 2
                    height:                     width
                    sourceSize.height:          width
                    mipmap:                     true
                    fillMode:                   Image.PreserveAspectFit
                    source:                     "/qmlimages/Gears.svg"
                }
                Label { text: "colorOrange"; color: odcPalette.colorOrange; }
                ODCColoredImage {
                    color:                      odcPalette.colorRed
                    width:                      ScreenTools.defaultFontPixelWidth * 2
                    height:                     width
                    sourceSize.height:          width
                    mipmap:                     true
                    fillMode:                   Image.PreserveAspectFit
                    source:                     "/qmlimages/Gears.svg"
                }
                Label { text: "colorRed"; color: odcPalette.colorRed; }
                ODCColoredImage {
                    color:                      odcPalette.colorGrey
                    width:                      ScreenTools.defaultFontPixelWidth * 2
                    height:                     width
                    sourceSize.height:          width
                    mipmap:                     true
                    fillMode:                   Image.PreserveAspectFit
                    source:                     "/qmlimages/Gears.svg"
                }
                Label { text: "colorGrey"; color: odcPalette.colorGrey;  }
                ODCColoredImage {
                    color:                      odcPalette.colorBlue
                    width:                      ScreenTools.defaultFontPixelWidth * 2
                    height:                     width
                    sourceSize.height:          width
                    mipmap:                     true
                    fillMode:                   Image.PreserveAspectFit
                    source:                     "/qmlimages/Gears.svg"
                }
                Label { text: "colorBlue"; color: odcPalette.colorBlue; }
            }
        }
    }
}
