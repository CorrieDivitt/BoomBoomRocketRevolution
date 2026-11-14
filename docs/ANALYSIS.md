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
| `Content/*.lvl` (30) | Tab-separated text | Header `SongBPM`, `SongLength` (s), `SongOffset` (s), `TargetWidth` (s); rows: optional launch beat, detonation beat, zone (-3..3), firework `Type`, colours, launcher, zoom, comment. Spec in 5.1 |
| `Content/*_Spl*.spl` | Text, 3 floats/line | Camera / launch / target splines per track (+ `Visualiser.spl`). Spec in 5.2 |
| `Content/*.mdl` (44) | Text point lists | Firework "model" shapes (Apple, Dragon, Skull…). Spec in 5.3 |
| `Content/contents.txt` | INI | Track list, scoreboard IDs (1–66), unlock UVs, model list. Spec in 5.6 |
| `Content/*.xma` (10 songs) | RIFF WAVE, format 0x0165 (**XMA1**), 48 kHz stereo | ~3.5 MB each. Spec in 5.9 |
| `Sounds/*.xma` (86) | XMA1 | Loops, crowd, launch/explode/tail SFX |
| `Fireworks/*.config` | INI | Per-grade (`Rubbish`/`Good`/`Awesome`/`Visualizer`) Count, Velocity, trails, sparkles — key to particle-quality scaling. Spec in 5.4 |
| `Fireworks/colours.raw` | 240-byte palette | 80 RGB8 entries. Spec in 5.5 |
| `Models/city.bbr` | Big-endian binary (Bizarre) | City mesh, 1.8 MB. Spec in 5.7 |
| `Textures/CityTextures.xpr` | XPR2 (`TX2D` entries) | 126 tiled DXT2/3 Xenos textures, 17 MB. Spec in 5.8 |
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

### Leaderboard schema (from the XLAST embedded in the `XSRC` section)
- 250 stats views (`LB_001`…), all `viewType="Leaderboard"`, ranked by **LB_SCORE** (`0x20000001`, int64), arcade sort **Descending**.
- Column attribute id → property: 65535 Rank (`0x10008001`), 65533 Gamer name (`0x40008002`), 65534 Score/rating (`0x20000001`), 1 Avg accuracy (`0x30000006`, double), 3 Hit ratio (`0x30000007`, double), 2 Grade (`0x10000008`, int32), 5 Lap (`0x10000004`, int32), 6 Time lasted (`0x30000005`, double), 4 BPM (`0x30000003`, double). Some views only have Rank/Name/Score.
- Game wrappers (hooked by `src/leaderboards.cpp`): `XSessionWriteStats` 0x8215FA28, `XUserReadStats` 0x820E76E0, `XUserCreateStatsEnumeratorByRank` 0x820E77C8, `...ByXuid` 0x820E7828, `XEnumerate` 0x820E7368. The game's leaderboard manager lives around 0x820C1C40–0x820C5700.

### Display / camera (widescreen work)
- Game init `0x8207ECD8` calls `XGetVideoMode`: widescreen flag selects aspect 16:9 (`0x82003350`) or 4:3; hi-def flag stores 1280x720 at `0x821AA718/1C`. Only three code sites hardcode 1280/720 (`0x8207F298`, `0x820CC8EC`, `0x8212D770`/`0x820B9BB8`).
- Camera objects start with `{float fov; float aspect; ...}`, position at `+0x50`; main camera at game object `0x821C1420 + 0x104B0` (fov 60 deg), another at `+0x103FC`. `0x82061018` = camera look-at/position setter.
- High frame-rate tests use 120 fps (project convention).

