# GM965: the texture pool's Phase 2 mapping probe stops the machine

Date: 2026-11-14. Status: open. The machine has not been reached since;
which step stopped it is not known.

Machine: TOMMOHP (`M-KYHT4Q`, Tom Rossell's HP laptop, GM965 GMA X3100
`8086:2A02`), through remote help, on the driver the 0.50.0 release checks
left (boot record: `Stage=enable-ok`, `DpmiMap=01000000:ok`,
`MtrrWrite=ok s=3 b=d0000001 m=ff000800`, `[IntelPool]
Result=not-gated-device`).

## What was run

`G3PM.VXD` + `V9XG3PM.EXE`, the netbook's Phase 2 probe
(`2026-11-12-intel-gen3-texture-pool-phase2.md`), extended for the GM965
the way Phase 0's probe was
(`../decisions/2026-11-13-intel-gm965-texture-pool-phase0.md`): `2A02`
added to the VxD's device list, and the publisher passing GTTMMADR +
512 KiB as the GTT base. Not committed. Both files were put in
`C:\V9XREMOTE\JOBS\G3PL` and hashed back, then `V9XG3PM.EXE` was started
with its defaults: allocate 8,192 pages, write their PTEs into entries
`800`-`27FF`, a pattern through the aperture and back, two 640x480 mode
cycles, check, restore.

## What happened

The agent took the `exec` (the relay shows the command delivered) and
never checked in again: no result, and no answer for the eight minutes
watched after. The probe's exec timeout was 180 s, so this is not a slow
run. Phase 0's read-only probe ran twice on this machine the day before
without trouble.

Nothing the probe does survives a power cycle: the VxD is loaded
dynamically, the BIOS rebuilds the GTT at boot, and no file the driver
reads was changed.

## Not known

- The step. The publisher writes each step's name to
  `C:\V9XDIAG\V9XG3PM.INI` before running it; the file is the first thing
  to read when the machine is back, if the write reached the disk.
- Whether the machine hung, faulted to a blue screen, or lost only its
  network.

## What differs from the netbook's passing run, none of it tested

- The GTT is written at GTTMMADR + 512 KiB, not through a GTTADR BAR. The
  2026-11-13 Phase 0 record only read it.
- Gen4 PTEs carry address bits 35:32 in bits 7:4. The frames Phase 0 saw
  were below 4 GiB, so the entries written should match Gen3's; the probe
  does not check.
- The driver maps and write-combines 16 MiB of GMADR here, where the
  netbook's maps 64 MiB. The probe maps the pool's 32 MiB itself, so its
  range (GMADR 8 to 40 MiB) is half under the driver's WC MTRR and half
  under the UC default.
- The mode cycle goes through the 0.48.0 panel-timing patch on this
  machine (1280x800 through a rewritten VBIOS slot).
- Spec update 316274-009 erratum 4: visual corruption with integrated
  graphics under Vista's paging model with Flex Memory in dual-channel
  asymmetric mode. Corruption, not a hang, and this machine's DIMM
  layout is not known.

## Next

- The owner has to restart the machine. Then read `V9XG3PM.INI`.
- Do not run the map probe on this machine again as it is. Split it so
  each step is its own run with its own flushed record (map and read back
  PTEs only; then the aperture pattern; then the mode cycle), starting
  with the smallest: one PTE.
- The driver's pool path stays gated to the netbook.

## 2026-11-14, later: it came back by itself, and the probe passes

Status now: open, not reproduced; the cause is not known.

The machine answered again at 09:04 on the same boot (55): it had not
been restarted. Its agent log has `exec-start` at 02:33:36 and
`exec-complete` at 09:02:45, exit `FFFFFFFD` after 23,352 s, with a poll
gap of 23,353 s: the whole system stood still for 6.5 hours inside the
probe's MAP call (`V9XG3PM.INI`: `Step=map`, the allocation accepted),
then carried on. A read-only `G3PL` run found the pool's entries all
`7FF7E001`, hash `90EB6205`, and the free-page count back.

The probe was then staged, shrunk and given a time-stamp watchdog, proved
on the netbook, and run on this machine from 4 pages and PTEs only up to
the full 8,192 pages with two mode cycles: every run passes, none times
out, the same frames included
([record](../decisions/2026-11-14-intel-gm965-texture-pool-phase2.md)).

So the "which step" question above has no answer: nothing in MAP is slow
or wrong when the machine is awake. The untested lead is the machine's
state at 02:33: 76 minutes without input, unattended, and back at the
moment its owner may have touched it. The test is the staged probe at
full size after 80 minutes or more without input; its watchdog turns a
slow aperture into a `TimedOut` line instead of a frozen machine.

## 2026-11-14, 10:53: a blue screen, and the idle test passes

The owner (through Michael): the machine showed a blue screen; no text
was noted. So the 6.5 hours were a fault waiting for a key, not a slow
loop, and the watchdog would not have caught it.

The idle test ran at 10:52, 90 minutes after this project's last command
to the machine (whether the owner touched it in between is not known),
gentlest step first: the IGD's config space (`V9XHSW.EXE`, port I/O
only), the read-only `G3PL`, then `V9XG3PM` at stages 1, 2, 3 and 4 on 4
pages and at full size. Every step passes; the full run's times match
the morning's (write 197 ms, read back 1,247 ms). The IGD's config space
is byte for byte the awake baseline taken at 09:23: command word 0007,
power state D0 (`PMCSR` at D4h = 0000). Evidence in
`../probe/gm965-texture-pool-phase2-2026-11-14/idle/`.

The idle lead is not supported, as far as this test reached an idle
machine. The fault has happened once in what is now 21 MAP calls on this
boot (the first), and never in the staged binary. What the first run had
that the others did not, apart from being first: the unstaged binary,
and 76 minutes since boot-time activity. Not known which, if either,
matters. The blue screen's own text is the missing evidence.
