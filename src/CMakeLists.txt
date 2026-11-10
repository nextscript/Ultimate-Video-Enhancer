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
    rendering/VideoFilterStage.cpp rendering/VideoFilterStage.h
    services/MediaFiles.cpp services/MediaFiles.h
    services/PlaylistModel.cpp services/PlaylistModel.h
    services/HotkeyModel.cpp services/HotkeyModel.h
    services/ToolUpdater.cpp services/ToolUpdater.h
    services/AppUpdater.cpp services/AppUpdater.h
    services/ResumableDownload.cpp services/ResumableDownload.h
    services/SingleInstance.cpp services/SingleInstance.h
    services/ProxyConfig.cpp services/ProxyConfig.h
    services/ProxyBridge.cpp services/ProxyBridge.h
    # Optische Medien (1.1.8): DVD-Video, Blu-ray, Audio-CD – Quellen, Fähigkeiten, Navigation; Wiedergabe über libmpv
    services/optical/OpticalMediaTypes.h
    services/optical/DiscCapabilities.cpp services/optical/DiscCapabilities.h
    services/optical/DiscSourceResolver.cpp services/optical/DiscSourceResolver.h
    services/optical/DiscNavigationBridge.cpp services/optical/DiscNavigationBridge.h
    services/optical/OpticalDriveEnumerator.cpp services/optical/OpticalDriveEnumerator.h
    services/optical/CdAudioStream.cpp services/optical/CdAudioStream.h
    services/optical/OpticalMediaService.cpp services/optical/OpticalMediaService.h
)
target_include_directories(uvecore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# BD-J (Blu-ray-Java-Menüs) nur mit ausdrücklich mitgelieferter Java-Laufzeit und libbluray-j2se-*.jar
option(UVE_WITH_BDJ "BD-J-Laufzeit für Blu-ray-Java-Menüs erwarten (Java + libbluray-j2se-*.jar im Paket)" OFF)
if(UVE_WITH_BDJ)
    target_compile_definitions(uvecore PUBLIC UVE_WITH_BDJ=1)
endif()
target_link_libraries(uvecore PUBLIC
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Network Qt6::OpenGL Qt6::Concurrent mpv::mpv)

# Öffentlicher Plugin-Header als eigene Schnittstellen-Bibliothek (für Plugins ohne Host-Code)
add_library(uve_plugin_api INTERFACE)
target_include_directories(uve_plugin_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/plugins)
target_link_libraries(uve_plugin_api INTERFACE Qt6::Core Qt6::Gui)
