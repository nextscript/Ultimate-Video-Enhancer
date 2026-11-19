# eigenständiger Build des TwitchChat-Plugins für den UVE Player
# (die Originalquelle des Spielers wird nur lesend für PluginApi.h eingebunden)
cmake_minimum_required(VERSION 3.25)
project(uve_twitchchat_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /wd4251 /wd4310)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX UNICODE _UNICODE)
endif()

# Qt6::Gui wird nur für die QImage-Signaturen in PluginApi.h benötigt (die DLL bindet ohnehin ins Qt6Gui des Players ein)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Gui Qml Quick)
qt_standard_project_setup(REQUIRES 6.5)

set(HOST_PLUGIN_API_DIR "M:/LAPS/UVE Player/src/plugins")   # nur PluginApi.h – Original bleibt unverändert
set(PLUGIN_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../out/plugin")
set(TEST_OUT_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/../out/test")

# gemeinsame Quellen (Chat)
set(CHAT_SOURCES
    MiniWebSocket.cpp MiniWebSocket.h
    EmoteCache.cpp EmoteCache.h
    InputEmoteStyler.cpp InputEmoteStyler.h
    SpellChecker.cpp SpellChecker.h
    GrammarRules.cpp GrammarRules.h
    LanguageToolClient.cpp LanguageToolClient.h
    TextPredictor.cpp TextPredictor.h
    WhisperService.cpp WhisperService.h
    NotifySound.cpp NotifySound.h
    TwitchAuthService.cpp TwitchAuthService.h
    TwitchChatController.cpp TwitchChatController.h
    TwitchChatModeration.cpp TwitchChatLive.cpp)
set(TOKENVAULT_SOURCES
    TokenVault.cpp TokenVault.h)
# Browser-Klassen (Helix/OAuth/Thumbnails/Browser-Controller) werden vom Plugin
# seit 2.1.0 NICHT mehr gebaut – sie liegen nur noch als Test-Regression da.
set(BROWSER_SOURCES
    ${TOKENVAULT_SOURCES}
    TwitchHelix.cpp TwitchHelix.h
    ThumbnailCache.cpp ThumbnailCache.h
    TwitchBrowserController.cpp TwitchBrowserController.h)

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
if(WIN32)
    link_libraries(winmm)          # MCI für den Benachrichtigungston
endif()

# Plugin-DLL (Qt-Plugin mit eingebettetem Manifest) – nur Chat, kein Browser mehr
add_library(twitchchat MODULE
    ${CHAT_SOURCES}
    ${TOKENVAULT_SOURCES}
    TwitchChatPlugin.cpp TwitchChatPlugin.h
    manifest.json)
set_target_properties(twitchchat PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
target_include_directories(twitchchat PRIVATE "${HOST_PLUGIN_API_DIR}")
target_link_libraries(twitchchat PRIVATE Qt6::Core Qt6::Network Qt6::Gui)
if(WIN32)
    target_link_libraries(twitchchat PRIVATE crypt32)   # DPAPI für den TokenVault
endif()
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(twitchchat PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY${cfg} ${PLUGIN_OUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY${cfg} ${PLUGIN_OUT_DIR})
endforeach()

# fertigen Plugin-Ordner bestücken (manifest + QML + Doku neben die DLL)
add_custom_command(TARGET twitchchat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json  ${PLUGIN_OUT_DIR}/manifest.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/ChatPanel.qml   ${PLUGIN_OUT_DIR}/ChatPanel.qml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/ChatNative.qml  ${PLUGIN_OUT_DIR}/ChatNative.qml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/Inbox.qml       ${PLUGIN_OUT_DIR}/Inbox.qml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/ChattersPopup.qml ${PLUGIN_OUT_DIR}/ChattersPopup.qml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/LiveChannels.qml ${PLUGIN_OUT_DIR}/LiveChannels.qml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/BoxSettingsPopup.qml ${PLUGIN_OUT_DIR}/BoxSettingsPopup.qml
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/sounds/pn.mp3   ${PLUGIN_OUT_DIR}/pn.mp3
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/README.txt      ${PLUGIN_OUT_DIR}/README.txt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md    ${PLUGIN_OUT_DIR}/CHANGELOG.md
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md ${PLUGIN_OUT_DIR}/THIRD_PARTY_NOTICES.md
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dict ${PLUGIN_OUT_DIR}/dict
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/third_party/hunspell/COPYING.MPL ${PLUGIN_OUT_DIR}/dict/COPYING_Hunspell_MPL.txt)

# Test 1: Konsole – echter Chat-Ablauf über MiniWebSocket (Fake-Host)
add_executable(chat_test
    test/chat_test.cpp test/fakehost.h
    ${CHAT_SOURCES}
    TokenVault.cpp TokenVault.h)
set_target_properties(chat_test PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
target_include_directories(chat_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${HOST_PLUGIN_API_DIR}")
target_link_libraries(chat_test PRIVATE Qt6::Core Qt6::Network Qt6::Gui)
if(WIN32)
    target_link_libraries(chat_test PRIVATE crypt32)
endif()
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(chat_test PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR})
endforeach()

# Live-Probe: angeheftete Nachricht eines echten Kanals
add_executable(pin_probe test/pin_probe.cpp test/fakehost.h ${CHAT_SOURCES} TokenVault.cpp TokenVault.h)
set_target_properties(pin_probe PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
target_include_directories(pin_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${HOST_PLUGIN_API_DIR}")
target_link_libraries(pin_probe PRIVATE Qt6::Core Qt6::Network Qt6::Gui)
if(WIN32)
    target_link_libraries(pin_probe PRIVATE crypt32)
endif()
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(pin_probe PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR})
endforeach()

# Test 2: Konsole – API-Parser, Thumbnail-Logik, OAuth-Zustände, Vault, Fake-Host-Integration
add_executable(browser_test
    test/browser_test.cpp test/fakehost.h
    ${CHAT_SOURCES}
    ${BROWSER_SOURCES})
set_target_properties(browser_test PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
target_include_directories(browser_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${HOST_PLUGIN_API_DIR}")
target_link_libraries(browser_test PRIVATE Qt6::Core Qt6::Network Qt6::Gui)
if(WIN32)
    target_link_libraries(browser_test PRIVATE crypt32)
endif()
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(browser_test PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR})
endforeach()

# Diagnose: Rohe MiniWebSocket-Verbindung mit Zustands-Ticks
add_executable(ws_diag test/ws_diag.cpp MiniWebSocket.cpp MiniWebSocket.h)
set_target_properties(ws_diag PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
target_include_directories(ws_diag PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ws_diag PRIVATE Qt6::Core Qt6::Network)
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(ws_diag PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR})
endforeach()

# Test 4: headless (offscreen) – lädt ChatPanel.qml + ChatNative.qml echt in der
# QML-Engine, damit QML-Ladefehler nie wieder unbemerkt ins Paket wandern
add_executable(qml_panel_test
    test/qml_panel_test.cpp test/fakehost.h
    ${CHAT_SOURCES}
    TokenVault.cpp TokenVault.h)
set_target_properties(qml_panel_test PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
target_include_directories(qml_panel_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${HOST_PLUGIN_API_DIR}")
target_link_libraries(qml_panel_test PRIVATE Qt6::Core Qt6::Network Qt6::Gui Qt6::Qml Qt6::Quick)
if(WIN32)
    target_link_libraries(qml_panel_test PRIVATE crypt32)
endif()
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(qml_panel_test PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR})
endforeach()

# Test 5: BTTV/FFZ/7TV – Parser offline, dann echte Kataloge + CDN-Bilder im echten Panel:
# misst, ob animierte Emotes im Chat wirklich laufen, und speichert Screenshots
add_executable(emote_test
    test/emote_test.cpp test/fakehost.h
    ${CHAT_SOURCES}
    TokenVault.cpp TokenVault.h)
set_target_properties(emote_test PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
target_include_directories(emote_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${HOST_PLUGIN_API_DIR}")
target_link_libraries(emote_test PRIVATE Qt6::Core Qt6::Network Qt6::Gui Qt6::Qml Qt6::Quick)
if(WIN32)
    target_link_libraries(emote_test PRIVATE crypt32)
endif()
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(emote_test PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR})
endforeach()

# Mod-Aktionen (gelöscht / Timeout / Bann / Chat geleert) im Verlauf, ohne Login
add_executable(modview_test
    test/modview_test.cpp test/fakehost.h
    ${CHAT_SOURCES}
    TokenVault.cpp TokenVault.h)
set_target_properties(modview_test PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
target_include_directories(modview_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${HOST_PLUGIN_API_DIR}")
target_link_libraries(modview_test PRIVATE Qt6::Core Qt6::Network Qt6::Gui Qt6::Qml Qt6::Quick)
if(WIN32)
    target_link_libraries(modview_test PRIVATE crypt32)
endif()
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(modview_test PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR})
endforeach()

# Test 6: Rechtschreibprüfung – Wortzerlegung und echte Wörterbücher (auch unter Umlaut-Pfad)
add_executable(spell_test test/spell_test.cpp SpellChecker.cpp SpellChecker.h GrammarRules.cpp GrammarRules.h
    LanguageToolClient.cpp LanguageToolClient.h TextPredictor.cpp TextPredictor.h)
set_target_properties(spell_test PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
target_include_directories(spell_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(spell_test PRIVATE Qt6::Core Qt6::Network)
target_compile_definitions(spell_test PRIVATE
    SPELL_DICT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/third_party/dict")
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(spell_test PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR})
endforeach()

# Test 7: Postfach – Gespräche, Gruppen-Verteiler, EventSub-Nachrichten, verschlüsselte Ablage (ohne Netz)
add_executable(whisper_test test/whisper_test.cpp WhisperService.cpp WhisperService.h TokenVault.cpp TokenVault.h
    MiniWebSocket.cpp MiniWebSocket.h)
set_target_properties(whisper_test PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
target_include_directories(whisper_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(whisper_test PRIVATE Qt6::Core Qt6::Network)
if(WIN32)
    target_link_libraries(whisper_test PRIVATE crypt32)
endif()
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(whisper_test PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR})
endforeach()

# Test 8: Benachrichtigungston – spielt pn.mp3 wirklich über MCI ab
add_executable(sound_test test/sound_test.cpp NotifySound.cpp NotifySound.h)
set_target_properties(sound_test PROPERTIES AUTOMOC OFF WIN32_EXECUTABLE FALSE)
target_include_directories(sound_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sound_test PRIVATE Qt6::Core)
target_compile_definitions(sound_test PRIVATE SOUND_FILE="${CMAKE_CURRENT_SOURCE_DIR}/sounds/pn.mp3")
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(sound_test PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR})
endforeach()

# Test 3: Konsole – prüft, ob die DLL als UVE-Player-Plugin erkannt wird
add_executable(plugin_probe test/plugin_probe.cpp)
set_target_properties(plugin_probe PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(plugin_probe PRIVATE Qt6::Core)
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(plugin_probe PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY${cfg} ${TEST_OUT_DIR})
endforeach()
