# Mach64 HAL's first physical draw hard-hung the Gateway

Status: three causes identified and fixed. Causes 1 and 2 were confirmed
on hardware at boot 15; cause 3 has one clean run, at boot 19.
Machine: Gateway Solo 2150, Velocity9x bound, boots 13-19, 2026-09-28/29.

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

## Cause 3: a 2D fill ran with 3D state still live

With causes 1 and 2 fixed, fills and the first triangles passed at
boot 15. But two further `V9XDDP.EXE` runs hard-locked the machine. The
probe's new write-through step log placed the lock during or just after
`ZDepthFillHr`: the first DirectDraw depth fill after Z-enabled
Direct3D draws.

The evidence ruled out two alternatives. DirectDraw-only surface
traffic passed four passes (`V9XTXM.EXE`). The Z buffer does not sit at
the top of VRAM: it is at offset `0x180000`.

What was left: the HAL's engine fill wrote only its own registers, so
`Z_CNTL`, `ALPHA_TST_CNTL` and `SCALE_3D_CNTL` still held the last
draw's values. X.Org zeroes them before 2D work.
`v9x_m64_build_2d_mode` now emits that batch ahead of every fill. With
it, boot 19 ran the whole probe to `Result=COMPLETE`, with
`ZDepthFillOk=1` and `D3DZCompareOk=1`. Evidence is in
[`../probe/ati-rage-mobility-m-hal-d3d-2026-09-28/`](../probe/ati-rage-mobility-m-hal-d3d-2026-09-28/README.md).

The evidence supports the fix but does not isolate a single register.
Any one of the three, or the combination, may be the one that hangs the
engine.

## Not established

- Which of causes 1 and 2 hung the first run, or whether both contributed.
- Whether cause 3's fix holds across a cold boot. It holds for three
  runs on boot 19: the default run, `/mixed` and `/zprivate`.
- Whether anything else a bare VBE boot leaves unset is also needed. The
  stock driver's initialisation is otherwise unknown.
- The next physical run should first confirm `BUS_CNTL` bit 27 is set with
  the read-only fingerprint. It should then exercise DirectDraw fills alone
  before Direct3D.
