# Graphics upgrades — research notes (R-405, R-408 to R-412)

Written 2026-11-11 from the recovered shader sources, the game files and the SDK's GPU code.
Nothing here has been built or tried; where something could only be confirmed in a running game
it says so.

## Short answers
- **HDR: yes, and the game suits it unusually well.** Fireworks are drawn as glowing sprites added
  on top of each other. Today that sum is cut off at white; in an HDR pipeline the same sum simply
  becomes "brighter than white". There are three levels, from free to substantial (below).
- **Particles and lighting: plenty of room.** Particle counts and textures are plain data files.
  The city is drawn with one flat colour for the whole scene, so even simple per-firework lighting
  would be a visible step up.
- **Ray tracing: possible in a limited form, not worth leading with.** Full path tracing (RTX
  Remix style) does not apply to this game. Ray-traced shadows and reflections from the fireworks
  could be added later on top of the lighting work; most of the visual gain comes before that.

## How the game draws a frame today
All of the game's shaders are known: the nine recovered HLSL sources cover every shader file the
executable loads, plus the stock `SimpleShaders.fxobj`.

| What | Shader | How it works |
|---|---|---|
| Fireworks, sparks, trails, street lights | `PointSprite` | Point sprites, added to the picture. Colour = vertex colour x texture, plus a white core scaled by an `Overbright` constant. Size shrinks with distance. |
| Smoke | `Smoke` | Sprites with the same `Overbright` term |
| City | `TextureModDiffuse` | Texture x one **scene-wide ambient colour** (constant c16). The per-vertex colour is commented out in the source. |
| Arrow icons | `Icon` | Textured quads |
| Menus and HUD | `UiFlorescence`, `UiMultiTexture` | 2D quads; three colour tints per texture channel |

Things that follow from this:
- **There is no bloom or other post-processing in the original.** The sprite shader writes a "bloom"
  value into alpha, but no shader reads it; it looks like a feature that was dropped.
- **The city has no real lighting.** Its vertices carry position, colour and texture coordinate only
  (no normals). Start-up builds a shadow map and the street lights once
  ("BuildShadowMap & BuildStreetLights"), and fireworks can only tint the whole city at once through
  the ambient colour.
- **Particle look is data.** `Fireworks/*.config` gives each firework type a particle count, speed,
  trail and sparkle setting per judgement tier (peony: 90 / 110 / 140 particles). Sprites come from
  `particles.dds` (a 128 x 2048 strip), `particle.dds` and `trail.dds` (128 x 128).
- **City assets:** one 1.8 MB mesh (`city.bbr`) and 126 textures, including glass floors, a pool, a
  river boat and a liner - surfaces where reflections would show.
- The game renders 1280x720; the port already scales that up (Resolution setting) and stretches it
  for wide screens.

Particle limits (traced 2026-11-11): sparks live in a system of their own that stops making new
ones at 70,000 alive; its vertex buffers are sized for exactly that. They are kept in blocks of 128,
2,048 blocks at most. A firework's `Count` becomes the emitter's rate, so it scales the number of
sparks directly. Two smaller pools (32,768 rockets and bursts, 16,384 trail segments) stay in the
hundreds whatever the setting. At 1x a busy moment reaches about half the limit; from about 3x up, busy sections reach it. Running at
the limit crashed the game twice, the second time after about 50 seconds with the GPU being handed
garbage shaders, which points at memory being overwritten. The Particle Density setting therefore
tops out at 5x. The port also stops adding sparks at 68,000 (`--particle_limit`), just short of the game's own
limit; a full song at 5x ran at that level without a crash. The crash left garbage in the saved shader cache, which then crashed every start-up;
the port now clears that cache when the previous start did not finish. The log line `[particles] last ... s`
shows sparks and blocks against their limits.

Not yet known (needs a look in a running game): the colour format of the game's render target and
whether it uses multisampling.

## HDR
### Level 1 - Windows Auto HDR (nothing to build)
Windows 11 can convert SDR games to HDR by itself for DirectX 11 and 12 games, and this port
presents through DirectX 12. If you have an HDR display: turn on **Use HDR** and **Auto HDR** in
Windows display settings, start the game and see whether the Auto HDR notice appears. Windows
decides per game, so it may or may not engage. It only stretches the finished SDR picture, so
clipped firework cores stay clipped - but it costs nothing to try and shows whether HDR suits the
game.

### Level 2 - HDR output from the port (medium)
Do the conversion ourselves, where we control it:
- The SDK's presenter hands the game picture to the window as an 8-bit image
  (`kSwapChainFormat = B8G8R8A8`), although the picture arrives as 10-bit. Change the swap chain to a
  10-bit HDR10 (or 16-bit float) format and set the HDR colour space when the display supports it.
- Add one step in the same place: expand highlights (inverse tone mapping) and add bloom, with
  the HUD kept at a fixed "paper white" so menus do not glare.
