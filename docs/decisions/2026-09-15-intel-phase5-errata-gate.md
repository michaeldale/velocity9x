# Intel Phase 5: the errata gate opens for a 3D draw, and the fill moves to the GPU

Date: 2026-09-15
Status: accepted. Extends, and does not inherit,
`2026-09-13-intel-phase4-gate-opened-by-risk-decision.md`.
Decided by Michael Dale after the assessment below.

## Why this record has to exist

The 2026-09-13 decision opened the errata gate **for Phase 4 specifically**,
and its central argument was that Phase 4 "submits a handful of `MI_NOOP` and
`MI_FLUSH` dwords and one colour blit, with the CPU polling head. That is close
to the minimum possible processor-to-graphics interaction."

That argument does not transfer to a triangle, and the Phase 4 result record
says so in terms: erratum 12 "remains undisclosed and unimplemented; this
workload did not provoke it, **which says nothing about a heavier one**."

So Phase 5 does not get the gate by inheritance. `V9X_I9XX_PHASE5` is a
distinct arm phase precisely so that a token issued under the Phase 4 decision
cannot authorise a 3D draw, and `arm-intel-phase5.ps1` refuses to arm until
this file exists.

## What changes, and what does not

Two errata are in scope, both from Intel's November 2009 *Mobile Intel 945
Express Chipset Family Specification Update*, document 309220-0132, both marked
as applying to 945GSE A3 with no silicon fix.

### Erratum 12 - the one that got worse

"An incorrect internal-buffer flush for a particular sequence of processor and
integrated-graphics memory accesses", with a system hang as the implication.
No published trigger, no published workaround.

Phase 5 as originally designed did **600 KiB of CPU writes through GMADR
immediately before the GPU read adjacent memory** - a bulk fill of the render
target, then a draw into it. That is a far closer match to the erratum's own
wording than anything Phase 4 did, and it is the single largest increase in
exposure this phase carried.

**It is removed rather than accepted.** See the design change below.

### Erratum 7 - the one that becomes relevant

Hang or blue screen on an "extended 3D workload" **on battery** with
Dual-Frequency Graphics Technology enabled. Intel's published workaround is to
disable DFGT in the graphics driver.

Phase 4 treated AC-only as precautionary, because Phase 4 was not a 3D
workload. Phase 5 is one, so the condition is now directly named rather than
adjacent.

**AC-only remains the requirement, and that is a judgement rather than a
measurement.** One triangle is not an "extended" workload by any reading of
that word, and running on AC removes the battery condition the erratum
specifies. The DFGT register state is still unmeasured, and this decision does
not pretend otherwise: it accepts the exposure rather than closing it. If
Phase 6 contemplates a sustained workload, that is where this assumption stops
being reasonable and the register state has to be read.

### What still holds from the Phase 4 assessment

Three of its five arguments survive unchanged, and one gets *stronger*:

- The consequence is a hang, not chip damage or persistent corruption. On a
  scratch Windows 98 stick a hang is a power cycle.
- The one-shot arm bounds the cost: disk state is disarmed before the first
  risky write, so a hang costs one boot and cannot repeat by accident.
- The machine already hard-locks on a full-screen DOS box return with zero GPU
  writes, so hang ambiguity on this target is not something the gate prevents.
- **The eighteen-month argument transfers better to Phase 5 than it did to
  Phase 4.** Intel's fix driver is August 2007 against a January 2006 part;
  unfixed drivers ran full 3D workloads on millions of laptops for eighteen
  months. Those were *3D* workloads. Phase 4 had to lean on "minimal
  interaction" instead, which was the weaker form of the same reasoning.

## The design change this decision requires

**The render target is filled by the GPU, not the CPU.** The bulk
`V9xGmadrFill` path is removed from the Phase 5 sequencer, and an
`XY_COLOR_BLT` at the head of the Phase 5 stream fills the target instead.

Three reasons, in order of weight:

1. It removes the CPU-write-then-GPU-read interleaving that most resembles
   erratum 12's description. The CPU no longer writes the target at all - it
   only reads it back afterwards, which is what every capture in this project
   already does.
2. The BLT is **Phase 4's proven packet**, executed successfully on this
   machine on 2026-09-14. It is the one GPU operation this hardware is known
   to perform correctly under this driver.
