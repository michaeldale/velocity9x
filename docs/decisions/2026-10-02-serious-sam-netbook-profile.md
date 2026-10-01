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

## Gen3 textures to 1024, measured (boot 88)

`V9XTSHP` gains a Gen3-only section: single levels at 256x256, 512x512,
512x256, 256x512, 1024x1024 and 1024x256, minified onto the 64x64 target
and compared exactly with a pattern that carries x and y mod 32, and full
512 and 1024 chains drawn MIPNEAREST at 64 and 32. With the HAL's
`texture_size_max` at 1024 (`gen3-large-V9XTSHP.INI`): every image exact,
the chains selecting levels 3 and 4 (512) and 4 and 5 (1024), no refusal,
timeout or reset; the existing shapes and 128x32 chains exact too. The
Direct3D path draws nothing for a refused batch, so an exact image is the
engine's. 2048 was not sampled; the ceiling stays at 1024.

Serious Sam, same procedure, HAL alone changed:

| | `ss3` (256) | `ss4` (1024) |
|---|---|---|
| frames, average | 510 in 282 s, 1.81 fps | 875 in 321 s, **2.73 fps** |
| CPU-textured batches per 10 s | 218 | **14** |
| their render-interface draws, of wall | 73.7% | 4.9% |
| hardware copies' create, evict and upload, of wall | 1.7% | **66.2%** |
| creates / failed / evictions per 10 s | 416 / 198 / 196 | 4,365 / 2,182 / 2,175 |
| upload per 10 s | 1.3 MB | 118 MB |

The CPU path is gone and video memory is now the limit: the copies are
made, evicted and made again.

**Most-recently-used eviction was tried and is reverted** (`ss5`, the
ICD alone changed from `ss4`): 1.91 fps, upload 141 MB a ten seconds,
hardware copies 76.4% of wall. A cache simulation of a fixed cyclic order
had favoured it (0 hits for LRU, 180 for MRU, 8 textures in 5 slots);
Serious Sam's order is not that, and the simulation did not model
DirectDraw's heap, where freeing one copy need not make room for a
larger one. The next step is the real use sequence, not another guess.

Regression checks on the same boot after these runs: Half-Life Direct3D
`mwd5` 40.89 fps (42.24 on boot 86), no new refusal in its window; Quake
2 demo2 20.6-20.8 fps (21.3-21.9 with the previous ICD build, which
lacked the `hwno` counters). Neither difference is separated from the
boot's history or the ICD's extra counters.

## Eviction: no rule fixes it, the trace says (`ss6`)

A build of the ICD (`icd-use-trace.patch`, not committed) recorded from
the first failed create the next 32,768 hardware-copy uses - texture,
copy bytes, frame, and whether the copy had a surface - into
`ss6-V9XGLUSE.BIN`. `evictsim.py` replays the order through a byte cache
under several rules, the capacity calibrated so LRU, the rule the trace
ran under, reproduces the observed hit rate.

| | |
|---|---|
| uses / textures / frames | 32,768 / 521 / 20 (about 2,000 uses a frame) |
| all copies resident | 14.1 MiB |
| observed hit rate (LRU) | 0.474 |
| calibrated capacity | 3.25 MiB |

| hit rate | 3.25 MiB | 4.88 | 6.50 | 13.0 |
|---|---|---|---|---|
| LRU | 0.474 | 0.481 | 0.491 | 0.984 |
| MRU | 0.499 | 0.588 | 0.658 | 0.980 |
| LRU sparing this frame's copies | 0.474 | 0.481 | 0.491 | 0.984 |
| largest of the older half | 0.486 | 0.538 | 0.707 | 0.984 |
| LRU-2 | 0.450 | 0.496 | 0.579 | 0.983 |
| **Belady (furthest next use, the bound)** | **0.556** | 0.669 | 0.790 | 0.984 |

At the capacity the netbook has, the best possible rule hits 0.556
against LRU's 0.474: no eviction rule recovers much, and MRU's 0.499 in
the model was a loss on the machine (`ss5`), the heap's fragmentation
unmodelled. LRU stays. What moves the hit rate is memory: about 13 MiB
holds the working set. A smaller copy does too - every copy of 170 KB or
more held at a quarter (its top level dropped) gives LRU 0.721 at 3.25
MiB in the same replay - at the cost of those textures' sharpness.

## Not established

- What the scenes look like; nobody watched.
- Serious Sam's whole texture working set in bytes.
- Why `ss2` and `ss3` differ beyond the demo mix.
