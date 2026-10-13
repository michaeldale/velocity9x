# A reporter's embedded GMA 950 board hard-locks on 3D under 0.17.0 (GitHub issue 6)

Date: 2026-10-14. Status: open, unreproduced. Review of the report only; no
code changed and nothing run on hardware.

Source: https://github.com/michaeldale/velocity9x/issues/6, opened
2026-10-13 by `interrupt7`. Full text: "I have an embedded system with
GMA 950 that hard locks when trying to render 3D with driver version
0.17.0." One attachment, `V9XSURV.TXT`, a DOS survey built from `649e73ab`
(the 0.17.0 changelog commit), run 2026-10-13 23:56, `Result PASS`. No
comments yet. The report does not say which program, which API, which
desktop mode, or when in the run the machine locks.

## What the survey shows

| Item | This board | Netbook (MICHAEL-NETBOOK) | Lenovo 3000 C100 (issue 3) |
|---|---|---|---|
| IGD | `8086:27AE` rev 03, SUBSYS `8086:27AE` | `8086:27AE` rev 03 | `8086:2592` (915GM) |
| Host bridge | `27AC` (945GME/GSE) | `27AC` | 915GM |
| CPU | `000106C2` (Atom, family 6 model 28) | Atom | Pentium M class |
| RAM | ~1 GiB (E820 top `3F6E0000`) | 2 GiB | 2 GiB |
| GGC (cfg 0x52) | `0030`, 8 MiB stolen | 8 MiB | `0030` |
| BSM (cfg 0x5C) | **`3F800000`** | `7F800000` | `7F800000` |
| BAR0 MMIO / BAR2 GMADR / BAR3 GTT | `FDF00000` / `D0000000` (256 MiB) / `FDF80000` | `FE980000` / `D0000000` / separate | `D0000000` / `A0000000` / `D0080000` |
| VBE memory | `0x7B0000` (123 x 64 KiB) | `0x7B0000` | `0x7B0000` |
| Video BIOS | Intel build 1585, PC 14.34, 01/08/2008, `$VBT CALISTOGA` | HP OEM | build 1256, PC 14.8 |
| Output | **no panel**: 4F11h `FlatPanelInfo=0000`, OEM panel modes 0x160-0x171 all refused, EDID from an external 1920x1080 monitor (manufacturer `05E3`) | LVDS 1024x576 on pipe B | LVDS |
| VBE modes offered | 640x480 to 1920x1440, 8/16/32 bpp | 1024x576 and 640x480 only | standard |

The rest of the bus is embedded-board hardware the driver does not touch:
Realtek 8168 and VIA VT6105 NICs, a VIA VT3415 PCIe-to-IDE bridge with
its own option ROM, a Ricoh R5C475 CardBus bridge, and an ITE IT8888
PCI-to-ISA bridge. The IGD is on IRQ 5. The "For Evaluation Use Only"
string in the VBIOS is in Intel's stock image and also appears in the
915GM's; it is not a sign of a pre-production part. The shadow-ROM
checksum mismatch is the usual post-POST state.

What matches: the IGD's PCI ID and revision, the host bridge, the CPU
family, the stolen size, and the VBE-reported size, so the descriptor's
check that the reserve lands at the generated `0x6B0000` passes and the
runtime ring is not withheld. The aperture is at `D0000000` as on the
netbook. By the 945GSE ID survey
(`docs/decisions/2026-09-17-intel-945gse-pci-id-survey.md`) this is
the identical die; the board is a 945GME or 945GSE design.

## What has never been exercised before this machine

Every Intel 3D run on record is on a laptop driving an LVDS panel. This
board has no panel and scans out to an external monitor, which on Gen3
mobile normally means the CRT/DAC on pipe A. Nothing in the survey reads
the pipe or plane registers (it does not map MMIO), so which pipe is live
is inferred, not measured.

Code that depends on the live pipe, each written to resolve it rather
than assume pipe B, and each only ever run against pipe B:

- `src/display32/engines/i9xx_scanout.c`: picks the one enabled pipe and
  the one plane routed to it, and declines the configuration if two pipes
  or two planes are on. If the BIOS left both pipes on (CRT plus a
  disabled-panel pipe) the flip is declined, which should be safe; a
  decline is not a lock.
- `src/chipsets/intel/i9xx_flip.c`: MI_DISPLAY_FLIP for plane 0 or 1.
  Plane A has never been flipped on hardware.
