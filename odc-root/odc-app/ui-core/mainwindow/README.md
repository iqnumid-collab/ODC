# MainWindow Module

## Overview
The application shell. Only 2 QML files: `MainWindow.qml` (root ApplicationWindow) and `MainWindowSavedState.qml` (window position persistence).

## Component Tree
```
ApplicationWindow
├── FlyView (always visible, anchors.fill)
├── PlanView (visible: false toggled)
├── toolDrawer (overlay for Analyze/Configure/Settings)
├── indicatorDrawer (popup for toolbar indicator details)
├── criticalVehicleMessagePopup (error banner)
└── LogReplayStatusBar (footer)
```

## Key Functions
- `showFlyView()`, `showPlanView()`, `showTool(title, source, icon)`
- `showIndicatorDrawer(component, indicatorItem)`
- `allowViewSwitch()` — guarded by validationErrorCount
- `createWindowedAnalyzePage()` — pop-out analyze pages
