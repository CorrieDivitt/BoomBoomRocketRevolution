# Handoff notes (written 2026-11-11, end of the features / modifiers session)

For whoever picks the project up next, human or Claude. The other docs go deeper on one subject
each: `PLAN.md` (phases), `TIMING.md` (song clock, judging, end of song), `GRAPHICS.md` (shaders,
HDR, particles), `DLC_PROTOTYPES.md`, `ANALYSIS.md` (game internals), `USER_GUIDE.md` (what the
player sees). The requirements list (`requirements.py` -> csv/json and the dashboard) is the task
list; statuses there are current as of this note.

## Where things stand
Everything below is built, pushed (GitHub `main` = `ae10659` plus this note) and confirmed in play
by the user unless marked otherwise.

| Area | State |
|---|---|
| Timing | Song clock smoothing, audio/input offsets, auto calibration, 1 kHz timestamped input, hit timing readout (setting "Show Accuracy", plain orange, 40 px above the line: position locked), results-screen timing line, assist tick |
| Menus | BBRR Settings with Video Settings, Calibration, Modifiers pages; live window mode, FXAA, glow, HDR, particle density (only Resolution needs a restart) |
| Graphics | Bloom with HUD mask, live HDR output (SDK patch 0004), Particle Density 0.25x-5x (locked), spark limit 68,000 (locked) |
| Modifiers | Scroll Speed 0.5x-4x, changes live; RB / LB step it during a song and on the song select screen |
| Song select | Length (left) and BPM x speed (right) in each bar, "Length" / "BPM" headings, Scroll Speed panel with LB / RB icons |
| Wide screens | Edge anchoring with a remembered layout per screen, so menus no longer jump after a transition |
| DLC | Original packages install from the `DLC` folder; prototype songs renamed "<Title> II" |

Accepted as done on 2026-11-11 without a dedicated test (the user will raise them if testing shows
problems): fail slow-down at high frame rates (1.67 s at any fps); the "Restart Song" / "Replay
Song" wording; Windows Auto HDR; prototype titles stay "<Title> II"; first-note behaviour at speeds
above 1x; the multiplayer song list (has length and BPM, no speed panel or buttons).

## Next up (the user's words: "the next phase of the plan")
- R-807 offset suggestion on the results screen: built 2026-11-11 and seen working (see below);
  wording now "(Current setting +20 ms)".
- Widescreen fixes built 2026-11-11, untested: shipped layouts (`ui_scripts/ui_layout.txt`) so
  first visits (pause menu) no longer snap late; hit readout band and results screen kept centred.
- Built 2026-11-11 (later): Rocket Acceleration (R-809, seen working) and No Background (R-808,
  confirmed working) under Modifiers; Hit Timing renamed Show Accuracy; first-arrow fix at higher speeds (R-802); pause menu keeps its place under a
  dialogue (R-415). **No Background needs SDK patch 0005: run `scripts\patch-sdk.ps1` before
  `scripts\build.ps1`.**
- Other modern rhythm-game features under Modifiers (R-801 research list).
  R-805 practice tools moved to the post-1.0 stretch goals (phase 9, now "Stretch goals (post-1.0)").
- Graphics: true HDR rendering, per-firework city lighting, shader replacement by hash (GRAPHICS.md order).
- Full playthrough of 30 charts (R-110) with the ultrawide layout pass (R-415).
- Stretch: R-904 full-width gameplay, R-905 visualizer from Windows audio, R-906 custom charts / auto chart generation, R-903 VR.

## Test plan additions
- **Test every menu change with both Menu Layout options** (Screen Edges and Centred). Two bugs this
  session only showed in one of them: headings carried off to the window edges, and menus jumping
  after a transition. Visit each screen at least twice: the first visit teaches the layout.
- High frame rate tests at 120 fps (not 144). The user's display is a 3440x1440 ultrawide, HDR, 120 Hz.
- After any particle work: a full busy song at Particle Density 5x, then read the `[particles]` lines.
- After any scroll-speed work: change speed mid-song both ways, check the first arrow of a song,
  and compare hit timing numbers before and after.

## Things learned the hard way this session
**Particles.** There are three pools and only one is the sparks. `[0x821C0518]` (32,768 slots) holds
rockets and bursts; `[game+0x74]` (16,384) holds trail segments; the sparks live in the system at
`[0x821C051C]`: alive count `+0x2814`, hard stop at 70,000 (also the exact size of its vertex
buffers), blocks of 128 at `+0x2004` (max 2,048). A firework's `Count` becomes the emitter's float at
`+0x1A8`; `sub_820E5D10` / `sub_820E5FB8` emit. Running *at* 70,000 crashed twice (GPU handed garbage
shaders: `ShaderTranslator ... Unsupported write to export`). The port stops adding at 68,000
(`--particle_limit`). 1x already reaches about half the limit in busy moments; from about 3x the
total is capped, so higher multipliers only change which fireworks get sparks. **Keep the
`[particles]` log lines** - the user asked for them to stay so a later crash can be recognised.

