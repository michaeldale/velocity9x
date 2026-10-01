# Write-combining the aperture, Stage B: write one MTRR, on two machines

Date: 2026-10-01. Status: proposed, not started. No code is written and
no MSR has been written by this driver. Two decisions below are Michael's.
Stage A: [`../decisions/2026-08-28-mtrr-stage-a-inspect-only.md`](../decisions/2026-08-28-mtrr-stage-a-inspect-only.md)

## Why now

Every CPU store through a graphics aperture is uncached: the driver has
never programmed an MTRR, and the netbook's BIOS leaves the GMADR at the
default type. Measured on the netbook this session:

| Workload | Writing the command ring | of wall time |
|---|---|---|
| Half-Life Direct3D, async (`nbasync1`) | 12.3 us a submission, ~295 dwords | 9.1% |
| Quake 2 OpenGL (`q2sink`) | 38.4 us a submission, ~850 dwords | 5.6% |

That is 40-45 ns a dword, one bus transaction per store. Quake 2's
lightmap writes inside Lock add 1.8%, and the VBE family's CPU drawing,
which Stage A was first written for, is the larger and unmeasured case.
Write-combining would collapse consecutive stores into burst writes. How
much of the 40 ns goes is the number Phase 3 measures; nothing here
promises it.

## What the machines report (`Mtrr=` in `V9XBOOT.INI`)

**Netbook, 2026-10-01** - the first non-emulated readout:

```
Mtrr=cpu=000f cap=00000508 def=00000c00 n=8 r=0 s=2 b=d0000000 z=00400000
Mtrr0=00000006 80000800    WB, 0 - 2 GiB
Mtrr1=7f800000 ff800800    UC, 8 MiB at 7F800000 (the stolen memory)
Mtrr2..7 = 0               free
```

`cap` has WC (bit 10) and eight variable pairs; `def` is enabled with
fixed ranges and a UC default; Stage A's policy accepts (`r=0`), would use
pair 2, and plans WC over **4 MiB** at `D0000000`.

**SOLO2150 (Gateway)**: not read. It was offline on 2026-10-01. Phase 0.

## The findings that change Stage A's rules

1. **4 MiB is too small.** The netbook's driver reaches 7.7 MiB of stolen
   memory through the GMADR (`VddReserve ... vram=8060928`); Stage A rounds
   the window *down* to the largest aligned power of two, never up, so it
   would leave the top 3.7 MiB - where the ring and textures may live -
   uncached. Rounding down was to avoid covering physical space that is not
   the framebuffer; inside one PCI BAR of known size, rounding up covers
   only more of the same aperture. Proposed rule: round up to a power of
   two, but never past the BAR's own size and alignment (the GMADR is 256
   MiB at `D0000000`), so 8 MiB here. Still one pair; still refused when
   any valid range overlaps.
2. **Two aliases of one memory.** The stolen memory is RAM at `7F800000`
   (UC by MTRR 1) and the aperture at `D0000000` (WC once this lands). Both
   types are uncacheable, so no cache holds a line either alias can make
   stale; mixing WB with either is what the SDM warns against, and that is
   not proposed. This is also the arrangement Linux's i915 uses for the
   GMADR. Stated, not measured here.
3. **Ordering before TAIL.** The ring is written through the aperture and
   published by one store to `RING_TAIL` in the MMIO BAR, which stays UC.
   On P6-family CPUs an uncached access drains the WC buffers ahead of it,
   but the driver should not lean on that: an explicit drain before the
   TAIL write - `SFENCE` where CPUID reports SSE (the netbook's Atom), and a
   locked read-modify-write of a stack word on CPUs without it (the
   Gateway, recorded as Pentium II-class) - costs a few cycles a submission.
   The same applies before any doorbell, flip or blit-start register a CPU
   write to the aperture precedes.
4. **Intel erratum 12** (309220-0132), already named in the async
   decision, concerns CPU and GPU access interleaving on this part.
   Write-combining changes the CPU's write pattern. Nothing shows it
   triggering; Phase 3's correctness runs are what can.

## The design

- **Ring 0 writes, nothing else does.** The mini-VDD gains the write
  sequence Stage A specified: interrupts off; CR4.PGE cleared where CPUID
  reports PGE; CR0.CD set, NW clear; WBINVD; CR3 reload; `DEF_TYPE.E`
  cleared; the pair written (base, then mask with V set); `E` restored;
  WBINVD and CR3 again; CR0 and CR4 restored. Uniprocessor, which Windows
  98 is. It runs once, at `Device_Init` after the 16-bit side has
  published the plan, and is read back: `Mtrr2=` must show what was
  written, or the pair is cleared again and the boot reports why.
- **`src/common/mtrr.c` still decides**, with rule 1 changed and its host
  tests extended: rounding up inside the BAR, refusal at the BAR's edge,
  and the existing 20,000-iteration property pass over overlap.
- **`check-tree.ps1`'s no-`WRMSR` assertion** becomes "exactly one WRMSR
  site, in the routine above", changed in the same commit.
- **The drain before TAIL** in `d3d_i9xx.c`'s submit and the 2D/flip ring
  writers, and before the Mach64's register writes that follow a texture
  upload, chosen by CPUID once.
- **Engines:** Intel GMADR on the netbook first. The Gateway's Rage
  Mobility has local VRAM in its own BAR; its 3D path writes MMIO
  registers, which stay UC, so its gain is texture uploads and locked
  surfaces, and it waits on its own readout.

## Decision 1 (Michael): the kill switch

Stage A asked for a SYSTEM.INI kill switch, because a wrong memory type
on real silicon looks like any other driver defect. The 2026-09-18 rule is
no INI keys or V9X3D verbs for new features. Options:

- **A.** No switch; on by default in the next build, recovered like any
  bad build (WININIT rename back to the previous one). Follows the rule.
- **B.** A recovery-only switch written by `V9X3D OFF` (which already
  turns everything off from DOS), with no verb of its own, read as "off"
  only when present. An exception to the rule, argued by the failure
  being global CPU state rather than one feature.

Recommended: B, because recovering a machine whose every aperture write
misbehaves should not need a working Windows to rename files from.

## Decision 2 (Michael): scope

Netbook only first (the one machine with a readout and the measured
cost), Gateway after its Phase 0 readout. Or both together once the
Gateway is read.

## Phases

0. **Readouts and layout, no write.** Gateway `Mtrr=` lines. On the
   netbook, the ring's and textures' offsets in the GMADR, to confirm what
   8 MiB covers. Record both in a decision doc.
1. **Policy, host-only.** `mtrr.c` rule 1 and its tests, test-first.
2. **The write and the drain**, behind nothing (or behind decision 1's
   switch). Gates: check-tree, build-host, run-checks.
3. **Netbook, one boot.** `Mtrr2` reads back WC over 8 MiB; Half-Life
   Direct3D async and Quake 2 timedemos against today's figures, the ring
   write per submission and per dword from the snapshot; 3DMark 99 and
   Half-Life watched for corruption by the operator. Stop and revert on any
   corruption, hang or timeout counter.
4. **Gateway**, after its readout: texture upload and Lock time, Half-Life
   OpenGL and Direct3D.

## Not in scope

- PAT, which would type individual pages and avoid MTRR range limits; it
  needs page-table edits in the VMM's tables and is a larger change.
- Any cacheable (WB) mapping of video memory.
- The VBE family's tier-0 drawing measurements, which this enables but
  does not plan.
