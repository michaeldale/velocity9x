# A full-screen DOS box comes back when the driver hooks INT 2Fh 4001h/4002h

Date: 2026-11-10
Issues: [`2026-08-28-dos-box-entry-hang-gma950.md`](../issues/2026-08-28-dos-box-entry-hang-gma950.md),
[`2026-09-12-netbook-dos-box-return-hardlock.md`](../issues/2026-09-12-netbook-dos-box-return-hardlock.md)
Plan closed by it: [`dos-box-vdd-virtualization.md`](../plans/dos-box-vdd-virtualization.md)
Evidence: [`docs/probe/86box-virge-dos-box-2026-11-10/`](../probe/86box-virge-dos-box-2026-11-10/)

Guest: `Win86SE` (86Box, S3 ViRGE/DX, Windows 98 SE, DirectX 8.0), s3
family, desktop 1024x768x16. Measured on 86Box only. Not run on any
Intel machine, where the issue was first seen.

## The round trip

Start > Run `command`, Alt+Enter, `ver`, Alt+Enter, with host-side
PrintWindow captures of the 86Box window and an agent ping at each step
(scratchpad `dos\roundtrip.ps1`; COMMAND.COM is never started through the
agent).

| Boot | Build | Back from full screen |
|---|---|---|
| 680 | main `852e8a1d` | 1024x768 at 60 Hz, but frozen: the pre-`ver` desktop with a striped band across the DOS window's title bar, the pointer does not follow agent moves, keys do not reach the box, agent `screenshot` (GDI) reads black. Agent alive |
| 681, 682 | main + `-DosBoxTrace` | 720x400 text, a lit column every 9 pixels, agent dead, emulator killed to recover: the August signature |
| 684 | `sswitch1` (this change, in `_TEXT`) | desktop repainted, the window shows `ver`'s output, pointer and keys work, GDI reads the desktop. A second trip and `exit` work; V9XD32 afterwards gives the same result lines as boot 678 |
| 685 | `sswitch2` (this change, in SSWCODE) | the same: desktop repainted, pointer follows, no band |

Boot 680's picture is the HD 4600's from boot 71 (2026-10-23: desktop
frozen with a striped band, GDI reads black), so the 86Box guest and the
Intel machine now show the same fault.

## What Windows' VDD does, read from VDD.VXD

`VDD.VXD` (120,887 bytes, dated 1999-04-23 in the CAB) from the guest's
`C:\WIN98\WIN98_54.CAB`, loaded with its fixups applied and disassembled
(scratchpad `tools\ledis.py`, capstone). Addresses are object number x
10000h plus the offset.

- The PM API entry (`DDB_PM_API_Proc`, F3133h) accepts calls only from
  the System VM, and dispatches 80h-8Ah by the caller's BX. BX is the
  VM id, 1 for the System VM, which is what the driver passes.
- `VDD_DRIVER_REGISTER` (9039Ch) stores the caller's ES:DI at
  11020h/11022h as the callback, and clears bit 2 of the flag byte at
  108C0h. `VDD_DRIVER_UNREGISTER` sets that bit.
- The routine that gives the display back to the System VM (F034Ch)
  calls mini-VDD entry 6 (PRE_VGA_TO_HIRES), then the stored callback by
  `Begin_Nest_Exec` + `Simulate_Far_Call`, then entry 7
  (POST_VGA_TO_HIRES), all inside `Begin_Critical_Section`. Its caller
  (F1470h) skips the whole restore while any of bits 0-2 of 108C0h is
  set.

So the VDD does call the driver's `ResetHiResMode` on the way back, from a
nested execution inside a critical section. Nothing in the VDD repaints
Windows; the DDK display samples do that themselves.

## What the DDK samples do and the driver did not

