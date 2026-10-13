# The Voodoo4's Direct3D engine passes V9XDDP's core on its first run

Date: 2026-10-12. Machine: A8U4I5, boot 429. Card: Voodoo4 4500 PCI on
Velocity9x, desktop at 15 MiB. Build: `vsa100-tier0` on `0b30e9f` plus this
commit (DRV, mini-VDD, HAL). Evidence:
[a8u4i5-voodoo4-d3d-2026-10-12](../probe/a8u4i5-voodoo4-d3d-2026-10-12/).
Plan: [3dfx-vsa100-d3d.md](../plans/3dfx-vsa100-d3d.md), step 3. Michael
watched the monitor; nothing looked wrong, and the desktop was normal after.

## What was built

- **`d3d_tdfx_map.c` (pure):** turns the neutral draw into `tdfx_3d.c`
  builder state, or refuses it with a counted reason. It also converts each
  triangle to the builder's units:
  - colour channels 0-255, with vertex 0's colour throughout when flat;
  - Z as sz × 65535;
  - 1/W as RHW;
  - S/W and T/W as 256ths of the texture's width times RHW.

  Its 107 host checks were written against a stub first.
- **`d3d_tdfx.c`:** places textures packed and 16-byte aligned, and maps
  surface offsets by the desktop's offset. Each batch is written after an
  idle wait, in runs of 16 writes behind PCI FIFO room. A bounded timeout
  quarantines the engine.
- **The 16-bit side:** the 2D window grew to cover the 3D registers
  (101000h bytes from BAR0 + 100000h). `vsa100_hw16.c` stamps `CAP_D3D`
  and `hardware-vsa100`, and the manifest says so.
- **The builder:** separate min and mag filters; a "modulate colour, texel
  alpha" combine for Direct3D's MODULATE on alpha textures; `texBaseAddr`
  written twice, a different base first, so a CPU download reaches the
  texture cache (section 20.3).

## Measured (V9XDDP, `V9XDD.INI` and `V9XDD2.INI`)

| Area | Result |
|---|---|
| Device | Direct3D HAL enumerated as hardware; three texture formats (565, 1555, 4444) |
| Triangles | pixel, all six vertex orders, other half, 12 of 12 shapes, sub-pixel, edges: OK |
| Vertex alpha blend | OK (`800Fh`) |
| Textures | base, tiled and negative coordinates, 4444; sizes 2-256 with their pitches; modulate, bilinear, decal, clamp; LOD 64/256 min and mag; perspective; alpha 4444/1555 and the alpha curves: OK |
| Texture matrix (`DD2`) | 64-256 × 1555/4444 × plain/chain/gapped × near/lin/mipnear/trilin/alpha/wrap: Ok=1 throughout the run |
| Depth | Z16 compare ladder, write mask, depth fill: OK |
| Colour key | `ColorKeyOk=1`, the draw refused |
| Not passed | fog, specular, DECALALPHA (refused); mip chain A-E, mip ladder, trilinear blend, mip level select (one level only); `FlipPixelOk=0` (no hardware flip) |

HAL snapshot after the run (HAL `0.16.1 0b30e9f-dirty`):
- engine flags 9 (valid), no idle timeouts;
- 184 textures placed (6.4 MB);
- refusals by reason: fog 4, specular 2, colour key 1, texture format 1,
  texture op 2 (DECALALPHA).

`V9XGDI /auto` PASS after.

## What this settles

The setup-unit path serves Direct3D's core on the 4500 at the first
attempt. The refusals are the ones the map was written to make, and no
engine wait timed out.

## Not established

- **The control.** The same run under 3dfx's own driver was not done.
- **Half-Life,** the gate game.
- **The tie rule.** Edge pixels pass V9XDDP's shape tests without one
  being applied.
- **Mipmaps, fog, specular, DECALALPHA, mirror, and 32-bit colour.** All
  later steps.
