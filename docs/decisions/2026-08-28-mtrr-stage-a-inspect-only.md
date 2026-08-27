# Write-combining the aperture, Stage A: read the registers, decide, write nothing

Date: 2026-08-28

Status: implemented; measured on three emulated guests 2026-08-28, and the
first finding is that most of this project's targets have no MTRRs at all
(see "First measurements" below). Unmeasured on physical hardware.

Plan: [tier0-quality.md](../plans/tier0-quality.md) item D1.

## What this is

Tier-0 draws with the CPU straight into the linear framebuffer. On a PC whose
BIOS leaves the PCI hole at the MTRR default type, that framebuffer is
uncached: every store crosses the bus alone, with no write combining. One
variable-range MTRR set to WC over the aperture is the standard fix, costs
nothing per frame, and is very likely the largest free performance win
available to the vbe family - the netbook (Atom N280, no acceleration, CPU
drawing every pixel) is the machine most starved by it.

It is also the most dangerous single thing this driver could do. MTRRs are
global CPU state. A range programmed over the wrong physical addresses puts
write-combining on memory that has nothing to do with this driver, and the
symptom appears somewhere else entirely, long afterwards, looking like
anything but a display driver bug.

So this change installs the whole apparatus and deliberately stops one step
short of acting, the way `gdi-accel-000` shipped all of the machinery with
none of it enabled.

## The split

Three pieces, and which piece owns which decision is the point:

- **`src\minivdd32\loader.asm` reads.** At `Device_Init` it establishes what
  the CPU admits to, then reads `IA32_MTRRCAP`, `IA32_MTRR_DEF_TYPE` and up to
  eight `PHYSBASE`/`PHYSMASK` pairs into locked data. It interprets none of
  them. Two new API functions report them. This is the same rule that keeps
  `significant_depth` out of ring 0 in the VBE cache - ring 0 reports facts -
  applied where the stake is much larger.

- **`src\common\mtrr.c` decides.** Pure arithmetic and pure policy: no I/O, no
  MSR, no OS. It takes the reported state plus the aperture and returns a plan
  or a numbered reason there is none. `tests\host\test_mtrr.c` holds it to a
  table of states including ones no machine here can produce, plus a
  20,000-iteration property pass asserting the invariant that matters: an
  accepted plan never names a pair that is in use and never covers an address
  some other valid range already describes.

- **`src\display16\enable16.c` publishes.** After the aperture is mapped -
  so the range considered is the one the driver actually draws through, not a
  BIOS claim about some mode - it assembles the state, runs the policy, and
  writes the answer plus every raw pair to the boot INI.

## The rule that makes it safe

MTRRs enabled, default type UC, and no valid range overlapping the aperture.

Those three together mean the aperture is currently uncached (so there is
something to win) *and* is not described as RAM by the BIOS (so changing it is
safe): on the ordinary PC arrangement, RAM is covered by write-back ranges and
the PCI hole is left at the default. Any other default type means that
reasoning does not hold, and the answer is to do nothing rather than to model
a second arrangement nobody here has measured.

Four more gates sit behind it: a 16 MiB floor on the aperture base, because no
linear framebuffer is below that and a bogus BIOS answer must not become WC
over RAM; a 1 MiB floor on the window; the window rounded **down** to the
largest power-of-two block the base is aligned to, never up, because rounding
up covers physical space that is not the framebuffer; and a free pair, since
this code never splits, resizes or evicts a range somebody else installed.

## Why it stops here

The rules above are reasoning, not evidence. What every real BIOS actually
puts in those registers is unmeasured, and a rule that is wrong on a real
machine should be discovered in an INI file rather than in a corrupted
desktop. Stage A collects exactly that: `Mtrr=` gives the CPU flags, both
control registers, the pair count and the decision with its reason code, and
`Mtrr0`..`Mtrr7` give the raw pairs so any refusal can be re-derived off the
machine instead of taken on trust.

`check-tree.ps1` asserts the mini-VDD contains no `WRMSR`, so Stage A cannot
quietly become Stage B: adding the write is a staged change that updates that
check with it.

## First measurements, 2026-08-28

Three emulated guests, all reporting the same thing:

