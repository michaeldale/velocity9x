# Intel Phase 1 measured on the netbook: the fingerprint passes, and the offsets are confirmed

Date: 2026-09-12
Status: accepted. **Phase 1 of `docs/plans/hardware-d3d-on-intel-gma950.md` is
complete.** Phase 2 (read-only GTT inventory) is unblocked.
Machine: MICHAEL-NETBOOK, HP Mini 110-1000, 945GSE / GMA 950, PCI
`8086:27AE` SUBSYS `308F103C` REV 03, AUO B101AW01 V2 panel, Windows 98 SE
booted direct from the live USB stick (MS-DOS compatibility mode disk I/O).
Package: `build\win98se-intel-gma`, build id `c049b20-dirty` (tree at
`c049b20` plus the uncommitted decoder fix that became `62a2c1d`; the binaries
hash-match that package's `SHA256.TXT`).
Sidecars: `2026-09-12-intel-phase1-physical-capture-INTELMM.txt`,
`-V9XBOOT.ini`, `-V9XHW.ini`, copied unmodified from `C:\V9XDIAG` on the
stick.

## How it was captured

The Intel package was installed with Have Disk on the function-0 display
devnode (see the false start below), the machine cold-booted, and the desktop
switched to 1024x576 at 16 bpp. The driver rewrites `INTELMM.TXT` on every
enable, so the file reflects that live mode. The stick was then read on the
development host and the file run through
`scripts\check-intel-mmio-capture.ps1 -ExpectedWidth 1024 -ExpectedHeight 576
-ExpectedBitsPerPixel 16 -ExpectedPitch 2048`, which reports `Result: PASS`,
`Flags 0000007F`, `RingQuiescent True`, `LivePipe 1`.

## What the hardware said

`V9XBOOT.INI`: `Stage=enable-ok`, aperture `D0000000` from BAR2, VBE 3.0,
6 cached modes, surface `1024x576x16` pitch 2048, VRAM 8,060,928 bytes.
`V9XHW.INI`: `Adapter=Intel GMA 950 (945GSE)`, `8086`/`27AE`. The PCI identity
matched for the first time on this machine; every earlier run was the generic
VBE family with `unmatched` ids.

`INTELMM.TXT`, BAR0 `FE980000` (512 KiB aligned, read from PCI config at run
time), all twenty repeat-read deltas zero:

| Register | Value | Decoded |
|---|---|---|
| PGTBL_CTL 2020 | `7FFC0001` | GTT enabled, at physical `7FFC0000` = 2 GiB minus 256 KiB |
| RING TAIL/HEAD/START/CTL | all `00000000` | ring not enabled, head equals tail |
| HWS_PGA 2080 | `1FFFF000` | hardware status page as left by the VBIOS |
| PIPEACONF 70008 | `00000000` | pipe A disabled |
| HTOTAL_A / VTOTAL_A / PIPEASRC | `031F027F` / `020C01DF` / `027F01DF` | 640x480 in 800x525 totals, stale |
| DSPACNTR/ADDR/STRIDE | `00000000` | plane A disabled |
| PIPEBCONF 71008 | `80000000` | pipe B enabled |
| HTOTAL_B 61000 | `053F03FF` | 1024 active, 1344 total |
| VTOTAL_B 6100C | `029F023F` | 576 active, 672 total |
| PIPEBSRC 6101C | `03FF023F` | source 1024x576 |
| DSPBCNTR 71180 | `95000000` | enabled, format 5 = 16 bpp 565, pipe select B |
| DSPBADDR 71184 | `00000000` | plane at aperture offset 0 |
| DSPBSTRIDE 71188 | `00000800` | 2048 bytes |

Decoded: live pipe B, live plane B, timing 1024x576 in 1344x672, source
1024x576, plane 16 bpp, stride 2048, address 0. Every Phase 1 relationship bit
is set and the ring-quiescent bit is set as well.

## What this confirms

- The allowlist offsets taken from the Gen3 audit are the right registers on
  this chip. Timing, source and plane geometry all agree with the mode the
  driver believes it set, and the 1344x672 totals equal the panel EDID's
  native timing recorded in Phase 0.
- The LVDS is scanned from pipe B with plane B. Pipe A holds stale 640x480
  timings with PIPECONF clear, consistent with the VGA/boot mode having used
  pipe A. The decoder's plane-to-pipe pairing fix (`62a2c1d`) was therefore
  not exercised here; it remains correct for other VBIOS layouts.
- The GTT lives at the top of the 2 GiB, in the stolen region: `7FFC0000` is
  256 KiB below 2 GiB, matching Phase 0's arithmetic of GGC 8 MiB stolen minus
  4F00h's 7.69 MiB usable = 256 KiB GTT plus 64 KiB scratch. The mini-VDD's
  MTRR readout independently shows `Mtrr1` as an 8 MiB uncacheable range at
  `7F800000`, which is that stolen block.
- The ring is idle at desktop with the VBIOS in charge, which is the Phase 3
  precondition, now observed rather than assumed.

## Hypotheses this kills or leaves open

- Killed: "the Gen3 offsets may not apply to the 945GSE VBIOS's mode". They
  do; three independent relationships agree.
- Killed: "the plane address bound against the VBE usable-memory figure will
  produce a false REVIEW". The plane sits at offset 0.
- Open: the aperture at `D0000000` has no write-combining MTRR. The default
  type is UC and the two variable ranges cover RAM only. This is consistent
  with the 21 MB/s CPU readback measured on 2026-08-27 and is a candidate for
  the plan's later performance work, not for Phase 2.
- Open: `HWS_PGA = 1FFFF000` is recorded, not interpreted.

## The false start, so nobody repeats it

The first install attempt went onto the "(Unknown Device)" node, which is the
IGD's PCI function 1 (`8086:27A6`, class 0380). Windows warned the driver was
not written for the hardware; the install was forced. Result, read from the
stick's `SYSTEM.DAT`: `Display\0004` (our INF) bound to `FUNC_01`, function 0
still on `Display\0003` "Standard PCI Graphics Adapter (VGA)" with no
`minivdd`. The driver loaded but `V9XBOOT.INI` stopped at `Stage=libmain`,
`VbeDetail=minivdd-no-api`, `Aperture=... b=00000000`; Windows fell back to the
4-bpp `vga.drv` row, and a resolution change garbled the panel into eight
tiled copies. Removing that binding and installing on the VGA-class node
produced the capture above. The runbook now carries this (commit `1d4ed65`).

## Gates

`check-tree`, `build-host` and `run-checks` green on the tree that built the
package. The validator's self-test and the physical capture both pass.
