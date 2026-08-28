# DOS-box display fault handoff — reproduced in 86Box, driver never entered

Date: 2026-08-28
Branch: `main`, eleven commits ahead of both remotes and **unpushed**
Issue: [`docs/issues/2026-08-28-dos-box-entry-hang-gma950.md`](../issues/2026-08-28-dos-box-entry-hang-gma950.md)

> **A second session has run steps 1 to 3 of the path below. Read
> "Session 2" at the foot of this document first: it establishes that the
> fault is on the way *out*, gives it a number, and closes step 3 as
> designed.**

Taking a DOS box full screen destroys the display. It was reported as a hang
on an HP Mini 110 (Intel GMA 950), spent most of a day being investigated as
one, and is not one. It has since reproduced in 86Box on an emulated S3
ViRGE, which is where the rest of the work should happen.

Read the issue doc for the evidence. This is the state of the world.

## The two findings that matter

**1. Nothing hangs.** The reporter annotated a photograph: the mouse cursor is
on screen and moves. Windows keeps running throughout. Only the picture is
destroyed, and the cursor renders as a tall narrow bar rather than an arrow -
the driver is still drawing a correct desktop into memory and the CRTC is
reading it with the wrong horizontal geometry.

Every trial recorded `hung` in `V9XDOSBX.INI` actually means *"the display
became unusable and the machine was powered off"*. The tool cannot tell those
apart. The outcomes stand; the word was wrong, and it sent three experiments
after a fault that does not exist.

**2. The display driver is never entered on this path.** A build tracing nine
points across `Disable`, `ResetHiResMode` and `ReEnable` wrote none of them,
on the netbook or in the guest.

That has a consequence worth carrying forward: `V9xHardwareReset` is reached
only from `ResetHiResMode` and `ReEnable`, so **no fix placed in either can
affect this** - including the stride re-assert shipped in 0.6.1 for the
corrupt-band issue.

## Reproduced in 86Box

`Win86SE` guest, 86Box 6.0, ViRGE/DX, Win98SE, agent on `127.0.0.1:9869`.
s3 family package built `8c59959-trace`, deployed with
`update-associated-driver.ps1`.

A full-screen DOS box in the guest shows the Windows 98 banner and `C:\>`
prompt legibly, overlaid with the same regular fine vertical striping the
netbook photographs show. **Not Intel-specific, not GMA-specific, not
tier-0-specific.**

### Guest state as left

- Display **640x480x16**, not its usual 800x600 - a `V9XMSW /set:` from the
  control runs, not restored.
- `8c59959-trace` driver installed, i.e. the `-DosBoxTrace` build. Reinstall a
  plain package before using this guest for anything else.
