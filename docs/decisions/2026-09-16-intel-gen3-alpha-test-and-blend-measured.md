# The GMA 950 rejects by alpha and blends by it, and the arithmetic is exact

**Machine**: MICHAEL-NETBOOK, Intel GMA 950 on 945GSE (8086:27AE), Windows 98 SE.
**Build**: `3d66dc7-dirty`, armed `p6-20260916-3d66dc7-dirty`, Phase 6.
**Capture**: `C:\temp\intel47` — `INTEL3D0.TXT`, schema 3, `Result=PASS`.
**Date**: 2026-09-16.

Five scenes, five draws, all 37 probes as predicted. This completes Phase 6's
feature list.

**On the `-dirty` build id**: the arm record carries `IntelArmCrc=780CC7E8` and
`IntelArmSceneCrc=7C7A3337`, which are byte for byte what the tree at `417e0c6`
generates. The working-tree changes the marker refers to were in the decoder and
its tests, which do not enter the stream. What ran is what the committed
builders produce.

## 1. Alpha test works, and the top byte is the alpha

Three triangles, one primitive, `COMPAREFUNC_GREATER` against `0x80`. The first
carries alpha `0xFF`, the second `0x40`, the third `0xC0`.

| Probe | Covered by | Read | Meaning |
|---|---|---|---|
| `AlpA` | A | `1C3E` A | A passed |
| `AlpAB0` | A, B | `1C3E` **A** | **B rejected** |
| `AlpAB1` | A, B | `1C3E` **A** | the same, a different row |
| `AlpBC` | B, C | `3038` C | C passed |
| `AlpC` | C | `3038` C | C ran |
| `AlpOut` | none | `0842` fill | |

The two `AlpAB` probes are the measurement. **B is drawn after A**, so without a
working alpha test it would hold those pixels; it holds none. Its colour
(`F325`) appears nowhere in the capture.

**Two results in one picture.** The alpha test rejects fragments, and the alpha
it tests is the **top byte of the vertex diffuse dword**. That byte position was
Mesa-sourced only for the vertex path - xf86's Gen3 render path emits no
per-vertex diffuse at all - and the other three bytes were already measured by
elimination across `intel41`-`intel46`. Had the top byte not been the alpha, all
three triangles would have drawn and both `AlpAB` probes would read `F325`.

**What a probe cannot say, stated plainly**: no probe distinguishes "B was
rejected" from "B never ran", because both leave the same pixels. What does is
that B is bracketed by two triangles that did draw, inside a single
`_3DPRIMITIVE` whose vertex count the decoder pins at nine.

## 2. Source-alpha blend works, and the arithmetic matches exactly

An opaque triangle drawn first, then a half-alpha one (`0x80`) over it.
`BLENDFUNC_ADD`, `SRC_ALPHA`, `INV_SRC_ALPHA`.

| Probe | Background | Read |
|---|---|---|
| `BlnUnder` | - | `1C3E`, the opaque triangle unblended |
| `BlnOver0` | the opaque triangle | `8BB1` |
| `BlnOver1` | the same, a different row | `8BB1` |
| `BlnFill` | the fill | `81A3` |
| `BlnOut` | - | `0842` fill |

The products were **reported, not required** - both the blend arithmetic and the
565 conversion were predictions. They match the obvious model exactly, on **six
channels across two different backgrounds**:

```
result = src * srcA + dst * (1 - srcA),  srcA = 0x80/255
dst expanded from 565 by bit replication, result converted back by round-to-nearest
```

| | src | dst | exact | stored | model |
|---|---|---|---|---|---|
| over `1C3E` | 248, 100, 40 | 24, 134, 247 | 16.58, 28.88, 17.40 | 17, 29, 17 | 17, 29, 17 |
| over `0842` | 248, 100, 40 | 8, 8, 16 | 15.62, 13.39, 3.41 | 16, 13, 3 | 16, 13, 3 |

Two backgrounds matter: a single coincidence cannot produce both, and the
fill-backed one exercises a destination that no triangle wrote.

This is also the same expansion-and-rounding model the modulate products fit in
`intel46`, now confirmed on a different operation. Together they are twenty-four
channels at values outside the originally measured set, all consistent with
bit-replicated expansion and round-to-nearest, with truncation excluded at
several.

## 3. `_3DSTATE_INDEPENDENT_ALPHA_BLEND` was emitted, and nothing objected

The blend scene carries the disable dword `0x6BA008A1` - the packet this driver
had never emitted, found by the audit in xf86's invariant block and Mesa's blend
state, and therefore in neither tree's half of the intersection this driver took
as its floor.

The draw produced the predicted colours and the parser rejected nothing
(`PostErrOk=1` with nine registers on all five scenes). That is consistent with
the packet being accepted and the alpha channel not being independently blended;
it is **not** a measurement of what would have happened without it, because no
scene omits it.

## 4. The rest of the table

- **Scene 0**, the Phase 5 regression: seven interior `1C3E`, seven exterior
  `0842`. Unchanged across six captures.
- **Scene 1**, modulate: `0A2F`, `79A3`, `180C`, `0400` - identical to
  `intel46`, so the retirement of the sampling-only scene cost nothing this
  boot could detect.
- **Scene 2**, depth write: the furthest triangle rejected where the nearer two
  wrote, present where it alone covers. Identical to `intel46`.
- **Guards**: `GLow`/`GUpp` unchanged on all five scenes; `TexG` and `DepG`
  unchanged against their pre-run readings.

## 5. The read budget, predicted and confirmed on one side

| | Reads |
|---|---|
| Phase 4 replay | 1070 |
| Mini-VDD, staging and verify | 956 |
| Driver, probes and guards | 55 |
| **Expected total** | **2047** |

`DriverApertureReads` read back **55**, exactly the predicted driver term - the
second boot running where that prediction has been exact. The mini-VDD side is
not independently counted, so the total remains confirmed on one side only.

2047 against 2126 on `intel46`: slightly fewer, and no hang. The aperture-read
hazard has now been cleared at this magnitude twice.

## 6. What Phase 6 did not answer

Carried forward rather than closed:

- **The depth scale.** Neither reference tree states how a post-transform Z maps
  to the 16-bit format, and both depth scenes were built to depend only on
  ordering. Unmeasured.
- **Destination alpha on a 565 target.** Excluded from the blend by using
  source-side factors only; still unsourced.
- **The 565 conversion as a single rule.** Twenty-four channels now fit one
  model, but they come from the shader-output path. Whether that is the same
  backend as the vertex-colour path remains an assumption.
- **Alpha test's use is single-sourced.** Only Mesa enables it in either tree.
  This boot shows the field placement is right on this part, which is stronger
  than a second header would have been - but it is one part.
- **Filtering, wrapping, tiling, non-power-of-two textures, texture formats
  with alpha, blend functions other than ADD.** None licensed, none exercised.
