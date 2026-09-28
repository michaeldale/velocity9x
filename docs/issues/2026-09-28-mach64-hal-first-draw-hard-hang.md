# Mach64 HAL's first physical draw hard-hung the Gateway

Status: two causes identified and fixed; the fix is untested on hardware.
Machine: Gateway Solo 2150, Velocity9x bound, boot 13, 2026-09-28.

## What happened

With Velocity9x bound and the BARs back at their BIOS addresses (see
[`../decisions/2026-09-28-mobility-first-velocity9x-bind.md`](../decisions/2026-09-28-mobility-first-velocity9x-bind.md)),
`V9XDDP.EXE`, the DirectDraw/Direct3D probe, was started. The machine then
stopped answering ICMP and the agent, and needed a physical power cycle.
The probe wrote no report, so it is not known whether the first engine use
was a DirectDraw colour or depth fill or a Direct3D draw. The HAL routes
both to the Mach64 by engine type.

This was the first time the HAL's Mach64 code, rather than the diagnostic
VxD, touched the engine.

## Cause 1: three register offsets missing block 0's +400h

`include/velocity9x/ati_mach64_regs.h` defined `FIFO_STAT` `0x310`,
`GUI_TRAJ_CNTL` `0x330` and `GUI_STAT` `0x338`. All three are block 0
registers (X.Org indices C4h, CCh and CEh), so the correct values are
`0x710`, `0x730` and `0x738`. The values that were there are unrelated
block 1 locations.

`v9x_m64_fifo_free` read its free-slot count from the wrong word, so
`v9x_m64_reserve` could believe the FIFO had room it did not have and
overrun it. On this part, that is a known way to lock the bus. The state
stream also wrote `GUI_TRAJ_CNTL`'s value into block 1.

The diagnostic VxD had its own correct constants (`ATIE1_GUI_STAT equ
0738h`, `0730h` in `AtiE4StateOffsets`), which is why every physical scene
worked. No host test could see the mistake, because the builders and the
fake MMIO shared the same wrong names.

Found by computing every header offset from X.Org's `BlockIOTag` indices.
Only these three differed. `ONE_OVER_AREA` at `0x29C` is the deliberate
`_UC` trigger alias, the one the diagnostics used.

Fixed in the header. `test_offsets_match_diagnostic` now pins the three
status offsets at compile time and holds the texture state's 19 offsets to
the VxD's proven table. With the old `GUI_TRAJ_CNTL` value, the host build
fails.

## Cause 2: register block 1 disabled after a bare VBE boot

`BUS_CNTL` read `7333A001` under Velocity9x, against `7B33A001` under the
stock driver: `BUS_EXT_REG_EN` (bit 27) was clear. Block 1 holds every
setup-engine register. The stock driver had enabled it, and X.Org does too,
for every 264VT-or-later chip. `eng_mach64.c` now sets it once at
validation, after the chip identity check.

## Not established

- Which of the two causes hung the machine, or whether both contributed.
- Whether anything else a bare VBE boot leaves unset is also needed. The
  stock driver's initialisation is otherwise unknown.
- The next physical run should first confirm `BUS_CNTL` bit 27 is set with
  the read-only fingerprint. It should then exercise DirectDraw fills alone
  before Direct3D.
