# The Oland ran at its BIOS's 300/300 MHz; the driver now sets 400/1000 from the VBIOS's own tables, 2001 SE 4234 to 4797

Date: 2026-11-18

Machine: W9QI0DAD (AMD FX-6300, Radeon Oland XT `1002:6610`, 2 GiB DDR3),
boot 112, the 0.55.0 code's `ati` package. Evidence:
[`../probe/oland-clocks-2026-11-18/V9XSIMM-boot112.INI`](../probe/oland-clocks-2026-11-18/V9XSIMM-boot112.INI).

## Why

Both Oland plans list "the engine clocks the BIOS leaves" as unmeasured:
the driver has no power-management path, so the card runs at whatever the
VBIOS set. The HD 4600 ran at its BIOS's 400 MHz, not its 1150 MHz RP0,
until the driver asked
([record](2026-11-14-haswell-render-clock-rp0.md)), and that alone was
worth about a third of its 3DMark 2001 SE score.

## How

V9XSIMM (`tools\diag\si_mmio_probe*`) gained the engine PLL (SPLL), memory
PLL (MPLL), reference-clock and power-management registers in its read
list and its VxD's windows. All of them are plain MMIO on Southern Islands
(radeon `sid.h`; `si_dpm.c` reads them with `RREG32`). None is an index and
data pair or a trigger, and nothing is written to the card. Run once on the
desktop, no 3D program running.

## What the registers say

| Register | Value | Fields |
|---|---|---|
| `CG_SPLL_FUNC_CNTL` | `80400000` | REF_DIV 0, PDIV_A 4, not bypassed, not in reset or sleep |
| `CG_SPLL_FUNC_CNTL_2` | `00000001` | SCLK_MUX_SEL 1 |
| `CG_SPLL_FUNC_CNTL_3` | `100B1C70` | FB_DIV `B1C70` = 728,176, DITHEN |
| `CG_CLKPIN_CNTL` | `00000001` | XTALIN_DIVIDE clear: the 27 MHz crystal undivided |
| `GENERAL_PWRMGT` | `00000404` | GLOBAL_PWRMGT_EN and STATIC_PM_EN clear: no DPM |
| `TARGET_AND_CURRENT_PROFILE_INDEX` | `00000000` | state 0 |
| `MPLL_FUNC_CNTL_1` | `00584BC1` | CLKF 88, CLKFRAC 1212, VCO_MODE 1 |
| `MPLL_AD_FUNC_CNTL` | `00000002` | YCLK_POST_DIV 2 |
| `MPLL_DQ_FUNC_CNTL` | `00000012` | YCLK_SEL 1, YCLK_POST_DIV 2 |
| `MC_SEQ_MISC0` | `B000062B` | memory type 11, DDR3 |
| `CG_MULT_THERMAL_STATUS` | `00004624` | 35 C |

**Engine clock, about 300 MHz.** `si_dpm.c` writes FB_DIV as
`sclk x (REF_DIV + 1) x PDIV_A x 16384 / reference`. So
`27 MHz x 728176 / 16384 / 1 / 4` = **300.0 MHz**, from a 1200 MHz VCO.

**Memory clock, about 300 MHz** (lower confidence). With `ci_dpm.c`'s
relation for non-QDR memory, `VCO = mclk x 2 x 2^post_div`, and a VCO of
`27 MHz x (88 + 1212/4096)` = 2384 MHz, it gives 298 MHz, or about 600 MT/s
on DDR3.

**No power management runs**: DPM is off, so nothing will raise either
clock under load.

## What this does not establish

- **The rated clocks of this board.** Its VBIOS's PowerPlay table was not
  read. R7 250 boards are commonly sold at about 1000-1050 MHz engine and
  900 MHz DDR3 (1800 MT/s), so this card may run at about 30 % of its
  engine clock and a third of its memory rate. That comparison is from
  general specifications, not from this card.
- **Two assumptions in the decode:**
  - The 27 MHz reference for both PLLs.
  - That SCLK_MUX_SEL 1 selects the SPLL's output. `si_dpm.c` programs 2
    when it sets a state. What 1 selects is not in the headers here.
- **The memory decode** rests on CIK's formula; the MPLL fields are the
  same on SI.
- **Under load:** one read at the desktop. With no DPM running nothing
  should change, but that was not read during a 3DMark run.

## The VBIOS's own tables (same day)

