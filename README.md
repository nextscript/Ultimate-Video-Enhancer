# UVE Player Plugins

A collection of Windows plugins (Qt/C++ DLL + QML UI) for the **UVE Player**, an mpv-based video player.
Install each ZIP via *Settings → Plugins → Install plugin…* and restart the player.

| Plugin | Version | Type | What it does |
|---|---|---|---|
| **GLSL Manager** | 1.0.1 | GPU effect | Runs the GLSL "Custom Filter Codes" from the *Ultimate Video Enhancer* userscript as mpv shaders. Create, edit, test, reorder, import/export filters; auto-generated sliders for uniforms; also runs mpv hook shaders like Anime4K. Toggle all: `Ctrl+Alt+Shift+G`. |
| **LumaScale** | 1.0.1 | GPU effect | AMD FidelityFX Super Resolution (EASU upscale + RCAS sharpen) applied to the luma plane, with all parameters in its own sidebar tab. |
| **Twitch Chat** | 2.2.8 | Utility | Native Twitch chat in the sidebar (IRC over WebSocket): see everything without login, sign in only to write. Twitch/BTTV/FFZ/7TV emotes (incl. animated & zero-width), emote picker, spell & grammar check (DE/EN), mentions, AutoMod, moderation. Auto-detects the channel (also when a stream proxy only hands the player a tcp:// or playlist address). *Live on Twitch* window (📺): followed, popular, categories, search – click plays the channel and the chat follows. `Ctrl+Alt+T`. [Details](#twitch-chat) |
| **Kick Chat** | 1.3.2 | Utility | Same idea for kick.com: read without login, write with Kick OAuth. Kick + 7TV emotes, badges, replies, sub/gift events, pinned messages, viewer count, moderation, spell check. Auto-detects the channel. `Ctrl+Alt+K`. |
| **UVETikTok** | 1.0.2 | Utility | Plays TikTok videos, TikTok LIVE streams and whole profiles as a playlist directly in the player – nothing is saved to disk. [Details](#uvetiktok) |
| **UVESpotify** | 1.0.4 | Utility | Plays Spotify tracks, albums and playlists via the matching track on YouTube, without ads. Sign in to play your own playlists and liked songs. [Details](#uvespotify) |
| **UVESuno** | 1.0.1 | Utility | Plays songs, playlists and whole profiles from suno.com as a stream, with titles, lengths and gapless transitions. [Details](#uvesuno) |
| **UVESponsorSkip** | 1.0.2 | Utility | Skips sponsors, intros, outros and more in YouTube videos using the SponsorBlock database – automatically or by key, with *Undo skip*. `Ctrl+Alt+S` / `Ctrl+Alt+Z`. [Details](#uvesponsorskip) |

## Twitch Chat

Without login (since 2.2.7) you see everything: chat, emotes, badges, sub/raid/bits/bot notices, pinned message, polls and
predictions, viewer count, account age in the user menu, and moderator actions – *message from X deleted*, *X timed out for
10 min*, *X banned*, *chat cleared*. Affected messages stay readable: struck through in a grey box labelled *deleted*,
*Timeout* or *banned*. Signing in is only needed to write and for account-bound things (sending, whispers, subscriber emotes,
*Following*, blocking, moderating). AutoMod boxes and new followers are only sent by Twitch to moderators and the streamer.

Live on Twitch (since 2.2.2): the 📺 icon next to the viewer list (👥) opens a window over the picture (UVE Player ≥ 1.1.3).

- **Tabs:** *Following* (your channels that are live – needs the sign-in in the chat), *Popular* (most watched live channels),
  *Categories* (games/categories with cover art and viewer count – a click shows their live channels) and
  *Search* (channels and matching categories). Popular, Categories and Search work without signing in.
- **Cards:** preview picture, profile picture, name, title, category (a click on it opens the category) and viewer count; the
  channel that is playing is marked.
- **A click on a card** plays the channel through the player's normal playback (quality menu included), the chat switches to the
  same channel and the window closes.
- Modern look with rounded cards; colours follow the player's light or dark theme. Hovering lifts the card and shows a ▶ button.
- Search shows results while you type. Drag the grip in the bottom-right corner to resize the window; the size is remembered.
- Two drop-down menus: ⇅ sort (*Recommended for you* or *Viewers (high → low)*, in every tab including the category list, except
  *Following*, which is always sorted by viewers) and
  🌐 language (all 35 languages Twitch can filter by, independent of the player language; not in *Following*, which always
  shows every followed channel that is live, and not for the category list itself). Both are remembered.
- In *Categories*, *Recommended for you* puts your most-clicked categories first (by number of clicks, even ones outside
  Twitch's top 30), then categories you watched channels in, then the viewer count. A badge on the cover shows why
  (*opened 2×*, *watched*, *followed live*).
- *Recommended for you* is the plugin's own recommendation: Twitch only personalises with its own website sign-in and the official
  API has no recommendations. Channels you follow (when signed in), channels you opened in this window and their categories come
  first, then the viewer count; *Popular* also mixes in live channels from your most-watched categories. Only channel and category
  names with a count are kept, locally in the plugin settings.

## UVETikTok

Open a TikTok link (*File → Open network stream…* or drag it onto the player) and it plays as a stream.

- **Supported links:** videos (`tiktok.com/@name/video/…`), short links (`vm.tiktok.com/…`, `vt.tiktok.com/…`, `tiktok.com/t/…`),
  `m.tiktok.com/v/…` and embed links, LIVE (`tiktok.com/@name/live`) and profiles (`tiktok.com/@name`).
- **Profiles as playlist:** a profile that is live right now plays its LIVE. Otherwise all of the user's videos (newest first, up to
  500, photo slideshows skipped) go into the playlist with titles and lengths (UVE Player ≥ 1.2.1); the first video starts at once,
  every further one is resolved fresh when it plays.
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

## UVESponsorSkip

Play a YouTube video and UVESponsorSkip looks up its segments in the community database of
[SponsorBlock](https://sponsor.ajay.app) – sponsors, intros, outros and more are skipped as you choose.

- **Per category:** *Automatic* (jump over it right away), *By key* (the OSD shows a hint, `Ctrl+Alt+S` skips) or *Ignore*.
  Defaults: sponsors automatic; self promotion, interaction reminders, intros and outros by key; preview/recap, non-music
  sections in music videos and filler ignored. Segments marked as *mute* in an *Automatic* category switch the sound off instead and back on afterwards.
- **Timeline markers:** every segment appears as a coloured bar in the player's seek bar, in the colour of its category (the same
  colours as on the settings page). Ignored categories are not marked; changes to the settings show up right away.
- **Undo skip:** `Ctrl+Alt+Z` (or the button) jumps back to where the skip happened and plays the section; it is not skipped
  automatically again in this video (`Ctrl+Alt+S` still works). Overlapping or adjacent segments are skipped in one jump.
- `Ctrl+Alt+Shift+S` switches the plugin on and off; all three keys can be changed under *Settings → Shortcuts* and are also in the
  *Plugins* menu.
- **Works with** `youtube.com/watch`, `youtu.be`, Shorts, `/live/`, embed and `music.youtube.com` links, also inside playlists.
- **Privacy:** only the first 4 characters of the SHA-256 hash of the video ID are sent, never the ID itself. Optionally (on by
  default) skipped segments are counted anonymously, like the browser extension does.
- Own settings page *Settings → UVESponsorSkip* (also via the button on the plugin card in *Settings → Plugins*) with the
  categories, the segments of the current video – click to jump to one – and skip/undo buttons.
- Segment data: SponsorBlock, CC BY-NC-SA 4.0.

## Other files

- `gvf_filter_codes_2026-11-09_01-08.json` – exported filter set in the userscript format (Matrix, FSR 1.0 Luma Upscale, Game Boy, Auto Color Enhance), importable into GLSL Manager.

## Requirements

Windows, UVE Player ≥ 1.0.6 (GLSL Manager ≥ 1.1.5, UVETikTok, UVESpotify, UVESuno and UVESponsorSkip ≥ 1.1.6; titles and lengths for Spotify, Suno and TikTok playlists ≥ 1.2.1).
All plugins are MIT-licensed.
