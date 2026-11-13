# Store submissions: Quiver Launcher and RecompLauncher

What the two catalogs need and what is ready for them. Prepared 2026-11-12, before the
1.0 release exists.

## How both catalogs work
Both list ports that are published as **GitHub releases**. Neither hosts game files: the
player supplies their own copy.
- **Quiver Launcher** (quiverlauncher.com) downloads the release asset, records each file's
  checksum when the release comes out, and offers updates only after a release has been out
  for 48 hours. It also runs a VirusTotal scan. Its catalog entries describe how the app is
  installed (see the fields below). The submit page could not be read automatically (the
  site blocks it), so the field list comes from Quiver's open-source catalog client
  (`packages/api` in tgeorgiadis/quiver-launcher); check the form for anything extra.
- **RecompStore / RecompLauncher** (recompstore.com) asks for the GitHub link, the project's
  details and pictures. Everything entered appears on the store page. Its "How it works"
  page says ports come from the developer's GitHub release and the store puts the player's
  game files where the port expects them; it does not publish technical rules. The more is
  filled in, the sooner a listing goes live.

## What the release looks like
`scripts\package.ps1 -Version 1.0.0` makes `out\release\BoomBoomRocketRevolution-v1.0.0-windows-x64.zip`
(plus a `.sha256` file and a separate symbols zip). Files at the top level:

| File | What it is |
|---|---|
| `BoomBoomRocketRevolution.exe` | The launcher: the program to start |
| `boomboomrocket.exe` + `*.dll` | The game |
| `ui_scripts\` | The port's menu scripts |
| `README.txt`, `USER_GUIDE.md`, `LICENSE.txt`, `THIRD_PARTY_NOTICES.md` | Documents |

The script refuses to package if any game data slipped in. The launcher installs the
player's files into the same folder (`Game Files`, `DLC`), so updating the zip in place keeps
them.

## Quiver catalog fields (prepared values)
| Field | Value |
|---|---|
| Name | Boom Boom Rocket Revolution |
| Project type | port |
| Game | Boom Boom Rocket (developer Bizarre Creations, publisher Electronic Arts, Xbox Live Arcade, April 2007) |
| Console (`consoles`, `basedOn.console`) | Xbox 360 (pick Quiver's Xbox 360 id on the form) |
| Edition (`basedOn.edition`) | Xbox Live Arcade |
| Supported OS | Windows (x64) |
| Repository | CorrieDivitt/BoomBoomRocketRevolution |
| `folderName` | `BoomBoomRocket-Revolution` (Quiver asks for Title-Project, no "-Recomp" suffix) |
| `releaseAssetFilter` | `*windows-x64.zip` (keeps the symbols zip out) |
| `preferredExecutables.windows` | `BoomBoomRocketRevolution.exe` |
| `setupFiles` | None needed: the launcher asks for the game itself. If the form insists, use the values below |
| Tags | rhythm, music, recompilation, xbox-360, xbla |
| AI use (`aiLevel`) | **Your call, and it is checked against the README.** Quiver records none / assisted / generated, from the developer or from signals in the repo. Much of this port's code was written with Claude, so "generated" or "assisted" is the honest answer; saying it up front avoids a mismatch later |

Optional `setupFiles` entry (only if Quiver should check the copy itself):
- label "Boom Boom Rocket (default.xex)", kind file, extensions `xex`, required
- accepted dump: sha256 `b1bc45589cec79e375e23fc48c122fa5902a2dfd6e3c9c765064dcd9e1a8709b`
  (sha1 `dbec2450d197e120b7d44fefd1caf620a592af1d`): the retail XBLA default.xex, title
  5841086A. This is the only version the port has been tested with.

Quiver also offers library art fields (`capsule`, `header`, `hero`, `logo`). Their sizes
are not in the client code; Steam's library sizes (capsule 600x900, header 920x430, hero
3840x1240, transparent logo) are a safe guess until the form says otherwise.

## RecompStore form (prepared text)
- **GitHub link:** https://github.com/CorrieDivitt/BoomBoomRocketRevolution
- **Type:** game port. **Platform:** Windows.
- **Short description:** A native Windows port of the Xbox Live Arcade rhythm game Boom Boom
  Rocket, with high frame rates, any resolution and aspect ratio, modern rhythm-game options
  and improved lighting. Requires your own copy of the game.
- **Long description:** Boom Boom Rocket Revolution is a static recompilation of Boom Boom
  Rocket for Windows. It plays the original game at your monitor's refresh rate and any
  resolution, in widescreen and ultrawide, with a smoothed song clock and audio/input
  calibration so notes line up with the music. It adds a scroll-speed modifier (changeable
  mid-song with LB/RB), camera speed and accessibility options, Autoplay, local profiles and
  two-player play, local leaderboards, keyboard support, and new lighting: fireworks light up
  the city and the water. DLC packs work from the original package files, unfinished
  prototype packs included. A launcher checks your game files and DLC and sets everything up.
- **What you need:** your own Boom Boom Rocket, either the unpacked game folder (with
  default.xex) or the game's package file from your Xbox 360; DLC packages optional.
- **Pictures:** to make. Screenshots of the port running (city lighting and water glow at
  night, an ultrawide shot, the BBRR settings menu, the launcher) and a logo or banner.
  Use our own captures and our own artwork, not the official box art or logos.

## Still to do before submitting
1. Release 1.0 on GitHub with the packaged zip (R-707). Both catalogs read the release.
2. Screenshots and a banner or logo of our own.
3. ~~Decide the AI-use answer~~ Decided 2026-11-13: "Yes, this project uses AI" (README section "AI use"); Quiver `aiLevel`: **generated**.
4. ~~A short "Requirements / How to install" section in the README~~ Done 2026-11-13 (README rewritten in the style of the ShadowGreat README).
5. Say where players can still get the game. It was delisted from the Xbox 360 store in early 2021; the
   Xbox One backward-compatible listing is what remains, so most players will bring a copy from an
   old console. (Source: delistedgames.com.)
6. Expect a Windows SmartScreen warning on first run (the exes are not code-signed). Worth a
   line in the README; code signing is optional and costs money.
