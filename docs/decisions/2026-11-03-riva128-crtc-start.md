# RIVA 128ZX scanout start: CR0D, CR0C, CR19 behind SR06, and flips through it

Date: 2026-11-03

Machine: M-7V636C (remote help), RIVA 128ZX 8 MiB, Windows 98 first
edition. Plan: `docs/plans/nvidia-riva128.md`, Phase 4 (8 bpp) and Phase 5.
Files: `docs/probe/m7v636c-riva128-crtc-2026-11-03/`.

## 8 bpp, owed from Phase 4

Boot 15, build `1d114d12`'s tree (the 16 bpp hang fix). Desktop
800x600x8: V9XDDF PASS (fill `5A`, copy `A5`, no mismatches), V9XDDP
`COMPLETE`. The trace snapshot taken in that mode read `EngineResets=2`,
`EngineIdleTimeouts=0`: the engine validated at 8 bpp. An 8-bit pixel looks
the same from the CPU and from the engine, so the readback alone could not
say which drew it.

## What the CRTC holds (V9XN3MM /crtc, boot 15)

A new `/crtc` switch in V9XN3MM reads the CRTC through the VGA ports,
opens SR06, reads again, counts retraces on input status 1 for 500 ms,
moves the start address down one row for two frames and puts it back. At
800x600 and 8, 16 and 32 bpp:

| | 8 bpp | 16 bpp | 32 bpp |
|---|---|---|---|
| SR06 before / unlocked / after | 00 / 01 / 00 | same | same |
| CR13 | 64 | C8 | 90 |
| CR19 locked / unlocked | 00 / 00 | 00 / 00 | 00 / **20** |
| CR1A locked / unlocked | 00 / 3C | 00 / 3C | 00 / 3C |
| CR28 locked / unlocked | 00 / 01 | 00 / 02 | 00 / 03 |
| CR0C, CR0D, CR25, CR2D | 00 | 00 | 00 |
| Retrace starts in 500 ms | 31 | 31 | 30 |
| Polls with 3DAh bit 3 set | 0.68 % | 0.69 % | 0.66 % |
| Start one row down, read back | C8h ok | 190h ok | 320h ok |

What this settles:

- **SR06 is the lock, and reads back its state:** 0 locked, 1 unlocked
  (riva_hw.c `nv3LockUnlock` writes 57h and 99h; neither source says what
  a read returns). Writing 99h locks it again.
- **The extended registers read 0 while locked.** CR19, CR1A and CR28 only
  show their values unlocked, so CR19 has to be read unlocked before its
  start bits are merged.
- **CR19 bits 7-5 are the row offset's bits 10-8.** At 32 bpp CR13 90h with
  CR19 20h is 190h, x 8 bytes = 3200, the pitch. 86Box `vid_nv3.h` says
  this; envytools `pcrtc.rst` says "Row Offset bits 3-5", which is wrong.
  The start write keeps those bits.
- **CR28 bits 1-0 are the depth** (1, 2, 3), as envytools says.
- **The start address takes writes:** CR0D, CR0C and CR19 bits 4-0, in
  dwords (riva_hw.c `SetStartAddress3`), read back as written at every
  depth. rivafb's CR2D bits 5-6 hold start bits 21-22, beyond 8 MiB;
  envytools gives CR2D only horizontal bits. Not written.
- **3DAh bit 3 is the vertical retrace**, about four lines of a 628-line
  frame at 61 Hz, not the whole blank. Every VGA-path chip in this tree
  reads the same bit.

Not settled: whether NV3 latches a new start at the retrace, as VGA does,
or applies it at once. There is no documented scanline or scanout-address
counter on NV3 (envytools' PCRTC page is "write me"; 86Box has none), so
only someone at the monitor can tell. The driver uses the core's default
timing (write any time, done at the next retrace start), which is right
for a latch at the retrace and only tears if it applies at once. The
in-blank timing the MGA uses would release a buffer still on screen if the
start latched.

## Flips (boot 16, the Phase 5 build)

`eng_nv3.c` writes the start through `v9x_nv3_build_crtc_start`
(`nv3_2d.c`, host-tested with the values above), opening SR06 around the
write and closing it again only if it was closed. `riva128_hw16.c` adds
`V9X_DD_ENGINE_CAP_FLIP`; `V9XHW.INI` reads `EngineCaps=00000007`. DRV, VXD
and HAL renamed in together; installed HAL hash `5BC70328174A3E26`, equal to
the package.

- V9XDDP from 16, 32 and 8 bpp desktops: `COMPLETE` each time,
  `Flip20Ms` 318, 321 and 335 (boot 15, with flips declined: 1), so each
  flip waited for a retrace. `FlipPixelOk=0`: the known instrument limit
  on every hardware-flip chip since 2026-08-11 (GDI reads its own page).
- Trace snapshot after the three runs: `FlipHandled=69`,
  `FlipDeclined=0`, `FlipStillDrawing=222171` (the polls while a flip
  waits), `EngineIdleTimeouts=0`.
- V9XDDP `/hold` (5 s on each verification colour) with `/crtc` read
  during it: start `25800h` with a 1280-byte row (640x480x16, the second
  buffer at 614400 bytes). After it exited: start 0 with the desktop's
  1600-byte row, SR06 locked. The flips reach the scanout and are undone.

Not seen: the monitor. Whether flips show without tearing, and that the
desktop looks right after an exclusive-mode program, need the owner's
eyes.
