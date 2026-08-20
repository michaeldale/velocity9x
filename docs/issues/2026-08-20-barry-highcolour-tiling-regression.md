# 16- and 32-bpp modes tile on the physical Trio64 after the 24/32-bpp work

Status: **open regression, branch `high-depth-dynamic-modes` (`3795d55`).**
BARRY has been returned to the pre-branch driver and is working normally.

Target: BARRY, physical S3 Trio64 (86C764), `PCI\VEN_5333&DEV_8811`, 2 MiB VRAM,
Windows 98 SE. Reached over the remote agent at `10.0.1.47:9869`.

## What happens

With the branch driver installed, every mode above 8 bpp renders the desktop
**tiled at half width** - two side-by-side copies of the image across the
screen, plus bands of stale framebuffer content. 8 bpp is unaffected and
pristine.

```
800x600x8      clean
1024x768x16    tiled          <- not a mode this branch added
800x600x32     tiled
```

`Stage=enable-ok` throughout, `V9XHW.INI` correct
(`VideoMemoryBytes=2097152`, `VideoMemoryStatus=valid`), agent responsive, and
`ChangeDisplaySettingsA` returns success for each of these modes. Nothing in the
driver's own reporting indicates a fault.

The half-width doubling is the signature of a surface whose content is laid out
at `width` bytes per row while being read at `width * bytes-per-pixel` - that is,
the 8-bpp stride used at a deeper colour depth. It is consistent across 16 and
32 bpp.

It survives a reboot, so it is not stale content left by
`V9X_HW16_VBE_NO_CLEAR` after a live switch, which was the first hypothesis.

## It is a regression, established two ways

**The pre-branch capture.** `build\driver-results\settings-tab-042\DESKTOP.BMP`,
taken on BARRY on 2026-08-19 with the 0.4.2-era driver, is a clean 1024x768
desktop at `SourceBitsPerPixel=16`. Same card, same mode.

**Re-deploying the pre-branch driver fixes it.** A package built from `main`
(`7bdbd80`) and installed over the branch driver restores a clean 1024x768x16
desktop on the same machine. That is where BARRY has been left.

## It does not reproduce under emulation

The same branch binary passes 11/11 on both 86Box S3 targets - ViRGE/DX `:9869`
and Trio64 `:9871` - with clean inspected screenshots at 1024x768x32, 800x600x32
and 1280x1024x16
(`docs\decisions\2026-08-20-high-depth-mode-matrix.md`). Both are **4 MiB**;
BARRY is **2 MiB**. Whether the VRAM size or the real BIOS is the variable is
not established.

## Where to look

Not diagnosed. The branch's pitch-adjacent changes are the obvious suspects, and
the strongest constraint on the search is that **1024x768x16 is not a mode this
branch added**: its table row, pitch (2048) and VBE number (0x0117) are
byte-identical to `main`, and the DIB engine still gets `FIVE6FIVE` at 16 bpp.
So the cause is something global rather than something about the new rows.

Candidates, in the order worth checking:

- `src\display16\dd16.c` - `v9x_dd_fill_modes` is new and now runs from
  `v9x_dd_block()`, earlier in the lifecycle than any previous mode-table work.
- `V9X_DD_MODE_COUNT` 7 -> 32 grows `v9x_dd_modes16[]` in the driver's DGROUP
  from 252 to 1152 bytes. The image links and the shared block still fits its
  4096-byte DPMI allocation (3096 bytes measured), but the 16-bit driver's
  DGROUP also carries the local heap (`option heapsize=1024`), and a 2 MiB card
  is not obviously the variable that should interact with that.
- `ValidateMode`'s new VRAM check reads `v9x_vbe_vram_reported`, which is only
  non-zero on a card whose size was actually established - 2 MiB here, 4 MiB on
  the guests. It should only ever refuse, never change a layout, but it is the
  one new code path whose behaviour genuinely differs between BARRY and the
  emulated targets.

A useful next measurement is the pitch the driver actually established, which
none of the current diagnostics publish for the `s3` family:
`v9x_active_pitch` and `shared->fb.pitch` at 1024x768x16 on BARRY versus on the
86Box Trio64. `V9XTRACE.EXE` wrote an empty `V9XTRACE.INI` on BARRY and
`V9XGDI.EXE` timed out at 800x600x32, so both want looking at too.

## Second, separate defect found on the way

`tools\diag\win16_driver_loader.c:153-163` asserts that `ValidateMode`
**rejects** 1280x1024x8, as its "an unsupported mode was incorrectly accepted"
check. That mode is now supported, so the probe exits 5 - and
`update-associated-driver.ps1` runs it as its preflight, so **every driver
deploy on this branch fails preflight once the branch driver is installed.** The
hardcoded mode has to come from something that is genuinely absent from the
family table. The branch has not been fixed for this yet; the control deploy
above used a locally patched copy asserting 1600x1200x8 instead.

## Lesson, and it is the D5 lesson again

Every GDI-side check passed on BARRY while the display was wrong: `enable-ok`,
a valid hardware report, and `ChangeDisplaySettingsA` returning success for a
mode that renders as garbage. The thing that caught it was looking at a
screenshot, and the thing that proved it was a *pre-existing screenshot to
compare against*. The 86Box matrix - 22 passing mode checks across two chips -
said nothing about it.
