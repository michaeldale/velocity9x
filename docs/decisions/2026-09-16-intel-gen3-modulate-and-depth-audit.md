# Gen3 audit: the MUL instruction, and the depth buffer

**Purpose**: license the next two Phase 6 steps - one texture-stage operation
(modulate) and 16-bit depth testing - before any code is written for either.
Same discipline and the same two trees as the
[texture packet audit](2026-09-16-intel-gen3-texture-packet-audit.md).

**Sources**: Mesa 22.3.0 gallium `i915` (OpenGL) and xf86-video-intel 2.20.19
(X Render, XvMC, Xv). Two projects, different authors, different purposes.

**Rule applied**: a value may enter a builder only if it is *used*,
consistently, by two code paths with different authors and different purposes.
Where that bar is not met it is stated as not met, not quietly relaxed.

---

## 1. MUL: opcode and instruction layout

`A0_MUL` is `0x3 << 24`, stated identically in both trees:

| Source | Site |
|---|---|
| Mesa | `i915_reg.h:524` - `#define A0_MUL (0x3 << 24) /* dst = src0 * src1 */` |
| xf86 | `i915_3d.h:89`, `i915_reg.h:538`, `sna/gen3_render.h:541`, `xvmc/i915_program.h:56` - same value, same comment |

Arithmetic instructions are **three dwords**, and Mesa's emitter states the
layout directly (`i915_fpc_emit.c:155`):

```
A0 = op | A0_DEST(dest) | mask | saturate | A0_SRC0(src0)
A1 = A1_SRC0(src0) | A1_SRC1(src1)
A2 = A2_SRC1(src1) | A2_SRC2(src2)
```

xf86's `i915_inst_arith` (`xvmc/i915_program.h`) packs the same three dwords
with the same macros.

### The trap: src1's swizzle is SPLIT across two dwords

This is the one thing in this section worth writing down in advance, because
getting it wrong produces a program the parser accepts and a picture that is
wrong in a plausible way.

- `src0`'s four channel selectors all live in **A1**, at shifts 28, 24, 20, 16.
- `src1`'s **X and Y** live in **A1**, at shifts 4 and 0.
- `src1`'s **Z and W** live in **A2**, at shifts 28 and 24.

Both trees state this identically (Mesa `i915_reg.h:568-586`, xf86
`i915_reg.h:537-565`). A naive implementation that put all four of src1's
selectors in A1 would emit `src1.xy` with Z and W reading as channel 0, which
for a modulate is a colour multiplied by `(r, g, r, r)` - a wrong picture, no
error.

### The exact dwords for `mul oC, R0, T8`

Derived from the macros, arithmetic shown so it can be checked rather than
trusted. `UREG(type, nr)` packs type at 29, nr at 24 and the identity swizzle
`X,Y,Z,W = 0,1,2,3` at 20, 16, 12, 8; `UREG_MASK` is `0xffffff00`.

| Register | UREG | Masked |
|---|---|---|
| `R0` (REG_TYPE_R=0, nr 0) | `0x00012345` | `0x00012300` |
| `T8` (REG_TYPE_T=1, nr 8 = diffuse) | `0x28012345` | `0x28012300` |
| `oC` (REG_TYPE_OC=4, nr 0) | `0x80012345` | masked by `UREG_TYPE_NR_MASK` to `0x80000000` |

| Field | Macro | Value |
|---|---|---|
| `A0_MUL` | - | `0x03000000` |
| `A0_DEST(oC)` | `>> 10` | `0x00200000` |
| `A0_DEST_CHANNEL_ALL` | `0xf << 10` | `0x00003C00` |
| `A0_SRC0(R0)` | `>> 22` | `0x00000000` |
| `A1_SRC0(R0)` | `<< 8` | `0x01230000` |
| `A1_SRC1(T8)` | `>> 16` | `0x00002801` |
| `A2_SRC1(T8)` | `<< 16` | `0x23000000` |
| `A2_SRC2(none)` | - | `0x00000000` |

**A0 = `0x03203C00`, A1 = `0x01232801`, A2 = `0x23000000`.**

`A1_SRC0(R0)` is `0x01230000`, which is already in the header as
`V9X_I9XX_FS_A1_SWIZZLE_XYZW` - the untextured program emits the same dword
for the same reason.

## 2. The modulate program

Five instructions where the sampling program has three:

```
dcl  T0          (the interpolated coordinate set)
dcl  S0          (the sampler, no channel mask)
dcl  T8          (the interpolated diffuse colour)
texld R0, S0, T0 (the texel into a temporary)
mul  oC, R0, T8  (modulate)
```

Sixteen dwords: the packet header plus five instructions of three.

