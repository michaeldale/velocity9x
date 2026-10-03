# A8U4I5 hard-locks when Half-Life's alpha-tested additive sprites are drawn

Date: 2026-10-03. Machine: A8U4I5 (Rage IIC AGP `1002:4757`, P3 1002 MHz,
Win98SE). Status: open; the batches are refused, so nothing ships that
locks. Evidence: `docs/probe/a8u4i5-rage-iic-registers-2026-10-02/`
`hl-d3d-mwd5-alphatest/`, `-census/`, `-bisect-*/`, `-census-geom/`,
`-census-texend/`, `phase8-hud/`.

## Symptom

Half-Life mwd5, Direct3D, 640x480. With RGB565 alpha-tested batches drawn
without the test (the test passes for every alpha they can have; see
`docs/decisions/2026-10-03-rage-iic-alpha-test.md`), the machine hard
locked during the first timedemo. Ping still answered, while the agent's
port refused connections. Michael confirmed the screen frozen and reset it each time.
The first lock came after at least 45 s (`hl-running.png` was drawing at
45 s), so it was not on the first frame.

## Bisection (one run of three demos each; the held batches counted and
refused)

| Build: drawn of the dropped-test batches | Result |
|---|---|
| all | hard lock (twice) |
| none (census) | 3 runs, 5.218 / 5.570 / 5.560 fps |
| untextured only | 3 runs |
| + textured unblended | 3 runs |
| + textured blended, not mip-mapped | hard lock |
| the same with the texture cache off for them | hard lock |
| + textured blended with Z on, not mip-mapped | 3 runs |

Locking class: about 3,500 batches per three runs. All are textured RGB565,
MODULATE, bilinear, blended ONE/ONE (every blend factor seen), with Z off
and no mip-mapping, and nearly all are flat. Their geometry
(`census-geom`): screen-space (rhw 0.99999994 throughout), tu and tv
exactly 0..1, positions x 16-624 and y 16-478, at most 128x128 pixels,
textures such as 256x128. That is the HUD and screen sprites. Their
textures end below `0x3911D8`, while textures already drawn reach
`0x3FF640`, so the end of VRAM is not the cause.

## What the chip did without locking

`ATIRX /hud`, the same `SCALE_3D_CNTL` (`0B490880`) over a fixed
rectangle drew every scene with guards intact and no timeout:
- with the cache off;
- with SRCALPHA;
- ONE/ONE alone;
- ONE/ONE followed at once by the HAL's 2D mode and a 2D fill;
- 64 quads back to back, with and without the fill;
- with Z test and with Z write.

## Hypotheses the evidence killed

- **The 4444 rewrite.** A build with it and none of these batches ran
  clean, and the rewrite never touches 565 textures.
- **The texture cache.** It locked with the cache off for these batches.
- **A 2D operation after a live blend** (RRG p.4-95: blending takes the
  destination read FIFO from the 2D engine). The HAL's fills and copies
  already switch to 2D mode first, and `/hud` H4 and H6 did not lock.
- **Texture placement at the top of VRAM.**
- **The state itself.** `/hud` drew it.

## The Rage XL draws them (2026-10-04)

With the same rule on the Mach64 path, A8U4I5 with the Rage XL PCI drew
the HUD sprites through three mwd5 runs with no lock, no refusal and no
timeout (`docs/decisions/2026-10-04-mach64-alpha-test-that-cannot-discard.md`).
That leaves the Rage IIC's own path - CPU triangle setup into its
trapezoids, its engine - as the place to look, not the draws themselves.

## Open

The lock needs something these scenes lack: the HAL's own pieces for real
triangles, one particular sprite or moment in the demo (the HUD draws every
frame, yet the lock came tens of seconds in), or an interaction with page
flipping. The next step is to find the moment: periodic snapshots or
screenshots during a locking run, or a record of the last batches that
survives the reset.
