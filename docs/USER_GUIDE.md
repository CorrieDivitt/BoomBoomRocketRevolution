# Boom Boom Rocket Revolution — user guide

A native Windows build of the Xbox 360 game Boom Boom Rocket. You need your own copy of the
game files; none are included.

## Starting the game
1. Put the game's files in a folder called `Game Files` (the one that contains `default.xex`)
   next to the program or in any folder above it.
2. Run `boomboomrocket.exe`. An Xbox controller is recommended.

The start-up logos are skipped automatically. Alt+F4 closes the game.

## Downloadable packs
Put the original package files (the long names such as `6AEA31EE…D758`, exactly as they came
from the console) in a folder called `DLC` next to `Game Files`. They are installed the next time
the game starts and removed again if you take the file away. `--dlc_path=<folder>` uses a
different folder.

## BBRR Settings
**Help & Options > BBRR Settings.** A saves, B leaves without saving, X restores the defaults.

| Page | Setting | What it does |
|---|---|---|
| Video Settings | Aspect Ratio | Match Window (default), 16:9, 21:9, 32:9 |
| | Window Mode | Fullscreen or Windowed; changes at once |
| | Anti-Aliasing | Off, FXAA, FXAA High; changes at once |
| | Resolution | 720p, 1440p (default), 2160p, 2880p; needs a restart, the game offers one |
| Calibration | Auto Calibration | Two short tap tests (sound, then picture) that set the two offsets below |
| | Audio Offset | Moves the notes against the music. Positive = notes arrive later |
| | Input Offset | Moves how presses are judged. Positive = presses count as made earlier |
| | Hit Timing | After each hit, shows how early (+) or late (-) it was in milliseconds; adds a timing line to the results screen |
| | Assist Tick | Plays a click at the exact moment of every rocket |
| (main page) | Skip Intro | Skips the start-up logos |
| | Menu Layout | Screen Edges (menus follow the window's edges on wide screens) or Centred |

The frame rate follows your display automatically; there is nothing to set.

### Getting the timing right
1. Run **Auto Calibration** once. Tap A on every beat of the sound test, then every time a bar
   reaches the line in the picture test.
2. Play a song with **Hit Timing** on. If the numbers are mostly negative you are being judged
   late: raise Input Offset. Mostly positive: lower it.
3. If the rockets look out of step with the music itself, change Audio Offset instead.

### Your own sounds
Short WAV files in a `calibration_sounds` folder replace the built-in clicks: `beat.wav` and
`subbeat.wav` for the calibration test, `tick.wav` for the assist tick. See the README in that folder.

## Song lists
Each song shows its length at the right of its bar.

## Command-line options
Settings can also be given when starting the game; these win over the saved settings.

| Option | Meaning |
|---|---|
| `--audio_offset_ms=<n>` / `--input_offset_ms=<n>` | The two offsets, in milliseconds |
| `--precise_input=false` | Judge presses by whole frames, as the original game did |
| `--assist_tick_volume=<n>` | Loudness of the assist tick (default 2.5) |
| `--show_song_length=false` | No track lengths on the song lists |
| `--skip_intro=false` | Show the start-up logos |
| `--ui_mods=false` | Turn off every menu addition (BBRR Settings, hit timing, renamed texts) |
| `--game_debug_log=true` | Write the game's own debug messages to the log |

## Files
- `boomboomrocket.log`, in the folder the game was started from: what happened in the last run.
- `settings.ini`, in the `boomboomrocket` folder inside your user folder: the saved settings.
  Delete it to return to the defaults.
