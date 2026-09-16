# Gen3 texture packet audit: MAP_STATE, SAMPLER_STATE, S2 and texld

Extends `2026-09-14-intel-gen3-3d-packet-audit.md` to the textured case. That
audit was scoped to "one flat-shaded, untextured, un-Z'd triangle" and mentions
`MAP_STATE` once, only to say a Gen2 packet is inapplicable because Gen3 loads
maps through it instead. It derives neither it nor `SAMPLER_STATE`.

## 1. Sources, and the rule

Same rule as the parent audit, which is the one that matters:

> A value may enter a builder only if it is **used**, consistently, by two code
> paths with different authors and different purposes. The shared header
> supplies the *name*; the two use sites supply the *evidence*.

The two trees, cloned to `build/external/audit`:

- **Mesa**, `mesa-22.3.0`, `src/gallium/drivers/i915/`. The classic DRI i915
  driver the parent audit used was removed before this tag; the **gallium**
  i915 driver is what remains. It is a different codebase by different authors
  from the classic one, which makes it an independent use site rather than a
  weaker one.
- **xf86-video-intel**, tag `2.20.19`, `src/i915_render.c`, `src/i915_3d.h`.
  The last release carrying the UXA Gen3 render path.

Mesa implements OpenGL; xf86 implements the X Render extension. Different
authors, different eras, different goals, same silicon.

`gitlab.freedesktop.org` serves git but blocks HTTP fetches behind Anubis, so
both were cloned rather than read over the web.

## 2. The finding that removes the most work

**`MAP_STATE` and `SAMPLER_STATE` are emitted DIRECTLY into the batch. Neither
goes through `_3DSTATE_LOAD_INDIRECT`.**

`i915_state_emit.c:emit_map` and `i915_render.c:861` both `OUT_BATCH` the
command and its payload inline, in the same stream as everything else.

The plan treated indirect state as an open question and the larger risk: it
would have meant state buffers in graphics memory, a second class of
GPU-readable allocation, and pointers nobody had derived. None of that is
needed. `LOAD_INDIRECT` stays exactly as Phase 5 emits it - an empty enable
mask and one zero dword, which is what makes the inline fragment program legal.

## 3. The finding that removes the largest risk

**S4 carries no texture-coordinate field.** Its bits are point width, specular
fog, colour, depth offset, the position format (XYZ / XYZW / XY / XYW) and a
fog parameter. There is no count and no per-unit field.

S2 alone declares which coordinate sets exist and their formats, and that is
what determines the vertex layout.

The plan named S4 as the biggest hazard, quoting the state builder's own
comment that a disagreement between S4 and the vertex dwords is "the single
most likely silent hang in the whole phase". **S4 does not change.** The vertex
grows, and S2 is what says so.

## 4. Established: `_3DSTATE_MAP_STATE`

Opcode `CMD_3D | (0x1d << 24) | (0x0 << 16)`, identical in both headers.

Both trees build the command the same way:

| | Mesa `emit_map` | xf86 `i915_render.c` |
|---|---|---|
| Command | `_3DSTATE_MAP_STATE \| (3 * nr)` | `_3DSTATE_MAP_STATE \| (3 * tex_count)` |
| Next dword | `sampler_enable_flags` | `(1 << tex_count) - 1` |
| Per unit | address, MS3, MS4 | address, MS3, MS4 |

So the length field is **three dwords per map**, the payload is an enable mask
followed by three dwords per enabled unit, and the length is therefore
`payload - 1` — the same convention `LOAD_STATE_IMMEDIATE_1` uses.

### MS3, the first per-map dword

Both trees compose it identically:

```
format | tiling | ((height - 1) << MS3_HEIGHT_SHIFT)
                | ((width  - 1) << MS3_WIDTH_SHIFT)
```

`MS3_HEIGHT_SHIFT` 21, `MS3_WIDTH_SHIFT` 10 in both headers. **Dimensions are
minus one**, stated by both use sites, not inferred from a header.

