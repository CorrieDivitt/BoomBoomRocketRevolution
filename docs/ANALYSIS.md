# Boom Boom Rocket — Game File Analysis

Findings from the retail XBLA dump in `Game Files/` (analysed 2026-11-10 with `tools/xex/*`).
Nothing in this document is game data; it only describes it.

## 1. Executable (`default.xex`)

| Field | Value |
|---|---|
| Format | XEX2, retail-encrypted (AES), LZX-compressed (window 2^15) |
| Title ID | `0x5841086A` (TitleInfo ID 1480657002 in ArcadeInfo.xml) |
| Original PE name | `BoomBoomRocket_Master.exe` (a *Master* — i.e. final/retail — build) |
| XDK | 2.0.5426 (ArcadeInfo `xdkVersion="2.0.5426.0"`, project 1.0.116.0) |
| Image base / size | `0x82000000` / `0x320000` (3,276,800 bytes) |
| Entry point | `0x820E83D8` |
| Default stack | 256 KB |
| Players | 1–2 (local) |
| Resources | `UIScript` (BZUI Lua UI bundle, 0x177D7 bytes) and `5841086A` (XDBF/SPA: achievements, leaderboards, strings) |

### Static libraries linked (all XDK 2.0.5426)
`XAPILIB`, `D3D9`, `D3DX9`, `XGRAPHC`, `XBOXKRNL`, `XAUD` (XAudio 1), `LIBCPMT`, `XMP` (Xbox Music Player — custom soundtracks / Visualizer), `XONLINE` (leaderboards/presence).
Also embedded: Lua 5.1, zlib 1.2.1, libjpeg (1998), libpng.

### PE sections
| Section | Address | Size |
|---|---|---|
| .rdata | 0x82000400 | 0x4B8B8 |
| .pdata | 0x8204BE00 | 0x5DD0 (**3,002 function entries**, covering 0x11B708 of 0x136B0C text bytes) |
| .text | 0x82060000 | 0x136B0C (318,147 instructions) |
| .data | 0x821A0000 | 0x14DE3C |
| .XBMOVIE / .idata / .XBLD / .reloc | 0x822EE000 / 0x822F0000 / 0x82300000 / 0x82300200 | small |

Code statistics: 8,709 VMX/VMX128 instruction words (opcodes 4/5/6), 313 `bctr` of which 292 follow `mtctr` (jump tables / indirect calls), 11 `mftb` (time-base reads).

### Compiler helper candidates (verify in Ghidra before use)
| Helper | Address |
|---|---|
| `__savegprlr_14` | 0x82162030 |
| `__restgprlr_14` | 0x82162080 |
| `__savefpr_14` | 0x821620E0 |
| `__restfpr_14` | 0x8216212C |
| `__savevmx_14` | 0x821648D0 |
| `__restvmx_14` | 0x82164B68 |
| `setjmp` | 0x82167220 (confirmed) |
| `longjmp` | 0x821675A0 (confirmed) |

### Imports — 166 total (43 xam.xex, 123 xboxkrnl.exe)
Checked against ReXGlue SDK v0.10.1 (`thirdparty/rexglue-sdk/src/kernel`): **162 implemented**, 4 missing:

| Missing in ReXGlue | Needed for | Plan |
|---|---|---|
| `XamUserCreateStatsEnumerator` | Reading leaderboards | Implement (R-503) |
| `XamShowAchievementsUI` | Guide achievements view | Native overlay or no-op (R-108) |
| `XamShowMarketplaceUI` | Trial upsell / DLC | No-op, full license (R-106, R-108) |
| `XamShowGamerCardUIForXUID` | Leaderboard gamer cards | No-op (R-108) |

Notable imports: `VdSwap` + full `Vd*` set (game drives the GPU directly through D3D9 → emulated by the Xenia-derived GPU backend), `XMACreateContext` / `XAudio*RenderDriver*` (hardware XMA decode), `KeQueryPerformanceFrequency` + `mftb` (timing), `XamUserRead/WriteProfileSettings`, `XamContentCreateEx` (saves), `XamContentGetLicenseMask` (trial check), `XamSessionCreateHandle` + `XMsgStartIORequest` (XOnline stats writes via the XGI app).
Regenerate the full list with `python tools/xex/analyze.py "Game Files/default.xex" <image.pe>`.

## 2. Content layout & formats

