# The survey at schema 2: what a VLB machine forced, and what it has not yet said

Date: 2026-08-20
Status: **built and host-verified; no 486 VLB run yet — the measurement section
below is deliberately empty**

Stage 1 of `docs\plans\vlb-survey-tool.md`. The card in hand is an S3 Trio on
VESA Local Bus in a 486, and the survey we shipped would have come back from it
nearly empty, because almost everything it collected was reached through PCI.
This records what was changed and why, and — separately, and honestly — what has
and has not been measured.

Read this note with `docs\specifications\vga-survey.md`, which is the report
contract and now describes schema 2 key by key. This note is the reasoning; that
one is the interface.

## What schema 2 adds

### 1. The chipset can be identified without PCI

`run_tier2` used to return immediately with `no-pci-display-device-identified`
when the PCI walk found nothing, so the S3 register probe — the thing we
actually want from a VLB card — never ran. It now falls back to
`identify_non_pci`, and the whole design of that function is the order of
operations:

**Read first, write second.** The identity registers are read with the locks
exactly as the BIOS left them, and the CR38/CR39 unlock keys are written only
after those reads have already spelled S3. The unlock becomes a confirmation of
an answer rather than a guess that could be wrong on hardware where 48h and A5h
in CR38/CR39 mean something else entirely.

Two accept signals, weighted by how specific each one is:

| Signal | Specificity | Accepts alone? |
|---|---|---|
| CR2D/CR2E high byte in {56, 88, 89, 8A, 8C, 91} | 6 of 256 | yes |
| CR30, one of 19 documented values | 19 of 256, over 7 of 16 nibbles | no |
| An S3 string in the ROM image at C000 | corroboration only | never |

The CR30-only case needing corroboration is the addition to the plan, and it is
there because CR30 is what the oldest VLB parts have — the 86C801/805 and 86C928
predate CR2D/CR2E — and a 19-value accept set on its own is a weaker gate than a
6-value one. Requiring the ROM to also name S3 costs nothing: the ROM is already
dumped verbatim into `[VideoBios]`, so this adds no information a host-side
reader could not find. It just has to be available *before* the unlock to gate
it.

Both signals are reported either way, under `[Chipset.Identify]`, so a refusal
is diagnosable from the report instead of being a dead end.

**The residual risk, stated plainly.** CR38/CR39 on a non-S3 card mean something
else. The locked-read-first ordering shrinks the exposure but does not remove
it, and the Tier 2 prompt now says so out loud on a machine with no PCI: it
tells the tester that the check is trusting their word about the card as much as
it is trusting the registers.

**The case this does not solve.** If a part genuinely gates *reads* behind
CR38/CR39 — which the plan's premise says S3 does not, and which every S3
capture so far supports, because the video BIOS leaves CR38/CR39 holding 48h/A5h
at POST — then the locked read returns nothing usable and the tool stops before
writing. On an 86C801/805 that would mean a report that cannot name the card.
That is the honest outcome of the safety ordering, and it is left as an open case
rather than papered over with an override switch. The parser now reports the
locked-versus-unlocked disagreement over CR30-CR3F explicitly, so the first run
that hits it will say so rather than quietly returning a wrong name.

### 2. The whole extended register file, not a list chosen for the PCI parts

Tier 2 used to read eleven CRTC and five sequencer registers. It still reports
those under their own names — nothing in the schema-1 output regressed — and
adds CR30-CR6F and SR08-SR1F as blobs behind the same unlock. CR30-CR3F and
SR08-SR1F overlap the locked Tier 1 dump on purpose: where the two disagree, the
disagreement is the finding, and the parser looks for exactly that.

Which registers differ on a VLB part is not yet known. Capturing all of them is
what stops finding out from costing another round trip to a tester. The
likely candidates — CR5D/CR5E extended overflow, and the CR40/CR53/CR55 group —
were none of them in the old list.

The DAC identity was added the same way: six consecutive reads of 3C6h,
bracketed at both ends by a read of 3C8h that resets the DAC's internal access
counter. Which read in the sequence returns the hidden command register differs
between parts, so all six are reported and the decoding is left host-side.
Nothing in it writes.

### 3. The linear window is interrogated

This is the question the run exists to answer. On the PCI parts the window is a
BAR and the host bridge routes it. On VLB the position is programmed into
CR58/CR59/CR5A and the 486 chipset has to decode it — on a machine that may have
less RAM than the window is wide and may not decode high addresses at all.

`/aperture` is a third opt-in, separate from `/tier2` and implying it (the
window base is something only Tier 2 can read). It copies 32 bytes out of the
window's physical base with INT 15h AH=87h — the BIOS extended-memory block
move: a service, not a mechanism this tool implements, and a copy *from* the
address in question *to* a buffer in the program's own data segment.

Three limits, and all three are in the report rather than only in this note:

- **16 MB.** The AH=87h descriptor base is 24 bits. If CR59/CR5A point higher,
  the base is reported and the read is skipped with a stated reason.
- **No mode was set.** On these parts the window may only answer once a mode has
  been set with linear addressing enabled, so a dead result is suggestive and
  not conclusive. Settling that needs a mode set, which belongs in a later
  bring-up probe on our own card and not in a tool handed to strangers.
- **A base at or below the top of installed RAM returns RAM.** That looks alive
  and proves nothing. The tool reports the base and the memory figures; the
  parser compares them and returns `unreadable-by-this-method` rather than
  `window-responds`.

Under EMM386 the CPU is in virtual-8086 mode and AH=87h is intercepted and
emulated by the memory manager rather than executed by the BIOS. The result is
usually still the physical bytes, but it is a different code path, so
`[Aperture]` repeats the HIMEM/EMM386/V86 facts from `[Platform]` beside its own
verdict, the parser attaches a caveat when it sees them, and the tester
instructions ask for a clean boot (F8, command prompt only) for the `/aperture`
run.

