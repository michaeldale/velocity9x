# MGA-2064W: GDI fill and copy on the drawing engine

Date: 2026-10-09

Phase 3 of [the Matrox plan](../plans/matrox-millennium-family.md), first
half: GDI solid fills and screen-to-screen copies, overlapping ones
included, run on the MGA engine. Text and monochrome expansion still
decline to the DIB Engine.

## What changed

- `gdi_accel.c` has an MGA arm, compiled only into the matrox family's DRV
  (`V9X_MGA_FAMILY`). It builds every operation with the same host-tested
  builder the HAL uses (`src/chipsets/matrox/mga_engine.c`), now linked into
  the 16-bit DRV as well. No register value is written that the builder did
  not produce.
- The engine is reached through the 16 KiB control aperture that the chip
  hook already has the mini-VDD map for DirectDraw. `V9xEngineSelector`
  now takes the descriptor's control base and limit instead of assuming the
  ViRGE's BAR + 16 MiB. The ViRGE passes the base its descriptor already
  reported, so for the ViRGE nothing changes. A window whose STATUS reads all
  ones is refused here, as the mini-VDD refuses it.
- Before every operation: wait for idle, which also means the 32-entry FIFO
  is empty, then write the per-mode setup (MACCESS, PLNWT, the open clip
  window), then the operation. The setup is written every time, not once at
  Enable, for the Trio64's reason: it is write-only, and a DOS box's BIOS
  mode set or the HAL can change it underneath.
- A successful idle wait writes the CRTC index back with its own value.
  This invalidates the chip's CPU read cache, which the engine's writes do
  not flush (MGA-1064SG 5.1.6), before the DIB Engine reads the
  framebuffer. That is what the BeginAccess drain relies on.
- 32 bpp is accepted on the MGA only. S3 is still 8 and 16.
- The builder decides geometry: a pitch outside the linearizer's list or
  an origin off its grid declines at gate 8 (`DeclineEngine`).

## Measured

`V9XGDI /accel` draws 500 seeded operations (206 fills, 90 copies, 41
overlap copies, the rest text and noise) on screen and into a reference
DC, compares them 20 times, and fails if an enabled primitive's counter
stays at zero. It ends by injecting a timeout and checking that the
session poisons, so each run gets its own boot. INIs are in
[`docs/probe/a8u4i5-mga2064w-gdi-2026-10-09/`](../probe/a8u4i5-mga2064w-gdi-2026-10-09/).

| Machine | Mode | Pitch (bytes) | Fills on engine | Copies on engine | Compared | Result |
|---|---|---|---|---|---|---|
| 86Box Win98SE-Millennium | 800x600x8 | 960 | 160 | 131 | PASS | PASS |
| 86Box Win98SE-Millennium | 800x600x16 | 1920 | 160 | 131 | PASS | PASS |
| 86Box Win98SE-Millennium | 800x600x32 | 3200 | 160 | 131 | PASS | PASS |
| A8U4I5, physical 2064W (boot 362) | 1024x768x16 | 2048 | 160 | 131 | PASS | PASS |
| A8U4I5 (boot 365) | 1024x768x8 | 1024 | 160 | 131 | PASS | PASS |
| A8U4I5 (boot 366) | 1024x768x32 | 4096 | 160 | 131 | PASS | PASS |

Every run had `IdleTimeouts=0`, `DeclineEngine=0` and `DeclineDepth=0`. In
the 8 and 16 bpp runs the injected timeout poisoned the session
(`PoisonedAfterInject=1`).

**Correction, same day:** the two 32 bpp runs did not run the injection,
clip or zero-counter checks. The harness treated 32 bpp as a depth no
primitive serves (`InjectionSkipped=depth-not-accelerated`), which is
true of the S3 but not the MGA, and an earlier version of this line said
every run had poisoned. The 32 bpp fills and copies did draw on the engine
and compare clean. The harness was fixed and 32 bpp re-run with every
check in the [text record](2026-10-09-mga2064w-text-on-the-engine.md). The
guest's 8 bpp mode carries the BIOS's padded 960-byte stride, which the
linearizer takes as a 960-pixel pitch.

Two further 1024x768x16 runs on A8U4I5 (boots 363 and 364) also passed.
They used an older `V9XGDI.EXE` (`text-virge-1`) by mistake, so they are
not counted above.

DirectDraw with GDI on the engine, A8U4I5 boot 367 at 1024x768x16:
`V9XDDP` `Result=COMPLETE`, with the fill and all four overlap cells
pixel-correct. The HAL's engine counters were not read this time.

Regression on the shared code, 86Box ViRGE/DX guest (boot 651,
1024x768x16): `/accel` PASS with EngineType 1, 160 fills and 131 copies.
The selector change leaves the ViRGE where it was.

## Not established

- **Nobody has looked at the monitor.** Every check above reads pixels back
  through GDI, the framebuffer as the CPU sees it, not what the RAMDAC
  scans out.
- **Speed.** Nothing was timed. The size threshold is the S3's 1024 pixels,
  not measured on this card.
- **The Millennium II (2164W).** It takes the same code with its BARs
  swapped, and has not run.
- At 32 bpp the harness ran none of its clip operations
  (`ClipOperations=0`, on both machines), for the reason in the correction
  above. The text record's 32 bpp runs do include them.
- `V9XENGINESELECTOR` changed signature, from no arguments to a base and a
  limit. No symbol became external.
