# Gen4: an application's alpha test with depth writes is refused and dropped, so Half-Life's grates and doors and parts of Incoming vanish

Date: 2026-11-11. Status: **open**. Fixed on branch `gen4-alpha-depth`
and measured on the GM45 (boot 19, below); the GM965 is unchecked.

Machine: issue 22's reporter's HP t5740 (`M-6B21TJ`, W9QI3DAB), GM45
`8086:2A42`, boot 17, driver and HAL `0.45.0 03782be9` (from the
snapshot's `DriverBuild`/`HalBuild`; V9XTRACE is the older
`0.44.0 e9d361b5-dirty` left in `C:\V9XSTAGE`). Captures:
[`../probe/m-6b21tj-game-reports-2026-11-11/`](../probe/m-6b21tj-game-reports-2026-11-11/).
Follows the Forsaken record,
[`2026-11-08-gen4-keyed-depth-write-draws-dropped-forsaken.md`](2026-11-08-gen4-keyed-depth-write-draws-dropped-forsaken.md).

## Report

The owner, by remote-help chat after the Forsaken fix:

- msg 163 (2026-11-08): Forsaken now renders correctly. In the Half-Life
  demo, "the floor grating and Elevator Doors in the hazard course demo are
  completely clear and missing".
- msg 165 (2026-11-08): the Incoming demo (Direct3D 5) "has transparent or
  missing textures", and runs slowly at 640x480.

Neither message has been answered.

## What was measured

V9XTRACE snapshots with the state census on (`-omit:2147483648`), before
and after each game. The census and counters are since boot; the deltas
below subtract the earlier snapshot. The mask was set back to 0 afterwards.

**Half-Life** (`C:\Games\Half-LifeUplink\HLDEMO.EXE -console +map hldemo1`,
the Uplink demo, `EngineType=3` = Direct3D, 640x480x16 fullscreen), about
two minutes on the map's start (`hl-A.INI` to `hl-B.INI`):

- `BatchesEngineRefused` and `M64Policy08` both rose by 5,390. No other
  reason moved. Reason 8 is `V9X_D3D_G4_REFUSE_ALPHA`.
- The census has two keys at exactly 5,390 draws each:

  | Key | State (d3d_state.h layout) | Fate |
  |---|---|---|
  | `28761C94` | texture op 4, alpha test, depth test, **depth write**, no blend | refused |
  | `24561CD2` | texture op 2, alpha test, depth test, no write, blended DESTCOLOR/SRCCOLOR | drawn |

  That is HL's two-pass drawing of a `{` (alpha-tested) texture: a base
  pass with the alpha test and depth writes, then the lightmap pass
  multiplied onto the frame buffer. The base pass is dropped. The lightmap
  pass alone multiplies the scene behind by the lightmap, close to white,
  which fits "completely clear". That last step is inferred, not seen.
- No frame was grabbed: HL issued two flips in the run
  (`FlipRingIssued=2`), so `-grab:37x4` never fired. The BMPs on the guest
  were Forsaken's from 2026-11-08 and were discarded.

**Incoming** (`C:\Games\INCOMING\INCOMING.EXE`, "IncomingLuxEtRobur
V1.01"), main menu over its animated landscape (`inc-A.INI` to
`inc-B.INI`, ~70 s):

- `M64Policy08` rose by 11,462 (16,852 cumulative minus HL's 5,390); no
  other reason moved.
- The same two keys again, 11,462 each: `28761C94` refused, `24561CD2`
  drawn. Other alpha-tested keys draw because they write no depth or have
  no depth test (`24461152` 19,086, `246612C0` 5,731, `24661114` 198,
  `24661112` 50).
- Four flip grabs of the menu (`inc-grab0..3.png`). The menu, logo and
  terrain draw. Which objects are missing cannot be told from the menu
  alone.
- **Start Game faulted the game**: "Incoming Error Report. exception
  code(c0000005) ADDRESS: (465973)" over a black 640x480 window
  (`inc-start-game-fault.png`); refusals did not move after Enter. The
  address is in INCOMING.EXE's image. Whether the driver causes it is not
  known. The owner reached gameplay (msg 165 reports its frame rate), so
  this may be a path difference (Enter on the menu, a first start) rather
  than the same session's experience. Not repeated.

## Cause

`v9x_d3d_g4_map` (`src/display32/d3d/d3d_g4.c`, the alpha-test/depth-write
block near line 834): an alpha test with depth writes is the combination
that hung the GM965 ([`../decisions/2026-10-21-gm965-direct3d.md`](../decisions/2026-10-21-gm965-direct3d.md)).
The Forsaken fix moved keyed draws onto a depth pre-pass and an EQUAL colour
pass, but an application's own alpha test with depth writes still returns
`V9X_D3D_G4_REFUSE_ALPHA`. Gen4 has no Direct3D fallback
(`v9x_r3d_fallback`), so the draw is dropped. Every Gen4/Gen5 part on
`d3d_g4.c` is affected (965, GM965, G4x, GM45, Ironlake), and any title
that alpha-tests depth-writing geometry: HL's `{` textures (grates, fences,
ladders, some doors), Incoming's census key above.

