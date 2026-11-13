# Handoff notes (updated 2026-11-13, after the launcher and release-prep session)

For whoever picks the project up next, human or Claude. The other docs go deeper on one subject
each: `PLAN.md` (phases), `TIMING.md` (song clock, judging, end of song), `GRAPHICS.md` (shaders,
HDR, particles), `DLC_PROTOTYPES.md`, `ANALYSIS.md` (game internals), `USER_GUIDE.md` (what the
player sees). The requirements list (`requirements.py` -> csv/json and the dashboard) is the task
list; its current state is copied into the **Tracker** section at the end of this file every time
`python3 docs/requirements.py` runs. `SUBMISSION.md` covers the store listings.

## Session 2026-11-13 (night): tracker tidy-up, no game testing
Built and confirmed by the user the same day:
- **Prototype DLC (R-112, done):** unlock icons drawn from the pack's own shapes (`src/dlc.cpp`),
  seen working. The fix for its French/German/Italian/Spanish titles (the files used pack 1's
  keys, clashing with the Rock Pack) is in too but untested: post-release check R-908.
- **Texture Filtering (R-406, done):** Video Settings row (Off/2x/4x/8x/16x) on the SDK's
  `anisotropic_override`; also in the launcher. Distant highlights look more subtle; little
  difference above 4x (the default).
- **Visualizer pull-back (R-909), A confirmed working; X (twice as far) built, not run:** hold A
  to move the camera back and up
  (`src/visualizer_zoom.cpp`, mid-asm hook at 0x82088B78). The first try, X widening the field of
  view after the camera function returned, did nothing: the camera is built inside that function.
  The second try moved the look-at point (taken for the eye) and only tilted the view: in
  sub_82088580 the camera position is game + 0x1044C, sitting ahead on the path, and r1+112 is
  what it looks at. Now the position is moved away from the look-at point.
- **Water Reflections (test) is back** in Video Settings (off by default), at the user's request,
  to look at from the pulled-back Visualizer view (R-417). Seen working only from steep views;
  **SDK patch 0022** (run `scripts\patch-sdk.ps1`) gives the mirrored bursts their own reflected
  matrix; tested, no real change (the mirror matrix was already a plain reflection). **Patch 0023**
  turns user clip planes off for the copy; tested, no change (there were none). **Patch 0024** logs
  where the normal sparks and the copy land on screen (`[bbrr] reflection probe`) so the next step
  is based on measurements (GRAPHICS.md, Water reflections). Built, not run.
Closed from code alone: R-007 (asset formats, ANALYSIS.md section 5), R-108 (both remaining XAM UI
stubs are unreachable or harmless; the game discards their results), R-211 (covered by R-905).
R-801 moved to the stretch goals; phase 8 is done. Phase 1 now waits only on play testing:
R-109 (Practice, Freestyle, 2P Battle, 2P Endurance not yet seen to the end) and R-110.