### 4. Platform facts a 486 makes relevant

New `[Platform]` section: CPU class, installed RAM three ways, A20, and the
memory managers. None of it matters on the PCI targets; all of it constrains
where an aperture can go.

The CPU part needed care. The binary is 8086 code — the build now passes `-0`
explicitly rather than inheriting it from `wcl`'s default — and the AC and ID
bit tests that separate 386 from 486 from CPUID need 32-bit PUSHFD/POPFD, which
are 386-only encodings. So the 32-bit probes sit behind a 16-bit pre-check:

1. FLAGS bits 12-15 stick set after being cleared → 8086 or 186. Stop.
2. FLAGS bits 12-14 will not stick when set → 286. Stop.
3. Only now execute PUSHFD. AC (bit 18) separates 386 from 486; ID (bit 21)
   says whether CPUID exists.
4. Only now execute CPUID.

Step 2 can answer "286" wrongly on a 386 under a memory manager, because POPF in
V86 mode is emulated by the monitor rather than executed. That costs detail,
never safety: what gates the 32-bit encodings is the probe result alone, never
an inference, and the report carries the probe answer and any inference from a
V86 host under separate keys so the two cannot be confused. A machine that lands
in that branch loses the CPUID detail and the E820 map, and says so.

A20 is detected read-only, by comparing the 256 bytes of the interrupt vector
table against their alias at FFFF:0010. The usual test writes a marker; this one
does not need to.

### 5. The build gate grew, and admits what it cannot see

`scripts\build-vga-survey.ps1` is the reason this tool can be handed to a
stranger, so it grew with the tool. It now also refuses the VBE setter functions
(4F05h-4F0Bh), and — because schema 2 introduced inline assembly — checks two
new surfaces:

- **Every literal port constant at an `inp`/`outp` call site** must be one of
  the audited VGA ports, and every non-literal must be one of the two derived
  index ports. A new port cannot be introduced without editing the list.
- **The raw opcode bytes a `#pragma aux` emits** are allowlisted (there is one:
  SMSW AX, spelled out by hand because Open Watcom's inline assembler will not
  accept the mnemonic at any CPU setting it offers for a 16-bit DOS target), and
  the state-changing instructions a pragma could otherwise smuggle in — LGDT,
  LIDT, LMSW, OUT, MOV to a control register, any software interrupt other than
  INT 15h — are named and refused.

What it still cannot do, and the script says so where the rules live: it cannot
check that no `outp` reaches a port outside the audited set, because most calls
take a variable and the port value is simply not in the source text.

`-GateSelfTest` runs the gate against nine deliberately broken copies of the
source and asserts every one is rejected. It needs no compiler, so it is a
check, not a ceremony.

## What has been verified, and how

Host-side only. No hardware or emulator has run schema 2.

| Check | Result |
|---|---|
| `build-vga-survey.ps1 -GateSelfTest` | 9 of 9 mutations rejected, clean source accepted |
| `build-vga-survey.ps1` | builds clean at `-0 -wx`, no warnings |
| `run-checks` | green, with the gate self-test now a step in it |

`parse-vga-survey.ps1` was then run over hand-built schema-1 and schema-2
reports, which between them exercise every branch the parser gained:

- the PCI and the non-PCI bus verdicts;
- identification from CR2D/CR2E, and from CR30 alone with ROM corroboration
  (which correctly comes back labelled unverified);
- identification from the Tier 1 locked dump when Tier 2 was declined;
- `nothing-decodes`, `unreadable-by-this-method` for a base inside RAM, and
  `not-requested`;
- the V86 caveat, attached from the memory-manager keys;
- the locked-versus-unlocked CR30-CR3F disagreement.

Those inputs were written by hand to drive the branches. **They are not captures
and are not committed**, precisely so that nothing under `docs\decisions\` can
later be mistaken for a measurement.

## What has not been measured

Nothing here. This section is where the results go, and it is empty on purpose.

The plan's verification sequence, in order, none of it done:

1. **Regression on PCI hardware we already have.** Run schema 2 on the 86Box
   ViRGE (`:9869`) and Trio64 (`:9871`) and confirm the report is a superset of
   the schema-1 one — identical values for every key that existed before. This
   needs no 486 and should come first.
2. **An emulated 486 VLB machine.** The `display_device_count == 0` branch has
   never executed on any target; everything surveyed so far has PCI. Build a
   486 VLB machine with an S3 VLB card in 86Box and run tier 1 → tier 2 →
   `/aperture` there, where a wedge costs an emulator reboot rather than a round
   trip to a tester.
3. **The 486 VLB target itself.** Tier 1, then tier 2, then `/aperture`, in that
   order, keeping every report. The ordering *is* the safety property: if a
   later stage wedges the machine, the earlier reports survive on disk.

Raw reports get committed beside this file when they exist, named the way the
VBE inventory dumps are, and the tables above filled in from them.

Two lessons from 2026-08-20 apply to surveys as much as to screenshots: read the
whole report before drawing a conclusion from any single field, and a value that
agrees with what we expected is not thereby correct.

## What this schema does not decide

- Whether the driver's DPMI mapping can reach the window on a 486. That needs a
  build, not a survey.
- Whether the 8514/A blitter behaves the same on the VLB part.
- The install path. A VLB card has no PCI hardware ID, and the INF is generated
  from the family manifest's `VendorId`/`DeviceId` pairs, so SetupX has nothing
  to bind. That needs a different binding — most likely a hand-chosen generic
  display ID plus Have Disk — and the survey says nothing about it.
