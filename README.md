# Boom Boom Rocket Recompiled

An unofficial native Windows port of the Xbox 360 Xbox Live Arcade game **Boom Boom Rocket** (Bizarre Creations / Electronic Arts, 2007), made by **static recompilation** of the original executable with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

> **Status:** planning / early setup. See [`docs/PLAN.md`](docs/PLAN.md) and the requirements list in [`docs/requirements.csv`](docs/requirements.csv).

## Goals
- Smooth, high-frame-rate play (144/240 Hz+) with gameplay identical to the original
- Any resolution and aspect ratio, plus graphics options (render scale, AA, filtering, bloom/lighting upgrades)
- Timing that stays locked to the music: audio-clock-driven beats, audio and input offsets, calibration
- Local and online leaderboards; achievements

## You need your own copy
This repository contains **no game files**. You must dump your own legally owned copy of Boom Boom Rocket from an Xbox 360 and place its extracted contents (including `default.xex`) in `Game Files/`. That folder is git-ignored.

## Building (Windows)
Prerequisites: Visual Studio 2022 (*Desktop development with C++*, which includes CMake and Ninja), LLVM/Clang 20+ installed separately (VS 2022 bundles Clang 19), Git, Python 3.
```powershell
git clone --recursive https://github.com/CorrieDivitt/BoomBoomRocketRevolution
cd BoomBoomRocketRevolution
# put your dump in "Game Files\"
powershell -ExecutionPolicy Bypass -File scripts\bootstrap.ps1
# then build (codegen runs automatically as part of the build)
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

## Layout
| Path | Contents |
|---|---|
| `docs/` | Plan, analysis, requirements tracker data |
| `tools/xex`, `tools/shaders` | XEX analysis and shader-source recovery helpers |
| `scripts/` | Setup scripts |
| `thirdparty/rexglue-sdk` | ReXGlue SDK (submodule) |

## AI use
Yes, this project uses AI. Much of the code and documentation was written with Claude (Anthropic),
directed and tested in play by the project owner.

## Credits
ReXGlue SDK, Xenia, XenonRecomp/XenosRecomp, and the Xbox 360 preservation community. Boom Boom Rocket is © Electronic Arts; this project is not affiliated with or endorsed by EA, Bizarre Creations or Microsoft.

## License
The project's own code is under the [BSD 3-Clause License](LICENSE). Third-party software it uses
(ReXGlue SDK, Xenia, FFmpeg, SDL and others) is listed with its licenses in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The game itself is not covered and is not
included: you need your own copy of Boom Boom Rocket.