| Guest | CPU | `Mtrr=` |
|---|---|---|
| Win86SE (ViRGE/DX) | Pentium MMX 200 | `cpu=0003 cap=0 def=0 n=0 r=3` |
| Win98SE-Trio64 | Pentium MMX 200 | `cpu=0003 cap=0 def=0 n=0 r=3` |
| Win98SE-BX-Trio64 | Pentium II 350 | `cpu=0003 cap=0 def=0 n=0 r=3` |

`cpu=0003` is CPUID plus MSR and **not** MTRR; `r=3` is `V9X_MTRR_NO_MTRR`.
`cap` and `def` are zero because no `RDMSR` executed at all - which is the
capability ladder working exactly as designed, and the single most important
thing Stage A had to prove. All three guests reached `Stage=enable-ok` with no
boot hang.

Two findings, and the second one is the significant one.

**1. 86Box does not emulate MTRRs, on any CPU.** The third row above is a
440BX/Pentium II guest built specifically to test the MSR path
(`Win98SE-BX-Trio64`, cloned from the Trio64 image, external agent port 9873);
it reports no MTRR bit either, and the 86Box binary contains no MTRR code. So
no 86Box guest can ever exercise the register reads, whatever CPU it is given.
The guest is kept anyway: it is the fleet's only P6-class Win98 target, and a
440BX board with AGP is the right era pairing for the coming Voodoo3 work in a
way the Socket 7 boards are not.

**2. Most of this project's hardware predates MTRRs.** They are a Pentium Pro
(P6, 1995) feature. The classic Pentium and Pentium MMX have MSRs but no
MTRRs, so:

- BARRY is a pre-MMX Pentium (established in the CrystalMark baseline: Ironfield
  reports `MMX OFF`), so it has **no MTRRs** and will report `r=3`;
- the 486 VLB machine has neither, and runs no mini-VDD anyway;
- every 86Box guest is out, per finding 1.

That leaves exactly two targets that can benefit: the **netbook** (Atom N280,
945GSE) and **SOLO2150** (450 MHz Pentium II, Rage Mobility-M). Both are
physical, and the netbook is the machine tier-0's uncached drawing hurts most,
so the win is still real - but it is a two-machine win, not a fleet-wide one,
and Stage B's cost/benefit has to be argued on that basis rather than on the
general claim the plan opened with.

This is exactly what Stage A was for. It cost one boot per guest and it
changed the size of the prize before any dangerous code was written.

## What Stage B needs before it is written

1. `Mtrr=` lines from the two machines that can produce anything but `r=3`:
   the **netbook** and **SOLO2150**. The emulated guests and BARRY are already
   measured and answer `no MTRRs` (above), so they can neither validate the
   rules nor benefit. The interesting number is the reason code - if those two
   report a non-UC default, the central rule needs rethinking before any code
   is written, and that is exactly the finding this stage exists to produce.
   Until then Stage B has no target it can be developed against in an
   emulator, which is a materially worse iteration loop than any other work in
   this project and is itself an argument for scheduling it late.
2. A measured before/after on the netbook, which needs the write; until then
   the size of the win is an expectation, not a number.
3. The write sequence itself: interrupts off, CR4.PGE cleared (only where
   CPUID reports PGE), CR0.CD set with NW clear, WBINVD, CR3 reloaded,
   `DEF_TYPE.E` cleared, the pair programmed, `E` restored, WBINVD and CR3
   again, then CR0 and CR4 restored. Uniprocessor only, which Win98 is.
4. A SYSTEM.INI kill switch read with `Get_Profile_Boolean`, on the
   `gdi_accel` pattern: a wrong memory type on real silicon looks like a
   driver defect, and the person hitting it needs a way to turn it off without
   a new build.

## Scope note

The inspection runs for every family, deliberately outside the
`V9X_NO_VBE_COLLECT` gate. That gate exists to keep nested BIOS calls out of
families that do not need them; none of this is a BIOS call, and the s3 and
matrox families benefit from the diagnostics exactly as much.

The mini-VDD is Windows 98 only by construction (naming one on Win95 4.00.950
stops the display devnode starting, Code 24), so Win95 gets no inspection and
would get no write-combining. That is the same boundary the dynamic VBE
pipeline already has, and it is a policy worth stating rather than an accident
- see the Track D note in the roadmap.
