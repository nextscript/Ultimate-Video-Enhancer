'use strict';
// Menüleiste nach VLC-Vorbild (Medien, Wiedergabe, Audio, Video, Untertitel, Werkzeuge, Ansicht, Hilfe).
// Tastenkürzel werden nur angezeigt (registerAccelerator: false) – ausgewertet werden sie
// im Renderer, damit Plugins (UVE nutzt STRG+ALT+…) Vorrang haben können.
const path = require('path');
const { Menu } = require('electron');

function item(label, action, accelerator, extra) {
  return Object.assign({ label, accelerator, registerAccelerator: false, click: (_m, win) => send(win, action) }, extra || {});
}

let target = null;
function send(win, action, arg) {
  const w = win || target;
  if (w && !w.isDestroyed()) w.webContents.send('menu', { action, arg });
}

function build(win, state) {
  target = win;
  const st = state || {};
  const recent = (st.recent || []).slice(0, 15);
  const pluginItems = (st.plugins || []).map((p) => ({
    label: `${p.name}  (${p.version})`,
    type: 'checkbox',
    checked: !!p.enabled,
    click: () => send(win, 'plugin-toggle', p.id)
  }));

  const template = [
    {
      label: '&Medien',
      submenu: [
        item('Datei öffnen…', 'open-file', 'CmdOrCtrl+O'),
        item('Mehrere Dateien öffnen…', 'open-files', 'CmdOrCtrl+Shift+O'),
        item('Ordner öffnen…', 'open-folder', 'CmdOrCtrl+F'),
        item('Netzwerkstream öffnen…', 'open-url', 'CmdOrCtrl+N'),
        {
          label: 'Zuletzt geöffnet',
          submenu: recent.length
            ? [...recent.map((r) => ({ label: path.basename(r) || r, sublabel: r, click: () => send(win, 'open-recent', r) })),
              { type: 'separator' }, item('Liste leeren', 'clear-recent')]
            : [{ label: '(leer)', enabled: false }]
        },
        { type: 'separator' },
        item('Wiedergabeliste speichern…', 'save-playlist', 'CmdOrCtrl+Y'),
        { type: 'separator' },
        item('Beenden', 'quit', 'CmdOrCtrl+Q')
      ]
    },
    {
      label: '&Wiedergabe',
      submenu: [
        item('Wiedergabe / Pause', 'toggle-play', 'Space'),
        item('Stopp', 'stop', 'S'),
        item('Vorheriger Titel', 'prev', 'P'),
        item('Nächster Titel', 'next', 'N'),
        { type: 'separator' },
        item('10 s zurück', 'seek-back', 'Left'),
        item('10 s vor', 'seek-fwd', 'Right'),
        item('Zu Zeit springen…', 'goto-time', 'CmdOrCtrl+T'),
        item('Einzelbild vor', 'frame-step', 'E'),
        { type: 'separator' },
        item('Schneller', 'faster', ']'),
        item('Langsamer', 'slower', '['),
        item('Normale Geschwindigkeit', 'normal-speed', '='),
        { type: 'separator' },
        item('A-B-Wiederholung', 'ab-loop', 'L'),
        item('Wiederholen umschalten', 'cycle-repeat', 'R'),
        item('Zufallswiedergabe', 'toggle-shuffle', 'Z', { type: 'checkbox', checked: !!st.shuffle })
      ]
    },
    {
      label: '&Audio',
      submenu: [
        item('Lauter', 'vol-up', 'Up'),
        item('Leiser', 'vol-down', 'Down'),
        item('Stumm', 'mute', 'M'),
        { type: 'separator' },
        item('Equalizer / Lautstärke-Boost…', 'equalizer', 'CmdOrCtrl+E'),
        item('Audiospur wechseln', 'cycle-audio', 'B')
      ]
    },
    {
      label: 'V&ideo',
      submenu: [
        item('Vollbild', 'fullscreen', 'F'),
        item('Seitenverhältnis wechseln', 'cycle-fit', 'A'),
        item('Bildschirmfoto', 'snapshot', 'Shift+S'),
        item('Immer im Vordergrund', 'always-on-top', '', { type: 'checkbox', checked: !!st.alwaysOnTop }),
        { type: 'separator' },
        {
          label: 'Ultimate Video Enhancer',
          submenu: [
            ['Einstellungen / Import-Export', 'i'], ['Grading-HUD', 'g'], ['HDR an/aus', 'p'], ['Profil an/aus', 'c'],
            ['Auto-Scene-Match', 'a'], ['Scopes-HUD', 's'], ['GPU-Modus', 'x'], ['Vorher/Nachher-Vergleich', 'u']
          ].map(([label, k]) => ({ label, accelerator: 'CmdOrCtrl+Alt+' + k.toUpperCase(), registerAccelerator: false, click: (_m, w) => send(w, 'uve-key', k) }))
            .concat([{ type: 'separator' }, { label: 'Nächstes Profil', accelerator: 'F8', registerAccelerator: false, click: (_m, w) => send(w, 'uve-key', 'F8') }])
        }
      ]
    },
    {
      label: '&Untertitel',
      submenu: [
        item('Untertiteldatei hinzufügen…', 'add-subtitle'),
        item('Untertitelspur wechseln', 'cycle-sub', 'V'),
        item('Verzögerung −50 ms', 'sub-delay-minus', 'G'),
        item('Verzögerung +50 ms', 'sub-delay-plus', 'H')
      ]
    },
    {
      label: 'W&erkzeuge',
      submenu: [
        item('Plugins verwalten…', 'plugins', 'CmdOrCtrl+P'),
        { label: 'Plugins', submenu: pluginItems.length ? pluginItems : [{ label: '(keine)', enabled: false }] },
        item('Medieninformationen…', 'media-info', 'CmdOrCtrl+I'),
        { type: 'separator' },
        item('Einstellungen…', 'settings', 'CmdOrCtrl+,')
      ]
    },
    {
      label: '&Ansicht',
      submenu: [
        item('Wiedergabeliste', 'toggle-playlist', 'CmdOrCtrl+L', { type: 'checkbox', checked: st.showPlaylist !== false }),
        item('Minimale Oberfläche', 'minimal', 'CmdOrCtrl+H'),
        item('Neu laden', 'reload', 'CmdOrCtrl+R')
      ]
    },
    {
      label: '&Hilfe',
      submenu: [
        item('Tastenkürzel', 'shortcuts', 'F1'),
        item('Nach Updates für FFmpeg und yt-dlp suchen…', 'check-tools'),
        item('Ultimate Video Enhancer auf GitHub', 'open-uve-repo'),
        { type: 'separator' },
        item('Über UVE Player', 'about')
      ]
    }
  ];
  Menu.setApplicationMenu(Menu.buildFromTemplate(template));
}

module.exports = { build, send };
