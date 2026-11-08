# Feature gap: the VBE family has no PAT route to write-combining

Date: 2026-11-08. Status: implemented, unreleased; measured on the
i5-4570 ([record](../decisions/2026-11-08-vbe-pat-write-combining.md)),
field machines not yet run. Raised by a field report
([GitHub issue 15](https://github.com/michaeldale/velocity9x/issues/15),
also filed as
[issue 25](https://github.com/michaeldale/velocity9x/issues/25)).

## Seen

On a FirePro W9000 in an X99 board (Xeon E5 v4), the 0.39.0 `vbe`
package booted with `MtrrWrite=refused` in `V9XBOOT.INI`, so the screen
stayed uncached. The reporter says the desktop still feels responsive at
1920x1080x32. Their `Mtrr=` lines came the same day: `r=7`, see below.

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

## The W9000's refusal reason (reply of 2026-11-08 02:08 UTC)

    Mtrr=cpu=000f cap=00002d0a def=00000c06 n=8 r=7 s=0 b=00000000 z=00000000
    Mtrr0=80000000 80000800
    Mtrr1=60000000 e0000800
    Mtrr2=00000000 00000800 hi
    Mtrr3=5f000000 ff000800
    Mtrr4=e0000004 f0000800
    Mtrr5..7 unused
    MtrrWrite=refused

`r=7` is `V9X_MTRR_DEFAULT_NOT_UC`: `def=c06` enables the MTRRs with a
default type of WB (6). The BIOS describes memory as "everything is WB
unless a range says otherwise" and carves out the holes with UC pairs:
2 GiB at 8000_0000 (pair 0), 512 MiB at 6000_0000 (pair 1), 16 MiB at
5F00_0000 (pair 3), and a WT pair over E000_0000-EFFF_FFFF (pair 4),
which pair 0's UC overrides. The frame buffer address was not in the
reply, but a PCI BAR on this board sits in the 2-4 GiB hole, so the
frame buffer is almost certainly inside pair 0's UC range. Three pairs
are free, yet the plan refuses before it reaches the overlap test,
because the default-type test comes first.

So the W9000 is the same shape as the HD 4600 (an explicit UC range over
the window), reported under a different reason. **`v9x_pat_plan_wc` as
written would not act on it**: it only accepts 11 and 12. By the SDM's
effective-type table a PAT WC page is WC whatever the MTRR type, so a
`vbe` PAT route should also accept 7. Whether to add 7 to the PAT plan
or to reorder the MTRR plan so an explicit UC overlap reports 11 first
is a design choice still open; either needs a host test first.

## Implemented (same day)

See the [record](../decisions/2026-11-08-vbe-pat-write-combining.md).
The PAT plan now also accepts 7. Two statements above were wrong:
the mini-VDD's PAT verbs were Intel-only (behind
`V9X_INTEL_MMIO_FINGERPRINT`), not linked by every family, and so were
`runtime.asm`'s callers; both are now built for vbe too. On the i5-4570
the `vbe` package boots with `MtrrWrite=refused` and
`PatWrite=ok pages=00001000`, and FbWrite goes from 66 to 11,357 MB/s
against `NOWC.TXT`. The first two "Done when" points are met there; the
third (an MTRR-route machine unchanged) rests on the code: the fallback
is only called after a refusal.

Two more field machines fit: the W9000 (`r=7`, its reply gave
`Aperture=... b=e0000000`, inside pair 0's UC range and pair 4's WT
range) and the GTX 970 of
[issue 17](https://github.com/michaeldale/velocity9x/issues/17)
(Sandy Bridge, `r=11`, frame buffer at EF000000 inside a UC pair over
E0000000-FFFFFFFF).

## Not known

- Whether PAT behaves the same on AMD CPUs and on the other chipsets
  `vbe` runs on. Only the i5-4570 has measured it.
