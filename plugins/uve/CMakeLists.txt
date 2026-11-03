# UVE-Effekt-Engine (ohne Qt-GUI) – auch von den Unit-Tests genutzt
add_library(uve_engine STATIC
    engine/UveParams.cpp engine/UveParams.h
    engine/CubeLut.cpp engine/CubeLut.h
    engine/ShaderBuilder.cpp engine/ShaderBuilder.h
    engine/LutProfile.cpp engine/LutProfile.h
    engine/MiniZip.cpp engine/MiniZip.h
    engine/AutoSceneMatch.cpp engine/AutoSceneMatch.h
    engine/ExpertSettings.cpp engine/ExpertSettings.h
    LutLibrary.cpp LutLibrary.h
)
target_include_directories(uve_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uve_engine PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent)
set_target_properties(uve_engine PROPERTIES AUTOMOC ON)
set_target_properties(uve_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Plugin-DLL (Qt-Plugin mit eingebettetem Manifest)
add_library(uve_enhancer MODULE
    UvePlugin.cpp UvePlugin.h
    UveController.cpp UveController.h
    manifest.json
)
qt_add_resources(uve_enhancer "uve_qml" PREFIX "/uve" BASE qml FILES qml/EnhancementsPanel.qml)
target_link_libraries(uve_enhancer PRIVATE uve_engine uve_plugin_api Qt6::Core Qt6::Gui Qt6::Qml)
set_target_properties(uve_enhancer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plugins
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plugins)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set_target_properties(uve_enhancer PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin/plugins
        RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin/plugins)
endforeach()

# Mitgelieferte Daten (LUT, UVE-Beispielprofile) neben die Plugins kopieren
add_custom_command(TARGET uve_enhancer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data ${CMAKE_BINARY_DIR}/bin/plugins/uve)

if(APPLE)
    set_target_properties(uve_enhancer PROPERTIES SUFFIX ".dylib")
endif()