`texld` must now write `R0` rather than `oC`. `REG_TYPE_R` is 0 and temporaries
need no declaration - Mesa states it in the `REG_TYPE_R` comment, "no need to
dcl, must be written before read". It is written before read here.

`T8` needs its declaration: `REG_TYPE_T` values "must be dcl'ed before use",
and the untextured program already declares exactly this register.

### Why this is the right shape for "one texture-stage operation"

This driver has no fixed-function texture-blend stage state, by choice: Phase 5
emits a constant fragment program instead, and `_3DSTATE_MAP_BLEND_OP` and its
arguments are not in the tree. A texture-stage operation is therefore a
different program, not new state. That keeps the step to one packet's contents
and no new packet at all.

## 3. Depth: the BUF_INFO

Identical in shape to the colour buffer's. Mesa `i915_surface.c:413-430`
builds both with one code path:

```c
if (util_format_is_depth_or_stencil(ps->format))
   surf->buf_info = BUF_3D_ID_DEPTH;
else
   surf->buf_info = BUF_3D_ID_COLOR_BACK;
surf->buf_info |= BUF_3D_PITCH(tex->stride);   /* pitch in bytes */
switch (tex->tiling) {
case I915_TILE_Y: ... BUF_3D_TILED_SURFACE | BUF_3D_TILE_WALK_Y; break;
case I915_TILE_X: ... BUF_3D_TILED_SURFACE; break;
case I915_TILE_NONE: break;
}
```

So: `BUF_3D_ID_DEPTH | BUF_3D_PITCH(bytes)`, address in the second payload
dword, **and linear adds nothing at all**. The pitch encoding is the same macro
the colour buffer uses, so this driver's existing pitch constant and its
multiple-of-four constraint carry over unchanged.

**Linear depth is legal, not merely expressible.** `i915_texture_tiling`
(`i915_resource_texture.c:164`) returns `I915_TILE_NONE` whenever
`!is->debug.tiling`, for every texture including depth. Tiling on this
generation is an optimisation behind a debug switch, not a requirement of the
depth surface. That was the open question the texture audit's linear-only
constraint left for this step, and it is answered.

### Single source, stated as such

`BUF_3D_ID_DEPTH` has **no xf86 use site**, and cannot have one: a compositor
has no depth buffer. The constant is defined in four xf86 headers and used in
none of them. The texture audit already recorded this
(`BUF_3D_ID_DEPTH (0x7<<24) | Mesa i915_vtbl.c:597 | used; xf86 has no depth
buffer at all, so no second site can exist`).

This is a genuine exception to the two-source rule, and it is the reason the
depth step is riskier than the texture step was. What it means concretely: the
encoding rests on one emitter, so the scene must be designed so that a wrong
binding shows up as a wrong *picture* that the guards and probes can name -
not as a silent one.

## 4. Depth: S6 and the format

| Field | Value | Sources |
|---|---|---|
| `S6_DEPTH_TEST_ENABLE` | `1 << 19` | Mesa `i915_reg.h:401`; xf86 `i915_reg.h:426`, `sna/gen3_render.h:429`, `xvmc/i915_structs.h:844` |
| `S6_DEPTH_TEST_FUNC_SHIFT` | `16`, 3-bit field | Mesa `i915_reg.h:402`; xf86 `i915_reg.h:427` |
| `S6_DEPTH_WRITE_ENABLE` | `1 << 3` | Mesa `i915_reg.h:411` |
| `COMPAREFUNC_LESS` | `2` | Mesa `i915_reg.h:895` |
| `DEPTH_FRMT_16_FIXED` | `0` in `_3DSTATE_DST_BUF_VARS` | already emitted by this driver |

The func field is **double-sourced by use**, which matters more than the
header: xf86's `i915_video.c:136` emits

```c
OUT_BATCH((2 << S6_DEPTH_TEST_FUNC_SHIFT) | ... | S6_COLOR_WRITE_ENABLE | ...);
```

- a literal 2, which is `COMPAREFUNC_LESS` in Mesa's table, written into the
same shift by an author who never included Mesa's enum. `xvmc/i915_xvmc.c:345`
independently sets `S6_COLOR_BUFFER_WRITE | S6_DEPTH_TEST_ENABLE`.

`S6_DEPTH_WRITE_ENABLE` is Mesa-header-only among the sites read here. It is
one bit in an already-sourced dword and is only set in the depth-write scene.

## 5. Depth: the clear is a BLIT

Mesa clears a depth buffer two ways, and the second one needs no new packet at
all. `i915_clear_depth_stencil_blitter` (`i915_surface.c:308`) calls
`i915_fill_blit` - an `XY_COLOR_BLT` into the depth surface with
`XY_COLOR_BLT_WRITE_RGB` and the packed depth as the fill pattern.

Mesa's own 16-bit packing (`i915_clear.c:103`) is:

