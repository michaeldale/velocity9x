# Intel Gen3 3D packet audit: what two trees actually agree on

**Date:** 2026-09-14
**Status:** Step 1 of `docs/plans/intel-gma950-phase5.md` Part 2, **in progress**.
Sections 3-7 are complete and were cross-checked under the rule in section 2.
Section 8 lists what is still open; no builder may be written against an open
item. Revised 2026-09-15: the S0/S1 question and the fragment program, the two
most important open items, are both closed at the end of section 7 - and the
second of them reverses section 5's vertex-format recommendation.
**Machine:** none. Every claim here is **documentation-derived and unconfirmed
on this machine**, and carries that status until the netbook says otherwise.

## 1. Why this exists, and the rule it follows

Phase 5 draws one flat-shaded, untextured, un-Z'd triangle into a 640x480
RGB565 offscreen target. Before Phase 4 there was no Intel write path at all;
before this audit there is no Intel *3D* reference of any kind. A repo-wide
search for `3DSTATE`, `3DPRIMITIVE`, `LOAD_IMMEDIATE`, "pixel shader" or
"fragment shader" across `docs/`, `src/`, `include/` and `scripts/` returns
nothing. The Gen3 hardware audit
(`2026-08-17-intel-gma-gen3-hardware-audit.md` §5) stops at the blitter: four
opcodes and five ring registers, all from Linux i915, which has no Gen3 render
support at all.

Per `docs/ddk-inputs.md`: these sources are consulted to establish **what the
hardware requires**. Velocity9x source is independently written from the
findings in this document, not transcribed from the trees below. Bit values and
opcodes are hardware facts and are recorded here as such; structure, naming and
control flow are ours.

**Gen3 3D is deleted upstream.** Mesa removed the classic `i915` driver in
2023; `xf86-video-intel` removed its UXA Gen3 render path after 2.20.19. A
reader who greps current Mesa or current xf86-video-intel will find nothing and
may conclude it never existed. It did. Both are reachable only at the pinned
refs below.

## 2. Sources, pinned - and why the plan's cross-check rule had to change

| Tree | Ref | Files read | Licence |
|---|---|---|---|
| Mesa, classic i915 | `027ccc89b2ab83fdb9dbc42c9f5a31c175c7f554` (branch `amber`, 2024-04-23) | `src/mesa/drivers/dri/i915/`: `intel_reg.h`, `i915_reg.h`, `i915_state.c`, `i915_fragprog.c`, `i915_program.h`, `intel_tris.c`, `i915_context.h` | MIT |
| xf86-video-intel | `a88a9b9a59fa2d5fd427fa6e1f74fb9844379264` (tag `2.20.19`) | `src/`: `i915_reg.h`, `i915_3d.h`, `i915_3d.c`, `i915_render.c`, `i830_reg.h`, `i830_render.c`, `i830_3d.c` | MIT |

Both from `gitlab.freedesktop.org` (Mesa project 176, xf86-video-intel project
612). 2.20.19 is the **last** xf86-video-intel tag carrying `i915_3d.h` and
`i830_render.c`; 2.21.0 and later do not have them.

### The plan's "independent cross-check partner" does not exist

The plan proposed xf86-video-intel as the **independent** partner to Mesa, with
the rule that "opcodes and lengths must match between the two trees". Measured:

- Mesa's `i915_reg.h` + `intel_reg.h` define 614 macros; xf86's `i915_reg.h`
  defines 597. **540 of them are byte-identical** once whitespace is
  normalised - 90% of the xf86 header.
- Mesa's copy is headed `Copyright 2003 VMware, Inc.`; xf86's is headed
  `Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.` VMware acquired
  Tungsten Graphics in 2008. These are the same file under the successor
  copyright holder.

**The two headers are one forked file, so agreement between them is not
corroboration.** Two copies of a header agreeing proves only that nobody edited
one of them, which is precisely the failure mode the plan's rule was written to
exclude - and the plan named it ("not two copies of a header") without
realising it had chosen two copies of a header.

The rule is therefore tightened for the rest of this audit, and this is the
form every claim below is held to:

