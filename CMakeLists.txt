cmake_minimum_required(VERSION 3.21)

project(Lightscreen VERSION 3.0.1 LANGUAGES CXX)

# GitHub repository ("owner/name") whose releases the update check reads (renamed with M16).
set(LIGHTSCREEN_UPDATE_REPO "Ogrueb/Lightscreen-Rd")

# Shown in the Windows file properties (cmake/lightscreen.rc.in).
set(LIGHTSCREEN_PRODUCT_NAME "Lightscreen Rd")
set(LIGHTSCREEN_COMPANY "Lightscreen Rd contributors")
set(LIGHTSCREEN_DESCRIPTION "Lightscreen Rd Screenshot Tool")
set(LIGHTSCREEN_COPYRIGHT "Copyright (C) 2008-2021 Christian Kaiser, 2026 Lightscreen Rd contributors")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 6.2 is the oldest supported version (docs/build.md). qt_standard_project_setup() needs 6.3, so the
# automatic moc/uic/rcc steps are switched on by hand.
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network Sql Concurrent)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR "On Windows only MSVC is supported (docs/build.md).")
endif()

add_subdirectory(tools/SingleApplication)
add_subdirectory(tools/UGlobalHotkey)

# Everything but main.cpp, shared by the app and the tests (each test links it instead of compiling the
# sources again).
add_library(lightscreen_core OBJECT
    dialogs/areadialog.cpp dialogs/areadialog.h
    dialogs/historydialog.cpp dialogs/historydialog.h dialogs/historydialog.ui
    dialogs/namingdialog.cpp dialogs/namingdialog.h dialogs/namingdialog.ui
    dialogs/optionsdialog.cpp dialogs/optionsdialog.h dialogs/optionsdialog.ui
    dialogs/previewdialog.cpp dialogs/previewdialog.h
    dialogs/updaterdialog.cpp dialogs/updaterdialog.h
    lightscreenwindow.cpp lightscreenwindow.h lightscreenwindow.ui
    tools/capturegeometry.cpp tools/capturegeometry.h
    tools/hdrcapture.cpp tools/hdrcapture.h
    tools/optimizer.cpp tools/optimizer.h
    tools/os.cpp tools/os.h
    tools/screenshot.cpp tools/screenshot.h
    tools/screenshotmanager.cpp tools/screenshotmanager.h
    tools/settings.cpp tools/settings.h
    tools/taskbar.h
    tools/windowpicker.cpp tools/windowpicker.h
    tools/uploader/imageuploader.cpp tools/uploader/imageuploader.h
    tools/uploader/customuploader.cpp tools/uploader/customuploader.h
    tools/uploader/uploader.cpp tools/uploader/uploader.h
    updater/updater.cpp updater/updater.h
    widgets/hotkeywidget.cpp widgets/hotkeywidget.h
    widgets/uploaderoptionswidget.cpp widgets/uploaderoptionswidget.h widgets/uploaderoptionswidget.ui
    lightscreen.qrc
)

# The headers include their ui_*.h, so whoever includes a header (the tests) needs uic's output directory.
get_property(lightscreen_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(lightscreen_multi_config)
    set(lightscreen_uic_dir "${CMAKE_CURRENT_BINARY_DIR}/lightscreen_core_autogen/include_$<CONFIG>")
else()
    set(lightscreen_uic_dir "${CMAKE_CURRENT_BINARY_DIR}/lightscreen_core_autogen/include")
endif()
target_include_directories(lightscreen_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${lightscreen_uic_dir}"
)

target_compile_definitions(lightscreen_core PUBLIC
    # Anything Qt deprecated before 6.0 is a compile error, so later deprecations cannot pile up.
    QT_DEPRECATED_WARNINGS
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
    APP_VERSION="${PROJECT_VERSION}"
    UPDATE_REPO="${LIGHTSCREEN_UPDATE_REPO}"
)
target_compile_options(lightscreen_core PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(lightscreen_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Sql Qt6::Concurrent
    SingleApplication
    UGlobalHotkey
)

if(WIN32)
    target_sources(lightscreen_core PRIVATE tools/taskbar.cpp)
    target_link_libraries(lightscreen_core PUBLIC
        gdi32 user32 ole32 shell32 shlwapi comctl32 dwmapi winmm uuid d3d11 dxgi runtimeobject)
else()
    target_link_libraries(lightscreen_core PUBLIC X11)
endif()

qt_add_executable(lightscreen WIN32 main.cpp)
target_link_libraries(lightscreen PRIVATE lightscreen_core)

if(WIN32)
    # Icon and version information, as qmake's RC_ICONS and VERSION/QMAKE_TARGET_* produced them.
    configure_file(cmake/lightscreen.rc.in "${CMAKE_CURRENT_BINARY_DIR}/lightscreen.rc" @ONLY)
    target_sources(lightscreen PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/lightscreen.rc")
    target_link_options(lightscreen PRIVATE "/MANIFESTUAC:level='asInvoker'")
endif()

option(BUILD_TESTING "Build the tests (tests/)" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
