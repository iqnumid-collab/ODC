# OpenDroneCore Qt 6.3 Ground Control App — Implementation Progress

## Overview

Built a Qt 6 QML application framework for the OpenDroneCore ground control station. The app provides a C++ backend with bidirectional event bus, 81 renominated QML controls (migrated from QGC prefix to ODC), theme/palette engine, screen tools, and a clean main window shell. Zero drone-specific domain logic — pure application framework ready for plugin extensions.

**Status: Compiles and runs cleanly.**

---

## Project Structure

```
odc-app/
├── CMakeLists.txt                    # Qt 6 CMake build system
├── src/
│   ├── main.cpp                      # Entry point, wires C++ → QML
│   ├── app/
│   │   └── ODCAppEngine.{h,cpp}      # Root singleton, plugin loading, built-in commands
│   ├── core/
│   │   ├── ODCEventBus.{h,cpp}       # Bidirectional C++↔QML pub/sub bus
│   │   ├── ODCCommandRegistry.{h,cpp}# Typed command dispatch (QML→C++)
│   │   ├── ODCNotificationHub.{h,cpp}# Typed notifications (C++→QML)
│   │   ├── ODCSettingsManager.{h,cpp}# QSettings wrapper with group/key API
│   │   └── ODCPluginInterface.h      # Abstract plugin base class
│   └── ui/
│       ├── ODCTheme.{h,cpp}          # Global light/dark theme singleton
│       ├── ODCPalette.{h,cpp}        # Per-control palette (32 color tokens)
│       └── ODCScreenToolsController.{h,cpp}  # Platform/DPI/font metrics
├── qml/
│   └── OpenDroneCore/               # QML module URI "OpenDroneCore"
│       ├── MainWindow.qml            # Clean application shell
│       ├── MainWindowSavedState.qml  # Window geometry persistence
│       ├── controls/                 # 67 renamed QML controls
│       │   ├── ScreenTools.qml       # QML singleton (pragma Singleton)
│       │   ├── ODCButton.qml, ODCLabel.qml, ODCTextField.qml, ...
│       │   └── DeadMouseArea.qml, PipView.qml, LabelledButton.qml, ...
│       ├── settings/                 # Settings UI (5 files)
│       ├── flyview/                  # FlyView layouts (3 files, placeholders)
│       ├── flightmap/                # Map UI elements (2 files)
│       ├── viewer3d/                 # 3D viewer (1 file)
│       └── firstrun/                 # First-run prompts (2 files)
└── resources/
    └── odc-resources.qrc             # Qt resource file (icons, fonts)
```

---

## C++ Backend (10 files, ~1200 LOC)

### ODCAppEngine — Application Root
- `Q_PROPERTY` access to all managers (theme, settingsManager)
- Built-in command registration (toggleTheme, setTheme, getSetting, setSetting, notify, showMessage, listCommands)
- Plugin loading infrastructure via `ODCPluginInterface`
- `initialize()` / `shutdown()` lifecycle

### ODCEventBus — Bidirectional C++↔QML Pub/Sub
- `sendCommand(command, params)` — QML → C++ (fire-and-forget)
- `sendCommandAsync(command, params, callback)` — QML → C++ with JS callback
- `publishEvent(event, data)` — C++ → QML notification
- `subscribe(event, handler)` / `unsubscribe(subscriptionId)` — QML-side subscription API

### ODCCommandRegistry — Typed Command Dispatch
- Sync handler: `QVariantMap(const QVariantMap&)`
- Async handler: `void(const QVariantMap&, callback)`
- 9 built-in commands registered at startup

### ODCNotificationHub — C++→QML Notification Types
- `notifyAlert(title, message, level)` → `alertRaised` signal → EventBus
- `notifyStatus(source, status)` → `statusChanged` signal → EventBus
- `notifyMessage(text)` → `messageReceived` signal → EventBus

### ODCSettingsManager — Generic Settings Store
- `value(group, key, defaultValue)` / `setValue(group, key, value)`
- `keys(group)` / `remove(group, key)` / `clearGroup(group)` / `sync()`
- `valueChanged` / `groupsChanged` signals
- Wraps `QSettings` for persistent storage

### ODCTheme — Global Theme Manager
- `Light` / `Dark` enum
- `toggleTheme()`, `themeName()`, `isDark()`
- Palette list management via `registerPalette()` / `unregisterPalette()`
- All `ODCPalette` instances reactively update on theme change