> A value may enter a builder only if it is **used**, consistently, by two
> code paths with different authors and different purposes. The shared header
> supplies the *name*; the two use sites supply the *evidence*. A value defined
> in the header and used in only one tree is **single-sourced** and goes in the
> table in section 8.

The use sites are genuinely independent: Mesa's `i915_state.c` /
`i915_fragprog.c` implement OpenGL; xf86's `i915_render.c` / `i915_3d.c`
implement the X Render extension and textured video. Different authors,
different eras, different goals, same silicon.

## 3. The highest-value artefact: a real minimal Gen3 setup

`xf86-video-intel/src/i915_3d.c`, function `I915EmitInvarientState`, is a
complete Gen3 3D invariant-state block from shipping code - and it is close to
what Phase 5 needs, which no amount of header reading would have established.
Its significance is not any single value but that a real driver's *whole*
minimal state block is only ~20 packets, and that it explicitly disables the
things Phase 5 also wants disabled.

The two findings that matter most to the plan's risk list come from it and are
recorded in §5 and §6.

## 4. Established: the target-setup packets

All cross-checked under the §2 rule.

### `MI_FLUSH` - bit 2 is the one that matters, and it must be clear

Opcode `(0x04 << 23)` in the MI ring, both trees. Bit meanings, named in both
(xf86 names all five, Mesa names two, and the two it names agree):

| Bit | Mesa name | xf86 name | Meaning |
|---|---|---|---|
| 0 | `FLUSH_MAP_CACHE` | `MI_INVALIDATE_MAP_CACHE` | invalidate texture map cache |
| 1 | - | `MI_STATE_INSTRUCTION_CACHE_FLUSH` | flush state/instruction cache |
| 2 | `INHIBIT_FLUSH_RENDER_CACHE` | `MI_INHIBIT_RENDER_CACHE_FLUSH` | **inhibit** render cache flush |
| 3 | - | `MI_END_SCENE` | end scene |
| 4 | - | `MI_WRITE_DIRTY_STATE` | write dirty state |

**The answer to the plan's question is that a bare `MI_FLUSH` with all bits
zero is correct for Phase 5, and this is a positive requirement, not a
default.** Bit 2 *inhibits* the render cache flush, so leaving it clear is what
causes the render cache to be flushed to memory - which is exactly what must
happen before the CPU reads the target back through GMADR. Setting bit 2 as a
"safe extra" would silently defeat the entire verification.

This is a sign-inverted bit name, and it is the kind of thing that produces a
correct-looking stream that reads back stale. It is recorded here because
Phase 5's whole result is a read-back.

### `_3DSTATE_BUF_INFO` - two dwords of target description

`(CMD_3D | (0x1d<<24) | (0x8e<<16) | 1)`, three dwords total.

- dword 1: `BUF_3D_ID_COLOR_BACK` `(0x3<<24)`, tiling bits, and
  `BUF_3D_PITCH(x)` = `((x)/4)<<2` - **the pitch is in bytes, encoded as
  dwords shifted back up by 2**, i.e. the field holds the byte pitch with its
  low two bits forced clear. The pitch must be a multiple of 4.
- dword 2: `BUF_3D_ADDR(x)` = `(x) & ~0x3`, the target address.
- **Tiled and fence bits must be clear.** `BUF_3D_USE_FENCE` `(1<<23)` and
  `BUF_3D_TILED_SURFACE` `(1<<22)`. Use site: `i915_render.c:884-892` sets
  `tiling_bits = 0` for an untiled destination and only ORs the tiled bits in
  when the pixmap is actually tiled. Phase 5's target is linear, so both are
  zero.

`i915_render.c:878` also records, as a comment from someone who evidently paid
for the knowledge, that **`BUF_INFO` is an implicit flush**.

### `_3DSTATE_DST_BUF_VARS` - RGB565 and the subpixel bias

`(CMD_3D | (0x1d<<24) | (0x85<<16))`, two dwords.

- `COLR_BUF_RGB565` = `(2<<8)`. Two independent use sites assign exactly this
  for a 16-bit destination: `i915_render.c:155` and `i830_render.c:153`.
- `DSTORG_HORT_BIAS(x)` = `(x)<<20`, `DSTORG_VERT_BIAS(x)` = `(x)<<16`. Use
  site: `i915_render.c:181` sets **both to `0x8`** unconditionally for every
  destination format.

