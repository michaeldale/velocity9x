# Trinity's engine clock: 200 MHz of 760; the driver now sets 304 MHz, the most at the boot voltage, through the VBIOS's SetEngineClock

Date: 2026-11-19. Machine: M-D7TFW4 (W9QI6DFC), A10-5700, HD 7660D
(Trinity, `1002:9901`). Read-only at first: the shadowed video BIOS copied with
`V9XTRACE -vbios` (64,000 bytes, AMD's code, not in the tree) and decoded
on the host; then the SMU read below, whose one write is the SMC index
register, put back as found.

## Why

`V9XHW.INI` says `ClockStatus=unavailable`. The Oland ran at its BIOS's
300 MHz of 1050 until the driver set clocks
([record](2026-11-18-oland-clocks-left-by-the-bios.md)), and the HD 4000
at its BIOS's request until it asked for RP0
([record](2026-11-19-ivybridge-render-clock-rp0.md)). On Trinity the
engine clock belongs to the APU's SMU: Linux `trinity_dpm.c` programs a
table of DPM states (`SMU_SCLK_DPM_STATE_n_CNTL_0`, a dentist divider and
a VID each) through the SMC index pair and turns `SCLK_DPM_EN` on. This
driver does none of it, so the SMU stays in its boot state.

## What the tables say

- FirmwareInfo v2.2: `ulDefaultEngineClock` **200.00 MHz**,
  `ulDefaultMemoryClock` 667.00 MHz.
- IntegratedSystemInfo v1.7: `ulBootUpEngineClock` **200.00 MHz**,
  `ulDentistVCOFreq` 3800.00 MHz, `ulBootUpUMAClock` 667.00 MHz,
  `ulMinEngineClock` 200.00 MHz. (The decode past those fields gave
  implausible values; its offsets are unchecked and nothing rests on them.)
- PowerPlay v6.1, its clock-info array (Sumo layout: `usEngineClockLow`,
  `ucEngineClockHigh`, VDDC index):

| Entry | Engine clock | VDDC index |
|---|---:|---:|
| 3 | 200 MHz | 0 |
| 2 | 304 MHz | 0 |
| 0 | 608 MHz | 2 |
| 1 | 760 MHz | 3 |

  The performance states list entries 0 and 1, or 2, 0 and 1; two others
  list only entry 3.

So the GPU most likely draws at 200 MHz, 26 % of the 760 MHz its table
allows. That rests on the SMU holding its boot state with DPM off, as
`trinity_dpm.c` implies; the clock was not read from the SMU itself (that
needs the SMC index pair, which the read-only probes do not use).

## The SMU, read (same day)

`V9XEGMM /smu` (new: `tools\diag\evergreen_mmio_probe*`). EGMM.VXD takes
an offset flagged as an SMC address only on Trinity and only in the
SMU's sclk DPM block (1F000h-1F11Fh) or `TN_CURRENT_GNB_TEMP` (1F390h);
it sets `TN_SMC_IND_INDEX_0` (200h), reads `TN_SMC_IND_DATA_0` (204h) and
puts the index back as found, with interrupts off. That index is the
probe's one write. Plain MMIO beside it: `GENERAL_PWRMGT` (670h) and
`TARGET_AND_CURRENT_PROFILE_INDEX` (684h). Run twice at the desktop, boot
77; evidence in
[`../probe/trinity-d7tfw4-smu-clock-2026-11-19/`](../probe/trinity-d7tfw4-smu-clock-2026-11-19/).

| Register | Value | Reading |
|---|---|---|
| `GENERAL_PWRMGT` | `0000FE04` | `GLOBAL_PWRMGT_EN` clear |
| `TARGET_AND_CURRENT_PROFILE_INDEX` | `00000000` | current and target state 0 |
| `SMU_SCLK_DPM_CNTL` (SMC 1F100h) | `00000000` | `SCLK_DPM_EN` clear |
| `SMU_SCLK_DPM_STATE_0..7_*` (40 registers) | all `00000000` | no state valid, no divider, no VID |
| `SMU_SCLK_DPM_TT_CNTL`, `_TTT`, `SMU_S_PG_CNTL` | `00000000` | |
| `TN_CURRENT_GNB_TEMP` (SMC 1F390h) | `00000205` | non-zero: the pair decodes (about 15 C by Linux's `(t / 8) - 49`; not checked) |

The first run, without the temperature, read 44 zeros, which could have
been a pair that decodes nothing; the second shows it is not.

So the BIOS leaves the SMU's sclk DPM unprogrammed and off. The 200 MHz
it boots at is set some other way (the VBIOS's own init); the SMU table
does not say what the engine runs at, and nothing here measures the clock
directly. The 200 MHz stands on the VBIOS's two tables alone.

## The clock register, found and set (same day, boot 77)

Simulating the VBIOS's tables (the Oland work's `atom_sim.py`, in the
scratchpad with the image) showed where the clock is, not in the SMU:
- **GetEngineClock** reads MMIO 600h and turns its low byte, a dentist
  divider ID, into a clock.
- **SetEngineClock** runs ComputeMemoryEnginePLL (304 MHz gives divider
  1250, ID 32h), waits for MMIO 604h bit 0, writes the ID to 600h, and
  waits for 604h bit 0 again. It sets no voltage.

`V9XEGMM /smu` with 600h and 604h added as plain reads: **600h = 46h**,
604h = 9. ID 46h is 200.00 MHz by `trinity_convert_did_to_freq` from the
3800 MHz VCO, and the simulator's GetEngineClock against these registers
returns 20000 (200 MHz). So the engine runs at 200 MHz, now from the card
and not only from the tables. Re-simulated with these values,
SetEngineClock(304 MHz) writes exactly 32h to 600h.

EGMM.VXD gained DIOC 2 (`V9XEGMM /sclk:XX`): that handshake, Trinity
only, refused for any ID but 46h (200 MHz) and 32h (304 MHz), the two at
the boot voltage, and refused unless 604h is ready first. Bits above the
ID's byte in 600h are kept (they read 0).

`/sclk:32`: 600h 46h to 32h, ready before after 0 polls and after after 1
(`Result=WRITTEN-READY`). The machine stayed up. At 304 MHz, the
group-flush build (`c0c9381b`'s HAL) as installed:

| | 200 MHz (boot 77, earlier) | 304 MHz |
|---|---:|---:|
| V9XD32, 32 bpp | PASS | PASS, failures 0 |
| 3DMark 2000, default | 4130 | **4227** |
| 3DMark 2001 SE, saved Low | 2462 | **2605** |

One run each. After both benchmarks 600h still read 32h, and
`TN_CURRENT_GNB_TEMP` went from 201h to 219h. A reset puts the BIOS's
200 MHz back; the driver did not set the clock yet (it does now, below).

3DMark 2000 moved 2 % for a 52 % clock rise, 2001 SE 6 %: at 304 MHz
neither is limited mainly by the engine clock. Whether 608 or 760 MHz
would do more is open: those need VDDC raised, which SetEngineClock does
not do.


## In the driver (boots 78 and 79)

As the Oland's clocks are set (`si_clock_apply.c`):
- `tn_clocks.c` (pure, host-tested): FirmwareInfo's default engine clock
  is the boot clock; the VDDC index of the PowerPlay level at that clock
  is the boot voltage; the target is the highest engine clock of any
  state's level at or below that index. No level at the boot clock, a
  table of another revision, or an index out of the array: nothing is
  raised.
- `tn_clock_apply.c` (HAL): the VBIOS's GetEngineClock and
  SetEngineClock run by the AtomBIOS interpreter against BAR2's window,
  at Evergreen engine validation (`eng_eg.c`) before the DMA block is
  reset, Trinity only. A clock already at its target is left alone; a
  table that stops part way leaves the engine unclaimed. The outcome,
  and the last validation that set the clock, go to
  `C:\V9XDIAG\V9XCLK.TXT`.

Host tests first, seen failing: a synthetic Sumo-layout PowerPlay in
M-D7TFW4's shape, and, with `V9X_TRINITY_ROM` and `V9X_TRINITY_INI`
naming the scratchpad image and boot 77's `/smu` snapshot, the real
tables: GetEngineClock 20000, the policy's 30400, and SetEngineClock
writing 32h to 600h. Both pass; `run-checks.ps1` passed.

Installed by `run-release-checks.ps1 -Machine trinity`, each boot after
a reset; evidence in
[`../probe/trinity-d7tfw4-clock-driver-2026-11-19/`](../probe/trinity-d7tfw4-clock-driver-2026-11-19/)
(boot 78) and
[`../probe/trinity-d7tfw4-clock-lastset-2026-11-19/`](../probe/trinity-d7tfw4-clock-lastset-2026-11-19/)
(boot 79):

| | boot 78 | boot 79 |
|---|---|---|
| V9XD32 at 32 and 16 bpp | PASS, PASS | PASS, PASS |
| other release probes | pass | pass |
| `V9XCLK.TXT` | `already-set` at 30400 (no LastSet fields yet) | `already-set`; `LastSet=set` 20000 to 30400 |
| 600h after | 32h | |
| 3DMark 2000 | 4228 | |
| 3DMark 2001 SE | 2628 | |

Boot 79 shows the order: the reset put the VBIOS's 200 MHz back, the
first validation set 304 MHz, and later revalidations (mode changes,
DirectDraw programs) found it set. The scores match the hand-set run's
(4227 and 2605).

## Still open

- **608 and 760 MHz** need VDDC raised (index 2 and 3), which neither
  SetEngineClock nor anything here does: Linux's DPM bring-up programs a
  VID per SMU state (`trinity_program_power_level`, `sumo_program_vc`).
  A wrong voltage on an APU is a hang or worse, on a remote-help machine.
- Whether 3DMark gains much more from the clock: 52 % more clock gave
  2 % and 7 %.
- Power and temperature at 304 MHz over a long run: not measured.
