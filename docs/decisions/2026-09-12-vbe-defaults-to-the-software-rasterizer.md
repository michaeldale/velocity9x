# The tier-0 package defaults to the software rasterizer

Status: accepted, 2026-09-12. Applies to the `vbe` family only.

## The decision

An absent `[Velocity9x] Direct3D` key in SYSTEM.INI is read as
`V9X_D3D_REQUEST_SOFTWARE` on the `vbe` package, and as
`V9X_D3D_REQUEST_HARDWARE` on every other family, which is what all four did
before this. A fresh tier-0 install therefore serves Direct3D from the CPU
rasterizer without the owner setting anything.

The mechanism is one manifest line. `V9X_D3D_DEFAULT_REQUEST` is defined in
`include/velocity9x/d3dmode.h` behind an `#ifndef`, defaults to
`V9X_D3D_REQUEST_HARDWARE`, and `packaging/families/vbe/family.psd1` overrides
it through `Build.Defines`. `src/display16/dd16.c` passes it as the
`GetPrivateProfileInt` default. Nothing about the resolve table changed, so the
rule that a setting cannot grant a capability still holds exactly as tested.

## Why

On tier-0 the choice this default makes is not "hardware or software". It is
"software or nothing".

The `vbe` family declares `EngineType = 'NONE'` and no chip in it claims
`V9X_DD_ENGINE_CAP_D3D`, so `v9x_d3d_mode_resolve` turns a HARDWARE request
into `V9X_D3D_STATE_NONE` on every card the package will ever meet. That is by
definition of the tier, not a gap to close: the package exists for cards the
driver has never been told about, reached through `PCI\CC_0300` or Have Disk.

So the old default advertised no Direct3D on the one family that cannot ever
advertise hardware Direct3D, and the CPU rasterizer that was written precisely
for that case sat behind a settings-page entry most owners of such a card would
never find. The rasterizer is the only Direct3D tier-0 can have.

## What it costs, stated plainly

- **Direct3D is advertised on machines whose owner did not ask for it.** An
  application that enumerates a Direct3D HAL device on a tier-0 card now finds
  one, and it is a CPU rasterizer. No period-machine measurement of it exists
  on any card. The recorded speed work is all relative, against this driver's
  own earlier software path on emulated S3 parts, and the emulator's
  framebuffer is host RAM: see
  [the scalar fixes](2026-09-10-rasterizer-scalar-fixes.md) and
  [the sampler fixes](2026-09-10-rasterizer-texel-units-and-bilinear.md). None
  of it predicts a frame rate on a real 1990s machine driven through VBE.
- **It does nothing at the family's own default depth.** The software engine
  is 16 bpp only (`src/display32/d3d/d3d_soft.c`), and the `vbe` INF's
  `DefaultMode` is `8,640,480`. A fresh install has to reach a 16 bpp mode
  before this default changes anything. That condition is not new - the opt-in
  route always had it - but it means "on by default" overstates what a
  first boot gets, and the manifest comment says so.
- **The rasterizer's limits are unchanged.** No texture alpha, no perspective
  correction, no mip selection, no fog.

It is one settings-page entry away from off, and the page preselects correctly
now (below).

## The settings page had to be told

`tools/diag/settings_propsheet.c` preselects its Direct3D selector from the raw
SYSTEM.INI value, and `tools/diag/settings_status.c` read that value with its
own HARDWARE default. On a package whose default is not HARDWARE, an absent key
would have left the page showing one thing while the driver did another - and
an OK on that page writes the selection back, so the disagreement would have
become a silent write of the wrong value.

The default is a build property of the package the page shipped in, so the
package says it: `src/display16/ddi.c` publishes it to
`C:\V9XDIAG\V9XHW.INI` as `Direct3DDefault=`, and `settings_status.c` uses that
as the default for its own read. This is the same two-keys split already used
for `Direct3D=` (the chip's word) against `Direct3DMode=` (this boot's
resolution): neither key can express both facts.

`v9x_d3d_request_text` in `src/common/d3dmode.c` writes the number, because the
16-bit driver links no formatting routine.

## What is asserted, and where

The value only exists on a family compile, so it is asserted in two places:

- `tests/host/test_d3dmode.c` covers the fallback every other family ships,
  that the default is a request the resolve table knows, that it prints as a
  digit the page can match, and the pair of outcomes the vbe override chooses
  between on a chip with no 3D engine.
- `scripts/check-tree.ps1` asserts the `#ifndef` guard in the header, that
  `dd16.c` actually uses the macro as its absent-key default, that both
  `ddi.c` and `settings_status.c` still name `Direct3DDefault`, and that the
  vbe manifest still carries `V9X_D3D_DEFAULT_REQUEST=2`. Each of the four was
  checked by breaking it and watching the gate fail.

## Evidence

`run-checks` passes: check-tree, the survey safety gate, host tests, and all
four family packages with their post-link audits and INF assertions.

The define reaches the compiled image: building the `vbe` 16-bit driver with
the same `-BuildId` with and without the manifest line produces different
binaries (`9e632e59...` against `8f2558e2...`).

Guest evidence is recorded in the section below.

## Guest result

See the appended section; if this document ends here, the guest run had not
been recorded when it was written.