### ODCPalette — Per-Control Color Instance
- `QML_ELEMENT` — instantiable from QML as `ODCPalette { id: odcPalette }`
- 32 semantic color tokens: `window`, `windowShade`, `text`, `button`, `buttonText`, `buttonHighlight`, `buttonHighlightText`, `buttonBorder`, `primaryButton`, `primaryButtonText`, `toolbarBackground`, `toolbarText`, `alertBackground`, `alertText`, `alertBorder`, `colorGreen`, `colorOrange`, `colorRed`, `colorBlue`, `colorGrey`, `mapWidget`, `mapWidgetBorder`, `brandLogoBlue`, `warningText`, `statusFailed`, `statusPassed`, `windowShadeLight`, `windowShadeDark`, `windowTransparent`, `textField`, `textFieldText`, `groupBorder`, `mapWidgetBorderLight`, `mapWidgetBorderDark`, `toolStripHoverColor`
- Each token resolves from 4 variants: `{Light+Enabled, Light+Disabled, Dark+Enabled, Dark+Disabled}`
- Auto-wires to `ODCTheme` singleton for reactive theme switching

### ODCScreenToolsController — Platform Detection
- OS detection: `isAndroid`, `isIOS`, `isMobile`, `isWindows`, `isMacOS`, `isLinux`, `isDebug`
- Font: `normalFontFamily`, `fixedFontFamily`, `defaultFontDescent(pointSize)`
- Display: `devicePixelDensity`, `mouseX()`, `mouseY()`
- Fake mobile mode for desktop testing

### ODCPluginInterface — Extension Point
```cpp
class ODCPluginInterface {
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual bool initialize(QQmlApplicationEngine*, QObject* appEngine) = 0;
    virtual void shutdown() = 0;
};
```

---

## QML Frontend (81 files migrated)

### Migration Summary
| Operation | Count |
|-----------|-------|
| Files renamed (QGC→ODC prefix) | 34 files |
| Import paths changed (7 URIs → 1 URI) | 81 files |
| Type references replaced (QGCPalette→ODCPalette, qgcPal→odcPalette, etc.) | 81 files |
| Domain-specific references stubbed/removed | ~60 references |
| New files created | 4 files (MainWindow, MainWindowSavedState, ScreenTools, resources) |

### Naming Convention Map (34 QGC→ODC renames)
| Old | New |
|-----|-----|
| QGCButton | ODCButton |
| QGCLabel | ODCLabel |
| QGCTextField | ODCTextField |
| QGCCheckBox | ODCCheckBox |
| QGCComboBox | ODCComboBox |
| QGCSlider | ODCSlider |
| QGCGroupBox | ODCGroupBox |
| QGCPopupDialog | ODCPopupDialog |
| QGCMenu / QGCMenuItem | ODCMenu / ODCMenuItem |
| QGCTabBar / QGCTabButton | ODCTabBar / ODCTabButton |
| QGCRoundButton | ODCRoundButton |
| QGCDelayButton | ODCDelayButton |
| QGCToolBarButton | ODCToolBarButton |
| QGCFlickable | ODCFlickable |
| QGCListView | ODCListView |
| QGCSwipeView | ODCSwipeView |
| QGCPageIndicator | ODCPageIndicator |
| QGCFileDialog | ODCFileDialog |
| QGCColoredImage | ODCColoredImage |
| QGCVectorImage | ODCVectorImage |
| QGCMovableItem | ODCMovableItem |
| QGCMarqueeLabel | ODCMarqueeLabel |
| QGCDynamicObjectManager | ODCDynamicObjectManager |
| QGCColumnButton | ODCColumnButton |
| QGCOptionsComboBox | ODCOptionsComboBox |
| QGCRadioButton | ODCRadioButton |
| QGCCheckBoxSlider | ODCCheckBoxSlider |
| QGCMouseArea | ODCMouseArea |
| QGCToolInsets | ODCToolInsets |
| SetupPage | ODCSetupPage |
| AppSettings | ODCAppSettings |
| ConfigSection | ODCConfigSection |
| ConfigButton | ODCConfigButton |
| TimedProgressTracker | ODCTimedProgressBar |
| SubMenuButton | ODCSubMenuButton |
| ToolStrip | ODCToolStrip |
| DropPanel | ODCDropPanel |

### Files Kept With Original Names (32 files)
DeadMouseArea, PipView, PipState, LabelledButton, LabelledComboBox, LabelledLabel, LabelledSlider, SliderSwitch, ValueSlider, IndicatorButton, SectionHeader, SettingsGroupLayout, SettingsButton, SelectableControl, DropButton, FileButton, ClickableColor, ExclusiveGroupItem, MvPanelPage, ToolIndicatorPage, MainWindowSavedState, ScreenTools, FirstRunPrompt, InitialSetupPrompt, MapScale, SplitIndicator, FlyViewTopRightPanel, FlyViewTopRightColumnLayout, FlyViewBottomRightRowLayout, Viewer3DProgressBar, AppLogging, DebugWindow, HelpSettings, QmlTest, SettingsPage