## Latest session (2026-11-12 evening to 2026-11-13)
Committed locally as `5441f95` and `1cb40fe` (plus the tracker/handoff commit after them); the user
pushes from their own terminal. Confirmed by the user unless marked otherwise.
- **Launcher (R-606, done):** `launcher/` builds `BoomBoomRocketRevolution.exe` (Dear ImGui on
  Direct3D 11, from the SDK's imgui copy; target `bbrr_launcher` in CMakeLists.txt).
  Setup tab: Browse for the game (default.xex in an unpacked folder, or the original XBLA package,
  read directly as STFS), checked against `launcher/game_files.inc` (327 files with SHA-256 of the
  retail copy; unknown versions allowed with a warning); Add DLC (any package for title 5841086A or
  the test title 58410859, prototypes allowed; drag and drop works); "Ready to install", then
  Install copies into the launcher's own folder (`Game Files`, `DLC`). The user chose the word
  "Install" (nothing is compiled on the player's PC: the release ships the built game).
  Game Settings tab: Player 1 gamertag (profiles.ini), the Video Settings rows, Menu Layout, Skip
  Intro, saved to settings.ini at once. Play starts `boomboomrocket.exe`; `--play` skips the window.
  The game offers to open the launcher if no game files are found. File dialogs default to all
  files (packages have no extension). Logic in `launcher_core.cpp` is plain C++ and was tested on
  Linux against the real files (`launcher/core_test.cpp`).
- **Shared settings table:** `src/settings_table.h` holds every setting (cvar, choices, default,
  page); the game and the launcher both read it.
- **Window Mode:** Fullscreen / Windowed / Borderless Window. Cvar `window_mode` (port) sets
  `fullscreen` and the new SDK cvar `window_borderless` (SDK patch 0021). Old settings files with
  only `fullscreen = false` become Windowed. Confirmed working.
- **Packaging:** `scripts\package.ps1 -Version x.y.z` builds
  `out\release\BoomBoomRocketRevolution-v<ver>-windows-x64.zip` (launcher, game, DLLs, ui_scripts,
  docs), a `.sha256` and a symbols zip; it refuses game data. Not run yet (next step: unzip into an
  empty folder and set up from scratch).
- **Store prep (R-708):** `docs/SUBMISSION.md` with Quiver catalog values (folderName
  `BoomBoomRocket-Revolution`, asset filter `*windows-x64.zip`, exe `BoomBoomRocketRevolution.exe`)
  and RecompStore text. Open: screenshots/logo of our own, the AI-use answer, README install section.
- **Water Reflections:** option removed, forced off in code (R-417, post-release). Brought back as a test option 2026-11-13.
- **Endurance (R-114):** song clock now keeps its measured tempo across pauses and lap loops
  (camera stutter after a pause); Autoplay still misses the first notes of each lap: parked by the
  user as a known post-release bug.
- **Next, in the order agreed:** test release (package, unzip, set up from scratch), full
  playthrough (R-110, with Camera Speed and leaderboards checks), README/screenshots/AI-use answer,
  tag v1.0 (R-707), submit to Quiver and RecompStore (R-708). Later: R-704, R-405, R-701.

## Where things stand
As of 2026-11-11 (see the latest session above for later work), everything below was built, pushed (`fb76196`) and confirmed in play
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
  wording now "Mostly early. Try lowering Input Offset a little (currently +15 ms)".
- Widescreen fixes built 2026-11-11, untested: shipped layouts (`ui_scripts/ui_layout.txt`) so
  first visits (pause menu) no longer snap late; hit readout band and results screen kept centred.
- Built 2026-11-11 (later): Rocket Acceleration (R-809, seen working) and No Background (R-808,
  confirmed working) under Modifiers; Hit Timing renamed Show Accuracy; first-arrow fix at higher speeds (R-802); pause menu keeps its place under a
  dialogue (R-415). **No Background needs SDK patch 0005: run `scripts\patch-sdk.ps1` before
  `scripts\build.ps1`.**
- Committed and pushed 2026-11-12 as `fb76196` (everything up to No Background). Next commit
  (2026-11-12), all confirmed in play: Particle Density 0x; Horizontal Drift Default/Off (R-810,
  reworked for camera swings, see below); Autoplay (R-811); Rocket Acceleration Default/Static;
  No Background renamed Background (Default/Off).
- Second 2026-11-12 commit: DDR Mode (R-812, four lanes by button, spacing 0.08); Scroll Speed
  0.25x-4x in 0.25 steps, slow speeds fixed by retiming every waiting note (`RetimeWaiting`; the
  game only checks a note once its launch time has come) and never launching before the song
  start; first-note fix now at 1x too; Autoplay masks the player's A/B/X/Y while the song clock
  moves and blocks achievements (hook on XUserWriteAchievements, sub_820E7478). User is done with
  modifiers for now.
- Third 2026-11-12 commit: SDK patch 0006 (run `scripts\patch-sdk.ps1`): in-game Achievements menu
  (XamShowAchievementsUI as an opaque XAM dialog, per player), ImGui overlays bright in HDR
  (immediate drawer HDR pixel shader), local profiles (KernelState::SignInUser, per-XUID title
  data and achievements). Game: src/profiles.cpp + BBRR Settings > Profiles (profiles.ini).
  Then: Player 1 Controller row (SDK patch 0007, controller order by device key) and the
  Visualizer fed by the PC's sound (src/visualizer.cpp: XMPCaptureOutput answered, WASAPI
  process loopback, Visualizer Sensitivity), both confirmed. Next candidates: keyboard (R-602),
  controllers (R-601), full playthrough (R-110).
- Then keyboard (R-602, done): SDK keyboard emulation on by default with W A S D / arrows
  (KeyboardDefaults in src/settings.cpp), Keyboard page with rebinding (src/keyboard.cpp),
  LB/RB speed steps read from the game's own controller read (any pad, keyboard Q/E).
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
easing flip unpredictably). Rocket Acceleration (R-809) uses a mid-asm hook at `0x8206A418`, where
both branches meet, to set f31 = f11 = f30 (flown / flight) when it is set to **Static**; **Default** (the
default) leaves the game's own movement. Named by what it does: on screen the original can look
steady and the even pace like speeding up (the climb's perspective). Each note's flight time is its own path
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

**Horizontal Drift (R-810).** A rocket flies a straight 3D path from its launch point to its
detonation point; `sub_8206A328` computes the new position (r1+80: x, y, z) and stores it in the
rocket (+68..+76, movement since last frame at +80..+88) - trail, arrow and firework all follow the
rocket. With the setting off, a mid-asm hook at `0x8206A4C8` solves x from the view matrix (game
`0x821C1420` + `0x103B0`, four rows of 16 bytes; the one `sub_8206A600` draws the icons with) so the
rocket stays in its launch point's screen column. Matrix order (p x M or M x p) is checked against
the icons' own clip coordinates in `BbrArrowIconHook` before first use.

**Autoplay (R-811).** `src/autoplay.cpp`. The controller read is `sub_820E6E50` (user, XINPUT_STATE*,
a wrapper for XamInputGetState; buttons at state+4); the press-to-rocket match in `sub_82066700`
compares a press's +16/+20/+24 with the rocket's bytes 0 / 8 (button) / 11 (player). Rocket byte 8 is
0/2/4/6 (byte 1 the matching bit); arrow colour = button and the game lists colours Green, Red,
Blue, Yellow, so byte 8 / 2 -> A, B, X, Y. The game reads the pad ~190 times a second (more than
once a frame), so a button is held 35 ms, then up 30 ms before it can go down again. Judging is centred on the rocket's
time only for a rocket Autoplay pressed and that is due now (within 50 ms). Score blocks:
`src/leaderboards.cpp` (XSessionWriteStats) and `LevelBreakdown.lua` (`GUI.save_needed`).

**Popups over menus.** A `Popups\*` script adds its widgets after the covered screen's; while it is
up, the covered screen's items keep that screen's finished sides (`g_under_signature`).

**First notes at higher scroll speed.** The game shortens a note whose launch would fall before the
song start by sliding its start point along the straight path (`0x820687C0`: start' = end -
(end - start) x shortened / full). The fix restores start and flight from a full-length note with the
same end whose path passes through start' (any note kind). Log: `[modifiers] first note at ...`.

**Offset suggestion (R-807).** `bbrr.hit.advice` (src/input_offset.cpp) writes STR118 when the
song had 20+ hits and one side (early or late) outnumbers the other by more than 1.5 times, whatever the average (changed 2026-11-12); it says which way to
move Input Offset (raise for late, lower for early) and the current setting, no target number
(changed 2026-11-12: one song is too rough to aim at a value). Shown below the timing line (or in
its place when Hit Timing is off) by ui_scripts/Ingame/LevelBreakdown.lua. Every results screen
logs an `[input] offset advice` line, including why nothing was shown.

**Timing.** The user's hits were steadily 36-44 ms late and the game's own frame timing agreed, so
it was an offset, not jitter; raising Input Offset fixed it. Late hits: raise Input Offset (or Audio
Offset if notes look out of step with the music). Positive Audio Offset = notes arrive later.

## How the work is done
- Pushing: the shell that sees `F:\Boom Boom Rocket` can commit but GitHub refuses pushes from it;
  the user runs `git push` in their own terminal. Git there cannot delete its own lock files: after
  a commit, rename any `.git\HEAD.lock`, `.git\index.lock`, `.git\objects\maintenance.lock` to
  `stale-...` directly under `.git`, or the user's git fails with "File exists".
- Copying files to the PC: check the written file's hash afterwards (once an old version was
  written while the tool reported success). Seen again 2026-11-13: committing a second
  version from the same staged file name wrote the first one; stage each version under a new name.
