# .uveproj schema (version 1)

```jsonc
{
  "schema": "uve-project",
  "version": 1,
  "app": "UVE Manager 1.0.0",
  "uveSource": { "name": "...", "version": "1.17.5", "repo": "...", "commit": "38b2ed1…", "sha256": "28b37df…" },
  "savedAt": "2026-10-29T12:00:00.000Z",
  "media": { "path": "D:\\Videos\\holiday.mkv", "size": 123, "mtimeMs": 0, "videoStream": 0, "audioStream": -1 },
  "effect": {
    "settings": { /* identical keys to the userscript user-profile "settings" object */ },
    "expert":   { /* full GVF_*_SETTINGS blocks */ },
    "customFilters": [ /* gvf custom filter records (id, label, code, type, enabled, blendMode, uniforms, …) */ ],
    "lut": { "name": "...", "group": "...", "matrix4x5": [20 numbers] } // embedded copy, or null
  },
  "inPoint": null, "outPoint": null,
  "export": { "container": "mp4", "videoCodec": "hevc", "encoder": "auto", "quality": 20, … },
  "ui": { "wipe": 0.5, "compare": true }
}
```

Loading validates every field (unknown keys dropped, numbers clamped to the UVE ranges). A missing video is
reported and can be relocated; a LUT that is neither in the library nor embedded is reported and not applied.

User profiles keep the userscript format (`{id,name,createdAt,updatedAt,settings}`) plus an optional `desktop`
object (expert overrides, custom filter stack, embedded LUT) that the userscript ignores on import.
