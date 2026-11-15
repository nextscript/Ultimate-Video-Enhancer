# UVE Player Plugins

A collection of Windows plugins (Qt/C++ DLL + QML UI) for the **UVE Player**, an mpv-based video player.
Install each ZIP via *Settings → Plugins → Install plugin…* and restart the player.

| Plugin | Version | Type | What it does |
|---|---|---|---|
| **GLSL Manager** | 1.0.1 | GPU effect | Runs the GLSL "Custom Filter Codes" from the *Ultimate Video Enhancer* userscript as mpv shaders. Create, edit, test, reorder, import/export filters; auto-generated sliders for uniforms; also runs mpv hook shaders like Anime4K. Toggle all: `Ctrl+Alt+Shift+G`. |
| **LumaScale** | 1.0.1 | GPU effect | AMD FidelityFX Super Resolution (EASU upscale + RCAS sharpen) applied to the luma plane, with all parameters in its own sidebar tab. |
| **Twitch Chat** | 2.2.1 | Utility | Native Twitch chat in the sidebar (IRC over WebSocket). Twitch/BTTV/FFZ/7TV emotes (incl. animated & zero-width), emote picker, spell & grammar check (DE/EN), mentions, AutoMod, moderation. Auto-detects the channel. `Ctrl+Alt+T`. |
| **Kick Chat** | 1.3.2 | Utility | Same idea for kick.com: read without login, write with Kick OAuth. Kick + 7TV emotes, badges, replies, sub/gift events, pinned messages, viewer count, moderation, spell check. Auto-detects the channel. `Ctrl+Alt+K`. |
| **UVETikTok** | 1.0.0 | Utility | Plays TikTok videos and TikTok LIVE streams directly in the player – nothing is saved to disk. [Details](#uvetiktok) |
| **UVESpotify** | 1.0.4 | Utility | Plays Spotify tracks, albums and playlists via the matching track on YouTube, without ads. Sign in to play your own playlists and liked songs. [Details](#uvespotify) |
| **UVESuno** | 1.0.1 | Utility | Plays songs, playlists and whole profiles from suno.com as a stream, with titles, lengths and gapless transitions. [Details](#uvesuno) |

## UVETikTok

Open a TikTok link (*File → Open network stream…* or drag it onto the player) and it plays as a stream.

- **Supported links:** videos (`tiktok.com/@name/video/…`), short links (`vm.tiktok.com/…`, `vt.tiktok.com/…`, `tiktok.com/t/…`),
  `m.tiktok.com/v/…` and embed links, LIVE (`tiktok.com/@name/live`) and profiles – a profile that is live right now plays its LIVE.
- **Quality:** the highest variant up to the limit you choose (*Best*, 1080p, 720p, 540p, 360p). TikTok's higher qualities are
  picture-only; the plugin adds the separate audio track automatically, so you always get sound.
- **LIVE format:** FLV (low latency, default) or HLS (more stable).
- Private or deleted videos and channels that are not live show a clear message; anything else unknown falls back to yt-dlp.
- Sidebar page *UVETikTok* (quality, LIVE format, status) is hidden by default; show it under *Settings → Plugins*.

## UVESpotify

Spotify audio is copy-protected (DRM) and cannot be played by other players. UVESpotify reads the track name, artist and length from
Spotify and plays the same song from YouTube – **without ads**, with or without a Spotify subscription.

- **Supported links:** tracks, albums, playlists and artists on `open.spotify.com` (also `intl-xx/…` links). Playlists and albums go
  into the playlist completely; the first track starts right away.
- **Playback:** *Audio only* (default) picks the version whose length matches Spotify (no intros or remixes); *Music video* prefers the
  official video.
- **Gapless playback:** while a track plays, the next one in the playlist is already looked up and resolved to its stream. With UVE
  Player ≥ 1.2.2 the player opens it in advance and switches over without any pause; older players start it in about half a second.
- **Spotify account (optional):** click *Sign in* in the sidebar page, log in to Spotify in your browser – done. Your playlists and
  *Liked Songs* appear as cards with covers; a click replaces the current playlist with all tracks, including titles and lengths
  (UVE Player ≥ 1.2.1). Signed in, playlists with more than 100 tracks are read completely. The sign-in is stored encrypted for your
  Windows account and renews itself.
- The built-in Spotify app is in development mode: only accounts the plugin author has added in the Spotify developer dashboard
  (*User Management*, max. 25) can sign in. Links work for everyone without signing in.
- Sidebar page *UVESpotify* is shown by default.

## UVESuno

Open a link from suno.com (*File → Open network stream…* or drag it onto the player) and it plays right away.

- **Supported links:** songs (`suno.com/song/…`), playlists (`suno.com/playlist/…`), profiles (`suno.com/@name` – all songs of the
  creator, up to 500) and short links (`suno.com/s/…`).
- Playlists and profiles go into the playlist completely, with titles and lengths (UVE Player ≥ 1.2.1); the first song starts at once.
- **Gapless:** with UVE Player ≥ 1.2.2 the next song is prepared in advance and follows without a pause.
- Plays what Suno offers openly: the MP3 when the creator allows downloads, otherwise the audio of Suno's official MP4 for sharing.
  Songs that are private, deleted or not finished show a clear message.
- **Picture:** always the song's cover (1024×1024), never Suno's own video – its burned-in text shows Greek, Korean, Cyrillic and
  other non-Latin titles and lyrics as empty boxes. Titles in the player itself are shown correctly in every language.
- Sidebar page *UVESuno* (status) is hidden by default; show it under *Settings → Plugins*.

## Other files

- `gvf_filter_codes_2026-11-09_01-08.json` – exported filter set in the userscript format (Matrix, FSR 1.0 Luma Upscale, Game Boy, Auto Color Enhance), importable into GLSL Manager.

## Requirements

Windows, UVE Player ≥ 1.0.6 (GLSL Manager ≥ 1.1.5, UVETikTok, UVESpotify and UVESuno ≥ 1.1.6; titles and lengths for Spotify playlists ≥ 1.2.1).
All plugins are MIT-licensed.
