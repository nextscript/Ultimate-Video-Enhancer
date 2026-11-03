# Kernbibliothek: Core, libmpv-Adapter, Plugin-Host, Rendering, Dienste
add_library(uvecore STATIC
    core/AppSettings.cpp core/AppSettings.h
    core/Logger.cpp core/Logger.h
    core/SafeMode.cpp core/SafeMode.h
    core/Translation.cpp core/Translation.h
    player/MpvController.cpp player/MpvController.h
    player/MpvVideoItem.cpp player/MpvVideoItem.h
    plugins/PluginApi.h
    plugins/PluginManifest.cpp plugins/PluginManifest.h
    plugins/PluginManager.cpp plugins/PluginManager.h
    plugins/PluginBridge.cpp plugins/PluginBridge.h
    ../plugins/uve/engine/MiniZip.cpp ../plugins/uve/engine/MiniZip.h   # ZIP-Pakete für „Plugin installieren“
    rendering/ShaderChain.cpp rendering/ShaderChain.h
    services/MediaFiles.cpp services/MediaFiles.h
    services/PlaylistModel.cpp services/PlaylistModel.h
    services/HotkeyModel.cpp services/HotkeyModel.h
    services/ToolUpdater.cpp services/ToolUpdater.h
    services/SingleInstance.cpp services/SingleInstance.h
    services/ProxyConfig.cpp services/ProxyConfig.h
    services/ProxyBridge.cpp services/ProxyBridge.h
)
target_include_directories(uvecore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uvecore PUBLIC
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Network Qt6::OpenGL Qt6::Concurrent mpv::mpv)

# Öffentlicher Plugin-Header als eigene Schnittstellen-Bibliothek (für Plugins ohne Host-Code)
add_library(uve_plugin_api INTERFACE)
target_include_directories(uve_plugin_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/plugins)
target_link_libraries(uve_plugin_api INTERFACE Qt6::Core Qt6::Gui)
