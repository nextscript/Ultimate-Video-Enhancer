cmake_minimum_required(VERSION 3.25)
project(uve_oauth LANGUAGES CXX)

# QML-Erweiterung des YouTube-Plugins: Loopback-Empfänger und PKCE für "Mit Google verbinden".
# Landet im Plugin-Paket unter oauth/ neben der qmldir ("plugin uve_oauth"); Qt sucht je System
# uve_oauth.dll (Windows), libuve_oauth.so (Linux) bzw. libuve_oauth.dylib (macOS).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Qml)

add_library(uve_oauth MODULE plugin.cpp OAuthLoopback.cpp OAuthLoopback.h)
target_link_libraries(uve_oauth PRIVATE Qt6::Core Qt6::Network Qt6::Qml)
if(WIN32)
    set_target_properties(uve_oauth PROPERTIES PREFIX "")
elseif(APPLE)
    set_target_properties(uve_oauth PROPERTIES SUFFIX ".dylib")
endif()
