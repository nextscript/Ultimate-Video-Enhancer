cmake_minimum_required(VERSION 3.25)
project(GlslManager VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Ordner des Player-Quellcodes (nur für src/plugins/PluginApi.h)
set(UVE_PLAYER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../UVE Player" CACHE PATH "UVE Player source folder")
if(NOT EXISTS "${UVE_PLAYER_DIR}/src/plugins/PluginApi.h")
    message(FATAL_ERROR "PluginApi.h nicht gefunden: ${UVE_PLAYER_DIR}/src/plugins (UVE_PLAYER_DIR setzen)")
endif()

# Zielschlüssel wie im Manifest ("libraries") und im Player (currentTargetKey)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _cpu)
if(CMAKE_OSX_ARCHITECTURES)
    string(TOLOWER "${CMAKE_OSX_ARCHITECTURES}" _cpu)
endif()
if(WIN32)
    if(_cpu MATCHES "arm64|aarch64")
        set(_key windows-arm64)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_key windows-x64)
    else()
        set(_key windows-x86)
    endif()
elseif(APPLE)
    if(_cpu MATCHES "arm64|aarch64")
        set(_key macos-arm64)
    else()
        set(_key macos-x86_64)
    endif()
else()
    if(_cpu MATCHES "arm64|aarch64")
        set(_key linux-aarch64)
    else()
        set(_key linux-x86_64)
    endif()
endif()
set(UVE_TARGET_KEY "${_key}" CACHE STRING "Zielschlüssel des Plugin-Moduls (windows-x64, linux-x86_64, macos-arm64, …)")

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /wd4251)
    add_compile_definitions(NOMINMAX UNICODE _UNICODE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Test LinguistTools)

add_library(glsl_core STATIC
    src/ShaderConverter.cpp src/ShaderConverter.h
    src/ShaderValidator.cpp src/ShaderValidator.h
    src/GlslController.cpp src/GlslController.h)
target_include_directories(glsl_core PUBLIC src "${UVE_PLAYER_DIR}/src/plugins")
target_link_libraries(glsl_core PUBLIC Qt6::Core Qt6::Gui)
set_target_properties(glsl_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(glsl_manager MODULE src/GlslPlugin.cpp src/GlslPlugin.h manifest.json)
target_link_libraries(glsl_manager PRIVATE glsl_core)
if(APPLE)
    set_target_properties(glsl_manager PROPERTIES SUFFIX ".dylib")   # wie die Qt-Plugins und der Player erwarten
endif()

# Fertiger Plugin-Ordner wie im ZIP: build/package/GLSLManager/ mit dem Modul unter bin/<Zielschlüssel>/
set(PKG "${CMAKE_BINARY_DIR}/package/GLSLManager")
qt_add_lrelease(glsl_manager TS_FILES lang/glsl_manager_en.ts QM_FILES_OUTPUT_VARIABLE GLSL_QM)
add_custom_command(TARGET glsl_manager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PKG}/qml" "${PKG}/lang" "${PKG}/bin/${UVE_TARGET_KEY}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:glsl_manager>" "${PKG}/bin/${UVE_TARGET_KEY}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json" "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${PKG}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/qml/GlslPanel.qml" "${CMAKE_CURRENT_SOURCE_DIR}/qml/GlslEditor.qml" "${PKG}/qml/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GLSL_QM} "${PKG}/lang/")

option(GLSL_BUILD_TESTS "Tests bauen" ON)
if(GLSL_BUILD_TESTS)
    enable_testing()
    add_executable(tst_glsl tests/tst_glsl.cpp)
    target_compile_definitions(tst_glsl PRIVATE GLSL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(tst_glsl PRIVATE glsl_core Qt6::Test Qt6::Qml Qt6::Quick Qt6::QuickControls2)
    add_test(NAME tst_glsl COMMAND tst_glsl)
    if(NOT WIN32)
        set_tests_properties(tst_glsl PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
