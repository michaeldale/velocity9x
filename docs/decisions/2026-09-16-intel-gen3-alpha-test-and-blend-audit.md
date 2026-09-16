# Gen3 audit: alpha test and source-alpha blend

**Purpose**: license the last two Phase 6 features before any code is written
for either. Same discipline and the same two trees as the
[texture](2026-09-16-intel-gen3-texture-packet-audit.md) and
[modulate/depth](2026-09-16-intel-gen3-modulate-and-depth-audit.md) audits.

**Sources**: Mesa 22.3.0 gallium `i915` (OpenGL) and xf86-video-intel 2.20.19
(X Render, XvMC, Xv).

**Rule applied**: a value may enter a builder only if it is *used*,
consistently, by two code paths with different authors and different purposes.
Where that bar is not met it is stated as not met.

---

## 1. Alpha test: S6's top twelve bits

| Field | Value | Sources |
|---|---|---|
| `S6_ALPHA_TEST_ENABLE` | `1 << 31` | Mesa `i915_reg.h:395`; xf86 `i915_reg.h:421` |
| `S6_ALPHA_TEST_FUNC_SHIFT` | `28`, 3-bit field | Mesa `:396`; xf86 `:422` |
| `S6_ALPHA_REF_SHIFT` | `20`, 8-bit field | Mesa `:398`; xf86 `:424` |
| `COMPAREFUNC_*` | as for depth | Mesa `i915_reg.h:893-900` |

Mesa's use site (`i915_state.c:494`) builds it in one expression:

```c
cso->depth_LIS6 |= (S6_ALPHA_TEST_ENABLE | (test << S6_ALPHA_TEST_FUNC_SHIFT) |
                    (((unsigned)refByte) << S6_ALPHA_REF_SHIFT));
```

The reference is an **8-bit unsigned byte**, not a float and not a 5- or 6-bit
quantity: `float_to_ubyte(alpha_ref_value)`. So the comparison happens at
8-bit precision regardless of the render target's format.

**Alpha test does not depend on the destination.** It compares the fragment's
own alpha against the reference, so a target with no alpha channel - which
RGB565 is - is not a constraint on it. That is a property of what the field
compares, not a claim about silicon.

**Single-source note**: only Mesa has an alpha-test *use* site. xf86 never
enables it, in any of its three Gen3 paths. The field definitions are
double-sourced and identical; the use is not. That is weaker than the depth
function, where xf86 independently wrote a literal into the same shift.

## 2. Blend: S6's middle bits, and both trees agree on the shape

| Field | Value | Sources |
|---|---|---|
| `S6_CBUF_BLEND_ENABLE` | `1 << 15` | Mesa `:403`; xf86 `:429` |
| `S6_CBUF_BLEND_FUNC_SHIFT` | `12`, 3-bit | Mesa `:404`; xf86 `:430` |
| `S6_CBUF_SRC_BLEND_FACT_SHIFT` | `8`, 4-bit | Mesa `:406`; xf86 `:432` |
| `S6_CBUF_DST_BLEND_FACT_SHIFT` | `4`, 4-bit | Mesa `:408`; xf86 `:434` |
| `BLENDFUNC_ADD` | `0x0` | Mesa `:239`; xf86 `i915_reg.h:252`, `i830_reg.h:443`, `sna/gen2_render.h:372` |
| `BLENDFACT_ONE` | `0x02` | Mesa `:929`; xf86 `i915_reg.h:236`, `sna/gen3_render.h:239` |
| `BLENDFACT_SRC_ALPHA` | `0x05` | Mesa `:932`; xf86 `:239`, `sna/gen3_render.h:242` |
| `BLENDFACT_INV_SRC_ALPHA` | `0x06` | Mesa `:933`; xf86 `:240`, `sna/gen3_render.h:243` |

**Double-sourced by use, in the same shape.** xf86 `sna/gen3_render.c:190`:

```c
return (S6_CBUF_BLEND_ENABLE | S6_COLOR_WRITE_ENABLE |
        BLENDFUNC_ADD << S6_CBUF_BLEND_FUNC_SHIFT |
        sblend << S6_CBUF_SRC_BLEND_FACT_SHIFT |
        dblend << S6_CBUF_DST_BLEND_FACT_SHIFT);
```

Mesa `i915_state.c:209` assembles the identical four terms. Two authors, two
purposes, one expression.

## 3. The finding: `_3DSTATE_INDEPENDENT_ALPHA_BLEND` is missing from our stream

This driver does not emit `_3DSTATE_INDEPENDENT_ALPHA_BLEND`, and **must start
doing so before it enables blending.**

xf86 emits it in its **invariant** block, twice and independently, each with the
comment *"Disable independent alpha blend"*:

- `i915_3d.c:49`
- `sna/gen3_render.c:1253`

