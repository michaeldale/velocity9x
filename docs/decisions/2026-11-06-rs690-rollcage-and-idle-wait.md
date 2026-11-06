# RS690: Rollcage through the HAL, and what the per-draw idle wait costs

Date: 2026-11-06. Machine: M-GB8KRE (RS690 1002:791F, Win98 SE, DirectX
6.1), boot 78. Build: branch `ati-rs690`, c5865960-dirty (the HAL with
the timing counters below). Captures:
`docs/probe/rs690-rollcage-2026-11-06/`.

## A game

The Rollcage demo's Direct3D build (Psygnosis, 1999, DirectX 6) is the
first game on this chip. The machine had none, so only the build's files
went across:

- `DIRECT3D\ROLLCAGE.EXE`, `QMIXER.DLL`;
- `IDXDATA\ROLLCAGE.IDX`, `.IMG`.

That is 21.6 MB in 677 s by `push-tree` through the relay, about
32 KB/s, to `C:\RCDEMO\ROLLCAGE`, hashes checked. It runs as
`DIRECT3D\ROLLCAGE.EXE /DISPATCHER`, detached. Its dialog offers
"Primary Display Driver" (the Velocity9x HAL) at 640x480x16 with Z
buffering; Play is at (452, 108) on the 640x480 desktop.

The title screen and the attract-mode race drew correctly (`rc-play1.png`,
`rc-play2.png`):

- textured track, rock walls and grass;
- the blended logo;
- the cars' additive blue trails.

The game ran for several minutes and was closed with the agent's `kill`;
the desktop came back.

The snapshot during the run (`rc-snap1.INI`):

| Counter | Value |
|---|---|
| hardware draws | 1,567,154 |
| triangles | 12,079,817 (7.7 a draw) |
| textured draws | 1,537,309 |
| refused | 0 |
| unrenderable | 0 |
| textures placed | 208 |

Nothing Rollcage asked for was refused. Its textures are not mipmapped
(D3dMipChainLevels 0), and it uses no fog.

## What the idle wait costs

Since the T15 fix (42cfea26), `v9x_rs690_3d_begin` waits on the CPU for
the engine to go idle before every draw. Without the wait, a draw that
starts while a 2D fill runs sends lines to wrong addresses
(`docs/issues/2026-11-05-rs690-t15-heap-words-zeroed-once.md`).

The HAL now times it with the time-stamp counter's low half. The sums are
in units of 256 cycles, carried in diagnostic fields this engine does not
otherwise use:

| Counter | Value |
|---|---|
| `M64Degenerate`: cycles in the wait | 113,343,877 (2.9 × 10¹⁰ cycles) |
| `M64PolicyLast`: cycles in whole draws | 297,073,665 (7.6 × 10¹⁰ cycles) |
| `M64DepthDraws`: draws whose wait took over 4096 cycles | 416,884 (27%) |

**About 38% of the CPU time spent in draws is the idle wait.** Rollcage
sends about eight triangles a draw, so each wait mostly sits out the
previous draw's pixels. Not all of it would come back without the wait,
because the CPU would then block on FIFO room instead. The probe's
bisection showed that only a 2D fill before a 3D draw causes the stray
writes; back-to-back 3D draws were clean (`/hnofill`, 0 words). So a wait
taken only after 2D work would remove most of this.

The catch is that 2D work also comes from the 16-bit display driver
(GDI), which the HAL's own flags do not see. A targeted wait has to tell
from the engine whether the 2D side is still busy, for example from
`RBBM_STATUS`'s per-block busy bits, and that needs measuring before it
is trusted.

## Not checked

- Frame rate. Rollcage reports none, and this was not a benchmark.
- 32 bpp. The dialog offered 16 bpp at 640x480.
- A full race played by input.
- What a targeted wait would save.