`0x8` in a 4-bit field is half of 16, i.e. **a half-pixel subpixel bias**. This
is the field the plan flagged as deciding "whether a software reference can
ever agree": with `0x8/0x8` the rasteriser samples at pixel centres, which is
the same convention `src/common/d3d_raster.c` uses. Recorded as the value to
use, and as the reason the comparison is expected to agree away from edges.

### `_3DSTATE_DRAW_RECT` - inclusive

`(CMD_3D|(0x1d<<24)|(0x80<<16)|3)`, five dwords: command, then one zero dword,
then ymin/xmin, then ymax/xmax, then yorig/xorig.

`DRAW_YMAX(x)` = `(x)<<16`, `DRAW_XMAX(x)` = `(x)`.

**Inclusive.** Use site `i915_render.c:904-906` emits
`DRAW_YMAX(height - 1) | DRAW_XMAX(width - 1)`. For a 640x480 target that is
`DRAW_YMAX(479) | DRAW_XMAX(639)` = `0x01DF_027F`. The same file notes the
draw rect is emitted **unconditionally**, unlike `BUF_INFO`.

## 5. Established: scissor, and the state-immediate encoding

### Scissor disable

`_3DSTATE_SCISSOR_ENABLE_CMD | DISABLE_SCISSOR_RECT`, a single dword. Use site
`i915_3d.c:96`. `i915_3d.c` follows it with `_3DSTATE_SCISSOR_RECT_0_CMD` and
two zero dwords; whether those are required when the enable bit is clear is
**not established** - see §8.

### `_3DSTATE_LOAD_STATE_IMMEDIATE_1` and the S-register encoding

`(CMD_3D | (0x1d<<24) | (0x04<<16))`, with `I1_LOAD_S(n)` = `1<<(4+n)` and a
trailing length field.

The length encoding is established from a use site rather than inferred:
`i915_3d.c:88` emits `I1_LOAD_S(3) | I1_LOAD_S(4) | I1_LOAD_S(5) | 2` followed
by exactly three dwords. `i915_render.c:927` emits `I1_LOAD_S(2) | I1_LOAD_S(6)
| 1` followed by two, and `i915_render.c:962` emits `I1_LOAD_S(1) | 0` followed
by one. **The trailing field is (number of S dwords - 1)**, and the S dwords
follow in ascending register order, one per set bit.

### S4 - the field the plan called the most likely silent hang

Double-sourced, and the two sites disagree in a way that is informative rather
than contradictory.

| Field | Value | Mesa site | xf86 site |
|---|---|---|---|
| `S4_VFMT_XY` | `(3<<6)` | - | `i915_3d.c:93` |
| `S4_VFMT_COLOR` | `(1<<10)` | `i915_fragprog.c:1268` | - |
| `S4_CULLMODE_NONE` | `(1<<13)` | `i915_state.c:566` | `i915_3d.c:92` |
| `S4_FLATSHADE_COLOR` | `(1<<15)` | `i915_state.c:741,746` | - |
| `S4_LINE_WIDTH_ONE` | `(0x2<<19)` | - | `i915_3d.c:91` |
| point width | `1 << 23` | - | `i915_3d.c:90` |

`S4_CULLMODE_NONE` is the only field used by both, and it agrees. The vertex
format is the split: xf86's minimal path declares `S4_VFMT_XY` (screen X,Y
only, colour from the default-diffuse register), Mesa declares `S4_VFMT_COLOR`
alongside a position format when colour is per-vertex.

**Both are viable for Phase 5 and they imply different vertex dwords.** The
plan requires S4's format bits to "agree exactly with the vertex dwords", so
the choice must be made and recorded before `i9xx_vertex.c` is written:

- **XY + default diffuse** - 2 dwords per vertex, colour from
  `_3DSTATE_DFLT_DIFFUSE_CMD`. Simplest, fewest floats, and xf86 ships it. Does
  not exercise per-vertex colour.
- **XY + COLOR** - 3 dwords per vertex. Closer to what Phase 6 would need.

~~Recommendation, to be confirmed in the design step: **XY + default diffuse**,
because it minimises the number of simultaneously-unverified things in the
first triangle, and because the flat-shaded requirement makes per-vertex colour
redundant. `S4_FLATSHADE_COLOR` is then irrelevant, which removes a field whose
provoking-vertex semantics are single-sourced.~~

