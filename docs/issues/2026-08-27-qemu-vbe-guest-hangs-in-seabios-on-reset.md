# The vbe QEMU guest hangs on reset, and only a fresh process recovers

Date: 2026-08-27
Status: **open.** Not a Velocity9x defect - two distinct hang signatures, both
before any of this driver's code runs, and the serial log confirms it wrote
nothing on a hung boot. Blocks the `vbe` mode matrix, not the enable gate.

## Symptom

`Win98SE-QEMU-StdVGA` (QEMU 4.2.0, `-machine pc-i440fx-4.2 -no-acpi -cpu
pentium2`, host `127.0.0.1:9872`) intermittently fails to come back from a
reboot. The console sits at:

```
SeaBIOS (version rel-1.12.0-59-gc9ba5276e321-prebuilt.qemu.org)
iPXE (http://ipxe.org) 00:03.0 CA00 PCI2.10 PnP PMM+07F90E30+07EF0E30 CA00

Press ESC for boot menu.
_
```

and never proceeds. It does **not** reach the Windows boot logo.

## What it is

The CPU is halted in the BIOS ROM waiting for an interrupt that never arrives.
From the QEMU monitor while hung:

```
VM status: running
EIP=0000ba15  CS =f000 000f0000     <- executing in the BIOS ROM segment
EFL=00000246 [---Z-P-] CPL=0 HLT=1  <- halted, and IF is set
ES =dc00                             <- an option ROM segment
```

`HLT=1` with interrupts enabled is the whole diagnosis: SeaBIOS has executed
`hlt` expecting a timer or device interrupt to wake it, and nothing does. That
points at the PIT/PIC or the IDE controller not being re-initialised across a
reset, which is emulator and machine-configuration territory.

## A second signature, same trigger

A later reset of the same guest hung in a **different** place, which is what
makes "the reset path is unreliable" a better description than any single
stopping point:

| | Where it stops | CPU state |
|---|---|---|
| Signature A | SeaBIOS, just after "Press ESC for boot menu" | `HLT=1` - halted with interrupts enabled, waiting for one that never arrives |
| Signature B | Windows 98 splash screen, 640x400 | `HLT=0`, `EIP=00009dfb`, `ES=0000` with 16-bit limits, 168 s of host CPU burned - spinning in real mode |

Signature B is a livelock in early real-mode boot, not a stall. Both follow a
reset of an existing QEMU process, and both are cured by killing QEMU and
relaunching.

## The serial log excludes this driver, and says so positively

The guest logs to COM1, so there is a record rather than an inference. On the
boot that hung at the splash, **Velocity9x wrote nothing at all** - no
`V9X-MINI` lines, no `V9X-DRV` lines. The last entries in the file are from the
*previous*, successful session and end cleanly:

```
V9X-MINI vbe-collect done
V9X-DRV load build=6f86e94
V9X-DRV lfb=0xFD000000 bytes=0x01000000
V9X-DRV enable-ok mode=800x600x16 lfb-mapped
V9X-DRV disable
```

Load, enable at 800x600x16, then a clean `disable` on shutdown. That is a driver
doing its job and being torn down properly.

It also fits the CPU state: real mode with 16-bit segment limits is *before*
Windows loads protected-mode VxDs, which is where both the mini-VDD and the
display driver live. The hang precedes any of this driver's code running, and the
same disk image with the same driver reached the desktop in about 30 seconds
minutes earlier.

## What recovers it, and what does not

| Action | Result |
|---|---|
| `sendkey ret` (in case the boot menu was waiting) | **no change** - it is hung, not prompting |
| `system_reset` from the monitor | **still hung** |
| Killing QEMU and relaunching the same command line | **recovers, and boots in ~30 s** |

That last row is the important one: the disk image and the guest are fine. A
fresh QEMU process boots this image quickly and cleanly. Only *resetting* an
existing process wedges, whether the reset comes from inside the guest
(`v9xctl reboot`) or from the monitor.

## Why this is not the display driver

It was first mistaken for one - the matrix died while stepping to 800x600x8, so
an 8-bpp mode-set looked like the trigger. It is not:

- The hang is in **SeaBIOS**, at a point where no Windows code, let alone a
  display driver, has executed.
- This is the `vbe` family: `Acceleration=none`, `GdiAcceleration=none`, and the
  `/accel` run on this guest recorded all 3231 calls declining at the very first
  gate with `Enabled=0`. No accelerated primitive can run here at all.
- After recovery the guest came up in 800x600x**8** with `Stage=enable-ok` and a
  clean framebuffer, so the 8-bpp mode itself works.

## The framebuffer is clean, and the streaks were not the guest

A photograph of the SDL window at 800x600x8 showed thin red and green horizontal
streaks across the desktop. A QEMU monitor `screendump` taken at the same time -
which reads the emulated framebuffer directly, bypassing Windows, GDI and
`GetDIBits` entirely - is **pixel-clean**: every row that differs sharply from
its neighbour is a legitimate content edge (title bar, the black CONTENTS bar,
the window bottom, the taskbar).

So those streaks were in the host-side SDL rendering or the capture of it, not in
anything the driver produced. Worth remembering as a technique: `screendump` is
the one capture path on this guest that no driver or GDI bug can influence, which
makes it the right instrument for "is the screen actually wrong?".

## Impact

- **The `vbe` enable gate is not blocked** and has now passed on the merged
  build: `Stage=enable-ok`, `/accel` `Result=PASS` with the engine-less shape
  above. That closes the long-standing gap where `vbe` had never been gated in
  the `gdi-accel` series.
- **The `vbe` mode matrix is blocked**, because it reboots between modes and each
  reboot is a chance to wedge. Running it needs either the reset problem fixed or
  a matrix mode that uses live mode switches instead of reboots - live switching
  works fine here, verified 8 bpp to 16 bpp with no reboot.

## Worth trying next, cheapest first

1. **Drop `menu=on`** from `-boot order=c,menu=on`. The hang is right after the
   boot-menu prompt is printed.
2. **Remove the iPXE option ROM** by giving the NIC `romfile=` (empty), since an
   option ROM at CA00 is in the picture and ES points into ROM space at DC00.
3. **Try `-machine pc` without the explicit `-4.2` version**, or a newer QEMU.
   QEMU 4.2 was chosen for this guest for unrelated reasons recorded in the
   Stage 1 handoff, so changing it is not free.
4. If the reset path stays broken, treat "reboot the vbe guest" as an operation
   that means kill-and-relaunch, and teach the matrix runner that for this target.

The exact working command line is recorded in the Stage 1 handoff and was
captured from the running process during this investigation.
