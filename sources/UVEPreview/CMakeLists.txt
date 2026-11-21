# UVEPreview – eigenständiger Build außerhalb des Player-Repos. Braucht nur PluginApi.h aus dem Player:
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt> -DUVE_PLAYER_DIR=<Player>
# libmpv wird nicht gelinkt: das Modul nutzt zur Laufzeit die libmpv, die der Player schon geladen hat
# (Header der Client-/Render-API liegen unter third_party/mpv, ISC-Lizenz).
cmake_minimum_required(VERSION 3.25)
project(UVEPreview VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /wd4251)
    add_compile_definitions(NOMINMAX UNICODE _UNICODE)
endif()

set(UVE_PLAYER_DIR "M:/LAPS/UVE Player" CACHE PATH "Quellordner des UVE Player")
option(BUILD_TESTING "Tests bauen" ON)
set(qt_parts Core Gui Qml Quick)
if(BUILD_TESTING)
    list(APPEND qt_parts Test)
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS ${qt_parts})
qt_standard_project_setup(REQUIRES 6.5)

add_library(uvepreview_worker STATIC PreviewWorker.cpp PreviewWorker.h)
target_include_directories(uvepreview_worker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(uvepreview_worker PUBLIC Qt6::Core Qt6::Gui ${CMAKE_DL_LIBS})
set_target_properties(uvepreview_worker PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(uve_uvepreview MODULE UvePreviewPlugin.cpp UvePreviewPlugin.h manifest.json)
qt_add_resources(uve_uvepreview "uvepreview_qml" PREFIX "/uvepreview" FILES PreviewLayer.qml UvePreviewSettings.qml)
target_include_directories(uve_uvepreview PRIVATE "${UVE_PLAYER_DIR}/src/plugins")
target_link_libraries(uve_uvepreview PRIVATE uvepreview_worker Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)

if(BUILD_TESTING)
    enable_testing()
    qt_add_executable(tst_uvepreview tst_uvepreview.cpp)
    target_include_directories(tst_uvepreview PRIVATE "${UVE_PLAYER_DIR}/src/plugins")
    target_link_libraries(tst_uvepreview PRIVATE uvepreview_worker Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
    target_compile_definitions(tst_uvepreview PRIVATE UVEPREVIEW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
                                                      UVE_PLAYER_DIR="${UVE_PLAYER_DIR}")
    add_test(NAME tst_uvepreview COMMAND tst_uvepreview)
endif()