- The display watermark (`36d0d7fe`, `7a678b04`): FW_BLC is rewritten
  from the live pipe's pixel rate and the plane's depth. On the netbook
  plane A was idle and got the idle answer; here plane A would be the
  live one, at a CRT pixel rate (65 MHz at 1024x768, 108 MHz at
  1280x1024) the netbook never had. A wrong watermark should underrun
  the display, not stop the machine, but that is i915's behaviour, not
  something measured here.

Code that depends on the desktop mode, where the netbook only ever ran
1024x576 and 640x480:

- Pitch unaliasing (`002ad93b`) widens a 16-bpp power-of-two pitch by 64
  through 4F06h. At 1024x768x16 the pitch becomes 2112. That was measured
  only against the netbook's VBIOS at 1024x576. The GMA 900 reporter in
  `2026-10-10-carmageddon2-exits-after-intro-gma900.md` ran 1024x768x16
  on 0.15.0, which includes `002ad93b`, and drew without locking, so the
  pitch change alone at that mode is not sufficient to lock a Gen3.
- Video memory pressure. The heap ends at `0x6B0000` (6.69 MiB). At
  1024x768x16 with the 2112 pitch a primary, two back buffers and a depth
  buffer take 6.19 MiB, leaving about half a MiB for textures; at
  1280x1024x16 three buffers alone exceed the heap. The netbook's
  1024x576 chain uses 4.6 MiB. Allocation failure should refuse, not
  overlap the ring, and the same arithmetic held on the GMA 900 at
  1024x768; it is listed because no Intel run has filled the heap this
  close to the reserve.

The BSM differs (`3F800000` against `7F800000` on both other Intel
machines). The armed Phase 4 and Phase 5 diagnostics refuse any BSM but
`7F800000` (`intel_exec16.c:439`, `intel_3d16.c:668`), and they are not on
the runtime path. The runtime status page takes its physical address from
the GTT's own entry (`d3d_i9xx.c`, `v9x_d3d_i9xx_hws_open`), not from a
constant, and HWS_PGA is no longer written. So the BSM is not expected to
matter; it is the first machine where that has been put to the test.

## Hypotheses, in the order worth testing

1. Something on the live-pipe path (flip, watermark, scanline wait) on
   pipe A or with two pipes on. Cheapest to separate with `IntelFlip=0`.
2. Asynchronous submission on a machine whose timing differs from the
   netbook's. Separated with `IntelAsyncSubmit=0`.
3. The 3D engine itself at a mode or heap fill never run on a 945. If
   both switches above leave it locking, `IntelRuntime3D=0` should stop
   it (3D then runs on the software interface), which at least confirms
   the lock is in the Gen3 runtime path and not in DirectDraw or the
   installer.
4. The application. Unknown until the reporter names it.

None of these is supported by evidence yet. The survey proves the board
is a 945GME/GSE with the netbook's memory layout and an external display;
it says nothing about the failure.

## What to ask the reporter

- The program and API (Direct3D, OpenGL, Glide, or DirectDraw), and
  whether the lock is at the first frame, at a mode switch, or later.
- Desktop resolution and colour depth, and the connector (VGA or DVI)
  the monitor is on.
- Whether 2D DirectDraw (a full-screen DirectDraw game with no 3D) works.
- After a reboot from the lock, `V9XUPD.EXE /REPORT`, which collects
  `V9XBOOT.INI` and `V9XHW.INI` from that boot; those record the live
  pipe, the plane, the watermark written, and the ring's state as the
  driver saw them. A snapshot from the locked session itself may not have
  been written.
- The same run with the desktop at 640x480x16, the one mode both this
  board and the netbook offer.
- If it still locks, step down with the packaged `V9X3D.BAT`, run from
  the package folder (beside `V9XDISP.DRV`), one step per boot. It
  rewrites `C:\V9XDIAG\INTELARM.TXT`:
  - `V9X3D SYNC`: asynchronous submission off, flip and 3D on.
  - `V9X3D ON`: flip and async off, 3D on.
  - `V9X3D OFF`: all three off (3D then falls to the software interface).
  - `V9X3D FLIP` restores the default.

  Note that this order tests async before the flip, the reverse of the
  hypothesis order above; there is no verb for "flip off, async on".

2026-10-14: these questions and the `V9X3D` ladder (SYNC, ON, OFF) were
posted to the issue. Awaiting the reporter.

## Field report V9X-6XQHDS (2026-10-14 09:47 server time)

