# GT218: pixel shader 1.1 and DDI 8, from the HD 4600 work

Date: 2026-11-15. Status: steps 0-3 done 2026-11-15
([XRGB8888](../decisions/2026-11-15-gt218-xrgb8888-textures.md),
[encoder, translator, probe](../decisions/2026-11-15-gt218-ps11-translator.md));
step 5 done ([guard band, two stages](../decisions/2026-11-15-gt218-guard-band-two-stages.md)); step 4 next. Decisions settled
2026-11-15 (end of this file): no T&L; two texture stages (step 5) are
done before step 4 publishes DDI 8. Order of work: 0, 1, 2, 3, 5, 4, 6,
7, 8.

Asked for: bring the HD 4600's Direct3D work (hardware T&L, DDI 7/8, pixel
shader 1.1, DXT, cube maps, bump formats) to the GeForce 210. Test machine:
W9QI0DAD (FX-6300, ASUS GeForce 210, nvidia family from main 5be22308).

## What carries over, and what does not

Most of the HD 4600 work is in the shared Direct3D core and needs only a
back end. The back-end interface is `V9X_D3D_ENGINE_OPS` and
`V9X_D3D_ENGINE_LIMITS` (`d3d_internal.h:370-758`).

| HD 4600 piece | Shared (carries over) | The GT218 must write |
|---|---|---|
| DDI 8 | caps 8, format list, `GetDriverInfo2` (`d3d_core.c:8420-8548`, `d3d_caps8.c`); DDI 8 is offered over DDI 6 to any back end with `draw_shader` (`d3d_core.c:4618`) | the `V9X_DD_ENGINE_CAP_D3D_DDI8` bit in `gt218_hw16.c` |
| PS 1.1 | parser and IR `V9X_PS11_PROGRAM` (`d3d_ps11.c`, `ps11.h`), reference interpreter `v9x_ps11_run`, shader table, constants and wide vertex (`d3d_ps.c`), batching, the wide clipper `v9x_r3d_clip_wide` | a Tesla lowering of the IR, and `draw_shader` |
| Fixed-function DOT3 / BUMPENVMAP | `v9x_d3d_ps_fixed` | nothing beyond `draw_shader`, but it needs two stages (below) |
| Cube maps, Q8W8V8U8 / V8U8 | claimed with `draw_shader` | cube placement in `create_surface`, cube and signed TIC formats |
| DXT | published by `limits.dxt_textures` | TIC formats, placement, Lock/Unlock if not linear |
| Two texture stages | stage pairing, caps | `texture_stages = 2`, two-unit fragment programs, a measured guard band so `clip_in_core` can be 0 |
| Hardware T&L / DDI 7 | the CPU-side key and constants (`d3d_tnl.c`, `r3d_tnl.c`) | a whole Tesla vertex program doing transform, lighting and fog |

**T&L is not planned.** The HD 4600's T&L device is built, host-tested and
**not offered** to Direct3D programs: it measured slower than the
runtime's own transform (3DMark 2000 1558 against 2322,
`d3d_core.c:245-254`). For Direct3D, DDI 7 adds nothing that DDI 8 over
DDI 6 does not give, which is how Gen3 got pixel shaders. T&L would only
pay for the OpenGL ICD, and is a separate plan if wanted.

## The GT218 today

`d3d_nv50.c` implements `texture_format`, `describe_caps`, `ready`,
`create_surface`, `destroy_surface`, `draw`, `accepts` and
`surface_written` (`d3d_nv50.c:1060-1077`). It has one texture stage
(`d3d_nv50.c:37-57`), Z16, the formats RGB565, ARGB1555, ARGB4444 and
ARGB8888, and DDI 5/6 only (the 16-bit side stamps no DDI 8 bit,
`gt218_hw16.c:130-132`).

It draws fixed function with one pass-through vertex program and 12
fragment-program variants built by its own encoder (`nv50_d3d.c:142-260`).
The encoder (`nv50_shader.h`) knows mov, fmul, fmad, fadd, rcp, interp, 2D
tex and exit. The fragment code area is 0x1000 bytes (`nv50_3d.h:102`),
most of it taken by the variants.

Open items that the steps below run into: XRGB8888 textures (V9XD32
T22X), Z Lock conversion, Z24S8, T20/T21 partial, no display-start flips.

## Steps

Each step has a gate. A step that needs the card runs on W9QI0DAD, which
restarts with `reset` only.

### 0. XRGB8888 textures