3. It needs no new packet type. At 16 bpp a 640x480 target is exactly a
   **320x480 32-bit** surface at the same 1280-byte pitch, so
   `v9x_i9xx_build_color_blt` fills it unchanged with the 565 fill word
   duplicated into a dword. The span works out to exactly `0x96000` bytes -
   the target's size to the byte - so the existing bounds check is the bound.

The cost is one more thing that can hang, which is why the fill gets its own
execute step: a hang there is Phase 4's proven packet failing at a new address,
which is a layout finding, not a 3D one.

## Decision

1. **The errata gate may be supplied as true for Phase 5** on any Mobile 945GSE
   A3 (`8086:27AE` revision 03 with host bridge `8086:27AC` revision 03). Every
   other arm input keeps its meaning and none may be overridden: the Phase 5
   token, `IntelArmPhase=5`, exact PCI identity and revision, the combined CRC
   covering the Phase 4 replay and the Phase 5 draw in execution order,
   `IntelEnableThisBoot`, Safe Mode, and the one-shot transfer to
   `IntelInFlight`.

   Scope is the stepping rather than the machine, because the errata table is
   per-stepping. It is **not** extended to Gen3 generally: nothing has been
   measured on any other Gen3 part, and the errata table differs per stepping.

2. **The CPU may not bulk-write the render target.** The GPU fills it with an
   `XY_COLOR_BLT` at the head of the Phase 5 stream. The CPU's only access to
   the target is reading it back. This is a condition of the gate opening, not
   an implementation preference.

3. **AC power only**, unchanged from the Phase 4 decision, and now load-bearing
   rather than precautionary. The DFGT register state remains unmeasured and
   this decision accepts that exposure explicitly.

4. **Hang interpretation: the Phase 4 rule, with a fresh budget.** A first hard
   hang is recorded (intent record on disk, photograph, `INTEL3D0.TXT` as far
   as it got) and the identical experiment is armed once more. A hang that
   reproduces from the same stream is the driver's fault and the kill criterion
   applies as written. A hang that does not reproduce is recorded as ambiguous
   with erratum 12 as a named suspect, and the phase continues. A third hang of
   any kind stops Phase 5.

   Phase 4 spent none of its budget, and Phase 5 starts with its own. The step
   numbering is what makes that workable: Phase 4 replays inside every armed
   Phase 5 boot, and the two phases have disjoint step numbers (1-12 against
   20-30) precisely so a hang can be attributed to one of them from the capture
   alone.

5. **Not a kill: no visible change on the panel.** The target is offscreen.
   The photograph's only job is to show the display was never ours to touch.
   "Nothing happened" is the expected result and must not be read as a failure.

## What this decision does not do

It does not claim erratum 12 is avoided. The sequence remains undisclosed, no
workaround is implemented, and the XP miniport diff
(`2026-09-13-intel-xp-miniport-diff-across-the-erratum-fix.md`) found no string
trace of it. Moving the fill to the GPU **reduces a specific, identifiable
exposure**; it does not establish that the remaining sequence is safe.

It does not measure DFGT. That remains the open question the Phase 4 decision
left, and it is inherited unresolved.

It does not authorise anything beyond one triangle. A sustained or repeated 3D
workload is a different assessment, and erratum 7's word "extended" is the
reason.

## Preconditions before the first armed boot

None of these is discretionary:

- B1 (unarmed) has run and its `INTEL3D0.TXT` validated. A decoder error or an
  unstable mapping caught there costs no armed boot.
- The Part 1 netbook regression boot has run: the segment split and the 1 MiB
  layout move have never been booted on this machine at all.
- The stick is re-armed. Step 2's layout move changed the Phase 4 execution CRC
  from `3EAA137B` to `A0DA64A1`, which invalidated the arm in place.
- AC power connected.

## Amendment, 2026-09-15: the one-shot requirement is relaxed to opt-in repeat

Decided by Michael Dale, after the first successful draw on build `6c81c52`.

The original decision required a one-shot token for every Phase 5 run. That is
relaxed: with `IntelArmRepeat=1` in the arm file, `IntelArmOnce` survives a
completed boot and the stick arms again without being re-armed. It is off by
default and set per stick, by `V9XARM5 R` or by hand.