Sent through `V9XUPD.EXE /REPORT` with no description or contact and no
issue link. It is taken to be this reporter on this board: 0.17.0
`649e73ab`, `8086:27AE` SUBSYS `8086:27AE` rev 03, 1,038,748 KB RAM
(the survey's 1 GiB), and `V9XSURV.INI` from 2026-10-13 23:57 in its
diagnostic directory, a minute after the survey attached to the issue.
Windows 98 SE 4.10.2222 A, DirectX 4.08.01.0901 (6.1). Four files:
`V9XSNA2.INI`, `V9XBOOT.INI`, `V9XHW.INI`, `V9XGLIDE.LOG`.

### What it establishes

- **The desktop is 1024x768x16 at the widened 2112 pitch**
  (`V9XBOOT.INI`: `Surface=pitch=2112 ... w=1024 h=768`,
  `VddReserve=vdd=1622016`). This is the mode the review called untested
  on a 945, and the one where a flip chain plus depth nearly fills the
  heap.
- **The Gen3 engine came up with a ring** (`EngineStamp=type=00000003
  caps=00002614 ring=E4F34000`, `Direct3D=hardware-gen3`,
  `Direct3DMode=hardware`). `EngineStatusEnable=map-refused` is the same
  line the netbook writes at every boot.
- **The program is Carmageddon II**, `C:\GAMES\CARMAGEDDON2\CARMA2.EXE`,
  the same game as the GMA 900 report
  (`2026-10-10-carmageddon2-exits-after-intro-gma900.md`), there as
  `CARMA2_HW.EXE`. `V9XGLIDE.LOG` has eight attaches between 2026-10-13
  23:27 and 2026-10-14 00:42, every one detached at once with
  `process exiting=0`, zero swaps and zero draws. The game loads the
  Glide DLL and unloads it without drawing; whatever locked, Glide did
  not draw it. Whether the lock is in Direct3D cannot be read from this
  report.
- **MTRRs** (`V9XBOOT.INI`): MTRR0 0-1 GiB write-back, MTRR1 the 8 MiB
  stolen range at `3F800000` uncached, MTRR2 `3F700000` 1 MiB uncached.
  No range covers the aperture at `D0000000`.
- **`INTELARM.TXT` is 56 bytes, dated 2026-10-13 23:10**. A file written
  by `V9X3D` is longer than that, so the reporter has probably not run it
  (posted after these boots). Its contents were not sent.

### What it does not contain

The snapshot sent, `V9XSNA2.INI`, was dumped 94 s after a fresh boot
(2026-10-14 02:50) with `DriverInitDone=0` and every Direct3D, flip, ring
and watermark counter zero. It describes the boot after the lock, not the
locked session. The directory listing shows two earlier full snapshots
that were not uploaded: `V9XSNAP.INI` (2026-10-14 00:19, 22,515 bytes)
and `V9XSNA1.INI` (02:41, 22,564 bytes). If either was dumped in a
session where the game ran, it holds the counters this report lacks. A
hard-locked session cannot dump its own snapshot, so the lock's last
moments may not be in either.

Also on disk and not sent: `INTELARM.TXT`, `INTEL3D0.TXT`,
`INTELMM.TXT`, `INTELRNG.TXT`, `INTELEVT.TXT`, `V9XSYNC.INI`.

`Display\0000` is a stale "Standard PCI Graphics Adapter (VGA)" entry
on `vga.drv`. The two cards present are bound correctly: function 0 to
Velocity9x and function 1 parked with problem 22, as on every 945.

### Next asks

- `V9XSNAP.INI`, `V9XSNA1.INI` and `INTELARM.TXT` from `C:\V9XDIAG`,
  attached to the issue.
- Whether Carmageddon II is run in Direct3D or Glide mode, and where in
  the game it locks (the GMA 900 machine got about 7 s of Direct3D
  drawing before the game quit by itself).
- The same run with the desktop at 640x480x16, which takes the 2112 pitch
  and the heap pressure out of the picture in one step.

## Local reproduction

None possible yet. The netbook is the same silicon but its VBIOS refuses
every mode above 576 lines and it has no external-only configuration
tested. Whether the netbook's VGA port can carry the desktop with the
lid panel off (pipe A live, pipe B off) is the nearest local analogue and
is untested. 86Box has no 945 model.

## Gates

None run; this record is a review of the report and the source at
`c61e09d8`. Not added to `docs/issues/README.md` because another session
is working in the tree.