```c
clear_depth = (packed_z_stencil & 0xffff) | (packed_z_stencil << 16);
```

- the 16-bit depth value **doubled into a dword**. That is exactly the shape of
this driver's texture quadrant fills and of the render-target fill, for exactly
the same reason: the blit fills in dwords and two 16-bit values share one.

**Recommendation: clear depth with `XY_COLOR_BLT`.** The alternative is Mesa's
render path, `_3DSTATE_CLEAR_PARAMETERS` plus `_3DPRIMITIVE | PRIM3D_CLEAR_RECT`
(`i915_clear.c:134-200`), which introduces a new state packet *and* a new
primitive type, neither of which can be double-sourced and neither of which
this driver needs. The blit is the one operation this part is measured to
perform correctly - three captures - and it keeps the CPU out of the aperture,
which the errata gate requires.

## 6. What is NOT sourced: the Z scale

**Neither tree states how a post-transform `XYZW` vertex's Z maps to a 16-bit
fixed depth buffer.** Mesa performs the viewport transform before the vertex
reaches the hardware and does not document the resulting scale for this
generation; xf86 has no depth buffer at all. Searching both trees for a depth
range, near/far or Z scale finds nothing on the i915 path.

The natural reading is that Z in `[0, 1]` maps across the format's full range,
but that is an inference and this project does not build on inferences.

**Consequence for the experiment.** The depth scenes must be designed so that
their result depends only on the *ordering* of Z values under any monotonic
mapping, never on absolute depth values:

- Clear the depth buffer to far.
- Draw a triangle at a larger Z, then an overlapping one at a smaller Z, in
  different colours. Under `LESS` the nearer one wins where they overlap, for
  any monotonic mapping.
- Then draw a third at a Z larger than both. It must not appear. Without this,
  "depth test works" is indistinguishable from "the second draw simply
  overwrote the first", which is what an unconditional draw does anyway.

The third draw is the whole experiment. The first two prove nothing on their
own, exactly as two opaque triangles could not reveal double coverage in the
edge scenes.

If the scale later matters - it will, for a real D3D depth range - that is its
own measurement with its own scene, and this audit does not license one.

## 7. What this changes in the decoder

The allowlist is written for the untextured, un-Z'd Phase 5 stream and has
already grown one mode. These steps add to it:

- **Vertex Z may be non-zero.** The decoder currently *requires* Z to decode to
  zero. That becomes mode-dependent, and in a depth scene the check is that Z
  decodes at all and lies in range - not that it is zero.
- **A depth `BUF_INFO` becomes acceptable**, and its address must be bounds-
  checked against a depth range passed in, exactly as the map address is
  against the texture range. The issue closed on 2026-09-16 said this in
  advance: "When Phase 6 adds a real depth buffer, this becomes a check that
  its address is inside the reserve, not a relaxation back to accepting zero."
  Address zero stays refused.
- **S6 is pinned per mode.** Depth test off, test on, and test-plus-write are
  three distinct dwords and the decoder must require the one the scene claims.
  A stream declaring depth writes in a scene that is not the depth-write scene
  would write into the depth buffer unasked.
- **A blit into the depth range is acceptable**, bounded by that range, as the
  texture paint's blits already are.
- **The shader length follows the mode** for a third program.

## 8. The scene table this licenses

Five scenes, against the five draws the 2026-09-16 errata amendment
authorises. No further amendment, and no scene dropped:

| Scene | Draw | What it establishes |
|---|---|---|
| 0 | Phase 5 triangle | Nothing changed. The standing regression. |
| 1 | Modulate, **white** vertices | White x texel = texel, so this must reproduce `intel45`'s four quadrant colours exactly. The texture regression and the program change isolated in one draw. |
| 2 | Modulate, **coloured** vertex | The multiply itself, against products computed host-side from measured 565 values. |
| 3 | Depth test, writes off | Two overlapping triangles and a third behind both. |
| 4 | Depth writes and clear | The same, with writes on, so the third is occluded by what the second *wrote* rather than by the clear. |

## 9. What this audit does not license

- **Alpha test and source-alpha blend.** No fields read here, and a prior
  question underneath them: an RGB565 texture carries no alpha, so an alpha
  source must be chosen first - vertex alpha, or a `MAPSURF` sub-format this
  driver has never emitted and whose paint path does not exist. Their own
  audit, after this build.
- **Tiling.** Linear is confirmed sufficient for depth; nothing here needs a
  tiled surface.
- **`PRIM3D_CLEAR_RECT` and `_3DSTATE_CLEAR_PARAMETERS`.** Read, understood,
  and deliberately not adopted - see section 5.
- **Any claim about the depth Z scale.** See section 6.
- **A general shader compiler.** Unchanged kill criterion. The modulate program
  is a constant, like the two before it.
