# Boom Boom Rocket Recompiled — Project Plan

**Goal:** a native Windows build of the Xbox 360 XBLA game *Boom Boom Rocket* (Bizarre Creations / EA, 2007), produced by static recompilation, with smooth high-frame-rate play, arbitrary resolutions, a range of graphics options, working leaderboards, and timing controls that keep the game locked to the music.

Repository: `github.com/CorrieDivitt/BoomBoomRocketRevolution` · Requirements: [`requirements.csv`](requirements.csv) (source: `requirements.py`) · Findings: [`ANALYSIS.md`](ANALYSIS.md)

---

## 1. Why this game is a good recomp target

- **Small, clean binary.** One 3.2 MB image, 3,002 functions listed in `.pdata`, a retail "Master" build on a single XDK (2.0.5426). No title update needed.
- **Standard Microsoft libraries.** D3D9, XAudio 1, XMP, XOnline — all paths the Xenia-derived runtimes already know.
- **Runtime coverage is already high.** 162 of its 166 kernel/XAM imports are implemented in ReXGlue v0.10.1; the 4 gaps are UI/leaderboard calls we want to replace anyway.
- **Data-driven gameplay.** Charts (`.lvl`), splines, fireworks and UI are text/Lua files, so timing and presentation can be reasoned about without reversing everything.
- **Shader source is recoverable.** The shipped `.updb` shader PDBs embed the original HLSL, which makes native shader replacement and lighting upgrades realistic.

## 2. Toolchain decision

| Option | What it is | Verdict |
|---|---|---|
| **ReXGlue SDK** (rexglue/rexglue-sdk, v0.10.1, BSD-3) | PPC→C++ codegen (XenonRecomp lineage) **plus** a full runtime: Xenia-derived kernel/XAM HLE, D3D12/Vulkan GPU backend, XMA audio, SDL3 input, VFS, ImGui overlays, `REX_HOOK` function overrides, mid-asm hooks, achievements extractor | **Primary.** Gives a booting build fastest; already used for XBLA ports (GoldenEye 007 XBLA, Castlevania SotN XBLA / NocturneRecomp) |
| XenonRecomp + XenosRecomp (hedge-dev) | Codegen + shader translator, **no runtime** (Unleashed Recompiled wrote its own) | Reference only. Revisit for the long-term native renderer (R-412) |
| Xenia Canary | Emulator | Behaviour reference: compare logs/frames when the recomp diverges |
| rexdex recompiler | Original 360 recompiler | Historical reference |
| Ghidra (+ XEX loader) / IDA | Static analysis | Required for finding the game loop, song clock, leaderboard and render code |
| `tools/xex`, `tools/shaders` (this repo) | XEX decrypt/decompress, import audit, HLSL recovery | Done; used for the analysis in ANALYSIS.md |

**Pinning:** ReXGlue warns of breaking API changes, so the submodule is pinned to a release tag and upgraded deliberately (`rexglue migrate`) on a branch.

### Windows prerequisites
Visual Studio 2022 with *Desktop development with C++*, **C++ Clang compiler for Windows (20.x+)** and *MSBuild support for LLVM (clang-cl)*; CMake 3.25+; Ninja; Git; Python 3. A CPU with AVX.

## 3. Architecture

```
Game Files/ (user dump, never committed) ──► assets/  (junction)
                                               │
 rexglue codegen ── boom_boom_rocket_config.toml ──► generated/ (PPC → C++)
                                               │
 src/  (our code)                              ▼
  ├─ app (ReXApp subclass: paths, settings, overlays)
  ├─ hooks/  REX_HOOK / mid-asm hooks
  │    ├─ timing   (game loop, delta time, interpolation)
  │    ├─ songclock (audio-clock beat timeline, offsets)
  │    ├─ render   (resolution, aspect, AA, post-FX, shader overrides)
  │    └─ online   (stats read/write → leaderboard service)
  ├─ audio/   native music path (pre-decoded PCM, WASAPI via SDL3)
  └─ ui/      ImGui settings, calibration, sync overlay
        │
        ▼
 ReXGlue runtime: kernel/XAM HLE · GPU (D3D12) · XMA · input · VFS
```

## 4. How each headline goal will be met

### 4.1 Always in sync with the music
1. **Find the song clock** (R-202). The charts are in beats with constant BPM and a seconds offset, so somewhere the game computes `song_time` (likely from XAudio voice position, `mftb`, or frame counts). Locate it in Ghidra via the `SongBPM`/`SongOffset` parser and the `.lvl` loader.
2. **Replace the music path** (R-203). Decode the 10 XMA1 songs to PCM at load (FFmpeg, already in the SDK) and play them through a low-latency SDL3/WASAPI stream we own, so we know exactly which sample is at the speaker.
3. **Drive the game from the audio clock** (R-204). `REX_HOOK` the song-time getter to return `audio_samples_played / 48000 − output_latency − audio_offset`. Smooth with a small PLL so time never jumps backwards.
4. **User controls**: global audio offset, separate input offset, per-song overrides, a tap-to-beat calibration screen, and a debug overlay showing audio time vs game beat (R-205…R-209).
5. **Judge with timestamps** (R-306). Poll input on its own ≥1 kHz thread and stamp each press with the audio clock so judging is independent of frame rate.
6. **Verify** (R-703): automated test that feeds perfectly timed presses and checks the judged error stays under 1 ms.

