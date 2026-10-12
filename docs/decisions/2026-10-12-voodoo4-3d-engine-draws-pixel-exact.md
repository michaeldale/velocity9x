# The Voodoo4's 3D engine draws what the builder says: flat, Gouraud, depth, blend, texture

Date: 2026-10-12. Machine: A8U4I5, boot 428. Card: Voodoo4 4500 PCI on
Velocity9x (desktop at 15 MiB). Tool: `TDFX3D.EXE` + `TDFX3D.VXD`
(`scripts\build-tdfx-3d.ps1`), build `05d9622-dirty`, with every register
write from `src\chipsets\tdfx\tdfx_3d.c`. Evidence:
[a8u4i5-voodoo4-3d-2026-10-12](../probe/a8u4i5-voodoo4-3d-2026-10-12/).
Plan: [3dfx-vsa100-d3d.md](../plans/3dfx-vsa100-d3d.md), step 2. Michael
watched the monitor.

## Method

**The setup.**
- **Surfaces:** a 64x32 RGB565 target at 24 MiB, a Z16 buffer one page
  above it (opposite bit-12 parity), and a 32x32 texture at 24 MiB +
  16 KiB, all in DirectDraw's heap.
- **Registers:** direct writes to BAR0 + 200000h; TMU0 through chip field
  +800h.
- **The setup unit:** fed X, Y, four colour floats, then Z, then 1/W and
  S/W, T/W as the state needed.
- **FIFO room:** before every run of 16 writes, the probe waited for 16
  free PCI FIFO entries.
- **Flush:** `nopCMD`, then idle, before and after each draw.

**Judging.** An integer model decides every pixel:
- a pixel whose centre is at least a pixel inside every edge must be drawn;
- one at least a pixel outside some edge must not be;
- the band between is counted but not judged.

## Measured

| Test | Inside pixels | Wrong | Worst step | Outside drawn | Result |
|---|---|---|---|---|---|
| Flat (red) | 429 | 0 | 0 | 0 | PASS |
| Gouraud (red, green, blue corners) | 429 | 0 | 1 | 0 | PASS |
| Depth (Z16, LESS; 40000, 20000, 60000) | all 2048 green | 0 | 0 | — | PASS |
| Blend (red at alpha 128 over blue) | all 2048 | 0 | 0 | — | PASS |
| Texture (32x32 565, replace, point) | 1024 texels | 0 | — | 0 | PASS |

- **Edges:** 135 of the 288 edge-band pixels were drawn, in both triangle
  tests.
- **Idle:** status was `5Fh` before and after the run, and every idle wait
  finished in 6-63 reads.
- **FIFO:** none of the 31 FIFO-room waits timed out.

## What this settles

1. **Direct writes to the setup unit draw.** The builder's state and
   triangle words, in its order, produce the modelled image. Colour as four
   separate floats works; packed `sARGB` was not tried (D37 stays open for
   it).
2. **Z is in depth counts.** A vertex Z of 20000.0 wrote `4E20h`, exactly,
   to every Z16 pixel, and the LESS test kept the nearer draw.
3. **S and T count 256ths of the texture's width.** With 8 per pixel on a
   32-texel texture, every texel landed on its pixel. That is what Glide's
   0-256 range implied.
4. **Blending with source alpha and its inverse is exact** at 16 bpp. The
   result `800Fh` is red 16 and blue 15, as computed.
5. **Gouraud colour is within one step** of the barycentric model; the
   model truncates, and the chip may round.
6. **Linear colour and depth buffers work at either page parity.** A
   follow-up run (`TDFX3D /parity`, same boot) put the Z16 buffer two pages
   above the target, on its own bit-12 parity. Everything came out the same
   as above: all 2048 pixels green, Z `4E20h` everywhere, idle after. So the
   retro-3dfx parity rule (D36) does not bind linear buffers, and the
   builder no longer refuses them (`TDFX3D-parity.TXT`).

## Not established

- **The fill rule** for edge pixels. Counted, not judged.
- **Packed colour** under direct writes (D37), and tiled buffers at either
  parity.
- **Bilinear filtering, modulate, clamp, perspective with W ≠ 1, 1555 and
  4444 textures, and Z functions other than LESS.** The builder emits all
  of these; none ran.
- **Whether the PCI FIFO waits are needed.** None timed out; whether PCI
  retry alone would have been safe was not tested.
