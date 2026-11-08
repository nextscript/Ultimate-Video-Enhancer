function(uve_add_test name)
    cmake_parse_arguments(T "" "" "SOURCES;LIBS" ${ARGN})
    qt_add_executable(${name} ${T_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${T_LIBS})
    target_compile_definitions(${name} PRIVATE
        UVE_TEST_MEDIA="${CMAKE_CURRENT_SOURCE_DIR}/media"
        UVE_TEST_DATA="${CMAKE_SOURCE_DIR}/plugins/uve/data"
        UVE_TEST_REF="${CMAKE_CURRENT_SOURCE_DIR}/reference/uve_reference.json"
        UVE_TEST_REF_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference"
        UVE_LANG_DIR="${CMAKE_SOURCE_DIR}/lang"
        UVE_QM_DIR="${CMAKE_BINARY_DIR}/bin/lang"
        UVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        UVE_LEGACY_SCRIPT="${CMAKE_SOURCE_DIR}/legacy-electron/plugins/uve/uve.user.js")
    add_test(NAME ${name} COMMAND ${name})
    # libmpv-2.dll neben die Test-EXE (Tests laden libmpv direkt, ohne Verzögerung)
    if(WIN32)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "PATH=${MPV_DIR}\;$<TARGET_FILE_DIR:Qt6::Core>\;$ENV{PATH}")
    else()
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endfunction()

uve_add_test(tst_manifest SOURCES unit/tst_manifest.cpp LIBS uvecore)
uve_add_test(tst_plugins SOURCES unit/tst_plugins.cpp LIBS uvecore)
uve_add_test(tst_playlist SOURCES unit/tst_playlist.cpp LIBS uvecore)
uve_add_test(tst_hotkeys SOURCES unit/tst_hotkeys.cpp LIBS uvecore)
uve_add_test(tst_proxy SOURCES unit/tst_proxy.cpp LIBS uvecore)
uve_add_test(tst_toolupdater SOURCES unit/tst_toolupdater.cpp LIBS uvecore)
if(TARGET uve-player)
    add_dependencies(tst_toolupdater uve-player)   # kopiert tools/7zr.exe nach bin
endif()
uve_add_test(tst_appupdater SOURCES unit/tst_appupdater.cpp LIBS uvecore)
uve_add_test(tst_i18n SOURCES unit/tst_i18n.cpp LIBS uvecore Qt6::Xml)
add_dependencies(tst_i18n uve_translations)
uve_add_test(tst_uve SOURCES unit/tst_uve.cpp LIBS uve_engine)
uve_add_test(tst_mpv_integration SOURCES integration/tst_mpv_integration.cpp LIBS uvecore uve_engine)
