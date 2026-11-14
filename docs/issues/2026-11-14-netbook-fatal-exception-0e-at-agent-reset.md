# Netbook stops on "fatal exception 0E at 018F:BFF86EBD" when the agent resets it: twice in one day, same address

Date: 2026-11-14. Status: open, cause not found. Two occurrences in
about twelve agent resets that day; the others completed.

Machine: MICHAEL-NETBOOK (W9QI2BE2, HP Mini, 945GSE / GMA 950), Windows
98 SE, v9x-remote-agent 0.16.0 (`dev-fault-3`).

## What was seen

Both times `run-release-checks.ps1` had staged the eight driver files by
WININIT rename and sent the agent's `reset`. The agent never answered
again and the address stopped answering ping. Michael found the panel
on a blue screen, the same text both times (photographs, not kept in
the tree):

    A fatal exception 0E has occurred at 018F:BFF86EBD. The current
    application will be terminated.
    * Press any key to terminate the current application.
    * Press CTRL+ALT+DEL again to restart your computer.

He power-cycled it each time without trying a key. Each time the next
boot came up with the staged files installed (all eight by SHA-256,
WININIT.INI consumed).

| | Reset out of | Boot after | Driver build before the reset |
|---|---|---|---|
| 1 | boot 338, 18:26 guest time | 339 | cap-only measurement build (specular cap claimed, not drawn) |
| 2 | boot 343, 20:48 guest time | 344 | `3ea75094` plus the two-stage pixel-centre move |

## What the agent's log says

The same three lines end both boots, as they end a reset that works:

    reset-accepted <job>
    reset-notified 33 1072ms      (32 1059ms the second time)
    reset-flush C

So the log does not place the fault: in what was left of the old boot
after the flush, or in a start of Windows that never reached the agent.
The boot counter went up by one across each, which the agent does when
it starts.

## What the two have in common, and what they do not

- BFF86EBD is in the range Windows 98 SE loads KERNEL32 at, and the
  screen is the one for a fault in a program's context, not a VxD's.
- Before the second, a V9XD32 `/ddi5` run in boot 343 had hung and been
  ended by the agent's 120 s limit (`exec-complete result=2
  exit=4294967293`). Whether that left a process or window behind for
  the reset's notification to meet was not looked at. Nothing is known
  to have been ended that way in boot 338; 3DMark 2001 SE had been
  opened and closed several times in it.
- The driver builds differ, and resets out of both kinds of build
  completed at other times that day.
- No `C:\WINDOWS\FAULTLOG.TXT` exists.

## Not tried

- A key press on the blue screen, to see whether Windows goes on and
  which program is named afterwards.
- Reading which module owns BFF86EBD on this machine's KERNEL32.
- The agent's reset path itself: what it calls after `reset-flush`.

Related, found the same evening: a V9XD32 `/ddi5` run that is ended
from outside leaves `V9XD32.EXE=5` under `[Velocity9x.Direct3DDdi]` in
SYSTEM.INI, and every later run of the probe then "restores" that 5.
The DDI 8 tests are skipped (`T19Result=SKIP-NO-PS`, `RunDdi=5`) until
the line is taken out by hand, across restarts.
