# The S3D engine writes ZRGB1555 because that is its only 16-bit destination

Date: 2026-09-02
Status: root cause established from a primary source
Closes the "cause not established" on
[`../issues/2026-09-01-virge-3d-writes-zrgb1555.md`](../issues/2026-09-01-virge-3d-writes-zrgb1555.md)

## The source nobody had opened

The issue named where to look and it had not been looked at: *"The Windows 98
DDK's own ViRGE driver is the place to look - `C:\98DDK\src\display\mini\s3v\`"*.
It is there. S3's shipping ViRGE display driver, with its own register header,
its Direct3D HAL, and its mode-setting assembly.

## What it says

`VIRGE1.H:130-133`, S3's own definition of the 3D command word's destination
field:

```c
#define cmdDEST_FMT_MASK           0xFFFFFFE3L
#define cmdDEST_FMT_8BPP_PAL       0L
#define cmdDEST_FMT_ZRGB1555       0x4L
#define cmdDEST_FMT_RGB888         0x8L
```

Three destination formats. Eight bits palettised, sixteen bits ZRGB1555,
twenty-four bits RGB888. **There is no RGB565 destination format.** The mask
clears bits 4:2, so the field has room for eight values and S3 defined three.

And `D3DRENDR.C:365-374`, where the driver picks one:

```c
switch (DDSurf_BitDepth(ctxt->lpLcl)) {
    case 8:  rndCommand |= cmdDEST_FMT_8BPP_PAL; break;
    case 16: rndCommand |= cmdDEST_FMT_ZRGB1555; break;
    case 24: rndCommand |= cmdDEST_FMT_RGB888;  break;
```

It switches on bit depth alone and never consults the channel masks. This is
exactly what `v9x_d3d_virge_draw_triangles` does, arrived at independently.

**Hypothesis 2 in the issue is established: the chip really does only write
1555 from the 3D engine.** Three independent sources now agree - 86Box's model,
a physical Trio3D/2X, and S3's own driver - and the third is not a model of the
hardware, it is what the hardware's authors shipped.

Hypothesis 1, that 86Box's model is incomplete and real silicon has a
destination-layout control it does not implement, is dead. If such a control
existed, S3's driver would use it, because S3's driver runs 565 desktops.

## Which it does, and that is the interesting part

The same driver sets 5:6:5 by default. `VGA.ASM:418-491` tags every 16 bpp
mode in the adapter table `FIVE6FIVE`, `VGA.ASM:3563-3580` copies that into
`wPDeviceFlags` at mode set, and `DDDRV.C:1030-1046` reports `0xf800/0x07e0/
0x001f` to DirectDraw when the flag is set and `0x7c00/0x03e0/0x001f` when it
is not.

So S3's driver reports a 565 primary to DirectDraw and then tells its own 3D
engine to write 1555 into it. On its face that is the same defect this project
filed against itself.

What sits between the two is a switch:

```asm
@@:     mov     ax,wHighColor
        test    ax,ax
        jz      short @f
        or      wPDeviceFlags,FIVE6FIVE         ;Assume force to 5:6:5
        cmp     ax,16                           ;Do they want 5:6:5?
        je      short @f                        ;yes.
        and     wPDeviceFlags,not FIVE6FIVE     ;no, make it 5:5:5
```

`wHighColor` is read from `SYSTEM.INI` (`INIT.ASM:123,242-248`, key
`HighColor`). `HighColor=15` runs the 16 bpp desktop as 5:5:5.

That is not proof the switch existed *for* this - the record does not say why
S3 added it, and a 555 desktop has other uses. But it is the only mechanism in
S3's driver that makes the 3D engine's output land in the right channels, and
it is a documented user-settable one. The period answer to "the 3D engine
writes 1555 into your 565 desktop" appears to be "then run the desktop at 555".

## What this means for Velocity9x

The engine is not wrong and must not be "fixed". The mismatch is between what
the driver *reports* the render target to be and what the engine can write, and
only one of the two can move.

The reachable version of S3's answer: **when hardware Direct3D is selected on an
S3D part, run 16 bpp as 5:5:5 and report it as such.** Four things stand in the
way, none of them hard, all of them findable:

1. The mode table publishes only 565 for 16 bpp - VBE `0x111/0x114/0x117/0x11a`
   with `rgb=0000f800,000007e0,0000001f`, measured from the guest's own
   `V9XMODES.INI`. The 15 bpp VBE modes `0x110/0x113/0x116/0x119` are not
   enumerated at all.
2. `v9x_d3d_core` rejects a render target that is not exactly RGB565
   (`d3d_core.c:375-377`).
3. `v9x_d3d_raster_rgb565` packs 565 unconditionally, so the software engine
   would have to learn its target's format - which it should anyway, since it
   currently assumes rather than reads.
4. The probe derives its expectations from the target's pixel format already,
   so it needs no change and would report the answer directly.

Then the experiment is decisive rather than merely plausible: on A8U4I5, a
15 bpp desktop with hardware Direct3D should turn every failing colour key to 1
with no change to the engine. If it does not, the engine is packing something
other than what both the emulator and S3's header say, and that is a different
and much more interesting finding.

This is not scheduled behind the software engine's missing capabilities by
accident - see below - but it is now a defined experiment rather than an open
question.

## Also in the file: what the ViRGE actually has

Read while establishing the above, and worth recording because it is the target
the hardware path should be measured against. S3's shipping caps
(`D3DDRV.C:230-271`) claim, on the same silicon this driver drives:

- `D3DPTADDRESSCAPS_WRAP | CLAMP` - and `cmdWRAP_EN` (`0x4000000`) is a real
  command bit the driver sets for either address mode (`D3DRENDR.C:83-84`).
- `D3DPBLENDCAPS_ONE | SRCALPHA` source, `ZERO | INVSRCALPHA` destination, and
  `D3DPSHADECAPS_ALPHAFLATBLEND | ALPHAGOURAUDBLEND`. `cmdALP_BLD_CTL` selects
  none, texture alpha, or source alpha.
- `D3DPTEXTURECAPS_PERSPECTIVE`, `ALPHA`, and the four mip filter caps.
- Texture blends `DECAL | MODULATE | DECALALPHA | MODULATEALPHA | COPY | ADD`.
- `D3DPRASTERCAPS_FOGVERTEX`, `D3DPSHADECAPS_FOGFLAT | FOGGOURAUD`.
- Texture formats XRGB1555, ARGB1555, ARGB4444, ARGB8888, 8-bit palettised,
  and `cmdTEX_CLR_FMT_8BPP_ClrInd` to sample them.

Velocity9x's ViRGE path claims a strict subset of that. The gap is not
hardware; it is unwritten driver.

One confirming detail on the texture side: S3's driver distinguishes ARGB1555
from ARGB4444 by testing whether the blue mask is `0x001f`
(`D3DRENDR.C:101-108`). RGB565's blue mask is also `0x001f`, so that test would
classify a 565 texture as 1555. A driver that ever published RGB565 as a
texture format could not have been written this way - which is the same
conclusion reached yesterday from 86Box's decode, from the other end.