> **Reversed 2026-09-15. Do not use this.** Reading the shader path showed the
> reasoning backwards: XY + default diffuse minimises *dwords* while maximising
> *unsourced behaviour* - the default-diffuse path is used by neither tree and
> `S4_FORCE_DEFAULT_DIFFUSE` has no use site at all. The replacement is
> **XYZW + per-vertex colour, all three vertices the same colour**, which keeps
> `S4_FLATSHADE_COLOR` off the critical path for a better reason. See "This
> reverses the vertex-format recommendation in section 5" at the end of §7.

## 6. Established: the indirect-state question the plan flagged as a near-kill

The plan asked "whether the pixel shader may be emitted inline or whether the
`LOAD_INDIRECT` PSP pointer is mandatory", and set out that an affirmative
answer would not automatically kill the phase but would enlarge it.

**Indirect state can be explicitly disabled.** `i915_3d.c:101-102`:

```
_3DSTATE_LOAD_INDIRECT | 0
0
```

- one command dword with an empty enable mask, one zero dword - commented in
  the source as "disable indirect state". `_3DSTATE_LOAD_INDIRECT` is
`(CMD_3D|(0x1d<<24)|(0x7<<16))` in both trees.

So the PSP indirect pointer is **not mandatory**, and the shader is emitted
inline through `_3DSTATE_PIXEL_SHADER_PROGRAM`
`(CMD_3D|(0x1d<<24)|(0x5<<16))`. **This removes the plan's largest structural
unknown in Part 2** and keeps the Phase 5 stream to a single flat array in the
reserve, exactly as designed.

The inline shader's own encoding is **not yet established** - see §8.

## 7. Established: primitive dispatch

`_3DPRIMITIVE` = `(CMD_3D | (0x1f << 24))`, with `PRIM3D_TRILIST` = `(0x0<<18)`
and `PRIM_INLINE` = `(0<<23)` / `PRIM_INDIRECT` = `(1<<23)`. Both trees define
the full primitive table identically and both name inline as bit 23 clear.

Mesa spells the inline form `PRIM3D_INLINE`; xf86 spells the base `PRIM3D` and
ORs the topology in. Same encoding, different spelling - which is the pattern
throughout and the reason §2's rule looks at use sites.

### S0 and S1 are not required for an inline primitive

This was §8's most important open item, because it decides the shape of the
vertex run. It is now closed, double-sourced, and the answer is **no**.

**What S0 and S1 are for.** S0 is the vertex buffer address
(`S0_VB_OFFSET_MASK`) and S1 the vertex width and pitch
(`S1_VERTEX_WIDTH_SHIFT` 24, `S1_VERTEX_PITCH_SHIFT` 16, both `0x3f` wide).
Both are parameters for *striding through a buffer*, and both are meaningless
when the vertices are already in the command stream.

**Mesa, structurally.** `i915_context.h:79-88` lists the entire emitted context
state: `I915_CTXREG_LIS2` through `LIS6`. **There is no `LIS0` or `LIS1`.**
S0 and S1 are not context state at all - they are loaded only by the indirect
vertex-buffer path in `intel_tris.c:245-279`, which is guarded by
`vb_bo != i915->current_vb_bo` and `vertex_size != current_vertex_size`. The
always-emitted state is `i915_state.c:927-930`:

```
_3DSTATE_LOAD_STATE_IMMEDIATE_1 | I1_LOAD_S(2) | I1_LOAD_S(3) |
                                  I1_LOAD_S(4) | I1_LOAD_S(5) |
                                  I1_LOAD_S(6) | 4
```

Five S registers, length field 4. A fifth independent confirmation of §5's
`(count - 1)` rule.

**Mesa's inline path, which loads neither.** `intel_start_inline`
(`intel_tris.c:89-112`) emits state, reserves one dword, and appends vertex
dwords through `intel_extend_inline`. `intel_flush_inline_primitive`
(`intel_tris.c:65-87`) then back-patches the reserved dword. It touches no S
register. `intel_set_prim` and `intel_get_prim_space` select it on
`intelScreen->no_vbo` alone - **there is no generation guard**, so this is the
same code on Gen2 and Gen3.

