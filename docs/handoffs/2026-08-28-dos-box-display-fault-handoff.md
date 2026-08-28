# DOS-box display fault handoff — reproduced in 86Box, driver never entered

Date: 2026-08-28
Branch: `main`, eleven commits ahead of both remotes and **unpushed**
Issue: [`docs/issues/2026-08-28-dos-box-entry-hang-gma950.md`](../issues/2026-08-28-dos-box-entry-hang-gma950.md)

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
