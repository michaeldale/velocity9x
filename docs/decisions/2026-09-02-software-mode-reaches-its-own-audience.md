# The software mode was unreachable from the page on every card it was written for

Date: 2026-09-02
Branch: `main`
Plan: [`s3-trio64-voodoo2-hybrid-3d.md`](../plans/s3-trio64-voodoo2-hybrid-3d.md),
mode 2.

## The question that found it

"Does software Direct3D work in the VBE driver path?" The answer turned out to
be yes in the driver and no in the user interface, and the second half was the
one worth fixing.

## The driver path is generic, and the Trio64 already proved it

`src\chipsets\s3\s3_hw16.c:11` states it plainly: each device entry carries its
own `enable_aperture` and `fill_engine_descriptor`, "so the ViRGE opens its
new-MMIO window and describes an S3D engine **while the Trio64 does neither**".

So the Trio64 - the guest every mode 2 measurement has been taken on - is
already the descriptor-less path, the `else` arm of `dd16.c` that zeroes the
whole engine block. That is the same arm the VBE, ATI and Matrox families take.
The chain, end to end:

1. **Mode resolution.** `v9x_d3d_mode_resolve(REQUEST_SOFTWARE, V9X_FALSE)`
   returns `STATE_SOFTWARE` - a chip with no Direct3D still resolves to the
   rasterizer. Host-tested at `tests\host\test_d3dmode.c:64`, and gated on
   every run since.
2. **The capability stamp.** `dd16.c` sets
   `CAP_D3D | CAP_D3D_SOFTWARE | ENGINE_VALID` **outside** the
   `fill_engine_descriptor` branch, in both stamp sites, with a comment saying
   that is deliberately for the four families with no descriptor.
3. **Engine selection.** `v9x_d3d_engine()` tests `CAP_D3D_SOFTWARE` before it
   tests `engine_type`, so a chip whose type is NONE still resolves the
   rasterizer.
4. **The engine.** `d3d_soft.c` and `d3d_raster.c` touch no chip register. They
   need the framebuffer's linear base, the target offset, a pitch and an
   extent, all of which the chip-neutral DirectDraw path fills on every family.
5. **Measured**, on the Trio64, on 2026-09-01: depth-tested textured triangles
   through exactly those five steps.

Nothing needed adding to the driver.

## What did need adding

`tools\diag\settings_propsheet.c` offered two choices - Hardware and Disabled -
and greyed the control out entirely when `direct3d_capable` was false, which is
true of every card without an S3D unit.

Both halves were correct when written and both had gone stale:

- The list comment said Software joins it "when they render pixels". It renders
  them.
- The greying was justified because "on a chip with no 3D engine no value can
  produce Direct3D". That is no longer true, and the cards it applied to are
  precisely the ones software mode exists for. **The mode worked and only the
  page could not reach it.**

The control is now always live, and `needs_engine` decides which entries exist:
only Hardware needs one, so a card without a Direct3D engine this driver
implements sees exactly **Software and Disabled**, and only the ViRGE is offered
Hardware.

Relabelling rather than hiding was tried first - Hardware present everywhere,
reading "this card has no 3D engine" - and it is worse. It puts an entry in the
list whose only effect is to produce no Direct3D, which is what the entry below
it already says, and it invites the reading that the mode is something the card
is failing at rather than one it was never offered.

The default stays reachable without it. HARDWARE is zero, which is what an
absent key means, so a card not offered it can still be sitting on it: the tail
of `v9x_page_fill_d3d` gives any loaded value the list does not carry its own
entry, and for that case labels it with the card's own words - "Not advertised
on this chip" - rather than the wording for a mode this build lacks. The entry
carries the loaded value, so selecting nothing writes nothing.

## Measured, on three cards

`Build=add87c6-dirty`, `V9XSETP.DLL` from each family's own package.

**S3 ViRGE/DX** (`Win86SE`, port 9869) - the only chip with a Direct3D engine
this driver implements:

```
Hardware (the chip's own engine)      <- selected
Software (CPU rasterizer - slow)
Disabled - advertise no Direct3D
```

**S3 Trio32/64** (`Win98SE-Trio64`, port 9871), sitting on `Direct3D=2`:

```
Software (CPU rasterizer - slow)      <- selected
Disabled - advertise no Direct3D
```

**ATI Mach64 VT2** (`Win98SE-Mach64VT2`, port 9873), sitting on the default:

```
Software (CPU rasterizer - slow)
Disabled - advertise no Direct3D
Not advertised on this chip           <- selected, carrying the loaded 0
```

Before this change the same control on the Trio64 and the ATI was a greyed box
reading "Requested mode is not in this build".

## The ATI ran it, end to end

The Mach64 VT2 is the second family to take the descriptor-less path, and it is
a stronger case than the Trio64: `Acceleration=none`, `GdiAcceleration=none`,
`ClockStatus=unavailable`, `ModeSwitching=vbe-lfb`. It has no 2D engine this
driver drives at all, and its registry display name is still the VBE-generic
one from an earlier install.

Driven entirely through the page - select Software, OK, restart - and then the
probe:

```
Direct3DMode=software     D3DHalFound=1        D3DCreateDeviceHr=0x00000000
TexFormatCount=2          D3DTrianglePixelOk=1 D3DSubpixelTriangleOk=1
D3DTriangleShapeOk=1      D3DZCompareOk=1      D3DZWriteMaskOk=1
D3DSpecularGouraudOk=1    D3DDepthFogOk=1      D3DBaseTextureOk=1
Tex4444PixelOk=1          D3DContextCycleOk=1  Result=COMPLETE
```

Every functional key that passes on the Trio64 passes here, and the published
caps are identical: `TriRaster=48`, `TriZCmp=255`, `TriShade=522`,
`TriTexture=34`, `TriFilter=3`, `TriBlend=3`, `TriAddress=4`, `ZDepth=1024`.

One trap on the way, and it is the reason to look at `Direct3DMode` before
believing a run: the guest's 16-bit `V9XDISP.DRV` was from 2026-08-30 and
resolved `Direct3D=2` to `mode-unimplemented`, because mode resolution lives in
the display driver rather than in the HAL. Pushing `V9XHAL.DLL` and
`V9XSETP.DLL` alone is not enough. The driver went over by the recorded route -
stage at `C:\V9XNDRV.BIN`, an 8.3 path in the root, and a `WININIT.INI`
`[Rename]` - which worked first time.

## What is still not measured

**No VBE guest ran.** `Win98SE-QEMU-StdVGA` was started and boots to a Windows
98 desktop reporting "Your display adapter is not configured properly", with no
shell icons and no agent answering on host port 9872. It is in a broken display
configuration left from earlier tier-0 work; reviving it is its own task and
was not attempted. The VM was stopped again.

So the VBE claim now rests on the chain above **plus** an end-to-end
measurement on the ATI, which takes the identical arm and reaches its
framebuffer through the same `vbe-lfb` mode switching the VBE family uses. That
is materially stronger than it was, and it is still not a VBE guest.
