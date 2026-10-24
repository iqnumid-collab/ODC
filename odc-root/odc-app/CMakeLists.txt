cmake_minimum_required(VERSION 3.22)
project(OpenDroneCoreApp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Qml Quick QuickControls2 QuickDialogs2)

set_source_files_properties(
    qml/OpenDroneCore/controls/ScreenTools.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(odc-app-qml
    URI "OpenDroneCore"
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES
        qml/OpenDroneCore/MainWindow.qml
        qml/OpenDroneCore/MainWindowSavedState.qml
        qml/OpenDroneCore/controls/ScreenTools.qml
        qml/OpenDroneCore/controls/ODCButton.qml
        qml/OpenDroneCore/controls/ODCLabel.qml
        qml/OpenDroneCore/controls/ODCTextField.qml
        qml/OpenDroneCore/controls/ODCCheckBox.qml
        qml/OpenDroneCore/controls/ODCComboBox.qml
        qml/OpenDroneCore/controls/ODCSlider.qml
        qml/OpenDroneCore/controls/ODCGroupBox.qml
        qml/OpenDroneCore/controls/ODCPopupDialog.qml
        qml/OpenDroneCore/controls/ODCPopupDialogFactory.qml
        qml/OpenDroneCore/controls/ODCSimpleMessageDialog.qml
        qml/OpenDroneCore/controls/ODCMenu.qml
        qml/OpenDroneCore/controls/ODCMenuItem.qml
        qml/OpenDroneCore/controls/ODCMenuSeparator.qml
        qml/OpenDroneCore/controls/ODCTabBar.qml
        qml/OpenDroneCore/controls/ODCTabButton.qml
        qml/OpenDroneCore/controls/ODCToolStrip.qml
        qml/OpenDroneCore/controls/ODCToolStripHoverButton.qml
        qml/OpenDroneCore/controls/ODCToolStripDropPanel.qml
        qml/OpenDroneCore/controls/ODCToolInsets.qml
        qml/OpenDroneCore/controls/ODCRoundButton.qml
        qml/OpenDroneCore/controls/ODCDelayButton.qml
        qml/OpenDroneCore/controls/ODCSetupPage.qml
        qml/OpenDroneCore/controls/ODCDropPanel.qml
        qml/OpenDroneCore/controls/ODCFileDialog.qml
        qml/OpenDroneCore/controls/ODCColoredImage.qml
        qml/OpenDroneCore/controls/ODCVectorImage.qml
        qml/OpenDroneCore/controls/ODCTimedProgressBar.qml
        qml/OpenDroneCore/controls/ODCMovableItem.qml
        qml/OpenDroneCore/controls/ODCMarqueeLabel.qml
        qml/OpenDroneCore/controls/ODCDynamicObjectManager.qml
        qml/OpenDroneCore/controls/ODCColumnButton.qml
        qml/OpenDroneCore/controls/ODCPageIndicator.qml
        qml/OpenDroneCore/controls/ODCListView.qml
        qml/OpenDroneCore/controls/ODCFlickable.qml
        qml/OpenDroneCore/controls/ODCFlickableScrollIndicator.qml
        qml/OpenDroneCore/controls/ODCOptionsComboBox.qml
        qml/OpenDroneCore/controls/ODCRadioButton.qml
        qml/OpenDroneCore/controls/ODCCheckBoxSlider.qml
        qml/OpenDroneCore/controls/ODCConfigButton.qml
        qml/OpenDroneCore/controls/ODCSubMenuButton.qml
        qml/OpenDroneCore/controls/ODCAppSettings.qml
        qml/OpenDroneCore/controls/ODCConfigSection.qml
        qml/OpenDroneCore/controls/DeadMouseArea.qml
        qml/OpenDroneCore/controls/PipView.qml
        qml/OpenDroneCore/controls/PipState.qml
        qml/OpenDroneCore/controls/LabelledButton.qml
        qml/OpenDroneCore/controls/LabelledComboBox.qml
        qml/OpenDroneCore/controls/LabelledLabel.qml
        qml/OpenDroneCore/controls/LabelledSlider.qml
        qml/OpenDroneCore/controls/SliderSwitch.qml
        qml/OpenDroneCore/controls/ValueSlider.qml
        qml/OpenDroneCore/controls/IndicatorButton.qml
        qml/OpenDroneCore/controls/SectionHeader.qml
        qml/OpenDroneCore/controls/SettingsGroupLayout.qml
        qml/OpenDroneCore/controls/SettingsButton.qml
        qml/OpenDroneCore/controls/SelectableControl.qml
        qml/OpenDroneCore/controls/DropButton.qml
        qml/OpenDroneCore/controls/FileButton.qml
        qml/OpenDroneCore/controls/ClickableColor.qml
        qml/OpenDroneCore/controls/ExclusiveGroupItem.qml
        qml/OpenDroneCore/controls/ODCMouseArea.qml
        qml/OpenDroneCore/controls/MvPanelPage.qml
        qml/OpenDroneCore/controls/ToolIndicatorPage.qml
        qml/OpenDroneCore/controls/ODCToolBarButton.qml
        qml/OpenDroneCore/settings/SettingsPage.qml
        qml/OpenDroneCore/settings/AppLogging.qml
        qml/OpenDroneCore/settings/HelpSettings.qml
        qml/OpenDroneCore/settings/DebugWindow.qml
        qml/OpenDroneCore/settings/QmlTest.qml
        qml/OpenDroneCore/flyview/FlyViewTopRightPanel.qml
        qml/OpenDroneCore/flyview/FlyViewTopRightColumnLayout.qml
        qml/OpenDroneCore/flyview/FlyViewBottomRightRowLayout.qml
        qml/OpenDroneCore/flightmap/MapScale.qml
        qml/OpenDroneCore/flightmap/SplitIndicator.qml
        qml/OpenDroneCore/viewer3d/Viewer3DProgressBar.qml
        qml/OpenDroneCore/firstrun/FirstRunPrompt.qml
        qml/OpenDroneCore/firstrun/InitialSetupPrompt.qml
    SOURCES
        src/app/ODCAppEngine.cpp
        src/core/ODCEventBus.cpp
        src/core/ODCCommandRegistry.cpp
        src/core/ODCNotificationHub.cpp
        src/core/ODCSettingsManager.cpp
        src/ui/ODCTheme.cpp
        src/ui/ODCPalette.cpp
        src/ui/ODCScreenToolsController.cpp
)

qt_add_executable(opendronecore
    src/main.cpp
)

target_include_directories(opendronecore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(odc-app-qml PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)

target_link_libraries(odc-app-qml PUBLIC
    Qt6::Core
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
)

target_link_libraries(opendronecore PRIVATE
    odc-app-qml
    Qt6::Core
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
)

target_compile_definitions(opendronecore PRIVATE
    $<$<CONFIG:Debug>:QT_DEBUG>
)

set_target_properties(opendronecore PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
