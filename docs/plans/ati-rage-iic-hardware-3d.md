# ATI Rage IIC: hardware 2D and Direct3D

Target: ATI 3D Rage IIC AGP, `PCI 1002:4757` rev `7A`, 4 MiB SDRAM, on
A8U4I5 (10.0.1.172). Started 2026-10-02.

## Progress - 2026-10-02

- Read-only register survey and desk research:
  [2026-10-02 register survey](../decisions/2026-10-02-rage-iic-register-survey.md).
  The part is a 264GT2C (Rage II class). It has the Mobility's register
  window (BAR2, 4 KiB), its 2D engine and the `GUI_STAT` FIFO model, but no
  triangle setup engine.
- Tier-0 bind (`397da71`) and first boot:
  [2026-10-02 first bind](../decisions/2026-10-02-rage-iic-first-velocity9x-bind.md).
  GDI, all 19 published modes, and DirectDraw mode/palette/flip all pass.
- DirectDraw HAL refusal on no-D3D chips found and fixed (`fc1ed35`).
  The HAL now attaches on this card with a 2.5 MiB heap.
- Phase 1 first attempt (`dc67d29`) hung A8U4I5 on its first engine
  copy (boot 132; [issue](../issues/2026-10-02-rage-iic-first-engine-copy-hangs.md)).
  The engine was at its power-on contents, and nothing initialises it
  before Velocity9x.
- Phase 1 second attempt (`06f760b`):
  - atyfb's known engine state at validate;
  - the 16-entry `FIFO_STAT` model;
  - an idle wait that also waits for `GUI_ACTIVE`.

  At boot 135, DirectDraw fill and copy run on the engine: 66 of 66 blits
  across 11 probe runs, all pixel-correct, no timeouts or resets. GDI,
  mode switches and the DirectDraw mode stress pass.
- **Phase 1 gate met.** Boot 135: 171 probe runs, 1,026 blits, all on the
  engine. Boot 136: 20 runs, 120 blits. All pixel-correct; FIFO
  timeouts, idle timeouts and resets 0 on both boots.
- **Phase 2 gate met** (`1e4789b`, `c35e825`).
  - Harness: a mapping-only VxD and a ring-3 runner built on the
    host-tested builders.
  - Twenty flat trapezoids gave the rules: the length counts rows; a span
    is `[leading, trailing)`; each edge walks X while its error is ≥ 0.
    Exact formula and evidence in
    [the edge model](../decisions/2026-10-02-rage-iic-trapezoid-edge-model.md).
  - Identical on boots 135 and 136; guards intact throughout.
- **Phase 3 done**, boot 136
  ([record](../decisions/2026-10-02-rage-iic-triangles-and-gouraud.md)).
  Host tests against an independent rasteriser and plane:
  - **Flat triangles:** 76 of 76 pixel-exact on the card.
  - **Interpolator formats:** measured by read-back.
  - **Gouraud:** 60 of 60 bit-exact against the engine model. That needed
    one rule found on the card: X_INC follows `DST_X_DIR`.
  - **Z16:** all eight compares, Z writes, a Z gradient, and a Z clear
    by engine fill.
- **Phase 4 steps 1-3 done**, boot 136
  ([record](../decisions/2026-10-02-rage-iic-texture-addressing.md)):
  - S/T normalised to the larger dimension, floor sampling, wrap at pitch
    and height (no clamp);
  - the engine's S/T walk, at START's 32-unit resolution;
  - 120 textured triangles, 20,520 pixels, every one exact against it;
  - the quadratic perspective fit measured against exact on the host,
    with an error estimate for Phase 5 to subdivide on.