`98DDK\src\display\mini\framebuf\SSWITCH.ASM`, used by every Windows 98
DDK display sample: at Enable the driver hooks INT 2Fh. AX=4001h (a DOS
box takes the display) sets `BUSY` in the PDEVICE's `deFlags`, so the DIB
Engine stops drawing to a screen it does not own. AX=4002h (Windows gets
it back) calls `ResetHiResMode` if the PDEVICE is still busy, then USER's
ordinal 275 to repaint every window. `ResetHiResMode` (VGA.ASM) clears
`BUSY` and calls `VDD_SAVE_DRIVER_STATE` after its mode set.

Velocity9x hooked nothing, and its `ResetHiResMode` reset the hardware
and palette only. Boot 680's frozen desktop is that: the mode is back and
nobody repaints.

## The change

`src\display16\runtime.asm` gains the hook (4001h/4002h handled with
carry clear, everything else chained by a far return built over the
interrupt frame, so the previous vector lives in DGROUP and needs no code
alias), its install and removal through INT 21h 35h/25h, and a lone
`VDD_SAVE_DRIVER_STATE` call. `src\display16\sswitch16.c` has the
handler: `BUSY` on 4001h; on 4002h `ResetHiResMode` if still busy, then
a repaint request (deferred if USER has disabled repaints).
`src\display16\ddi.c`:

- installs the hook at the end of a successful Enable, after resolving
  USER's repaint entry there, where `GetProcAddress` may run; removes it
  in Disable;
- `ResetHiResMode` clears `BUSY` and saves the VDD state;
- `V9xRequestRepaint`, a far entry for the handler.

New external names: `V9xScreenSwitch` (sswitch16.c, called by the hook),
`V9xRequestRepaint` (ddi.c), and the asm `V9xHookScreenSwitch`,
`V9xUnhookScreenSwitch`, `V9xVddSaveState`. None is exported from the
driver; Michael agreed to the design on 2026-11-10.

### Its own code segment

In `_TEXT` the change put the ati driver 219 bytes over its 57,344-byte
budget (57,563). The hook's assembly and sswitch16.c are now one code
segment, SSWCODE (246 bytes), listed in every family manifest; every
call into it and out of it is far. `_TEXT` after the move: ati 57,337
(7 bytes under budget), intel-gma 56,231, s3 51,579, vbe 49,817.

wlink packs small code segments of different classes into one NE
segment: on intel-gma, HSWCODE and SSWCODE share segment 0004. The
binary audit counted one executable segment per manifest name and so
failed there; it now counts the physical segments the map assigns to the
code rows (`scripts\audit-family-binary.ps1`).

## What this disputes in the August records

[`2026-08-29-dos-box-exit-tier0.md`](2026-08-29-dos-box-exit-tier0.md)
and the plan concluded that no code of ours runs on the path, because a
`-DosBoxTrace` build recorded no trace point. Every one of those runs was
a traced build. The trace writes a profile file and flushes it from each
point, `ResetHiResMode` included, and the VDD calls `ResetHiResMode`
nested inside a critical section. Boots 681-682 hung in text mode with
the traced build and boot 680 did not without it. So the August signature
is probably the trace hanging inside the callback, not the callback being
skipped. That the write is what hangs is inferred from those three boots;
it was not separately measured. `-DosBoxTrace` should not be used on this
path again.

Also disputed: "the main VDD requires the full banking and latch set
before it will manage this transition". The VDD's restore path calls the
registered callback with no such test.

## Not known

- Whether the Intel family (GMA 950, HD 4600) comes back the same way.
  The netbook's return was a hard lock, not a freeze, in 2026-09.
- Whether the striped band at boot 680 is the VDD's restore, or memory
  the DIB Engine drew into while it should have been busy.
- Tier-0 (`vbe`) on the Mach64 guest: not rerun.
- The DOS session's cursor after a trip. Typed in full screen, `ver`
  lands on row 0 over the banner, and after the return the next command
  is appended to it (`verexit`, "Bad command or file name"). Main's build
  showed the same row 0 at boot 680, so it predates this change. The INT
  10h guard's deferred mode set (ba81ce11), which runs before the VM's
  first INT 10h in full screen, is a suspect; not checked.

Gates: check-tree and run-checks pass.
