# Plugin API (version 1)

Header: `src/plugins/PluginApi.h`. Goal since **1.0.6**: new plugins extend the player **without the player having to be
updated**. There are three building blocks that are extended by name instead of by new functions:

| Building block | Direction | How |
|---|---|---|
| **Events** | player → plugin | `IPluginEvents::onEvent(name, data)` (native) or `plugin.onEvent(name, data)` (QML) |
| **Commands** | plugin → player | `host->call(pluginId, name, args, &error)` (native) or `plugin.call(name, args)` (QML) |
| **Contributions** | manifest → player | `contributes`: pages, menu entries, hotkeys, translation files, URL handlers |

New events and commands are simply added in later versions. The interface (and therefore `apiVersion 1`) does not
change, existing plugins keep working. To check whether the running player knows a command: `hasCommand(name)`.

## Plugin forms

### 1. Plugin folder (recommended)

```
plugins/<name>/
    manifest.json          required
    <library>.dll          native logic ("library")      – or –   Main.qml  pure QML logic ("main")
    bin/<target>/<lib>     native logic per system ("libraries", since 1.2.9)
    lang/<name>_<code>.qm  own translations (e.g. example_en.qm)
    *.qml, images …        pages and resources
```

Plugins are searched in the user folder (`%APPDATA%\nextscript\UVE Player\plugins`, takes precedence) and in the program
folder (`<program>\plugins`). The manifest in the folder applies even if the library has an embedded manifest – pages,
menus or translations can be changed without recompiling.

**Installing:** *Settings → Plugins* → *Install plugin …* → choose a `.zip` (with `manifest.json` at the root or in exactly one
subfolder) or the `manifest.json` of a folder. The package is validated (manifest, paths) and moved to `plugins/<id>/` on
the **next start** (loaded DLLs are locked on Windows).

**Removing (since 1.1.3):** *Settings → Plugins* → *Remove*. Plugins from the user folder are disabled immediately and deleted at the
**next start**; until then the removal can be undone. Their settings and data are only deleted when the user selects that
option. Bundled plugins (program folder) can only be disabled. If a user plugin replaced a bundled one with the same id, the bundled
one is active again after the removal.

### 2. Pure QML plugin (no compiler needed)

`"main": "Main.qml"` – a non-visual QML object. Optional: `property var plugin` (the bridge, see below) and the functions
`activate()`, `suspend()`, `deactivate()`.

```qml
import QtQml
QtObject {
    id: root
    property var plugin
    property Connections events: Connections {
        target: root.plugin
        function onEvent(name, data) {
            if (name === "media.loaded") root.plugin.call("osd.show", { text: "Playing: " + data.title })
        }
    }
}
```

Network access in QML: `XMLHttpRequest`.

### 3. Single library (previous format)

A DLL directly in `plugins/` with an embedded manifest (`Q_PLUGIN_METADATA(... FILE "manifest.json")`). Still works;
contributions (`contributes`) can also be in the embedded manifest, but translation files and pages from files only work
in a plugin folder.

## Native plugins (C++)

```cpp
class MyPlugin : public QObject, public uve::IPlayerPlugin, public uve::IPluginEvents {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID UVE_PLAYER_PLUGIN_IID FILE "manifest.json")
    Q_INTERFACES(uve::IPlayerPlugin uve::IPluginEvents)
public:
    bool initialize(uve::IPlayerHost *host, QString *error) override;
    void activate() override; void suspend() override; void deactivate() override;
    QObject *controller() override;    // object for the pages (property "controller")
    QUrl panelQml() const override;     // optional, otherwise empty → pages via contributes.panels
    QString panelTitle() const override;
    void onEvent(const QString &name, const QVariantMap &data) override;   // IPluginEvents (optional)
};
```

Same compiler and Qt version as the player (Qt 6.12; Windows: MSVC 2022 x64, Linux: GCC on Ubuntu 24.04 x86-64,
macOS: Apple clang, ARM64).

## Manifest

```json
{
  "id": "org.example.plugin", "name": "…", "version": "1.0.0", "apiVersion": 1, "minHostVersion": "1.0.6",
  "type": "utility", "description": "…", "author": "…", "license": "…",
  "library": "example.dll",
  "main": "Main.qml",
  "supportedPlatforms": ["windows", "linux", "macos"], "requiredBackends": [], "permissions": [],
  "contributes": {
    "panels":   [ { "id": "main", "title": { "de": "Beispiel", "en": "Example" }, "qml": "Panel.qml", "location": "sidebar" },
                  { "id": "settings", "title": "Settings", "qml": "Settings.qml", "location": "settings" },
                  { "id": "dialog", "title": "Dialog", "qml": "Dialog.qml", "location": "modal",
                    "modal": { "scope": "player", "mode": "movable", "width": 460, "height": 260 } } ],
    "menu":     [ { "id": "doit", "title": { "de": "Ausführen", "en": "Run" } } ],
    "hotkeys":  [ { "id": "doit", "title": "Run", "default": "Ctrl+Alt+B" } ],
    "translations": "lang",
    "urlHandlers": [ "^https?://(www\\.)?example\\.com/" ]
  }
}
```