- This is an SDK patch in one area (`src/ui/d3d12/d3d12_presenter.cpp` and the final paint shader),
  plus two settings (HDR on/off, peak brightness). No game-side changes.
- Limit: still built from the clipped SDR picture, like Auto HDR, but tuned for fireworks and with
  bloom.

### Level 3 - true HDR rendering (large)
Keep the brightness the game already computes instead of clipping it:
- Make the game's colour target a 16-bit float buffer. The SDK already has float targets for games
  that ask for them (`R16G16B16A16_FLOAT` in the render target cache); the work is making this
  game's 8-bit target use one, and carrying float values through the copy to the frame that is
  presented. Additive sprites then add up past white, and the `Overbright` cores keep their
  intensity.
- Tone map and bloom at the end (the bloom from level 2, now fed real highlights), then HDR10 output.
- Touches the render target cache, the resolve/copy path, the texture the game reads the frame
  back from, and presentation. Highest risk of the three, because it changes how the emulated GPU
  stores every pixel; it needs the unknowns above settled first.
- Works for SDR displays too: the same float picture tone-mapped to SDR gives soft roll-off into
  white instead of flat clipping. So this is the foundation for "improved lighting effects" whether
  or not the player has an HDR screen.

## Better particles
In rising order of effort:
1. **More particles** - scale `Count` in the firework configs (or at load time, so the files stay
   untouched) with a quality setting. First check the particle system's limits; the executable has a
   "Failed to initialise particle system" path, so there is a fixed budget somewhere.
2. **Sharper sprites** - the sprite textures are 128 pixels. At 1440p and above a large firework
   sprite is magnified well past that. Replace them with higher-resolution versions through a
   texture override (R-413).
3. **Glow** - bloom (HDR level 2 or 3) is the single biggest change to how fireworks read.
4. **Native particle shader** - with the source in hand, the sprite shader can be replaced on the PC
   side: smoother fall-off, colour that stays saturated when sprites pile up, soft edges where
   sprites meet buildings. The SDK identifies each game shader by a hash of its code, so a
   replacement table keyed by that hash is a natural SDK patch; it does not exist yet.
5. **Sprite size cap** - the port already carries a patch for sprite size at high resolutions;
   very large close-up sprites should be checked once counts and resolution go up.

