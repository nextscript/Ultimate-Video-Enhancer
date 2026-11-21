# UVE Watch Party – standalone build outside the player repository. Needs only PluginApi.h from the player:
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt> -DUVE_PLAYER_DIR=<Player>
# WebRTC: libdatachannel (DataChannels only, no media) with Mbed TLS, both linked statically into the module, so the
# package needs no extra libraries. Sources are fetched by FetchContent; local clones can be used with
#   -DFETCHCONTENT_SOURCE_DIR_LIBDATACHANNEL=<dir> -DFETCHCONTENT_SOURCE_DIR_MBEDTLS=<dir>
cmake_minimum_required(VERSION 3.25)
project(UVEWatchParty VERSION 1.0.5 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(UVE_PLAYER_DIR "M:/LAPS/UVE Player" CACHE PATH "Source folder of the UVE Player")
option(BUILD_TESTING "Build tests" ON)

# ------------------------------------------------------------------ Mbed TLS + libdatachannel (static)
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# Dependencies are linked into the module, nothing of them is installed
set(CMAKE_SKIP_INSTALL_RULES ON)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
# libdatachannel needs DTLS-SRTP in Mbed TLS (also without media)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/cmake")
add_compile_definitions("MBEDTLS_USER_CONFIG_FILE=<uvewp_mbedtls_config.h>")
FetchContent_Declare(mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG v3.6.4
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE TRUE
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(mbedtls)
if(NOT TARGET MbedTLS::MbedTLS)
    add_library(uvewp_mbedtls INTERFACE)
    target_link_libraries(uvewp_mbedtls INTERFACE mbedtls mbedx509 mbedcrypto)
    add_library(MbedTLS::MbedTLS ALIAS uvewp_mbedtls)
endif()

set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
set(NO_MEDIA ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
FetchContent_Declare(libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG v0.23.2
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE TRUE
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(libdatachannel)

# ------------------------------------------------------------------ plugin
set(CMAKE_AUTOMOC ON)
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/permissive-> $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)
    add_compile_definitions(NOMINMAX)
endif()

set(qt_parts Core Gui Network Qml Quick)
if(BUILD_TESTING)
    list(APPEND qt_parts Test)
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS ${qt_parts})
qt_standard_project_setup(REQUIRES 6.5)

add_library(uvewp_core STATIC
    SyncLogic.h
    WsClient.cpp WsClient.h
    Crypto.cpp Crypto.h
    Nostr.cpp Nostr.h
    RoomHost.cpp RoomHost.h
    Tunnel.cpp Tunnel.h
    Manifest.cpp Manifest.h
    BlockCache.cpp BlockCache.h
    Swarm.cpp Swarm.h
    StreamServer.cpp StreamServer.h
    WatchPartyController.cpp WatchPartyController.h)
target_include_directories(uvewp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} "${UVE_PLAYER_DIR}/src/plugins")
target_link_libraries(uvewp_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network LibDataChannel::LibDataChannelStatic MbedTLS::MbedTLS)

add_library(uve_uvewatchparty MODULE WatchPartyPlugin.cpp WatchPartyPlugin.h manifest.json)
qt_add_resources(uve_uvewatchparty "uvewatchparty_qml" PREFIX "/uvewatchparty"
    FILES WatchPartyPanel.qml WatchPartySettings.qml)
target_link_libraries(uve_uvewatchparty PRIVATE uvewp_core Qt6::Qml Qt6::Quick)
if(APPLE)
    set_target_properties(uve_uvewatchparty PROPERTIES PREFIX "lib" OUTPUT_NAME "uvewatchparty" SUFFIX ".dylib")
elseif(UNIX)
    set_target_properties(uve_uvewatchparty PROPERTIES PREFIX "lib" OUTPUT_NAME "uvewatchparty")
endif()

if(BUILD_TESTING)
    enable_testing()
    qt_add_executable(tst_uvewatchparty tst_uvewatchparty.cpp)
    target_link_libraries(tst_uvewatchparty PRIVATE uvewp_core Qt6::Test Qt6::Qml Qt6::Quick)
    target_compile_definitions(tst_uvewatchparty PRIVATE UVEWP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME tst_uvewatchparty COMMAND tst_uvewatchparty)
endif()
