# Text acceleration hangs the physical Trio64 within a minute of the desktop

Date: 2026-09-06
Status: **open. Machine hung, awaiting a physical reset; cause not yet
measured.** Read with
[the DOS-box desktop-doubling issue](2026-09-06-dos-box-doubles-the-desktop-on-physical-trio64.md),
found the same day on the same card: the screenshot this record calls a
capture artefact was the real display, the DOS boxes the agent spawned had
already corrupted it, and the hang followed within seconds. Whatever copies
the framebuffer on a DOS-VM transition, doing it with a CPU-data text command
latched is the one form that can wait forever.
Severity: high for `GdiAccelText=1`; none for the shipping default, which is 0.

## What happened

Build 005 ([record](../decisions/2026-09-06-gdi-accel-005-text.md)), commit
`29fb2d8`, deployed to BARRY - physical S3 Trio32/64 86C764, 2 MB,
`10.0.1.47:9869`, agent 0.6.0 - by the WININIT rename route: `V9XDISP.DRV`
44,614 bytes, `V9XHAL.DLL` 42,496, `V9XMINI.VXD` 10,236, replacing the
2026-08-27 build (35,742 / 29,184 / 9,436). `SYSTEM.INI` `[Velocity9x]` had
been empty; `GdiAccelText=1` was added. Mode 800x600x16.

Boot 104 reached the desktop. Within about thirty seconds after `DesktopReady`:

- `DIR` of the three files confirmed the swap.
- `TYPE C:\V9XDIAG\V9XHW.INI` read `Adapter=S3 Trio32/64 86C764`,
  `GdiAcceleration=gdi-fill-copy-overlap-text`.
- A screenshot completed. It shows the desktop with its icon labels drawn,
  at half height and doubled horizontally with black below - the 16-bpp
  surface read as 8 bpp, which the 2026-08-27 record also saw from this agent
  and attributed to the capture, not the driver.

The next command, a `put` of `V9XGDI.EXE`, timed out. Every command since has
timed out, and **ICMP ping gets no reply**, so the machine is hung rather than
the agent. Total desktop life: roughly one minute.

## What is and is not established

Established: the machine hung with text acceleration on, on its first boot
with it, and did not hang on the previous boot with the same three binaries'
predecessors and text off. The emulated Trio64 ran the same driver through 11
modes and two CrystalMark sessions without a hang.

Not established: which operation hung it. The labels in the capture are drawn,
so the text path executed at least once and returned. What ran between the
screenshot and the hang is whatever a Windows 98 desktop does in the next
seconds - the taskbar clock's minute tick is a text draw - and the agent's own
file write for the `put`.

## What the same day established elsewhere

The I/O trace on A8U4I5 measured that a windowed DOS box's video BIOS writes
02H to 4AE8H and nothing else on the engine file, dropping the Trio64 out of
enhanced mode ([the DOS-box record](2026-09-06-dos-box-doubles-the-desktop-on-physical-trio64.md)).
BARRY's screenshot moments before the hang showed that state. A text
primitive whose PIX_TRANS feed is in flight when that bit clears is a CPU
writing data to an engine that will never take it; candidate 2 below, with
the enabler now measured rather than guessed. Still not reproduced under the
single-string probe, which was built and never run: A8U4I5 then turned out to
hard-lock on plain framebuffer read-after-write with no acceleration at all,
so it cannot host the probe, and BARRY is down.

## Candidates, in the order to test

1. **The FIFO poll never sees "empty" on silicon.** `v9x_gdi_trio_wait_fifo_empty`
   waits for the low byte of `9AE8H` to read zero. If the real register's bit
   convention is inverted from the assumption (bits set for *free* slots), the
   wait spins its full `V9X_GDI_FIFO_SPIN_LIMIT` (2 million ISA-timed reads,
   seconds) and then poisons - which would stall, not hang, and every later
   text call would decline. A hang needs something worse.
2. **`rep outsw` to `PIX_TRANS` stalls the bus.** If the chip holds the CPU
   with wait states until the engine accepts data, and the engine is not in a
   state to accept it, the string instruction never completes and nothing -
   not the timer, not the network stack - runs again. That is a hard hang with
   exactly this signature. The emulator never stalls a port write.
3. **The command word.** `53B3H` is what the emulator special-cases and what
   the emulator's own consumption model required, but its bit 1 was never read
   from an S3 databook. If silicon treats the word differently - a different
   transfer width, a wait for a different data register - candidate 2 follows.
4. **The `wait_idle` at the end of the callback**, if the engine never goes
   idle after a CPU-data command that received the wrong amount of data,
   spins its limit and poisons - a stall, not a hang, unless combined with 2.

What distinguishes them: whether the hang is immediate on the first accelerated
string or accumulates. The labels drawn in the capture say the first strings
completed, so candidate 2 alone does not fit unless the stall is data-dependent
(a row length, a FIFO state). A hang on the clock tick would be one string of
digits, opaque, in the taskbar font.

## Recovery

A physical reset. The machine will boot with `GdiAccelText=1` still in
`SYSTEM.INI` and may hang again within the minute. A watcher on the host polls
the agent and `put`s the original `SYSTEM.INI` (empty `[Velocity9x]` section)
the moment it answers, which the previous boot's timing suggests is enough. If
it is not: boot to the command prompt (F8) and remove the `GdiAccelText=1`
line from `C:\WINDOWS\SYSTEM.INI`.

The three new binaries can stay: text off, they are the same fill, copy and
overlap paths this machine already ran on 2026-08-27, plus the Trio3D and
software-D3D work since, none of which touches this chip's GDI path.

## Next measurement

Not another blind boot. The instrument to build is a bounded single-string
probe: `V9XGDI /textprobe` drawing one known string with `GdiAccelText`
enabled through the INI, after the driver has come up with it *disabled* -
which needs a runtime enable (an escape that flips the primitive bit for one
call) so the desktop itself never draws through the path. Then the first
string on silicon is the probe's, its `V9X_GDITEXTDUMP` and stats are read
before anything else runs, and a hang is attributable to one operation with
known arguments.
