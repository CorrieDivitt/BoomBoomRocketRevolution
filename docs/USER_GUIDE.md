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
| | Glow | Off (default), Low, Medium, High: a soft glow around fireworks and other bright things, but not the HUD; changes at once |
| | Particle Density | A multiplier on the number of particles in each firework: 0.25x, 0.5x, 0.75x, 1x (default, the original amount), then 2x to 5x; 0x turns the fireworks' sparks off completely; changes at once |
| | HDR | Off (default) or On, for HDR displays with HDR switched on in Windows; changes at once |
| | Resolution | 720p, 1440p (default), 2160p, 2880p; needs a restart, the game offers one |
| Calibration | Auto Calibration | Two short tap tests (sound, then picture) that set the two offsets below |
| | Audio Offset | Moves the notes against the music. Positive = notes arrive later |
| | Input Offset | Moves how presses are judged. Positive = presses count as made earlier |
| | Show Accuracy | After each hit, shows how early (+) or late (-) it was in milliseconds; adds a timing line to the results screen |
| | Assist Tick | Plays a click at the exact moment of every rocket |
| Modifiers | Scroll Speed | 0.25x to 4x in steps of 0.25 (default 1x). Arrows travel faster and sit further apart, as in StepMania; the timing against the music does not change. During a song, **RB** raises it and **LB** lowers it one step, without pausing; the new speed shows briefly in the middle of the screen and is saved |
| | Rocket Acceleration | Default (the game's own movement) or Static (arrows move at one even speed along their path the whole way up). Changes at once, also during a song |
| | Horizontal Drift | Default (the original): arrows drift sideways towards where their firework will go off. Off: each arrow climbs straight up and stays in the screen column where it first appears, even when the camera swings, so none come in from the sides. Changes at once |
| | DDR Mode | Off (default) or On: a novelty mode like DDR. Arrows climb straight up in four lanes in the middle of the screen, sorted by button: Left (X), Down (A), Up (Y), Right (B). Two arrows of the same button on the same beat share a lane and overlap. Changes at once |
| | Background | Default or Off: when Off, during a song the city and sky are not drawn, so the arrows, fireworks and HUD are on black - for practising without distractions. Menus keep their city |
| | Autoplay | Off (default) or On: every rocket is hit automatically at exactly the right moment. While the song plays your own A, B, X and Y presses are ignored (they work again in the pause menu). For showing the game off and for testing - a song in which Autoplay was on at any moment saves no score, locally or on the leaderboards, and earns no achievements; and once it has been used the game does not save its profile again until it is restarted |
| (main page) | Skip Intro | Skips the start-up logos |
| | Menu Layout | Screen Edges (menus follow the window's edges on wide screens) or Centred |

The frame rate follows your display automatically; there is nothing to set.

### Getting the timing right
1. Run **Auto Calibration** once. Tap A on every beat of the sound test, then every time a bar
   reaches the line in the picture test.
2. Play a song with **Show Accuracy** on. If the numbers are mostly negative you are being judged
   late: raise Input Offset. Mostly positive: lower it.
3. If the rockets look out of step with the music itself, change Audio Offset instead.

### Your own sounds
Short WAV files in a `calibration_sounds` folder replace the built-in clicks: `beat.wav` and
`subbeat.wav` for the calibration test, `tick.wav` for the assist tick. See the README in that folder.

## Song lists
Each song's bar shows its length at the left and its tempo at the right. The tempo is the song's
BPM multiplied by the current scroll speed, so it tells you how fast the arrows will feel. On the
song select screen the scroll speed is shown to the right of the list, and **LB** / **RB** lower
and raise it there too.

## Command-line options
Settings can also be given when starting the game; these win over the saved settings.

| Option | Meaning |
|---|---|
| `--audio_offset_ms=<n>` / `--input_offset_ms=<n>` | The two offsets, in milliseconds |
| `--precise_input=false` | Judge presses by whole frames, as the original game did |
| `--assist_tick_volume=<n>` | Loudness of the assist tick (default 2.5) |
| `--show_song_length=false` | No track lengths or tempos on the song lists |
| `--skip_intro=false` | Show the start-up logos |
| `--ui_mods=false` | Turn off every menu addition (BBRR Settings, hit timing, renamed texts) |
| `--game_debug_log=true` | Write the game's own debug messages to the log |
| `--scroll_speed=<n>` | Scroll speed multiplier, any value from 0.25 to 8 |
| `--scroll_speed_buttons=false` | LB / RB do not change the scroll speed |
| `--no_background_draws=<list>` | Which draws No Background skips (vertex:pixel shader hash pairs); only needed if something is hidden that should not be, or the other way round |
| `--hdr_paper_white_nits=<n>` / `--hdr_peak_nits=<n>` | HDR: brightness of ordinary white (default 200) and of the brightest highlights (default: what the display reports) |

## Files
- `boomboomrocket.log`, in the folder the game was started from: what happened in the last run.
- `settings.ini`, in the `boomboomrocket` folder inside your user folder: the saved settings.
  Delete it to return to the defaults.
- `cache`, in the same folder: saved shaders. Safe to delete; the game clears it by itself if the
  previous start did not finish.
