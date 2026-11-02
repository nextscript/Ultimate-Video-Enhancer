# Import-/Exporttabellen (ergänzend mit `pefile`, da kein REA-Deep-Provider verfügbar)

## vlc.exe (VLC 3.0.24)
- Export: nur `WinMain`
- Import aus `libvlc.dll` (7): `libvlc_new`, `libvlc_set_app_id`, `libvlc_set_user_agent`, `libvlc_add_intf`,
  `libvlc_playlist_play`, `libvlc_wait`, `libvlc_release`
- Weitere: KERNEL32 (64), msvcrt (80), WININET (11, Update-Prüfung), ADVAPI32 (5), SHELL32 (`CommandLineToArgvW`,
  `SHGetFolderPathW`), USER32 (`MessageBoxW`)
- Versionsinfo: FileVersion 3.0.24, ProductName „VLC media player“

## libvlc.dll
- 316 Exporte (`libvlc_media_*`, `libvlc_audio_*`, `libvlc_video_*`, Equalizer, …)
- 131 Importe aus `libvlccore.dll` (input_*, aout_*, config_*, playlist)

## plugins\gui\libqt_plugin.dll / plugins\video_output\libdirect3d11_plugin.dll
- Je 3 Exporte: `vlc_entry__3_0_0f`, `vlc_entry_copyright__3_0_0f`, `vlc_entry_license__3_0_0f`
- Qt-GUI: 172 Importe aus libvlccore (playlist, var_*, config_*, addons_manager_*)
- VLC-Modulkategorien (Ordner `plugins\`): access 37, codec 56, demux 47, video_filter 41, audio_filter 25,
  stream_out 20, video_chroma 19, video_output 15, packetizer 14, … (Index-Cache `plugins.dat`)

## libmpv-2.dll (mpv-Build 20261102)
- 206 Exporte; mpv-API vollständig (client + render), daneben eingebettete Bibliotheken (opus_*, sixel_*, libbluray-JNI)
- Importe u. a.: OPENGL32, d2d1, DWrite, **vulkan-1.dll**, WS2_32, CRYPT32, AVRT, api-ms-win-crt-*
- Erzeugte MSVC-Importbibliothek: `deps\mpv\mpv.lib` aus `mpv.def` (Exportliste wie oben)