Mesa emits it from blend state rather than invariant state
(`i915_state.c:169,177`), which is why it fell outside the *intersection* of the
two invariant blocks that the 2026-09-14 packet audit chose as this driver's
floor. The intersection rule is sound and this is the case where it
under-emits: a packet one tree treats as invariant and the other as blend state
appears in neither intersection.

**Why it stops being harmless.** IAB blends the alpha channel with its own
function and factors, independent of the colour ones. While `S6_CBUF_BLEND_ENABLE`
is clear nothing consults it, which is every stream this driver has ever run.
The moment a blend scene sets that bit, whatever IAB state the engine last had
applies - and this driver has never reset an engine or seen one freshly reset.

The disable form is identical in both xf86 sites, bit for bit:

```c
_3DSTATE_INDEPENDENT_ALPHA_BLEND_CMD |
IAB_MODIFY_ENABLE |                                  /* IAB_ENABLE left CLEAR */
IAB_MODIFY_FUNC       | (BLENDFUNC_ADD   << IAB_FUNC_SHIFT)   |
IAB_MODIFY_SRC_FACTOR | (BLENDFACT_ONE   << IAB_SRC_FACTOR_SHIFT) |
IAB_MODIFY_DST_FACTOR | (BLENDFACT_ZERO  << IAB_DST_FACTOR_SHIFT)
```

| Field | Value | Sources |
|---|---|---|
| command | `CMD_3D \| (0x0b << 24)` = `0x6B000000` | Mesa `:227`; xf86 `i915_reg.h:223` |
| `IAB_MODIFY_ENABLE` | `1 << 23` | Mesa `:228`; xf86 `:224` |
| `IAB_ENABLE` | `1 << 22` | Mesa `:229`; xf86 `:225` |
| `IAB_MODIFY_FUNC` | `1 << 21` | Mesa `:230`; xf86 `:226` |
| `IAB_FUNC_SHIFT` | `16` | Mesa `:231`; xf86 `:227` |
| `IAB_MODIFY_SRC_FACTOR` | `1 << 11` | Mesa `:232`; xf86 `:228` |
| `IAB_SRC_FACTOR_SHIFT` | `6` | Mesa `:233`; xf86 `:229` |
| `IAB_MODIFY_DST_FACTOR` | `1 << 5` | Mesa `:235`; xf86 `:231` |
| `IAB_DST_FACTOR_SHIFT` | `0` | Mesa `:236`; xf86 `:232` |

**The dword is `0x6BA008A1`.**

Note the `MODIFY` bits: this packet changes only the fields whose modify bit is
set, so the disable must set all three field-modify bits as well as
`IAB_MODIFY_ENABLE`. Setting `IAB_MODIFY_ENABLE` alone would leave the factors
as they were, which is the mistake this paragraph exists to prevent.

## 4. Where the alpha comes from, and why no new texture format is needed

The previous audit left this open: "an RGB565 texture carries no alpha, so the
alpha source is either vertex alpha or a `MAPSURF` sub-format this driver has
never emitted." The answer is **vertex alpha, through the program this driver
already has**, and a new format is not needed.

### The vertex diffuse dword carries alpha in its top byte

Mesa's vertex emitter (`i915_prim_emit.c:105`) packs a `EMIT_4UB_BGRA` colour as
`pack_ub4(attrib[2], attrib[1], attrib[0], attrib[3])`, and `pack_ub4`
(`u_pack_color.h:639`) places its arguments at bits 0, 8, 16 and 24. So the
dword is `B | G<<8 | R<<16 | A<<24` - **alpha in the top byte**.

**Single-sourced for the vertex path.** xf86's Gen3 render path emits no
per-vertex diffuse colour at all: it puts its colour in a fragment-program
constant (`gen3_init_solid`, `SHADER_CONSTANT`, `FS_C0`). The nearest xf86
corroboration is `gen3_render.c:2100`, `channel->is_opaque = (color >> 24) ==
0xff`, which is a Render-protocol colour rather than a vertex dword.

**But this project has already measured three of the four bytes.** Every capture
from `intel41` to `intel46` confirms R at bits 16-23, G at 8-15 and B at 0-7 -
the driver's own constant `0xff1587f9` renders as `1C3E`, repeatedly. The alpha
byte is the one remaining, by elimination. What is *not* established is that the
hardware treats it as alpha, and that is what the experiment measures rather
than assumes.

### The untextured program already writes it

`mov oC, T8.xyzw` moves all four channels of the interpolated diffuse register
to the output colour, alpha included. So an **untextured** alpha scene needs no
program change, no new texture format, and no assumption about what a 565
texel's alpha reads as - the texture is not in the path at all.

That is also the right isolation: the alpha features are being measured, not
the texture's interaction with them.

