# QGroundControl.Controls Module (Core Controls)

## Overview
The `QGroundControl.Controls` module provides the foundation UI component library. All 98 QML components in this directory are the building blocks used across every view in the application.

## Design System Dependencies
- **QGCPalette** (`QGCPalette { id: qgcPal; colorGroupEnabled: enabled }`) — Every component creates a palette instance. All colors reference `qgcPal.*` properties.
- **ScreenTools** (singleton) — Typography, spacing, sizing. All dimensions are derived from `defaultFontPixelWidth/Height`.
- **QGroundControl** (singleton) — Global state access: `QGroundControl.multiVehicleManager`, `.settingsManager`, `.corePlugin`, `.globalPalette`.

## C++ Binding Pattern
Types are registered via Qt6 declarative macros (`QML_ELEMENT`, `QML_SINGLETON`, etc.) in header files. Static QML module via `qt_add_qml_module(QGroundControlControlsModule URI QGroundControl.Controls)` with NO_PLUGIN — linked directly to executable.

## Component Categories

### Primitives (Direct replacements for Qt Quick Controls)
- **QGCButton.qml** — Standard push button. Supports `primary` mode, icon+text, hover/pressed highlight at 0.2 opacity. C++ deps: QGCPalette, ScreenTools.
- **QGCLabel.qml** — Universal text. OpenSans font, `qgcPal.text` color, antialiased.
- **QGCTextField.qml** — Text input with validation support, units display, help button, iOS numeric keyboard hints.
- **QGCCheckBox.qml** — Custom checkbox with SVG checkmark indicator, hover highlight.
- **QGCComboBox.qml** — Dropdown with custom popup positioning (above/below), size-to-contents support.
- **QGCSlider.qml** — Slider with optional zero-centered display, boundary labels, value display on handle.
- **QGCGroupBox.qml** — Grouped container with `qgcPal.windowShade` background.
- **QGCSwipeView.qml** — Minimal SwipeView wrapper.
- **QGCListView.qml** — Styled list view.
- **QGCFlickable.qml** / **QGCFlickableScrollIndicator.qml** — Scroll containers.

### Layout & Structure
- **ConfigSection.qml** — Configuration section with icon + heading, `windowShade` background, rounded corners.
- **ConfigButton.qml** — Navigation button with setup completion indicator.
- **SectionHeader.qml** — Styled section divider.
- **SetupPage.qml** — Base page for vehicle setup with Armed/Flying disable overlay, Advanced checkbox, section filter.
- **DropPanel.qml** — Dropdown panel with arrow indicator.
- **QGCToolInsets.qml** — Declarative inset calculation for map-safe-area awareness.
- **MvPanelPage.qml** — Multi-vehicle panel base.
- **ExclusiveGroupItem.qml** — Exclusive selection group.
- **SelectableControl.qml** — Selectable wrapper.

### Toolbar & Navigation
- **QGCToolBarButton.qml** — Toolbar button with manual checked state management (for view switch prevention).
- **QGCRoundButton.qml** — Circular map overlay button with optional rotation animation.
- **ToolStrip.qml** — Vertical icon toolbar (7-unit wide) with exclusive check state, scrollable.
- **ToolStripHoverButton.qml** — Individual tool strip button with icon/images, text, drop panel support.
- **ToolStripDropPanel.qml** — Drop panel from tool strip.
- **QGCColumnButton.qml** — Column-layout button.
- **QGCDelayButton.qml** — Press-and-hold button (long-press required).
- **IndicatorButton.qml** — Status indicator button for toolbar.

### Dialogs & Popups
- **QGCPopupDialog.qml** / **QGCPopupDialogFactory.qml** — Modal dialog system with dynamic creation.
- **QGCSimpleMessageDialog.qml** — Simple message dialog via `showMessageDialog()`.
- **QGCMenu.qml** / **QGCMenuItem.qml** / **QGCMenuSeparator.qml** — Popup menu system.

### Tab System
- **QGCTabBar.qml** — Custom TabBar supporting hidden tabs (unlike Qt's native TabBar).
- **QGCTabButton.qml** — Individual tab button with managed check state and separator lines.

### Specialized Controls
- **FlightModeMenu.qml** — Flight mode selection popup.
- **PreFlightCheckGroup.qml** / **PreFlightCheckButton.qml** / **PreFlightCheckModel.qml** — Pre-flight checklist.
- **AutotuneUI.qml** — PID auto-tuning UI.
- **PIDTuning.qml** — PID parameter tuning display.
- **RCChannelMonitor.qml** — RC channel bars.
- **RCToParamDialog.qml** — RC-to-parameter mapping.
- **Axismonitor.qml** — Sensor axis monitor.
- **JoystickThumbPad.qml** — Virtual joystick.
- **VehicleRotationCal.qml** / **VehicleSummaryRow.qml** — Vehicle setup helpers.
- **InstrumentValueLabel.qml** / **InstrumentValueValue.qml** / **InstrumentValueEditDialog.qml** — Telemetry display.
- **HeightIndicator.qml** — Altitude indicator.
- **ValueSlider.qml** — Value slider.
- **AltFrameDialog.qml** / **AltFrameCombo.qml** — Altitude reference frame.
- **ParameterEditor.qml** / **ParameterEditorDialog.qml** / **ParameterDiffDialog.qml** — Parameter editing.
- **EditPositionDialog.qml** — Coordinate editing.
- **PipView.qml** / **PipState.qml** — Picture-in-picture management.
- **QGCMapLabel.qml** — Map overlay label.
- **QGCMarqueeLabel.qml** — Scrolling marquee text.
- **QGCQGeoCoordinate.qml** — QGeoCoordinate wrapper for QML.
- **QGCDynamicObjectManager.qml** — Dynamic QML object factory.
- **QGCVectorImage.qml** / **QGCColoredImage.qml** — SVG/colored image rendering.
- **QGCFileDialog.qml** — File dialog.
- **LabelledButton.qml** / **LabelledComboBox.qml** / **LabelledLabel.qml** — Form row widgets.
- **SubMenuButton.qml** — Sidebar navigation button.
- **SettingsButton.qml** — Settings navigation button.
- **SettingsGroupLayout.qml** — Bordered settings group.
- **ConnectionStatusRow.qml** / **FixStatusDot.qml** — Connection indicators.
- **SliderSwitch.qml** — Toggle switch.
- **DeadMouseArea.qml** — Click-through blocker.
- **FileButton.qml** — File browse button.
- **MissionCommandDialog.qml** — Mission command text dialog.
- **TimedProgressTracker.qml** — Timed progress bar.
- **OfflineMapButton.qml** — Offline map button.
- **APMSubMotorDisplay.qml** — Sub motor display.
- **FactSlider.qml** — Fact-bound slider.
- **ToolIndicatorPage.qml** — Base page for tool indicator drawers.
- **KMLOrSHPFileDialog.qml** — KML/SHP import dialog.
- **LogReplayStatusBar.qml** — Log replay status bar.

## Key Conventions
- Every component creates `QGCPalette { id: qgcPal; colorGroupEnabled: enabled }` at root
- All text uses `QGCLabel {}` (never raw `Text`)
- All spacing uses `ScreenTools.defaultFontPixelWidth/Height` multiples
- Colors never hardcoded - always through `qgcPal.*` properties
- Toolbar buttons manage checked state manually (not `checkable`/`autoExclusive`) for view switch prevention
- Overlays use `DeadMouseArea` to block click-through to map
