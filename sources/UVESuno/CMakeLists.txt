# Eigenständiger Build außerhalb des Player-Repos. Braucht nur PluginApi.h aus dem Player:
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt> -DUVE_PLAYER_DIR=<Player>
cmake_minimum_required(VERSION 3.25)
project(UVESuno VERSION 1.0.1 LANGUAGES CXX)

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

add_library(uvesuno_core STATIC SunoCore.cpp SunoCore.h SunoNet.cpp SunoNet.h)
target_include_directories(uvesuno_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uvesuno_core PUBLIC Qt6::Core Qt6::Network)

add_library(uve_uvesuno MODULE UveSunoPlugin.cpp UveSunoPlugin.h manifest.json)
qt_add_resources(uve_uvesuno "uvesuno_qml" PREFIX "/uvesuno" FILES UveSunoPanel.qml)
target_include_directories(uve_uvesuno PRIVATE "${UVE_PLAYER_DIR}/src/plugins")
target_link_libraries(uve_uvesuno PRIVATE uvesuno_core Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml)

enable_testing()
qt_add_executable(tst_uvesuno tst_uvesuno.cpp)
target_link_libraries(tst_uvesuno PRIVATE uvesuno_core Qt6::Test)
add_test(NAME tst_uvesuno COMMAND tst_uvesuno)
