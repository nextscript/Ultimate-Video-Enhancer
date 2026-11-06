set(UVE_APP_EXTRA)
if(WIN32)
    configure_file(app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    set(UVE_APP_EXTRA ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()

qt_add_executable(uve-player WIN32 MACOSX_BUNDLE
    main.cpp
    PlayerApp.cpp PlayerApp.h
    ${UVE_APP_EXTRA}
)
set_target_properties(uve-player PROPERTIES
    OUTPUT_NAME "UVEPlayer"
    MACOSX_BUNDLE_BUNDLE_NAME "UVE Player"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.nextscript.uveplayer"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
target_compile_definitions(uve-player PRIVATE UVE_PLAYER_VERSION="${PROJECT_VERSION}")

qt_add_qml_module(uve-player
    URI UvePlayer
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/ControlBar.qml
        qml/SeekBar.qml
        qml/PlaylistPanel.qml
        qml/VideoContextMenu.qml
        qml/SettingsDialog.qml
        qml/ExpertSettingsPage.qml
        qml/PluginsDialog.qml
        qml/UpdateDialog.qml
        qml/AboutDialog.qml
        qml/OpenUrlDialog.qml
        qml/MediaInfoDialog.qml
        qml/DiagnosticsOverlay.qml
        qml/IconButton.qml
        qml/VecIcon.qml
    RESOURCES
        resources/icon.png
)

# Qt-Shader (RHI): Vorher/Nachher-Zusammensetzung → :/uve/shaders/splitview.frag.qsb
qt_add_shaders(uve-player "uve_shaders"
    PREFIX "/uve"
    FILES shaders/splitview.frag
)

target_link_libraries(uve-player PRIVATE uvecore Qt6::Quick Qt6::QuickControls2 Qt6::Qml Qt6::Network)
if(UVE_WITH_WEBENGINE)
    # QtWebEngineQuick: stellt QtWebEngineQuick::initialize() bereit; Plugin-Seiten importieren QtWebEngine
    target_link_libraries(uve-player PRIVATE Qt6::WebEngineQuick)
endif()
uve_link_mpv(uve-player)

# Laufzeitdateien neben die EXE: libmpv (Unterordner mpv\, explizit geladen), yt-dlp, 7zr (Updater)
set(UVE_BIN ${CMAKE_BINARY_DIR}/bin)
if(WIN32)
    add_custom_command(TARGET uve-player POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${UVE_BIN}/mpv ${UVE_BIN}/tools/yt-dlp
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MPV_DIR}/libmpv-2.dll ${MPV_DIR}/version.json ${UVE_BIN}/mpv/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/deps/tools/yt-dlp/yt-dlp.exe ${CMAKE_SOURCE_DIR}/deps/tools/yt-dlp/version.json ${UVE_BIN}/tools/yt-dlp/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/deps/tools/7zr.exe ${UVE_BIN}/tools/
        VERBATIM)
endif()
add_custom_command(TARGET uve-player POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${UVE_BIN}/lang
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/lang/README.md ${UVE_BIN}/lang/README.md
    VERBATIM)

# Übersetzungen: lang/uve_<code>.ts (Qt Linguist) → bin/lang/uve_<code>.qm (lrelease, bei jedem Build aktuell)
file(GLOB UVE_TS_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/lang/uve_*.ts)
set(UVE_QM_FILES)
foreach(ts ${UVE_TS_FILES})
    get_filename_component(base ${ts} NAME_WE)
    set(qm ${UVE_BIN}/lang/${base}.qm)
    add_custom_command(OUTPUT ${qm}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${UVE_BIN}/lang
        COMMAND Qt6::lrelease -silent ${ts} -qm ${qm}
        DEPENDS ${ts}
        COMMENT "lrelease ${base}.ts"
        VERBATIM)
    list(APPEND UVE_QM_FILES ${qm})
endforeach()
add_custom_target(uve_translations ALL DEPENDS ${UVE_QM_FILES})
add_dependencies(uve-player uve_translations)
