# Prototype DLC packs — what they are and what they need (R-112)

Checked 2026-11-11 by unpacking the packages with `tools/stfs/stfs.py` and comparing files
byte for byte. Nothing unpacked is kept in the repo.

## Packages in the DLC folder
| File | Title in the package | Status |
|---|---|---|
| `6AEA31EE…D758` | Boom Boom Rock Pack | Retail pack. Works. |
| `A8A6F470…EB58` | Boom Rocket Test 2 | Prototype. Works after the automatic repairs below. |
| `_inactive\8DD68D00…2F58` | Boom Boom Rock Pack | Early build of the retail Rock Pack. Not needed. |
| `_unreadable\A8A6F470…EB58` | (none) | Same name and size as the prototype, but not a readable package: the header is not `LIVE`/`PIRS` and no file table can be found. |

## "Boom Rocket Test 2" (pack id 2)
80 files: five songs, each with three charts and seven or eight camera/launch/target paths,
18 firework models, and five language files.

**The music is not new.** Four of the five music files are byte-identical to songs in the
game; the fifth is a different encode of a song in the game.

| Prototype file | Game file | Music | Charts |
|---|---|---|---|
| `Moonlight_Sonata_2.xma` | `Moonlight_Sonata.xma` | identical | different on all three difficulties |
| `Beethovens_Fifth_2.xma` | `Beethovens_Fifth.xma` | identical | different |
| `Dvorak_9th_2.xma` | `Dvorak_9th.xma` | identical | different |
| `Valkyries_2.xma` | `Ride_Of_The_Valkyries.xma` | identical | different |
| `Flower_Duet_2.xma` | `Flower_Duet.xma` | different encode, 4 KB shorter | different |

The charts keep each song's tempo and length but place the rockets differently (for example
Valkyries Easy: 99 lines against 119 in the game; Flower Duet Easy: 128 against 118). So the pack
is a second set of charts for five shipped songs — most likely an earlier or alternative pass —
with test firework models (`Bonus_test4…6.mdl`, `XVI…XXX.mdl`).

What is wrong with it, and what the port already does about it at start-up (`src/dlc.cpp`):
| Problem | Handling |
|---|---|
| No licence bits, so the game ignores the pack | licence mask written |
| `contents.txt` has no `UnlockFireworkUSize/VSize` and no `UnlockFirework…Coord` keys | added |
| No `Unlock_firework_Icons.tga` | drawn from the pack's own shapes (see below); other packs without one borrow one, or get a blank one |
| Track 2 lists `William_Tell_2.xma`, which is not in the pack; `Beethovens_Fifth_2.xma` and its charts are in the pack but not listed | the track is pointed at the unlisted music file |
| Titles are placeholders (`Pack 2 Track 01 [NO TRANSLATE]`) | replaced: song title + "II", pack "Prototype Pack" |

Still to do for a proper clean-up:
- ~~Unlock icons: the borrowed sheet shows another pack's fireworks.~~ (2026-11-13) The prototype's
  15 unlockable shapes are Roman numerals XVI–XXX (the hidden-firework numbering carried on from
  the game's I–XV). A pack whose original `contents.txt` has no `UnlockFirework…Coord` keys now gets
  `Unlock_firework_Icons.tga` drawn at every start from its own shapes (`DrawIconSheet` in
  `src/dlc.cpp`: bold white strokes in alpha, 8×4 cells of 64 px like the retail sheets), and each
  track and difficulty is pointed at its shape's cell. Existing installs that borrowed the Rock
  Pack's sheet are fixed on the next start; retail packs are never touched. Tested on Linux with
  the real packs; not yet seen in game.
- Leaderboard ids 103–138 are the prototype's own; scores for these charts should stay local.
  Already true: every board is local (`src/leaderboards.cpp`). Keep these ids out of any online
  service (R-504).
- ~~Only the English language file has placeholder titles to replace~~ (2026-11-13): all five have
  them, and the French, German, Italian and Spanish files use `IDS_DLC_PACK_1_*` keys (copied from
  pack 1) where `contents.txt` asks for `IDS_DLC_PACK_2_*`. So those languages had no titles for
  the prototype songs, and the prototype's placeholders shared keys with the Rock Pack's real titles.
  `NamePlaceholderTitles` now renames another pack's key to the pack's own on placeholder lines only,
  then fills in the titles. Tested on the real files on Linux; not yet run in game (check a
  prototype song and a Rock Pack song with the game set to French).
- Decide the final titles ("II" suggests new music; "(Prototype)" would be more accurate).

## Early Rock Pack build (`_inactive`)
Identical to the retail Rock Pack — same 80 files, same charts, models and texts — except the
five music files. Those have no `fmt ` chunk (the retail files start `RIFF…WAVE fmt `, the early
ones `RIFF…WAVE data`) and a different `XMA2` chunk (44 bytes against 36); the audio data
is the same length but not byte-identical. It adds nothing over the retail pack, and its music
would need a `fmt ` chunk added before the game's player could be expected to open it.
Recommendation: leave it inactive.
