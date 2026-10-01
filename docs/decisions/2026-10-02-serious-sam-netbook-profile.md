# Serious Sam on the netbook: textures over 256 are drawn by the CPU, and that is three quarters of the frame

Date: 2026-10-02
Machine: MICHAEL-NETBOOK, GMA 950, Atom N280, boot 87, async submission
on. Serious Sam: The First Encounter v1.05 (1.00c with the 1.04 patch),
OpenGL on the Velocity9x ICD, 640x480 fullscreen, 16-bit colour and Z, the
installed settings (`Scripts_PersistentSymbols.ini`: one texture unit,
`tex_iNormalSize=9`, i.e. up to 512).
Evidence: [`../probe/serious-sam-netbook-2026-10-02/`](../probe/serious-sam-netbook-2026-10-02/)

## Benchmarking it

From the engine's open source (Serious Engine 1.10, `GameMP/Game.cpp`,
`SeriousSam.cpp`, `CmdLine.cpp`): `dem_bProfile=1` before a demo starts
asks for a profile, printed and written to `Temp\DemoProfile.lst` when the
demo finishes; `PlayDemo("name")` plays `Demos\name.dem`; `+script` runs a
script at startup; `Quit()` exits.

What 1.05 does: `dem_bProfile` takes a value, `PlayDemo` works from the
console, `dem_strPostExec` does not exist, `PlayDemo` from a `+script` runs
before the menu and is cancelled by it, and no profile report or
`DemoProfile.lst` appeared after a demo played through. So the measure is
the ICD's own ten-second lines: a Serious Sam demo plays in real time, so
frames over its fixed length are the frame rate (`sstimeline.py`). The
procedure (`ssam-run.ps1`) launches the game, waits for the menu, types
`PlayDemo("auto-demo0001")` into the console, waits, and quits with
`Quit()`. A screen was not watched.

## Two runs

| | `ss2` | `ss3` (with the reason counters) |
|---|---|---|
| demo intervals (draws above 1,000 in ten seconds) | 48, 481 s | 28, 282 s |
| frames, average | 1,665, **3.46 fps** | 510, **1.81 fps** |
| slowest / fastest ten seconds | 1.0 / 27.6 fps | 1.3 / 6.1 fps |
| render-interface draws per 10 s / CPU-drawn batches | 7,368 / 427 | 4,347 / 218 |
| texture creates / failed / evictions per 10 s | 2,985 / 1,494 / 1,487 | 416 / 198 / 196 |
| upload per 10 s | 23.4 MB | 1.3 MB |

`ss3`'s game log shows `auto-demo0001` and then `auto-demo0002` started,
so its window mixes two demos; the runs are not a pair and their frame
rates are not compared.

## Why batches go to the CPU (`ss3`, new `hwno` counters)

| `v9x_gl_hw_texture` answered no because | per 10 s |
|---|---|
| **the texture is over 256 texels a side** (`too-big`) | **649** |
| no CPU image or no hardware textures | 85 |
| backing off after a failed create | 2 |
| no surface even after evicting | 2 |

and the time: render-interface draws of CPU-textured batches were **73.7%
of wall time**; the hardware copies' create, evict and upload 1.7%. In the
HAL over the window, 125,295 draws averaged 1,642 us; the 107,493 that
reached the Gen3 engine averaged 37.7 us.

`ss2`'s failed creates are `DDERR_OUTOFVIDEOMEMORY`, almost all 256x256
with nine levels, so video memory is the second limit. But the first is
that every texture over 256 is drawn by the CPU at milliseconds a batch,
whatever memory is free.

## What the 256 is

`v9x_d3d_i9xx_limits.texture_size_max` (`d3d_i9xx.c`): "GENERALISED from
one measured size" (a 32x32 map), with MAP_STATE able to hold any size to
2048; the ceiling was set at 256 because a 2048-square map is 8 MiB and so
is the stolen memory. It is a memory argument, not a sampler measurement.
A 512 map with its chain is about 680 KB.

## Next, by size

1. Measure Gen3 sampling 512 and 1024 maps (the `V9XTSHP` probe's shape)
   and raise the ceiling to what reads back exactly.
2. Eviction that does not miss on every use of a cyclic working set.
3. The system-memory pool ([plan](../plans/intel-gen3-system-memory-textures.md)).

## Not established

- What the scenes look like; nobody watched.
- Serious Sam's whole texture working set in bytes.
- Why `ss2` and `ss3` differ beyond the demo mix.
