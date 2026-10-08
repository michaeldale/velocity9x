# The render interface draws into a Glide flip chain: clears, flips, depth, clip and lines read back the same on Gen3 and the software engine

Date: 2026-10-08
Machines: MICHAEL-NETBOOK (GMA 950, Gen3), boot 127; `Win98SE-Fast-D3D`
(86Box, `vbe` package, software engine), boot 602.
Driver: as installed on each (0.13.0 set on the netbook). GLIDE2X.DLL and
V9XGLIDP.EXE built from the Phase 2 tree.
Evidence: [`../probe/glide-phase2-probe-2026-10-08/`](../probe/glide-phase2-probe-2026-10-08/)
Plan: [glide-2x-wrapper.md](../plans/glide-2x-wrapper.md), Phase 2.

## Why

The ICD only ever drew into offscreen DirectDraw surfaces and presented
with a Blt. A Glide game owns the screen: `grSstWinOpen` takes exclusive
fullscreen at its own resolution and flips. The plan made "does the
render interface accept a flip chain's buffers as targets" the question
to answer before anything depends on it, with a fallback (an offscreen
back buffer and a Blt) if not.

## What was built

`glide_surface.c` opens DirectDraw exclusive and fullscreen, sets
640x480x16, and makes a primary flip chain with a back buffer
(`DDSCAPS_3DDEVICE`) plus a 16-bit Z surface. It loads V9XHAL.DLL and
describes after the mode set. NFS II SE passes no window, so the DLL makes
one. `glide_dll.c` now:
- keeps the Glide state from every state call;
- clears through the interface's clear, inside the clip window;
- flips on `grBufferSwap`;
- locks the real buffer for `grLfbLock` reads and 565 writes, and
  `grLfbReadRegion`;
- draws untextured triangles and lines through Phase 1's state and vertex
  mapping. Textured draws are counted and skipped until Phase 3.

V9XGLIDP loads the DLL as a game does and reads every result back through
`grLfbLock`, not a screenshot.

## Results

| Check | Gen3 (engine 3) | Software (engine 1) |
|---|---|---|
| Open, 640x480, two buffers, depth | PASS | PASS |
| Clear back red; after a flip the front is red | F800, F800 | F800, F800 |
| Clear back green; front still red | 07E0, F800 | 07E0, F800 |
| White triangle inside / outside | FFFF / 07E0 | FFFF / 07E0 |
| W-buffer: first, farther rejected, nearer accepted | 001F, 001F, F800 | 001F, 001F, F800 |
| Clip window 50x50: inside / outside | FFFF / 0000 | FFFF / 0000 |
| Line at y = 400 | FFFF | FFFF |
| Triangle with the 3 << 18 snap bias | FFFF | FFFF |
| Desktop after `grSstWinClose` | 1024x576, intact | 1024x768, intact |

Every value is the same on both engines. Both describes report target
formats 0x2 (RGB565 only). Gen3 offers hardware textures up to 1024;
the software engine none (`hw max=0`).

**The flip chain works as a render target on both engines.** The
fallback is not needed.

## Disputed by the evidence

- **Lines centred on their coordinate.** The first netbook run drew the
  line as a strip from y - 0.5 to y + 0.5. It lit nothing at y = 400
  (read 0000) while every triangle passed. The strip's edges fall on
  half-pixels, and the engine's sample for row 400 sat on its excluded
  edge. The line now runs from y to y + 1, which lights row y whichever
  pixel corner or centre an engine samples. Which row a Voodoo lights for
  an integer y is not measured.
- **A failed run.** The first read of the second run's INI said
  `Result=INCOMPLETE` although the log showed close, shutdown and detach.
  Windows 98 had not yet flushed `WritePrivateProfileString`'s cache when
  the agent fetched the file; a second fetch read `PASS failures=0`. So
  read probe INIs after a pause, or twice.

## Not covered

Textures, fog, chroma key, blending and the alpha test on the device are
Phase 3. The cull winding is still unmeasured; the probe draws with
culling off. The interval-0 swap is taken as interval 1 (no DirectX 5
flag for "no wait").

## Gates

`build-host.ps1` (with the line test changed first and seen to fail, 10
failures), `check-tree.ps1`, `build-glide.ps1`, `build-glide-probe.ps1`,
and `run-checks.ps1`.
