# Returning from a full-screen DOS box hard-locks the netbook under the Intel package

Filed: 2026-09-12
Status: open, one occurrence, not yet reproduced deliberately
Machine: MICHAEL-NETBOOK, HP Mini 110-1000, 945GSE / GMA 950, Windows 98 SE
direct-booted from the live USB stick, `intel-gma` package build
`827b2d3-dirty` (Phase 3 event matrix), desktop at 1024x576x16.

## What happened

During runbook 3.2c, after a cold boot and one resolution change to
640x480x16 and back:

1. Starting `command` showed **full-screen corruption** immediately.
2. Alt+Enter gave a working full-screen DOS session.
3. Alt+Enter again, to return to the desktop, **hard-locked the machine**.
   No response; power-cycled.

## Evidence recovered

`build\driver-results\netbook-intel-phase3-20260912-run1\` (copied from
`C:\V9XDIAG` after the power cycle). `INTELEVT.TXT` holds three records, all
`Flags=0000003F`, all with the same ownership state as the Phase 1 and 2
baselines (PGTBL_CTL `7FFC0001`, ring registers zero, HWS_PGA `1FFFF000`,
fences zero, GTT hash `4D8707C5`):

| Seq | Kind | Context |
|---|---|---|
| 1 | boot-enable | 0161 |
| 2 | mode-switch | 0111 |
| 3 | mode-switch | 0161 |

Two facts fall out of this before the lock is even considered:

- A Display Properties resolution change is a ReEnable rebuild (kind 4)
  with **no Disable**. The runbook had assumed Disable then Enable. Corrected.
- Firmware ownership did not change across two mode switches: same ring,
  same fences, same PGTBL, same GTT hash.

There is **no Disable record** for the DOS box, although `Disable()` publishes
one as its first action. Either Disable was never reached, or the record was
written into Windows' profile cache and lost with the lock. The publisher at
the time did not force a flush; it does now (`032b9cd` and the follow-up), so
a repeat will tell these apart.

## What is known about the path

The DOS box return is the `reenable-same-mode` branch of `ReEnable()` in
`src\display16\ddi.c`: `V9xHardwareReset()`, palette, then
`v9x_publish_hardware_diagnostics()` (Phase 1 MMIO fingerprint and the Phase 2
GTT capture: 16384 mini-VDD calls and a 256 KiB file write), then the
mode-restore event, then `V9xVddPostMode()`. The DOS box entry is
`Disable()`: the event publish, `V9xVddUnregister()`, `V9xHardwareDisable()`.

No Velocity9x driver has previously been through a full-screen DOS box round
trip on this machine. The 2026-08-27 VBE-family session did not exercise it.
`docs\decisions\2026-08-29-dos-box-exit-tier0.md` records the tier-0 DOS box
work on emulated targets.

## Hypotheses, none tested

1. **The Phase 1-3 diagnostics inside ReEnable are the trigger.** They read
   MMIO and the GTT through the mini-VDD immediately after the VDD has
   restored the VBE mode. They ran without incident at boot and at two mode
   switches, so timing relative to the VDD handoff would have to matter.
   Test: a build with the diagnostics skipped on the same-mode branch, or
   moved after `V9xVddPostMode()`.
2. **The VBE mode restore itself fails on this VBIOS after a VGA text mode.**
   The VBE family would show the same lock. Test: the VBE package, same
   sequence.
3. **The initial corruption is the real fault and the lock is downstream.**
   `command` started with a corrupt screen before any Alt+Enter, which
   points at the windowed DOS box or the VDD's first save, not at the return.
   Test: photograph the corruption; try a windowed DOS box alone.
4. **The DIB Engine selector fault** from
   `docs\issues\2026-08-14-hellbender-dibeng-gpf.md`, which has a DOS box
   signature on other targets.

## 2026-09-24: hypothesis 3 measured - any DOS VM corrupts the scanout

Same machine, now a hard-disk Win98 SE install reachable over the network
(v9x-remote-agent 0.6.2, 10.0.1.254:9869). Package 0.8.0 `intel-gma`,
build `890c828`, first install through Device Manager, one warm reboot.
Operator watching the panel; one action per observation:

| # | action | panel |
|---|---|---|
| 1 | `V9XMSW /set:1024x576x8` (agent `exec`, Win32) | correct |
| 2 | agent `screenshot` (GDI readback) | correct |
| 3 | agent `shell START V9XGDI.EXE /auto` | **corrupt** before the test window appeared |
| 4 | `/set:1024x576x16`, `/set:1024x576x8` | correct |
| 5 | `exec NOTEPAD.EXE` (Win32) | correct |
| 6 | agent `shell VER` (COMMAND.COM in a hidden VM, nothing else) | **corrupt** |
| 7 | `/set:1024x576x16`, wait 8 s, `shell VER` | **corrupt** |

Photograph: `docs\decisions\2026-09-24-netbook-panel-after-dos-vm-8bpp.jpg` - fine regular vertical
striping over the whole panel with scattered light-blue blocks and no trace of
the desktop's layout (no taskbar, no window edges), so this is different
memory or a different scanout format, not the desktop with a wrong palette.
It matches the 0.6.1b description in
`2026-08-28-dos-box-entry-hang-gma950.md`.

What this establishes:

- **A DOS VM that never becomes visible is enough.** No full-screen switch,
  no Alt+Enter. The agent's `shell` verb (it runs `COMMAND.COM /C`) and
  `START` from it both do it. Hypothesis 3 holds: the corruption on entry
  is its own fault, upstream of the return path.
- **Depth is not the variable** (rows 6 and 7), consistent with 0.6.1.
- **A mode set repairs it** every time (rows 4, and the first boot's
  640x480 corruption cleared at row 1), and the plane and pipe registers the
  mode-switch capture records afterwards are the normal ones (plane B
  enabled, 8 bpp, stride 1024, base 0, pipe source 1024x576).
- **The GDI side never sees it.** `V9XGDI /auto` ran inside the corrupt
  state and wrote `Result=PASS` at 1024x576x8; the agent's screenshots show
  the correct desktop. Readback through the aperture is intact.

This contradicts the 0.6.1 `V9XDOSBX` trial on the same machine (windowed
box "survived"). Either that trial did not look at the panel while the box
was open, or something since 0.6.1 made VM creation disturb the display.
Not established which.

**Captured the same evening:** the display registers in the corrupt state
show the VGA plane re-enabled on pipe B and plane B disabled - the panel is
scanning out VGA 720x400, not our framebuffer.
`docs\decisions\2026-09-24-a-dos-vm-switches-the-netbook-to-the-vga-plane.md`.
**Do not use the agent's
`shell` verb on this machine for tests that need a correct panel** - use
`exec` with full paths and `get` for files.

## Next

Reproduce once with the flushing build, after the mode and DPMS rows have
been copied off, and photograph both the initial corruption and the final
state. Then hypothesis 1 first, since it is the only one this project
introduced.