`V9XSIMM /rom` copied the 64 KiB video BIOS from its C0000h shadow
(`V9XROM.BIN`, not in the tree: it is AMD's).

- **FirmwareInfo v2.2:** default engine clock 300.00 MHz, default memory
  clock 299.00 MHz, 27 MHz reference, boot VDDC 900 mV. These are the
  registers' clocks exactly, which confirms the decode above.
- **PowerPlay v6.1:** the performance state's levels are 300/300 MHz at
  800 mV, 400/1000 at 900, 1000/1000 at 1150, and 1050/1000 at 1200
  (engine/memory, VDDC). Other entries list 500 MHz engine at 800 mV.
- **VoltageObjectInfo v3.1:** VDDC through SVI2, which on SI the SMC
  drives. We do not run the SMC, so the voltage stays at 900 mV.

So the card runs at 29 % of its top engine clock and 30 % of its memory
clock. At the boot voltage the table allows the engine up to 500 MHz.

## Raising the engine clock to 500 MHz

`atom_sim.py` (in the evidence folder, with `atom_seq.py`, which turns a
trace into a /seq file, the sequences replayed and the snapshots) interprets the
VBIOS's command tables against a register file seeded from V9XSIMM.
Its semantics are Linux `atom.c`'s, as recalled.

- **Self-checks:**
  - GetEngineClock returns 29,999 (10 kHz units) from the live
    registers.
  - SetEngineClock at 300 MHz reproduces the card's FB_DIV `0B1C70`.
  - At 400, 500 and 1050 MHz it gives FB_DIV `0ED090`, `1284B0` and
    `1371C0`, post-dividers 4, 4 and 2. That is
    27 x FB / 16384 / PDIV = 400.0, 500.0 and 1050.0 MHz.
- **The sequence:**
  1. The SPLL goes into bypass, with a change request and a poll on
     SPLL_STATUS bit 1.
  2. The dividers are set and pulsed.
  3. The SPLL leaves bypass.
  4. A table recomputes the MC arbiter's DRAM timings, refresh rate and
     burst time for the new engine clock.
- **Arbiter discrepancy, not resolved:** at the unchanged 300 MHz the
  simulated arbiter timings are one count above the card's in every
  byte. That is either a rounding difference in the interpreter or a
  different formula at boot. Either way it is in the slower, safer
  direction, and the refresh rate matched exactly.

SIMM.VXD gained its one write path, DIOC 2 (`V9XSIMM /seq:FILE`): a list
of writes and polls, refused whole unless every write is to the SPLL or
those four arbiter registers and every poll is of SPLL_STATUS, each poll
bounded at a million reads. On W9QI0DAD, boot 112, desktop idle:

| step | result |
|---|---|
| a list with a write to 2000h | refused, nothing written |
| the 300 MHz sequence (no change) | 34 ops; the poll after the divider update took 211 reads; machine up |
| 400 MHz | FB_DIV `0ED090`; V9XD32 as before (only T19 FfSpecFog, the known issue) |
| 500 MHz | FB_DIV `101284B0`; V9XD32 as before; 35 C |
| 3DMark 2001 SE, grabs, at 500 MHz | **4379**, against **4234** at 300 MHz (boot 110, same driver code); frames whole |

**+3.4 % for 67 % more engine clock.** The engine clock is not what holds
this card back. Car Chase Low runs at the refresh in most grabbed frames,
and the memory clock is still 300 MHz DDR3. The clock lasts until the next
boot; nothing in the driver sets it.

## Raising the memory clock to 1000 MHz

SetMemoryClock (table 11) is a full DDR3 frequency change:

1. The memory clients are blacked out (`MC_SHARED_BLACKOUT_CNTL`) and the
   CPU's access to VRAM is turned off (`BIF_FB_EN` 0).
2. The DRAM is commanded through `MC_SEQ_CMD`.
3. The memory PLL is reprogrammed, with an `MPLL_CTLREQ` /
   `MPLL_CHG_STATUS` handshake like the engine PLL's.
4. The DLL is reset twice, the sequencer's RAS/CAS/misc timings and the
   DRAM mode registers (`MC_PMG_CMD_MRS`, `_EMRS`, `_MRS1`) are loaded for
   the new rate, the arbiter is recomputed, and access is restored.

Altogether 93 writes, 15 delays of 2-100 us and one PLL poll.

- **Read first:** V9XSIMM gained the 27 sequencer, DLL and MPLL
  registers the table reads. They are plain registers; the sequencer's
  two index/data pairs (2A44h, 2B3Ch) stay out.
- **No-change check:** seeded with them, the simulated SetMemoryClock at
  the unchanged 299 MHz ends with every register at its starting value.
  The two exceptions are its own model of the PLL's acknowledge bit and
  one arbiter byte (a step from the engine-clock replay).
- **At 1000 MHz:** the memory PLL comes out as CLKF 74 + 150/4096 on 27
  MHz with post-divider 0, which is 999.5 MHz. PowerPlay lists memory at
  1000 MHz with VDDC 900 mV, the boot voltage.

DIOC 2 gained delays (four SRBM_STATUS reads a microsecond at least; longer
is the safe side), a poll on `MPLL_CNTL_MODE` bit 16, the table's 33
registers in its write allow-list, and interrupts off for the whole list,
since VRAM is unreachable from the CPU between its first and last writes.
On W9QI0DAD, boot 112, engine already at 500 MHz:

| step | result |
|---|---|
| a list with a write to 2000h | refused, nothing written |
| SetMemoryClock replay at 299 MHz (no change) | 109 ops; MPLL acknowledged after 24 reads; every register back as it was; machine up |
| 1000 MHz | 109 ops; MPLL acknowledged after 23 reads; `MPLL_FUNC_CNTL_1` `004A0961`; `BIF_FB_EN` 3 again; 36 C |
| after it | the desktop read back through the CPU intact; V9XD32 as before, T4 and T14 0 mismatches, clears exact, only T19 FfSpecFog |
| 3DMark 2001 SE, grabs, 500/1000 MHz | **4860**, against 4379 at 500/300 and 4234 at the BIOS's 300/300; frames whole, Car Chase at 60 fps in nine of ten grabs |

The clocks last until the next boot; nothing in the driver sets them yet.

## In the driver

[The plan](../plans/ati-si-clocks-from-the-vbios.md) moved this into the HAL:

- `src/common/atombios.c`: a C89 AtomBIOS interpreter with the simulator's
  semantics, run live against the register window as Linux runs its
  tables.
- `src/chipsets/ati/si/si_clocks.c`: the policy with a profile argument,
  for a later Display Properties setting. The default profile takes the
  highest engine and memory clock any PowerPlay state lists at or below
  the boot VDDC.
- `engines/si_clock_apply.c`: runs SetEngineClock, then SetMemoryClock, at
  Oland engine validation, before the CP starts. It skips a clock already
  at its target, and records the outcome in `C:\V9XDIAG\V9XCLK.TXT`.

Host tests:

- every operand and opcode on synthetic tables, and the policy on a
  miniature of W9QI0DAD's tables;
- with `V9X_OLAND_ROM` and `V9X_OLAND_INI` set, the card's own VBIOS
  against `V9XSIMM-3.INI`. The C interpreter gives the probe's FB_DIV
  `0ED090` for 400 MHz, and for 1000 MHz memory the MPLL `004A0961`, the
  sequencer timings and mode registers the card took, and GetMemoryClock
  99,998. The policy picks 400/1000.

On W9QI0DAD's VBIOS that target is **400 MHz engine and 1000 MHz memory**,
not the probe's 500: the 500 MHz entry is named by no state.

Boot 115, whole ati set by `run-release-checks.ps1` after a `reset` (so a
cold POST at 300/300):

- `V9XCLK.TXT`: `LastSet=set`, 39,999 and 99,998 (10 kHz). The plan read
  11 levels, 7 of them at or below 900 mV. A later revalidation, after a
  mode change with the CP running, found both clocks still there.
- Every release probe as before: V9XD32 fails only T19 (FfSpecFog).
- 3DMark 2001 SE, grabs: **4797**, against 4234 at the BIOS's clocks and
  4860 at the probe's 500/1000. Frames whole; Car Chase at 59-61 fps in
  nine of ten grabs.

## Not established

- After a DOS box: not run, because DOS programs through the agent have
  stopped Windows rebooting on these machines.
- Above 500 MHz engine: the table asks for more voltage, which only the
  SMC sets here (SVI2).
- How the clocks behave over hours or under heat: the runs were minutes
  long, at 35-36 C.
- One 3DMark run at each step.
- The interpreter is a recollection of atom.c, checked only by its
  round trips (both no-change replays end where they started) and by the
  PLL arithmetic.
