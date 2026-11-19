# Evergreen class takes XRGB8888 textures: V9XD32 T22X black to exact on Trinity

Date: 2026-11-19. Machine: M-D7TFW4 (W9QI6DFC), Radeon HD 7660D
(Trinity, `1002:9901`), remote help. Follows the
[0.56.0 baseline](2026-11-19-trinity-0.56.0-baseline.md) on the same
machine. Evidence:
[`../probe/trinity-d7tfw4-probes-2026-11-19/`](../probe/trinity-d7tfw4-probes-2026-11-19/)
(before) and
[`../probe/trinity-d7tfw4-xrgb-2026-11-19/`](../probe/trinity-d7tfw4-xrgb-2026-11-19/)
(after), each with the runner's record.

## Before (boot 74, `3ad8f652-dirty`, the 0.56.1 tree)

The release probes by `run-release-checks.ps1 -Machine trinity`, the first
on this chip since 0.46.0: V9XGLP, V9XGLIDP (failures 0), V9XR3DP,
V9XWPRS and V9XFBCMP pass. V9XD32 fails one check at 32 and at 16 bpp:
`T22XOk=0`, all four pixels `00000000`, while the ARGB8888 quad beside it
(`T22AOk=0F`) is exact. T22 was added on 2026-11-11, after Trinity's last
V9XD32 run, so this is its first run on an Evergreen-class chip, not a
regression. `T20ModAlphaOk=1`: the DX5 MODULATE texel-alpha fix
(`148af927`), host-tested only on this class until now, holds.

The texture was created (`T22XCreateHr=0`) and its draw returned 0: the
HAL took the surface and the draw, then refused it at the map, because
`v9x_d3d_eg_format_of` knew no XRGB8888. An application that makes its
textures in a 32 bpp desktop's format would get black surfaces with no
error. The R600 class had the same gap and closed it on 2026-11-13
(`d3d_r6.c`, M-NZHWBR boot 7).

## Change

The R600 class's fix, in Evergreen's terms:

- `d3d_eg.c`: XRGB8888 is a texture format, sampled as 8_8_8_8 with
  `has_alpha` 0, and the fifth entry of the published texture formats.
- `eg_3d.h` / `d3d_eg_map.c`: `texture_opaque`, set for a texture without
  alpha.
- `eg_3d.c`: an opaque texture's resource takes the XYZ1 swizzle (W reads
  the constant 1), as a 565 texture's already did.

Host tests first, seen failing to compile against the old state struct:
the map sets `texture_opaque` for a texture without alpha and clears it
for one with; an opaque 8888 resource has DST_SEL_W 5. `run-checks.ps1`
passed.

## After (boot 75)

| | 32 bpp | 16 bpp |
|---|---|---|
| T22X pixels | 002040C0 00C08020 0010E070 0090A0F0 | 2217 BC04 16EE 8D1D |
| `T22XOk` | 0F | 0F |
| V9XD32 | **PASS**, failures 0 | **PASS**, failures 0 |

The 32 bpp pixels equal T22A's. V9XGLP, V9XGLIDP, V9XR3DP, V9XWPRS and
V9XFBCMP pass as before. Eight of eight files hash-matched after the
reset.

## Not covered

- Cedar and Caicos, the other Evergreen rows: same code, no machine
  online.
- A game that uses XRGB8888 textures: none identified or run.
- The screen: not looked at.
