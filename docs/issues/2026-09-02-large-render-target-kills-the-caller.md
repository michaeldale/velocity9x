# RETRACTED: "DrawPrimitive into a 640x480 render target terminates the caller"

Filed: 2026-09-02
**Status: invalid, retracted the same day. The fault was in the test, not the
driver.**
Reproduce the corrected test: `V9XDDP.EXE /bigtarget`

## The claim, and why it was wrong

A new probe test created a 640x480 `3DDEVICE | OFFSCREENPLAIN | VIDEOMEMORY`
surface, created a Direct3D HAL device on it, and called `DrawPrimitive`. The
process died inside that call. It was bisected with stage markers to the exact
call, reproduced byte-identically on repeat runs, and reproduced on three
targets: a physical S3 Trio3D on the hardware S3D path, an emulated ViRGE/DX on
the same path, and an emulated Trio64 in **software** mode with no S3D engine at
all.

Reproducing across two engines and three machines looked like strong evidence of
a defect in shared code. It was strong evidence of a defect in the one thing all
three runs shared that was not the driver: **the test.**

**The test never created a viewport.** `IDirect3DDevice2::DrawPrimitive` with no
current viewport faults. The probe's existing 64x64 test creates one, adds it to
the device and calls `SetCurrentViewport` before drawing; the new test copied the
drawing and not the setup.

## The corrected result

With a viewport created, added, set and made current, the same test passes
everywhere it previously died:

| Target | Mode | Result |
|---|---|---|
| `Win98SE-Trio64` (86Box) | software rasterizer | `D3DBigShapeOk=1`, `D3DBigRaw=63488` (red, RGB565) |
| A8U4I5, physical S3 Trio3D/2X | hardware S3D | `D3DBigShapeOk=1`, `D3DBigRaw=31744` (red, ZRGB1555) |

`D3DBigStage=7`, `D3DBigDrawHr=0x00000000`, `D3DBigOutsideRaw=0` on both, and
`Result=COMPLETE`. **A screen-sized render target draws correctly on both
engines**, with the 1555-into-565 difference that is already a separate issue.

## What was actually learned

The test is worth keeping - it closes a real gap. Every other Direct3D pixel
test in this probe draws into a 64x64 surface with a 128-byte pitch, low in
video memory. This one exercises a 1280-byte stride and a high `DEST_BASE`,
which is what a game uses, and it now passes on both engines rather than being
assumed to.

Two process lessons, which are the reason this file is kept rather than deleted:

1. **Reproducing across targets does not mean the driver is at fault.** It
   narrows the cause to something shared, and the harness is shared too. The
   software engine reproducing it should have been read as "this is not the
   S3D path" *and* "this may not be the driver at all"; only the first was.
2. **A new test that fails on its first run is more likely to be wrong than the
   thing it tests.** The bisect correctly found the failing call and was then
   used to reason about the driver rather than to check the call's own
   preconditions against the working test twenty lines above it.

## The switch

`/bigtarget` stays a switch rather than becoming default. Not because it is
dangerous now - it passes - but because it has been run on two targets and the
probe's default set is run on every family. Promote it once it has been through
an ATI and a VBE run.