**What is unchanged.** Every other gate still applies to every boot: the errata
gate, the build-id match, the combined-CRC match covering both streams in
execution order, the PCI identity and revision check, `IntelEnableThisBoot`,
and the Phase 4 replay passing before the draw. Each has caught a real defect -
the CRC match alone caught a stick still armed to `D0478966` after the S6 fix
changed the stream.

**What is unchanged and matters most.** The in-flight transfer is kept. A run
that completes retires the token and clears `IntelInFlight`; a run that
**hangs** leaves it set, and the next boot reports `INCOMPLETE` and refuses
exactly as before. So successful boots repeat and a configuration that hangs
the machine stops the loop instead of repeating it.

**What is given up.** A completed boot no longer requires a deliberate human
act before the next one. If a configuration draws successfully but is wrong in
some way that does not hang - writing outside the intended region, say - it
will do so on every boot until the key is removed. The guards either side of
the target are the check for that and are published in every capture.

**What is NOT authorised by this amendment.** The scope of the original
decision is unchanged: one triangle per boot. Nothing here permits sustained or
repeated 3D work within a boot, which is what erratum 7's word "extended"
concerns, and nothing here authorises a second draw in the same boot.

## Amendment, 2026-09-16: multiple draws per boot are authorised, bounded at five

Decided by Michael Dale, asked for explicitly after the Phase 6 plan proposed a
five-scene boot and was refused by the scope above. Asked first as "allow a
second draw" and then widened to five when the consequence of the narrow form
was put - that the shared-edge experiment needs three scenes and would not fit.

**What is authorised:** up to five independent draws in one armed boot. Five is
a bound, not a target, and it is the number this build was designed around.

**What is unchanged.** Every gate still applies to every boot: the errata gate,
the build-id match, the combined-CRC match over every stream in execution
order, the PCI identity and revision check, `IntelEnableThisBoot`, the Phase 4
replay before the first draw, and the in-flight transfer that turns a hang into
a refusal on the next boot. The additional draws sit inside those gates, not
beside them.

**The condition the authorisation rests on.** Each draw re-emits complete
state, performs its own fill, and flushes its own probes before the next
submits. A draw may not depend on anything another draw left behind.

That is not housekeeping. It is what keeps a failure attributable to one draw
rather than to the set, and it is also what makes a hang cheaper than it is
today: draw *n*'s probes are on disk before draw *n+1* submits, so a boot that
wedges on the fourth still returns three complete results and names the fourth.
A build that let one draw inherit another's state would be outside this
amendment even at a count of two.

### What this is weighed against, stated so a later reader is not guessing

Erratum 7 concerns *extended* 3D operation. Five draws is more 3D work than
one, in the direction the erratum names, and nobody has measured where
"extended" begins on this part - the word is Intel's and is not a threshold.
What is known is that two armed boots of one triangle each completed cleanly,
plus the Phase 4 blit. That is the entire body of evidence about sustained work
on this part and it is silent on five.

Erratum 12 concerns a CPU/GPU access sequence, and more draws means more
aperture reads. The read budget is the response and it is explicit: the capture
publishes a running cumulative count so a hang names the read it stopped at,
this build's total is bounded and asserted host-side at 52 probes, and the
measured hazard is 153,600 reads hanging where 45 did not.

The cost of being wrong is what the original gate was opened against and has
not changed: a hang on a scratch install, on AC power, recoverable from DOS.
The Phase 4 hang-interpretation rule applies unchanged - first hang record and
repeat once; reproducible is ours and kills; not reproducible is ambiguous with
erratum 12 a named suspect; third hang stops.

### What is NOT authorised

More than five draws. Any draw that inherits state from another. Sustained or
repeated 3D work in the sense of a workload rather than a bounded set of
independent diagnostic draws. Raising the bound by editing a constant without a
further decision recorded here - the constant exists so the count is visible,
not so it is easy to change.

### Consequence for the Phase 6 plan

The five-scene build 1 is authorised as designed: the regression scene first,
then the colour scene, then the three shared-edge scenes.

The edge experiment stays three scenes rather than two triangles in one,
because two opaque triangles in a single scene cannot reveal double coverage -
the second overwrites the first, and the result is indistinguishable from
coverage by the second alone.
