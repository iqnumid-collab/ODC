import OpenDroneCore

SettingsButton {
    icon.color: setupComplete ? textColor : "red"

    property bool setupComplete: true
}
