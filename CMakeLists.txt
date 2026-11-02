cmake_minimum_required(VERSION 3.25)

project(UVEPlayer VERSION 1.0.0 LANGUAGES CXX
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

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Qml Network OpenGL Concurrent Test Xml LinguistTools)
qt_standard_project_setup(REQUIRES 6.5)

include(cmake/Mpv.cmake)

enable_testing()

# Gemeinsame Kernbibliothek (ohne GUI-Abhängigkeiten zu QML), auch von Tests genutzt
add_subdirectory(src)
# Anwendung
add_subdirectory(app)
# Plugins (getrennte DLLs, über PluginApi.h angebunden)
add_subdirectory(plugins/uve)
add_subdirectory(plugins/examples/sleeptimer)
# Tests
add_subdirectory(tests)
