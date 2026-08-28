# The DOS-box fault is on the way out, and it is a ninth-dot artefact

Date: 2026-08-28 (second session of the day)
Issue: [`docs/issues/2026-08-28-dos-box-entry-hang-gma950.md`](../issues/2026-08-28-dos-box-entry-hang-gma950.md)
Handoff this continues: [`docs/handoffs/2026-08-28-dos-box-display-fault-handoff.md`](../handoffs/2026-08-28-dos-box-display-fault-handoff.md)

Evidence: `claude\personal\v9x-86box-dosbox\2026-08-28\` - host-side window
captures, the three serial captures, and the driving scripts.

## Machine

`Win86SE` 86Box 6.0 guest, `ym430tx`, S3 ViRGE/DX PCI (`virge375_pci`),
Windows 98 SE, remote agent on `127.0.0.1:9869`. Desktop **640x480x16** for
every run below, `pitch=1280`, LFB at `e0000000`. s3 family package, three
builds:

| Build | Mini-VDD | Serial device |
|---|---|---|
| `8c59959-trace` | shipping four callbacks | `file` |
| `ssTrace1` | + five screen-switch observer hooks | `pipe` |
| `ssPlain1` | shipping four callbacks | `pipe` |

All three carry the display driver's `-DosBoxTrace` nine-point instrument.

## Measured

### The full-screen box itself is fine

`command.com` launched windowed through the agent, then one agent-injected
`ALT+ENTER`. 720x400 text mode at 70 Hz, Windows 98 banner and `C:\>`
legible, **no striping**, unchanged at 2, 5, 10 and 20 seconds
(`captures\host-03-fs-t02.png`, `host-03-fs-t20.png`). The agent answered
`ping` normally with the box full screen.

This contradicts the first 86Box note in the issue, which reported the
striping as what a full-screen box looks like here. It is not; that capture
must have been taken after an exit had been attempted.

### The exit destroys the picture, and the number says where

Second `ALT+ENTER`. Within two seconds (`captures\host-04-ret-t02.png`,
unchanged at 15 s):

- **80 lit columns at a 9-pixel period** across the 720-pixel text mode.
  Counted, not eyeballed: `scripts\stripe-period.ps1` thresholds each column
  over rows 250-450 and reports 80 hits with a gap histogram of `9 x 79`.
  720 / 80 = 9 is the VGA character cell, so this is one lit column per cell -
  the **ninth dot** - and not a stride.
- The DOS text stays legible underneath, and the timing stays text-mode:
  70 Hz, 720x400, emulator window unchanged in size.
- **Windows never returns.** No desktop, no mode change.
- The agent stops answering at that instant. Windows itself keeps running:
  six host captures two seconds apart hash to four distinct frames
  (`scripts\liveness.ps1`), so something is still animating.

Reproduced on `ssPlain1` after a fresh boot, with the same 80 columns and the
same `9 x 79` histogram. Two builds, two boots, one number.

### Nothing of ours is entered on either leg

- `DosBox=` in `C:\V9XDIAG\V9XBOOT.INI` still read `disable-exit` after a hard
  reset - the value the earlier graceful-reboot control left. None of the nine
  display-driver trace points fired.
- The serial capture gained **no line at all** across the full round trip
  (`serial\com1-sstrace3.bin` ends at the boot's `enable-ok`). So the 16-bit
  driver is not entered, and `SET_MONITOR_POWER_STATE` - which does trace - is
  not called either.

`VESA_SUPPORT` and `VESA_CALL_POST_PROCESSING` remain unobserved: neither hook
writes a serial line, and that was not changed here.

### The injection method is not the fault

`ALT+ENTER` injected with no DOS box open: `ActionsPerformed: 4`, agent
answers `ping` immediately afterwards, desktop unchanged
(`captures\ctrl-01-altenter-nobox.png`). So the wedge needs the DOS box; the
agent's `keybd_event` path is not by itself a deadlock.

## What this kills

**Observer-only hooks on the screen-switch callbacks are not free.** With
`ssTrace1` installed - five hooks whose entire body is one serial write and a
return, plus `CLC` on `CHECK_SCREEN_SWITCH_OK`, which is what not hooking it
means - the box **never goes full screen at all**, twice out of two attempts.
The windowed desktop stays on the panel, no striping appears, the agent wedges
at the `input` call, and **not one trace byte is emitted**. The same
`ALT+ENTER`, on the same guest with the same pipe serial device and the plain
`ssPlain1` mini-VDD, goes full screen with the agent healthy.

So the A/B isolates the hooks, not the serial device, and the handoff's
standing DDK reading - *"these callbacks are additive, not overriding; the
DDK's own mini-VDDs just `ret` from them"* - is wrong in a second way. It is
true that a no-op hook does not stop the main VDD doing its work. It is not
true that installing one is observationally free.

Whether the hook body wedges inside `V9xMini_Serial_Write` (bounded port I/O
on 0x3F8-0x3FB from a callback invoked in the switching VM's context) or the
presence of a dispatch entry changes the main VDD's path before any hook is
called, is **not distinguished by this evidence**. The absent trace byte is
consistent with both: a hook that hangs on its first byte, and a hook that is
never reached.

*Settled later the same day - it is the dispatch entry, not the write. See
experiment 3 below.*

The practical consequence: **step 3 of the handoff cannot be run as designed.**
A serial trace from these five callbacks does not observe this path, it
replaces the fault with a different one.

## What was still open at this point

*Item 1 was answered the same day: no, and the mode set never reaches the
driver. Item 3 is still open. See the four experiments below.*

1. **Does re-asserting the mode repair the picture?** Untested, and not
   testable from the host: the agent dies with the fault, so `V9XMSW /set:`
   cannot be launched while the picture is broken. It came back once for 8
   seconds and refused every request thereafter. A guest-side delayed trigger -
   `CHOICE /T:y,30` in a batch file, then the re-assert - would fire inside the
   guest with no host involvement, and is the cheap next attempt.
2. **What lights the ninth dot.** A character-cell-periodic artefact is VGA
   text state: the sequencer's dot-clock bit, or the attribute controller's
   ninth-dot handling. Which register holds the wrong value is not measured. An
   86Box register dump at the broken moment would settle it and no tooling for
   that exists yet.
3. **Whether `VESA_SUPPORT` is called on this path.** Cheap to answer without
   touching the screen-switch slots: the hook is already installed and shipping,
   and only needs a serial line added to its entry.

## Tooling facts worth keeping

- **The agent's screenshot cannot see this fault.** It reads the GDI primary,
  and the driver's drawing into memory is the part that is *not* wrong. Only a
  host-side `PrintWindow(hwnd, hdc, 2)` on the 86Box window shows what the
  emulated CRTC scans out. Choose the window by title: the user runs other
  86Box VMs at the same time, and "first process" picks whichever answers.
- **86Box's File COM device does not flush live.** Not on a graceful guest
  reboot, not on a hard reset. `build\vm-logs\com1.log` sat at 243,903 bytes
  across two boots and gained them all when the emulator process exited.
  `serial1_device = pipe` plus `scripts\capture-serial-pipe.ps1` is the live
  route, and the other five VMs in that folder already use it. The capture
  client must be restarted after every emulator restart.
- `update-associated-driver.ps1` needs `-ControllerPath` or `V9X_AGENT_CTL`.
- Piping a build script through `Select-Object` or `*>` under
  `$ErrorActionPreference = 'Stop'` turns ML.EXE's stderr banner into a
  terminating error, and the real assembler diagnostic is lost. Run it bare.

---

## Four more experiments, same day. Three negatives and one that matters.

Evidence for all four: `claude\personal\v9x-86box-dosbox\2026-08-28\`, same
guest, same 640x480x16 desktop, host-side captures and a live COM1 pipe.

### 1. The armed guest-side repair: neither probe brings the picture back

`REPAIR.BAT` (kept with the scripts) waits with `CHOICE /T:y,30`, runs
`MODE CO80`, waits 20 more, then runs `V9XMSW /set:800x600x16`. Launched
detached in the DOS box, so both probes fire from inside the guest with no host
involvement, and both are armed *before* anything is broken.

**Control first, which passed completely.** With no round trip, the log
recorded every step in order and `V9XMSW.INI` reported
`Result=PASS ChangeResult=0`, `StartW=640 -> RequestW=800`, with the agent
confirming `screen 800x600`. A Win32 mode set launched from inside a DOS box
works, and the timing works.

**Through the fault, nothing repairs.** Stripe counts on the host captures at
t=31, 51 and 86 s: 80 columns, `9 x 79`, every time. The text mode never comes
back and the desktop never returns.

One measurement is stronger than the captures: **no `V9X-DRV switch-ok` line
appeared on the serial log**, and the control shows a successful mode set does
emit one. So the re-assert never reached the display driver at all. The fault
is not a missing trigger; the mode-set path itself is blocked.

`REPAIR.LOG` survived the reset carrying only `ARMED`, which is consistent with
the batch never getting past its first `CHOICE` - and also with the later lines
being lost to the write cache, so it is not decisive on its own. The probe now
echoes each step to `COM1` for that reason.

### 2. `-NoScreenSwitch` does not refuse anything: the VDD never asks

Deployed to the guest as `ssNoSw1`. `MiniVDD_CheckScreenSwitchOK` returns carry
set, unconditionally, which is "prohibit the switch".

**The box went full screen anyway, and the agent answered `ping` five times out
of five while it was there.** So the main VDD does not consult
`CHECK_SCREEN_SWITCH_OK` on this path - if it had, the box would have stayed
windowed. The netbook's reading of this build ("refusing does not prevent it")
is confirmed on a second machine, with the mechanism now named.

That also means the trace build's wedge came from one of the other four hooks.

### 3. The wedge is the hook, not the serial write

`-ScreenSwitchQuiet` installs the same five hooks with the serial writes
assembled out: five bodies that do nothing but `ret`, and `clc` on the check.

**It wedges identically** - the box never reaches full screen, the agent dies
at the `input` call, twice measured. So `V9xMini_Serial_Write` is exonerated,
and the cause is the presence of a dispatch entry in one of
`PRE_HIRES_TO_VGA`, `POST_HIRES_TO_VGA`, `PRE_VGA_TO_HIRES`,
`POST_VGA_TO_HIRES`.

**This closes the obvious fix route.** A repair placed in `POST_VGA_TO_HIRES` -
where re-asserting the mode on the way back belongs - cannot work, because
installing that hook at all breaks the transition before the fault it would
repair.

It also suggests why, and the DDK's own s3v mini-VDD is the evidence: it hooks
these four *and* `SAVE_REGISTERS`, `RESTORE_REGISTERS`,
`ACCESS_VGA_MEMORY_MODE`, `VIRTUALIZE_CRTC_IN`/`OUT`, the bank and latch set,
and thirty more. **These callbacks look like a package, not a menu.** Hooking a
subset appears to tell the main VDD that the mini-VDD manages the hardware,
after which the VDD stops doing the parts we did not take over. Stated as the
hypothesis it is - nothing here measures the VDD's internal decision.

### 4. `VDD_DRIVER_REGISTER` with EDX = -1 changes nothing, and was reverted

EDX is the virtualization request. The DDK's framebuffer driver documents both
values - `mov edx,-1` for "tell VDD NOT to attempt to virtualize" against
`xor edx,edx` for yes - and gates the yes on a per-chip `bCanVirtualize` flag
plus a megabyte of video memory. `src\display16\runtime.asm` passes zero at
both registration sites.

**There is a real claim mismatch there**: saying yes asks for VGA four-plane
graphics in a window, which needs `GET_VDD_BANK`, `SET_VDD_BANK`,
`SET_LATCH_BANK`, `SAVE_LATCHES`, `ACCESS_VGA_MEMORY_MODE` and
`VIRTUALIZE_CRTC_IN`/`OUT` in the mini-VDD, and ours installs none of them.

Built as `ssNoVirt1` with `-1` at both sites and run in the guest: **entry
clean, exit striped, 80 columns, `9 x 79`, agent down.** Identical to the plain
build.

Reverted rather than kept. It is defensible on the DDK's rule alone, but it has
no measured benefit here and it is not behaviour-neutral - `-1` is what makes
Windows run graphics-mode DOS apps full screen instead of in a window, which
would be a user-visible change shipped on a null result. Whether the claim
should be corrected anyway is a separate question, and it wants its own
measurement: whether a DOS graphics app in a window works at all today.

## Where that leaves a fix

Nothing tried today repairs the exit leg, and two routes are now closed rather
than untested: a repair in a screen-switch hook (experiment 3) and a repair
triggered from inside the guest (experiment 1). What is left, in the order it
seems worth trying:

1. **Take the whole package for the s3 family.** Implement `SAVE_REGISTERS`,
   `RESTORE_REGISTERS` and the bank/latch/CRTC set in the mini-VDD against the
   real S3 registers, the way the DDK's s3v does, rather than hooking a subset.
   Experiment 3's hypothesis predicts a subset can only make things worse; this
   is the only route that follows the DDK's own model. It is a design change of
   real size and needs agreement before it starts - and it is family-specific,
   so it does nothing for tier-0.
2. **Stop the box going full screen from outside the VDD.** Windows' own
   per-application settings decide whether a DOS box may go full screen, and a
   PIF the package installs would keep it windowed - which is measured to be
   safe. Heavy-handed and user-visible, but a windowed DOS box is what a user
   would rather have than a display they must power-cycle.
3. **Watch the VDD rather than infer it.** Every observation route from inside
   the guest is now exhausted: the driver is not entered, the mini-VDD cannot be
   hooked, and the mode-set path is blocked. What remains is an emulator-side
   view - 86Box's own debugger or a register dump at the broken moment - which
   would say directly which register holds the ninth-dot value and what wrote
   it.
