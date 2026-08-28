# The VDD reserves no off-screen memory, and telling it the truth does not change that

Date: 2026-08-29
Issue: [`docs/issues/2026-08-28-dos-box-entry-hang-gma950.md`](../issues/2026-08-28-dos-box-entry-hang-gma950.md)
Preceding measurements: [`2026-08-29-dos-box-exit-tier0.md`](2026-08-29-dos-box-exit-tier0.md)

Guest: `Win86SE`, s3 family, 640x480x16, builds `urdTrace1` through `vram5`.
**The fault is not fixed.** Three candidates eliminated, one real conformance
gap closed, and the evidence now points at one remaining route.

## What was checked and closed by reading, before spending a build

Three suspects in the registration path, all three already correct:

- **The callback's segment is not discardable.** `RESETHIRESMODE` compiles into
  `_TEXT`, and the linker directive is `segment '_TEXT' preload fixed shared` -
  one code segment for the whole driver. The DDK's framebuffer driver marks its
  own `_TEXT` merely `PRELOAD MOVEABLE`, so we are stricter than the reference.
- **The VM handle is right.** `INT 2Fh AX=1683h` into BX, which is exactly what
  `framebuf\INIT.ASM` does, after `1684h` for the entry point.
- **The I/O-trap pairing is right.** `STOP_IO_TRAP` at registration and
  `START_IO_TRAP` on unregister and on the failure path, as the reference does -
  and the stock ATI driver leaves trapping stopped too, so it cannot be the
  differentiator anyway.

**Our export list also matches the DDK's**, ordinal for ordinal, except
`GetDriverResourceID @450`, which has nothing to do with this path.

## `UserRepaintDisable` is not called either

The one hook of ours the DDK samples use on the screen-switch path, exported at
ordinal 500 and never measured there. `v9x_dosbox_trace` now also accumulates a
`DosBoxTrail=` key, so a whole sequence survives instead of only the last step,
and the trail is proven by a graceful reboot:

```
DosBoxTrail=disable-enter,disable-pre-hardware,disable-exit
```

Across the DOS-box round trip the trail did not change at all. So USER does not
call `UserRepaintDisable` on this transition, and **the last driver-side trigger
is eliminated.** There is no hook of ours on this path, in the driver or in the
mini-VDD.

## The VDD reserves nothing, and that is measurable

`VDD_DRIVER_REGISTER` answers with "the size in bytes of the visible screen
plus the memory allocated by the VDD" - the DDK's framebuffer driver reads it to
place its own off-screen areas *below* the VDD's, because "the VDD is using the
memory directly below the visible screen". **This driver has never read it.**

Now recorded, as `VddReserve=` in the boot trace:

```
VddReserve=vdd=614400 visible=614400 vram=4194304 info=131
```

614400 is 640x480x16 exactly. The VDD returned precisely what it was handed, so
**it reserved nothing for itself** - and a full-screen DOS box has no off-screen
area to have its state saved into.

## Why it reserved nothing, and the ordering bug that was found on the way

The main VDD does not take the size from the display driver's
`VDD_REGISTER_DISPLAY_DRIVER_INFO` (service 0x83). That service is routed to
the *mini-VDD's* `REGISTER_DISPLAY_DRIVER`, function 0, and the arguments are
whatever a driver and its own mini-VDD agree on - the DDK's three samples pass
three different things, and the XGA one uses it as a two-way call. The VDD asks
for the total through a separate callback, `GET_TOTAL_VRAM_SIZE`, function 36.

Our mini-VDD installed neither. Both are now implemented: function 0 stores what
the driver reports, function 36 answers it with carry, or NC when it has not
been told - three instructions, the way the DDK's s3v does it.

That exposed the real ordering defect. Traced with a serial line in each:

```
V9X-MINI vram-asked
V9X-MINI regdd-called
```

**The VDD asks exactly once per boot, before the driver has told us anything,
and never asks again.** Our answer at that moment was "unknown", so the VDD
concluded there was nothing beyond the visible screen. The driver established
its size inside `V9xHardwareEnable`, one call too late, because the query
arrives during `VDD_PRE_MODE_CHANGE`.

Fixed by establishing the figure before the first call into the VDD - a family's
`read_video_memory` hook or the mini-VDD's cached controller answer, neither of
which calls the BIOS - and moving the hand-over ahead of the pre-mode call. The
order is now:

```
V9X-MINI regdd-called
V9X-MINI vram-asked
V9X-MINI regdd-called
```

**And the VDD still reserves nothing.** `vdd=614400 visible=614400 vram=4194304`
with the size delivered, asked for at the right moment, and answered with 4 MiB.
The round trip is unchanged: entry clean, exit 80 lit columns at a 9-pixel
period, no desktop, agent dead.

## What that means

The reservation is not gated on the size. It is gated on something else, and
the only candidate left standing is the one three separate measurements now
point at: **the VDD will not use off-screen memory for a mini-VDD that cannot
do the banking and latch work** - `GET_VDD_BANK`, `SET_VDD_BANK`,
`SET_LATCH_BANK`, `SAVE_LATCHES`, `ACCESS_VGA_MEMORY_MODE`,
`VIRTUALIZE_CRTC_IN`/`OUT`. The DDK's framebuffer driver gates its own
"attempt to virtualize" request on exactly that capability, per chip.

So the three independent lines of evidence agree:

1. Hooking a *subset* of the screen-switch callbacks makes the transition worse
   (empty bodies wedge it).
2. `EDX = -1` at registration changes nothing, and `EDX = 0` - "attempt to
   virtualize" - buys nothing either.
3. The VDD reserves no memory even when correctly told there is 4 MiB of it.

All three are what you would expect if the main VDD requires the full
virtualization set before it will manage this transition at all.

## What was kept, and why, given the fault is unfixed

Kept:

- The `DosBoxTrail=` sequence and the `VddReserve=` figures. Pure diagnostics,
  and they are what made this session's measurements possible.
- The two mini-VDD callbacks, the early size establishment, and the reordered
  hand-over. This is a real conformance gap - the VDD asks a question all four
  DDK mini-VDDs answer, and ours answered "I don't know" because the driver had
  not looked yet. Replacing a false answer with a true one is defensible on its
  own, and whoever implements the virtualization set needs this plumbing anyway.
  Recorded here so nobody reads it as a fix: **it changes the VDD's behaviour
  not at all, and the fault is identical with and without it.**

Not kept, from the previous session, and for contrast: `EDX = -1` was reverted
because it had no measured benefit *and* changed user-visible behaviour - DOS
graphics apps would run full screen instead of windowed. The test for keeping a
null-result change is whether it is defensible without the fault.

## The one route left

Implement the banking and latch half of the mini-VDD dispatch table so the main
VDD will treat the driver as one it can virtualize for. That is a design change
of real size, it is chip-specific in its details, and it is the only remaining
candidate. Until then the mitigation - keeping the box windowed from outside the
VDD - is the only thing that protects a released package, tier-0 included.
