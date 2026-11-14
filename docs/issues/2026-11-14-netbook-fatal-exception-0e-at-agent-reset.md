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

## 2026-11-14, later: what the address is, and what the reset does around it

Read, not run. Nothing below was reproduced.

**The agent's reset** (`v9x-remote-agent`, `src/guest/reset.c`, 0.16.0's
is the same shape): send `WM_ENDSESSION(TRUE)` to every top-level window
of every other process, flush the registry, write `reset-flush`, freeze
the log, take a level-1 lock on C: (Int 21h 440Dh/084Ah through VWIN32),
sleep 3 s, write CF9h. `reset-flush C` is therefore the last line of
every reset, good or bad, and about four seconds pass between telling
every program the session is over and the reset itself.

**The address.** The netbook's `KERNEL32.DLL` (471,040 bytes) loads at
BFF70000; BFF86EBD is in `.text`, in an unexported function that starts
at BFF86DDF. It reserves 128 KiB, commits the first 64 KiB of it (both
results checked), does the same again, then reads two words at +0Ch and
+0Eh of its first argument and steps past 10h: the named and id entry
counts of an `IMAGE_RESOURCE_DIRECTORY`. The faulting instruction is

    BFF86EBD: mov word ptr [eax],di

the first write into the first block it committed, and `di` is a shift
count it has just worked out from FFFFh. That is the first word of a
16-bit resource table (its alignment shift): the function builds the
table 16-bit USER reads for a 32-bit module's resources. So some
program was loading a module with resources, or first using one's, and
the first touch of memory just committed could not be satisfied.

**What follows from that, and what does not.**

- It is a module load in some program, not the agent's own reset code:
  after `reset-flush` the agent makes one `CreateFile`, one
  `DeviceIoControl` a drive and a `Sleep`.
- Two readings fit, and nothing on the disk separates them (the agent's
  `stat` returns no file times; VNC.LOG and EVENTS.LOG are written by
  the agent and show only the starts it saw):
  1. **In the old boot, in the four seconds.** A program reacting to
     `WM_ENDSESSION`, or one that happened to start then, loads a module
     while C: is locked. The blue screen stops every thread, the agent's
     among them, so CF9h is never written and the machine sits there.
     The staged files are installed by the power cycle's boot.
  2. **In the next start of Windows**, in a program that runs before
     the agent does.
- Reading 1 predicts that a key on the blue screen ends that program
  and the machine then resets itself within a few seconds. Reading 2
  predicts it goes on to the desktop.
- The failing resets are not marked out in the agent's log from the
  five good ones between them: 33 and 32 windows notified against 33,
  33, 34 and 33; uptimes of 4.5 minutes and more on both sides. The
  second failure came 23 s after a V9XD32 run ended, the good ones 2
  to 14 minutes after their last program; the first failure about 2.5
  minutes after 3DMark 2001 SE was closed. Two points, not a pattern.
- Why a first touch of committed memory would fail with 800 MiB free
  is not explained. That the volume lock has a part is a guess.

## 2026-11-14, later still: a key brings it back, and six trials that did not reproduce it

**A key does not get past it.** Michael, both times: a key press on the
blue screen brought the same screen straight back, two or three times,
before he power-cycled. So the program could not be ended; ending it
faulted again. That fits a fault that recurs on every path out, and
does not by itself say which boot it was in (the two readings above).

**The window, measured.** In the agent's log the end-of-session pass
takes about 1.05 s after `reset-accepted` and `reset-flush` follows at
once; with the 3 s settle, the volume is locked from about 1.1 s to
4.1 s after the reset is accepted. (The netbook's clock runs about
4.5 minutes behind the host's.)

**Six trials on boot 344's build, boots 344 to 350, each a reset from
`v9xctl` as the release checks send it.** All six reset cleanly and the
agent was back within two minutes:

| Trial | What ran around the reset | Boot |
|---|---|---|
| a | nothing (the script's program start failed to parse) | 344 → 345 |
| b | Notepad started 2.7 s after the reset was sent: inside the lock | 345 → 346 |
| c | 3DMark 2001 SE started 2.7 s after: inside the lock | 346 → 347 |
| d | Notepad started 0.5 s after: during the end-of-session pass | 347 → 348 |
| e | V9XD32 run to its end (PASS), reset 20 s after | 348 → 349 |
| f | V9XD32 ended by the agent's 4 s exec limit (as the hung `/ddi5` run was in boot 343), reset 20 s after | 349 → 350 |

So a program merely starting inside the lock window, or a V9XD32 run
just before, or one killed, is not enough. Two failures in about
eighteen resets that day.

Not tried: a program left half-closed at the reset (boot 338 had had
3DMark 2001 SE opened and closed through `close-window` several times
in its last minutes; whether every instance had exited was not
checked); the netbook's NETGEAR wifi utility, a tray program that
polls, as the one faulting; the agent's reset without the volume lock.
The agent is its own repository (`v9x-remote-agent`); a change to its
reset is a change there.

Until it is understood: when the agent stops answering after a
`reset`, look at the panel. If it shows this screen, a key will not
clear it; power-cycle. The staged files are installed by the boot that
follows.

Related, found the same evening: a V9XD32 `/ddi5` run that is ended
from outside leaves `V9XD32.EXE=5` under `[Velocity9x.Direct3DDdi]` in
SYSTEM.INI, and every later run of the probe then "restores" that 5.
The DDI 8 tests are skipped (`T19Result=SKIP-NO-PS`, `RunDdi=5`) until
the line is taken out by hand, across restarts.
