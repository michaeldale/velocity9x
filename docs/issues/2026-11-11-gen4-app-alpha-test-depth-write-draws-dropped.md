# Gen4: an application's alpha test with depth writes is refused and dropped, so Half-Life's grates and doors and parts of Incoming vanish

Date: 2026-11-11. Status: **open**. Logs taken, nothing changed in source
or on the machine.

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
