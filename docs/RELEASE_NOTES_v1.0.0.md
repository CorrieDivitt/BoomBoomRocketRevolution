# Boom Boom Rocket Revolution 1.0.0

The first public release: **Boom Boom Rocket (Xbox Live Arcade) as a native Windows game**, made by
static recompilation of the original executable. **No game files are included**: you need your own copy
of Boom Boom Rocket (the unpacked game folder with `default.xex`, or the original package file from your
Xbox 360). DLC is optional.

**38 new features and 38 new settings** on top of the original game.

## Highlights
- **Smooth at any refresh rate:** the original engine's frame judder is fixed and the song clock is
  smoothed, at 60, 120, 144 or 240 Hz. A Frame Rate setting lets slower PCs lock to a steady rate.
- **Any screen:** widescreen and ultrawide (16:9, 21:9, 32:9), 720p up to 2880p, fullscreen, windowed or
  borderless, HDR output.
- **Better-looking fireworks:** City Lighting, Water Glow, Glow, Particle Density (0x to 5x), anti-aliasing
  and texture filtering.
- **Timing you can trust:** song clock locked to the music, 1 ms button timing, Auto Calibration, audio
  and input offsets, Show Accuracy (ms per hit), an offset suggestion on the results screen, Assist Tick.
- **Modifiers:** Scroll Speed (change it mid-song with LB / RB), Autoplay, Background Off, Camera Speed,
  Rocket Acceleration, Horizontal Drift, DDR Mode.
- **More of the game:** local leaderboards, achievements, up to 8 local profiles and two-player play,
  your DLC from the original packages (prototype packs repaired), a Visualizer that reacts to music
  playing on your PC, song lengths and tempos on the song lists.
- **Controls:** Xbox, PlayStation, Switch Pro and most other controllers, plus keyboard with rebindable keys.
- **Launcher:** checks your game files and DLC, installs them, and sets the video options before you play.

The full feature list and settings are in the [README](https://github.com/CorrieDivitt/BoomBoomRocketRevolution#readme).

## Download
| File | What it is |
|---|---|
| `BoomBoomRocketRevolution-v1.0.0-windows-x64.zip` | The game and launcher. Unzip to a folder of its own and run `BoomBoomRocketRevolution.exe` |
| `BoomBoomRocketRevolution-v1.0.0-windows-x64.zip.sha256` | Checksum of the zip |
| `BoomBoomRocketRevolution-v1.0.0-symbols.zip` | Debug symbols, only for reading crash reports (players don't need it) |

## You need
- Windows 10 or 11, 64-bit, and a DirectX 12 graphics card.
- Your own copy of Boom Boom Rocket.

## Good to know
- Windows SmartScreen may warn because the programs aren't code-signed: **More info → Run anyway**.
- On a slower PC keep 720p (the default) and set Frame Rate to 60.
- Run **Auto Calibration** (BBRR Settings > Calibration) once on each PC.
- Logs and crash dumps are in `Documents\boomboomrocket\logs`; please attach the newest log to any
  [issue](https://github.com/CorrieDivitt/BoomBoomRocketRevolution/issues).
- Known issues: in Endurance, Autoplay misses the first notes of each lap; Freestyle mode has had less
  polish than the other modes.

This project uses AI: much of the code and documentation was written with Claude (Anthropic), directed
and tested in play by the project owner.

Boom Boom Rocket is © Electronic Arts. This project is unofficial and not affiliated with or endorsed by
Electronic Arts, Bizarre Creations or Microsoft.
