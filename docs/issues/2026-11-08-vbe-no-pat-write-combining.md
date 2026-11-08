# Feature gap: the VBE family has no PAT route to write-combining

Date: 2026-11-08. Status: open, feature gap. Raised by a field report
([GitHub issue 15](https://github.com/michaeldale/velocity9x/issues/15),
also filed as
[issue 25](https://github.com/michaeldale/velocity9x/issues/25)).

## Seen

On a FirePro W9000 in an X99 board (Xeon E5 v4), the 0.39.0 `vbe`
package booted with `MtrrWrite=refused` in `V9XBOOT.INI`, so the screen
stayed uncached. The reporter says the desktop still feels responsive at
1920x1080x32. Their `Mtrr=` lines were asked for on 2026-11-08, so the
refusal reason (`r=`) is not known yet.

## Why

Since 0.39.0 the `vbe` family write-combines the linear frame buffer
through one variable MTRR pair, and only that. The plan
(`src/common/mtrr.c`) refuses when a valid range already overlaps the
window (`V9X_MTRR_ALREADY_COVERED`, 11) or every pair is in use
(`V9X_MTRR_NO_SLOT`, 12). Server and later desktop BIOSes often cover the
PCI hole with an explicit UC pair or use all the pairs.

The HD 4600 has the same BIOS shape (all eight pairs used, an explicit
UC pair over its aperture, `r=11`). It has a second route: a PAT entry set
to WC, and the aperture's page table entries pointed at it. It boots with
`MtrrWrite=refused` and `PatWrite=ok`, which the 0.40.0 field report
V9X-4462GX also shows
([record](../decisions/2026-10-28-haswell-pat-write-combining.md)). By the
Intel SDM's effective-type table, a page whose PAT type is WC is WC inside
an MTRR range of type UC, which is why the route works where the MTRR one
cannot.

## What exists

- The policy is shared and host-tested: `v9x_pat_plan_wc` in
  `src/common/pat.c`, with `tests/host/test_pat.c`. It acts only on
  reasons 11 and 12, and checks CPUID, the MSRs and PAT support first.
- The ring-0 side is the mini-VDD's PAT calls (`V9xMiniPatInfo` and the
  cover call). Every family links them.
- The hook is `V9X_HW16_DEVICE.write_combine_fallback`. `enable16.c`
  calls it when the MTRR plan refuses, honours `C:\V9XDIAG\NOWC.TXT`, and
  lets it write its own `PatWrite=` line.
- Only the HD 4600 devices set the hook (`hsw_hw16.c`). The `vbe` device
  would cover the linear frame buffer mapping, with the window clamped to
  the VRAM the BIOS reports, as its MTRR path does.

## Done when

- A `vbe` machine whose MTRR plan refuses with 11 or 12 boots with
  `PatWrite=ok` and an intact desktop.
- A fill or upload measurement shows the gain against the same machine
  with `NOWC.TXT`.
- A machine where the MTRR route works is unchanged.

The i5-4570 (`r=11`) can test the `vbe` package for the first point.

## Not known

- Whether PAT behaves the same on AMD CPUs and on the other chipsets
  `vbe` runs on. Only the i5-4570 has measured it.
- The W9000 machine's refusal reason. If it is not 11 or 12, PAT does not
  apply to it.
