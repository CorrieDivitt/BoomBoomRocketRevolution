# Boom Boom Rocket — Timing Model (R-202, R-301)

Reverse-engineered from the retail `default.xex` on 2026-11-10. Addresses are guest (PPC) addresses.

## Summary
- **Song time is driven by the audio clock, not by frames.** Each frame the game asks the XAudio
  source voice how many samples it has played and converts that to seconds.
- **The per-frame time step is variable.** During a song, delta time = change in song time; in menus
  it is real elapsed time from the CPU time base. Nothing in the main update assumes 1/60 s,
  except one fade timer (below).
- Therefore higher frame rates should mostly "just work" by raising the guest vblank rate, and the
  timing work in phase 2 becomes *calibration* (offsets/latency), not a rewrite.

## Key data
| Address | Meaning |
|---|---|
| `0x821A0564` | Chart header block written by the `.lvl` loader (`0x82067408…`): `+0x0 TargetWidth`, `+0x8 SongOffset`, `+0xC SongBPM`, `+0x10 SongLength` (floats) |
| `0x821C1420` | Main game object (`this` for the update below) |
| game `+0x129C4` | Current song time in seconds (float) — read by every beat/position calculation |
| game `+0x129C8` | Song time on the previous frame |
| game `+0x125CC` | Pointer to the music player object |
| game `+0x129D0` | Last time-base sample (64-bit, from `mftb`) |
| game `+0x12EAC` | Fade level (0…1) |
| game `+0x12EB0` | Accumulated real time |
| `0x821A0508` | **Audio latency compensation constant = 0.045 s** added to song time |
| `0x821A050C` | 0.16 — coefficient of a second, optional correction term |
| `0x821A0578` | Game state / mode block (`+0` mode, `+4` sub-state, `+0xB4` pause/flags) |
| `0x821A0560` | 60.0 (frame rate constant, only used for the fade step) |

## Key functions
| Address | What it does |
|---|---|
| `0x82077100` | **Per-frame time update.** Returns delta time in `f1`. Song mode: calls `0x82080928` (refresh player position), then `songTime = 0x82080B00(player)`, `dt = songTime - prevSongTime` (negative dt is logged and clamped to 0). Other modes: `dt = (mftb_now - mftb_prev) / KeQueryPerformanceFrequency`, added to song time. Also steps the fade timer by `(1/60) / 0.5` **per frame** (frame-locked). |
| `0x82080928` | Music player refresh: queries the XAudio voice position via `0x8213A568` (locked call to the voice's position method), adds the start offset, handles loop wrap; result in player `+0xCC` (samples). |
| `0x82080B00` | Song seconds = `samples(+0xCC) / sampleRate(+0xF0) + 0.045`, minus an optional correction `0.16 * f(...)` when a flag is set. |
| `0x82077A70` | Beat phase: `t' = (t - SongOffset)`, `beats = t' * BPM/60`, returns the fractional beat in seconds. |
| `0x82078AD0` | Beat-relative position used by note/launch logic (same maths plus a 0…2 wrap). |
| `0x820E7450` | Time-base read (`mftb` with high-word retry) — the game's `QueryPerformanceCounter`. |

## Implications for the plan
- **R-204 (audio-clock timeline):** already true in the original game. What remains is making sure
  the runtime's XAudio "samples played" matches what is actually leaving the speakers (host audio
  buffer latency), and exposing offsets. Hook points: `0x82080B00` (add user offset) or the
  constant at `0x821A0508`.
- **R-205/206 (offsets):** a user audio offset can be applied by patching/overriding `0x821A0508`
  (or wrapping `0x82080B00`); an input offset must be applied where presses are judged (to find).
- **R-302 (high frame rate):** set `video_mode_refresh_rate` (24–240 Hz guest vblank) or
  `--vsync=false`. Known frame-locked code: the fade timer in `0x82077100` (fades run faster at
  >60 fps) — fix by scaling with dt. Particle/camera code still needs a visual check.
- **R-805 (practice slow-down):** the executable embeds the **SoundTouch** time-stretch library
  (string "SoundTouch : Sample rate not defined"), which may already support tempo changes.