## Better lighting
The goal: buildings near a firework light up in its colour, instead of the whole city changing tint.
- Each explosion is a known event in the game (the function that spawns the firework,
  `0x82075E18`, reads the rocket's position; where it takes the colour from still has to be traced),
  so a short list of "lights" - position, colour, brightness
  fading over about a second - can be kept on the PC side. A dozen at once is plenty.
- Replace the city shader with one that adds those lights by distance. The mesh has no normals, but
  a pixel shader can derive a flat normal from the surface itself, which suits blocky buildings.
  The replacement vertex shader has to pass the world position along.
- Needs the same shader-replacement patch as the particle shader, plus a way to hand our light list
  to the shader. No changes to game data.
- Optional extras once that works: reflections of fireworks in the glass, pool and water surfaces
  (screen-space first), and light on the smoke.

## Ray tracing
- **RTX Remix does not apply.** It works by taking over DirectX 8/9 games that use the old
  fixed-function pipeline and rebuilding their scene. This game is shader-based and reaches the PC's
  GPU through an emulated Xbox GPU on DirectX 12, so there is nothing for Remix to hook.
- **What is possible:** a hybrid. The city is one static mesh, so it can be turned into a ray-tracing
  structure once. The replaced city shader can then ask, per pixel, "is this firework visible from
  here?" (DirectX ray queries) and "what does this glass reflect?". That gives real shadows from
  buildings between a firework and a wall, and true reflections on glass and water.
- **What it needs:** everything in "Better lighting" first, plus getting the city's triangles in
  world space (from `city.bbr` or by capturing the draw calls), and a DXR-capable GPU.
- **Is it worth it?** Fireworks are short, bright and mostly in open sky; the city is a dark
  backdrop. Distance-based lighting, bloom and screen-space reflections deliver most of what the eye
  would notice. Ray-traced shadows and reflections would be a showcase extra, not a foundation -
  best treated as a stretch goal after the rest is in.

## Suggested order
| Step | What | Size | Depends on |
|---|---|---|---|
| 1 | Try Windows Auto HDR; capture the render target format and particle limits in a running game | S | you, at the PC |
| 2 | Particle count / quality setting | S-M | limits from step 1 |
| 3 | HDR output with bloom (level 2) | M | SDK presenter patch |
| 4 | Shader replacement by hash (SDK patch), native particle shader | M-L | |
| 5 | Per-firework lighting on the city | L | step 4 |
| 6 | True HDR rendering (level 3) | L-XL | steps 1, 3 |
| 7 | Screen-space reflections | M | step 5 |
| 8 | Ray-traced shadows and reflections | XL | steps 5, 7 |

Anti-aliasing (R-405): FXAA is already selectable. The SDK also contains CAS and FSR output
filters (`GuestOutputPaintConfig`), which could be exposed as a "Sharpening" setting with little work.

## Water reflections (researched 2026-11-12, from the draw log of a frame in play)
How the water works today: each frame the game first draws a **mirror pass** - the sky dome
(`04F2/B36F`), the city (`C837/2B08`, about 110 draws, plus `6773/88B6`) and two small point-sprite
draws (`1DB9/C471`, 1 and 213 points close to the ground: street lights or rockets on their launch
pads) - with the camera flipped about the water plane (y = -100; the view matrix's second row is
negated). That picture is then sampled by the water quad (`B90D/FB9F`), drawn after the normal city.

What is missing from the mirror pass: the **bursts** (`BA59/7C30`, up to ~19,000 points per frame) and
the **lit smoke** (`BBFF/9567`). (Correction: `63B4/4B1E` is not the sparks - its pixel shader multiplies
by c11.x = 0, so it draws nothing; probably a star field that is switched off.) They are only drawn in
the normal pass, after the water. So the water cannot show them: they are not in the picture it reads.

How it could be done, cheapest first:
1. **Firework light on the water (small).** The water shader has its own light constants
   (`pc0` position, `pc4` colour). Feeding it the same light City Lighting gives the buildings would
   wash the firework colours over the water near a burst. Not a reflection - no shapes - but cheap,
   low risk, and it reuses patch 0011.
2. **Real reflections of the sparks (medium).** Built 2026-11-12 in a simpler form first (patch 0019):
   the burst draw is issued again inside the emulated GPU with the mirror camera. See the tracker (R-417).
   Have the game draw its sparks (and possibly smoke)
   a second time during the mirror pass, with the mirrored camera. Best done on the game side: find
   the game function that draws the spark system and the one that runs the mirror pass, and call
   the first from inside the second, so it picks up the mirrored camera the way the street lights do.
   Steps: a probe build that logs which game functions issue those draws (the return addresses of
   the draw calls); then the change; then checks. Risks: the mirror picture may be smaller than the
   screen, so single sparks may shimmer or vanish in it; the sparks cost twice the GPU time
   (up to 68,000 points at Particle Density 5x); the sparks' own sizing and fading use the normal
   camera's distance, which may look slightly off in the mirror.
   Patch 0022 (2026-11-13): the copy only showed from steep views (about 50 degrees down or more)
   and vanished as the camera levelled out. The burst shader sizes each spark by 1 / clip z
   (`PointSprite.hlsl`), so the copy needs the reflected point's real depth, which the borrowed
   mirror-pass matrix only gave for steep views. The copy now uses the burst matrix reflected about
   the water plane y = h (clip_i = dot(c_i, p), so each register's .y is negated and .w gains 2h .y);
   h is measured each frame from the mirror pass (-100). Still no depth test: the copy shows over
   buildings in front of the water. Making it hide behind them is the next step (draw it into the
   mirror picture, or test it against depth at the water surface).
   Test of 0022: no real change. The log showed the mirror pass's matrix is a plain reflection
   about y = -50 (not -100 as noted above), so the depth theory was wrong, and the draw list puts the
   water (draw 1504) before the bursts (1513), so the water does not cover the copy either. Patch
   0023 turns user clip planes off for the copy (a plane left on from the mirror pass would cut away
   everything below the water, and how much depends on the angle) and logs the burst draw's
   `PA_CL_CLIP_CNTL` and planes.
   Test of 0023: no change; the burst draw has no user clip planes (`PA_CL_CLIP_CNTL 00080000`), and
   with clipping on the draw-extent estimator uses the viewport, so it does not trim the copy either.
   Patch 0024 measures instead of guessing: every 3 seconds it runs the burst vertex shader on the
   CPU (`ShaderInterpreter`) for about 400 sparks, with the normal and the copy's constants, and logs
   where they land (`[bbrr] reflection probe`): in front of the camera, on screen, pixel rows and
   columns, depth, point size.
   Run of 0024: no numbers; the SDK's shader interpreter refuses the burst shader ("shader cannot
   be run on the CPU"). Patch 0025 does the maths itself: it reads each sampled spark's position from
   the vertex buffer (first three-float attribute of the shader's vertex bindings) and applies
   `PointSprite.hlsl` (clip_i = dot(c_i, (p, 1)); size factor (1 - c7.x) + 1000 c7.x / clip.z). It
   also logs the sparks' world heights, the viewport and the vertex layout.
3. **Replaying the draws on the GPU side (large).** Copy the spark draw into the mirror pass inside
   the emulated GPU. Avoids finding the game functions but means re-creating render state the GPU
   emulation does not keep around; not recommended.
