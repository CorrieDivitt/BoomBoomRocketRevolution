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
| `0x82064978` | Per-frame gameplay update `(obj, time)`: shifts the last three song times through `0x821BE9C8` (this frame), `0x821BE9CC` (previous), `0x821BE9D0` (the one before). |
| `0x82066700` | **Press judging.** For a rocket with target time `T` (`[press+0x7C]+0x48`) and the span `W = [0x821BE9D0, 0x821BE9CC]`: `accuracy = 1 - abs(T - clamp(T, W)) / (TargetWidth * 0.5)`; below 0 is a miss. A press therefore counts as made during the previous frame. Result goes to `0x82065760` (score/grade). |
| `0x820E7450` | Time-base read (`mftb` with high-word retry) — the game's `QueryPerformanceCounter`. |

## Implications for the plan
- **R-204 (audio-clock timeline):** already true in the original game. What remains is making sure
  the runtime's XAudio "samples played" matches what is actually leaving the speakers (host audio
  buffer latency), and exposing offsets. Hook points: `0x82080B00` (add user offset) or the
  constant at `0x821A0508`.
- **R-205/206 (offsets):** a user audio offset can be applied by patching/overriding `0x821A0508`
  (or wrapping `0x82080B00`); the input offset (`--input_offset_ms`, `src/input_offset.cpp`) shifts the press span `W` while `0x82066700` runs, so only judging moves.
- **R-306 (precise input):** `--precise_input` (default on, same file) reads the controller every millisecond on its own thread and, when a press is judged, replaces `W` with the single song time at which the button went down (`latest song time - time since the press - input_frame_credit_ms`). Falls back to the game's span when there is no fresh XInput press. The log prints `[input]` statistics every 40 presses.
- **R-302 (high frame rate):** set `video_mode_refresh_rate` (24–240 Hz guest vblank) or
  `--vsync=false`. Known frame-locked code: the fade timer in `0x82077100` reads its per-frame step from
  `0x821D4330` (initialised once to 1/60). **Fixed:** `src/frame_stats.cpp` writes the real frame
  time there before each call. Tested OK at 120 and 144 fps. Particle/camera code still needs a visual check.
- **R-805 (practice slow-down):** the executable embeds the **SoundTouch** time-stretch library
  (string "SoundTouch : Sample rate not defined"), which may already support tempo changes.

## Judgement (how a press is scored)
`0x82066700` runs for a rocket and the presses of the frame. With `d` = distance between the
rocket's time and the press span (0 inside the span) and `W` = the chart's `TargetWidth` in seconds
(first lines of each `.lvl`; 0.125 to 0.5 depending on song and difficulty):

`accuracy = 1 - d / (W / 2)`

| Accuracy | Shown as | Grade value | Firework | `d` as a share of `W/2` |
|---|---|---|---|---|
| >= 0.95 | Perfect | 6 | large | within 5% |
| > 0.8 | Awesome | 5 | large | within 20% |
| > 0.4 | Good | 3 | medium | within 60% |
| > 0 | OK | 1 | small | within 100% |
| <= 0 | Missed | 0 | none | outside |

Thresholds are the floats at `0x821A04E8` (0.95, 0.8, 0.4). The grade value is `n * 5 / 3 / 1` with
`n` from the rocket's effect list (1 in what has been seen), sent to the HUD as the `GRADE` message;
the HUD also has texts for 2 (Bad) and 4 (Excellent). This is the only place in the executable that
sends `GRADE`, so those two can only appear if a rocket's effect list carries `n` = 2 or 4; whether any
chart does has not been checked in a running game. Points for a hit are `accuracy^4 * 10000` before multipliers (`0x821A0510` = 4.0,
`0x821A0514` = 10000). In casual mode (game mode 3) every hit counts as accuracy 1.

The original span is a whole frame, so at 60 fps a press had about 16.7 ms of extra leeway on top of
these windows. `--precise_input` keeps that leeway (8.3 ms either side of the real press time) so the
windows are the same at any frame rate.

## End of a song and failing a song
Game state lives at `0x821A0578` (`+4` = state: 1 playing, 4 failing, 2 game over, 3 finished;
`+0x114`/`+0x134` = each player's bonus meter). The per-mode updates are `0x8207B470`, `0x8207B6A0`
and `0x8207BBC0`.

- **Finished:** `0x82080830(player)` returns true once the player's position `+0xCC` reaches its end
  position `+0xFC`. The game then sends the HUD the `FINISHED` message, and the HUD script counts
  0.1 s down before asking for the results screen. `+0xCC` is the value `src/song_clock.cpp` shifts
  for the audio lag, which is why a positive audio offset used to leave it short of `+0xFC`
  forever; the clock now runs on to the real position when the audio stops.
- **Failing:** when a meter reaches 0 the state becomes 4 and the HUD gets `PRE_GAME_OVER`. Each
  frame the timer at state `+0xC8` drops by the float at `0x821A0794` (0.01), the music pitch
  follows it down (`0x82077E78`, `0x82080868`), and at 0 the music is paused and `GAME_OVER` is
  sent. That is 100 frames: 1.67 s at 60 fps. `src/frame_stats.cpp` scales the step by the real
  frame time so the slow-down lasts 1.67 s at any frame rate.
- The song clock plays no part in the failing countdown itself.

## Judder and the smoothed song clock (R-213)
The XAudio voice position only advances when an audio buffer is processed, so the song time read once
per video frame moves in uneven steps and arrows judder at any frame rate. `0x82080A40` (frame delta)
reads player `+0xC4`; `0x82080B00` (song time) reads player `+0xCC`. `src/song_clock.cpp` hooks the
refresh `0x82080928` and replaces both counters with an estimate that advances with the host clock and
is steered toward the real audio position (0.5 s time constant, snaps on seek/loop, holds when audio
stalls or pauses). The raw values are restored before the game's next refresh.
When the audio position stops (end of a song, the slow-down after failing) the clock runs on until it
reaches the real position and the audio-lag shift is handed back, so the game still sees the song end;
without this a positive audio offset left the game waiting forever at the end of a song.
`--smooth_song_clock=false` disables it; the log prints `[songclock]` step statistics every 10 s.

## Host audio latency and offsets (R-204, R-205)
The voice position the game reads is the *mixed* position. In this runtime the guest mixer stays
`--audio_maxqframes` frames ahead of the host device (256 samples at 48 kHz = 5.33 ms each; default
8 = 42.7 ms), because the audio worker lets it mix that many frames before the device takes the
first one. So the music that is heard lags the song clock by that queue plus the device's own
buffer and whatever follows it (USB, Bluetooth, TV).

`src/song_clock.cpp` therefore shifts the song time (player `+0xCC` only, so the frame delta from
`+0xC4` is untouched):
- `--audio_sync_compensation` (default on) subtracts the known queue (`audio_maxqframes * 5.33 ms`).
- `--audio_offset_ms` (default -20, from the first play test: -15..-25 felt best) is the user offset on top: positive = music is heard later than
  expected, notes arrive later; negative = notes arrive earlier.
- `--audio_maxqframes=4` halves the queue itself (lower sound latency, more risk of crackle).

The game's own `+0.045 s` constant (`0x821A0508`) is left alone; it most likely compensated for TV
display lag on the console. The log line `[songclock] song clock shifted by ...` shows what is applied.
