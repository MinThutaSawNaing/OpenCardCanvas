# ============================================================================
#  OpenCardCanvas - Card design, personalization, preview and printing suite
#  Platform : Windows 10 / 11 (x64)
#  Toolchain: Visual Studio 2022 (MSVC v143) + Qt 6 + CMake
# ============================================================================
cmake_minimum_required(VERSION 3.21)

project(OpenCardCanvas
    VERSION 1.0.0
    DESCRIPTION "Professional ID card design, personalization, preview and printing software"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# Qt discovery.
#   -DOCC_QT_PREFIX=<kit dir>   (highest precedence)
#   %QTDIR%
#   CMAKE_PREFIX_PATH
# ---------------------------------------------------------------------------
if(DEFINED ENV{QTDIR})
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
endif()
if(DEFINED OCC_QT_PREFIX)
    list(PREPEND CMAKE_PREFIX_PATH "${OCC_QT_PREFIX}")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core Gui Widgets PrintSupport Svg Network Sql Test)
qt_standard_project_setup()


# ---------------------------------------------------------------------------
# Compiler options
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/utf-8 /permissive- /W4 /MP /bigobj /Zc:__cplusplus)
    add_compile_options(/wd4100 /wd4251 /wd4127 /wd4324)
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
        _CRT_SECURE_NO_WARNINGS
        _WIN32_WINNT=0x0A00 WINVER=0x0A00
        QT_DISABLE_DEPRECATED_BEFORE=0x060500)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Generated version header (single source of truth for the version number)
# ---------------------------------------------------------------------------
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils/Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/occ/Version.h"
    @ONLY)

# ---------------------------------------------------------------------------
# Windows resource script (application icon + version information)
# ---------------------------------------------------------------------------
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/win/app.rc")
    set(OCC_APP_RC "${CMAKE_CURRENT_SOURCE_DIR}/resources/win/app.rc")
else()
    set(OCC_APP_RC "")
endif()

# ---------------------------------------------------------------------------
# Sources, grouped by layer. Dependencies point one way:
#   utils -> core -> codes/rendering -> project/personalization/printing
#   -> commands/canvas -> ui
# ---------------------------------------------------------------------------
set(OCC_UTILS
    src/utils/Logger.cpp        src/utils/Logger.h
    src/utils/AppPaths.cpp      src/utils/AppPaths.h
    src/utils/Settings.cpp      src/utils/Settings.h
    src/utils/SimpleZip.cpp     src/utils/SimpleZip.h
    src/utils/BidiClient.cpp    src/utils/BidiClient.h
    src/utils/GdiPrintSurface.cpp src/utils/GdiPrintSurface.h
    src/utils/ImageCache.cpp    src/utils/ImageCache.h
    src/utils/TextUtils.cpp     src/utils/TextUtils.h
)

set(OCC_CORE
    src/core/CardTypes.h
    src/core/Units.cpp          src/core/Units.h
    src/core/CardGeometry.cpp   src/core/CardGeometry.h
    src/core/CardObject.cpp     src/core/CardObject.h
    src/core/TextObject.cpp     src/core/TextObject.h
    src/core/ImageObject.cpp    src/core/ImageObject.h
    src/core/PhotoObject.cpp    src/core/PhotoObject.h
    src/core/ShapeObject.cpp    src/core/ShapeObject.h
    src/core/QrObject.cpp       src/core/QrObject.h
    src/core/BarcodeObject.cpp  src/core/BarcodeObject.h
    src/core/GroupObject.cpp    src/core/GroupObject.h
    src/core/CardSide.cpp       src/core/CardSide.h
    src/core/CardDocument.cpp   src/core/CardDocument.h
    src/core/ObjectFactory.cpp  src/core/ObjectFactory.h
)

set(OCC_CODES
    src/codes/QrEncoder.cpp     src/codes/QrEncoder.h
    src/codes/BarcodeEncoder.cpp src/codes/BarcodeEncoder.h
)

set(OCC_RENDER
    src/rendering/RenderContext.cpp src/rendering/RenderContext.h
    src/rendering/TextLayout.cpp    src/rendering/TextLayout.h
    src/rendering/CardRenderer.cpp  src/rendering/CardRenderer.h
    src/rendering/ExportRenderer.cpp src/rendering/ExportRenderer.h
)

set(OCC_CANVAS
    src/canvas/CanvasGeometry.cpp src/canvas/CanvasGeometry.h
    src/canvas/GuideModel.cpp     src/canvas/GuideModel.h
    src/canvas/SnapEngine.cpp     src/canvas/SnapEngine.h
    src/canvas/RulerWidget.cpp    src/canvas/RulerWidget.h
    src/canvas/CardCanvas.cpp     src/canvas/CardCanvas.h
    src/canvas/CanvasHost.cpp     src/canvas/CanvasHost.h
)

set(OCC_PROJECT
    src/project/AssetStore.cpp        src/project/AssetStore.h
    src/project/ProjectSerializer.cpp src/project/ProjectSerializer.h
    src/project/ProjectValidator.cpp  src/project/ProjectValidator.h
    src/project/AutoSave.cpp          src/project/AutoSave.h
    src/project/TemplateStore.cpp     src/project/TemplateStore.h
)

