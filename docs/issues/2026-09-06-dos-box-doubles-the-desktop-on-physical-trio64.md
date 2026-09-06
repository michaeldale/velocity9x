# A DOS box rewrites the desktop at twice its stride on physical Trio64 silicon

Date: 2026-09-06
Status: **root cause measured (see "What the I/O trace found"); a fix was
measured working and is not merged, because the same machine then hard-locked
for a reason that turned out to be unrelated - see "The hard locks".**

## What the I/O trace found (later the same day)

The `-IoTrace` mini-VDD build traps the whole 8514/A port file for V86 VMs and
logs every access. Across one windowed DOS box the DOS VM makes **exactly one
access to the engine file: a byte write of 02H to 4AE8H**, ADVFUNC_CNTL, by
its video BIOS. No engine command, no data port. That write clears ENB EHFC.

So the "doubling" is not a copy. Once the Trio64 leaves enhanced mode with the
desktop still displayed, CPU and CRTC both address the same DRAM as VGA planes:
everything drawn before appears interleaved - twice side by side at half size -
anything drawn after looks right, and a repaint "repairs" it because the two
sides agree again. It is also why the engine stops landing writes (2026-08-27)
and why a CPU-data text command caught mid-transfer never completes (BARRY).
The intermittency is whether anything repaints before someone looks.

**Fix, measured:** the `-ShieldAdvFunc` variant swallows a V86 VM's write to
4AE8H (reads pass through, the System VM passes through). Six DOS boxes with
acceleration on, six intact desktops, against five doubled of five without
it. Not merged as a default: the machine then hard-locked under the harness,
and until that was traced (below, unrelated) nothing new could be trusted.
The always-on form of the shield in `loader.asm` was proposed and declined
for now.

## The hard locks

A8U4I5 hard-locked, no ICMP, seven times running the `/accel` harness or
parts of it. Bisected on the machine:

| Run | Outcome |
|---|---|
| `/accel` via a DOS box, shield mini-VDD | lock |
| `/accel` via Win32 exec, no DOS box, shield mini-VDD | lock |
| `/accel` via exec, trap-free mini-VDD | lock |
| probe `pump` 2000 fills, idle wait each | done |
| probe `pumpn` 2000 fills, no waits at all | done |
| probe `pumpc` 2000 overlapping BitBLTs | done |
| `/accel /kinds:1 /nocompare` (500 fills, no readback) | done |
| `/accel /kinds:1` (fills + readback), `GdiAccelSync=0` | lock |
| `/accel /kinds:1`, `GdiAccelSync=1` (engine idle before every return) | lock |
| **`/accel /kinds:1` with `GdiAccel=0`** (DIB Engine fills, same readback) | **lock** |

The last row is the verdict: with the engine never touched by the driver, CPU
writes through the linear aperture followed closely by CPU reads of the same
region lock this host. Screenshots (reads only) and the desktop (writes) both
survive; the harness is what mixes them at full speed. The same card ran this
harness clean in BARRY's Pentium on 2026-08-27 and the Trio3D in this same
slot ran 3DMark 99. This is the PCI Trio64 against a Pentium III board - the
fault class a board's PCI delayed-transaction and passive-release settings
decide, and the one S3's own driver carried a `BusThrottle` switch for.

Not established: whether the stock S3 driver locks the same way here, and
whether a BIOS PCI setting cures it. Those are the two controls before any
further driver work is judged on this machine. Nothing about GDI acceleration
or text is implicated by these locks.

## The original record follows

Status at the time of writing, superseded above: open, reproduced on demand,
mechanism unknown. Rate measured; every driver-side hypothesis tested that
afternoon was dead. The next instrument was named at the end, was built, and
answered.
Severity: high. On A8U4I5 with the shipping defaults, the first DOS box after
boot destroyed the desktop in five of five boots.

## What it looks like

The desktop appears twice side by side, each copy half width and half height,
with black below; anything drawn afterwards is at its correct place and size.
Photographed on A8U4I5's monitor by the user, and identical in the agent's GDI
readback. Both BARRY (2026-09-06, after the text build was deployed) and
A8U4I5 (before anything was deployed to it) showed it; the 2026-08-27 BARRY
record's "post-run display artifact - the window duplicated at several scales"
was almost certainly the same thing.

It is memory content, not scanout and not GDI's pitch. The CRTC file CR00-CR70
and the S3 sequencer read identically before and after, `V9XBOOT.INI` still
says `pitch=1600`, and a freshly repainted Start button lands where it belongs.
Reading the doubled image against the original: displayed byte `b` holds what
was at byte `2b`. Something copied the visible framebuffer onto itself as a 2:1
byte subsample, 480 KB of it. A desktop refresh (F5) repaints it clean.

## Trigger and rate

A windowed DOS VM: every `v9xctl shell` is one (COMMAND.COM). Measured on
A8U4I5, physical S3 Trio64 `5333:8811` 2 MB moved from BARRY, Pentium III,
Windows 98 SE, 800x600x16, agent 0.6.2:

