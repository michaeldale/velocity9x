# The VBE tier-0 driver runs a real S3 Trio3D, and the software rasterizer with it

Date: 2026-09-02
Branch: `main`
Release under test: 0.7.0, `Build=1ca57e0` - the published binary, not a dirty
tree.

Machine: **A8U4I5**, `10.0.1.172`, physical. Windows 98 SE, remote agent 0.6.1.

## The card

`PCI\VEN_5333&DEV_8A13&SUBSYS_888810B0&REV_02` - an **S3 Trio3D/2X**. The
project's own ROM survey
([2026-08-29](2026-08-29-s3-device-id-survey.md)) puts `8A13` on
`TRIO3D2X_8mbsdr.VBI`, and **no Velocity9x family binds it**: the `s3` family
covers `8810`-`8814`, `8901` and `8A01`.

Before the install the machine was running Microsoft's `vga.drv` at 640x480, and
its registry still carried NVIDIA GeForce2 and RIVA TNT2 class entries from
cards previously in the box. Neither is fitted; the only enumerated display
devnode is the S3, and `SYSTEM.INI` had
`display.drv=Standard PCI Graphics Adapter (VGA)` bound to it.

## How it was installed

The `vbe` family's second model, the manual-select one:

```
"Velocity9x VBE-generic display (any VESA VBE 2.0+ adapter)"=Velocity9x.Install.Manual,, PCI\CC_0300
```

No hardware ID, `PCI\CC_0300` as a **compatible** ID. Windows offered it under
**"Show compatible devices"** rather than requiring "Show all hardware", so this
was the designed tier-0 path doing its job on a card nothing claims - not a
forced install.

**The package's own preflight ran first and passed.** `V9XSTAGE.EXE` wrote
`Stage=query-ok`: both binaries load and the display driver's GDIINFO and mode
validation are coherent, with nothing installed and no mode set. On real
hardware, where recovery from a failed display install needs someone at the
keyboard with F8, that is worth the two minutes.

## Measured

### The driver comes up

```
Stage=enable-ok
Adapter=Generic VESA adapter (no chip-specific support)
PciVendorId=5333   PciDeviceId=8A13
VbeController=v=0200 mem=64 caps=00000001 rev=0101
VbeCache=s=1817 l=63 q=63 c=45 p=0 f=0107
VbeDetail=ok
ModeSwitching=vbe-lfb   Acceleration=none   GdiAcceleration=none
Surface=pitch=640 bpp=8 dwb=640 dds=640 w=640 h=480
VddReserve=vdd=307200 visible=307200 vram=4194304 info=131
```

VBE 2.0, 4 MiB reported through `4F00h`, **63 modes listed, 63 queried, 45
cached, 0 failures**, linear framebuffer at `0xD8000000` on every one. The
cached set runs from 320x200x8 to 1600x1200x16 including 24 bpp and 32 bpp
variants - a far wider inventory than the family's declared 640/800/1024
baseline, which is the runtime BIOS scan doing what it is for.

A colour-depth change to 16 bpp took `DrawPitch` to 1280 and the desktop came up
clean.

### The software rasterizer, on real silicon

`Direct3D=2` set through the Display Properties selector, then a restart:
`Direct3DMode=software`, and the probe at 640x480x16:

```
Build=1ca57e0            Result=COMPLETE
D3DTargetRMask=63488     D3DExpectRed=63488     (RGB565, queried)
D3DHalFound=1            D3DCreateDeviceHr=0x00000000
TexFormatCount=2
D3DTrianglePixelOk=1     D3DSubpixelTriangleOk=1  D3DTriangleShapeOk=1
D3DZCompareOk=1          D3DZWriteMaskOk=1        D3DSpecularGouraudOk=1
D3DDepthFogOk=1          D3DBaseTextureOk=1       Tex4444PixelOk=1
D3DContextCycleOk=1
```

Every functional key passes, and the published caps are identical to the two
emulated guests: `TriRaster=48`, `TriZCmp=255`, `TriShade=522`,
`TriTexture=34`, `TriFilter=3`, `TriBlend=3`, `TriAddress=4`, `ZDepth=1024`.

**This is the first time any of it has run on real hardware** - the VBE family,
and the CPU rasterizer.

### The selector, on a fourth card and the first real one

```
Software (CPU rasterizer - slow)
Disabled - advertise no Direct3D
Not advertised on this chip        <- selected, carrying the loaded 0
```

No Hardware entry, which is correct: the Trio3D has a 3D engine and this driver
implements nothing for it.

### The clipper defect reproduces on silicon

`D3DEdgeCentreRaw=63488` and `D3DEdgeTopLeftRaw=63488` with
`D3DEdgeRightRaw=0` and `D3DEdgeBottomRaw=0`. The last row and column of a
full-target triangle are missing here exactly as on both emulated engines, which
moves [the issue](../issues/2026-09-01-clipper-loses-last-row-and-column.md)
from "both engines under 86Box" to "everywhere it has been looked for".

### A machine with real MTRRs

```
Mtrr=cpu=000f cap=00000508 def=00000c00 n=8 r=0 s=3 b=d8000000 z=00400000
```

Decoded against `include\velocity9x\mtrr.h`: the CPU reports CPUID, MSR, MTRR
and PGE; `MTRRCAP` gives eight variable ranges with the write-combining bit set;
`MTRRDEF` is enabled with a UC default; eight pairs were read; **`r=0` is
`V9X_MTRR_OK`** and `s=3` is the free slot the planner chose, for the 4 MiB
aperture at `0xD8000000`.

Every 86Box guest in this fleet reports `V9X_MTRR_NO_MTRR` - the emulator models
none on any CPU
([2026-08-28](2026-08-28-mtrr-stage-a-inspect-only.md)). **This is the first
machine anywhere in the project where the write-combining decision comes back
OK**, which makes it the target Stage B of
[`tier0-quality.md`](../plans/tier0-quality.md) has been missing. Stage A still
writes nothing, and nothing here changed that.

## What this does not establish

- **Nothing about speed.** No benchmark was run. The rasterizer's cost is still
  unmeasured on any machine, emulated or real.
- **Nothing about 2D acceleration**, which tier-0 does not attempt:
  `Acceleration=none`, `GdiAcceleration=none`, and the probe's fill timings
  (`BltFillMs=12`, `PrimaryFillMs=12`) are CPU fills.
- **`FlipPixelOk=0`.** The flip path reports `Flip20Ms=1` and
  `FlipMaxMs=1` with a failed pixel check. Not investigated here; tier-0 has no
  hardware flip and the surface it verifies may not be the one presented.
- **Nothing about the `s3` family on this card.** Whether a Trio3D can be driven
  by the Trio64 register path is untouched, and `8A13` remains unbound.
- **One deviation from INSTALL.TXT**, stated because it is a deviation: step 8
  asks for a full shutdown rather than a warm restart for the first test. That
  instruction is written for a VM that can be cold-started from the host. This
  machine is remote and a shutdown would have stranded it, so both restarts were
  warm. The card still POSTs and re-runs its video BIOS on a warm boot, but the
  cold path is untested here.
