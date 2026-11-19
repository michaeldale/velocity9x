# Trinity's engine clock: the VBIOS boots it at 200 MHz of 760, and nothing raises it

Date: 2026-11-19. Machine: M-D7TFW4 (W9QI6DFC), A10-5700, HD 7660D
(Trinity, `1002:9901`). Read-only: the shadowed video BIOS copied with
`V9XTRACE -vbios` (64,000 bytes, AMD's code, not in the tree) and decoded
on the host. Nothing on the card was written.

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

## What raising it would take

- **304 MHz** is at the boot voltage (VDDC index 0): only the boot state's
  divider changes. Dentist divider for 304 MHz from a 3800 MHz VCO: 1250
  (`trinity_convert_did_to_freq`: DID 50, `50 x 25`). How the SMU takes a
  new divider for the state it is in, without DPM running, is not known.
- **608 and 760 MHz** need the SMU to raise VDDC (its VID per state),
  which is Linux's full DPM bring-up: the state table, `sumo_program_vc`,
  `SCLK_DPM_EN`. A wrong voltage on an APU is a hang or worse, on a
  remote-help machine the owner has to power-cycle.
- Either way the first step is a probe that reads the SMU's state table
  and `SMU_SCLK_DPM_CNTL` through the SMC index pair (200h/204h), to
  confirm the 200 MHz, before anything writes.