**xf86, independently.** `i830_render.c:788-790` emits
`PRIM3D_INLINE | PRIM3D_RECTLIST` and then writes vertex floats directly with
`OUT_BATCH_F`, with nothing between the command dword and the first vertex.

**The length field.** Both trees back-patch it, and the two formulas are
identical:

| Tree | Code | Reduces to |
|---|---|---|
| Mesa | `_3DPRIMITIVE \| primitive \| (used - 2)`, `used = 1 + vertex dwords` | vertex dwords − 1 |
| xf86 | `batch_ptr[vertex_index] \|= vertex_count - 1`, `vertex_count = 3 * per_vertex` | vertex dwords − 1 |

**Length field = (total vertex dwords) − 1.** Note it counts *dwords*, not
vertices, and does not include the command dword.

**How the GPU knows the vertex width.** From S2 (texture coordinate formats)
and S4 (the `S4_VFMT_*` bits) - both of which *are* always-emitted context
state. This is the structural reason S1 is unnecessary inline: the vertex
layout is already fully declared, and S1 only tells a buffer-walker how far to
step.

**Vertices are raw IEEE-754 float dwords in screen pixel units.** `OUT_BATCH_F`
is a float-to-uint32 pun, and `i830_render.c:791-792` emits `dstX + w` and
`dstY + h` directly - not normalised, not fixed-point, not clip-space. This
validates the plan's `i9xx_float.c` design: bit-preserving float transport with
no FPU is exactly what the hardware wants, and nothing 16-bit needs a `float`
type.

**Consequence for Phase 5.** The stream is: state packets, then one
`_3DPRIMITIVE | PRIM3D_TRILIST | (dwords - 1)` dword, then the vertex dwords.
No vertex buffer, no S0, no S1, no second allocation in the reserve. `i9xx_ring.c`'s
existing `v9x_i9xx_ring_plan` can size the whole thing as one contiguous run.

### The fragment program

Closed 2026-09-15. The encoding is double-sourced by two **independently
written emitters**, which is the strongest evidence in this audit: xf86's
`i915_3d.h` macros expand register fields inline, while Mesa's
`i915_program.c` lowers from its own `UREG` intermediate form through
`A0_DEST()` / `A0_SRC0()` helpers with entirely different shift constants. Two
implementations, one encoding.

**Framing.** `_3DSTATE_PIXEL_SHADER_PROGRAM` = `(CMD_3D|(0x1d<<24)|(0x5<<16))`,
with the length back-patched after the body:

| Tree | Code | Reduces to |
|---|---|---|
| xf86 | `FS_END`: `batch_used - _shader_offset - 2` | payload dwords - 1 |
| Mesa | `i915_fini_program`: `declarations[0] \|= program_size + decl_size - 2` | payload dwords - 1 |

The same `(count - 1)` convention as every other packet in this audit.

**Every instruction is exactly three dwords.** xf86's `i915_fs_dcl` emits D0
then two zeros; Mesa's `i915_emit_decl` emits `D0_DCL | D0_DEST(reg) | flags`,
`D1_MBZ`, `D2_MBZ`. xf86's `_i915_fs_arith_masked` and Mesa's
`i915_emit_arith` both emit A0, A1, A2. Field placement agrees:

- **DCL**: `D0_DCL` `(0x19<<24)`, type at shift 19, register number at shift
  14, channel mask at shift 10 (`D0_CHANNEL_ALL` = `0xf<<10`).
- **ALU**: opcode at shift 24 (`A0_MOV` = `0x2<<24`), dest type at 19, dest
  number at 14, dest channel mask at 10, src0 type at 7, src0 number at 2;
  then A1 carries src0's per-channel swizzle at shifts 28/24/20/16 and src1;
  then A2 carries src1's remainder and src2.
- Register types: `REG_TYPE_T` 1, `REG_TYPE_OC` 4. `T_DIFFUSE` is T register
  **8**.

**Errata recorded in the source**, worth carrying into our header: for `T`
declarations only `(x)`, `(xy)`, `(xyz)`, `(w)` and `(xyzw)` are allowed -
`(xz)`, `(xw)` and `(xzw)` are forbidden for diffuse or specular. Phase 5 uses
`(xyzw)`, which is allowed.

