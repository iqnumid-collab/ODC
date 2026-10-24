import OpenDroneCore

Rectangle {
    id: progressBody

    property string progressText: qsTr("Progress")
    property real progressValue: 100.0

    color: odcPalette.windowShadeDark
    height: _progressCol.height + 2 * ScreenTools.defaultFontPixelWidth
    opacity: (progressValue < 100) ? (1.0) : (0.0)
    radius: ScreenTools.defaultFontPixelWidth * 2
    visible: opacity > 0
    width: ScreenTools.screenWidth * 0.2

    Behavior on opacity {
        NumberAnimation {
            duration: 300
        }
    }

    ODCPalette {
        id: odcPalette

        colorGroupEnabled: true
    }

    Column {
        id: _progressCol

        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
        }

        ProgressBar {
            id: _progressBar

            from: 0
            to: 100
            value: progressBody.progressValue

            anchors {
                left: parent.left
                margins: ScreenTools.defaultFontPixelWidth
                right: parent.right
            }
        }

        ODCLabel {
            anchors.horizontalCenter: parent.horizontalCenter
            color: odcPalette.text
            font.bold: true
            font.pointSize: ScreenTools.mediumFontPointSize
            horizontalAlignment: Text.AlignHCenter
            text: progressText + Number(Math.floor(progressBody.progressValue)) + " %"
        }
    }
}