`library`, `libraries` **or** `main` (plugin folders only).
All paths are relative to the plugin folder and must stay inside it (no absolute paths, no `..`). Texts in the manifest: a string or `{ "<language>": "…" }`
(fallback English, then German). This also applies to `description` (since 1.2.0), shown on the plugin card in *Settings → Plugins*. Available backends: `mpv-user-shaders`, `mpv-render-api-opengl`,
`plugin-events`, `plugin-commands`, `qml-plugins`.

| Contribution | Effect |
|---|---|
| `panels` | `location: "sidebar"` → tab in the sidebar, which the user can hide or move to *Settings* (since 1.1.5); `"settings"` → own entry in *Settings* (since 1.1.3) and a button on the card of the plugin in *Settings → Plugins*; `"modal"` → window opened with `modal.open` (since 1.1.3). `defaultPlacement: "hidden"` or `"settings"` starts a sidebar page there until the user chooses a place (since 1.1.6). `qml` relative to the folder or `qrc:/…` |
| `menu` | entry in the *Plugins* menu → event `action.triggered {id}` |
| `hotkeys` | entry in *Settings → Shortcuts* (editable) → event `action.triggered {id}`. A default shortcut that is already taken is not applied |
| `translations` | folder with `*_<code>.qm` (default `lang`), switched together with the player language; source language German, fallback English |
| `urlHandlers` | regular expressions; matching addresses go to the plugin before loading (event `media.resolve`) |

Pages receive the properties `theme`, `host` (player object), `controller` (plugin object or QML logic) and `plugin`
(the bridge) – each only if the page declares it. Settings pages can scroll: give the page an `implicitHeight`. A sidebar page can
end up in *Settings*, so it should adapt to the available width; it is only loaded while it is shown there.

## Modal windows (since 1.1.3)

`plugin.call("modal.open", { panel: "dialog", scope: "video", mode: "movable", data: { … } })` opens one of the plugin's own
pages in a window with a title bar and a close button. Defaults come from `"modal"` in the manifest; arguments take precedence.
Opening the same page again updates the open window. *Esc* and the close button always work, so a window cannot trap the user.

| Option | Values |
|---|---|
| `scope` | `player` (default): over the whole player including the menu bar, blocks the player. `video`: limited to the picture, without the control bar. `sidebar`: limited to the sidebar (in fullscreen, where it is hidden: the picture). With `video` and `sidebar` the rest of the player stays usable |
| `mode` | `movable` (default): drag the title bar, it stays inside the scope. `fixed`: centred, not movable. `fill`: fills the scope |
| `width`, `height` | size in pixels (default: 720 wide, height of the page or 480); never larger than the scope |
| `maxWidth`, `maxHeight` | upper limit in pixels, also for `fill` |
| `title` | replaces the title of the panel |
| `closeOnOutsideClick` | `true`: a click next to the window closes it (default `false`) |
| `data` | any value for the page, available as `modal.data` |

The page can declare `property var modal`: `modal.data`, `modal.scope`, `modal.mode`, `modal.backgroundAlpha`, `modal.close(result)`.
`modal.backgroundAlpha` (since 1.3.0) is the opacity of the window background from *Settings → Interface* (1 = opaque); pages
with their own background should use it so the window looks evenly transparent. When the window closes,
the plugin receives `modal.closed`. Windows close automatically when the plugin is disabled or removed.
Complete example: `plugins/examples/modaldemo/` (pure QML, copy the folder to the user plugin folder).

## Bridge (`plugin` in QML)

`plugin.call(name, args)`, `plugin.hasCommand(name)`, `plugin.commands()`, `plugin.settings()`,
`plugin.saveSettings(obj)`, `plugin.log(text, level)`, `plugin.osd(text, ms)`, `plugin.lastError()`,
`plugin.resolve("image.png")` (file in the plugin folder as a URL), properties `id`, `folder`, `dataDir`, `language`,
signal `event(name, data)`.

## Events

