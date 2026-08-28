# Making the main VDD willing to manage the DOS-box round trip

Branch: `dos-box-vdd-virtualization`
Issue: [`docs/issues/2026-08-28-dos-box-entry-hang-gma950.md`](../issues/2026-08-28-dos-box-entry-hang-gma950.md)
Evidence this rests on:
[`2026-08-29-dos-box-exit-tier0.md`](../decisions/2026-08-29-dos-box-exit-tier0.md),
[`2026-08-29-dos-box-vdd-reservation.md`](../decisions/2026-08-29-dos-box-vdd-reservation.md)

## What is established, in one paragraph

Taking a DOS box full screen and back destroys the display: entry is clean, the
exit leaves a 720x400 text mode with one lit dot per character cell - 80 columns
at a 9-pixel period - and Windows never returns. It is ours (ATI's own driver
survives the same round trip on the same emulated card), and it is
chip-independent (identical on an S3 ViRGE/DX under the s3 family and an ATI
Mach64 VT2 under tier-0). No code of ours runs on the path: not the display
driver's nine trace points, not `UserRepaintDisable`, not
`SET_MONITOR_POWER_STATE`, not either VESA hook. Hooking a *subset* of the
screen-switch callbacks makes it worse - empty bodies wedge the transition
before the box even reaches full screen. And the main VDD reserves no off-screen
memory for itself, even now that it is correctly told the card has 4 MiB.

## The hypothesis this plan tests

**The main VDD will not manage this transition for a mini-VDD that cannot do
the banking and latch work.** It wants the CPU able to see video memory as
4-plane VGA at A0000h while the screen still shows the Windows desktop, and that
needs a set of callbacks ours does not install:

| Function | # | What it is for |
|---|---|---|
| `GET_BANK_SIZE` | 37 | window size and where VRAM appears |
| `GET_CURRENT_BANK_WRITE` / `READ` | 32 / 33 | the bank the hardware is on |
| `SET_BANK`, `SET_VDD_BANK`, `GET_VDD_BANK`, `RESET_BANK` | 34, 2, 1, 3 | moving the window |
| `SET_LATCH_BANK`, `RESET_LATCH_BANK`, `SAVE_LATCHES`, `RESTORE_LATCHES` | 22, 23, 24, 25 | the VGA latches |
| `ACCESS_VGA_MEMORY_MODE` / `ACCESS_LINEAR_MEMORY_MODE` | 11 / 12 | switching the chip between the two views |
| `MAKE_HARDWARE_NOT_BUSY` | 15 | drain the engine before the CPU touches VRAM |
| `SAVE_REGISTERS` / `RESTORE_REGISTERS` | 8 / 9 | the chip extension registers the VDD does not know |

The DDK's own s3v mini-VDD installs all of these and about twenty more, and the
DDK's framebuffer *display* driver gates its "attempt to virtualize" request on
a per-chip `bCanVirtualize` flag that exists for exactly this reason.

## The design question this raises, and the answer for now

The mini-VDD is deliberately chip-agnostic: one `v9xmini.vxd`, built per family
only for its rescue-probe list. Banking registers are not chip-agnostic. The
existing unguarded S3 DPMS writes are already recorded as a defect for this
reason, so the answer is **not** to add a second set of unguarded S3 pokes.

For this branch: a build-time family guard (`V9X_MINIVDD_S3`) around the
S3-specific bodies, set by `build-minivdd-skeleton.ps1` from the family it is
already told. If the hypothesis survives, the shape worth having is a table the
display driver hands the mini-VDD at `REGISTER_DISPLAY_DRIVER` time - it already
runs, measured this session - so the mini-VDD stays chip-agnostic and the
family's own backend owns the register knowledge. **That restructure is not part
of this branch**; it is what the branch earns the right to propose.

## Stages, each with its own gate

The stages are ordered so that the cheapest thing that could falsify the
hypothesis runs first, and so that no stage installs a callback known to make
the fault worse until something says the VDD is engaging.

### Stage 1 - queries only. Does the VDD ask?

Install only the **read-only** query callbacks, each answering truthfully and
tracing once: `GET_BANK_SIZE`, `GET_CURRENT_BANK_WRITE`, `GET_CURRENT_BANK_READ`.
None of them is a notification, none is on the screen-switch path, and none
changes a register.

