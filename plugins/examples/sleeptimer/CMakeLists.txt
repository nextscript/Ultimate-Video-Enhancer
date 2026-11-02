add_library(uve_sleeptimer MODULE SleepTimerPlugin.cpp SleepTimerPlugin.h manifest.json)
qt_add_resources(uve_sleeptimer "sleeptimer_qml" PREFIX "/sleeptimer" FILES SleepTimerPanel.qml)
target_link_libraries(uve_sleeptimer PRIVATE uve_plugin_api Qt6::Core Qt6::Qml)
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(uve_sleeptimer PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY${cfg} ${CMAKE_BINARY_DIR}/bin/plugins
        RUNTIME_OUTPUT_DIRECTORY${cfg} ${CMAKE_BINARY_DIR}/bin/plugins)
endforeach()

if(APPLE)
    set_target_properties(uve_sleeptimer PROPERTIES SUFFIX ".dylib")
endif()
