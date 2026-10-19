# The Mystique's drawing engine is the 2064W's: MGA2D passes, the driver claims it

Date: 2026-10-19

Status: measured on A8U4I5 (Michael's Mystique, 051A revision 02).
Phase 2 of [matrox-mystique.md](../plans/matrox-mystique.md). Builds
`99bed900-dirty`: the probe change ran on boot 506 under the CPU-only
driver; the engine claim ran on boot 507.

## The write probe

`MGA2D.EXE`/`MGA2D.VXD` accepted only 0519 and 051B. They now accept
051A, and only in the 1064SG's order: the VxD checks that BAR0 is not
prefetchable and BAR1 is, and leaves the card unmapped otherwise. So an
1164SG, or 86Box marking neither BAR, is refused. Past that check the
Mystique takes the 2064W's path. Its bus FIFO is the 2064W's, 32
entries with fifocount <5:0> (MGA-1064SG specification 4-56), and STATUS
dwgengsts is bit 16 (4-74). The probe's setup writes MACCESS = pwidth
alone (`mga_engine.c`), so jedecrst <14> and memreset <15> stay clear,
which the specification requires (4-64, 5-21).

Run under the CPU-only Phase 1 driver at 800x600, with the region at
3-4 MiB (VRAM 4 MiB, from VBE and the driver's memory walk):

| Depth | Fill | Copy fwd | Copy down | Copy right | Copy up-left | Wide fill | Wide copy | Result |
|---|---|---|---|---|---|---|---|---|
| 8 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| 16 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| 32 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |

The edges match the 2064W's: a TRAP fill's right edge is exclusive, and a
BITBLT's last column is written (`...LastColumnWritten=n/n`,
`...ColumnRightOfRectWritten=0/n`). FIFOSTATUS read 220h before and after
(32 free, bempty), as the specification's reset value. No wait timed out.
Reports in
[`docs/probe/a8u4i5-mystique-engine-2026-10-19/`](../probe/a8u4i5-mystique-engine-2026-10-19/).

## The driver claim

`millennium_hw16.c`'s Mystique entry now carries the family's engine hook,
claiming SOLID_FILL, SCREEN_COPY and FLIP, no D3D. The hook passes no
fallback BAR for 051A: its two chips have opposite orders, so where
neither BAR is prefetchable (86Box) it stays on the CPU. On silicon the
prefetch bit picks BAR0. Deployed by WININIT rename, boot 507:

- `V9XHW.INI`: `EngineCaps=00000007`,
  `GdiAcceleration=gdi-fill-copy-overlap-text`, `Stage=enable-ok`.
- `V9XGDI /accel`: `Result=PASS`, `EngineType=7`, 198 engine fills and
  132 copies, `Compared=PASS` over 20 comparisons, no idle or FIFO
  timeouts; the injected fault was caught and poisoned the engine as
  designed.
- `V9XDDP`: `Result=COMPLETE`, `BltFillPixelOk=1`, `FlipHr` OK, 20 flips
  in 331 ms (at most 17 ms each), the same pacing as the 2064W
  ([record](2026-10-09-mga2064w-page-flips.md)).
- The desktop screenshot after boot is clean.

**What the monitor shows.** `FlipPixelOk=0`, as on every chip, because
the probe reads back through GDI, which sees only GDI's page. OBS is
connected to this session, but its inline screenshots come back
truncated and its saved files land on the OBS PC. So Michael watched
`V9XDDP /hold` on A8U4I5's monitor (same boot, `V9XDD-hold.INI`: COMPLETE,
20 flips in 332 ms): "The whole screen went solid red for about 5
seconds, then solid blue, then back." The start-address units are right
at 800x600x16: a wrong unit would have shown part of a page or another
surface.

The flip units come from the 1064SG specification itself (5-66, in
`v9x_mga_display_start`), and the chip's CRTCs are its own, not a
TVP3026's.

## What this does not establish

- Tearing at the top of the frame on a flip, as on the 2064W: not
  looked for.
- The 1164SG and the 86Box profiles.
- Direct3D, which is Phase 3 and needs separate approval. ARGB4444 is to
  be checked early there.

Gates: `run-checks.ps1` passes.
