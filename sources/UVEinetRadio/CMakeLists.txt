# UVEinetRadio – standalone build outside the player repository. Needs only PluginApi.h from the player:
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt> -DUVE_PLAYER_DIR=<Player>
# Same Qt (6.12) and toolchain as the player. The package also needs the files from qml/ and lang/*.qm next to the module.
# Optional live test with real libmpv: -DUVEINETRADIO_MPV_DIR=<folder with include/mpv and the import library>
cmake_minimum_required(VERSION 3.25)
project(UVEinetRadio VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
if(MSVC)
    add_compile_options(/utf-8 /permissive- /Zc:__cplusplus /W3)
    add_compile_definitions(NOMINMAX UNICODE _UNICODE)
endif()

set(UVE_PLAYER_DIR "M:/LAPS/UVE Player" CACHE PATH "Source folder of the UVE Player")
set(UVEINETRADIO_MPV_DIR "" CACHE PATH "libmpv (include/ + import library) for the optional live playback test")
option(BUILD_TESTING "Build tests" ON)

set(qt_parts Core Gui Network Qml)
if(BUILD_TESTING)
    list(APPEND qt_parts Test Quick QuickControls2)
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS ${qt_parts})
qt_standard_project_setup(REQUIRES 6.5)

add_library(uveinetradio_core STATIC
    RadioTypes.cpp RadioTypes.h
    RadioBrowser.cpp RadioBrowser.h
    Caches.cpp Caches.h
    StationStore.cpp StationStore.h
    StreamProbe.cpp StreamProbe.h
    StationListModel.cpp StationListModel.h
    RadioController.cpp RadioController.h)
target_include_directories(uveinetradio_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} "${UVE_PLAYER_DIR}/src/plugins")
target_link_libraries(uveinetradio_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network)

# Module name uve_uveinetradio.dll / libuve_uveinetradio.so / .dylib – the plugin build finds it by its target name
add_library(uve_uveinetradio MODULE UveInetRadioPlugin.cpp UveInetRadioPlugin.h manifest.json)
target_link_libraries(uve_uveinetradio PRIVATE uveinetradio_core Qt6::Qml)

# Pages and translations are plain files in the package (qml/, lang/); copy them next to the module
set(UVEINETRADIO_QML
    qml/RadioPanel.qml qml/RadioSettings.qml qml/NowPlaying.qml qml/StationRow.qml qml/StationLogo.qml
    qml/RButton.qml qml/RField.qml qml/RFilter.qml qml/RadioTexts.qml)
set(UVEINETRADIO_TS lang/uveinetradio_en.ts lang/uveinetradio_de.ts)
foreach(f ${UVEINETRADIO_QML})
    add_custom_command(TARGET uve_uveinetradio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${f}" "$<TARGET_FILE_DIR:uve_uveinetradio>/${f}")
endforeach()
add_custom_command(TARGET uve_uveinetradio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json" "$<TARGET_FILE_DIR:uve_uveinetradio>/manifest.json")
# Prebuilt .qm files (checked in) are copied too, so the package works without Qt Linguist on the build machine
foreach(code en de)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lang/uveinetradio_${code}.qm")
        add_custom_command(TARGET uve_uveinetradio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/lang/uveinetradio_${code}.qm"
                    "$<TARGET_FILE_DIR:uve_uveinetradio>/lang/uveinetradio_${code}.qm")
    endif()
endforeach()

if(BUILD_TESTING)
    enable_testing()
    qt_add_executable(tst_uveinetradio tests/tst_uveinetradio.cpp tests/TestHttpServer.h tests/FakeHost.h)
    target_link_libraries(tst_uveinetradio PRIVATE uveinetradio_core Qt6::Test Qt6::Qml Qt6::Quick Qt6::QuickControls2)
    target_compile_definitions(tst_uveinetradio PRIVATE UVEINETRADIO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME tst_uveinetradio COMMAND tst_uveinetradio)

    if(UVEINETRADIO_MPV_DIR)
        qt_add_executable(tst_uveinetradio_live tests/tst_live.cpp tests/FakeHost.h)
        target_include_directories(tst_uveinetradio_live PRIVATE "${UVEINETRADIO_MPV_DIR}/include")
        find_library(UVEINETRADIO_MPV_LIB NAMES mpv libmpv mpv.dll PATHS "${UVEINETRADIO_MPV_DIR}" NO_DEFAULT_PATH)
        if(NOT UVEINETRADIO_MPV_LIB)
            find_library(UVEINETRADIO_MPV_LIB NAMES mpv)
        endif()
        target_link_libraries(tst_uveinetradio_live PRIVATE uveinetradio_core Qt6::Test "${UVEINETRADIO_MPV_LIB}")
    endif()
endif()
