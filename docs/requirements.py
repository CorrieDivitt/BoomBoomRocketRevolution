"""Source of truth for the requirements tracker.
Edit the REQS list (status / notes) and run:  python docs/requirements.py
It regenerates docs/requirements.csv and docs/requirements.json, which the dashboard reads."""
import csv, json, os
PHASES = {
 0: "Foundation & analysis", 1: "Recompile & boot", 2: "Audio & music sync",
 3: "Frame rate & timing", 4: "Graphics & resolution", 5: "Leaderboards & achievements",
 6: "Input, UX & settings", 7: "QA, CI & release"}
# id, phase, category, title, priority, status, effort, acceptance, depends_on, notes
REQS = [
 ("R-001",0,"Analysis","Verify game dump (default.xex + content)","P0","done","S","default.xex parses; TitleID 5841086A; all 10 tracks present","", "XEX2, XDK 2.0.5426, image base 0x82000000, entry 0x820E83D8"),
 ("R-002",0,"Analysis","Decrypt/decompress XEX to PE image","P0","done","S","tools/xex/unxex.py produces 3,276,800-byte PE starting with MZ","R-001","Retail AES + LZX (window 2^15)"),
 ("R-003",0,"Analysis","Kernel/XAM import inventory vs ReXGlue","P0","done","S","166 imports listed with implementation status","R-002","162/166 implemented in ReXGlue v0.10.1; 4 XAM gaps"),
 ("R-004",0,"Setup","Repo scaffolding + ReXGlue SDK submodule","P0","done","S","git repo with docs, tools, .gitignore excluding game data; SDK at thirdparty/rexglue-sdk","", "Remote: github.com/CorrieDivitt/BoomBoomRocketRevolution (push pending)"),
 ("R-005",0,"Setup","Install Windows toolchain","P0","todo","S","VS 2022 + Clang 20 (clang-cl), CMake 3.25+, Ninja on PATH; scripts/bootstrap.ps1 passes checks","", ""),
 ("R-006",0,"Setup","Build & install ReXGlue SDK","P0","todo","M","cmake --preset win-amd64 builds; rexglue.exe runs","R-005","Pin SDK to v0.10.1 tag; bump deliberately"),
 ("R-007",0,"Analysis","Document asset formats","P1","in_progress","M","Spec for .lvl/.spl/.mdl/.config/.bbr/.xpr/XMA in docs/ANALYSIS.md","R-001",".lvl/.spl/.config are text; .bbr/.xpr binary; audio XMA1 48kHz stereo"),
 ("R-008",0,"Legal","No copyrighted game data in repo","P0","done","S",".gitignore blocks Game Files/, assets/, *.xex, *.xma, *.xpr; CI never needs assets","", "Users supply their own legally-owned dump"),
 ("R-009",0,"Analysis","Static analysis setup (Ghidra/IDA + XEX loader)","P1","todo","S","Image loads with symbols for imports; pdata functions applied","R-002","Used for game-loop/timing reverse engineering"),

 ("R-101",1,"Recomp","rexglue init project + codegen config","P0","todo","S","boom_boom_rocket_config.toml points at assets/default.xex; codegen target exists","R-006",""),
 ("R-102",1,"Recomp","Clean codegen of all functions","P0","todo","L","Codegen with no validation errors (no --force); ~3,002 pdata functions + discovered extras","R-101","292 mtctr/bctr sites (jump tables); 8,709 VMX/VMX128 words"),
 ("R-103",1,"Recomp","Locate setjmp/longjmp (if used)","P1","todo","S","Addresses set in TOML or confirmed unused","R-009",""),
 ("R-104",1,"Runtime","Boot to title screen (D3D12)","P0","todo","L","Splash, legal screens and main menu render correctly","R-102",""),
 ("R-105",1,"Runtime","VFS mounts: GAME:, game:, DLC%d:","P0","todo","S","All Failed-to-load paths resolve; case-insensitive lookups","R-104","Strings show GAME:\\ and game:\\ mixed case"),
 ("R-106",1,"Runtime","Full-license / trial flag","P0","todo","S","XamContentGetLicenseMask reports full game; IsTrialVersion false","R-104",""),
 ("R-107",1,"Runtime","Save data & profile settings persistence","P0","todo","M","Unlocks/scores survive restart (XamContentCreateEx, XamUser*ProfileSettings)","R-104",""),
 ("R-108",1,"Runtime","Stub missing XAM UI calls","P1","todo","S","XamShowAchievementsUI / MarketplaceUI / GamerCardUIForXUID return gracefully (or open native overlay)","R-104","Not implemented in ReXGlue v0.10.1"),
 ("R-109",1,"Gameplay","All modes playable","P0","todo","L","Standard, Endurance, Practice, Freestyle, Visualizer, 2P Battle, 2P Endurance complete without crash","R-104",""),
 ("R-110",1,"Gameplay","All 10 tracks x 3 difficulties complete","P0","todo","M","30 charts finish with correct results screen and grade","R-109",""),
 ("R-111",1,"Content","DLC track pack support","P2","todo","M","Owned DLC package mounted at DLC%d:\\ and extra tracks listed","R-105","Update brought total to 15 tracks"),

 ("R-201",2,"Audio","XMA1 music & SFX playback","P0","todo","M","All 10 songs and 86 SFX/ambience files decode and play (runtime XMA decoder via FFmpeg)","R-104","Files are WAVE_FORMAT_XMA (0x165), 48 kHz"),
 ("R-202",2,"Sync","Identify the game's song clock","P0","todo","M","Documented function(s) that compute current song time/beat and their time source","R-009","Clues: SongBPM/SongOffset strings, 11 mftb sites, KeQueryPerformanceFrequency"),
 ("R-203",2,"Audio","Native low-latency music path","P0","todo","L","Songs pre-decoded to PCM and played via WASAPI (SDL3); reported output latency < 20 ms","R-201",""),
 ("R-204",2,"Sync","Audio-clock-driven beat timeline","P0","todo","L","Song time comes from audio device playback position; drift < 2 ms over a full song","R-202,R-203",""),
 ("R-205",2,"Sync","Global audio offset setting (ms)","P0","todo","S","-200..+200 ms offset applied to judging and visuals","R-204",""),
 ("R-206",2,"Sync","Input latency offset setting (ms)","P0","todo","S","Separate input offset shifts judgement windows only","R-204",""),
 ("R-207",2,"Sync","Calibration screen (tap to the beat)","P1","todo","M","Guided tap test suggests audio + input offsets","R-205,R-206",""),
 ("R-208",2,"Sync","Per-song offset overrides","P2","todo","S","Optional per-track offset table in user config","R-205","Ride of the Valkyries Easy already ships SongOffset 0.03"),
 ("R-209",2,"Sync","Sync debug overlay","P1","todo","S","Overlay shows audio time, game beat, delta, frame time","R-204",""),
 ("R-210",2,"Audio","Audio device & buffer options","P1","todo","S","Choose device, buffer size, exclusive/shared mode, volume sliders","R-203",""),
 ("R-211",2,"Feature","Visualizer with user music files","P2","todo","L","Visualizer mode plays local mp3/flac/ogg via XMP replacement","R-203","Original used XMP + console music"),

 ("R-301",3,"Timing","Determine game loop timing model","P0","todo","M","Documented: fixed 60 Hz step vs delta time; frame-count dependencies listed","R-009","Strings report timings in frames"),
 ("R-302",3,"Timing","Unlocked frame rate (120/144/240+)","P0","todo","XL","Gameplay identical at 60/144/240 fps (replay test); no speed-up","R-301",""),
 ("R-303",3,"Timing","Fixed-step sim with render interpolation","P1","todo","L","Simulation at 60 Hz, camera/particles interpolated per frame","R-301","Fallback if delta-time patching is impractical"),
 ("R-304",3,"Timing","Frame limiter, VSync, VRR","P0","todo","S","Cap options (30..360/unlimited), VSync toggle, G-Sync/FreeSync friendly","R-302",""),
 ("R-305",3,"Timing","Low-latency presentation","P1","todo","M","Flip-model swapchain, waitable object, max 1 frame queued","R-304",""),
 ("R-306",3,"Input","High-rate timestamped input","P0","todo","M","Input polled >= 1 kHz on its own thread; presses timestamped against audio clock","R-204",""),

 ("R-401",4,"Graphics","Arbitrary resolutions","P0","todo","M","Any window/fullscreen resolution up to 4K+; correct projection","R-104",""),
 ("R-402",4,"Graphics","Aspect ratios 16:9 / 16:10 / 21:9 / 32:9","P0","todo","M","Hor+ 3D view; UI anchored and not stretched","R-401",""),
 ("R-403",4,"Graphics","Internal resolution scale","P1","todo","S","Render scale 0.5x-4x independent of output size","R-401","Xenia draw_resolution_scale path first"),
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
 ("R-414",4,"UI","Graphics settings menu","P0","todo","M","In-app (ImGui) menu for all graphics options; saved to config","R-401",""),

 ("R-501",5,"Achievements","Local achievements from SPA","P1","todo","M","12 achievements (200G) tracked and toasted; persisted","R-107","ReXGlue has an achievements XEX-to-TOML extractor"),
 ("R-502",5,"Leaderboards","Local leaderboards","P0","todo","M","66 scoreboard IDs (contents.txt) store top scores locally per mode/difficulty","R-107",""),
 ("R-503",5,"Leaderboards","Implement XUserReadStats / XSessionWriteStats / stats enumerator","P0","todo","L","Game's own leaderboard screens show data; XamUserCreateStatsEnumerator implemented","R-502","Enumerator missing in ReXGlue v0.10.1"),
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
 ("R-707",7,"Release","Public release v1.0","P0","todo","M","Tagged GitHub release with build artifacts (no game data)","R-110,R-204,R-302,R-401,R-502",""),
]
FIELDS=["id","phase","phase_name","category","title","priority","status","effort","acceptance","depends_on","notes"]
rows=[dict(zip(FIELDS,[r[0],r[1],PHASES[r[1]]]+list(r[2:]))) for r in REQS]
here=os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(here,"requirements.csv"),"w",newline="",encoding="utf-8") as f:
    w=csv.DictWriter(f,fieldnames=FIELDS); w.writeheader(); w.writerows(rows)
with open(os.path.join(here,"requirements.json"),"w",encoding="utf-8") as f:
    json.dump(rows,f,indent=1)
print(len(rows),"requirements written")