| Path | Format | Notes |
|---|---|---|
| `Content/*.lvl` (30) | Tab-separated text | Header `SongBPM`, `SongLength` (s), `SongOffset` (s), `TargetWidth 0.2 (1/4 beat)`; rows: `Launch` (beat), `Detonation` zone (-2..2), firework `Type`, colours, launcher, zoom, comment |
| `Content/*_Spl*.spl` | Text, 3 floats/line | Camera / launch / target splines per track (+ `Visualiser.spl`) |
| `Content/*.mdl` (44) | Text vertex lists | Firework "model" shapes (Apple, Dragon, Skull…) |
| `Content/contents.txt` | INI | Track list, scoreboard IDs (1–66), unlock UVs, model list |
| `Content/*.xma` (10 songs) | RIFF WAVE, format 0x0165 (**XMA1**), 48 kHz stereo | ~3.5 MB each |
| `Sounds/*.xma` (86) | XMA1 | Loops, crowd, launch/explode/tail SFX |
| `Fireworks/*.config` | INI | Per-grade (`Rubbish`/`Good`/`Awesome`) Count, Velocity, trails, sparkles — key to particle-quality scaling |
| `Fireworks/colours.raw` | 240-byte palette | |
| `Models/city.bbr` | Big-endian binary (Bizarre) | City mesh, 1.8 MB |
| `Textures/CityTextures.xpr` | XPR2 (`TX2D` entries) | Tiled Xenos textures, 17 MB |
| `Textures/*.dds, *.tga` | DDS / TGA | Particles, smoke, cubemap |
| `UI/Data/ui.xpr`, `*.dat` | XPR2 + font data | BZUI front-end |
| `UI/Text/*.ini` | INI | Localised strings (EN/FR/DE/IT/ES) |
| `shaders/*.xvu / *.xpu` | Xenos microcode | Loose shaders; more are embedded in the XEX (e.g. `CommonVS.xvu`) |
| `shaders/*.updb` | XML shader PDB | **Contain the original HLSL source** (9 files recovered with `tools/shaders/extract_updb_hlsl.py`) |
| `shaders/SimpleShaders.fxobj` | D3DX effect object | |

### Tracks (10 in base game)
| Track | BPM | Length (s) | Notes E / M / H |
|---|---|---|---|
| Flower Duet | 75 | 156.0 | 101 / 174 / 268 |
| Dvořák 9th | 150 | 148.0 | 144 / 222 / 296 |
| William Tell | 135 | 188.6 | 222 / 300 / 361 |
| Hall of the Mountain King | 128 | 192.2 | 219 / 316 / 349 |
| 1812 Overture | 160 | 189.0 | 189 / 277 / 417 |
| Ride of the Valkyries | 120 | 154.0 | 93 / 154 / 213 (Easy has SongOffset 0.03) |
| Moonlight Sonata | 135 | 245.3 | 201 / 308 / 423 |
| Carmen Overture | 120 | 171.3 | 175 / 201 / 273 |
| Beethoven's Fifth | 120 | 184.5 | 205 / 289 / 361 |
| Toccata and Fugue | 136 | 202.5 | 127 / 254 / 349 |

**Timing model implication:** charts are in *beats* at a constant BPM with a per-chart offset in seconds, so
`beat = (song_time_s − SongOffset) × BPM / 60`. Driving `song_time_s` from the audio device's playback clock is enough to keep notes, judging and visuals locked to the music at any frame rate.

## 3. SPA / Live data (resource `5841086A`)
- 12 achievements, 200 G total (IDs 1–11, 13).
- Leaderboards: 66 scoreboard IDs in `contents.txt` — 60 per-track boards (10 tracks × {Quick, Endurance} × {Easy, Medium, Hard}, IDs 4–63) plus 6 combined boards (IDs 1–3 Standard, 64–66 Endurance). Fields include BPM, Laps, Time Lasted, Accuracy, Hit ratio, Grade.
- Rich presence strings for Standard / Endurance / Practice / Freestyle / Visualizer / 2P Battle / 2P Endurance.

## 4. Other observations
- UI is Bizarre's **BZUI** system driven by Lua 5.1 scripts (`UISetup.lua`, `UIEnums.lua` inside `UIScript`) — a natural place to re-anchor UI for wide aspect ratios.
- Debug strings report load times in *frames* ("InitLevel() took %f secs. %.1f frames"), suggesting a 60 Hz frame-locked loop — to be confirmed (R-301).
- Path strings mix `GAME:\` and `game:\`; also `DLC%d:\` for track packs.
