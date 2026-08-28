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
never reached. Written down as the open question it is.

The practical consequence: **step 3 of the handoff cannot be run as designed.**
A serial trace from these five callbacks does not observe this path, it
replaces the fault with a different one.

## What is still open

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
