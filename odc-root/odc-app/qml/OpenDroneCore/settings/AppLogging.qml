import OpenDroneCore
import OpenDroneCore
import QtQuick
import QtQuick.Layouts

Item {
    id: root
    objectName: "settingsPage_AppLogViewer"

    readonly property real _indicatorWidth: ScreenTools.defaultFontPixelWidth * 0.4
    readonly property real _margin: ScreenTools.defaultFontPixelWidth
    readonly property real _cellLeftMargin: _margin * 0.3
    readonly property real _cellRightMargin: _cellLeftMargin
    readonly property real _rowHeight: ScreenTools.defaultFontPixelHeight * 1.6

    function _levelColor(lvl) {
        switch (lvl) {
        case LogEntry.Debug:    return odcPalette.colorGrey
        case LogEntry.Info:     return odcPalette.text
        case LogEntry.Warning:  return odcPalette.colorOrange
        case LogEntry.Critical: return odcPalette.colorRed
        case LogEntry.Fatal:    return odcPalette.colorRed
        default:                return odcPalette.text
        }
    }

    ODCPalette {
        id: odcPalette

        colorGroupEnabled: enabled
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Column header ────────────────────────────────────────
        HorizontalHeaderView {
            id: headerView

            Layout.fillWidth: true
            clip: true
            syncView: tableView

            delegate: Rectangle {
                required property var display

                color: odcPalette.windowShade
                implicitHeight: headerLabel.contentHeight + ScreenTools.defaultFontPixelHeight * 0.4
                implicitWidth: headerLabel.contentWidth + ScreenTools.defaultFontPixelWidth * 2

                ODCLabel {
                    id: headerLabel

                    anchors.left: parent.left
                    anchors.leftMargin: _margin * 0.5
                    anchors.verticalCenter: parent.verticalCenter
                    font.bold: true
                    font.family: ScreenTools.fixedFontFamily
                    font.pointSize: ScreenTools.defaultFontPointSize
                    text: parent.display ?? ""
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    color: odcPalette.groupBorder
                    height: 1
                    width: parent.width
                }

                Rectangle {
                    anchors.right: parent.right
                    color: odcPalette.groupBorder
                    height: parent.height
                    width: 1
                }
            }
        }

        // ── Log table ────────────────────────────────────────────
        TableView {
            id: tableView
            Layout.fillHeight: true
            Layout.fillWidth: true
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            columnSpacing: 0
            rowSpacing: 0
            model: LogManager.model
            resizableColumns: true
            reuseItems: true

            readonly property real minMessageColumnWidthInChars: 40

            columnWidthProvider: function (col) {
                const explicit_ = tableView.explicitColumnWidth(col)
                if (explicit_ > 0) {
                    return explicit_;
                }
                const charWidth = ScreenTools.defaultFontPixelWidth
                const compact = _cachedWidth < charWidth * 60
                const catW = compact ? charWidth * 14 : charWidth * 22
                const tsW  = compact ? charWidth * 9  : charWidth * 12
                const srcW = compact ? charWidth * 16 : charWidth * 22
                switch (col) {
                case LogEntry.MessageColumn:
                    return Math.max(charWidth * minMessageColumnWidthInChars, _cachedWidth - catW - tsW - srcW)
                case LogEntry.CategoryColumn:
                    return catW;
                case LogEntry.TimestampColumn:
                    return tsW;
                case LogEntry.SourceColumn:
                    return srcW;
                }
                return -1;
            }

            property bool _loadCompleted: false
            property real _cachedWidth: 0
            property bool _following: false
            property bool _atBottom: false

            function _updateAtBottom() {
                if (!_loadCompleted) {
                    return
                }
                const maxY = contentHeight - height
                if (maxY <= 0) {
                    _atBottom = true
                } else {
                    _atBottom = (contentY >= maxY - _rowHeight)
                }
                // Stop following if user scrolls away from bottom
                if (!_atBottom) {
                    _following = false
                }
            }

            onWidthChanged: {
                _cachedWidth = width;
                _layoutTimer.restart();
            }

            onContentYChanged: _updateAtBottom()
            onContentHeightChanged: _updateAtBottom()
            onHeightChanged: _updateAtBottom()

            Component.onCompleted: {
                _cachedWidth = width;
                _loadCompleted = true;
            }

            Timer {
                id: _layoutTimer

                interval: 0

                onTriggered: tableView.forceLayout()
            }

            delegate: Rectangle {
                required property int column
                required property var display
                required property int level
                required property int row

                color: row % 2 === 0 ? odcPalette.window : odcPalette.windowShade
                implicitHeight: _rowHeight

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.top: parent.top
                    color: root._levelColor(parent.level)
                    visible: parent.column === 0 && parent.level >= LogEntry.Warning
                    width: _indicatorWidth
                }

                ODCLabel {
                    anchors.left: parent.left
                    anchors.leftMargin: parent.column === 0 ? _indicatorWidth + _cellLeftMargin : _cellLeftMargin
                    anchors.right: parent.right
                    anchors.rightMargin: _cellRightMargin
                    anchors.verticalCenter: parent.verticalCenter
                    color: root._levelColor(parent.level)
                    elide: Text.ElideRight
                    font.family: ScreenTools.fixedFontFamily
                    font.pointSize: ScreenTools.defaultFontPointSize
                    text: parent.display ?? ""
                }
            }

            ODCLabel {
                x: tableView.contentX + (tableView.width - width) / 2
                y: tableView.contentY + (tableView.height - height) / 2
                color: odcPalette.colorGrey
                text: qsTr("No log entries")
                visible: tableView.rows === 0
            }

            Timer {
                id: scrollTimer
                interval: 50

                onTriggered: {
                    if (tableView.rows > 0)
                        tableView.positionViewAtRow(tableView.rows - 1, TableView.AlignBottom);
                }
            }

            Connections {
                function onRowsInserted() {
                    if (tableView._loadCompleted && tableView._following) {
                        scrollTimer.restart();
                    }
                }

                target: LogManager.model
            }
        }

        // ── Separator ─────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: odcPalette.groupBorder
        }

        // ── Filter bar ────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: filterRow.implicitHeight + _margin
            color: odcPalette.windowShade

            RowLayout {
                id: filterRow

                anchors.fill: parent
                anchors.leftMargin: _margin
                anchors.rightMargin: _margin
                spacing: _margin * 0.75

                ODCComboBox {
                    id: levelCombo

                    model: [qsTr("All Levels"), qsTr("Debug"), qsTr("Info"), qsTr("Warning"), qsTr("Critical"), qsTr("Fatal")]
                    sizeToContents: true

                    Component.onCompleted: currentIndex = LogManager.model.filterLevel + 1
                    onActivated: index => {
                        LogManager.model.filterLevel = index - 1;
                    }
                }

                ODCComboBox {
                    id: categoryCombo

                    model: [qsTr("All Categories")].concat(LogManager.model.categoriesList)
                    sizeToContents: true

                    onActivated: index => {
                        LogManager.model.filterCategory = index === 0 ? "" : model[index];
                    }
                }

                ODCTextField {
                    id: searchField

                    Layout.fillWidth: true
                    Layout.minimumWidth: _margin * 10
                    placeholderText: qsTr("Search…")

                    onTextChanged: LogManager.model.setFilterTextDeferred(text)
                }

                ODCButton {
                    ToolTip.text: qsTr("Regex search")
                    ToolTip.visible: hovered
                    checkable: true
                    checked: LogManager.model.filterRegex
                    text: qsTr(".*")

                    onClicked: LogManager.model.filterRegex = checked
                }

                ODCLabel {
                    color: odcPalette.colorRed
                    font.bold: true
                    text: qsTr("\u26A0 Disk Error")
                    visible: LogManager.hasError

                    ODCMouseArea {
                        anchors.fill: parent

                        onClicked: LogManager.clearError()
                    }
                }

                ODCButton {
                    text: qsTr("Categories")

                    onClicked: filtersDialogFactory.open()
                }

                ODCButton {
                    text: qsTr("Save")

                    onClicked: saveFileDialog.openForSave()
                }

                ODCButton {
                    text: qsTr("Clear")

                    onClicked: LogManager.model.clear()
                }
            }
        }
    }

    ODCButton {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: headerView.height + _margin
        anchors.top: parent.top
        text: qsTr("Show Latest")
        visible: tableView._loadCompleted && !tableView._atBottom && tableView.rows > 0
        opacity: 0.75

        onClicked: {
            tableView._following = true
            tableView.positionViewAtRow(tableView.rows - 1, TableView.AlignBottom)
        }
    }

    ODCPopupDialogFactory {
        id: filtersDialogFactory

        dialogComponent: Component {
            LoggingCategoriesDialog {
            }
        }
    }

    ODCFileDialog {
        id: saveFileDialog

        readonly property var _suffixes: ["txt", "csv"]

        defaultSuffix: _suffixes[0]
        folder: ""
        title: qsTr("Save app log")

        onAcceptedForSave: file => {
            LogManager.writeMessages(file)
        }
    }
}