- **Readout**: which of them the VDD calls, and whether `VddReserve=vdd=` grows
  above the visible bytes.
- **Kill condition**: if the VDD asks none of them, it is not evaluating
  banking at all, the hypothesis is wrong, and this branch stops here rather
  than implementing twenty more callbacks on a guess.

### Stage 2 - the memory-view switch

`ACCESS_VGA_MEMORY_MODE` and `ACCESS_LINEAR_MEMORY_MODE`, with real S3 bodies,
plus `MAKE_HARDWARE_NOT_BUSY`. This is the pair that lets the VDD show the CPU
a VGA view without disturbing the visible screen.

- **Readout**: `vdd=` reservation, then the round trip.

### Stage 3 - banking and latches

`SET_BANK`, `SET_VDD_BANK`, `GET_VDD_BANK`, `RESET_BANK`, `SET_LATCH_BANK`,
`RESET_LATCH_BANK`, `SAVE_LATCHES`, `RESTORE_LATCHES`.

- **Readout**: as above.

### Stage 4 - register save and restore

`SAVE_REGISTERS` and `RESTORE_REGISTERS` against the S3 extension set the DDK's
s3v saves: CR40, CR42, CR53 and the 3C2h state, held in a per-VM control-block
area allocated at `Device_Init` (`_Allocate_Device_CB_Area`), which we do not
have today.

Deliberately last. Hooking these tells the main VDD the mini-VDD manages the
state itself, so a half-finished pair is worse than none - the same trap the
screen-switch notifications turned out to be.

### Stage 5 - the screen-switch notifications, if and only if the rest works

`PRE`/`POST_HIRES_TO_VGA` and `PRE`/`POST_VGA_TO_HIRES`, which are measured to
wedge the transition when installed *alone*. The hypothesis says they are part
of a package; this is where that gets tested, and it is the last thing to try.

## Gates for every stage

- `run-checks` green.
- The mini-VDD build audit pins the exact `(guard, callback, handler)` set, so a
  stage cannot add a callback nobody decided to add.
- Deployed to the `Win86SE` 86Box guest, s3 family, and measured: `VddReserve=`
  from the boot trace, then the round trip with the host-side capture and the
  stripe count.
- Recorded in a decision doc per stage that produces a result, including the
  stages that produce nothing.

## What success looks like

`vdd=` above the visible bytes, then a round trip whose exit leg returns a
desktop instead of 80 lit columns, with the agent alive. Anything less than the
second is not a fix, however good the first looks.

---

## Outcome: Stage 1 met its kill condition. Stages 2 to 5 are not built.

2026-08-29, result in
[`docs/decisions/2026-08-29-dos-box-banking-not-asked.md`](../decisions/2026-08-29-dos-box-banking-not-asked.md).

The VDD asks **nothing** about banking. `GET_BANK_SIZE` and both current-bank
queries were installed, answering truthfully and tracing once, and neither was
ever called - across boot, mode set and a full round trip. `vdd=` unchanged, the
fault unchanged.

What it does ask this mini-VDD, in full: `GET_CHIP_ID` and `CHECK_HIRES_MODE` at
init, and `GET_TOTAL_VRAM_SIZE` once during the first mode set. So no query of
ours is the gate, and building the banking and latch set on the theory that it
would change the VDD's decision has nothing behind it.

**No code from this branch is on `main`.** The docs were merged and the code was
not, deliberately: what survived the trim is a single `CHECK_HIRES_MODE`
handler, which the VDD does ask and this mini-VDD can answer honestly - and
which buys nothing measurable. A new dispatch entry with no measured benefit is
the same trade that got `EDX = -1` reverted, and this session established that
entries in that table can have effects nothing predicted. It lives on
`dos-box-vdd-virtualization` as a complete experiment record; merge it only if
something measures a benefit.

Removed on the branch itself, before that decision: the banking queries, which
are never asked, and `GET_CHIP_ID` - Plug-and-Play card-change detection per the
DDK's own comment, not a capability gate, and a mini-VDD that identifies no chip
must not install it.

**This plan is closed.** The next move is not another stage; it is reading the
other side - 86Box's debugger at the broken moment, or `VDD.VXD` itself - to
find what it tests before deciding not to call a registered `RESETHIRESMODE`.
