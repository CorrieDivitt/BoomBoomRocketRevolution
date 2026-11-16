# Boom Boom Rocket Revolution

<a href="https://recomplauncher.com/game/boomboomrocketrevolution"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

**Boom Boom Rocket (Xbox Live Arcade) as a native Windows game, played from your own copy**, smooth at
any refresh rate, sharper and wider than the original, with modern rhythm-game options, calibration,
local leaderboards and achievements.

**Version 1.0.0**

Boom Boom Rocket Revolution is a **static recompilation** of the original Xbox 360 executable, made with
the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk): the game's own code turned into a native
Windows program, not an emulator running it.

> **No part of Boom Boom Rocket is included here or in the download.** You need your own copy of the
> game (and any DLC you own). The launcher checks your files and installs them next to itself.

**38 new features and 38 new settings** on top of the original game, all listed below.

---

## What's in it

### Smooth play on modern screens
- **Judder fixed:** the original engine paced its frames unevenly, so arrows and the city stuttered even
  at a steady frame rate. Each frame now starts on a real refresh of your display, and the song clock is
  smoothed, so motion is even at 60, 120, 144 or 240 Hz.
- **High frame rates:** runs at your monitor's refresh rate (up to 240 fps), with the game's timing,
  fades and slow-downs kept at their original speed.
- **Frame Rate setting:** lock to 60, 120, 144 or 240, unlock it, or follow the monitor, so a slower PC
  with a fast monitor can still play steadily.
- **Any shape of screen:** widescreen and ultrawide (16:9, 21:9, 32:9 or match the window) with a wider
  view of the city instead of stretching; menus can sit at the screen edges or stay centred.
