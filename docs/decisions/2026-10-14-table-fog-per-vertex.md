# Direct3D table fog is drawn per vertex: the Rage XL matches the formula in six scenes

Date: 2026-10-14

Status: Measured on A8U4I5 (ATI 3D Rage XL PCI, 5:5:5 desktop), boot
470. Step 1 of
[rage-pro-table-fog-and-paletted-textures.md](../plans/rage-pro-table-fog-and-paletted-textures.md).
Evidence: `docs/probe/a8u4i5-rage-xl-table-fog-2026-10-14/`. One game,
Thief II, at boots 470-471 (below).

## The change

None of the engines this driver programs has a fog table. Each fogs from a
factor carried in the vertex specular alpha. Table fog is therefore
computed in the core, per vertex:

- **`d3d_fog.c`** (pure, host-tested in `test_d3d_fog.c`) computes the
  factor at a depth for LINEAR, EXP and EXP2, as Direct3D defines them,
  and writes it into each vertex's specular alpha.
- **`d3d_core.c`**:
  - records FOGTABLEMODE, START, END and DENSITY (floats in DWORDs);
  - when FOGENABLE is set and the mode is not NONE, sends each batch to
    the engine through a copy that carries the table factors;
  - skips the execute-buffer path's CPU fog fold under table fog, since
    the specular alpha is then not a factor.
- **Caps**: `D3DPRASTERCAPS_FOGTABLE` on every engine that already claims
  `FOGVERTEX`: Mach64 class, Rage II, ViRGE, SiS 6326, Gen3. One core
  function serves them all.

## Which depth

Without `D3DPRASTERCAPS_WFOG`, Direct3D defines pixel-fog start and end in
device space, 0 to 1 (SDK, "Eye-Relative vs. Z-Based Depth"). WFOG is not
claimed, so the depth is Z. One exception: a LINEAR range ending past 1.0
cannot be device Z. It is a title passing eye units whatever the caps say,
so its depth is W, 1/rhw. EXP and EXP2 stay on Z: their density gives no
such tell, and a wrong Z reading under-fogs instead of fogging the screen
solid.

## The measurement

V9XDDP's new scenes each draw a red triangle at one depth, fog colour blue,
with the vertex fog factor at 255. Any fog that appears therefore comes
from the table:

| Scene | Mode, range | Depth | Expected red | Measured red / blue |
|---|---|---|---|---|
| 0 | LINEAR 0..1 | z 0.5 | 128 | 132 / 123 |
| 1 | LINEAR 0..1 | z 0 | 255 | 255 / 0 |
| 2 | LINEAR 0..1 | z 1 | 0 | 0 / 255 |
| 3 | EXP, density 2 | z 0.5 | 94 | 90 / 165 |
| 4 | EXP2, density 2 | z 1 | 5 | 0 / 255 |
| 5 | LINEAR 100..500 | W 400 | 64 | 66 / 189 |

Every check matches within one 5-bit step, which is the scene's tolerance.

The other V9XDDP checks were compared against boot 469 (`555-auto`, the
same card). The count went from 205/222 to 206/223; the one new pass is
`D3DTableFogOk`, and no other key changed.

## Thief II: the first game, and the blend refusal it exposed

The Thief II demo, mission "The Unwelcome Guest", A8U4I5:

- **Boot 470, the change above.** Thief II turns table fog on once the
  cap is there (`FogTableBatches` 656,704, `FogTableLastMode` 1, EXP). It
  keeps fog enabled for its blended lightmap pass. The policy refused
  every fogged blend: 419,824 draws, reason 15 (`FOG_WITH_BLEND`). The
  room drew unlit and flat (`boot470-thief2-fog-refused-blend.png`).
  Before the cap was claimed, Thief II most likely never enabled fog, so
  this was a regression from claiming FOGTABLE.
- **The fix.** A draw that asks for both fog and blending is now drawn
  blended without the fog, on both the Mach64 class (`mach64_policy.c`)
  and the Rage II (`rage2_draw.c`). Previously it was refused. The blend
  unit is the fog unit, so they cannot be combined, and neither engine
  has a CPU fallback: a refusal drew nothing. Host tests were written
  first and failed before the change. `M64FogDropped` counts these
  draws.
- **Boot 471.** The lightmaps draw (`boot471-thief2-fog-dropped.png`).
  The counts were 319,244 draws, 187,977 of them fog-dropped, and 128,194
  fogged. The remaining 45,018 refusals are reason 18, colour key, which
  this change does not touch.

Whether the fog itself looks right has not been checked. The agent's
screenshot is too dark to show distant fog, and EXP on Z under-fogs if
Thief II gives its density in eye units.

## Limits

- **Blended surfaces get no fog.** Since boot 471 the Mach64 class and
  the Rage II draw a fogged blend without its fog, for vertex fog as
  well as table fog.
- **The factor is linear between vertices**, not evaluated per pixel. A
  large triangle across an EXP curve fogs differently from a true table.
- **Not measured on the other engines.** Gen3, the Rage II, SiS 6326 and
  ViRGE claim the cap through the same core code.
- **The depth rule is untested on a game.** The plan asks for two
  table-fog titles before the rule is settled.