Format for our case: `MAPSURF_16BIT` `(2 << 7)` with `MT_16BIT_RGB565`
`(0 << 3)`, so the format contribution is `0x00000100`. Mesa names this pair in
`translate_texture_format`; xf86 reaches the same encoding through its own
format table.

Tiling bits `MS3_TILED_SURFACE (1 << 1)` and the tile-walk bit `(1 << 0)` must
both be **clear** for a linear texture, on the same argument the render target
already uses.

### MS4, the second per-map dword

Here the two trees **diverge**, and the divergence is recorded rather than
resolved by preference:

- **xf86** sets `((pitch / 4) - 1) << MS4_PITCH_SHIFT` and nothing else.
- **Mesa** additionally ORs `MS4_CUBE_FACE_ENA_MASK` `(0x3f << 15)`,
  `max_lod << MS4_MAX_LOD_SHIFT`, and `(depth - 1) << MS4_VOLUME_DEPTH_SHIFT`.

`MS4_PITCH_SHIFT` is 21 in both, and both express the pitch as **dwords minus
one** — corroborated.

For a 2D texture with one mip level and no cube faces, Mesa's extra terms are
`max_lod = 0` and `depth - 1 = 0`, which contribute nothing; the cube-face mask
is the only real difference and Mesa sets it unconditionally for every texture
including 2D ones.

**Judgement, recorded as one:** take xf86's minimal form. It is the closer
analogue — a single 2D surface with no mips, sampled once — and enabling six
cube faces on a texture that has none is a claim about state we have no reason
to make. If the hardware refuses, Mesa's form is the first thing to try, and
the difference is one constant.

### The first per-map dword is the address

Both trees emit it as a relocation (`OUT_RELOC`, `OUT_RELOC_PIXMAP`), so it is
a graphics address in the same space as the render target's `BUF_INFO`
address — which is the space Phase 4 measured and Phase 5 uses.

**Not established:** the alignment the address requires. Both trees let the
kernel's relocation machinery place the buffer and neither states a constraint.
Page alignment is what this driver will use, because the reserve is allocated
in pages anyway and it cannot be less safe than a smaller alignment.

## 5. Established: `_3DSTATE_SAMPLER_STATE`

Opcode `CMD_3D | (0x1d << 24) | (0x1 << 16)` — the same `0x1d` major with
sub-opcode 1 rather than 0. Both headers agree.

Identical structure to `MAP_STATE`, and both trees build it the same way:
command `| (3 * nr)`, an enable-mask dword, then three dwords per sampler.

### SS2, filtering

Both use sites compose it from three shifted filter fields:

| Field | Shift | Both trees |
|---|---|---|
| MIP filter | 20 | yes |
| MAG filter | 17 | yes |
| MIN filter | 14 | yes |

`FILTER_NEAREST` is 0 and `MIPFILTER_NONE` is 0, so **nearest-with-no-mips is
SS2 = 0** in the filter fields. xf86's render path sets exactly
`MIPFILTER_NONE << SS2_MIP_FILTER_SHIFT` plus its filter value.

### SS3, addressing

| Field | Shift / bit | Both trees |
|---|---|---|
| `SS3_NORMALIZED_COORDS` | `1 << 5` | yes |
| TCX address mode | 12 | yes |
| TCY address mode | 9 | yes |
| TCZ address mode | 6 | yes |
| Texture map index | 1 | yes |

`TEXCOORDMODE_CLAMP_EDGE` is 2.

The **map index** is the one to notice: `unit << SS3_TEXTUREMAP_INDEX_SHIFT` in
both trees. A sampler names which map it reads; sampler *n* and map *n* are not
implicitly paired. Phase 6 uses unit 0 for both, and the field is still written
explicitly because an implicit pairing is an assumption.

`SS3_NORMALIZED_COORDS` means coordinates are `[0,1]` rather than texels. xf86
sets it unconditionally; Mesa sets it when the sampler is normalized. Phase 6
sets it, which makes the vertex UVs independent of the texture's size.

### SS4 is the border colour

xf86 writes `0x00000000` and comments it as the border colour. With
clamp-to-edge addressing nothing samples the border, so zero is correct and
also inert.