## Options, not chosen

1. Extend the keyed two-pass path to an application's alpha test. Colour
   is exact. The pre-pass writes depth over the alpha-killed texels too, so
   anything drawn behind a grate *later* in the frame is hidden there;
   what is already drawn shows through. HL draws the world before its
   `{` brush entities, so most of what lies behind a grate is already
   there.
2. Draw it without the alpha test: the grate appears solid, holes filled
   with the texel colour.
3. Lift the hang: depth after the pixel shader on Gen4 (mode N), parked
   2026-10-21 after six variants.

Whether the alpha-test-with-depth-write hang happens on the GM45 (G4x, not
the GM965's 965 core) has never been tested. If it does not, the refusal
could be limited to the 965 cores. That would need a guarded probe on the
GM45 and a reset path ready.

## Not done

- No fix, no build, nothing installed. The machine keeps `0.45.0
  03782be9`.
- No HL frame grabbed, and the hazard course itself not run (this demo is
  Uplink; the owner's hazard course demo was not found on the machine).
- Incoming's fault after Start Game not investigated.
- The owner has not been answered.

## Fix in source, not yet measured (2026-11-11, option 1)

`v9x_d3d_g4_map` no longer refuses an application's alpha test with depth
writes. It sets `depth_prepass` for any alpha-tested draw that writes
depth, keyed or not, and `v9x_d3d_g4_draw_keyed_depth` is renamed
`v9x_d3d_g4_draw_alpha_depth`. The passes are unchanged: depth only
(untextured, no alpha test, colour writes masked), then colour with the
alpha test, depth writes off and the depth test EQUAL. Reason 8 is now
returned only for an alpha function outside NEVER..ALWAYS. The cost is the
same as the keyed case: depth is written over discarded texels, so
anything drawn later behind a grate is hidden there.

The OpenGL ICD's Gen4 draws go through the same map, so GL programs that
alpha-test with depth writes now draw through the two passes as well.
Before this change they were dropped. Not measured.

`run-checks.ps1` passed. Build `254b7655-dirty` (this change only) was
installed on M-6B21TJ by `run-release-checks.ps1 -Machine gm45`: 8 of 8
files hash-checked, boot 17 to 18
(`../probe/m-6b21tj-game-reports-2026-11-11/release-check-gm45/gm45.log`).
The machine then stopped checking in with the relay at 09:39 UTC, about
two minutes after the install check, before the probe push. Neither the
probes nor any 3D program had run, so the new path was not reached. Why
it went offline is not known: the wifi, the owner, or the driver at idle
are all open. **That build stays installed** until the machine returns.

Owed when it returns: confirm it booted to the desktop on `254b7655-dirty`
(`DriverBuild` in a snapshot); V9XD32, V9XGLP, V9XGLIDP and V9XR3DP as a
regression check; Half-Life and Incoming traced as above, expecting
`M64Policy08` to stay 0 and census key `28761C94` to draw; a hang check
on the GM965 (TOMMOHP) before this ships.

## Measured on the GM45 (2026-11-11, boot 19)

M-6B21TJ checked in again at 21:26 UTC on boot 19 (the owner restarted
it; boot 18 was the install's reset). Desktop at 1024x768x16,
`V9XBOOT.INI` `Stage=enable-ok`, snapshot `DriverBuild` and `HalBuild`
both `0.47.0 254b7655-dirty`. Captures:
[`../probe/m-6b21tj-game-reports-2026-11-11/boot19/`](../probe/m-6b21tj-game-reports-2026-11-11/boot19/).

**Probes** (`run-release-checks.ps1 -Machine gm45 -Baseline
-SkipProbeBuild`, nothing installed, every report `Build=254b7655-dirty`):
V9XD32 default PASS and `/ddi5` PASS (`Failures=0` both), V9XGLP PASS
(`LifetimeFailures=0`), V9XGLIDP PASS failures=0, V9XR3DP COMPLETE.
Same verdicts as boot 12 on 0.44.0.

**Half-Life**, same command and map as above, census on at `hl-A.INI`
(empty census: first enable since boot) and about two minutes on the map
to `hl-B.INI`:

- `M64Policy08` stayed 0; `BatchesEngineRefused` stayed at 4 (the four
  reason-4 refusals from before HL started, not this path).
- Census key `28761C94`, the base pass refused 5,390 times on boot 17, drew
  6,576 times. Its lightmap partner `24561CD2` is not in the census this
  time; the blended passes counted are `24521CD2` (143,246) and others, so
  HL's state mix differs from boot 17's in more than this key. Not
  explained.
- The machine did not hang or fault. A screen capture during play
  (`hl-screen.png`) shows a lit corridor with an overhead slatted grille
  drawn solid; whether that brush is one of the `{` textures is not known,
  and the owner's hazard course is still not on the machine.

**Incoming**, started from the desktop after HL quit, ~70 s at the main
menu (`inc-B.INI`, cumulative from `hl-A.INI`):

- `M64Policy08` 0; `28761C94` rose by 5,824 (6,576 to 12,400), all drawn.
- The menu, logo and landscape draw (`inc-menu.png`), as on boot 17.
- **Start Game did not fault this time.** It went to the demo's "Rage"
  intro flyby over the terrain (`inc-start.png`, `inc-C.INI`, no new
  refusals). Gameplay itself was not reached; Incoming was ended with
  `kill`. Boot 17's c0000005 at 465973 did not repeat in one try, so it
  stays unexplained.

The census mask was set back to 0 afterwards. Left installed:
`254b7655-dirty` (branch `gen4-alpha-depth`).

Still owed: a look at the grates and doors themselves (the hazard course,
or the owner's eyes); Incoming gameplay; the GM965 hang check on TOMMOHP
before this ships, since the two passes have only run on G4x.

## Owner's confirmation, and Incoming's crash reproduced (2026-11-12)

The owner, by chat, 2026-11-11 23:35 UTC (msg 173), on `254b7655-dirty`:
Half-Life's textures "now work across all renderers". Msgs 174/175:
Incoming is the only title still slow and with render bugs; its video
options change neither; the Start Game crash happened to them once, and
the second launch worked.

The 0.47.0 release check (another session) then installed released
`0.47.0 0246432c` by hard reset at 03:44 UTC (guest `EVENTS.LOG`, local
time UTC-5). **That release does not contain this fix**
(`b614ac2d` is not an ancestor of `0246432c`), so the machine is back to
dropping these draws.

Boot 23, `0.47.0 0246432c`, census on (`boot23/g-A.INI` to `g-B.INI`):
Incoming's menu; Enter on Start Game did nothing; a click on it ran the
"Rage" logo flyby (`g3.png`), then a screen showing the pilot name
(`g4.png`); a click on the name gave **the same fault as boot 17**,
"exception code(c0000005) ADDRESS: (465973)" (`g6.png`). Over the run:
`M64Policy08` 0, `BatchesEngineRefused` unchanged at 4, and key
`28761C94` absent, so no reason-8 draw was reached before the fault. The
census keys differ from boot 19's (high byte `2C`, min filter field 6:
mipmapped), not examined. Boot 19 never got past the flyby, so its "no
fault" above was not this path.

What the game does at 465973 (`INCOMING.EXE` from the guest, disassembled
with capstone): after the name-entry handler at `4677E0` (DirectInput
scancodes Home/End/arrows/Enter editing a string at `4BCBE8`), it stores
-64.0 (`C2800000`) to `+8` of three objects through the pointers at
`4BCECC`, `4BCEAC` and `4BCE8C`; `4BCECC` is NULL, so the first store
faults. Those pointers belong to a list headed at `4BCE88` that
`465700` builds. Why the list is empty at that point is not found: a
failed resource or surface creation by the driver would explain it, a
game bug would too. Not tested on another driver.

Not done: Incoming gameplay is still unreached; its render bugs and
frame rate are unexamined.

## Merged into 0.48.0 and measured (2026-11-12, boot 24)

Merged to main as `be9f701d` (run-checks passed). Built as
`be9f701d-dirty` (untracked evidence files from other work in the tree)
and installed on M-6B21TJ by `run-release-checks.ps1 -Machine gm45`:
8 of 8 files hash-checked, boot 23 to 24. Captures:
[`../probe/m-6b21tj-game-reports-2026-11-11/boot24-0.48.0/`](../probe/m-6b21tj-game-reports-2026-11-11/boot24-0.48.0/).

- Probes: V9XD32 default and `/ddi5`, V9XGLP, V9XGLIDP, V9XWPRS and
  V9XFBCMP PASS, V9XR3DP COMPLETE, every report `Build=be9f701d-dirty`.
- Half-Life had been left on OpenGL by the owner (`HKCU\Software\Valve\
  HLDemo\Settings` `EngineType=2`; `-d3d` on the command line does not
  override it). The first run (`v48-hl-*`) was OpenGL: no census, no
  refusals, no fault. For Direct3D, `EngineType` was set to 3 by
  `regedit /s`, and set back to 2 afterwards (read back as 2).
- Direct3D, about two minutes on hldemo1 (`v48-hld-A.INI` to
  `-B.INI`): `M64Policy08` 0, `BatchesEngineRefused` unchanged at 4.
  Key `28761C94` (the base pass) drew 7,776 times and `24561CD2` (its
  lightmap pass) 7,776: the pairing boot 17 showed, now both drawn.
  `v48-hld.png` shows the corridor drawn; no hang.

The GM965 hang check is still owed before 0.48.0 ships.

## The GM965 (2026-11-12, TOMMOHP boot 44)

TOMMOHP (`M-KYHT4Q`, GM965 GMA X3100 `8086:2A02`, the 965 core that
hung on alpha test with depth writes) checked in again after three weeks
offline. Its agent was hot-updated from 0.12.0 to 0.16.0 (`dev-fault-3`).
The same `be9f701d-dirty` package was installed by
`run-release-checks.ps1` (a one-machine list outside the repo): 8 of 8
files hash-checked, boot 43 to 44, the "Enter Network Password" box
cancelled with Escape. Captures:
[`../probe/tommohp-gm965-alpha-depth-2026-11-12/`](../probe/tommohp-gm965-alpha-depth-2026-11-12/).

- Probes: V9XD32 default and `/z24`, V9XGLP, V9XGLIDP, V9XWPRS and
  V9XFBCMP PASS, V9XR3DP COMPLETE.
- The Direct3D census over V9XD32 and V9XGLP (`A.INI` to `B.INI`) has
  no alpha-tested state at all, so V9XD32 does not reach the new path.
- V9XGLP does: its alpha section draws an alpha-tested quad (GREATER
  0.666) with depth test LEQUAL and depth writes on, then a lightmap pass
  over it, as Half-Life OpenGL does. Rerun on its own, its GL log has
  `failed r4=0 r7=0 r8=0` for the whole run (`V9XGL-glp-rerun.LOG`, the
  last 400 lines), where earlier processes in this machine's GL log (older
  builds; which programs they were was not checked) read `r8=1`. With
  `r8=0` and the alpha results right, the draw now goes to the engine through the two
  passes instead of being refused. Results exact (`V9XGLP-rerun.INI`):
  AlphaHole 00FF00, AlphaSolid FF0000, AlphaLitHole 008200,
  AlphaLitSolid 840000. No hang.
- Quake 2 demo, OpenGL windowed on demo1 for a minute: drew
  (`q2.png`), GL log `hw-refused=0`, `r8=0`, no hang. Whether Quake 2
  issues alpha tests with depth writes in that scene was not checked.
  Its `config.cfg` was saved before and put back after.

So the two-pass path has run on the 965 core without hanging, in one
probe draw at 1024x768x16. Direct3D on the GM965 has not drawn an
alpha-tested depth-writing primitive (no D3D program on the machine
does); that is the remaining gap.
