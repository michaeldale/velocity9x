# Intel Phase 4: the write gate opens on a risk decision, not on an erratum workaround

Date: 2026-09-13
Status: accepted. Supersedes the gate condition in
`2026-09-12-intel-phase4-errata-gate.md`; that record's evidence stands.
Decided by Michael Dale after the assessment below.

## What was being protected

Erratum 12 of Intel spec update 309220-0132 applies to the measured 945GSE
A3 and has no published workaround. The 2026-09-12 record made a separate
`errata_gate` input to the arm contract, false in the driver, so that no
valid arm token could reach an Intel MMIO write. The stated way to open it
was a citable workaround, or "explicitly supersede the plan's kill criterion
with a narrower risk decision". This is that decision.

## Assessment

- The erratum's consequence is a system hang. It is not chip damage, not
  persistent corruption. On a scratch Windows 98 stick a hang is a power
  cycle, and the plan already accepts wedging the machine.
- The 945GM shipped in January 2006; Intel's fix driver is August 2007.
  Unfixed drivers ran full 3D workloads on millions of laptops for eighteen
  months. The erratum needs "a specific sequence of processor and internal
  graphics memory access"; eighteen months to characterise it says the
  sequence is uncommon.
- Phase 4 submits a handful of `MI_NOOP` and `MI_FLUSH` dwords and one colour
  blit, with the CPU polling head. That is close to the minimum possible
  processor-to-graphics interaction. Nothing suggests it reaches the pattern;
  nothing proves it cannot.
- The one-shot arm design bounds the cost: disk state is disarmed before the
  first risky write, so a hang costs one boot and cannot repeat by accident.
- The XP miniport diff (`2026-09-13-intel-xp-miniport-diff-across-the-
  erratum-fix.md`) accounts for erratum 7 (render-clock switching, which this
  driver never touches) and finds no string trace of erratum 12. Locating a
  workaround now means disassembly with an uncertain outcome
  (`docs/plans/intel-xp-miniport-disassembly.md`).
- The machine already hard-locks on a full-screen DOS box return with zero
  GPU writes. Hang ambiguity on this target is not something the gate was
  preventing.

The gate was protecting the interpretability of a hang, not the hardware.
Holding Phase 4 indefinitely for that is judged the wrong trade.

## Decision

1. The `errata_gate` input to `v9x_i9xx_arm_evaluate()` may be supplied as
   true for Phase 4 on this machine. Every other input keeps its meaning:
   `IntelEnableThisBoot`, Safe Mode, exact PCI identity and revision, phase,
   the one-shot token moved to `IntelInFlight` by the DOS helper, and the
   command CRC. No write is reachable without all of them.
2. **Hang interpretation rule**, added to the plan's kill criterion. A first
   hard hang is recorded (intent record on disk, photograph, `INTELRNG.TXT`
   as far as it got) and the identical experiment is armed once more. A hang
   that reproduces from the same stream is the driver's fault and the kill
   criterion applies as written. A hang that does not reproduce is recorded
   as ambiguous with erratum 12 as a named suspect, and the phase continues.
   A third hang of any kind in the phase stops it.
3. AC power only, until the render-clock register state is measured; that is
   the erratum 7 caution and is unchanged.
4. The disassembly plan continues as background work. It becomes a
   prerequisite again only if Phase 4 produces a non-reproducible hang.

Implementation clarification, 2026-09-13: the one-shot token transfer in item
1 is performed by the Intel display driver's `DriverInit`, rather than by a
DOS helper. It starts with a false in-memory arm latch, persists and verifies
`IntelEnableThisBoot=0` first, and treats any surviving `IntelInFlight` as an
incomplete attempt. The complete transaction and two-boot procedure are in
`docs/plans/intel-phase4-first-write-design.md`. This changes the transport,
not the risk decision or any remaining arm check.

## What this does not do

It does not make the package capable of a write today. As of `b087acd` no
code calls `v9x_i9xx_arm_evaluate()`, no code reads the `IntelArmOnce`
family of keys, the `DriverInit` token transaction does not exist, and neither the
mini-VDD nor the 16-bit driver contains a ring-memory or tail-register store.
The Phase 4 work committed so far is the safe half: layout, exclusion,
packet builders, decoder, CRC and the arm contract as a pure function. The
execution half is the next implementation slice and is written against this
record and the plan's arm-and-disarm section. Its first write to silicon is
agreed in a design note before it is coded.