T19's textures are A8R8G8B8 and X8R8G8B8, and the Gen3 plan added both
32-bit formats first. Take XRGB8888 (a TIC format with alpha forced to one,
or the 8888 format with the swizzle's alpha from ONE).
**Gate:** V9XD32 T22X passes at 32 and 16 bpp; nothing else changes.

### 1. Encoder: the instructions PS 1.1 needs

Add, each with a host test:
- source negation and absolute value (PS 1.1 `-r0`, `_bias`, `_bx2`, SUB);
- destination saturate and the [-1, 1] clamp PS 1.1 registers need;
- min, max, set and select (CND, and CMP if PS 1.4 ever follows);
- kill (texkill, and colour key, which the GT218 refuses today);
- 3D/cube texture fetch (texm3x3tex, cube maps).

Every new encoding is checked against envytools' nv50 ISA tables and
Mesa's nv50 emitter in `build/external/nv50/` (as the existing ones were
against EXA's programs), by a host test holding the words.
**Gate:** build-host; no card.

### 2. Translator: PS 1.1 IR to a Tesla fragment program

`v9x_nv50_ps11_build`, in `src/chipsets/nvidia/gt218/`, consuming
`V9X_PS11_PROGRAM` as the Gen3 and Haswell lowerings do. To hold it to the
reference interpreter (`v9x_ps11_run`), the host test needs a small model
of the Tesla subset the encoder emits: decode our own words and execute
them, the way the Haswell test runs an EU model.
**Gate:** build-host: every program in `test_d3d_ps11.c`'s corpus, run
through the translator and the model, matches the interpreter within the
precision the Gen3 test allows.

### 3. On the card, outside the HAL

The probe stage that did each Tesla first (`V9XN5CH /3d:...`) gains
`/3d:ps11`: upload a translated program, draw one quad with up to four
textures, read back, compare with the interpreter. The analogue of
V9XHSWMM `/ps11` on the HD 4600.
**Gate:** the probe's corpus exact (or within the stated precision) at
32 and 16 bpp, no MP trap.

### 4. `draw_shader` in the HAL, and DDI 8

- Four TIC/TSC entries; the vertex program passes the wide vertex's eight
  texture coordinate sets and two colours (28 floats a vertex in the push);
- a program cache in a larger code region (the 0x1000-byte fragment area
  is nearly full; the 2 MiB reserve has room, the scratch layout moves);
- clipping with `v9x_r3d_clip_wide` in the back end, as Haswell and Gen3
  do (`clip_in_core` does not cover this path);
- Z-tested shader draws go through the shadow target, like every Z draw;
- `V9X_DD_ENGINE_CAP_D3D_DDI8` in `gt218_hw16.c`.

**Gate:** V9XD32 T19 (needs a 32 bpp or R5G6B5 desktop) and T17 pass;
V9XD32 T3-T22 and V9XDDP unchanged from the 5be22308 baseline; 3DMark 2001
SE runs with the pixel shader tests listed.

### 5. Two texture stages

Needed before the caps tell the truth: with `draw_shader`, the core claims
the fixed-function DOT3 and BUMPENVMAP ops, but keeps stages 1 and 2 only
on an engine with two stages (`d3d_core.c:7318`). Two-unit fragment
programs, `texture_stages = 2`, and a measured guard band so
`clip_in_core` can be 0 (the GT218's coordinate limit is unmeasured).
**Gate:** V9XD32 T10, T12, T13; 3DMark 2000 unchanged or better
(recorded, not compared).

### 6. Cube maps and bump formats

Cube placement in `create_surface`, the cube TIC type, and the signed
formats Q8W8V8U8 and V8U8 (and L6V5U5 / X8L8V8U8 only if T19's FfBemL
rows pass, as on Haswell).
**Gate:** T19 with cubes and the FfBem rows.

### 7. DXT

DXT1/3/5 TIC formats. Whether Tesla samples compressed textures from a
pitch-linear surface or needs block-linear placement is unmeasured: probe
first, then placement and Lock/Unlock to match.
**Gate:** T19 Dxt1 rows, 3DMark 2001 SE's texture tests.

### 8. 3DMark 2001 SE Nature

The HD 4600's end point. **Gate:** Nature runs, its Image Quality frame
inspected (the grab needs flips, or a windowed run).

## Costs and risks

- Steps 1-3 are host work plus one probe; 4 is the big HAL step; 5-7 each
  touch formats and placement. The Gen3 plan took about a week of sessions
  for steps of this kind.
- Every new encoding is a chance of an MP trap (`INVALID_OPCODE`, the
  pairing rule measured in 5a). The probe stage comes before the HAL so a
  trap costs a probe run, not a HAL session.
- Program space and the scratch layout are the likeliest surprise.

## Decisions (Michael, 2026-11-15)

1. **T&L is left out.** The HD 4600 does not offer its T&L device either;
   T&L for the OpenGL ICD would be a plan of its own.
2. **Two stages first.** Step 5 lands before step 4 publishes DDI 8, so
   the caps never claim BUMPENVMAP that cannot draw. Step 4's draw_shader
   can be built and probed before step 5; only the DDI 8 bit waits.