### This reverses the vertex-format recommendation in section 5

Section 5 recommended **XY + default diffuse**, on the reasoning that it
minimises the number of simultaneously-unverified things. Reading the shader
path shows that reasoning was backwards: it minimises *dwords* while maximising
*unsourced behaviour*.

**The default-diffuse path is used by neither tree.**

- Mesa declares and reads `T_DIFFUSE` (`i915_fragprog.c:121`, for
  `VARYING_SLOT_COL0`) - but always with **per-vertex** colour. Its
  default-diffuse state block (`i915_state.c:997-1004`) is inside `#if 0`.
- xf86 emits `_3DSTATE_DFLT_DIFFUSE_CMD` with a zero operand
  (`i915_3d.c:57`, `i830_3d.c:48`) - but its shader never reads `T_DIFFUSE`,
  because a compositor's colour comes from a texture.

So *nobody* sets a non-zero default diffuse and then reads it from a shader.
Whether the `T_DIFFUSE` interpolator falls back to the default register when
the vertex format declares no colour is **unestablished**, and
`S4_FORCE_DEFAULT_DIFFUSE` `(1<<5)` - the bit that would plausibly control it -
has **zero use sites in either tree**. It is header-only.

**Revised recommendation: per-vertex colour, with all three vertices the same
colour.** Every link is then use-site backed:

| Element | Encoding | Source |
|---|---|---|
| position | `S4_VFMT_XYZW` `(2<<6)`, 4 floats | Mesa `i915_fragprog.c:1260` |
| colour | `S4_VFMT_COLOR` `(1<<10)`, **one dword of packed BGRA unsigned bytes** | Mesa `i915_fragprog.c:1268` (`EMIT_4UB_4F_BGRA`, size 4) |
| shader reads it | `dcl T_DIFFUSE` then `mov OC, T_DIFFUSE` | Mesa `i915_fragprog.c:121` |

Two consequences worth stating plainly:

- **The colour is not four floats.** It is one dword of packed bytes in BGRA
  order. A builder that emits floats here would be wrong in a way that still
  produces a plausible picture, which is the worst kind of wrong.
- **Giving all three vertices the same colour makes flat versus smooth shading
  moot**, which removes `S4_FLATSHADE_COLOR` and the provoking-vertex rules
  from the critical path entirely - both were single-sourced. The plan asks for
  a flat-shaded triangle; a uniformly-coloured one satisfies that under either
  shading mode, and cannot disagree with the software reference about which
  vertex provided the colour.

**Why XYZW rather than XY**, despite XY being fewer dwords: both position
formats are single-sourced at bit level (`S4_VFMT_XY` only in xf86
`i915_3d.c:93`, `S4_VFMT_XYZW` only in Mesa), so neither is free. XYZW is the
better bet because Mesa's is the general-purpose path exercised by every GL
application on this silicon, and because Mesa's own comment at
`i915_fragprog.c:1257-1259` says W is emitted **"Always ... to get consistent
perspective correct interpolation of primary/secondary colors"** - which is
precisely the interpolation Phase 5 depends on. With W = 1.0 at every vertex
the perspective divide is the identity, so screen coordinates pass through
unchanged.

Vertex is therefore **5 dwords**: x, y, z, w as floats, then one packed colour
dword. Three vertices = 15 dwords, and the `_3DPRIMITIVE` length field is 14.

### The minimal program, derived

Per `docs/ddk-inputs.md` these are **derived from the field placements above,
not transcribed**, and they are **unvalidated** - they must be reproduced by
`i9xx_fragprog.c` and asserted by a host test before any of them reaches the
netbook:

```
7D050005   _3DSTATE_PIXEL_SHADER_PROGRAM, length 5 (6 payload dwords)
190A3C00   D0: DCL, type T(1)<<19, nr 8<<14, channels xyzw
00000000   D1_MBZ
00000000   D2_MBZ
02203CA0   A0: MOV, dest OC(4)<<19 nr 0, channels xyzw, src0 T(1)<<7 nr 8<<2
01230000   A1: src0 swizzle .xyzw (X=0<<28, Y=1<<24, Z=2<<20, W=3<<16)
00000000   A2: no src1, no src2
```