## 4. Other observations
- UI is Bizarre's **BZUI** system driven by Lua 5.1 scripts (`UISetup.lua`, `UIEnums.lua` inside `UIScript`) — a natural place to re-anchor UI for wide aspect ratios.
- Debug strings report load times in *frames* ("InitLevel() took %f secs. %.1f frames"), suggesting a 60 Hz frame-locked loop — to be confirmed (R-301).
- Path strings mix `GAME:\` and `game:\`; also `DLC%d:\` for track packs.
- **Dump gotcha:** `Ride_Of_The_Valkyries_SplCamera_Out.spl` and `..._SplTarget_Out.spl` are 39 characters; some
  extraction tools truncate names to 38 (`*.sp`), which makes that song fail with a dirty-disc error.
  `scripts/bootstrap.ps1` now restores the `.spl` copies.
  The `.sp` names are not game files: v1.0.0's launcher file list wrongly required them (issue #1),
  so clean dumps and the original package were refused. Since v1.0.1 the list has the 325 real files and
  the launcher installs a cut-short `.sp` as `.spl` when the real one is absent.

## 5. Asset format specifications (R-007)

Worked out 2026-11-13 from the retail files and the game's own loaders in the recompiled code
(`generated/default`). Every count, stride and index rule below was checked against all shipped
files; "seen" means a value range observed in the data rather than read from the code.
All text files use CRLF line ends. Binary files are big-endian unless stated.

### 5.1 Charts: `Content/<Song>_{Easy,Med,Hard}.lvl`
Loader `sub_82067360` (header block written to `0x821A0564`, see TIMING.md). The file is first
read into a cell grid by `sub_82083050`: rows are split on LF, cells on TAB (a CR also ends a
cell); the column count is the widest row, and missing cells are NULL. Nothing is trimmed and
blank lines stay in as empty rows.

| Row (0-based) | Content |
|---|---|
| 0 | Song name and difficulty (`Beethovens<TAB>Hard`). Not read; it is wrong in several files (Carmen all say `Hard`, Dvořák says `Rave`, Valkyries is blank) |
| 1 | `SongBPM<TAB><float>` |
| 2 | `SongLength<TAB><float>` seconds |
| 3 | `SongOffset<TAB><float>` seconds (0 everywhere except Valkyries Easy, 0.03) |
| 4 | `TargetWidth<TAB><float><TAB>(comment)` — the hit window in **seconds** (0.125–0.5). The comment gives it in beats, e.g. `0.25 (1/2beat)` at 120 BPM |
| 5 | Column titles, not read: `Launch Detonation Zone Type PrimaryCol SecondaryCol Launcher ZoomDistance Comment` |
| 6+ | One firework per row |

The header keys are checked at those fixed rows (`strncmp`), not searched for.

Firework row columns:

| Col | Title | Meaning |
|---|---|---|
| 0 | Launch | Optional launch beat. Empty in every shipped chart; if present it is converted with `beat × 60 / BPM + SongOffset` |
| 1 | Detonation | **Beat the rocket must be hit on** (the note time). A row whose cell 1 is empty or missing is skipped, which covers the blank separator lines |
| 2 | Zone | Lane / screen position of the burst, integer -3..3 (seen; `-0` occurs) |
| 3 | Type | Firework type plus optional flags, matched by case-sensitive substring (`strstr`) |
| 4 | PrimaryCol | **The button to press**, always one of `Green` (A), `Red` (B), `Blue` (X), `Yellow` (Y) |
| 5 | SecondaryCol | Second burst colour: any of the 7 colours, or empty |
| 6 | Launcher | Optional; seen 0, 7–12, 16–20 |
| 7 | ZoomDistance | Optional camera zoom change; seen -150..375 |
| 8+ | Comment | Free text (`intro`, `main1`); not used |

Beats seen: whole, half, quarter and third beats (fractions .0 .25 .333 .5 .667 .75).

**Types** known to the loader (`0x82000AC0`–`0x82000B6C`): `Peony`, `Willow`, `Circle`, `Ring`,
`Comet`, `Bees`, `Palm`, `Croisette`, `Spider`, `Sparkle`, `3DShape`, `FastPeony`, `FastRing`,
`BigPeony`, `BigCroisette`, `BigSpider`, `BigWillow`, `BigSparkle`. **Flags** appended to the
type: `+Special`, `+Crowd`, `+Shake` (any order). A type that matches nothing makes the note
builder `sub_82065E70` log `Unknown firework type` and drop the note.
**Colours** (`0x82000B74`–`0x82000BA4`): `Green`, `Red`, `Blue`, `Yellow`, `Orange`, `Cyan`,
`Magenta`. The first four are the buttons A, B, X, Y (rocket byte +8 = index × 2, see `src/autoplay.cpp`).

Data errors in the shipped charts, and what the loader does with them:

| File:line | Row | Effect |
|---|---|---|
| `Dvorak_9th_Hard.lvl:175` | extra leading tab, beat lands in the Zone column | cell 1 empty → row skipped (note at beat 188.5 is missing in game) |
| `Carmen_Overture_Hard.lvl:155` | type `Blue` | no type matches → note dropped |
| `William_Tell_Easy.lvl:151` | type `Palmt` | substring match → plays as `Palm` |
| `Beethovens_Fifth_Easy.lvl:160`, `Mountain_King_*.lvl` | comment text in the ZoomDistance column (`madbit`, `chorusA 4`) | reads as 0 / ignored |

Timing: `note_time_s = SongOffset + beat × 60 / SongBPM` (TIMING.md).

### 5.2 Splines: `Content/<Song>_Spl{Camera,Launch,Target}[_In|_Out].spl`, `Visualiser.spl`
One control point per line, `x<TAB>y<TAB>z` floats, no header. Loader `sub_82086948`
(r3 = spline, r4 = file name, f1 = length in seconds); `sub_82085F08` evaluates a point at a song
time (see `src/camera_speed.cpp`). The main loop splines have 15 points per song; `_In` (9) and
`_Out` (4) are the intro and ending camera moves. Name suffixes come from `0x8200135C`
(`_SplTarget.spl`, `_SplLaunch.spl`, `_SplCamera.spl`) and `0x82003968` (`_Out.spl`, `_In.spl`).
`Flower_Duet_SplCamera_Int.spl` matches no suffix the game builds, so it is presumably unused.
`GAME:\content\visualiser.spl` (30 points) is the Visualizer camera.

### 5.3 Firework shapes: `Content/*.mdl`
One point per line, `x y z` separated by single spaces, no header; 36–962 points per file,
coordinates within about ±0.15. 12 files are flat (z = 0); the rest are 3D (DNA, atom, galaxy,
planet…). Each point is presumably one star of the shaped burst (not traced in code). Which files exist in game is set by the
`[Model_n]` sections of `contents.txt` (5.6).

### 5.4 Firework tuning: `Fireworks/<type>.config`
INI, 18 files (one per type: `peony`, `ring`, `comet`, `spider`, `palm`, `willow`, `bees`,
`croisette`, `sparkle`, `model`, `bonus_model`, `fast_peony`, `fast_ring`, `big_peony`,
`big_spider`, `big_willow`, `big_croisette`, `big_sparkle`). Each has the sections `[Rubbish]`,
`[Good]`, `[Awesome]` (hit grade) and `[Visualizer]`, every one with the same 13 keys:

| Key | Values seen |
|---|---|
| `Count` | 10–1200 particles (becomes the emitter float at `+0x1A8`, see HANDOFF particles) |
| `Velocity` | 150.0–2000.0 |
| `Scale` | 0.8–1.0 |
| `LifeScale` | 0.375–5.0 |
| `FadeMode` | `Monochrome`, `Random`, `ThroughNext`, `ThroughNext2`, `ThroughNext3`, `ThroughPalette` |
| `TrailType` | `None` or `"<Light\|Medium\|Heavy>Weight <Short\|Medium\|Long>Length"` (quoted) |
| `Sparkles`, `TrailSparkles` | `None`, `NoneToLight`, `Light`, `LightToMedium`, `Medium` |
| `TwoTone` | `true` / `false` |
| `Texture`, `TrailTexture` | small integers (0–9, 14): cells of the particle texture sheet |
| `Rotation`, `TrailRotation` | 0.0–5.5 |

### 5.5 Palette: `Fireworks/colours.raw`
240 bytes = 80 entries of R, G, B (one byte each), no header. Loaded from both
`GAME:\Fireworks\colours.raw` and `game:\fireworks\colours.raw`. The entries are bright firework
hues in short runs (greens, reds, yellows…), consistent with the `ThroughPalette` /
`ThroughNext*` fade modes stepping through it.

### 5.6 Track and model list: `Content/contents.txt` (also in each DLC pack)
INI with `#` comments, `key = value` (tabs or spaces around `=`).
- `[Pack]`: `Id`, `IdsPackname`, six `ScoreboardId_Combined_{Standard,Endurance}_{Easy,Medium,Hard}`,
  `UnlockFireworkUSize`, `UnlockFireworkVSize` (cell size in the unlock-icon sheet).
- `[Track_1]`…`[Track_10]`: `Id` (0-based), `Filename` (the `.xma`; the chart and spline names are
  built from its stem, e.g. `Mountain_King` → `Mountain_King_Hard.lvl`; matched case-insensitively,
  `Flower_Duet.xma` is `flower_duet.xma` on disk), `IdsSongname`,
  six `ScoreboardId_{Quick,Endur}_{Easy,Medium,Hard}`, and
  `UnlockFirework{U,V}Coord_{Easy,Medium,Hard}`.
- `[Model_1]`…`[Model_38]` (hidden fireworks): `Id`, `Filename` (`.mdl`), optional
  `SecondFilename` (a second shape, e.g. `DNA_1`/`DNA_2`), `HiddenInTrack` (song `.xma`) and
  `Difficulty` (0–2) for the 30 unlockable ones, `UseInBonusRun` for the 8 bonus-run shapes,
  and `Speed`, `Scale`, `PitchVar`, `RollVar`, `YawVar` (spin).
The DLC loader requires every `UnlockFirework*` key (see `src/dlc.cpp`, DLC_PROTOTYPES.md).

### 5.7 City mesh: `Models/city.bbr`
Loader `sub_820CAAC8` (reads the whole file to the buffer at `[0x822E7238]`, then sets up the table pointers at `0x822E7240`).
Header = 9 big-endian u32 counts, then the tables back to back in this order, then a string pool.
The sizes add up exactly to the file size (1,856,992 bytes).

| Header word | Count (city) | Table, in file order | Stride | Record |
|---|---|---|---|---|
| `h[0]` | 124 | Texture names | 4 | u32 offset into the string pool (`BBR_Tower_Mid_06.tga`…) |
| `h[4]` | 30,426 | Vertices | 32 | `float pos[3], normal[3], uv[2]` |
| `h[3]` | 18,493 | Triangles | 12 | `u32 i0, i1, i2`, indices **local to the submesh's first vertex** |
| `h[2]` | 435 | Submeshes | 20 | `u32 texture` (index into the name table, `0xFFFFFFFF` = untextured, 13 of them), `firstTriangle`, `triangleCount`, `firstVertex`, `vertexCount` (both ranges contiguous) |
| `h[1]` | 172 | Meshes | 8 | `u32 submeshCount, firstSubmesh` (contiguous) |
| `h[5]` | 6,766 | Instances | 76 | `u32 name` (pool offset, `Scraper_01`, `Lifts124`…), `u32 parent` (always `0xFFFFFFFF`), `u32 mesh`, `float m[4][4]` (row-major, translation in the last row, D3D style) |
| `h[8]` | 4,081 | Path points | 12 | `float x, y, z` |
| `h[7]` | 28 | Vehicles | 80 | `u32 name` (`Cars_001`, `Boat_003`, `Helicopter_001`…), `u32 parent` (`0xFFFFFFFF`), `float m[4][4]`, `u32 firstPathPoint, pathPointCount` (contiguous, covering all path points) |
| `h[6]` | 85,408 | String pool | 1 | NUL-terminated names: 124 textures, then 6,766 instances, then 28 vehicles |

Notes: normals are unit length; UVs tile (well outside 0..1). 72 vertices of `Bridge_F1`
sit far outside the city (x up to 19,082, z down to -24,323); harmless in the original, worth
knowing for any bounding-box or lighting work. 122 of the 124 texture names match a
`CityTextures.xpr` entry once upper-cased with `.` → `_` (`BBR_Roof_Stone.tga` ↔ `BBR_ROOF_STONE_TGA`); the other two are an empty name and `BBR_Temp.tga`.

### 5.8 Textures: `Textures/CityTextures.xpr`, `UI/Data/ui.xpr`
Xbox Packed Resource v2. Header: `char magic[4] = "XPR2"`, `u32 headerSize` (0x3000 / 0x800),
`u32 dataSize`, `u32 count`, then `count` directory entries of
`char type[4]` (`TX2D` in both files), `u32 headerOffset`, `u32 headerSize` (0x34),
`u32 nameOffset` — offsets relative to byte 12. Names are upper-case with the extension folded in
(`BBR_BLACK_TGA`). Pixel data starts at `12 + headerSize`; file size = `12 + headerSize + dataSize`.
Each resource header is a 52-byte `D3DBaseTexture`: 7 u32 (Common, ReferenceCount, Fence,
ReadFence, Identifier, BaseFlush, MipFlush) then the 6-dword Xenos texture fetch constant:
dword 0 bits 0–1 type (2 = texture), bits 22–30 pitch / 32, bit 31 tiled; dword 1 bits 0–5
format, bits 6–7 endian (1 = 8-in-16), bits 12–31 base address >> 12 (relative to the data
start); dword 2 width − 1 (bits 0–12) and height − 1 (bits 13–25); dword 5 bits 12–31 the mip
address. All 126 city textures are 2D, tiled, format 19 (`k_DXT2_3`, BC2), 32×32 to 256×256.
`ui.xpr` holds 19 `TX2D` entries (5.2 MB). The game rejects a bad magic with
`Invalid Xbox Packed Resource (.xpr) file`.

### 5.9 Audio: `Content/*.xma`, `Sounds/*.xma`
RIFF/WAVE with chunks `fmt `, `data`, `XMA2`, `seek` in that order (all 96 files).
`fmt ` (32 bytes) is `XMAWAVEFORMAT` (tag `0x0165`, XMA1): `wFormatTag`, `BitsPerSample` = 16,
`EncodeOptions`, `LargestSkip`, `NumStreams` = 1, `LoopCount` (u8), `Version` = 3, then one
`XMASTREAMFORMAT`: `PsuedoBytesPerSec`, `SampleRate`, `LoopStart`, `LoopEnd` (both 0
everywhere), `SubframeData`, `Channels`, `ChannelMask` (u16). All are 48 kHz stereo except
`UI_multiplier_down_final.xma` (mono) and `UI_multiplier_shake.xma` (47,953 Hz).
`LoopCount` = 255 (loop forever) on all 10 songs and 16 ambience/loop sounds (`*_loop`, `City`,
`Crowd_*`, `UI_multiplier_maxloop`, `UI_multiplier_shake`); 0 on the one-shot effects.
The `XMA2` and `seek` chunks carry the XMA2-style block/seek table the decoder uses.
Song files are found from `Filename` in `contents.txt`; sound effects by fixed names in the
XEX (`0x82002870`–`0x82002E8C`).

### 5.10 Other files (outside R-007 scope, for reference)
`Textures/*.dds` / `*.tga` are standard DDS / TGA. `UI/Text/*.ini` are string tables.
`UI/Data/*.dat` are BZUI font data (not decoded). Shader formats are covered in GRAPHICS.md.
