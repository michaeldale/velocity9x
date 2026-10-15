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
- **Caps**: `D3DPRASTERCAPS_FOGTABLE` on every engine that fogs from the
  specular alpha: Mach64 class, Rage II, SiS 6326, Gen3. One core
  function serves them all. Not on the ViRGE (below).

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

## Thief II gives its EXP density in eye units (2026-10-15)

At boot 479 the counters showed Thief II's table fog:

| State | Value |
|---|---|
| Mode | EXP |
| Density | `3C2EC33E` (0.0107) |
| Start / end | 0 / 1 |

Over device Z, 0..1, that density fogs about 1%. So the boot-470 rule
drew nothing visible: through the demo's first archway the
sky was black (`boot479-thief2-z-fog*.png`).

**NVIDIA's "Implementing Fog in Direct3D"** (Rogers):
- For transformed vertices under a perspective projection, Direct3D fogs
  on W = 1/RHW.
- Fog start, end and density are then given in world units.
- Z is only for an affine projection.
- The RIVA 128 and TNT emulated table fog per vertex, as this driver does.

The rule now takes W for an EXP or EXP2 density below 1.0 as well. Below
that, a density cannot fog device Z visibly: EXP reaches at most 63% at
the far plane. Host test first; V9XDDP's EXP scenes, density 2, stay on Z.

**Compared with the reference.** The VOGONS "Table Fog & 8-bit Paletted
Textures" thread (page 24) has the demo's starting view with and without
table fog:

| | Sky through the arch | Building beyond |
|---|---|---|
| Reference, with table fog | grey haze | hazed, lit windows |
| Reference, without | black | black |
| Boot 480 (`boot480-thief2-w-fog*.png`) | grey haze, matching | **black**, lit windows |

The building differs because Thief draws it in two passes: a textured
base pass, then a lightmap pass that multiplies into it. This engine
cannot fog a blend, so the lightmap pass is drawn unfogged (see above).
The fogged grey base is then multiplied by a dark lightmap down to near
black. A card that fogs both passes keeps it grey. Open: an approximation
for multiply blends under fog.

## Fogged multiplies, fogged in the texture stage (2026-10-15)

Direct3D fogs the source before the blend: S' = S f + F (1 - f). For a
multiply (DESTCOLOR/ZERO, or ZERO/SRCCOLOR) the result is D S'. The
texture stage can make S' itself: Phase 4 item 10 measured ALPHA_DECAL on
a texture without alpha as C = Cf (1 - Af) + Ct Af. So for such a draw
`mach64_policy.c` (`fog_as_decal`) does three things:
- sets ALPHA_DECAL;
- sends each vertex with the fog colour as RGB and its own fog factor as
  alpha;
- turns fog off, leaving the blend unit a multiply.

It applies only where that is exact:
- the texel alone is the source: DECAL or COPY, or MODULATE by white
  vertices;
- the texture has no alpha;
- there is no alpha test, no specular and no composite.

Every other fogged blend keeps its fog dropped. Host tests first, for the
policy and the two map helpers.

**V9XDDP's new `D3DFogMul` scene:** a white target, a red RGB565 texel,
COPY, DESTCOLOR/ZERO, blue fog.

| Factor | Boot 480, dropped | Boot 481 |
|---|---|---|
| 255 | `7C00` | `7C00` |
| 128 | `7C00` | `400F` |
| 0 | `7C00` | `001F` |

V9XDDP read 207/225 then 208/225; `D3DFogMulOk` was the only key that
changed.

**Thief II, the demo's first archway (boot 481,
`boot481-thief2-fog-multiply*.png`).** The building beyond now draws as
hazed grey with lit windows, as in the VOGONS reference, where at boot
480 it was black. In the mission, 54,859 multiply draws took the texture
stage. The fog stayed dropped on 331,640 other blended draws: other
blends, or multiplies by a coloured vertex. Which they are has not been
counted.

## The other engines (2026-10-15)

Evidence: `docs/probe/table-fog-other-engines-2026-10-15/`.

- **GMA 950** (netbook, boots 181-182). All six scenes give the Rage XL's
  values: red 132, 255, 0, 90, 8 and 66 against 128, 255, 0, 94, 5 and 64.
  V9XDDP went 202/224 to 203/224, and `D3DTableFogOk` is the only key that
  changed.
- **ViRGE/DX** (86Box Win86SE, boots 667-668). Every scene drew unfogged.
  Its vertex fog does not draw either on this path: `D3DDepthFogOk` and
  `D3DFogTexOk` read 0 before and after. Its fog is a colour fold done on
  the execute-buffer path only
  ([issue](../issues/2026-10-15-virge-claims-vertex-fog-it-does-not-draw.md)).
  FOGTABLE is no longer claimed there; V9XDDP read 194/224 both times.
- **Not run:** the SiS 6326 and the Rage II. They claim FOGTABLE because
  they fog from the specular alpha, as the Rage XL and Gen3 measured do.

## Limits

- **Blended surfaces get no fog.** Since boot 471 the Mach64 class and
  the Rage II draw a fogged blend without its fog, for vertex fog as
  well as table fog.
- **The factor is linear between vertices**, not evaluated per pixel. A
  large triangle across an EXP curve fogs differently from a true table.
- **Rage II and SiS 6326 not measured.** They claim the cap through the
  same core code. Gen3 is measured.
- **The depth rule is measured on one game,** Thief II (above). The plan
  asks for two table-fog titles before the rule is settled.
- **Fogged blends other than an exact multiply** draw unfogged: additive
  and alpha blends, and multiplies by a coloured vertex. The Rage II
  drops the fog on every blend; the texture-stage fog is Mach64 class
  only.
