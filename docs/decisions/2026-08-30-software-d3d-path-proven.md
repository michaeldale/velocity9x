# A Trio64 created a Direct3D device and drew, and the ViRGE ladder did not move

Date: 2026-08-30
Branch: `main`
Plan: [`s3-trio64-voodoo2-hybrid-3d.md`](../plans/s3-trio64-voodoo2-hybrid-3d.md),
mode 2, work-order steps 4 and 5.

Guests: `Win98SE-Trio64` (86Box, S3 Trio32/64 86C764, 4 MiB, 800x600x16, host
port 9871) and `Win86SE` (ViRGE/DX 86C375, 1024x768x16, port 9869). Same `s3`
family binary on both.

## What was built

Not a rasterizer. `draw_triangles` fills each triangle's bounding box with a
flat colour taken from the first vertex - deliberately, and the file says so at
the top. The point was to make the whole path visible end to end before any
edge-stepping arithmetic exists, because this project has twice spent a long
time proving that the thing wrong with a new Direct3D path was not the part
that had just been written.

Around it, the change that actually mattered: **three ViRGE-specific gates left
the chip-neutral draw path.**

- `v9x_d3d_engine()` now tests `V9X_DD_ENGINE_CAP_D3D_SOFTWARE` before it tests
  `engine_type` - mode first, chip second.
- `v9x_engine_status_validated()` is gone from all three draw entry points,
  replaced by a `ready` hook appended to `V9X_D3D_ENGINE_OPS`. The ViRGE's
  implementation is the old test verbatim; the software engine returns 1.
- The unconditional `v9x_engine_validate_status()` beside each of those went
  with it, into the ViRGE's `ready`.

The mode reaches the 32-bit side as a capability bit rather than a new shared
block field, so no ABI or layout change: `V9X_DD_ENGINE_CAP_D3D_SOFTWARE`,
stamped by `dd16.c` **outside** the descriptor branch, because four of the six
families supply no `fill_engine_descriptor` at all and those are exactly the
cards this mode is for.

And `v9x_dd_block()` now stamps the capability word, which runs on the
`DDGET32BITDRIVERNAME` escape and therefore before `DriverInit`. That is what
finally lets `v9x_d3d_publish_engine()` select - the function has carried a
comment since 2026-08-29 explaining why it could not.

## Measured on the Trio64, which has no 3D engine at all

`Direct3D=2` in `SYSTEM.INI`, `Stage=enable-ok`, and:

```
Direct3DMode=software        (V9XHW.INI; the chip's own Direct3D= is
                              still not-advertised, and that is correct)
D3DHalFound=1                D3DDeviceCount=4
D3DDevice2Name=Direct3D HAL  D3DCreateDeviceHr=0x00000000
D3DTrianglePixelRaw=63488    TexFormatCount=0
```

**A card that has never had Direct3D enumerated a hardware Direct3D device,
created it, and put pixels on the screen.** 63488 is 0xF800: pure red in
RGB565, which is what a red triangle's bounding box should be.

`TexFormatCount=0` is correct and deliberate - `describe_caps` advertises no
texture formats because nothing samples one yet, and `texture_format` declines
every surface to match.

### The one failing key is the interesting one

`D3DTrianglePixelOk=0`, because the probe compares against **31744** - 0x7C00,
which is red in **ZRGB1555**, the value the ViRGE writes.

Both engines drew red. They disagree about what red is, and the software
engine is the one writing the format the surface is described as. README
already records this as a known ViRGE defect: "The S3D triangle engine writes
native ZRGB1555 into a surface described as RGB565, which is an unresolved
mismatch." A second engine has now made that mismatch visible from the outside
rather than only in a comment - the probe's expected value is the buggy one.

Not fixed here. Changing it means deciding whether the ViRGE path or the probe
is wrong, and that is a separate question with its own evidence. What this run
establishes is that the disagreement is real and testable.

## No regression on the ViRGE

Same build, hardware mode, after all three gates moved:

```
Result=COMPLETE          D3DHalFound=1            TexFormatCount=2
D3DTrianglePixelRaw=31744  D3DTrianglePixelOk=1   D3DBaseTextureOk=1
D3DTrilinearBlendOk=1      Tex4444PixelOk=1       D3DContextCycleOk=1
D3DZCompareOk=1            D3DZWriteMaskOk=1      ZDepthFillOk=1
```

Every functional key matches the ladder recorded before the change. Moving the
gates into the vtable changed nothing about the chip that used to own them,
which is what a refactor of a gate should look like.

## What this does not establish

- **That it renders.** It fills bounding boxes. Every question about triangle
  coverage, interpolation, depth and texturing is untouched, and the caps
  advertise none of them.
- **Anything about speed.** Two 800x600 rectangles say nothing about a
  rasterizer's cost, and the target machine for that is BARRY.
- **The tier-0 and ATI families.** Both take the same code path as the Trio64 -
  no descriptor hook, capability stamped outside the branch - but neither was
  booted with software mode on.

## One operational trap, paid for twice

`WININIT.INI`'s `[Rename]` deleted `V9XDISP.DRV` and did not perform the
rename, leaving a **0-byte display driver and no desktop**. The staging file
was present and the right size.

The difference from the runs that worked is the path: those staged at
`C:\V9XNDRV.BIN`, this one at `C:\V9XREMOTE\NDRV.BIN`, and `V9XREMOTE` is nine
characters. Real-mode `WININIT` processing appears not to handle a non-8.3
directory in a rename source, and it does the `NUL=` deletion regardless.

**Stage `WININIT.INI` rename sources at 8.3-compliant paths.** Better, for a
file the desktop is not currently holding: put it straight to
`C:\WINDOWS\SYSTEM` and reboot, which is what recovered both machines here and
needs no rename at all.
