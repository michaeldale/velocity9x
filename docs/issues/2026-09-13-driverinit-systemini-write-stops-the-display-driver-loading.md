# Writing SYSTEM.INI from DriverInit stops the display driver loading

Filed: 2026-09-13
Status: fixed in source, not yet confirmed on the machine
Machine: MICHAEL-NETBOOK, HP Mini 110-1000, 945GSE / GMA 950, Windows 98 SE
booted direct from the live USB stick
Found in: `intel-gma` package build `ebaa1ed-dirty`, the first Phase 4
first-write package, on its unarmed boot one

## Symptom

The desktop came up at 640x480 in sixteen colours. Nothing suggested a crash.

## What the evidence said

`C:\V9XDIAG` was copied off the stick. Every file in it was **byte-identical**
to the 12 September Phase 3 capture and carried that date. The single
exception was `V9XSYNC.INI`, written on the day of the test, reporting
`Build=ebaa1ed-dirty`.

So the new package was deployed correctly: `V9XSETP.DLL` is the new build and
ran from the `RunServices` key. Both `V9XDISP.DRV` and `V9XMINI.VXD` copied
off `WINDOWS\SYSTEM` hash-match the built package exactly. `SYSTEM.INI` still
read `display.drv=pnpdrvr.drv` with `[boot.description] display.drv=Velocity9x
Intel GMA 950 (945GSE)`, so the devnode binding was intact.

Two facts locate the failure precisely:

1. **`V9XBOOT.INI` was not updated at all.** The driver writes `libmain` into
   it from `v9x_display_boot_log()`, and
   `docs\issues`/`ddi.c:377` record that even a mini-VDD which fails to load
   still leaves `libmain` in the trace, which is how a 486 once came up on
   VGA. The absence of the marker puts the failure *earlier* than that write.
2. **`SYSTEM.INI` contained no `[Velocity9x]` section**, so the first write
   inside the only function that runs before the trace did not complete.

## Cause

`DriverInit` in `src\display16\loader.c` called
`v9x_intel_boot_arm_prepare()` as its first statement, before
`v9x_display_boot_log()`. That function's first action was

```c
v9x_intel_boot_set("IntelEnableThisBoot", "0")
```

which is `WritePrivateProfileString` into **`SYSTEM.INI`**, immediately
followed by `WritePrivateProfileString(0, 0, 0, "SYSTEM.INI")` to force the
profile cache to disk.

`DriverInit` is called by GDI while it is loading the display driver named in
`SYSTEM.INI`. The driver therefore asked Windows to flush and re-read that
file from inside the load it was servicing, on a machine whose disk I/O is
real-mode INT 13h through MS-DOS compatibility mode. `DriverInit` did not
return success, and Windows fell back to the INF's 4-bpp `vga.drv` row:

```
HKR,"MODES\4\640,480",drv,,vga.drv
```

which is the 640x480 sixteen-colour desktop that was observed.

The driver has years of evidence that it can write `C:\V9XDIAG` at this
point. It had none that it can write `SYSTEM.INI` at any point, and that
assumption was never stated or tested.

## Fix

Two changes, both chosen for stability over minimality after the trade-off
was put to Michael.

- **The arm state moved out of `SYSTEM.INI`.** It now lives in
  `C:\V9XDIAG\INTELARM.TXT`, declared in `diagpaths.h` beside every other
  diagnostic file, and read and written by `intel_boot16.c` and
  `intel_exec16.c` through the same profile API on a path the driver
  demonstrably owns. No Velocity9x component writes `SYSTEM.INI` any more,
  and neither does `scripts\arm-intel-phase4.ps1`, which previously rewrote a
  Win98 boot file on the stick to set up the experiment.
- **The boot trace goes first.** `v9x_display_boot_log()` now runs before
  `v9x_intel_boot_arm_prepare()`. A `libmain` marker on disk with nothing
  after it names the arm transaction as the failure, exactly as an absent
  marker names whatever runs before it. This defect cost a boot precisely
  because it left no trace.

The arm file also travels with the evidence: a returned `V9XDIAG` folder now
carries the state that authorised the run beside the capture it produced.

## Hypotheses killed

- "The mini-VDD API v6 binary fails to load." It may or may not; this boot
  never reached the point of asking. A failing mini-VDD leaves `libmain` in
  the trace and this boot did not.
- "Only some files were copied." All four hash-match the package.
- "The devnode binding was lost, like the 2026-09-12 function-1 install." The
  binding is intact in `SYSTEM.INI` and `[boot.description]`.

## Still unknown

Whether the v6 mini-VDD loads, and whether the Phase 4 sequence runs. The next
unarmed boot answers both, and now has a trace that says where it stopped.