| Name | Data |
|---|---|
| `media.loading` | `url` – the user chose a medium (before any resolution) |
| `media.resolve` | `requestId`, `url`, optionally `quality` – only to the plugin whose `urlHandlers` match; answer with `media.resolved`. A plugin with the capability `stream-quality` keeps the player's *Resolution* menu for YouTube/Twitch addresses: `quality` is sent when the user chose an entry (the yt-dlp `format_id`, e.g. `1080p60__source_`, or `best`) and the address is then resolved again |
| `media.resolve` with `preload: true` | since 1.2.2, only to plugins with the capability `media-preload`: resolve the **next** playlist entry in advance while the current one plays. Answer with `media.resolved` as usual (optionally with `title`), but do not change the running playback (no `mpv.set`). The player hands the result to mpv ahead of time and switches over without a gap |
| `media.resolveCancelled` | `requestId` – the result is no longer needed |
| `media.loaded` | `path` (loaded), `source` (chosen), `title`, `duration` |
| `media.ended` | `reason` (`eof`, `stop`, `error`, …), `path`, `source`, `error` |
| `player.stopped` | `path` |
| `playback.paused` | `paused` |
| `language.changed` | `code` |
| `action.triggered` | `id` – menu entry or hotkey of this plugin |
| `modal.closed` | `panel`, `result` (value of `modal.close(result)`, otherwise empty) – since 1.1.3 |
| `seekbar.seek` | `behind` (seconds behind the live edge) – since 1.2.5, only to the plugin that last sent `seekbar.live`: the user clicked or dragged on the timeline |
| `seekbar.live` | – since 1.2.5, only to that plugin: the user clicked the LIVE badge at the end of the timeline |
| `app.quit` | – program exit |

Ignore unknown events – later versions may send more.

## Commands

| Name | Arguments → result |
|---|---|
| `commands` | → list of all commands |
| `app.info` | → `version`, `apiVersion`, `language`, `platform`, `backends` |
| `osd.show` | `text`, `ms` |
| `log` | `message`, `level` |
| `settings.get` / `settings.set` | → plugin settings / `settings` (object) |
| `plugin.dataDir` | → data folder of the plugin |
| `panel.open` | `panel` (id from `contributes.panels`) – show the page in the sidebar or in *Settings*. If the user hid the page, it stays hidden and the player shows a hint (since 1.1.5) |
| `modal.open` | `panel`, optional `scope`, `mode`, `width`, `height`, `maxWidth`, `maxHeight`, `title`, `closeOnOutsideClick`, `data` – since 1.1.3, see *Modal windows* |
| `modal.close` | `panel` (without: all windows of this plugin) – since 1.1.3 |
| `media.resolved` | `requestId`, `url` (to play) or `error`; optional `title` – since 1.2.2, shown as the media title of a preloaded entry |
| `player.state` | → `path`, `source`, `title`, `position`, `duration`, `paused`, `state`, `playlistIndex` |
| `player.load` | `url` or `urls`, `next` – add to the playlist and play |
| `player.pause` | `paused` (without: toggle) |
| `player.stop`, `player.next`, `player.previous`, `player.reload` | – |
| `player.seek` | `seconds`, `relative` |
| `playlist.add` | `urls`, `play`, `next`, optional `titles` and `durations` (seconds) per entry, shown in the playlist before playback, and `replace` (clear the playlist first) – since 1.2.1 |
| `playlist.items` | → `[{path, title, current}]` |
| `playlist.play` | `index` |
| `mpv.get` / `mpv.set` / `mpv.command` | `name` / `name`, `value` / `args` – direct mpv access for everything without its own command |
| `seekbar.live` | `active`; with `active: true` also `window` (length of the whole timeline in seconds, e.g. since the viewer started watching), `limit` (how far back the rewind buffer still reaches, ≤ `window`; since 1.2.7 the part of the timeline before it is hatched and marked with an orange line), `behind` (current distance to the live edge), `atLive`, `busy`, optional `path` (default: the loaded medium) – since 1.2.5: turns the timeline of a live stream into a rewind bar for that medium. The timeline sends `seekbar.seek` / `seekbar.live` back; send it again whenever the values change and `active: false` when the stream ends |
| `video.filter.set` | `graph` – since 1.1.2: video filter with frame history (`atadenoise` or `hqdn3d` with numeric options, empty = remove), one owner at a time. Hardware decoding uses copy mode while it is active. Removed when the plugin is deactivated |
| `video.filter.status` | → `active`, `graph`, `error`, `hwdec`, `dropped`, `fps` – since 1.1.2 |
| `webview.available` | → `available`, `host`, `qt`, `engine` – since 1.0.7 in WebEngine builds: the player can show embedded web pages; panel QML may then `import QtWebEngine` (`WebEngineView`, `WebEngineProfile`) |

