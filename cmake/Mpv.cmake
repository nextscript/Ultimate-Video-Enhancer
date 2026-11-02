# libmpv (Client-API 2.x) einbinden.
# Erwartet: ${MPV_DIR}/include/mpv/client.h, ${MPV_DIR}/mpv.lib (MSVC-Importbibliothek), ${MPV_DIR}/libmpv-2.dll
# Die Importbibliothek wird aus den echten DLL-Exporten erzeugt (scripts/make-mpv-implib.ps1).

if(WIN32)
    set(MPV_DIR "${CMAKE_SOURCE_DIR}/deps/mpv" CACHE PATH "Ordner mit libmpv-Entwicklerdateien")

    if(NOT EXISTS "${MPV_DIR}/include/mpv/client.h")
        message(FATAL_ERROR "libmpv-Header fehlen unter ${MPV_DIR}/include – siehe docs/BUILD.md")
    endif()

    add_library(mpv::mpv SHARED IMPORTED GLOBAL)
    set_target_properties(mpv::mpv PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${MPV_DIR}/include"
        IMPORTED_IMPLIB "${MPV_DIR}/mpv.lib"
        IMPORTED_LOCATION "${MPV_DIR}/libmpv-2.dll")
else()
    # Linux/macOS: System-libmpv (apt: libmpv-dev, Homebrew: mpv) über pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
    add_library(mpv::mpv INTERFACE IMPORTED GLOBAL)
    target_link_libraries(mpv::mpv INTERFACE PkgConfig::MPV)
    set(MPV_DIR "" CACHE PATH "unbenutzt außerhalb von Windows")
endif()

# libmpv wird verzögert geladen (/DELAYLOAD): main.cpp lädt vorher die neueste vorhandene
# libmpv-2.dll (Update im Benutzerordner oder mitgelieferte Version) explizit und kann bei
# Fehlern (z. B. fehlende vulkan-1.dll) eine verständliche Meldung zeigen statt abzustürzen.
function(uve_link_mpv target)
    target_link_libraries(${target} PRIVATE mpv::mpv)
    if(MSVC)
        target_link_options(${target} PRIVATE "/DELAYLOAD:libmpv-2.dll")
        target_link_libraries(${target} PRIVATE delayimp)
    endif()
endfunction()
