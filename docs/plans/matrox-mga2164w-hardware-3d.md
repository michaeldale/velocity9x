# Matrox MGA-2164W: hardware Direct3D

Date: 2026-10-10

Status: approved 2026-10-10 as written, with two answers to the decisions
below. The reference driver is Matrox's own, served on the LAN at
`http://10.0.0.7:8081/simple/retro_web_download_file/306/` (A8U4I5 can
download it directly). Alpha blending: Michael wants to see both options,
the stipple first, then the software refusal, compared on the same scenes.

Target: the Millennium II MGA-2164W (102B:051B, 4 MiB) in A8U4I5, on the
`matrox` family with DirectDraw and GDI on the drawing engine
([first boot](../decisions/2026-10-10-mga2164w-first-boot.md)). Follows
[matrox-millennium-family.md](matrox-millennium-family.md), "Later".

**The MGA-2064W (Millennium) gets no hardware Direct3D.** Michael,
2026-10-10: the Millennium I does not have it. Its Gouraud/Z trapezoids
exist, but an untextured device is not worth exposing, and it stays on the
software rasteriser. Everything below is gated on the 2164W's device id in
its own hw16 hook, inside the one `matrox` family.

## What the chip is believed to do

Third-party descriptions, not measurements
([vintage3d](https://vintage3d.org/mga1.php), dosdays, VGA Legacy): the
2164W has the Mystique's texture engine on the Millennium's core.

- Perspective-correct, **point-sampled** textures: no bilinear filter, no
  mipmaps.
- **No alpha blending, no fog, no specular.** Translucency is a 4x4
  screen-door stipple (DWGCTL.trans), and texel alpha stipples.
- Gouraud shading, 16- or 32-bit Z.
- Texel formats 4/8-bit palettised, 5:5:5, 5:6:5 and 4:4:4:4, in local
  memory only.

This is a 1997 DirectX 3-class part. The device it can honestly publish
is narrow, and most of what applications ask for beyond it must be
refused per batch and drawn in software.

## What the documents give

[mga2164w-3d-engine.md](../specifications/mga2164w-3d-engine.md) holds
the extraction. Matrox's public 2164W and 1064SG specifications are
redacted editions:

- **Documented:** TRAP with atype ZI (Gouraud, Z written) and I (Z
  compared, not written); the zmodes; DR0-DR15 interpolants (17.15 Z,
  9.15 colour); 32-bit Z through MACCESS.zwidth and the DRn_Z32 registers
  at 2C50h-2C6Ch; ZORG; the trans stipple patterns; the Bresenham edge
  registers; BAR0 framebuffer and BAR1 control aperture on this chip;
  pseudo-DMA.
- **Cut out:** every texture register (the 2C00h-2C34h block), the
  texture opcode (0110 is missing from the opcode table), the texture
  palette layout, and bus mastering.
- **Hypotheses for the cut parts** come from 86Box's emulation (section 10
  of the extraction) and recollection of XFree86's `mga_reg.h`. Both
  agree on the offsets. Neither is evidence about the silicon, and 86Box
  textures on its 2064W too.

So the texture interface has to be measured before any of it is coded,
which is the probe-first shape SiS and the Rage IIC already use.

## Shape

- `src/chipsets/matrox/mga_3d.c`, host-tested: triangle to two
  trapezoids (edge Bresenham terms, SGN, FX, YDST/LEN), interpolant
  gradients and prestep (Z, RGB, s/t/q), the DWGCTL/MACCESS/TEXCTL words.
  CPU edge setup, as `rage2_trap.c` does for the Rage IIC; the chip has no
  setup engine.
- A write probe, `MGA3D.EXE`, on the `MGA2D` VxD pattern: draws into a
  guarded off-screen target, reads back, compares against the builder's
  expected pixels. The VxD learns the 2164W's BAR order and the DWGREG1
  range (2C00h-2DFFh); MGA2D today accepts only 0519 with BAR0 control.
- `src/display32/d3d/d3d_mga.c` on the neutral `draw` entry
  (`V9X_D3D_ENGINE_OPS`), with refusal codes in a host-tested map
  (`d3d_mga_map.c`), like `d3d_sis6326_map.c`.
- The 16-bit side: `millennium_hw16.c`'s engine hook stamps `CAP_D3D` for
  051B only, once the probe phases pass. The family manifest's chip entry
  carries `Direct3D='hardware-mga2164w'` and the check-tree contract that
  goes with it.

## Phases

0. **The whole memory.** Done 2026-10-10
   ([record](../decisions/2026-10-10-mga2164w-memory-walk.md)): the family
   walks its memory after mapping and the 2164W runs with 8 MiB, its
   BIOS's 4 notwithstanding
   ([issue](../issues/2026-10-10-mga2164w-vbe-reports-half-its-memory.md)).
   Matrox's HAL is the reference
   ([baseline](../decisions/2026-10-10-mga2164w-matrox-hal-baseline.md)).
1. **Untextured triangle (probe).** Done 2026-10-10
   ([record](../decisions/2026-10-10-mga2164w-trapezoids.md)): flat and
   Gouraud trapezoids match the specification's edge terms pixel for
   pixel; the DR y registers are a pure d/dy and the engine folds the
   left edge's steps in itself; levels truncate; no sub-pixel input exists.
   The triangle-to-trapezoid mapping under Direct3D's rules moves to
   phase 4. As planned: 16 bpp, off-screen. One flat
   triangle, then Gouraud. Settles: the fill rule against Direct3D's
   top-left convention, how a triangle splits into trapezoids and which
   edge is reloaded, whether DRn y-increments are pure d/dy or along the
   left edge, sub-pixel handling (the registers are integers), the GO
   alias, dither off.
2. **Depth (probe).** Done 2026-10-10
   ([record](../decisions/2026-10-10-mga2164w-depth.md)): 16- and 32-bit Z
   as specified - ZORG, the Z pitch, the six compares (unsigned), ZI and
   I, truncated integer storage, the left-edge fold - except that the
   stored value clamps at the top as well as at 0. Planned as: ZI and I
   with 16-bit Z, then 32-bit: ZORG alignment and the bank advice, the
   stored bits, clamp or wrap at 1.0, compare direction, Z pitch.
3. **Textures (probe).** Done 2026-10-10 for 565 and 1555
   ([record](../decisions/2026-10-10-mga2164w-textures.md)): opcode 0110
   and the 2C00h block as 86Box has them, coordinates, wrap, clamp,
   pitch, perspective, key and modulate confirmed; corrected for bit
   replication, the decal alpha rule (565 counts as alpha 0), no 4444,
   32-byte TEXORG and a 1/8-texel perspective bias. Left as 3b: the
   palettised formats and the LUT load, textures with depth, and a 16 bpp
   target. As planned: first, does opcode 0110 draw at all with the
   2C00h block as hypothesised; the probe refuses the 2064W, so this never
   runs on the Millennium. Then TW16 linear (npcen), the TMR scales and
   TEXWIDTH/TEXHEIGHT, wrap and clamp, the pitch field; then perspective
   with q; then TW15/TW12, colour key, decal against modulate, the
   palette formats and the LUT load.
4. **Driver engine.** `mga_3d.c` from the probe-proven encodings,
   `d3d_mga.c`, caps from phases 1-3 only. Exit: V9XDDP's triangle,
   shading, depth and texture checks pass, and every check the chip
   cannot do is refused, not drawn wrong.
5. **Applications.** Final Reality, 3DMark 99, Half-Life (Direct3D), one
   at a time. Refusal counts recorded; scores recorded if a run produces
   them, never chased.

## Decisions wanted

1. **Probe first.** Phases 1-3 are tools and decision records before any
   driver code. Recommended.
2. **A reference driver.** Matrox's own Windows 98 Millennium II driver
   has a Direct3D HAL. Installed beside ours (one Enum `Driver` value, as
   with SiS), V9XDDP under it gives the baseline the exit gate compares
   against, and a register capture of its draws would settle phase 3
   faster than blind probing. Needs the package from you; it is not on
   this host. Recommended.
3. **Alpha blending is refused, not stippled.** A batch asking for
   SRCALPHA blending is drawn by the software rasteriser, correctly,
   rather than as a screen-door on the engine. The stipple is what
   Matrox's driver did; it could follow later as its own measured change.
   Recommended: refuse.
4. **16 bpp first, 32 bpp after phase 4.** 8 bpp desktops keep software
   Direct3D. Recommended.
5. **The engine type stays `MGA`.** The 2D paths test `== MGA` in about
   twenty places; a separate 2164W type (the Rage II precedent) would
   touch all of them. Instead `d3d_select` maps `MGA` to the 3D engine
   only when `CAP_D3D` is stamped, which only the 2164W's hook does, and
   `test_d3d_select.c`'s "2064W has no 3D" row keys on the cap.
   Recommended.

## Safety contract

- Probe writes go to off-screen VRAM inside guards; nothing scans out.
- Every wait is bounded; a stuck engine is a recorded timeout.
- One application at a time in phase 5; no DOS boxes on A8U4I5.
- The probe refuses any chip but 051B for 3D phases.

## Open questions only hardware answers

- Whether the 2C00h texture block and opcode 0110 are as hypothesised.
- The TMR fixed-point scales and how q is applied.
- Pure d/dy or edge-relative y increments.
- The fill rule and sub-pixel behaviour of integer-coordinate edges.
- Which Z bits are stored, and what happens at the ends of the range.
- Whether texel alpha and the trans stipple combine.
