# Eigenständiger Build des KickChat-Plugins für den UVE Player
# (der Player wird nur lesend für PluginApi.h eingebunden – er bleibt unverändert)
cmake_minimum_required(VERSION 3.25)
project(uve_kickchat_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /wd4251 /wd4310)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX UNICODE _UNICODE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Gui Qml Quick)
qt_standard_project_setup(REQUIRES 6.5)

set(HOST_PLUGIN_API_DIR "M:/LAPS/UVE Player/src/plugins" CACHE PATH "Ordner mit PluginApi.h")
set(PLUGIN_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../out/plugin")
set(TEST_OUT_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/../out/test")

set(CHAT_SOURCES
    MiniWebSocket.cpp MiniWebSocket.h
    NotifySound.cpp NotifySound.h
    InputEmoteStyler.cpp InputEmoteStyler.h
    SpellChecker.cpp SpellChecker.h
    GrammarRules.cpp GrammarRules.h
    TextPredictor.cpp TextPredictor.h
    LanguageToolClient.cpp LanguageToolClient.h
    TokenVault.cpp TokenVault.h
    KickEmoteCache.cpp KickEmoteCache.h
    KickAppDefaults.h
    KickAuth.cpp KickAuth.h
    KickChatController.cpp KickChatController.h)

# Hunspell 1.7.6 (MPL 1.1, unverändert aus dem Original-Tarball), statisch in die DLL
set(HUNSPELL_SRC third_party/hunspell/src/hunspell)
add_library(hunspell_static STATIC
    ${HUNSPELL_SRC}/affentry.cxx ${HUNSPELL_SRC}/affixmgr.cxx ${HUNSPELL_SRC}/csutil.cxx
    ${HUNSPELL_SRC}/filemgr.cxx ${HUNSPELL_SRC}/hashmgr.cxx ${HUNSPELL_SRC}/hunspell.cxx
    ${HUNSPELL_SRC}/hunspelltrace.cxx ${HUNSPELL_SRC}/hunzip.cxx ${HUNSPELL_SRC}/phonet.cxx
    ${HUNSPELL_SRC}/replist.cxx ${HUNSPELL_SRC}/suggestmgr.cxx)
set_target_properties(hunspell_static PROPERTIES AUTOMOC OFF POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(hunspell_static PUBLIC HUNSPELL_STATIC PRIVATE BUILDING_LIBHUNSPELL)
target_include_directories(hunspell_static PUBLIC ${HUNSPELL_SRC} PRIVATE third_party/hunspell/msvc)
if(MSVC)
    target_compile_options(hunspell_static PRIVATE /W0)
endif()
link_libraries(hunspell_static)

function(kick_output target dir)
    foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
        set_target_properties(${target} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY${cfg} ${dir}
            RUNTIME_OUTPUT_DIRECTORY${cfg} ${dir})
    endforeach()
endfunction()

# Plugin-DLL (Qt-Plugin mit eingebettetem Manifest)
add_library(kickchat MODULE ${CHAT_SOURCES} KickChatPlugin.cpp KickChatPlugin.h manifest.json)
target_include_directories(kickchat PRIVATE "${HOST_PLUGIN_API_DIR}")
target_link_libraries(kickchat PRIVATE Qt6::Core Qt6::Network Qt6::Gui)
if(WIN32)
    target_link_libraries(kickchat PRIVATE crypt32 winmm)
endif()
kick_output(kickchat ${PLUGIN_OUT_DIR})

add_custom_command(TARGET kickchat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json ${PLUGIN_OUT_DIR}/manifest.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/ChatPanel.qml ${PLUGIN_OUT_DIR}/ChatPanel.qml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/KickChat.qml  ${PLUGIN_OUT_DIR}/KickChat.qml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/sounds/pn.mp3 ${PLUGIN_OUT_DIR}/pn.mp3
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/README.txt    ${PLUGIN_OUT_DIR}/README.txt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md  ${PLUGIN_OUT_DIR}/CHANGELOG.md
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md ${PLUGIN_OUT_DIR}/THIRD_PARTY_NOTICES.md
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dict ${PLUGIN_OUT_DIR}/dict
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hunspell/COPYING.MPL ${PLUGIN_OUT_DIR}/dict/COPYING_Hunspell_MPL.txt)

# Test 1: Konsole – Parser offline + echter Kanal (Kanal-Infos, Verlauf, Live-Chat über Pusher)
add_executable(chat_test test/chat_test.cpp test/fakehost.h ${CHAT_SOURCES})
target_include_directories(chat_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${HOST_PLUGIN_API_DIR}")
target_link_libraries(chat_test PRIVATE Qt6::Core Qt6::Network Qt6::Gui)
if(WIN32)
    target_link_libraries(chat_test PRIVATE crypt32 winmm)
endif()
kick_output(chat_test ${TEST_OUT_DIR})

# Test 2: offscreen – lädt ChatPanel.qml + KickChat.qml echt in der QML-Engine mit echtem Chat
add_executable(qml_panel_test test/qml_panel_test.cpp test/fakehost.h ${CHAT_SOURCES})
target_include_directories(qml_panel_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${HOST_PLUGIN_API_DIR}")
target_link_libraries(qml_panel_test PRIVATE Qt6::Core Qt6::Network Qt6::Gui Qt6::Qml Qt6::Quick)
if(WIN32)
    target_link_libraries(qml_panel_test PRIVATE crypt32 winmm)
endif()
target_compile_definitions(qml_panel_test PRIVATE QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    DICT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/third_party/dict")
kick_output(qml_panel_test ${TEST_OUT_DIR})

# Test 3: prüft, ob die DLL als UVE-Player-Plugin erkannt wird (Metadaten)
add_executable(plugin_probe test/plugin_probe.cpp)
target_link_libraries(plugin_probe PRIVATE Qt6::Core)
kick_output(plugin_probe ${TEST_OUT_DIR})