## One package for several systems (`libraries`, since 1.2.9)

A package can carry the native module for each system. `libraries` maps a **target key** to the module in the package:

```json
{
  "minHostVersion": "1.2.9",
  "supportedPlatforms": ["windows", "linux", "macos"],
  "libraries": {
    "windows-x64": "bin/windows-x64/example.dll",
    "linux-x86_64": "bin/linux-x86_64/libexample.so",
    "macos-arm64": "bin/macos-arm64/libexample.dylib"
  }
}
```

| Target key | System |
|---|---|
| `windows-x64`, `windows-arm64`, `windows-x86` | Windows |
| `linux-x86_64`, `linux-aarch64` | Linux |
| `macos-arm64`, `macos-x86_64` | macOS |

- The key is taken from the **running player binary** (its build architecture), not from the machine: an x64 player running
  emulated on ARM picks `windows-x64`.
- With `libraries` present only the matching entry counts and `library` is ignored. Without a matching entry the package is
  refused (*Platform or architecture '…' is not supported by this package*) – when installing and when loading.
- Without `libraries` everything stays as before (`library` or `main`).
- Installing checks that the selected module is in the package and is a UVE Player plugin; loading checks that it exists and
  has the UVE Player plugin IID. A refused package leaves the installed version and any update waiting for the next start
  untouched.
- Set `minHostVersion` to `1.2.9` or later: older players do not know `libraries` and refuse the package with
  *Requires host ≥ 1.2.9*. Their plugin updater still offers the package (it only compares versions), the installation then
  fails with that message and the installed version keeps working.
- Dependencies of a module that are not part of Qt or the player go next to the module in its `bin/<target>/` folder.

## Validation before loading (`validateManifest`)

id format, name, SemVer version, `apiVersion == 1`, `minHostVersion ≤ host`, platform, backends; for plugin folders also:
the module selected from `libraries` (or `library`) exists and is a UVE Player plugin, or `main` exists. Rejected plugins are not loaded and are shown with the
reason in *Settings → Plugins* (error log `logs\plugin-errors.log`).

## Lifecycle

`discover → verify → load → initialize → activate ⇄ suspend → deactivate → unload`
Enabling/disabling at runtime without a restart; disabled plugins lose their pages, menu entries, hotkeys and URL handlers
immediately. Exceptions in `initialize`, `onMediaChanged`, `onEvent` and commands are caught.

## Host services (`IPlayerHost`)

Per-plugin settings (JSON), data folder, set/remove a GPU shader stage (since 1.1.2 a plugin can pass
`<pluginId>#<name>` as owner for a further stage; `onShaderError` then starts with `<pluginId>#<name>: `), OSD, logging, read the current media path /
position / pause state, set pause, still frame without effects (`grabVideoFrame`, thread-safe),
since 1.0.2 `setVideoEqualizer` and `sampleDisplayFrame`, since 1.0.6 `call`, `hasCommand`, `commands`,
since 1.0.7 the command `webview.available` (embedded web views via Qt WebEngine, when the player is built with it).

## Online updates (since 1.1.9)

*Settings → Plugins* offers updates from the branch
[`uve-player-plugins`](https://github.com/nextscript/Ultimate-Video-Enhancer/tree/uve-player-plugins). To publish a plugin there:

- Put the package (`.zip` with `manifest.json` at the root or in one sub-folder) in the **root** of the branch as
  `<Name>-<Version>.zip`. `<Name>` is compared without spaces, dashes and case with the manifest `name`, the last part of the `id`
  or the whole `id` (`kickchat-1.3.1.zip` ↔ `"name": "Kick Chat"`, `GLSLManager-1.0.0.zip` ↔ `"name": "GLSL Manager"`).
- Raise `version` in `manifest.json`; the highest version in the branch is offered when it is newer than the installed one.
- The `id` in the package must equal the installed plugin's `id`, otherwise the player refuses the package.
- Older versions may stay in the branch; files in sub-folders and other file types are ignored.

## Security

Plugins (native and QML) run in the main process and are **trusted code**; `mpv.*` gives full access to mpv.
There is no signature verification and no process isolation; only install plugins from trustworthy sources.

Examples: `plugins/examples/sleeptimer/` (native, previous format), `plugins/examples/modaldemo/` (QML, modal windows and settings page), `tests/unit/tst_plugins.cpp` (QML plugin folder).