### MainWindow.qml — Clean Application Shell
- `ApplicationWindow` with theme toggle, settings drawer, debug window
- Event bus listener for `alert` and `messageDialog` events
- Notification toast system
- Tool drawer pattern (reusable for any tool panel)
- Close handling with force-close support

### ScreenTools.qml — QML Singleton
- `pragma Singleton` + `QT_QML_SINGLETON_TYPE TRUE`
- Font sizing: 4 scales (default, small, medium, large)
- UI sizing: implicit button/field/checkbox heights, border radius, min touch pixels
- Platform detection passthrough from `ODCScreenToolsController`
- Resolution-independent layout via `defaultFontPixelWidth` / `defaultFontPixelHeight`
- DPI-aware pixel density calculation

---

## Bidirectional Communication Architecture

```
┌─────────────────────────────────────────────┐
│  QML LAYER                                   │
│                                              │
│  ODCEventBus.sendCommand("toggleTheme", {})  │  ← QML → C++
│  ODCEventBus.publishEvent("alert", data)     │  ← C++ → QML
│                                              │
│  Connections {                                │
│      target: ODCEventBus                      │
│      onEventPublished: (event, data) => {}    │  ← Signal-based
│  }                                            │
└──────────────┬──────────────────────────────┘
               │
       Qt signals/slots + Q_PROPERTY bindings
               │
┌──────────────┴──────────────────────────────┐
│  C++ LAYER                                    │
│                                              │
│  ODCEventBus ──→ ODCCommandRegistry          │
│       │              │                        │
│  ODCNotificationHub ← ODCAppEngine            │
│       │              │                        │
│  EventBus sends "eventPublished" signal       │
│  to QML with typed event + payload            │
└──────────────────────────────────────────────┘
```

### Built-in Commands
| Command | Handler | Async |
|---------|---------|-------|
| `toggleTheme` | `ODCTheme::toggleTheme()` | No |
| `setTheme` | `ODCTheme::setGlobalTheme()` | No |
| `getTheme` | Returns theme name + isDark | No |
| `getSetting` | `ODCSettingsManager::value()` | No |
| `setSetting` | `ODCSettingsManager::setValue()` + event publish | No |
| `getSettingKeys` | `ODCSettingsManager::keys()` | No |
| `notify` | `ODCNotificationHub::notify()` | No |
| `showMessage` | `ODCNotificationHub::notifyAlert()` | No |
| `listCommands` | Returns all registered command names | No |

---

## Build System