- **Higher resolutions:** 720p (the Xbox 360's own) up to 2880p.
- **Window modes:** fullscreen, windowed or borderless.

### Better-looking fireworks
- **City Lighting:** buildings near a firework light up in its colour, fading with distance (from subtle
  to "Acid Trip").
- **Water Glow:** the shine on the water takes the colour of the fireworks and moves to where they burst.
- **Glow:** a soft bloom around fireworks and bright lights, leaving the HUD crisp.
- **HDR output** for HDR displays.
- **Particle Density:** from no sparks at all up to five times the original amount.
- **Anti-aliasing** (FXAA) and **Texture Filtering** (up to 16x) for cleaner edges and sharper distant
  textures.

### Timing you can trust
- **Song clock locked to the music:** notes follow the audio actually being played, with no drift over a
  song.
- **1 ms button timing:** Xbox pad presses are timed to the millisecond on their own thread, not once a
  frame; PlayStation and other pads are read every millisecond so no press is lost or late.
- **Auto Calibration:** two short tap tests (sound, then picture) set your audio and input offsets.
- **Audio Offset and Input Offset** to fine-tune by hand.
- **Show Accuracy:** each hit shows how early or late it was in milliseconds, StepMania style, and the
  results screen adds your average and spread.
- **Offset suggestion:** after a song, the results screen tells you which way to move Input Offset if
  most of your hits were early or late.
- **Assist Tick:** a click on the exact moment of every rocket, to practise timing.

### Modifiers
- **Scroll Speed:** 0.25x to 4x; arrows travel faster and further apart while the timing stays the same.
  Change it mid-song with LB / RB, and see it on the song select screen.
- **Autoplay:** watch any song played perfectly (no scores or achievements are saved).
- **Background Off:** hides the city behind the arrows for distraction-free practice.
- **Camera Speed:** slows the camera's flight through the city, down to standing still, for anyone the
  moving background bothers.
- **Rocket Acceleration:** arrows speed up like rockets (the original) or climb at an even pace.
- **Horizontal Drift:** arrows drift sideways as in the original, or climb straight up their column.
- **DDR Mode:** arrows climb in four lanes in the middle of the screen, sorted by button.

### More of the game
- **Local leaderboards:** the Leaderboards menu works again, with every profile's best scores on this PC.
- **Achievements** unlock and can be viewed in the game.
- **Profiles:** up to 8 local players, each with their own name, saves, scores and achievements; two can
  play together.
- **Player 1 Controller:** choose which pad is Player 1.
- **Your DLC:** owned track packs install straight from the original package files, and the unfinished
  prototype packs are repaired so they play.
- **Visualizer that listens to your PC:** fireworks go off to whatever music Windows is playing, with a
  sensitivity setting and a camera that pulls back for a wide view.
- **Song lengths and tempos** on the song lists.
- **Skip Intro:** straight to the title screen.

### Controls
- **Any controller** Windows knows: Xbox pads, PlayStation, Switch Pro and most others (button maps from
  SDL_GameControllerDB are included).
- **Keyboard play**, with every button rebindable in the game.

### The launcher
One window takes you from your game files to playing:
1. **Game files:** choose `default.xex` in your unpacked game folder, or the game's package file exactly as it
   came from your Xbox 360. Every file is checked against the retail copy.
2. **DLC (optional):** add your DLC package files (or drop them on the window).
3. **Install:** the files are copied next to the launcher.
4. **Game Settings:** your gamertag and the video settings, before you start.
5. **Play.**

### In-game settings
Everything is in the game: **Help & Options > BBRR Settings**. A saves, B leaves without saving, X restores
the defaults. Settings are saved in `Documents\boomboomrocket\settings.ini`.

---

## Playing

### You need
- Windows 10 or 11, 64-bit, and a **DirectX 12** graphics card.
- Your own copy of **Boom Boom Rocket** (Xbox 360 XBLA): the unpacked game folder with `default.xex`, or
  the original package file. DLC packages are optional.
- A controller is recommended; the keyboard works too.

### Install
1. Download `BoomBoomRocketRevolution-v<version>-windows-x64.zip` from the
   [Releases page](https://github.com/CorrieDivitt/BoomBoomRocketRevolution/releases).
2. Unzip it to a folder of its own, for example `C:\Games\BoomBoomRocket`.
3. Run `BoomBoomRocketRevolution.exe` and follow the Setup tab from top to bottom.

### Updating
Unzip the new version over the old folder. Your game files, DLC, saves, profiles and settings are kept.

### Controls

| | Controller | Keyboard |
|---|---|---|
| A / B / X / Y (play the arrows) | A / B / X / Y | S / D / A / W (as on the pad), Enter is A too, Backspace is B too |
| Menus | D-pad | Arrow keys |
| Scroll speed down / up (in a song) | LB / RB | Q / E |
| Bonus Run | Both triggers | Space |
| Pause | Start | Esc |
| Back | Back | Tab |
| Visualizer: zoom in / pull back / pull back further | RB / A / X | E / S / A |

Keys are changed in **BBRR Settings > Keyboard**.

### Settings

| Page | Setting | What it does |
|---|---|---|
| **Profiles** | Player 1 / Player 2 | Who is playing; each profile has its own saves, scores and achievements |
| | Player 1 Controller | Which pad plays as Player 1 |
| | Rename Player 1 / 2, New Profile | Change a gamertag or add a player (up to 8) |
| **Calibration** | Auto Calibration | Two short tap tests that set both offsets for you |
| | Audio Offset | Moves the notes against the music |
| | Input Offset | Moves how your presses are judged |
| **Video Settings** | Resolution | 720p up to 2880p (restart) |
| | Window Mode | Fullscreen, windowed or borderless |
| | Aspect Ratio | Match the window, 16:9, 21:9 or 32:9 |
| | Frame Rate | 60, 120, 144, 240, unlocked, or match the monitor (restart) |
| | HDR | Output for HDR displays |
| | Anti-Aliasing | Smooths jagged edges (FXAA) |
| | Texture Filtering | Keeps distant textures sharp |
| | Particle Density | How many sparks each firework throws |
| | City Lighting | Lights buildings in the colour of nearby fireworks |
| | Water Glow | Colours the water with the fireworks |
| | Glow | A soft glow around bright things |
| **Modifiers** | Scroll Speed | How fast the arrows travel |
| | Show Accuracy | Shows how early or late each hit was |
| | Assist Tick | A click on every rocket |
| | Autoplay | The game plays the song for you |
| | Background | Hides the city during songs |
| | Camera Speed | How fast the camera moves through the city |
| | Rocket Acceleration | Rockets speed up, or climb at an even pace |
| | Horizontal Drift | Arrows drift sideways, or climb straight up |
| | DDR Mode | Four lanes in the middle of the screen |
| | Visualizer Sensitivity | How strongly the Visualizer reacts to your music |
| **Keyboard** | Use Keyboard, D-pad, key for each button | Play with the keyboard and choose its keys |
| | Skip Intro | Skips the start-up logos |
| | Menu Layout | Menus at the screen edges, or centred as on a 16:9 TV |

The full details of every setting and the command-line options are in [docs/USER_GUIDE.md](docs/USER_GUIDE.md).

---

## Things to know (from testing)

**Windows Defender or SmartScreen may warn about the launcher or the game.** They're new and not
code-signed. For SmartScreen ("Windows protected your PC"), click **More info → Run anyway**. The source
code is all here to check.

**On a slower PC, start with 720p** (the default) **and Frame Rate 60.** A GTX 1650 ran about 86 fps at
720p and about 33 fps at 1440p. A steady 60 plays better than an uneven higher rate.

**Calibrate once on each PC.** Speakers, headphones, TVs and controllers all add their own delay:
BBRR Settings > Calibration > Auto Calibration takes a minute.

**If something goes wrong**, the logs (one per start) and any crash dumps are in
`Documents\boomboomrocket\logs`. Please attach the newest log, and the crash dump if there is one, to an
[issue](https://github.com/CorrieDivitt/BoomBoomRocketRevolution/issues).

**Known issues:** in Endurance, Autoplay misses the first notes of each lap. Freestyle mode has not had
the same polish as the other modes.

---

## Building from source (developers)

The release is ready to play; this is only for working on the port itself.

Prerequisites: Visual Studio 2022 (*Desktop development with C++*, which includes CMake and Ninja),
LLVM/Clang 20+ installed separately (VS 2022 bundles Clang 19), Git, Python 3.

```powershell
git clone --recursive https://github.com/CorrieDivitt/BoomBoomRocketRevolution
cd BoomBoomRocketRevolution
# put your unpacked game in "Game Files\" (git-ignored)
powershell -ExecutionPolicy Bypass -File scripts\bootstrap.ps1
powershell -ExecutionPolicy Bypass -File scripts\patch-sdk.ps1
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

`patch-sdk.ps1` applies the port's changes to the ReXGlue SDK (`patches/rexglue-sdk/`) and installs it;
run it again whenever a patch is added. `build.ps1` runs the code generation and builds the game and the
launcher.

**Make the release zip** (refuses to include any game data; also writes a checksum and a symbols zip for
reading crash dumps):

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -Version 1.0.0
```

### Project layout

| Path | What it is |
|---|---|
| `src/` | The port's features: settings, timing, modifiers, lighting, profiles, leaderboards, DLC, crash reports |
| `launcher/` | The launcher (Dear ImGui on Direct3D 11) |
| `ui_scripts/` | The port's menu screens and changes to the game's own (Lua) |
| `patches/rexglue-sdk/` | Changes to the ReXGlue SDK, applied by `scripts\patch-sdk.ps1` |
| `config/` | Code-generation settings and hooks into the game's code |
| `calibration_sounds/` | Metronome sounds for Auto Calibration |
| `docs/` | Plan, analysis of the game, timing and graphics notes, user guide, requirements |
| `tools/` | XEX analysis and shader-source recovery helpers |
| `scripts/` | Setup, build and packaging |
| `thirdparty/` | ReXGlue SDK (submodule) and SDL_GameControllerDB |

---

## AI use
Yes, this project uses AI. Much of the code and documentation was written with Claude (Anthropic),
directed and tested in play by the project owner.

## Credits
ReXGlue SDK, Xenia, XenonRecomp/XenosRecomp, and the Xbox 360 preservation community.

## Licence
- Boom Boom Rocket Revolution's own code: [BSD 3-Clause](LICENSE).
- The ReXGlue SDK, Xenia, FFmpeg, SDL, Dear ImGui and the other software it is built on are listed with
  their licences in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Boom Boom Rocket is © Electronic Arts. This project is unofficial and not affiliated with or endorsed by
Electronic Arts, Bizarre Creations or Microsoft. No game files are included.