Seven dwords in total. The header value is the one to check first, since it is
the only one a length error can hide in.

## 8. Open - nothing may be built against these yet

The plan's rule is that any value that would have to be guessed or swept kills
the phase. **Nothing in this list is currently in that state**; they are
unfinished reading, not dead ends. But no builder may be written until each is
resolved to §2's standard.

1. ~~The minimal fragment program.~~ **Closed 2026-09-15**; see the end of
   section 7. One thing it raises stays open: whether the derived seven-dword
   program is byte-correct. That is a host-test obligation, not a reading one.
2. ~~S0/S1 - the vertex buffer.~~ **Closed 2026-09-15**; see the end of §7.
   They are not required for an inline primitive.
3. **Whether `LOAD_STATE_IMMEDIATE_2` may be omitted.** Not yet examined.
4. **Depth buffer disable.** Phase 5 is un-Z'd. `_3DSTATE_DEPTH_SUBRECT_DISABLE`
   appears at `i915_3d.c:99`, and `DEPTH_FRMT_*` bits live in `DST_BUF_VARS`,
   but whether a second `BUF_INFO` with `BUF_3D_ID_DEPTH` must be emitted (and
   what a null depth buffer looks like) is not established.
5. **Whether the two zero dwords after `_3DSTATE_SCISSOR_RECT_0_CMD` are
   required** when scissor is disabled (§5).
6. **The invariant-state block's necessity.** `I915EmitInvarientState` emits
   ~20 packets. Which are genuinely required after a cold ring versus which are
   xf86 defending against its own prior state is not established, and it
   matters: every packet Phase 5 emits is a packet that can hang it.
7. **Provoking vertex.** `_3DSTATE_RASTER_RULES_CMD` at `i915_3d.c:75-81` sets
   `TRI_FAN_PROVOKE_VRTX(2)` and related. Single-sourced, and only relevant if
   the XY+COLOR vertex format is chosen (§5).

### Single-sourced - must not be used

Values appearing in the shared header and used by only one tree, recorded so a
later reader does not mistake header presence for evidence:

| Value | Used by | Note |
|---|---|---|
| `S4_FLATSHADE_COLOR` `(1<<15)` | Mesa only | irrelevant under the recommended vertex format |
| `S4_VFMT_COLOR` `(1<<10)` | Mesa only | ditto |
| `S4_VFMT_XY` `(3<<6)` | xf86 `i915_3d.c:93` only | **needed by the recommended format - must be resolved before use.** Its *meaning* is now corroborated - `i830_render.c:674` and `i915_render.c:703` both start their per-vertex count at `2; /* dest x/y */`, so an XY-only vertex is two dwords in both trees - but the bit value `3<<6` still has one use site. |
| `MI_FLUSH` bits 1, 3, 4 | xf86 header only, no use site | Phase 5 leaves all three clear |
| `LOD_PRECLAMP_*`, `DEPTH_FRMT_*` | not used in the read files | out of scope |

`S4_VFMT_XY` is the one entry on this list that the recommended design needs.
Resolving it - by finding a second use site, or by choosing the Mesa-sourced
format instead - is a precondition for §5's recommendation, not a detail.

## 9. Consequence for the plan

Two of Part 2's named risks are **reduced** by this audit:

- the indirect PSP pointer is not mandatory (§6), so the stream stays one
  reviewed array;
- the subpixel bias is a known constant that matches our software rasteriser's
  convention (§4), so the reference comparison is expected to agree away from
  edges.

One is **raised**: the plan's cross-check methodology assumed two independent
trees and had one tree plus a fork (§2). Every claim above has been re-derived
against use sites instead, and the seven open items in §8 must be held to the
same standard. The audit is not finished, and **step 2 (the memory layout move)
must not start until §8 is closed** - which is the plan's own ordering rule,
and the reason it put the audit first.

## 10. Confidence

Everything in §4-§7 is documentation-derived and unconfirmed on this machine.
The GMA 950 in MICHAEL-NETBOOK has executed exactly one command in its life
under this driver - an `XY_COLOR_BLT`, on 2026-09-14 - and nothing in this
document has been near it.
