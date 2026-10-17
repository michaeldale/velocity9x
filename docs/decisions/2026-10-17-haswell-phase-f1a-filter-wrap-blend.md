# Haswell Phase F1a: bilinear, wrap, mirror and alpha blend match; the VF cache outlives a batch

2026-10-17, i5-4570 (`8086:0412` rev 06), Windows 98 SE, boot 9, with the
B2 driver on the desktop. `V9XHSWMM.EXE /shade:4` and `/shade:5`, on
Phase E's layout and generator, part of Phase F in
`docs\plans\intel-haswell-direct3d.md`. Evidence:
`docs\probe\hsw-4570-state-f1a-2026-10-17\`.

## The state

Three more SAMPLER_STATEs beside E's nearest/CLAMP one: LINEAR/LINEAR
with CLAMP, and NEAREST with WRAP (0) and with MIRROR (1) on all three
axes (Vol 2a Texture Coordinate Mode). There is also a BLEND_STATE with
blend enabled, SRC_ALPHA (`03h`) and INV_SRC_ALPHA (`13h`), and ADD (Vol
2a), whose alpha uses the colour factors. The wrap and mirror runs use
texture coordinates 0-2. The blend run blends the texture over the target's
guard (`A5h` in every channel).

## First run: wrap and mirror wrong, and not because of the sampler

Wrap had 1021 and mirror 1013 mismatches out of 1036 judged. Their pixels
equalled the CLAMP run's at coordinates 0-1, as if the vertices still
held 1.0. A rerun that recorded the vertex buffer as built showed 2.0
(`40000000`) at both places. The batch prefix's PIPE_CONTROL invalidated
the state, constant, texture and instruction caches, but not the VF
cache. The vertex buffer sits at the same graphics address in every run,
so the vertex fetcher returned the previous run's vertices: the cache
survived the batch end, the ring teardown and the next ring setup.
`VF_CACHE_INVALIDATE` (bit 4) was added to that PIPE_CONTROL.

## After the fix: all twelve variants pass

| Variant | Inside judged | Not judged | Mismatches | Max error |
|---|---|---|---|---|
| wrap (nearest, 0-2) | 1036 | 133 | 0 | 0 |
| mirror (nearest, 0-2) | 1036 | 133 | 0 | 0 |
| bilinear (CLAMP) | 1103 | 66 | 0 | 1 |
| alpha blend over the guard | 1032 | 137 | 0 | 0 |

Phase E's eight variants were rerun on the changed batch and all pass
again (`*-vf-invalidate.INI`). The bilinear reference samples at
coordinate x 8 - 0.5, with 8-bit weights and edge clamping; the hardware
agrees with it within 1/255.

## Findings

- Bilinear filtering, WRAP and MIRROR addressing, and fixed-function
  alpha blending work as encoded.
- **The VF cache must be invalidated** by any batch whose vertices may
  have changed at an address used before. The HAL's draw path needs the
  same, or a fresh vertex address per draw.
- Phase E's results came from the same vertex data every time, so the
  stale cache could not have changed them; they pass again after the fix.

## Not measured

- Depth (F1b, next), mipmaps, LOD selection, anisotropic filtering, other
  texture formats, blend factors other than these two.
- Bilinear at texel-grid points away from the reference's 8-bit weight
  rounding (the tolerance is 2).

## Next

F1b, depth: a depth buffer, depth test and write across several
primitives. Then F2, the `V9X_D3D_ENGINE_OPS` table.
