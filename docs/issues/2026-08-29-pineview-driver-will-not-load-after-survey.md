# The driver would not load again after the DOS survey, and safe mode was needed

Date: 2026-08-29
Status: **open, cause not established. The leading suspect is the mode sweep in
the installed build, not the survey.**
Machine: Intel Pineview, PCI `8086:A011`, 1024x600 panel, ~8 MiB VRAM,
VBE 3.0. Reported by CentaurHauls.
Collection: `claude\personal\v9x-centaurhauls-acer\` - `V9XDIAG` from a healthy
boot of the same install.

## Reported

> After running the survey tool from DOS and restarting I couldn't get this
> driver to load again, had to boot into safe mode and remove it.

This is the most serious thing in the report. The machine was left needing safe
mode, which is the outcome the recovery instructions exist for but not one a
user should reach by running a diagnostic.

## Why the survey is probably not the cause

`V9XSURV.EXE` runs `Access=query-only` and its safety gate refuses port writes,
descriptor loads and inline `INT 10h` - twelve mutations are rejected by the
gate's own self-test in `run-checks`. It writes one file. Nothing it does
survives a reboot.

## What the installed build does at every boot, which the survey does not

The collection says the installed driver is **`b8c5103-sweep`**, and
`VbeCache=... f=2107` has `SWEEP_RAN` (0x2000) set.

The sweep sets modes into the real video BIOS at `Device_Init`, on every boot.
On this machine that is thirty blind `4F02h` calls: 36 modes listed, 6 already
described, and the cap `V9X_STAGE1_SWEEP_MAX` is 32 with the cache cap at 64,
so nothing stops it before it has tried them all. `build-minivdd-skeleton.ps1`
says what that costs in its own parameter comment:

> it issues 4F02h into the real video BIOS at Device_Init, which is a heavier
> call than the collection's and can hang a boot on a BIOS that does not come
> back

So every boot of this build is a fresh run of thirty mode sets on a BIOS that
refuses to describe any of them. A boot that does not come back from one of
those is exactly "the driver would not load again", and the sequencing in the
report - it worked, then after a restart it did not - fits a per-boot hazard
better than it fits a diagnostic that changed nothing.

**Not established.** The survey may still be implicated, and coincidence is
possible. What would settle it: `C:\V9XDIAG\V9XBOOT.INI` from the failed boot.
`Stage=` records the furthest point reached and survives a power cut, so it
names the step that did not return.

## What to do now

**Stop running the sweep build on this machine.** Its value there is now
measured at zero - see
`docs\decisions\2026-08-28-pineview-vbe-mode-list.md` - so it is thirty
BIOS mode sets per boot buying nothing. A plain package carries none of them.

Beyond that, the sweep wants a guard it does not have: it is a boot-time
experiment with a known hang mode and no way for a machine that hit it to skip
the sweep on the next boot. A "swept once, do not sweep again" latch in
`V9XBOOT.INI`, or a sweep that runs on request rather than at every
`Device_Init`, would make the hazard survivable. Neither exists.

## Related

- `docs\issues\2026-08-28-survey-null-assignment.md` - the survey's own defect,
  and now partly retested on this machine: the message still prints, the freeze
  does not reproduce.
- `docs\decisions\2026-08-28-pineview-vbe-mode-list.md` - why the sweep gains
  nothing here.