- PowerShell scripts: always give the full `powershell -ExecutionPolicy Bypass -File scripts\x.ps1`
  form; `.\scripts\x.ps1` fails on the user's machine.
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
  `edit` / `updated` times). The update needs the document's current version number (79 after the
  refresh on 2026-11-13); read the document first to get the live value.

**Horizontal Drift Off (R-810).** Mid-asm hook at 0x8206A4C8 (`BbrArrowDriftHook`, rocket's new
position at r1+80..88; icon at r31: +0 rocket, +32 path start, +44 firework point). The rocket keeps
the firework's x and z, then is slid along the camera's left-right direction (the gradient of clip
x, from the view matrix at game + 0x103B0, order checked by `NoteArrowProjection`) to the screen
column it first appeared in, kept per icon. Do not solve world x for a screen column: when the
camera swings that becomes ill-conditioned and the arrows fly about (seen 2026-11-12).

## Tracker
<!-- tracker:start (written by docs/requirements.py; do not edit by hand) -->
101 requirements: 4 in progress, 30 todo, 67 done. Details and notes per item: docs/requirements.csv or the dashboard.

| ID | Title | Priority | Status |
|---|---|---|---|
| **Phase 0** | **Foundation & analysis** | | |
| R-001 | Verify game dump (default.xex + content) | P0 | done |
| R-002 | Decrypt/decompress XEX to PE image | P0 | done |
| R-003 | Kernel/XAM import inventory vs ReXGlue | P0 | done |
| R-004 | Repo scaffolding + ReXGlue SDK submodule | P0 | done |
| R-005 | Install Windows toolchain | P0 | done |
| R-006 | Build & install ReXGlue SDK | P0 | done |
| R-007 | Document asset formats | P1 | done |
| R-008 | No copyrighted game data in repo | P0 | done |
| R-009 | Static analysis setup (Ghidra/IDA + XEX loader) | P1 | done |
| **Phase 1** | **Recompile & boot** | | |
| R-109 | All modes playable | P0 | in progress |
| R-110 | All 10 tracks x 3 difficulties complete | P0 | todo |
| R-101 | rexglue init project + codegen config | P0 | done |
| R-102 | Clean codegen of all functions | P0 | done |
| R-103 | Locate setjmp/longjmp (if used) | P1 | done |
| R-104 | Boot to title screen (D3D12) | P0 | done |
| R-105 | VFS mounts: GAME:, game:, DLC%d: | P0 | done |
| R-106 | Full-license / trial flag | P0 | done |
| R-107 | Save data & profile settings persistence | P0 | done |
| R-108 | Stub missing XAM UI calls | P1 | done |
| R-111 | DLC support (owned track packs) | P0 | done |
| R-112 | Clean up prototype DLC packs | P2 | done |
| R-113 | Skip start-up logos | P1 | done |
| **Phase 2** | **Audio & music sync** | | |
| R-203 | Native low-latency music path | P2 | todo |
| R-208 | Per-song offset overrides | P2 | todo |
| R-210 | Audio device & buffer options | P1 | todo |
| R-212 | Timing validation across audio/PC setups | P0 | todo |
| R-201 | XMA1 music & SFX playback | P0 | done |
| R-202 | Identify the game's song clock | P0 | done |
| R-204 | Audio-clock-driven beat timeline | P0 | done |
| R-205 | Global audio offset setting (ms) | P0 | done |
| R-206 | Input latency offset setting (ms) | P0 | done |
| R-207 | Calibration screen (tap to the beat) | P1 | done |
| R-209 | Sync debug overlay | P1 | done |
| R-211 | Visualizer with user music files - covered by R-905 | P2 | done |
| R-213 | Smooth arrow motion (interpolated song clock) | P0 | done |
| R-214 | Game volume boost | P2 | done |
| **Phase 3** | **Frame rate & timing** | | |
| R-302 | Unlocked frame rate (120/144/240+) | P0 | in progress |
| R-305 | Low-latency presentation | P1 | todo |
| R-301 | Determine game loop timing model | P0 | done |
| R-303 | Fixed-step sim with render interpolation | P1 | done |
| R-304 | Frame pacing matched to the display | P0 | done |
| R-306 | High-rate timestamped input | P0 | done |
| **Phase 4** | **Graphics & resolution** | | |
| R-405 | Anti-aliasing (MSAA / SMAA / FXAA) | P1 | in progress |
| R-408 | Native replacement shaders | P1 | todo |
| R-412 | D3D9-level renderer replacement | P2 | todo |
| R-413 | Texture dump & replacement packs | P2 | todo |
| R-415 | Ultrawide menu/HUD layout pass | P1 | todo |
| R-401 | Arbitrary resolutions | P0 | done |
| R-402 | Aspect ratios 16:9 / 16:10 / 21:9 / 32:9 | P0 | done |
| R-403 | Internal resolution scale | P1 | done |
| R-404 | Window modes & display select | P0 | done |
| R-406 | Anisotropic filtering | P2 | done |
| R-407 | Recover HLSL from .updb shader PDBs | P1 | done |
| R-409 | HDR bloom & tonemapping for fireworks | P1 | done |
| R-410 | Improved city lighting | P2 | done |
| R-411 | Particle quality/quantity scaling | P1 | done |
| R-414 | In-game Revolution settings menu | P0 | done |
| R-416 | Track length on the song lists | P2 | done |
| **Phase 5** | **Leaderboards & achievements** | | |
| R-501 | Local achievements from SPA | P1 | done |
| R-502 | Local leaderboards | P0 | done |
| R-503 | Implement XUserReadStats / XSessionWriteStats / stats enumerator | P0 | done |
| R-506 | Player identity / profile | P1 | done |
| **Phase 6** | **Input, UX & settings** | | |
| R-603 | Button glyph swap | P2 | todo |
| R-601 | Controller support (XInput, DualSense, Switch Pro) | P0 | done |
| R-602 | Keyboard support & rebinding | P0 | done |
| R-604 | Local 2-player | P1 | done |
| R-605 | Settings persistence | P0 | done |
| R-606 | Launcher: game files, DLC, settings, play | P1 | done |
| **Phase 7** | **QA, CI & release** | | |
| R-701 | GitHub Actions Windows build | P1 | todo |
| R-702 | Input replay regression tests | P1 | todo |
| R-703 | Sync accuracy test | P0 | todo |
| R-704 | Performance target | P1 | todo |
| R-705 | Build & user guides | P1 | todo |
| R-707 | Public release v1.0 | P0 | todo |
| R-708 | Store listings: Quiver Launcher and RecompStore | P1 | todo |
| R-706 | Licensing decision for project code | P0 | done |
| **Phase 8** | **Modern rhythm features** | | |
| R-802 | Note speed / scroll-speed modifier | P2 | done |
| R-803 | Timing feedback (early/late, ms offset) | P2 | done |
| R-806 | Average timing on the results screen | P2 | done |
| R-807 | Offset suggestion on the results screen | P2 | done |
| R-808 | Background setting (practice) | P2 | done |
| R-809 | Rocket Acceleration setting | P2 | done |
| R-810 | Horizontal Drift setting | P2 | done |
| R-811 | Autoplay | P2 | done |
| R-812 | DDR Mode | P2 | done |
| **Phase 9** | **Stretch goals (post-1.0)** | | |
| R-909 | Visualizer camera pull-back (A, X twice as far) | P2 | in progress |
| R-114 | Endurance mode: issues found in testing | P1 | todo |
| R-417 | Fireworks reflected in the water | P2 | todo |
| R-504 | Online leaderboard service | P1 | todo |
| R-505 | Score integrity checks | P2 | todo |
| R-801 | Research modern rhythm-game features | P1 | todo |
| R-805 | Practice tools (section loop, slow-down) | P2 | todo |
| R-902 | Accessibility review | P2 | todo |
| R-903 | VR support | P2 | todo |
| R-904 | Gameplay across the full screen width | P2 | todo |
| R-906 | Custom charts and automatic chart generation | P2 | todo |
| R-907 | True HDR rendering (float render target) | P2 | todo |
| R-908 | Prototype DLC titles in other languages | P2 | todo |
| R-804 | Beat guides (measure lines / timing bars) - dropped | P2 | done |
| R-901 | Reduce background/camera motion option | P2 | done |
| R-905 | Visualizer driven by audio playing in Windows | P2 | done |
<!-- tracker:end -->