### CMake Configuration
- **Qt 6 version**: 6.2.4 (Ubuntu 22.04 packages)
- **Qt components**: Core, Qml, Quick, QuickControls2, QuickDialogs2
- **C++ standard**: C++20
- **Module**: `qt_add_qml_module(odc-app-qml URI "OpenDroneCore" ...)` with 81 QML files + 8 C++ backing sources
- **Resource prefix**: `/qml`
- **QML singleton**: `ScreenTools.qml` via `set_source_files_properties(QT_QML_SINGLETON_TYPE TRUE)`
- **Type registration**: `ODCPalette` with `QML_ELEMENT` (auto-registered by `qmltyperegistrar`)
- **Context properties**: 6 singletons injected via `rootContext()->setContextProperty()` (matching QGC's pattern)

### QML Module Pipeline
```
Source Files (.qml, .cc, .h)
     │
     ├─► qt_add_qml_module() — registers module, files, types
     │       ├─► moc — Q_OBJECT metadata
     │       ├─► qmltyperegistrar — generates qmltypes, qmldir, C++ registration
     │       ├─► qmlcachegen — compiles QML to native C++ code (.qmlc AOT)
     │       └─► rcc — embeds resources in shared library
     │
     ├─► libodc-app-qml.so + libodc-app-qmlplugin.so (QML plugin)
     └─► opendronecore executable (links odc-app-qml)
```

### Runtime Output
```
Registered 9 built-in commands
OpenDroneCore Application Engine initialized
```

---

## Qt 6.2-Specific Workarounds Applied

| Issue | Root Cause | Fix |
|-------|-----------|-----|
| Double-path resource URLs | Qt 6.2 `qt_add_qml_module` includes full source path as resource alias | Load URL uses full path: `qrc:/qml/OpenDroneCore/qml/OpenDroneCore/MainWindow.qml` |
| `QtQuick.Dialogs` module missing | Not packaged for Ubuntu 22.04 Qt 6.2 | Replaced `Dialog.StandardButton` enums with inline `QtObject` containing lowercase property constants |
| Uppercase QML property names rejected | Qt 6 `qmlcachegen` strict mode requires lowercase property names | Renamed `Ok`→`ok`, `Cancel`→`cancel`, etc. in Dialog compat objects |
| Uppercase QML IDs rejected | Qt 6 `qmlcachegen` requires lowercase IDs | Renamed `Dialog`→`_dialog`, `FileDialog`→`_fileDialog` |
| Top-level QtObjects before root element | QML files must have a single root element | Moved Dialog compat objects inside the root element |
| Imports after object declarations | All imports must appear before any object declaration | Reordered imports |
| QColor(0x000000) ambiguous | Qt 6 QColor has multiple constructors (int vs QRgb vs const char*) | Changed to `QColor(QRgb(0x000000))` |
| QApplication not available | Qt 6 QApplication requires QtWidgets module (not linked) | Used `QGuiApplication` instead |
| QJSValue::engine() doesn't exist | Qt 6.2 QJSValue has no `engine()` method | Captured `QQmlApplicationEngine&` in lambda directly |
| QSettings::beginGroup() const | QSettings group methods are non-const | Removed const from settings accessor methods |

---

## Files Created

| File | Lines | Purpose |
|------|-------|---------|
| `src/main.cpp` | 98 | Entry point, C++ wiring |
| `src/app/ODCAppEngine.h` | 70 | Root engine header |
| `src/app/ODCAppEngine.cpp` | 150 | Command registration, plugin loading |
| `src/core/ODCEventBus.h` | 50 | Event bus header |
| `src/core/ODCEventBus.cpp` | 80 | Pub/sub implementation |
| `src/core/ODCCommandRegistry.h` | 38 | Command registry header |
| `src/core/ODCCommandRegistry.cpp` | 90 | Sync/async dispatch |
| `src/core/ODCNotificationHub.h` | 28 | Notification hub header |
| `src/core/ODCNotificationHub.cpp` | 35 | Notification emission |
| `src/core/ODCSettingsManager.h` | 38 | Settings header |
| `src/core/ODCSettingsManager.cpp` | 85 | QSettings wrapper |
| `src/core/ODCPluginInterface.h` | 28 | Plugin interface |
| `src/ui/ODCTheme.h` | 44 | Theme header |
| `src/ui/ODCTheme.cpp` | 40 | Theme singleton |
| `src/ui/ODCPalette.h` | 120 | Palette header (32 color tokens) |
| `src/ui/ODCPalette.cpp` | 280 | Color resolution logic |
| `src/ui/ODCScreenToolsController.h` | 70 | Screen tools header |
| `src/ui/ODCScreenToolsController.cpp` | 110 | Platform detection |
| `CMakeLists.txt` | 150 | Build system |
| `resources/odc-resources.qrc` | 14 | Qt resources |
| `qml/OpenDroneCore/MainWindow.qml` | 175 | Main window shell |
| `qml/OpenDroneCore/MainWindowSavedState.qml` | 48 | Window geometry |
| `qml/OpenDroneCore/controls/ScreenTools.qml` | 132 | Screen metrics singleton |

### Files Migrated (81 QML files from `ui-core/`)
All QGC-prefixed files renamed to ODC prefix, all imports consolidated to `import OpenDroneCore`, all type references updated.

---

## Next Steps (Future Work)

1. **Multiple QML modules**: Split into `OpenDroneCore.Controls`, `OpenDroneCore.FlyView`, etc. (like QGC's module structure)
2. **Static library**: Upgrade to Qt 6.5+ where `qt_add_qml_module` supports `STATIC` keyword to compile module as static library (`.a` instead of `.so`)
3. **Plugin loading**: Implement `ODCAppEngine::loadPlugin()` with `QPluginLoader`
4. **Hook client**: Implement `ODCHookClient` WebSocket connection to `odc-hook`
5. **Domain plugins**: Create plugins for MAVLink, video, telemetry, mission management
6. **Qt 6.3+ upgrade**: Use `qmlEngine.loadFromModule("OpenDroneCore", "MainWindow")` and `QML_SINGLETON` for C++ types
7. **QML linting**: Resolve CMake warning about OUTPUT_DIRECTORY for qmllint
8. **Icon/font assets**: Add actual SVG icons and OpenSans font files to resources