- Package also at `C:\V9XREMOTE\JOBS\dosbox-trace-s3\` in the guest.
- `V9XBOOT.INI` holds `DosBox=disable-exit` from the control below.

### Driving it

```powershell
$ctl = 'C:\everything\claude\personal\v9x-remote-agent\scripts\v9xctl.ps1'
Start-Process 'C:\86Box\86Box.exe' -ArgumentList ('-P "C:\Users\michael\86Box VMs\Win86SE"')
& $ctl ping -Port 9869          # poll this, not the TCP port - it opens first
& $ctl exec -Port 9869 -Application 'command.com' -Detach -ShowWindow
& $ctl input -Port 9869 -Sequence 'key ALT+ENTER'
```

**The agent goes unreachable while the box is full screen** and comes back
after roughly a minute, or immediately once the guest leaves full screen.
That is not a wedge - poll and wait.

To bring it back when the agent cannot be reached, send Alt+Enter to the
emulator window from the host:

```powershell
$ws = New-Object -ComObject WScript.Shell
$null = $ws.AppActivate('Win86SE - 86Box'); $ws.SendKeys('%{ENTER}')
```

## The instrument, and the control that validates it

`-DosBoxTrace` on `build-active-package.ps1` / `build-win16-ddi-skeleton.ps1`
writes a `DosBox=` step to `V9XBOOT.INI` at nine points and flushes the
profile cache after each, so a step survives a power cut. Own key, not
`Stage=`, which the settings page and boot-trace tooling match on.

**An absent trace proves nothing until the trace is known to fire.** Two
controls failed to exercise it, and both are traps worth knowing:

- `V9XMSW /set:` to a **different** mode takes `ReEnable`'s mode-change
  branch, which is not instrumented.
- `/set:` to the **current** mode writes nothing at all -
  `ChangeDisplaySettings` declines a no-op before the driver is reached.

A **graceful reboot** does call `Disable`. After one, `V9XBOOT.INI` reads
`DosBox=disable-exit`. That is the control; use it whenever this instrument is
rebuilt.

## What has been eliminated, and what that is worth

Three differential builds, all still in tree behind build switches, none
shipping, each audited so the image must carry exactly what the switch asked
for:

| Switch | What it does | Result |
|---|---|---|
| `-VgaReturn` | mini-VDD hooks `PRE_HIRES_TO_VGA`, sets INT 10h mode 3 itself | fault unchanged |
| `-NoScreenSwitch` | refuses the switch via `CHECK_SCREEN_SWITCH_OK` | fault unchanged |
| `-NoDpms` | excludes the unguarded S3 DPMS register writes | **built, never run** |

Discount `-VgaReturn` and `-NoScreenSwitch`: both were aimed at a hang that
does not exist, so they say nothing about their mechanisms.

One DDK fact worth not rediscovering: `PRE_HIRES_TO_VGA` and friends are
**additive, not overriding**. The DDK's own s3v mini-VDD just `ret`s from them
and the VDD proceeds regardless. A no-op hook measures nothing and looks like
a clean negative.

## Open questions, in the order worth asking

1. **What does the guest desktop look like on return from the box?** That is
   the netbook's actual symptom and it has not been captured - the guest was
   reset mid-run. One scripted step now.
2. **Why does the VDD never call back?** Registration succeeds; it hands the
   VDD `RESETHIRESMODE` and a failure would abort `Enable` with
   `Stage=fail-vdd-register` rather than the `Stage=enable-ok` on disk. So the
   VDD holds the callback and does not use it on this transition. Whether it
   believes it restored the state itself from the `VDD_SAVE_DRIVER_STATE`
   snapshot taken at registration is a guess, and is written down as one.
3. **Does re-asserting the mode repair the picture?** Untested. `V9XDOSBX.EXE`
   `8c59959` now does it automatically when the box closes, and on
   Ctrl+Alt+Shift+R - but see the defect below.
4. `-NoDpms` still wants running, and the unguarded S3 writes on non-S3
   silicon want fixing regardless of what it says.

## Defects in the tooling, found the hard way

- **The rescue hotkey cannot work as shipped.** A full-screen DOS box owns the
  keyboard on Win9x; a Win32 `RegisterHotKey` never sees Ctrl+Alt+Shift+R. The
  automatic re-assert when the box closes is the usable half. Either drop the
  hotkey or find a trigger that survives a full-screen VM.
- **`V9XDOSBX.INI` cannot distinguish a hang from an unreadable screen.** It
  records `hung` for both. Anything read from the earlier trials needs that
  caveat applied.
- The tool under-recorded planar modes as `640x480x1` until `PLANES` was
  multiplied through; trials 3 and 4 in the netbook's file carry the old
  wrong depth with correct outcomes.

## Suggested path forward

The 86Box repro changes what is possible, not just where. The netbook forced
differential builds because a VxD could not be traced there; the guest has
COM1 to a host file. That constraint is gone, and the plan below leans on it.

In order:

1. **Capture the return.** Restart the guest, take the box full screen, send
   Alt+Enter host-side to bring it back, screenshot the desktop. One scripted
   pass; it answers whether the guest shows the netbook's actual symptom
   (corrupt desktop on return) or recovers. Everything after this depends on
   which it is.
2. **Test the repair in the guest, not blind on the netbook.** With the
   desktop corrupt, run `V9XMSW /set:` to a *different* mode via the agent
   (a same-mode set is a no-op before the driver is reached — see the control
   above). If the picture comes back, the fault reduces to "find a trigger",
   and the fix is small. If not, the mode is not the whole of what was lost.
3. **Serial-trace the mini-VDD across the round trip.** `V9xMini_Serial_Write`
   exists and 86Box gives it somewhere to go. Instrument the four installed
   callbacks plus registration, take the box full screen and back, read the
   log. This directly attacks open question 2 — why the VDD holds
   `RESETHIRESMODE` and never uses it — with a measurement instead of a guess.
   It is the first time anything on the VxD side of this fault can be observed
   rather than eliminated.
4. **Run `-NoDpms` in the guest** while it is set up. Cheap now, and it closes
   the one differential build that was never run. Expected no change; record
   it either way.
5. **Fix the tooling defects** so the next reader is not misled: rename the
   `hung` outcome (`unreadable` or `no-outcome`), drop or replace the rescue
   hotkey, keep the automatic re-assert.

Housekeeping, before or alongside:

- **Push the eleven commits.** Nothing above requires them to stay local, and
  an unpushed day of forensics is a risk with no offsetting benefit.
- **Restore the guest**: reinstall a plain (non-trace) package, set it back to
  800x600, clear `DosBox=` from `V9XBOOT.INI` — or snapshot the current state
  first if the trace install is worth keeping as a baseline.
- Retitle or annotate the issue doc's filename claim (`entry-hang`) if it is
  linked from anywhere else; the body already corrects itself.

Steps 1–3 are one guest session. If step 2 repairs the picture, the shape of
the fix is a re-assert trigger and step 3 becomes about choosing where that
trigger lives; if it does not, step 3's log is the only lead left.

## Netbook state

HP Mini 110, Intel 945GSE / GMA 950, `8086:27AE`. Hard-reset after the last
run. Evidence collections under
`claude\personal\v9x-intel950\` — `0.6.1`, `0.6.1a`, `0.6.1b`, `standard`,
`1build` (`-VgaReturn`), `2build` (`-NoScreenSwitch`), `3build` (trace).

Its mode cache is worth remembering: 36 modes listed, **all 36 described**,
6 admitted by our own rules. That is the opposite of the NAV50's Pineview
BIOS, so **the mode sweep has nothing to sweep on this machine** and a clean
run of it there would mean nothing.

---

# Session 2, same day: what the guest actually does

Full record with the numbers:
[`docs/decisions/2026-08-28-dos-box-exit-ninth-dot.md`](../decisions/2026-08-28-dos-box-exit-ninth-dot.md).
Evidence: `claude\personal\v9x-86box-dosbox\2026-08-28\`.

## Answered

**Step 1 - capture the return. The entry is clean and the *exit* is the
fault.** At a 640x480x16 desktop the full-screen box is a correct 720x400 text
mode at 70 Hz with no striping, held over 20 seconds, agent answering `ping`
throughout. The second `ALT+ENTER` breaks it within two seconds: **80 lit
columns at a 9-pixel period** across 720 pixels - one per VGA character cell,
the ninth dot - with the DOS text still legible, the timing still text-mode,
and Windows never coming back. Reproduced on two builds and two boots with the
same gap histogram.

That corrects this document's own "a full-screen DOS box shows the striping".
It does not; the earlier capture was taken after an exit attempt.

**The driver is not entered on either leg**, confirmed twice over: `DosBox=`
stayed at the control's `disable-exit`, and the serial log gained no
`V9X-DRV` line across the round trip. `SET_MONITOR_POWER_STATE` is not called
either.

**Step 3 - serial-trace the mini-VDD. It cannot be done from those callbacks.**
`-ScreenSwitchTrace` (new, on `build-active-package.ps1`) installs
observer-only hooks on `CHECK_SCREEN_SWITCH_OK` and the four HiRes/VGA
notifications. With it installed the box **never goes full screen**, twice out
of two, the agent wedges at the keystroke, and not one trace byte arrives. The
same keystroke with the plain mini-VDD on the same guest and the same serial
device goes full screen with the agent healthy - so the A/B isolates the hooks,
not the serial change.

So "these callbacks are additive, the DDK's own mini-VDDs just `ret`" is right
about the main VDD still doing its work and wrong about the cost: installing
one is not observationally free. The switch stays in the tree behind its guard
as a recorded negative.

**A control worth having: the agent's injection is not the fault.**
`ALT+ENTER` with no DOS box open returns cleanly and the agent survives.

## Not answered

**Step 2 - does re-asserting the mode repair the picture?** Still untested, and
it cannot be driven from the host: the agent dies with the fault. It answered
once, 8 seconds later, then refused everything. **The next attempt should arm
the repair inside the guest before breaking anything** - a batch file that
waits (`CHOICE /T:y,30`) and then runs `V9XMSW /set:` to a *different* mode,
launched detached, and the round trip started immediately after. If the picture
comes back, the fix is a trigger; if not, the mode is not what was lost.

**Step 4 - `-NoDpms`.** Still never run. Cheap while the guest is set up.

## Where to look next, in order

1. **The guest-side delayed repair** above. It is the one measurement that
   changes the shape of the fix, and it needs no new build.
2. **Trace `VESA_SUPPORT`.** It is already installed and shipping and writes
   nothing. One serial line at its entry says whether Windows routes a VESA
   call through us on this path, and it touches no screen-switch slot - so it
   is not exposed to the negative above.
3. **What lights the ninth dot.** A character-cell-periodic artefact is VGA
   text state - the sequencer dot-clock bit, or the attribute controller's
   ninth-dot handling. Which register holds the wrong value is unmeasured, and
   an 86Box-side register dump at the broken moment is the only obvious way to
   see it. No tooling for that exists.
4. `-NoDpms`, to close it either way.

## Guest state as left

- Win86SE, **`ssPlain1`** installed: shipping mini-VDD, `-DosBoxTrace`
  display driver. 640x480x16.
- **`serial1_device` is now `pipe`**, not `file`. The old value is saved at
  `86box.cfg.file-backup` in the VM folder. The file device does not flush
  live - not on a guest reboot, not on a hard reset - so the pipe is the only
  usable serial route. Attach with:

  ```powershell
  Start-Process powershell -ArgumentList '-NoProfile','-File',
    'C:\everything\velocity9x\scripts\capture-serial-pipe.ps1',
    '-PipeName','velocity9x-com1','-OutputPath','<file>','-NoConsole'
  ```

  It disconnects whenever the emulator restarts; reattach each time.
- Host-side capture is `claude\personal\v9x-86box-dosbox\2026-08-28\scripts\capture-86box.ps1`.
  Use it rather than the agent's screenshot: the agent reads the GDI primary,
  which is exactly the part of this fault that is not wrong. It selects the
  86Box window **by title** because the user runs other VMs in parallel.

---

# Session 2, continued: step 2 answered, and two routes closed

Record: [`docs/decisions/2026-08-28-dos-box-exit-ninth-dot.md`](../decisions/2026-08-28-dos-box-exit-ninth-dot.md),
"Four more experiments".

**Step 2 is answered: no.** A repair armed inside the guest before the round
trip - `CHOICE /T:y,30`, `MODE CO80`, then `V9XMSW /set:` to a different mode -
does not bring the picture back. A control with no round trip passed every
step, so the instrument is sound. And the serial log carries **no
`V9X-DRV switch-ok`** where the control produces one, so the mode set never
reached the display driver. **This was not a missing trigger. The mode-set path
itself is blocked.** `MODE CO80` does not clear the ninth dot either.

**Step 4 is answered for `-NoScreenSwitch`, which was thought settled.** Run in
the guest, the box goes full screen anyway with the agent healthy throughout.
The VDD does not consult `CHECK_SCREEN_SWITCH_OK` on this path at all, so
refusing the switch from the mini-VDD is closed, not merely unproven.

**And the trace build's wedge is the hook itself.** `-ScreenSwitchQuiet` - the
same five hooks with the serial writes assembled out, bodies that do nothing
but `ret` - wedges identically. So a repair cannot live in `POST_VGA_TO_HIRES`
either: installing the hook breaks the transition before the fault it would
repair. The DDK's s3v mini-VDD hooks those four alongside `SAVE_REGISTERS`,
`RESTORE_REGISTERS` and the bank/latch/CRTC set, which suggests these callbacks
are a package rather than a menu, and that a subset tells the VDD the mini-VDD
owns hardware it does not actually manage. Hypothesis, not measurement.

**One more negative, and it was reverted:** `VDD_DRIVER_REGISTER` with EDX = -1
("do not attempt to virtualize", which is what the DDK's framebuffer driver
passes unless its mini-VDD can do four-plane graphics in a window - ours
cannot) changes nothing. The claim mismatch in `runtime.asm` is real and worth
its own decision; it is not this fault, and shipping it on a null result would
have changed how graphics-mode DOS apps are windowed for no measured gain.

## What is left to try, in order

1. **Take the whole package for the s3 family.** `SAVE_REGISTERS`,
   `RESTORE_REGISTERS` and the bank/latch/CRTC set in the mini-VDD, against
   real S3 registers, the way the DDK's s3v does. It is the only route that
   follows the DDK's model rather than fighting it, and experiment 3 predicts
   any subset makes things worse. A design change of real size, family-specific,
   nothing for tier-0 - **get agreement before starting it.**
2. **Keep the box windowed from outside the VDD.** Windows' own per-application
   settings decide whether a DOS box may go full screen, and a PIF the package
   installs would hold it in a window - which is measured to be safe on both
   machines. Heavy-handed and user-visible, and still better than a display
   that needs the power switch.
3. **Watch the VDD from the emulator.** Every in-guest route is now exhausted:
   the driver is not entered, the mini-VDD cannot be hooked, and the mode-set
   path is blocked. 86Box's own debugger or a register dump at the broken moment
   would say which register carries the ninth-dot value and what wrote it.

Still not run: `-NoDpms`, and tracing the already-installed `VESA_SUPPORT` hook
(one serial line at its entry, no screen-switch slot touched).

## Guest state as left, second update

- **`ssNoVirt1` was reverted in the tree but is still what the guest booted
  last.** Reinstall a package built from `main` before trusting a run.
- Serial stays on `serial1_device = pipe`; captures for the day are in
  `claude\personal\v9x-86box-dosbox\2026-08-28\serial\`.
- `REPAIR.BAT` is at `C:\V9XDIAG\REPAIR.BAT` in the guest and echoes each step
  to `COM1` now, so an armed probe's progress survives a wedge and a reset.
  Its `V9XMSW` path points at `C:\V9XREMOTE\JOBS\ssnosw1\`; change it to
  whichever job folder the current package went to.

---

# Session 2, final: the two cheap leftovers are done

**`VESA_SUPPORT` is not called on this path.** `-VesaTrace` (new; traces both
already-installed VESA hooks, so it adds no dispatch entry) fires twice at boot
and stays silent across the whole round trip. With the shipping mini-VDD,
**nothing of ours runs between the keystroke and the destroyed picture** - not
the driver's nine points, not `SET_MONITOR_POWER_STATE`, not either VESA hook.

**`-NoDpms` is closed without running it.** All four `V9xMini_Set_Dpms` call
sites are inside those three measured-silent procs, so the routine cannot
execute during a round trip. Stronger than the build would have been: a "no
change" result would have left open whether the writes happened and were
harmless. Guest only - the netbook has no serial port - and the unguarded S3
writes on non-S3 silicon are still a defect on their own account.

So the list in "What is left to try" is the list. Nothing on it can be done
from this session: item 1 is a design change needing agreement, item 2 is a
product decision, item 3 needs emulator-side tooling that does not exist yet.
The two things a person can do that would move it are in the same order:
confirm the exit-leg finding and the 9-pixel period on the HP Mini 110, and try
`V9XMSW` by hand from Start, Run while the picture is broken - a real keypress,
which host automation cannot deliver to 86Box.

---

# Session 3 (2026-08-29): tier-0 measured, and the emulator exonerated

Record: [`docs/decisions/2026-08-29-dos-box-exit-tier0.md`](../decisions/2026-08-29-dos-box-exit-tier0.md).
Evidence: `claude\personal\v9x-86box-dosbox\2026-08-29-tier0\`.

The vbe tier-0 package now runs in 86Box on an **ATI Mach64 VT2**
(`Win98SE-VBE-Tier0`, host port 9872, COM1 pipe `native-s3-com1`), installed
through the INF's manual "any VESA VBE 2.0+ adapter" entry. Disk and NVR backed
up to `Velocity9x Backups\Win98SE-VBE-Tier0-pre-v9x-20260829\` first.

**Tier-0 fails identically to s3**: entry clean, exit 80 lit columns at a
9-pixel period, no desktop, agent dead, no `DosBox=` key. Two families, two
unrelated chips, two different desktop modes, one number.

**ATI's own driver on the same VM survives the round trip** - the desktop comes
back and the agent answers five pings. So the emulator is not the artefact and
the fault is ours, which is the netbook's standard-VGA control reproduced
somewhere a script can reach.

**This kills the s3-register-save/restore proposal as a fix for this issue.** An
artefact identical on a ViRGE and a Mach64 cannot be S3 extension-register
state. The 9-pixel period is standard VGA text state, which the main VDD
restores itself.

## Revised list of what is left

1. **The shared path.** What the driver tells the main VDD at registration
   (`VDD_DRIVER_REGISTER` flags - one measured, null; `VDD_SAVE_DRIVER_STATE`;
   the re-registration on a live mode switch), and the VESA
   linear-framebuffer mode the VDD was never told about.
2. **Keeping the box windowed from outside the VDD** - the PIF route. Family
   and chip independent, so it is the only thing that protects tier-0, and a
   windowed box is measured safe on every machine tried.
3. **Emulator-side observation.** 86Box's debugger or a register dump at the
   broken moment would name the register that carries the ninth dot. Every
   in-guest route is exhausted.

## Still needing a person

Confirm the exit leg and the 9-pixel period on the HP Mini 110. Both measured
cases are 86Box, and the netbook's photograph is a corrupt *hi-res desktop*
rather than a legible text page - so whether it is the same fault is still
assumed, not shown.

## Tier-0 guest state as left

- **ATI's 4.02 driver is active** (the control), not Velocity9x. The vbe
  package is installed in the INF cache and can be reselected through
  Display Properties, Advanced, Adapter, Change.
- `vbeTrace1` was the Velocity9x build measured; the package folder is the VM's
  mounted CD (`build\win98se-vbe`), so a rebuild is picked up by Have Disk
  without copying anything in.
- Once Velocity9x is the associated driver again,
  `update-associated-driver.ps1 -Port 9872` works for further builds; while
  ATI's driver is active it does not, and the install has to go through the UI.
- The DOS VBE inventory is at `C:\V9XVBE.EXE` in the guest and its report at
  `C:\V9XDIAG\V9XVBE.TXT`.