**A crash can poison the shader cache.** Garbage shaders get saved to
`Documents\boomboomrocket\cache\shaders` and then every start dies in "Initializing shader storage".
The port now leaves `cache\starting.txt` during start-up and clears the shader folder if the last
start never finished. Manual cure: delete that folder.

**Scroll speed.** A note (0x64 bytes, built by `sub_82065E70`, ctor `sub_82065958`) has launch time
`+0x1C`, start `+0x20`, end `+0x2C`, flight time `+0x38`, launched flag `+4`. `sub_82064978` walks
every note every frame and calls its check `sub_82064C50` (vtable `0x82000A34` slot 2), which
launches when song time is *past* `+0x1C`. Launch makes a pool entry ("rocket": `+0x3C` flown,
`+0x40` left = flight + half the hit window) and an arrow icon (`sub_8206A948`: `+0` rocket, `+4`
note, `+0x38` flight). `sub_8206A328` places the icon at start + (end - start) x f(flown / flight).
So flight time alone sets the speed. Lessons:
- Rewriting a launch time to exactly "now" never launches (strictly-greater test): use now - 1 ms.
- Notes whose launch falls before the song starts are built by the game part-way up with a
  shortened flight (`0x820687C0`); restore the full path from a same-lane note before scaling.
- `0x821BE9C8` is the previous frame's song time (the start time while a chart loads).

**Menu scripting.** Button `text` takes a `UIText.<KEY>` value (or the special `"STR<n>"` stored
string), not the key's name. Button icons are written `~~{LB}~~`, `~~{RB}~~`, `~~{A}~~` ... in the
string table. Lua x is in a 640-wide layout whose 320 is the screen centre: a 16:9 screen runs from
about -106 to 746, and the song bars end at about 555. Font sizes: 18 / 24 / 32 / 48 (title). The
screen title style is `new_piped_orange`. A script wraps a game screen by loading
`orig:<same name>` and then overriding `Init` / `Update`. Stored string slots in use: 118 offset
suggestion, 119 speed panel, 120-123 hit readout, 124/125 song bar texts, 126 results line, 127 speed
notice. There are only 128 (0-127); the game uses low numbers per song, so new ones go below 118.

**Edge anchoring.** Sides are decided per frame from quad positions, so a screen that is still
arriving reads differently from the finished one (the main menu holds an in-between arrangement for
about 0.3 s; text with flying letters is "too wide to place"). The finished layout is remembered per
"<last script>|<widget count>" in `cache\ui_layout.txt`, learned only from what is still there 1.5 s
after loading, and applied during arrival to wide items as well. Scripts can exempt a rectangle with
`BBRR("bbrr.ui.keep", "x0,y0,x1,y1")`. The log has a line per layout change during arrival.
Kept so far: song select headings and speed panel, the HUD hit readout band (it sits over the
arrows, which never move), and the results screen above y = 400 (box, labels, values, Rating).
The port ships finished layouts in `ui_scripts/ui_layout.txt` (copied from the user's
`cache\ui_layout.txt` on 2026-11-11, 30 screens); the player's own cache is read after it and
wins. After the full playthrough (R-415), copy the cache over it again so every screen ships.

**Beat guides (R-804, dropped).** Measure lines on the beat grid, then a bar through every arrow
(Timing Bars), were built and removed on 2026-11-11 - the user did not want either. Why lines on a
grid cannot work: see "How arrows move".

**How arrows move.** `sub_8206A328` places an arrow at start + (end - start) x u with u = (flown / flight)^1.5
across and ((flown / flight)^1.5)^0.7 upwards (constants at `0x82000D38` = 1.5, `0x820008D0` = 0.7) - a
rocket picking up speed. It tests byte `0x821A01C4` to skip the easing, but that byte is the top of a
32-bit value used for something else (`0x8099FFF0` in the image): **never write it** (doing so made the
easing flip unpredictably). Rocket Acceleration Off (R-809) uses a mid-asm hook at `0x8206A418`, where
both branches meet, to set f31 = f11 = f30 (flown / flight). Each note's flight time is its own path
length over the song's rocket speed (zone and zoom distance differ), 3.2-5.7 s within one song.

**Shader microcode in GPU memory vs the .xpu files.** Guest memory holds the microcode words in the
file's byte order; the emulator's `Shader::ucode_dwords()` are host-endian, i.e. those bytes read
big-endian. D3D renumbers some registers when a shader is created: PointSprite, UiFlorescence and
UiMultiTexture match their files exactly, TextureModDiffuse (city, `9567C79307ACC6F5`) on 10 of 12
words. The shader cache `Documents\boomboomrocket\cache\shaders\shareable\5841086A.xsh` (header 8 bytes,
then per shader: u64 hash, u32 dword count | type << 31, dwords in guest byte order) is handy for
checking this offline.