set(OCC_PERSONALIZATION
    src/personalization/CsvImporter.cpp    src/personalization/CsvImporter.h
    src/personalization/TemplateEngine.cpp src/personalization/TemplateEngine.h
    src/personalization/DataMapper.cpp     src/personalization/DataMapper.h
    src/personalization/BatchRenderer.cpp  src/personalization/BatchRenderer.h
)

set(OCC_PRINTING
    src/printing/PrinterTypes.cpp           src/printing/PrinterTypes.h
    src/printing/ICardPrinter.h
    src/printing/WindowsPrinterBackend.cpp  src/printing/WindowsPrinterBackend.h
    src/printing/EntrustXpsPrinter.cpp      src/printing/EntrustXpsPrinter.h
    src/printing/SimulatorPrinter.cpp       src/printing/SimulatorPrinter.h
    src/printing/PrinterManager.cpp         src/printing/PrinterManager.h
)

set(OCC_COMMANDS
    src/commands/UndoCommands.cpp src/commands/UndoCommands.h
)

set(OCC_UI
    src/ui/IconFactory.cpp          src/ui/IconFactory.h
    src/ui/ToolsPanel.cpp           src/ui/ToolsPanel.h
    src/ui/PropertyPanel.cpp        src/ui/PropertyPanel.h
    src/ui/LayersPanel.cpp          src/ui/LayersPanel.h
    src/ui/AlignPanel.cpp           src/ui/AlignPanel.h
    src/ui/PersonalizationPanel.cpp src/ui/PersonalizationPanel.h
    src/ui/PrintPreviewDialog.cpp   src/ui/PrintPreviewDialog.h
    src/ui/PrinterSettingsDialog.cpp src/ui/PrinterSettingsDialog.h
    src/ui/PreferencesDialog.cpp    src/ui/PreferencesDialog.h
    src/ui/AboutDialog.cpp          src/ui/AboutDialog.h
    src/ui/DiagnosticsDialog.cpp    src/ui/DiagnosticsDialog.h
    src/ui/NewCardDialog.cpp        src/ui/NewCardDialog.h
    src/ui/CardSizeDialog.cpp       src/ui/CardSizeDialog.h
    src/ui/MainWindow.cpp           src/ui/MainWindow.h
)

# ---------------------------------------------------------------------------
# occcore - the application engine (static library)
# ---------------------------------------------------------------------------
add_library(occcore STATIC
    ${OCC_UTILS} ${OCC_CORE} ${OCC_CODES} ${OCC_RENDER} ${OCC_CANVAS}
    ${OCC_PROJECT} ${OCC_PERSONALIZATION} ${OCC_PRINTING} ${OCC_COMMANDS}
    ${OCC_UI}
)

target_include_directories(occcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_compile_definitions(occcore PUBLIC OCC_APP_NAME="OpenCardCanvas")

target_link_libraries(occcore
    PUBLIC
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport
        Qt6::Svg Qt6::Network Qt6::Sql
    PRIVATE
        # Windows print spooler, GDI and the BidiSpl COM server used by the
        # XPS Card Printer SDK path. There is no bidispl.lib in the SDK, so the
        # spooler COM classes are obtained with __uuidof(BidiSpl) and only the
        # COM libraries below need linking.
        winspool ole32 oleaut32 uuid gdi32 user32
        shell32 comdlg32 shlwapi version advapi32
)

# ---------------------------------------------------------------------------
# OpenCardCanvas - the application
# ---------------------------------------------------------------------------
qt_add_executable(OpenCardCanvas WIN32 src/main.cpp)
target_link_libraries(OpenCardCanvas PRIVATE occcore)
if(OCC_APP_RC)
    target_sources(OpenCardCanvas PRIVATE ${OCC_APP_RC})
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(OCC_BUILD_TESTS "Build the OpenCardCanvas unit tests" ON)
if(OCC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Packaging helper: windeployqt, so build/bin becomes a portable release.
#     cmake --build build --target portable
# The Qt bin directory is derived from the imported Qt6::Core target, because
# not every Qt installation exposes a Qt6::Qmake target.
# ---------------------------------------------------------------------------
get_target_property(_occ_qt_core_imploc Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT _occ_qt_core_imploc)
    get_target_property(_occ_qt_core_imploc Qt6::Core IMPORTED_LOCATION)
endif()
if(_occ_qt_core_imploc)
    get_filename_component(OCC_QT_BIN_DIR "${_occ_qt_core_imploc}" DIRECTORY)
else()
    set(OCC_QT_BIN_DIR "")
endif()

if(OCC_QT_BIN_DIR)
    add_custom_target(portable
        COMMAND ${CMAKE_COMMAND}
            -DQT_BIN_DIR=${OCC_QT_BIN_DIR}
            -DAPP_DIR=$<TARGET_FILE_DIR:OpenCardCanvas>
            -DDIST_DIR=${CMAKE_BINARY_DIR}/dist
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployQt.cmake"
        DEPENDS OpenCardCanvas
        COMMENT "Creating the portable distribution (windeployqt) in build/dist"
        VERBATIM)
endif()