**`S5_WRITEDISABLE_ALPHA` must stay clear**, which it is: this driver emits
`S5 = 0`. Mesa sets that bit from a colour mask (`i915_state.c:202`); with it
set, the alpha the blender needs would never be written.

## 5. Destination alpha is NOT sourced, and is excluded

Mesa carries a remap - `i915_remap_lis6_blend_dst_alpha` (`i915_state.c:102`) -
that rewrites `BLENDFACT_DST_ALPHA` to `ONE` and `INV_DST_ALPHA` to `ZERO` for
render targets whose alpha does not exist. It applies that remap to
`rgbx_or_bgrx` formats and to `A8`. **It does not apply it to B5G6R5**
(`i915_surface.c:366-369`), which means Mesa either believes the hardware
supplies alpha = 1 for a 565 destination or never asks. Neither tree states
which.

**Consequence: use source-side factors only.** `SRC_ALPHA` and
`INV_SRC_ALPHA` are functions of the fragment, not the destination, so they are
unaffected. `DST_ALPHA`, `INV_DST_ALPHA` and `SRC_ALPHA_SATURATE` are excluded
from this step and remain unlicensed.

That is no loss: "source-alpha blend" in the plan means exactly
`SRC_ALPHA, INV_SRC_ALPHA`, which is what every alpha-over composite uses.

## 6. The experiments

Both scenes are **untextured**, drawing flat-coloured overlapping triangles.

### Alpha test

`S6_ALPHA_TEST_ENABLE`, `COMPAREFUNC_GREATER`, reference `0x80`. Three
triangles at different vertex alphas - say `0xC0`, `0x40` and `0xFF` - each with
its own region and a shared one.

- The `0xC0` and `0xFF` triangles draw.
- The `0x40` triangle does **not**, anywhere.
- It must still have a region where it alone would have drawn, so that
  "correctly rejected" is distinguishable from "never executed" - the same
  requirement the depth scene's third triangle had, and for the same reason.

This measures the alpha test AND, in the same picture, that the top byte is
alpha: if it is not, all three triangles draw or none do.

### Source-alpha blend

`S6_CBUF_BLEND_ENABLE`, `BLENDFUNC_ADD`, `SRC_ALPHA`, `INV_SRC_ALPHA`, with the
IAB disable emitted. One opaque triangle drawn first, then a second at alpha
`0x80` overlapping it.

The overlap must read **neither** source colour and must lie between them. As
with modulation, the exact value is a prediction - it depends on the blend
arithmetic and then on the 565 conversion - so it is **reported**, while the two
things that are not predictions **fail**: reading either unblended colour means
no blend happened, and reading the fill means nothing drew.

The non-overlapping regions of both triangles read their own colours, which is
what says both draws executed.

## 7. The scene table: this needs two slots and there are none

Five scenes, five authorised. Two more features need two more slots, and seven
draws per boot would need **a further errata amendment** - a risk decision, not
a build decision.

**The alternative, and the one to prefer**: retire the two scenes whose
questions `intel46` answered.

| Scene | Status | Proposal |
|---|---|---|
| 0 plain | The untextured regression | Keep |
| 1 texture | Addressing measured `intel45`, reconfirmed `intel46` | **Retire** - scene 2 covers the texture path |
| 2 modulate | Multiply measured, conversion narrowed | Keep; it is now the texture regression too |
| 3 depth test, no writes | Binding proven to work | **Retire** - scene 4 covers the binding |
| 4 depth write | The depth result | Keep |

That leaves three kept scenes and two free slots, needs no amendment, and
matches what was done when the colour and edge scenes retired.

**The cost, stated**: if the modulate scene fails on a later boot, "the texture
path broke" and "the multiply broke" become indistinguishable in that boot,
where today scene 1 would separate them. `intel46` measured both, so the
evidence exists; what is lost is the ability to re-separate them without a
scene. That is the same trade the retirement of the colour scene made.

Ids 5 and 7 retire and are not reused, as 1 through 4 were not.

## 8. What this audit does not license

- **`DST_ALPHA`, `INV_DST_ALPHA`, `SRC_ALPHA_SATURATE`** - see section 5.
- **Blend functions other than `ADD`.** `SUBTRACT`, `MIN` and `MAX` are defined
  in both trees and used by neither on this path.
- **Independent alpha blending.** The packet is emitted only to DISABLE it.
- **Any texture format with alpha.** `MT_16BIT_ARGB1555` and `MT_16BIT_ARGB4444`
  exist and are not needed; a textured alpha source is a separate step.
- **Constant blend colour.** `_3DSTATE_CONST_BLEND_COLOR` is emitted by xf86 and
  is not needed for source-alpha factors.
- **Logic ops and stencil**, which `_3DSTATE_MODES_4` carries. Out of scope.
- **Any claim about the alpha byte's position** beyond elimination from three
  measured bytes. The alpha-test scene measures it.
