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
| No `Unlock_firework_Icons.tga` | copied from another pack, or a blank one is made |
| Track 2 lists `William_Tell_2.xma`, which is not in the pack; `Beethovens_Fifth_2.xma` and its charts are in the pack but not listed | the track is pointed at the unlisted music file |
| Titles are placeholders (`Pack 2 Track 01 [NO TRANSLATE]`) | replaced: song title + "II", pack "Prototype Pack" |

Still to do for a proper clean-up:
- Unlock icons: the borrowed sheet shows another pack's fireworks.
- Leaderboard ids 103–138 are the prototype's own; scores for these charts should stay local.
- Only the English language file has placeholder titles to replace; check the other four in game.
- Decide the final titles ("II" suggests new music; "(Prototype)" would be more accurate).

## Early Rock Pack build (`_inactive`)
Identical to the retail Rock Pack — same 80 files, same charts, models and texts — except the
five music files. Those have no `fmt ` chunk (the retail files start `RIFF…WAVE fmt `, the early
ones `RIFF…WAVE data`) and a different `XMA2` chunk (44 bytes against 36); the audio data
is the same length but not byte-identical. It adds nothing over the retail pack, and its music
would need a `fmt ` chunk added before the game's player could be expected to open it.
Recommendation: leave it inactive.