## 6. Established: S2, the texture coordinate format

`S2_TEXCOORD_FMT(unit, type) = type << (unit * 4)` in both headers, and both
use sites treat S2 as **eight nibbles, one per unit**.

`TEXCOORDFMT_NOT_PRESENT` is `0xf` and `TEXCOORDFMT_2D` is `0x0`.

xf86 shows the idiom exactly: start from `~0` — every unit absent, which is the
value Phase 5 already emits — then clear unit 0's nibble and OR in the format.
For one 2D coordinate set on unit 0 that gives `0xfffffff0`.

## 7. Established: the vertex layout

Mesa's `i915_state_derived.c` emits attributes in a fixed order, and that order
is the vertex layout:

1. Position — XYZ or XYZW
2. Point size, if per-vertex
3. Primary colour
4. Secondary colour / specular fog
5. Fog parameter
6. Texture coordinates, units 0..7, each as its S2 nibble declares

So **texture coordinates follow the colour**, and Phase 5's existing vertex —
four position floats then one packed colour dword — grows by two floats at the
end. Seven dwords per vertex.

The same function ORs `hwtc << (i * 4)` into what it calls `hwfmt[1]`, which is
S2 — independently reproducing xf86's `S2_TEXCOORD_FMT` macro from a different
direction. That is the corroboration for the nibble layout.

## 8. Established: `texld` and the declarations

Both trees emit a texture load as **exactly three dwords**, the same width as
the arithmetic instructions the current fragment program already uses:

| Dword | Content | Mesa | xf86 |
|---|---|---|---|
| 0 | `T0_TEXLD \| dest type << 19 \| dest nr << 14 \| sampler nr << 0` | `opcode \| T0_DEST(dest) \| T0_SAMPLER(sampler)` | macro expands the same fields |
| 1 | address reg type << 24, nr << 17 | `T1_ADDRESS_REG(coord)` | same |
| 2 | zero | `T2_MBZ` | `OUT_BATCH(0)` |

`T0_TEXLD` is `0x15 << 24`. Shifts 19, 14, 0, 24 and 17 are identical in both
headers and used identically by both emitters.

Declarations are three dwords too: `D0_DCL` `(0x19 << 24)` with the register
type at shift 19 and number at 14, plus `D0_CHANNEL_ALL` `(0xf << 10)` for
everything **except** sampler registers, then two zero dwords. xf86 declares
`T0` and `S0` before sampling from them; that conditional on the channel mask
is explicit in its macro.

Register types: `REG_TYPE_T` 1 (interpolated), `REG_TYPE_S` 3 (sampler),
`REG_TYPE_OC` 4 (output colour).

The program Phase 6 needs is therefore: declare T0, declare S0, `texld R0 <- S0
using T0`, move R0 to OC. Four instructions, twelve dwords, plus the header —
against the current two instructions and six.

## 9. What is NOT established

- **The texture address alignment.** Neither tree states one; page alignment
  is a choice, not a finding.
- **Whether `MS4_CUBE_FACE_ENA_MASK` is required.** The trees disagree and
  xf86's minimal form is a recorded judgement, not a corroborated value.
- **Anything about the sampler's behaviour**, as opposed to its encoding. This
  audit says what the packets mean, not what the silicon does with them, and
  nothing here has been near the hardware.
- **Non-power-of-two textures, mip levels, tiling.** All out of scope; the
  texture is one linear power-of-two 2D surface with one level.

## 10. Consequence for the plan

`docs/plans/intel-phase6-texture-scene.md` listed six unaudited items. Four are
now established and two removed outright:

| Item | Status |
|---|---|
| `_3DSTATE_MAP_STATE` | Established, §4 |
| `_3DSTATE_SAMPLER_STATE` | Established, §5 |
| `LOAD_INDIRECT` with a real payload | **Not needed** — both trees emit map and sampler state directly |
| S2 texture coordinate format | Established, §6 |
| S4 with UV | **No change** — S4 has no texcoord field |
| `texld` | Established, §8 |

One divergence and one alignment question remain, both recorded above as
judgements rather than findings.