- **Phase 4 steps 4-9 measured**, boot 136
  ([record](../decisions/2026-10-02-rage-iic-filtering-formats-blending-fog.md)):
  - bilinear: texel centres and 3-bit weights; the mag/min decision per
    pixel at one texel; linear-min without linear-mag draws nothing;
  - 1555/4444 zero-extended, with the 1-bit alpha mask;
  - modulate and alpha decal; the six blend factors each way;
  - fog through the blend unit with an ARGB8888 colour; the scissor.

  The arithmetic is modelled to within one LSB: 493 of 512 bilinear
  pixels and 376 of 384 blend/modulate/fog pixels are exact.
- Next: a second boot for the Phase 4 scenes, then Phase 5.

## Goal

Hardware Direct3D on the Rage IIC through Velocity9x's shared render
interface, and the 2D engine for DirectDraw fill and copy, without
promising anything the chip or 4 MiB cannot do.

The chip has no setup engine. Every triangle is set up on the CPU: split
into trapezoids at the middle vertex, and each trapezoid is given leading-
and trailing-edge Bresenham terms plus a start value and X/Y increments
per interpolated attribute. The engine walks the spans. That is how ATI's
own Direct3D HAL drives it: the survey read its last values in
`RED_START`, `Z_START`, the S/T increment registers and `TRAIL_BRES_*`.

## Decisions fixed by this plan

