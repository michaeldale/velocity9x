# Mach64-class cards decline every flip, so full-screen DirectDraw flickers

Date: 2026-10-03. Reported by MarxVeix on an ATI 3D Rage XL AGP
(`1002:474D`, 8 MB, 0.10.0 `cfa3654`). Evidence:
`docs/probe/rage-xl-agp-marxveix-2026-10-03/`. Status: open; the
diagnosis is from the source, and not measured.

## Symptom

DxDiag (DirectX 7a) flickers in its full-screen DirectDraw test and in
the software and Direct3D tests. So does DX7's full-screen test. The
flicker cannot be seen in screenshots, and the DxDiag tests report
success.

## Probable cause

The Rage XL is bound as a Rage Pro-class alias and runs the Mach64
engine (`V9XHW.INI`: `Direct3D=hardware-mach64`, engine type 4). Only
the Rage IIC declares `V9X_DD_ENGINE_CAP_FLIP` (`rage_iic_hw16.c:77`).
The Mach64 class declares `V9X_DD_ENGINE_CAP_D3D` alone
(`mobility_hw16.c:84`), and `m64_scanout.c` serves `ATI_RAGE2` only. So
`v9x_can_set_display_start` is false, every Flip is declined, and
DirectDraw presents by copying the back buffer to the front, unsynced
with the vertical blank. That is the Rage IIC's state before 0.10.0
(`docs/decisions/2026-10-03-rage-iic-scanout-start.md`).

The snapshots sent were taken at boot (all counters zero), so
`FlipDeclined` from a flickering run is not in the evidence.

## Reproduced on A8U4I5 (2026-10-03)

With a Rage XL PCI (`1002:4752`) fitted, V9XDDP's run left
`FlipDeclined=23` in the snapshot. Every flip it asked for was declined
(`docs/probe/a8u4i5-rage-xl-pci-2026-10-03/first-boot/V9XSNA7.INI`). The
flicker itself has not yet been watched on the monitor.

## Fixed for the Rage XL PCI (2026-10-04)

The Rage IIC's CRTC flip is enabled for `4752`. In 3DMark 99, 4,209
flips were handled and none declined, the reported refresh went from
"VSync Off" to 59 Hz, and Michael watched the monitor: "looks good"
(`docs/decisions/2026-10-04-rage-xl-page-flips.md`). The reporter's Rage
XL AGP (`474D`) and the rest of the class are not enabled yet: each
needs a watched run. DxDiag's full-screen test was not rerun.

## Next (before the fix)

- Measure the Mach64-class CRTC as the Rage IIC was measured (ATIRX
  `/crtc`): whether `CRTC_OFF_PITCH` moves the scanout in the VBE modes,
  and when a write takes effect. Candidates are a Rage XL PCI (owned) or
  the Gateway's Rage Mobility (10.0.1.22). Its panel CRTC may differ from
  a desktop part's.
- Then extend `m64_scanout.c` and the cap to the Mach64 type for the
  chips measured.
- Ask the reporter for a V9XTRACE snapshot taken right after a
  flickering test, to confirm `FlipDeclined` is non-zero.
