# Eigenständiger Build außerhalb des Player-Repos. Braucht nur PluginApi.h aus dem Player:
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt> -DUVE_PLAYER_DIR=<Player>
cmake_minimum_required(VERSION 3.25)
project(UVESpotify VERSION 1.0.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /wd4251)
    add_compile_definitions(NOMINMAX UNICODE _UNICODE)
endif()

set(UVE_PLAYER_DIR "M:/LAPS/UVE Player" CACHE PATH "Quellordner des UVE Player")
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Test)
qt_standard_project_setup(REQUIRES 6.5)

add_library(uvespotify_core STATIC SpotifyCore.cpp SpotifyCore.h SpotifyNet.cpp SpotifyNet.h)
target_include_directories(uvespotify_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uvespotify_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network)

add_library(uve_uvespotify MODULE UveSpotifyPlugin.cpp UveSpotifyPlugin.h manifest.json)
qt_add_resources(uve_uvespotify "uvespotify_qml" PREFIX "/uvespotify" FILES UveSpotifyPanel.qml)
target_include_directories(uve_uvespotify PRIVATE "${UVE_PLAYER_DIR}/src/plugins")
target_link_libraries(uve_uvespotify PRIVATE uvespotify_core Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml)
if(WIN32)
    target_link_libraries(uve_uvespotify PRIVATE crypt32)
endif()

enable_testing()
qt_add_executable(tst_uvespotify tst_uvespotify.cpp)
target_link_libraries(tst_uvespotify PRIVATE uvespotify_core Qt6::Test)
add_test(NAME tst_uvespotify COMMAND tst_uvespotify)
