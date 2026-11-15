# GeForce 210 (GT218): measure the write-combined BAR1 mapping, and keep it only if the engine agrees

Date: 2026-11-15. Status: done 2026-11-15, measured and kept: [../decisions/2026-11-15-gt218-write-combining.md](../decisions/2026-11-15-gt218-write-combining.md).

Machine: W9QI0DAD (10.0.1.172:9869 while it shares the NIC with A8U4I5),
AMD FX-6300, Gigabyte SB700 board, ASUS GeForce 210 `10DE:0A65` subsys
`1043:83F4` rev A2, on the nvidia package built from origin/main
`19ec1531` since boot 8 (2026-11-15).

Precedents, same mechanism: [vbe](../decisions/2026-11-07-vbe-write-combining.md),
[Trinity](../decisions/2026-11-08-trinity-write-combining.md) (an engine
card), [Rage 128](../decisions/2026-10-24-rage128-write-combining.md).
Mechanism: [write-combining-stage-b.md](write-combining-stage-b.md).

## Where it stands

Write-combining is already on for this card; it has never been measured.

- `gt218_hw16.c` opts in with `V9X_HW16_WC_VRAM`. Its comment still reads
  "Over BAR1 it is unmeasured", from boot 83 on M-BXMFA8
  ([BAR1 heap record](../decisions/2026-11-11-gt218-bar1-heap.md), "Open").
- The window is the full 128 MiB mapping: the family's
  `measure_video_memory` hook makes the VRAM figure 128 MiB, so
  `WC_VRAM` and `WC_MAPPING` give the same pair here.
- W9QI0DAD boots 8 and 11 (`V9XBOOT.INI`):
  `Mtrr=... r=0 s=3 b=c0000000 z=08000000`,
  `MtrrWrite=ok s=3 b=c0000001 m=f8000800`, `ApertureFromBar=b=c0000000`.
  The BIOS ranges are WB RAM (MTRR 0-1) and a 2 MiB UC hole at
  `BFE00000` (MTRR 2); none touches `C0000000`. The record is fresh, not
  the stale line Trinity's 0.40.0 boot left behind: MTRR 3 reads empty
  before the write, and the mask matches the plan line's `z=`. A Windows
  restart on this board clears the MTRRs, so plain restarts are enough for
  the on/off comparison (no `reset`, no power cycle).
- The CPU writes the engine's data through this mapping, so the drains
  matter. `eng_nv50.c` drains before every kick (lines 198, 275, 671),
  the 16-bit GDI arm calls `V9XWCDRAIN` (`runtime.asm`) before its kick,
  and the shared Unlock and Flip drains cover Lock writes. Before
  `19ec1531`, every engine run (M-BXMFA8 boots 108-127) also ran with
  this pair on, so the engine has worked under WC. That shows it works,
  not how much WC gains.

So this plan measures. It does not build anything new. If WC's numbers
hold up and the engine checks match with WC off, the only code change is
the comment.

## Steps

### 0. Preconditions

- Agent 0.18.0 or newer (it is).
- dispbench tools on the guest (`C:\everything\dispbench`,
  `scripts\run-bench.ps1 -ConfigId w9qi0dad-gt210-v9x-nvidia -Environment
  metal -EndpointHost 10.0.1.172`). This machine has no dispbench config
  yet: add one, following the existing `metal` entries.
- V9XD32 (`scripts\build-d3d32-probe.ps1`) put in `C:\V9XDIAG`, with
  V9XDDP and V9XGDI from the installed package's job folder
  (`C:\V9XREMOTE\JOBS\w9qi0dad-nvidia-19ec1531`).
- Record the installed files' hashes. Every run in this plan uses the same
  `19ec1531` set, and only `NOWC.TXT` changes.

### 1. WC off (baseline)

1. `put` an empty `C:\V9XDIAG\NOWC.TXT`, `reboot -DismissLogon`.
2. Confirm `MtrrWrite=off-marker` and `Mtrr3=00000000 00000000` in this
   boot's `V9XBOOT.INI` (check the boot counter matches, as Trinity's stale
   line showed is needed).
3. dispbench `-Mode 1024x768x16 -Tools DBTIME,DBGDI,DBDD`, then the same
   at `1024x768x32` (the GT218 row has 32 bpp modes; switch with
   `V9XMSW /set:1024x768x32`, and back afterwards).
4. Engine checks at 16 bpp, in this order, because `/accel` poisons the
   session on purpose:
   V9XDDP (both `V9XDD.INI` and `V9XDD2.INI`), V9XD32 `/bpp16` and 32,
   V9XGDI `/auto`, then V9XGDI `/accel` last.
5. 3DMark 2000 default run once, from C:\data's installer if it is not
   installed yet. Record the score, but don't compare
   it (scores are recorded, never chased); the frame grabs and the
   timeout/refusal counters are the evidence.

### 2. WC on

Rename `NOWC.TXT` away (a plain rename by the agent, not a delete;
`NOWC.OFF` as on A8U4I5), `reboot -DismissLogon`, confirm
`MtrrWrite=ok s=3 b=c0000001 m=f8000800`, then repeat steps 1.3-1.5
unchanged.

### 3. What decides it

- **Speed:** FbWrite, FillRect, BitBlt-to-screen and DirectDraw
  system-to-video Blt, off against on. The precedents gained 4.8 to 79
  times on FbWrite; a GT218 behind PCIe x16 should land near the HD 8570's
  ~9.5 times, but that is a guess. GDI fills and copies now run on the
  engine (plan step 5.4), so FillRect may move less than FbWrite. That
  result is worth recording too.
- **Correctness:** V9XDDP `_Ok` keys identical between off and on (the 30
  zero keys of boot 8 are the GT218's open list and should not change).
  V9XD32 results identical, `/auto` PASS both ways, 3DMark frames correct
  and 0 engine timeouts (`V9XN50F.TXT` absent) both ways.
- Any `_Ok` key or V9XD32 test that passes off and fails on is a missing
  drain. Find the write path (a Lock that skips Unlock's drain, or a push
  written after the last drain), fix it, write the host test where the
  path allows, and rerun step 2 only.

### 4. Record

- `docs/decisions/2026-11-xx-gt218-write-combining.md`: both tables, the
  boot records, the dispbench run ids, and anything the evidence disputes.
- `gt218_hw16.c`: replace "Over BAR1 it is unmeasured" with the measured
  figure and the record's name. No other code change unless step 3 finds a
  drain.
- If this lands on main, the next release's changelog line goes under the
  nvidia family.

## Out of scope

- WC over all 256 MiB of BAR1. The mapping is 128 MiB, and widening it is
  the heap question in the BAR1 record, not this one.
- PAT. The MTRR route works on this CPU, so no fallback is needed.
- The RIVA 128 row (no WC opt-in; its machine M-7V636C is a different
  design).
- M-BXMFA8. It belongs to an owner and runs the same code, so W9QI0DAD's
  result applies there by the same-design rule, with no owner time spent.

## Costs and risks

- Four restarts (two per state) at about 45 s each on this machine, plus
  dispbench runs at roughly 10 minutes per mode and state, and one
  3DMark 2000 run per state (~10 min). Expect about 1.5 hours.
- A missing drain shows up as wrong pixels or an engine timeout, not a
  hang: the engine gives up on any fault with no rebuild (`eng_nv50.c`).
  If the machine hangs anyway, Michael power-cycles it; record which
  check was running.
- 10.0.1.172 is shared with A8U4I5. Check `info` ComputerName is W9QI0DAD
  before every step.
