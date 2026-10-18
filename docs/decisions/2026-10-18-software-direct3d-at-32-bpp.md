# The software Direct3D engine renders at 32 bpp; hardware engines still refuse it

2026-10-18. Phase 2 of
[`../plans/32bpp-acceleration-and-3d.md`](../plans/32bpp-acceleration-and-3d.md).
Machines: the 86Box Trio64 guest (host port 9871, 4 MiB, `Direct3D=2`,
software engine), and MICHAEL-NETBOOK (GMA 950, hardware engine) as the
regression control. Evidence:
[`../probe/software-d3d-32bpp-2026-10-18/`](../probe/software-d3d-32bpp-2026-10-18/).

## The change

Until now every Direct3D engine, the software one included, rendered only
to a 16 bpp target, and a 32 bpp mode got no HAL device at all.

- **The rasterizer** (`d3d_raster.c`) writes and blends an XRGB8888 target:
  eight bits a channel, the X byte cleared by a full store and kept by a
  masked one. Depth stays 16-bit; the rasterizer interpolates it in 16.8,
  and a 24-bit software depth buffer is not built.
- **The render-interface numbering** gains XRGB8888 as format 5 (3 and 4
  are texture layouts in the same space). The validators measure an 8888
  surface at four bytes a pixel, and the CPU clear packs it.
- **The core** (`d3d_core.c`) classifies a 32-bit X8R8G8B8 pixel format and
  asks the engine whether it draws that layout. An engine opts in with a new,
  appended limits field, `target_pitch_max_xrgb8888`. Zero, which every
  hardware engine has, means 16 bpp only, as before. A target's pixel size
  must equal the desktop's, so the clear fills and engine setup that take
  their width from the mode cannot disagree with the target.
- **The software engine** opts in (pitch up to 1FF0h, the same widest row as
  its 16 bpp limit) and reports render depths DDBD_16 | DDBD_32, Z DDBD_16.

Host tests: rasterizer (packing, exact flat colour, pitch, blend reading the
destination, colour mask), render-interface clear and validator at 8888.
The rasterizer and clear tests failed before the change (the rasterizer's
did not compile; five clear checks failed). The validator tests were
written after the validator change and were not seen failing.

## The instrument

`V9XD32.EXE` (new, `tools\diag\d3d32_probe_win32.c`): exclusive 640x480x32
(or `/bpp16`, the control), the IID_HAL device on the flip chain's back
buffer with a 16-bit Z buffer, then T1 a flat triangle (every pixel at
least a pixel inside is the colour, every pixel two pixels outside is
zero), T2 a Gouraud triangle (each corner's own channel at least 200, the
others at most 60), T3 depth (a quad at 0.5, a triangle behind it, one in
front), T4 a flat triangle into a 64x64 offscreen target. At 32 bpp the
colour compare is exact and the X byte must be zero; at 16 bpp each
channel is compared within 8 after expansion. V9XDDP could not be used: it
sets 640x480x16 and reads 16-bit pixels throughout.

## Measured

| Machine, engine | Mode | HAL render depths | T1 (3861 pixels judged) | T2 | T3 | T4 | Result |
|---|---|---|---|---|---|---|---|
| Trio64 guest, software | 640x480x16 | `0500h` (16, 32) | 0 mismatches, `12B3` | 3 of 3 | 3 of 3 | 0 mismatches | PASS |
| Trio64 guest, software | 640x480x32 | `0500h` | 0 mismatches, `0013579B` | 3 of 3, centre `00555555` | 3 of 3 | 0, `002468AC` | PASS |
| Netbook, Gen3 hardware | 640x480x16 | `0400h` (16) | 0 mismatches | 3 of 3 | 3 of 3 | 0 mismatches | PASS |
| Netbook, Gen3 hardware | 640x480x32 | `0400h` | — | — | — | — | `NO-HAL-AT-DEPTH` |

The Trio64 snapshot after both runs: `CountD3dContextCreate=4`,
`D3dContextRejects=0`, draws counted, DRV and HAL builds agree. That is our
HAL drawing, not Microsoft's emulation. The netbook snapshot: 2 contexts,
`I9xxDrawsSubmitted=6`, no breadcrumb or idle timeouts. The 32 bpp run
stopped at enumeration because Gen3 does not offer the depth.

## Findings

- **The software engine renders Direct3D at 32 bpp**, exactly for flat
  colour, with depth ordering, to a flip chain's back buffer and to an
  offscreen target.
- **A hardware engine's 16 bpp path is unchanged**, and it does not offer
  32 bpp, so a title at 32 bpp on a hardware-mode machine still gets no HAL
  device, as before.

## Not measured, and owed

- A real title at 32 bpp on the software engine, and its speed.
- Clear2 (DDI 6) at 32 bpp: V9XD32 drives the DX5 interface.
- **Whether a hardware-mode machine should fall back to the software engine
  at 32 bpp.** The plan's Phase 2 said it would. It is not done here:
  it would change which renderer a 32 bpp title gets on every
  hardware-mode machine (ours instead of Microsoft's RGB emulation), and
  that is Michael's decision.
- The OpenGL ICD, Glide and 3DCIF at 32 bpp. The render interface now
  publishes format 5 when the software engine runs at 32 bpp; the three
  front ends do not know it yet and decline, as they did before.
