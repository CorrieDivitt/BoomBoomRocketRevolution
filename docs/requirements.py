"""Source of truth for the requirements tracker.
Edit the REQS list (status / notes) and run:  python docs/requirements.py
It regenerates docs/requirements.csv and docs/requirements.json, which the dashboard reads."""
import csv, json, os
PHASES = {
 0: "Foundation & analysis", 1: "Recompile & boot", 2: "Audio & music sync",
 3: "Frame rate & timing", 4: "Graphics & resolution", 5: "Leaderboards & achievements",
 6: "Input, UX & settings", 7: "QA, CI & release", 8: "Modern rhythm features", 9: "Accessibility (stretch)"}
# id, phase, category, title, priority, status, effort, acceptance, depends_on, notes
REQS = [
 ("R-001",0,"Analysis","Verify game dump (default.xex + content)","P0","done","S","default.xex parses; TitleID 5841086A; all 10 tracks present","", "XEX2, XDK 2.0.5426, image base 0x82000000, entry 0x820E83D8"),
 ("R-002",0,"Analysis","Decrypt/decompress XEX to PE image","P0","done","S","tools/xex/unxex.py produces 3,276,800-byte PE starting with MZ","R-001","Retail AES + LZX (window 2^15)"),
 ("R-003",0,"Analysis","Kernel/XAM import inventory vs ReXGlue","P0","done","S","166 imports listed with implementation status","R-002","162/166 implemented in ReXGlue v0.10.1; 4 XAM gaps"),
 ("R-004",0,"Setup","Repo scaffolding + ReXGlue SDK submodule","P0","done","S","git repo with docs, tools, .gitignore excluding game data; SDK at thirdparty/rexglue-sdk","", "Remote: github.com/CorrieDivitt/BoomBoomRocketRevolution (push pending)"),
 ("R-005",0,"Setup","Install Windows toolchain","P0","done","S","VS 2022 + Clang 20 (clang-cl), CMake 3.25+, Ninja on PATH; scripts/bootstrap.ps1 passes checks","", ""),
 ("R-006",0,"Setup","Build & install ReXGlue SDK","P0","done","M","cmake --preset win-amd64 builds; rexglue.exe runs","R-005","Built on VS 2026 + LLVM 22; SDK v0.10.1 Release install"),
 ("R-007",0,"Analysis","Document asset formats","P1","in_progress","M","Spec for .lvl/.spl/.mdl/.config/.bbr/.xpr/XMA in docs/ANALYSIS.md","R-001",".lvl/.spl/.config are text; .bbr/.xpr binary; audio XMA1 48kHz stereo"),
 ("R-008",0,"Legal","No copyrighted game data in repo","P0","done","S",".gitignore blocks Game Files/, assets/, *.xex, *.xma, *.xpr; CI never needs assets","", "Users supply their own legally-owned dump"),
 ("R-009",0,"Analysis","Static analysis setup (Ghidra/IDA + XEX loader)","P1","todo","S","Image loads with symbols for imports; pdata functions applied","R-002","Used for game-loop/timing reverse engineering"),

 ("R-101",1,"Recomp","rexglue init project + codegen config","P0","done","S","boom_boom_rocket_config.toml points at assets/default.xex; codegen target exists","R-006",""),
 ("R-102",1,"Recomp","Clean codegen of all functions","P0","done","L","Codegen with no validation errors (no --force); ~3,002 pdata functions + discovered extras","R-101","Clean after 3 [functions] overrides in config/default.toml; 51 files generated"),
 ("R-103",1,"Recomp","Locate setjmp/longjmp (if used)","P1","done","S","Addresses set in TOML or confirmed unused","R-009","setjmp 0x82167220, longjmp 0x821675A0 (used by libjpeg error handling)"),
 ("R-104",1,"Runtime","Boot to title screen (D3D12)","P0","done","L","Splash, legal screens and main menu render correctly","R-102","Boots past EA logo to menus on D3D12 (Xenos plugin); fixes in config/default.toml"),
 ("R-105",1,"Runtime","VFS mounts: GAME:, game:, DLC%d:","P0","done","S","All Failed-to-load paths resolve; case-insensitive lookups","R-104","Game files load via auto-detected game_data_root"),
 ("R-106",1,"Runtime","Full-license / trial flag","P0","done","S","XamContentGetLicenseMask reports full game; IsTrialVersion false","R-104","App sets license_mask=1; verified unlocked 2026-11-10 (use --license_mask=2 to test trial)"),
 ("R-107",1,"Runtime","Save data & profile settings persistence","P0","todo","M","Unlocks/scores survive restart (XamContentCreateEx, XamUser*ProfileSettings)","R-104",""),
 ("R-108",1,"Runtime","Stub missing XAM UI calls","P1","todo","S","XamShowAchievementsUI / MarketplaceUI / GamerCardUIForXUID return gracefully (or open native overlay)","R-104","Not implemented in ReXGlue v0.10.1"),
 ("R-109",1,"Gameplay","All modes playable","P0","in_progress","L","Standard, Endurance, Practice, Freestyle, Visualizer, 2P Battle, 2P Endurance complete without crash","R-104","First full song played with no obvious issues (2026-11-10)"),
 ("R-110",1,"Gameplay","All 10 tracks x 3 difficulties complete","P0","todo","M","30 charts finish with correct results screen and grade","R-109",""),
 ("R-111",1,"Content","DLC support (owned track packs)","P0","done","L","Users who own the Boom Boom Rocket DLC can point the app at their own DLC package file: it is installed, licensed and mounted, the extra tracks appear and play; no DLC content shipped","R-105","Done: original LIVE package files in a DLC folder (or --dlc_path) are unpacked on startup via the SDK content manager; folder contents decide what is active (src/dlc.cpp). Rock Pack verified by user 2026-11-10"),
 ("R-112",1,"Content","Clean up prototype DLC packs","P2","todo","M","Unreleased prototype packs (Boom Rocket Test 2, early Rock Pack build) load with proper song/pack names, their own unlock icons or sensible placeholders, correct leaderboards, and every listed track, chart and model verified in play","R-111","Stretch goal. Test 2 already loads after automatic repair of its contents.txt (missing UnlockFirework keys, William_Tell_2 -> Beethovens_Fifth_2, borrowed icon sheet). Still to do: placeholder names, icon art, play-test all 5 tracks, try early Rock Pack (different XMA header), scrambled A8A6 copy is unreadable"),
 ("R-113",1,"Boot","Skip start-up logos","P1","in_progress","S","Start-up splash and legal screens are skipped or fast-forwarded; game reaches the title screen in a few seconds","R-104","--skip_intro (default on) runs the UI clock 16x until Main\\TitleScreen.lua is requested (src/intro_skip.cpp); untested"),

 ("R-201",2,"Audio","XMA1 music & SFX playback","P0","done","M","All 10 songs and 86 SFX/ambience files decode and play (runtime XMA decoder via FFmpeg)","R-104","Music + SFX play via runtime XMA decoder (SDL audio)"),
 ("R-202",2,"Sync","Identify the game's song clock","P0","done","M","Documented function(s) that compute current song time/beat and their time source","R-009","Song time = XAudio voice samples played / rate + 0.045 s (0x82077100, 0x82080B00); see docs/TIMING.md"),
 ("R-203",2,"Audio","Native low-latency music path","P2","todo","L","Songs pre-decoded to PCM and played via WASAPI (SDL3); reported output latency < 20 ms","R-201","Demoted from P0: sync is solved by compensating the host audio queue (R-204). Only worth doing for lower sound latency; --audio_maxqframes=4 already halves the queue"),
 ("R-204",2,"Sync","Audio-clock-driven beat timeline","P0","done","L","Song time follows the music actually being played; no drift over a full song","R-202","Done: song clock = XAudio voice position minus the host audio queue (audio_maxqframes x 5.33 ms, default 42.7 ms), --audio_sync_compensation (src/song_clock.cpp, docs/TIMING.md). User A/B test 2026-11-10: new behaviour feels good"),
 ("R-205",2,"Sync","Global audio offset setting (ms)","P0","in_progress","S","-200..+200 ms offset applied to judging and visuals","R-204","--audio_offset_ms works (default -20; user found -15..-25 best on USB headset). Still needs exposing in the settings menu (R-414)"),
 ("R-206",2,"Sync","Input latency offset setting (ms)","P0","done","S","Separate input offset shifts judgement windows only","R-204","Done: Input Offset in BBRR Settings > Calibration (--input_offset_ms, -100..100) shifts the press span used by the judging function 0x82066700; auto calibration sets it with the audio offset. Play-tested 2026-11-10: arrows stay with the music, only judging moves."),
 ("R-207",2,"Sync","Calibration screen (tap to the beat)","P1","in_progress","M","Guided sound tap test and picture tap test (as in band games) suggest the audio offset; later also the input offset","R-205,R-206","First version (untested): BBRR Settings > Calibration (ui_scripts/Options/BBRR_calibrate.lua): 12 taps to clicks, 12 taps to a ring lining up; audio offset = (sound lateness - picture lateness) - host audio queue. Picture lateness is logged for the future input offset (R-206)"),
 ("R-208",2,"Sync","Per-song offset overrides","P2","todo","S","Optional per-track offset table in user config","R-205","Ride of the Valkyries Easy already ships SongOffset 0.03"),
 ("R-209",2,"Sync","Sync debug overlay","P1","in_progress","S","Overlay shows audio time, game beat, delta, frame time","R-204","Hit Timing (per-hit +/- ms next to the arrow, like StepMania) and Assist Tick (click at each rocket's moment) in BBRR Settings > Calibration; [input] log prints average and spread every 40 hits. Needs play-testing."),
 ("R-210",2,"Audio","Audio device & buffer options","P1","todo","S","Choose device, buffer size, exclusive/shared mode, volume sliders","R-203",""),
 ("R-211",2,"Feature","Visualizer with user music files","P2","todo","L","Visualizer mode plays local mp3/flac/ogg via XMP replacement","R-203","Original used XMP + console music"),

 ("R-212",2,"Sync","Timing validation across audio/PC setups","P0","todo","M","Measured offset/drift on USB headset, onboard audio, HDMI/TV, Bluetooth, low-end PC; results logged","R-204","User plans to test multiple devices"),

 ("R-213",2,"Sync","Smooth arrow motion (interpolated song clock)","P0","done","M","Arrow scrolling is visibly smooth at 60/120/144 fps; hit timing unchanged; A/B via --smooth_song_clock","R-202","Verified 2026-11-10: arrows very smooth with src/song_clock.cpp (audio position stepped up to 21 ms)"),

 ("R-301",3,"Timing","Determine game loop timing model","P0","done","M","Documented: fixed 60 Hz step vs delta time; frame-count dependencies listed","R-009","Variable dt (audio-driven in songs, mftb in menus); only frame-locked item found: fade step 1/60 per frame"),
 ("R-302",3,"Timing","Unlocked frame rate (120/144/240+)","P0","in_progress","XL","Gameplay identical at 60/144/240 fps (replay test); no speed-up","R-301","120 and 144 fps tested OK (2026-11-10); fade timer now uses real frame time (src/frame_stats.cpp); needs 240/uncapped + replay check"),
 ("R-303",3,"Timing","Fixed-step sim with render interpolation","P1","todo","L","Simulation at 60 Hz, camera/particles interpolated per frame","R-301","Fallback if delta-time patching is impractical"),
 ("R-304",3,"Timing","Frame pacing matched to the display","P0","in_progress","M","No frame-rate setting needed: the game runs at the display's refresh rate with each frame started on a real display refresh; no judder; optional unlimited mode","R-302","Untested: guest refresh rate = main display refresh (src/settings.cpp ApplyFrameRate); SDK patch 0002 ticks the guest vblank from the desktop compositor (DwmFlush) with an evenly spaced timer fallback and no millisecond sleeps in the GPU wait. Before: 120 fps limit gave 5.5-12 ms frame times (judder) on a 120 Hz display"),
 ("R-305",3,"Timing","Low-latency presentation","P1","todo","M","Flip-model swapchain, waitable object, max 1 frame queued","R-304",""),
 ("R-306",3,"Input","High-rate timestamped input","P0","in_progress","M","Input polled >= 1 kHz on its own thread; presses timestamped against audio clock","R-204","--precise_input (default on): controller read every 1 ms on its own thread; a press is judged at the song time the button went down instead of by whole frames. XInput pads only; keyboard and other pads use the game's frame span. Needs play-testing and tuning of input_frame_credit_ms from the [input] log."),

 ("R-401",4,"Graphics","Arbitrary resolutions","P0","done","M","Any window/fullscreen resolution up to 4K+; correct projection","R-104","Verified 2026-11-10 at 1440p and 4K via --resolution_scale=2/3 (game renders 1280x720 x scale)"),
 ("R-402",4,"Graphics","Aspect ratios 16:9 / 16:10 / 21:9 / 32:9","P0","in_progress","M","Hor+ 3D view; UI anchored and not stretched","R-401","--aspect_ratio=auto: 3D view, menu shape, full-screen covers and edge anchoring work on 3440x1440 (src/widescreen.cpp). Arrow icons fixed (clip-space quad used a fixed 9/16 factor, hooked in sub_8206A600); follows window resize. Per-screen layout polish tracked in R-415"),
 ("R-415",4,"UI","Ultrawide menu/HUD layout pass","P1","todo","M","During the full playtest, every menu and HUD screen is checked at 21:9 and 32:9: side elements follow the window edges, nothing splits from its group or pops during animations; fixes via grouping rules or per-screen overrides (--ui_anchor_debug log)","R-402,R-110","Edge anchoring works on most menus but not all (user test 2026-11-10); do with the full playtest"),
 ("R-403",4,"Graphics","Internal resolution scale","P1","done","S","Render scale 0.5x-4x independent of output size","R-401","--resolution_scale / --draw_resolution_scale_x,_y (1-8); needs exposing in settings menu (R-414)"),
 ("R-404",4,"Graphics","Window modes & display select","P0","todo","S","Windowed, borderless, exclusive fullscreen; monitor choice","R-104",""),
 ("R-405",4,"Graphics","Anti-aliasing (MSAA / SMAA / FXAA)","P1","todo","M","Selectable AA; fireworks trails stay crisp","R-401",""),
 ("R-406",4,"Graphics","Anisotropic filtering","P2","todo","S","1x-16x setting applied to city textures","R-401",""),
 ("R-407",4,"Shaders","Recover HLSL from .updb shader PDBs","P1","done","S","9 HLSL sources recovered from 8 .updb files into extracted/ (git-ignored)","R-001","tools/shaders/extract_updb_hlsl.py; embedded XEX shaders still need XenosRecomp/disassembly"),
 ("R-408",4,"Shaders","Native replacement shaders","P1","todo","L","Loose + embedded shaders replaced with native HLSL equivalents, pixel-matched","R-407",""),
 ("R-409",4,"Lighting","HDR bloom & tonemapping for fireworks","P1","todo","L","Optional HDR pipeline with bloom; off = original look","R-408",""),
 ("R-410",4,"Lighting","Improved city lighting","P2","todo","L","Firework light reflected on city (dynamic point lights), optional","R-408",""),
 ("R-411",4,"Particles","Particle quality/quantity scaling","P1","todo","M","Multiplier on firework Count with stable perf","R-302","Fireworks/*.config hold Count/Velocity per grade"),
 ("R-412",4,"Graphics","D3D9-level renderer replacement","P2","todo","XL","Statically linked D3D9 calls hooked to a native renderer (bypassing GPU emulation)","R-408","Long-term path for full fidelity"),
 ("R-413",4,"Textures","Texture dump & replacement packs","P2","todo","M","XPR2/DDS/TGA textures dumpable and overridable by hash","R-104",""),
 ("R-414",4,"UI","In-game Revolution settings menu","P0","in_progress","L","'BBRR Settings' entry in the game's own Help & Options menu opens a settings screen in the game's style (spinner rows, A save / B cancel / X defaults); settings saved to a file and applied at start-up; home for the calibration screen","R-401","First version (untested): UI scripts are Lua 5.1 and can be replaced from ui_scripts/ (src/ui_mod.cpp); screen ui_scripts/Options/BBRR.lua with aspect ratio, menu layout, audio offset, skip intro; saved to settings.ini (src/settings.cpp). Still to add: resolution scale, frame rate, window mode, DLC, calibration"),

 ("R-501",5,"Achievements","Local achievements from SPA","P1","todo","M","12 achievements (200G) tracked and toasted; persisted","R-107","ReXGlue has an achievements XEX-to-TOML extractor"),
 ("R-502",5,"Leaderboards","Local leaderboards","P0","done","M","66 scoreboard IDs (contents.txt) store top scores locally per mode/difficulty","R-107","Verified 2026-11-10: scores show in song select and persist across restarts (src/leaderboards.cpp)"),
 ("R-503",5,"Leaderboards","Implement XUserReadStats / XSessionWriteStats / stats enumerator","P0","in_progress","L","Game's own leaderboard screens show data; XamUserCreateStatsEnumerator implemented","R-502","ReadStats path verified via song select; full leaderboard screens (ByRank/ByXuid enumerators) still to verify"),
 ("R-504",5,"Online","Online leaderboard service","P1","todo","XL","Self-hosted REST service; scores submitted & fetched; basic validation","R-503",""),
 ("R-505",5,"Online","Score integrity checks","P2","todo","M","Server rejects impossible scores (max per chart from note counts)","R-504",""),
 ("R-506",5,"Online","Player identity / profile","P1","todo","S","Local gamertag; optional online account token","R-504",""),

 ("R-601",6,"Input","Controller support (XInput, DualSense, Switch Pro)","P0","todo","S","All controllers work via SDL3 with hot-plug","R-104",""),
 ("R-602",6,"Input","Keyboard support & rebinding","P0","todo","M","Default 4-colour + D-pad mapping; full rebinding UI","R-601",""),
 ("R-603",6,"Input","Button glyph swap","P2","todo","M","UI shows Xbox/PlayStation/keyboard prompts to match device","R-602",""),
 ("R-604",6,"Input","Local 2-player","P1","todo","S","Two controllers, or controller + keyboard","R-601",""),
 ("R-605",6,"UX","Settings persistence","P0","todo","S","User TOML in %APPDATA%; CLI overrides","R-414",""),
 ("R-606",6,"UX","First-run setup wizard","P1","todo","M","Points at a dump, validates hashes, copies to assets/","R-104","Uses ReXApp OnFinalizePaths"),

 ("R-701",7,"CI","GitHub Actions Windows build","P1","todo","M","CI builds runtime without game data (codegen output cached/user-generated)","R-102",""),
 ("R-702",7,"QA","Input replay regression tests","P1","todo","L","Recorded inputs replay to identical score at 60/144/240 fps","R-302",""),
 ("R-703",7,"QA","Sync accuracy test","P0","todo","M","Automated measurement: judged time error < 1 ms vs chart","R-204",""),
 ("R-704",7,"Perf","Performance target","P1","todo","M",">= 240 fps at 1440p on a GTX 1660-class GPU; 60 fps on Steam Deck","R-302",""),
 ("R-705",7,"Docs","Build & user guides","P1","todo","S","README build steps verified on a clean machine","R-006",""),
 ("R-706",7,"Release","Licensing decision for project code","P0","todo","S","LICENSE file chosen and compatible with ReXGlue (BSD-3) and FFmpeg terms","", "Open decision"),
 ("R-707",7,"Release","Public release v1.0","P0","todo","M","Tagged GitHub release with build artifacts (no game data)","R-110,R-111,R-204,R-302,R-401,R-502",""),
 ("R-806",8,"Feature","Average timing on the results screen","P2","todo","M","Results screen shows mean hit offset (ms, early/late) and spread for the song just played; optional per-difficulty history","R-801,R-803","Requested 2026-11-10"),
 ("R-901",9,"Accessibility","Reduce background/camera motion option","P2","todo","M","Setting (Off/Reduced/Static) that damps or freezes the city camera movement from the *_SplCamera splines without affecting gameplay or note timing","R-104","Stretch goal: background movement can be disorienting (user feedback 2026-11-10)"),
 ("R-902",9,"Research","Accessibility review","P2","todo","S","docs/ACCESSIBILITY.md: motion, flashing/brightness of fireworks, colour-blind-safe note colours, UI/text scaling, one-handed/remappable controls; prioritised list","R-104","Stretch goal"),
 ("R-903",9,"Feature","VR support","P2","todo","XL","Play in a VR headset (OpenXR): stereo rendering of the city and fireworks with head tracking, menus and HUD on a comfortable panel, timing unaffected","R-402,R-302","Stretch goal (requested 2026-11-10). Needs per-eye camera/projection hooks and a stereo present path in the SDK"),
 ("R-801",8,"Research","Research modern rhythm-game features","P1","todo","M","docs/RHYTHM_FEATURES.md: survey (speed mods, timing feedback, measure/beat lines, etc.), feasibility vs .lvl beat data + engine hooks, prioritised list","R-109","Requested 2026-11-10"),
 ("R-802",8,"Feature","Note speed / scroll-speed modifier","P2","todo","M","Adjustable approach speed independent of song tempo; judging unchanged","R-801",""),
 ("R-803",8,"Feature","Timing feedback (early/late, ms offset)","P2","todo","M","Per-hit early/late indicator and end-of-song timing histogram / mean offset","R-801,R-306",""),
 ("R-804",8,"Feature","Measure and beat lines","P2","todo","M","Optional beat/measure guide lines derived from SongBPM/SongOffset","R-801",""),
 ("R-805",8,"Feature","Practice tools (section loop, slow-down)","P2","todo","L","Loop a section and play at 50-100% speed with pitch-corrected audio","R-801,R-203","Game embeds SoundTouch time-stretch library - may support tempo change already"),
]
FIELDS=["id","phase","phase_name","category","title","priority","status","effort","acceptance","depends_on","notes"]
rows=[dict(zip(FIELDS,[r[0],r[1],PHASES[r[1]]]+list(r[2:]))) for r in REQS]
here=os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(here,"requirements.csv"),"w",newline="",encoding="utf-8") as f:
    w=csv.DictWriter(f,fieldnames=FIELDS); w.writeheader(); w.writerows(rows)
with open(os.path.join(here,"requirements.json"),"w",encoding="utf-8") as f:
    json.dump(rows,f,indent=1)
print(len(rows),"requirements written")
