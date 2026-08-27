# Write-combining the aperture, Stage A: read the registers, decide, write nothing

Date: 2026-08-28

Status: implemented, unmeasured on hardware

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

## What Stage B needs before it is written

1. `Mtrr=` lines from every reachable machine: BARRY (physical Trio64), the
   netbook (945GSE), the 486 if it ever runs a mini-VDD, and both emulated
   guests. The interesting number is the reason code - if real BIOSes mostly
   report a non-UC default, the central rule needs rethinking before any code
   is written, and that is exactly the finding this stage exists to produce.
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