| Condition | Doubled / DOS boxes |
|---|---|
| `GdiAccel=1` (shipping), first DOS box after boot | **5 / 5** (boots 46, 47, 48, 50, 51) |
| `GdiAccel=0`, no other engine-port activity | **2 / 14** (boots 49, 57, 58) |
| `GdiAccel=0`, the register probe run first, even read-only (`nocmd`) | **5 / 5** (boots 52-56) |

So it is intermittent on its own and near-certain after any 8514/A port
activity - the driver's accelerated fills, or the probe merely reading engine
registers and writing Read Register Select. What the two have in common is
port I/O to 8514/A addresses and CRTC index writes; neither leaves a register
value that matters (below).

## What is dead

Each of these was an A/B on the machine, one variable, one DOS box, screenshot
through a readback that matched the monitor:

- **The mini-VDD's memory-size entries** (`REGISTER_DISPLAY_DRIVER`,
  `GET_TOTAL_VRAM_SIZE`, added 2026-08-29). Built out with `-NoVramSize`: still
  doubles.
- **ADVFUNC bit 0 state at DOS-box time.** Cleared from ring 3 before the box
  (`V9XTC32 off`): still doubles.
- **A latched real command in 9AE8H** re-issued by a VDD restore. NOP left
  latched (`nop`): still doubles.
- **Any latched engine register.** Every register the probe writes restored
  to its pre-run readback (`put`): still doubles. The pre-run state was itself
  wide-open scissors, `MAJ/MIN_AXIS` 0FFFH, `PIX_CNTL` A000H - the same values
  the probe and driver leave.
- **The engine having executed at all.** Probe with no command of any kind
  (`nocmd`): still doubles. This is the one that turns "engine state" into
  "port activity".
- **GDI acceleration as the cause.** It raises the rate from about 1 in 7 to
  5 in 5; with it off the fault still occurs. It is an aggravator, not the
  cause, and an earlier conclusion in this session that it was the cause rested
  on one clean boot that was the 1-in-7.

Register diff, clean against doubled, same boot: `4AE8H` bit 0 cleared and
`SR08` relocked - the 2026-08-27 findings - and nothing else. `Rb4AE8` reads
000BH on this machine where BARRY read 008BH; the driver reads 0401H through
its own path. The extra bits differ by machine and by reader and carry no
information here.

## What the mechanism has to be

Something with access to the whole framebuffer copies it at the wrong stride
during DOS-VM creation, and does so more readily once the 8514/A register file
has been touched from ring 3. The candidates left are outside this driver's
code:

1. **The real video BIOS running in the new V86 VM.** The VDD traps the
   standard VGA ports for the VM but not the S3 extended and 8514/A ports;
   `4AE8H` bit 0 being cleared on every DOS box is that BIOS's mode set
   reaching the hardware (2026-08-27). If that mode set also uses the engine to
   move or clear memory, with geometry read from registers the driver or probe
   left, that is a copy with the wrong parameters. The rate dependence on prior
   port activity fits a BIOS that reads engine status to choose a path.
2. **The main VDD's chip-aware save and restore**, which writes `SR08` and
   `4AE8H` and may write more that is not readable back.

Neither is distinguishable from the evidence so far, because nothing here can
see what a V86 VM does to the ports.

## The next instrument

The mini-VDD installs `Install_IO_Handler` traps on the 8514/A port set
(`42E8H`, `4AE8H`, `82E8H`-`E2E8H` at the ..E8H stride) **for V86 VMs only**,
logging the first N port, value and direction per VM creation to the serial
line and passing the access through unchanged. One DOS box then says whether
the VM's BIOS touches the engine and with what, and whether the count differs
between a boot that doubles and one that does not. If it does touch the
engine, the fix is the DDK's own for this class: virtualize those ports for
V86 VMs so the BIOS talks to a shadow and the desktop is never the target. If
it does not, the actor is the VDD and the record moves to its restore path.

Not to be run with `GdiAccelText=1` anywhere. BARRY's hang
([issue](2026-09-06-text-acceleration-hangs-physical-trio64.md)) followed this
fault by seconds and a re-issued or mis-parameterised CPU-data command is the
one form of this that waits forever.

## Instruments added

`V9XTC32` grew `Cr00-Cr2F`, the unlocked `SrU09-SrU1F`, a `Pre*` capture of
the engine's latched state before it writes anything, and the arms `off`,
`nop`, `put`/`putm`/`putw`/`putg`, `nocmd`. `build-minivdd-skeleton.ps1`
grew `-NoVramSize`. Screenshots on A8U4I5 fail intermittently in the
controller with "hostName too long" and must be retried; a run that reported
ten straight failures as "doubled" is why the rate table above cites only
runs with real captures.

## State left behind

A8U4I5: display driver and HAL from `29fb2d8`, `GdiAccel=0` in `SYSTEM.INI`
(left off deliberately: on this machine it makes the first DOS box a certain
loss of the desktop), `GdiAccelText` absent. The installed mini-VDD is the
`-NoVramSize` differential build from boot 47 onward, not the shipping one; a
`-NoDpms` build was made and never deployed. `C:\V9XTC32.EXE` and
`C:\V9XGDI.EXE` are the current builds. BARRY: hung with `GdiAccelText=1`,
awaiting a reset; on its next boot the same DOS-box fault applies to it.
