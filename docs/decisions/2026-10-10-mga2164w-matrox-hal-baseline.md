# What Matrox's own HAL publishes on the MGA-2164W

Date: 2026-10-10. A8U4I5 (10.0.1.172), boot 383, the physical Millennium
II (102B:051B, subsys 1200102B, rev 00, 4 MiB) under Matrox's Windows 9x
driver 4.33c (`MGAPDX64.DRV`/`.VXD` 1999-04-12, `MGALLX64.DLL`). Evidence
in `docs/probe/a8u4i5-mga2164w-matrox-hal-2026-10-10/`. This is the
reference the [hardware Direct3D plan](../plans/matrox-mga2164w-hardware-3d.md)
compares against.

## Install and swap

The package came from Michael's LAN server as a self-extracting ZIP. It
was unpacked on the host with 7-Zip (not run), pushed to `C:\MGAREF\W9X433C`
and installed by Update Driver > Have Disk over our driver. Windows warned
that the current driver was a closer match; accepted. The INF reused
`Display\0012`. Both class keys are saved:

- `display-0012-velocity9x.reg` (in the first-boot probe folder) - ours.
- `display-0012-matrox-433c.reg` - Matrox's. It adds `minivdd2`, so going
  back to ours needs `"minivdd2"=-` beside the import.

The driver files of both stay in `SYSTEM`; a swap is the registry import
and a warm restart.

## Apertures, from Configuration Manager

| BAR | Range | Use |
| --- | --- | --- |
| 0 | `DC000000-DCFFFFFF` | framebuffer, 16 MiB window |
| 1 | `DD000000-DD003FFF` | control aperture, 16 KiB |
| 2 | `DE000000-DE7FFFFF` | ILOAD / pseudo-DMA window, 8 MiB |

The order the 2164W specification gives (2-2), the reverse of the 2064W
for the first two (`resources-apertures.png`, first-boot folder).

## V9XDDP under Matrox's HAL, 800x600x16

`Result=COMPLETE`, 1028 keys. `GblNoHardware=0`, **7.0 MiB off-screen**
(`GblHalVidMemTotal=0x0070C400`). That does not fit a 4 MiB card: the
BIOS's 4F00h said `mem=64` (4 MiB) on boot 380 and our driver believes
it. **V9XVRAM.EXE settles it: at least 8 MiB.** Run under Matrox's
driver on boot 383, all sixteen signatures from 0 to 7.5 MiB held
(`LeadingPointsHeld=16`, `Result=AT-LEAST-8MIB`; the tool stops at 8
MiB, so 16 MiB is not ruled out). This card's VBE BIOS under-reports
its memory by half, and Velocity9x, which sizes the matrox family from
4F00h, uses 4 MiB of it. A Z buffer and textures need the rest; the
family needs a chip-side size (Matrox's driver has one) before phase 2. The Direct3D HAL device (`D3DDevice2`) publishes:

| Cap | Raw | Decoded |
| --- | --- | --- |
| RenderDepth | 1280 | 16 and 32 bpp targets |
| ZDepth | 1024 | 16-bit Z only (the chip has 32-bit Z; Matrox does not publish it) |
| ZCmpCaps | 255 | all eight compares |
| SrcBlendCaps / DestBlendCaps | 16 / 32 | SRCALPHA / INVSRCALPHA only |
| AlphaCmpCaps | 0 | no alpha test |
| ShadeCaps | 0x228A | flat and Gouraud RGB, specular flat and Gouraud RGB, **alpha flat stippled** |
| TextureCaps | 11 | perspective, power of two, transparency |
| TextureFilterCaps | 1 | nearest only |
| TextureBlendCaps | 79 | decal, modulate, decalalpha, modulatealpha, copy |
| TextureAddressCaps | 21 | wrap, clamp, independent u/v; no mirror |
| MiscCaps | 0x73 | mask planes, mask Z, cull none/cw/ccw |
| RasterCaps | 1 | dither |
| MaxVertices | 1024 | |

Texture formats: five enumerated; 565 and 1555 among them, 4444 not.
The other three are not named by V9XDDP's keys (palettised, presumably;
not checked).

Results under it: the triangle, slot, half and reverse checks, the twelve
shapes, the subpixel triangle and Gouraud specular pass. Every textured
check reads 65535 (`D3DBaseTextureOk=0`, `D3DTiledTextureOk=0`, fog,
specular-texture, mip and trilinear 0) - the textured triangle did not
reach the target, or drew white. Not investigated: whether it is
Matrox's HAL or the probe's texture setup. The Z checks did not run
(`88760231h`, V9XDDP's not-run marker), as under SiS.

## What this settles and what it does not

- Matrox's HAL treats translucency as a stippled flat alpha and publishes
  SRCALPHA/INVSRCALPHA blending on that basis. The stipple option in the
  plan is what Matrox shipped.
- The hardware supports specular Gouraud in some form that Matrox
  publishes; the documents do not show how (no specular registers on the
  2164W). Open.
- 32-bit Z exists in the chip (MACCESS.zwidth) but is not published.
- Matrox's drawing registers are write-only, so its register traffic
  cannot be read back the way SiS's state was; the reference is its
  output, pixel for pixel, on the same scenes.
