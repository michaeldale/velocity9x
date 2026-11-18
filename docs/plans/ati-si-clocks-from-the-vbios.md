# Oland: set the clocks the VBIOS allows, from the driver

Date: 2026-11-18. Status: done for W9QI0DAD (boot 115: the driver set
400/1000 from a cold POST, 3DMark 2001 SE 4797); the Display Properties
setting is a later release's. Follows
[the clock record](../decisions/2026-11-18-oland-clocks-left-by-the-bios.md):
W9QI0DAD's Oland ran at its BIOS's 300 MHz engine and 300 MHz DDR3; 500 and
1000 MHz, set by a probe replaying the VBIOS's own SetEngineClock and
SetMemoryClock, took 3DMark 2001 SE from 4234 to 4860.

A later release will offer the clock setting in Display Properties
(Michael, 2026-11-18). So the target is a policy with a profile argument,
not a constant: the settings page will choose the profile, and the
driver applies it the same way.

## Design

**1. An AtomBIOS interpreter** (`src/common/atombios.c`,
`include/velocity9x/atombios.h`).

- C89, no Windows or DDK headers, host-tested.
- It runs a command table live, as Linux's `atom.c` does, through the
  caller's callbacks: register read and write, PLL and MC indirect access
  (refused here), and a microsecond delay.
- Bounded: a step limit ends a table that loops (a poll that never
  completes) with an error, as atom.c's 5 s watchdog does.
- Its semantics are the ones `docs/probe/oland-clocks-2026-11-18/atom_sim.py`
  ran. On the card, that interpreter's SetEngineClock and SetMemoryClock
  traces, replayed, changed both clocks, and its no-change runs ended
  where they started.
- Host tests:
  - each opcode and operand form, on synthetic tables;
  - with the card's ROM present locally (it is AMD's, not in the tree):
    the recorded traces reproduced against a register file seeded from
    the snapshots, skipped when the ROM is absent.

**2. The policy** (`src/chipsets/ati/si/si_clocks.c`, host-tested).

- It reads the ROM's FirmwareInfo (boot VDDC, default clocks) and its
  PowerPlay v6 state, clock-info and non-clock arrays.
- Given a profile, it returns the engine and memory targets:
  - `V9X_SI_CLOCKS_BIOS`: the default clocks, so nothing is set;
  - `V9X_SI_CLOCKS_BOOT_VOLTAGE`: the highest engine and the highest
    memory clock among the levels any state lists at a VDDC no higher than
    the boot VDDC. That is the most the card is specified for at the
    voltage it runs at, because nothing here drives the SVI2 regulator.
- On W9QI0DAD that is 400 MHz engine and 1000 MHz memory. The 500 MHz
  engine run used a clock-info entry no state references, so the default
  does not take it.
- Anything not understood (a table revision other than 6, VDDC 0, a level
  below the default clocks) gives the BIOS profile.

**3. The HAL applies it** (`vi_cp.c`, Oland mode).

- When: at engine open, before the CP, RLC or DMA engines start, so no
  GPU client is running.
- The ROM image is the C0000h shadow; the HAL checks its signature, the
  ATOM header, and the PCI data structure's 1002:6610.
- Engine first, then memory, as measured.
- Skipped when GetEngineClock and GetMemoryClock already return the
  targets, so a reopen does not repeat a memory switch.
- The outcome and both clocks go to the HAL's diagnostics (V9XTRACE).
- A table that ends in error stops there: the engine never starts on a
  clock change that did not finish, and the CP path is refused, as for
  any other engine-open failure.

SetMemoryClock turns the CPU's VRAM access off while it runs, with
interrupts on (as on Linux). The HAL holds the Win16 lock, so GDI does
not draw meanwhile. A software cursor drawn at interrupt time could lose
one frame's writes. Nothing should hang: with BIF_FB_EN off, reads and
writes complete without reaching VRAM.

## Gates

- `build-host` (interpreter and policy tests), `run-checks`.
- On W9QI0DAD, from a cold boot:
  - V9XSIMM shows 400/1000;
  - the release probes pass;
  - 3DMark 2001 SE with grabs;
  - a mode change and a DOS box, then the clocks read again.
- The Carrizo, Mullins and Evergreen rows are not touched. Their clock
  paths (SMU8, CIK SMC) differ.

## Not in scope

- Raising VDDC (SVI2, through the SMC).
- The Display Properties option.
- Clock gating and power states at idle: the card stays at the target
  clocks on the desktop, as the HD 4600 stays at RP0.
