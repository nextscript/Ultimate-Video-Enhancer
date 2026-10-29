# Format support

Decoding is done by the portable FFmpeg build; what it can decode, UVE Manager can export. The extension is only a
hint – every file is validated with ffprobe.

| Input | Preview | Export | Notes |
|---|---|---|---|
| MP4/M4V/MOV (H.264, HEVC, AV1, VP9) | direct | yes | HEVC preview uses the Windows platform decoder |
| MKV / WebM | direct (most codecs) | yes | |
| AVI, WMV, FLV, MPEG/MPG, TS/MTS/M2TS, 3GP, OGV, MXF, VOB | via cached H.264 preview proxy | yes | proxy is only for the preview |
| ProRes, DNxHD/DNxHR, MPEG-2, MPEG-4 ASP | via proxy | yes | |
| 10/12-bit sources | yes | yes, 16-bit processing path | choose "10-bit output" to keep precision |
| HDR10 / HLG | Chromium tone mapping | zscale + Hable tone mapping to SDR BT.709 | warned in the UI |
| VFR | yes | converted to constant average frame rate | warned in the UI |
| Rotated / anamorphic | yes | rotation applied, display aspect kept | |
| Alpha (VP8/VP9/ProRes 4444) | flattened | flattened | warned |
| DRM / encrypted | rejected | rejected | no circumvention |
| Audio-only | rejected with message | – | |

| Output container | Video codecs | Audio |
|---|---|---|
| MP4 | H.264, HEVC (hvc1), AV1 | copy (AAC, MP3, AC-3, E-AC-3, ALAC, Opus, FLAC) or AAC/Opus |
| MKV | H.264, HEVC, AV1, VP9 | copy (any) or AAC/Opus |
| MOV | H.264, HEVC, AV1, ProRes 422 HQ | copy (AAC, PCM, ALAC, AC-3…) or AAC |
| WebM | VP9, AV1 | Opus |

Encoders: NVENC H.264/HEVC/AV1 when a working session is detected (tested at startup), otherwise x264, x265,
SVT-AV1 / libaom, libvpx-vp9, prores_ks. "2× Upscale" is Lanczos resampling – not AI.