| Question | Decision |
|---|---|
| Engine identity | A new engine type, `ATI_RAGE2`. Never `ATI_MACH64`: `d3d_select.c` routes that type to `d3d_mach64.c`, whose every triangle needs the setup engine. |
| 2D | Reuse the Mach64 2D core (`mach64_engine.c` builders, `eng_mach64.c` emission). Anything GTPro-only, such as `ALPHA_TST_CNTL` in `v9x_m64_build_2d_mode`, is omitted for this chip, not written and hoped harmless. |
| 3D setup | CPU, in host-testable C (`src/chipsets/ati/rage2_setup.c`): triangle to trapezoids to register values. No register access in it. |
| Submission | Direct MMIO with FIFO reservation, as on the Mobility. No GUI-master DMA initially. |
| Private scenes | The register stream is built in C and executed by a bounded executor (a small VxD, or the HAL's own mapped window). Not one assembly routine per scene. |
| Display | VBE mode setting stays. |
| Formats | 16-bpp targets, Z16, RGB565/ARGB1555/ARGB4444 textures. |
| Unsupported state | Refused before emission and counted, as on every other engine. |

## Hardware capability boundary (provisional)

From the sources the survey cites, to be confirmed one scene at a time:

- flat and Gouraud shading;
- 16-bit Z, eight compare functions;
- textures power-of-two up to 1024x1024; point and bilinear filtering;
- source blend factors ZERO, ONE, DSTCLR, INVDSTCLR, SRCALPHA,
  INVSRCALPHA; destination factors ZERO, ONE, SRCCLR, INVSRCCLR, SRCALPHA,
  INVSRCALPHA. No destination alpha;
- fog from the alpha interpolator, exclusive with alpha blending;
- texture modes: replace, modulate, alpha decal;
- perspective by quadratic S/T interpolation (second derivatives), not
  per-pixel 1/W. Its accuracy is a measurement, not a given.

Not attempted initially: mipmapping, CI4/CI8 textures, specular,
GUI-master DMA, AGP textures.

The 4 MiB budget is the Mobility's: 640x480 front + back + Z16 is
1.76 MiB, which leaves about 2.2 MiB for textures. A full-screen
1024x768 back and Z buffer do not fit.

## Safety contract

The Mobility plan's contract applies unchanged
([ati-rage-mobility-hardware-3d.md](ati-rage-mobility-hardware-3d.md),
Safety contract). In brief:
- one bounded scene or feature per build;
- identity checks first: chip `4757`, revision `7A`;
- guard patterns around every off-screen target;
- bounded FIFO and idle waits, and at most one reset-and-replay;
- every intended write recorded before it is executed.

This machine also has rules of its own:
- it is remote, so warm restarts only and never a shutdown;
- no DOS programs through the agent;
- driver deploys use plain WININIT renames with no `NUL=` lines.

## Phase 1 - the 2D engine on DirectDraw

1. Add `V9X_DD_ENGINE_TYPE_ATI_RAGE2` (5), with the family manifest
   vocabulary, the family matrix and the selector arms. Direct3D
   selection maps it to NONE.
2. Widen the mini-VDD map check (`loader.asm`) and the HAL validate
   (`eng_mach64.c`) from `4C4D` to {`4C4D`, `4757`}. The register window
   is the same, as measured.
3. A `rage_iic_hw16.c` engine hook like the Mobility's, stamping
   `ATI_RAGE2` with `SOLID_FILL | SCREEN_COPY`.
4. `eng_mach64.c` serves `ATI_RAGE2` for fill and copy, with a Rage II 2D
   mode reset that writes only registers this chip has.

**Done:** `V9XDDP` fills, source copy and the four overlapping copies
correct on the engine (the HAL's blit counters show the engine took
them), `V9XGDI` and the `V9XMSW` cycles pass, and no engine timeouts.
Then 1,000 alternating fills and copies with no timeout, across two
boots.

**Measure in this phase:** `GUI_STAT[25:16]` under load. Is it the free
count `CMDFIFO_SIZE_MODE 0` implies, so 192 at idle? If not, fall back to
the `FIFO_STAT` population count.

## Phase 2 - the first trapezoid

A private scene at 640x480x16 into a small guarded off-screen RGB565
target. One flat-coloured trapezoid with horizontal top and bottom, then
one with both edges sloped, from the register sequence in the spec. No
Z, texture or blend.

**Done:** interior, exterior and edge pixels match a CPU reference on two
boots, guards intact, and the edge rule (which pixels a span includes)
written down.

**Kill:** the trapezoid needs undocumented register changes with no
bounded hypothesis, or writes land outside the target.

## Phase 3 - triangles

`rage2_setup.c`, host-tested against a CPU rasteriser with the same fill
rule:
- split at the middle vertex;
- choose the long edge;
- compute both edges' Bresenham terms;
- emit one or two trapezoids.

Then, one physical scene per step:
1. flat triangle;
2. Gouraud (R, G, B start and increments);
3. Z16 test without write, all eight compares;
4. Z16 write and clear.

## Phase 4 - textures and the rest of the boundary

One feature per build, each with a CPU reference:
1. RGB565 texture, point sampling, affine S/T;
2. S/T second derivatives, compared with exact perspective;
3. wrap/clamp;
4. bilinear;
5. ARGB1555/4444;
6. texture modes;
7. each advertised blend pair;
8. fog without blending;
9. scissor.

## Phase 5 - Direct3D integration

`src/display32/d3d/d3d_rage2.c` behind `V9X_D3D_ENGINE_OPS`, selected on
`ATI_RAGE2`. Truthful caps from Phases 3-4, refusal counters, and the
existing core above it (clipping, culling, records). Gate: the D3D probe
scenes, then Final Reality and 3DMark 99 against the native baseline
([2026-10-02 baseline](../decisions/2026-10-02-rage-iic-native-baseline-a8u4i5.md)).
Scores are recorded, not chased.

## Phase 6 - OpenGL

`V9XGL.DLL` routes supported draws through the same ops. ATI shipped no
working ICD for this card (baseline), so this would be its first hardware
OpenGL.

## Open questions only hardware answers

- `GUI_STAT[25:16]` semantics under load.
- Whether `RED_START` and the other GTB-tagged registers at `+7C0`-`+7F8`
  take effect. The survey shows ATI's driver writes them.
- The S/T second-derivative format and how accurate the perspective is.
- What reads of the bus-master block do (skipped so far).
- The screen overwrite seen once under ATI's driver during `V9XSTAGE`.

## Source basis

The register survey's sources: RRG-G02700, PRG-215R3, SDK-C02700
(ATI3DCIF), atyfb, xf86-video-mach64, and the DRI mach64 notes. A cited
register-level spec of the trapezoid engine is to follow in
`docs/specifications/`.
