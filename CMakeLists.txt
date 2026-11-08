cmake_minimum_required(VERSION 3.25)

project(UVEPlayer VERSION 1.1.3 LANGUAGES CXX
        DESCRIPTION "UVE Player – Medienplayer mit libmpv und Ultimate-Video-Enhancer-Plugin")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /wd4251)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX UNICODE _UNICODE)
endif()

# Ausgaben gesammelt, damit Plugins und Tests neben der EXE liegen
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# Mehrkonfigurations-Generatoren (Visual Studio) ohne Unterordner Release/Debug
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/bin)
endforeach()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Qml Network OpenGL Concurrent Test Xml LinguistTools ShaderTools)
qt_standard_project_setup(REQUIRES 6.5)

# Qt WebEngine (Erweiterung zu Qt 6.12): Schnittstelle für Plugin-Web-Ansichten (webview.available).
# Ohne installierte Qt6WebEngineQuick-Komponente mit -DUVE_WITH_WEBENGINE=OFF bauen.
option(UVE_WITH_WEBENGINE "Qt WebEngine einbauen (WebViews für Plugins, z. B. Twitch-Embed-Chat)" ON)
if(UVE_WITH_WEBENGINE)
    # Die Qt-SDK-Repository enthält für 6.12.0 GA keine WebEngine-Binaries (Windows/Linux/macOS) –
    # CI und Fremd-Builds fallen dann automatisch ohne WebViews zurück (Plugins nutzen ihre Native-Ansicht).
    find_package(Qt6 6.5 QUIET COMPONENTS WebEngineQuick)
    if(NOT Qt6WebEngineQuick_FOUND)
        set(UVE_WITH_WEBENGINE OFF)
        message(STATUS "Qt WebEngine-Komponente nicht installiert - Build ohne eingebettete Web-Ansichten")
    else()
        add_compile_definitions(UVE_WITH_WEBENGINE=1)
    endif()
endif()

include(cmake/Mpv.cmake)

enable_testing()

# Gemeinsame Kernbibliothek (ohne GUI-Abhängigkeiten zu QML), auch von Tests genutzt
add_subdirectory(src)
# Anwendung
add_subdirectory(app)
# Explorer-Vorschaubilder (Shell-Erweiterung, nur Windows)
if(WIN32)
    add_subdirectory(thumbnailer)
endif()
# Plugins (getrennte DLLs, über PluginApi.h angebunden)
add_subdirectory(plugins/uve)
add_subdirectory(plugins/examples/sleeptimer)
# Tests
add_subdirectory(tests)
