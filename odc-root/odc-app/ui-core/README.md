# UI-Core: Generic QML Components (81 files)

Pure software/UI components extracted from QGroundControl — reusable in **any** application.
These have no dependency on drone concepts, MAVLink, vehicle data, or flight operations.

## Contents

| Directory | Files | Key Components |
|-----------|-------|----------------|
| **controls/** | 66 QML | `QGCButton`, `QGCLabel`, `QGCTextField`, `QGCCheckBox`, `QGCComboBox`, `QGCSlider`, `QGCTabBar`, `QGCMenu`, `QGCPopupDialog`, `QGCFileDialog`, `ToolStrip`, `DeadMouseArea`, `SettingsGroupLayout`, `PipView`, `QGCMouseArea`, `QGCListView`, `QGCDelayButton`, `QGCGroupBox`, `ConfigSection`, `DropPanel`, `SubMenuButton` ... |
| **mainwindow/** | 2 QML | `MainWindow.qml`, `MainWindowSavedState.qml` (application shell pattern) |
| **settings/** | 5 QML | `SettingsPage.qml` (base template), `AppLogging.qml`, `HelpSettings.qml`, `QmlTest.qml`, `DebugWindow.qml` |
| **flyview/** | 3 QML | `FlyViewTopRightColumnLayout`, `FlyViewBottomRightRowLayout`, `FlyViewTopRightPanel` (layout containers) |
| **first-run/** | 2 QML | `FirstRunPrompt.qml`, `InitialSetupPrompt.qml` |
| **flightmap/** | 2 QML | `MapScale.qml` (cartographic scale bar), `SplitIndicator.qml` |
| **viewer3d/** | 1 QML | `Viewer3DProgressBar.qml` |

## Design System

These components depend on:
- **QGCPalette** — 48 semantic color tokens (light/dark themes, enabled/disabled states)
- **ScreenTools** — Typography (font families, 4 size scales) and spacing (all derived from base font pixel metrics)
- See `../design-system/` for extracted token JSON files

## Component Categories

### Primitives (Qt Quick overrides)
QGCButton, QGCLabel, QGCTextField, QGCCheckBox, QGCComboBox, QGCSlider, QGCSwipeView, QGCGroupBox, QGCRadioButton, QGCListView, QGCTabBar/QGCTabButton

### Buttons & Controls
QGCRoundButton, QGCColumnButton, QGCDelayButton, QGCToolBarButton, IndicatorButton, DropButton, ClickableColor, FileButton

### Layout & Structure
ConfigSection, SectionHeader, DropPanel, ToolStrip, ToolStripHoverButton, ToolStripDropPanel, SettingsGroupLayout, SetupPage, MvPanelPage, SelectableControl, ExclusiveGroupItem

### Dialogs & Menus
QGCPopupDialog, QGCPopupDialogFactory, QGCSimpleMessageDialog, QGCFileDialog, QGCMenu/QGCMenuItem/QGCMenuSeparator

### Utilities
DeadMouseArea, QGCFlickable, QGCFlickableScrollIndicator, QGCMovableItem, QGCDynamicObjectManager, QGCMarqueeLabel, PipView/PipState, SliderSwitch, TimedProgressTracker, QGCToolInsets

### Images & Icons
QGCColoredImage, QGCVectorImage, QGCPageIndicator

### Form Helpers
LabelledButton, LabelledComboBox, LabelledLabel, LabelledSlider, SettingsButton, SubMenuButton, AppSettings, ConfigButton, QGCOptionsComboBox, QGCMouseArea, ValueSlider

## Key Dependencies (C++ Backend)
- `Q_PROPERTY`: QGCPalette colors, ScreenTools metrics
- `QML_SINGLETON`: ScreenTools, QGroundControl (global state)
- `QML_ELEMENT`: ToolStripAction, ToolStripActionList