**No Background (R-808) and SDK patch 0005.** `rex/graphics/hidden_shaders.h`: the port gives the GPU
side a list of (vertex shader hash, pixel shader hash) pairs, 0 = any, and while hiding is on
`IssueDraw` skips matching draws. The same patch logs a draw list on request (`RequestDrawDump`,
frames delimited in `IssueSwap`): the port asks for two frames the first time No Background is on in
a song. The GPU backend is a separate DLL (`rexgpu-xenos.dll`) whose symbols the game cannot link
to, so anything the game and the GPU code share must live in rexruntime (which exports
everything): the registry is in `src/ui/d3d12/d3d12_presenter.cpp`, next to the glow mask.
Matching by shader file failed: the city's shader in memory matches none of the game's files, and
`9567C79307ACC6F5` (SimpleShaders.fxobj, generic textured) is the arrows' (with vertex shader
`436FD4BB733B0BD5`). The draw list of one frame (2026-11-11), in order: clears (`0A6D/2E37`), sky
(`0C88/0AA0`), backdrop (`BBFF/9567`), city mesh pass 1 (`C837/2B08`, `6773/88B6` between), point
sprites, city mesh pass 2 (`04F2/C9D2`), city details (`D1ED/238C`, ~1000 draws), sky dome (`04F2/B36F`, also drawn before the city) and
water quad (`B90D/FB9F`) - added after the first test still showed the sky -, sparks
(`63B4/4B1E`), glow passes (`1121/...` rectangles), fireworks (`1DB9/C471`), arrows (`436F/9567`),
HUD (UiMultiTexture `87A9/3F1B`, UiFlorescence `DE79/8D93`). The sky, backdrop and city pairs are the
default of `--no_background_draws` (`src/modifiers.cpp`). Confirmed working 2026-11-11. Switched on from the Get Ready countdown (screen scripts, `bbr::NoteScreenForNoBackground`) or while game state (`0x821A0578`+4) is 1 or 4. `patch-sdk.ps1`
also cleans files that patches add, so 0005's new header does not block a fresh re-apply.

**Popups over menus.** A `Popups\*` script adds its widgets after the covered screen's; while it is
up, the covered screen's items keep that screen's finished sides (`g_under_signature`).

**First notes at higher scroll speed.** The game shortens a note whose launch would fall before the
song start by sliding its start point along the straight path (`0x820687C0`: start' = end -
(end - start) x shortened / full). The fix restores start and flight from a full-length note with the
same end whose path passes through start' (any note kind). Log: `[modifiers] first note at ...`.

**Offset suggestion (R-807).** `bbrr.hit.advice` (src/input_offset.cpp) writes STR118 when the
song had 20+ hits, an average 15+ ms from zero and 70%+ of hits on that side; suggested Input
Offset = current - average, rounded to 5 ms, within +-100 ms. Shown below the timing line (or in
its place when Hit Timing is off) by ui_scripts/Ingame/LevelBreakdown.lua. Every results screen
logs an `[input] offset advice` line, including why nothing was shown.

**Timing.** The user's hits were steadily 36-44 ms late and the game's own frame timing agreed, so
it was an offset, not jitter; raising Input Offset fixed it. Late hits: raise Input Offset (or Audio
Offset if notes look out of step with the music). Positive Audio Offset = notes arrive later.

## How the work is done
- Code is edited in the Linux sandbox that sees `F:\Boom Boom Rocket`; it cannot run Windows
  builds. The user builds with `powershell -ExecutionPolicy Bypass -File scripts\build.ps1` and
  tests; the log is `boomboomrocket.log` in the project folder and is the main source of evidence.
- Give commands one per code block. Say plainly when something is built but not run.
- Never commit `Game Files`, `DLC` or `calibration_sounds\*.wav`. Commit author
  `CorrieDivitt <CorrieDivitt@users.noreply.github.com>`.
- Game code is hooked with `REX_HOOK_RAW(sub_X)` (works for virtual calls too, the function table
  uses the same symbol) and `[[midasm_hook]]` entries in `config/default.toml`; SDK changes live in
  `patches/rexglue-sdk/` and are applied by `scripts/patch-sdk.ps1`.
- After changing `docs/requirements.py`, run it and refresh the dashboard's data file (below).

## The requirements dashboard and its data file
- **Dashboard:** the Claude artifact at https://claude.ai/artifact/4jmcgAaAJwoWTm14PHEFiH
  (artifact id `1e44263b-9291-45d4-a988-91dc12c0e8da`), private to the project owner.
- **Source of truth:** `docs/requirements.py` in the repo. Running `python3 docs/requirements.py`
  rewrites `docs/requirements.csv` and `docs/requirements.json` beside it.
- **The data file the dashboard reads:** `docs/requirements.csv`
  (`F:\Boom Boom Rocket\docs\requirements.csv` on the PC), uploaded to the artifact as an asset.
  The artifact's database document `datasets/requirements` points at the uploaded copy through its
  `source.url` field (a `/_blob/<id>` address that changes with every upload).
- **To refresh it:** run the script, upload the new `requirements.csv` to the artifact as an asset,
  then update `datasets/requirements` so `source.url` is the new `/_blob/<id>` (and bump the
  `edit` / `updated` times). The update needs the document's current version number; it was 46
  after the last refresh on 2026-11-11, so read the document first to get the live value.
