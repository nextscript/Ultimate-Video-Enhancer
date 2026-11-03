# Explorer-Thumbnail-Provider (nur Windows): Vorschaubilder für Videos, wenn UVE Player Standard-App ist.
# Ohne Qt und ohne Importbibliothek – libmpv wird zur Laufzeit aus mpv\libmpv-2.dll geladen.
configure_file(thumbnailer.rc.in ${CMAKE_CURRENT_BINARY_DIR}/thumbnailer.rc @ONLY)
add_library(uve-thumbnailer SHARED
    ThumbnailProvider.cpp
    UVEThumbnailer.def
    ${CMAKE_CURRENT_BINARY_DIR}/thumbnailer.rc)
set_target_properties(uve-thumbnailer PROPERTIES OUTPUT_NAME "UVEThumbnailer" AUTOMOC OFF
    MSVC_RUNTIME_LIBRARY "MultiThreaded")   # statische CRT: läuft in dllhost ohne Abhängigkeiten
target_include_directories(uve-thumbnailer PRIVATE ${MPV_DIR}/include)
target_link_libraries(uve-thumbnailer PRIVATE ole32 shlwapi)

# Prüfprogramm: erzeugt ein Vorschaubild über die COM-Schnittstelle (wie der Explorer)
add_executable(tst_thumbnailer tst_thumbnailer.cpp)
set_target_properties(tst_thumbnailer PROPERTIES AUTOMOC OFF)
target_link_libraries(tst_thumbnailer PRIVATE ole32 shlwapi)
add_dependencies(tst_thumbnailer uve-thumbnailer uve-player)   # uve-player kopiert mpvibmpv-2.dll nach bin
add_test(NAME tst_thumbnailer COMMAND tst_thumbnailer $<TARGET_FILE:uve-thumbnailer> ${CMAKE_SOURCE_DIR}/tests/media/test-legacy.avi)
