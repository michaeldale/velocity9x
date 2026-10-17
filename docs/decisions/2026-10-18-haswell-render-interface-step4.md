# HD 4600 render interface, step 4: two texture units; Half-Life OpenGL 61.3 to 67.7 fps

Date: 2026-10-18. Machine: the i5-4570 (`8086:0412`), boots 23 (two
units) and 24 (the same source with `texture_units` 1), the HAL replaced
by WININIT rename each time. Step 4 of
[intel-haswell-glide-3dcif-opengl.md](../plans/intel-haswell-glide-3dcif-opengl.md).
Evidence:
[`../probe/hsw-4570-render-interface-step4-2026-10-18/`](../probe/hsw-4570-render-interface-step4-2026-10-18/).

## What changed

- **Combine mapping** (`v9x_d3d_hsw_two_unit_stages`, `d3d_hsw_map.c`,
  host-tested):
  - Unit 0's `color_op`/`alpha_op` go to one of the generator's stage 0s:
    REPLACE/REPLACE is TEXTURE, REPLACE/FRAGMENT is DECAL,
    MODULATE/MODULATE is MODULATE, MODULATE/REPLACE is MODULATE_TEXALPHA.
  - Without texture alpha the sampler returns 1, so alpha MODULATE and
    FRAGMENT are folded together for that unit. REPLACE and MODULATE stay
    apart: they agree only when the fragment's alpha is 1, which is not
    known.
  - Unit 1 must be MODULATE/MODULATE (`MODULATE_TEX1`, the one second
    stage Phase E measured). Anything else is refused with the new reason
    10, `V9X_D3D_HSW_REFUSE_COMBINE`, and the ICD splits the draw into two
    passes.
- **Second sampler:** the kernel's TEX1 reads binding 2 with sampler 1
  (`hsw_eu.c`). The draw writes sampler 1 16 bytes after sampler 0, at
  `350h`, before the border colour at `360h`.
- **Vertex:** a two-unit draw's vertex is 40 bytes (`tl_uv1`): the 32-byte
  TL vertex, then `texcoords1`'s tu1 tv1. The copy interleaves them, and
  the VERTEX_ELEMENT for the second coordinate reads offset 32. A batch
  then holds 477 triangles instead of 597.
- **Kernel slot:** stage 1 is part of the slot (`stage0*8 + stage1*4 +
  specular*2 + fog`), 48 distinct slots of the 64.
- **`texture_units` 2.** Only render-interface draws carry `texcoords1`;
  Direct3D's caps are unchanged.
- **Host tests** failed first, then passed: the mapping, the sampler-1
  message descriptor, the paired probe samplers, and the 40-byte VE/VB
  words.
- **Gates:** run-checks green.

## Measured

**V9XGLP** (boot 23): `Extensions=GL_SGIS_multitexture`, 30 Ok keys at
1, none at 0. SGIS was not advertised at boot 20.

| check | read | Ok |
|---|---|---|
| `SgisModulate` | `636510` | 1 |
| `SgisReplace` | `84FF42` | 1 |
| `SgisBlend` | `630063` | 1 |
| `SgisDecal` | `A5B639` | 1 |
| `SgisCoordLeft` / `Right` | `000000` / `C66531` | 1, 1 |
| `SgisUnit1Off` | `C66531` | 1 |

**Who drew them** (`V9XGL-probes-boot23.LOG`, the probe's last `paths`
line):
- `two-units=36/72`: 36 batches went to the engine as one pass.
- `hw-refused=3/6` and `split=3/6`: the three draws whose unit 1 is
  REPLACE, BLEND or DECAL (`tex1=…/02010101`, `02010400`, `02040300`)
  were refused as combine and drawn as two passes. Their checks pass all
  the same.
- `cpu=0/0`.

The `SgisMip*` keys read `L0` everywhere, as at boot 20: the engine
samples level 0 only.

**V9XR3DP:** identical to boot 20, line for line.

## Half-Life `-gl` `timedemo mwd5`, 640x480 full screen

Three runs each, in one Half-Life session. The first run of a session is
cold, as before.

| HAL | boot | runs (fps) | two-unit batches |
|---|---|---|---|
| `texture_units` 1 | 24 | 52.6, **61.3**, 61.1 | 0 |
| `texture_units` 2 | 23 | 57.2, **67.7**, 67.4 | 91,724 (of 369,989) |
| `texture_units` 2, `-nomtex` | 23 | 65.4, **67.6**, 67.3 | still used |

- **Single-pass lightmaps: +10%,** 61.3 to 67.7 on the same source. The
  step 2 figure of 56.9 is no baseline: it predates c960e9ed, which
  built the kernel in cached memory.
- **Half-Life ignores `-nomtex`.** Its log still counts two-unit batches,
  so that row is a third two-unit session, not a one-unit one. The
  one-unit row came from rebuilding with `texture_units` 1.
- **No refusals** in either session (`hw-refused=0/0`, `cpu=0/0`).
- **The frame drew textured and lit** (`hl-gl-mwd5-frame-two-units.png`).
  This was checked from an agent screenshot, not by eye on the monitor.
- **Netbook comparison:** 16.8 fps on the GMA 950. That was not re-run.

**Bucket split** (`buckets.ps1`, for the three runs plus the menu):

| bucket | one unit (µs/call) | two units (µs/call) |
|---|---|---|
| EngineDraw | 129.65 (219,049 calls) | 109.82 (253,257 calls) |
| RenderDrain | 27.21 | 21.42 |
| RingWrite (vertex copy) | 53.77 | 44.15 |
| RingWait (state write) | 5.25 | 6.22 |
| Decode (kernel build) | 3.15 | 3.79 |

The two-unit session made more engine draws, each cheaper; the total
engine time is about the same (28.4 s and 27.8 s). Why the draw count
went up is not established. The fps gain is measured; the mechanism
behind it is not.

## Not covered

- **Mipmaps:** level 0 only, as before.
- **Unit-1 combines other than MODULATE:** these take two passes.
- **Quake 2:** not on the 4570 (step 3).
