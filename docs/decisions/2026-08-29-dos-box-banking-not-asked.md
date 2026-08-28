# The VDD never asks about banking, so the virtualization hypothesis has no support

Date: 2026-08-29
Branch: `dos-box-vdd-virtualization`
Plan: [`docs/plans/dos-box-vdd-virtualization.md`](../plans/dos-box-vdd-virtualization.md)
Preceding: [`2026-08-29-dos-box-vdd-reservation.md`](2026-08-29-dos-box-vdd-reservation.md)

Guest: `Win86SE`, s3 family, 640x480x16, builds `bank1`, `bank2`, `hires1`.
**Stage 1's kill condition is met and the branch stops at Stage 1.**

## What Stage 1 was

The plan's hypothesis was that the main VDD refuses to manage the DOS-box round
trip for a mini-VDD that cannot do banking and latch work. Stage 1 installed
only the **read-only** banking queries, each answering truthfully and tracing
once - `GET_BANK_SIZE` (64 KiB and "VRAM at A0000h", the DDK's own answer) and
`GET_CURRENT_BANK_WRITE` / `_READ` (CR35 with CR51's high bits, under a family
guard, the register sequence the DDK's s3v uses). No notification slot was
touched, because installing a subset of those is already measured to wedge the
transition.

The gate was: does the VDD ask, and does `VddReserve=vdd=` grow?

## Measured

**It asks neither.** No `bank-size-asked`, no `bank-read-asked`, across boot,
mode set and a full round trip. `vdd=614400 visible=614400` unchanged, and the
round trip unchanged: entry clean, exit 80 lit columns at a 9-pixel period, no
desktop, agent dead.

Two further queries were added to see what the VDD *does* ask, and this is the
part worth keeping:

```
V9X-MINI init build=bank2
V9X-MINI power-callbacks-ok callbacks=4
V9X-MINI vbe-collect disabled
V9X-MINI chipid-asked          <- GET_CHIP_ID, at mini-VDD init
V9X-MINI hires-asked           <- CHECK_HIRES_MODE, at mini-VDD init
V9X-DRV load build=bank2
V9X-MINI regdd-called
V9X-MINI vram-asked            <- GET_TOTAL_VRAM_SIZE, during pre-mode
V9X-MINI regdd-called
```

So the main VDD's whole interrogation of this mini-VDD is: **chip id and hi-res
state at init, and the total memory once during the first mode set.** Nothing
about banks, nothing about latches, nothing about memory-access modes.

## What that kills

The hypothesis, as stated. The VDD's refusal to reserve memory is not gated on a
banking capability it asks about, so implementing the banking and latch set on
the theory that it would change the VDD's mind has no evidence behind it -
Stages 2 to 5 of the plan are not worth building. That is exactly what Stage 1
was ordered first to find out, and it cost one build.

**It does not prove the converse.** The VDD may decline banking-related work
without asking, on the strength of what it already knows. What is established is
narrower and still useful: no query of ours is the gate, so there is nothing to
answer differently.

## Two things learned on the way

**`GET_CHIP_ID` is not a capability gate.** The DDK's s3v comment is explicit:
"used by the Main VDD's Plug&Play code to determine whether a card has been
changed since the last time that Windows was booted". It returns EAX and has no
carry convention. The Stage 1 build answered NC without setting EAX, which would
leave the VDD reading whatever EAX held - and an unstable answer across boots is
exactly what would make Plug and Play think the card changed. **Removed again**:
this mini-VDD identifies no chip, and an installed handler that cannot answer is
worse than an absent one.

**The reservation may be a red herring.** The DDK's framebuffer driver says the
VDD "will also allocate some memory so that it can perform this virtualization
correctly", where the virtualization in question is *graphics-mode DOS apps in a
window*. A driver whose mini-VDD cannot do that has no reason to expect an
allocation - so `vdd == visible` may be the normal answer for this class of
driver rather than a symptom, and the full-screen round trip may never have
needed an off-screen area at all.

## What became of the code

**None of it is on `main`.** This document and the plan were merged; the code was
not.

The branch trimmed itself to one callback - `CHECK_HIRES_MODE`, which the VDD
asks and this mini-VDD can answer honestly: CY for the Windows VM once the
display driver has registered, NC otherwise, which is what it answers at init
before any driver has loaded, with the Windows VM handle coming from
`REGISTER_DISPLAY_DRIVER`'s own contract. It is correct, it is cheap, and it
changes nothing measurable. That is not enough to earn a new entry in the
mini-VDD dispatch table, on the same test that reverted `EDX = -1`, and this
session is precisely where installing entries in that table turned out to have
effects nobody predicted - an empty body in a notification slot wedges the
transition outright. It stays on `dos-box-vdd-virtualization`; merge it if
something ever measures a benefit, or if a later fix needs the VDD to know.

Dropped on the branch before that: the banking queries (never asked, so
unmeasured behaviour buying nothing) and `GET_CHIP_ID` (cannot be answered
truthfully). The family guard went with the banking bodies, so the mini-VDD is
chip-agnostic again apart from the DPMS defect already on file.

## Where this leaves the fault

Every route from inside the guest is now exhausted, and this branch closes the
last hypothesis that had a mechanism behind it:

- No hook of ours is called on the path - driver or mini-VDD, measured
  individually.
- Installing screen-switch hooks makes it worse, empty bodies included.
- The virtualization request flag changes nothing either way.
- The VDD is now told the truth about memory and asks nothing about banking.

What is left is not another build. It is **reading the other side**: 86Box's
debugger at the broken moment, or the Windows 98 `VDD.VXD` itself, to find what
it tests before deciding not to call a registered `RESETHIRESMODE`. Until
somebody does that, the windowed-only mitigation is the only thing that protects
a released package, tier-0 included.