### 4.2 Smooth, high frame rate
1. Confirm the loop model (R-301): the debug strings count load time in frames, which suggests a fixed 60 Hz, vsync-locked loop.
2. Preferred: patch the frame delta so simulation scales with real time (R-302). Fallback: run simulation at a fixed 60 Hz and interpolate camera splines and particles for rendering (R-303).
3. Remove the 60 Hz present lock in the `VdSwap` path; add frame cap, VSync, VRR and a low-latency flip-model swapchain (R-304, R-305).
4. Regression: replay recorded input at 60/144/240 fps and require identical scores (R-702).

### 4.3 Resolutions and graphics options
Staged so each step ships something:
- **Stage A – emulated GPU, better output:** any resolution, aspect ratios up to 32:9 (Hor+ projection patch, UI re-anchored in the BZUI Lua scripts), internal render scale, window modes, AA, anisotropic filtering (R-401…R-406).
- **Stage B – native shaders:** replace the game's shaders with HLSL rebuilt from the recovered `.updb` sources, pixel-matched first (R-407, R-408).
- **Stage C – lighting upgrades:** optional HDR bloom/tonemapping for fireworks, firework light on the city, particle-count scaling via the `Fireworks/*.config` grades (R-409…R-411).
- **Stage D – long term:** hook the statically linked D3D9 layer and render natively, bypassing GPU emulation (R-412); texture dump/replace packs (R-413).
All options live in one ImGui settings menu saved to the user config (R-414, R-605).

### 4.4 Leaderboards and achievements
- Local first: implement the missing `XamUserCreateStatsEnumerator` and route `XSessionWriteStats`/`XUserReadStats` (already dispatched in ReXGlue's XGI app) to a local store keyed by the 66 scoreboard IDs, so the game's own leaderboard screens work offline (R-502, R-503).
- Achievements from the SPA (12, 200 G) with native toasts (R-501).
- Online: a small self-hosted REST service with basic validation (max possible score per chart from note counts) (R-504…R-506).

## 5. Milestones

| Milestone | Exit criteria | Key requirements |
|---|---|---|
| **M0 Foundation** ✅ partly | Dump analysed, repo + SDK in place, toolchain installed, SDK builds | R-001…R-009 |
| **M1 First boot** | Codegen clean, game reaches main menu on D3D12 | R-101…R-108 |
| **M2 Playable** | All modes, 30 charts, saves, audio working | R-109…R-111, R-201 |
| **M3 Locked to the music** | Audio-clock timeline, offsets, calibration, < 2 ms drift | R-202…R-211, R-306 |
| **M4 High frame rate** | 144/240 fps with identical gameplay, low-latency present | R-301…R-305 |
| **M5 Visual options** | Resolutions/aspect, AA, settings menu, native shaders, bloom | R-401…R-414 |
| **M6 Leaderboards** | Local leaderboards + achievements, then online | R-501…R-506 |
| **M7 Release** | Input/UX polish, CI, tests, docs, licence, v1.0 tag | R-601…R-606, R-701…R-707 |

M3 and M4 can run in parallel once M2 is reached; M5 Stage A can start as soon as M1 boots.

## 6. Day-to-day workflow
1. `scripts/bootstrap.ps1` (once): checks tools, links `assets/` → `Game Files/`, builds/installs the SDK, scaffolds the app with `rexglue init`.
2. Loop: run the `boom_boom_rocket_codegen` target → build → run → read the log for unresolved calls, missing imports, bad jump tables → add `[functions]`, `[[switch_tables]]` or hooks in the TOML / `src/hooks` → repeat. Use `--force` only to get past known issues while working through them.
3. When behaviour differs, run the same dump in Xenia Canary and compare logs and frames.
4. Update `docs/requirements.py` statuses and run it; the dashboard reads `requirements.csv`.

## 7. Risks
| Risk | Mitigation |
|---|---|
| GPU emulation issues (point sprites, EDRAM resolves, tiled XPR textures) | Compare against Xenia Canary; Stage B/D native rendering |
| Song clock is frame-derived and spread across code | Mid-asm hooks at each read; worst case re-implement the song-time module natively |
| Frame-count logic hidden in particle/camera code | Fixed-step + interpolation fallback (R-303) |
| ReXGlue API churn | Pin tag, migrate on a branch, keep hooks thin |
| XMA1 decode edge cases | Offline pre-decode with FFmpeg and compare waveforms |
| Legal / IP | No game files, extracted shaders or generated code in the repo; users supply their own dump; project described as unofficial; licence chosen before release (R-706) |

## 8. Open decisions
- Project licence (R-706).
- Whether generated C++ is committed (normally **not**, as it derives from the game binary).
- Online leaderboard hosting.
