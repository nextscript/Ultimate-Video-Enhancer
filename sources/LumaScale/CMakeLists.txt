cmake_minimum_required(VERSION 3.25)
project(LumaScale VERSION 1.0.0 LANGUAGES CXX)

# Header of the player's plugin API (src/plugins/PluginApi.h)
set(UVE_PLAYER_SOURCE "M:/LAPS/UVE Player" CACHE PATH "UVE Player source folder")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)

add_library(lumascale MODULE LumaScalePlugin.cpp LumaScalePlugin.h manifest.json)
qt_add_resources(lumascale "lumascale_res" PREFIX "/lumascale" FILES LumaScalePanel.qml lumascale.glsl.in)
target_include_directories(lumascale PRIVATE "${UVE_PLAYER_SOURCE}/src/plugins")
target_link_libraries(lumascale PRIVATE Qt6::Core Qt6::Gui)
