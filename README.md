# UVE Player Plugins

A collection of Windows plugins (Qt/C++ DLL + QML UI) for the **UVE Player**, an mpv-based video player.
Install each ZIP via *Settings → Plugins → Install plugin…* and restart the player.

| Plugin | Version | Type | What it does |
|---|---|---|---|
| **GLSL Manager** | 1.0.1 | GPU effect | Runs the GLSL "Custom Filter Codes" from the *Ultimate Video Enhancer* userscript as mpv shaders. Create, edit, test, reorder, import/export filters; auto-generated sliders for uniforms; also runs mpv hook shaders like Anime4K. Toggle all: `Ctrl+Alt+Shift+G`. |
| **LumaScale** | 1.0.1 | GPU effect | AMD FidelityFX Super Resolution (EASU upscale + RCAS sharpen) applied to the luma plane, with all parameters in its own sidebar tab. |
| **Twitch Chat** | 2.2.1 | Utility | Native Twitch chat in the sidebar (IRC over WebSocket). Twitch/BTTV/FFZ/7TV emotes (incl. animated & zero-width), emote picker, spell & grammar check (DE/EN), mentions, AutoMod, moderation. Auto-detects the channel. `Ctrl+Alt+T`. |
| **Kick Chat** | 1.3.2 | Utility | Same idea for kick.com: read without login, write with Kick OAuth. Kick + 7TV emotes, badges, replies, sub/gift events, pinned messages, viewer count, moderation, spell check. Auto-detects the channel. `Ctrl+Alt+K`. |
## Other files

- `gvf_filter_codes_2026-11-09_01-08.json` – exported filter set in the userscript format (Matrix, FSR 1.0 Luma Upscale, Game Boy, Auto Color Enhance), importable into GLSL Manager.

## Requirements

Windows, UVE Player ≥ 1.0.6 (GLSL Manager ≥ 1.1.5). All plugins are MIT-licensed.
