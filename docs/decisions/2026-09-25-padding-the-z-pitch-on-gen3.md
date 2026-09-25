# Padding the Gen3 Z pitch off a power of two: 244 to 280 at 1024x576, a third of the per-pixel cost

2026-09-25, MICHAEL-NETBOOK, boot 11, shared ABI 2026092504. 3DMark 99
Max, 1024x576x16, triple buffering. Follows
`2026-09-25-the-gen3-head-wait-is-pixels.md`, which found each pixel about
ten times dearer at 1024x576 than at 640x480 and named the shared
2048-byte colour and depth pitch as the one layout difference between the
modes.

## What changed

The HAL places a lone video-memory Z buffer itself when its natural pitch
- `(width * 2 + 7) & ~7`, the DDK's - is a power of two, and gives it 64
bytes more (Mesa's linear granule): 2112 at 1024x576. Any other pitch is
left to DirectDraw exactly as before, so 640x480's 1280 is untouched. The
block comes from the mip-tree allocator, now a shared helper
(`v9x_d3d_i9xx_place_block`), and is freed the same way. The core had
refused any Z pitch but the packed one, which is the ViRGE's rule because
it programs its own stride; a new engine limit, `depth_pitch_own`, lets an
engine that programs the surface's pitch (Gen3's BUF_INFO) accept a wider
one. The ViRGE and the software engine keep the old rule.

## Measured

`ZPlaced=2`, `ZPlacedPitch=2112`, the depth surface accepted and 242,299
depth-tested draws; `MipTreeFrees` equals the 6,082 trees plus the two Z
buffers, so nothing leaked. Report
`2026-09-25-netbook-padded-z-3dmark1024-report.txt`.

| | Z pitch 2048 (boot 10) | Z pitch 2112 (boot 11) | 640x480 (boot 10) |
|---|---|---|---|
| 3DMarks | 244 | **280** | 692 |
| head wait, % of the run | 36.2 | **21.4** | 2.7 |
| head wait per batch | 880 us | **580 us** | 91.5 us |
| 10K-50K px batch | 1.9-2.7 ms | 1.3-2.1 ms | 0.17-0.32 ms |
| 50K-200K px batch | 8.2-12.6 ms | 4.8-10.9 ms | 0.6-1.0 ms |

## What it says

- **Aliasing is real and is part of the cost.** Taking one of the two
  surfaces off a power-of-two pitch cut the per-pixel cost by roughly a
  third and raised the score 15%, with nothing else changed.
- **It is not all of it.** Each pixel still costs five to eight times what
  it does at 640x480. The colour buffers still share a 2048-byte pitch -
  and all three of them with each other - which is the next candidate; the
  scanout reads them, so their pitch is the mode's stride and changing it
  is a mode-programming change in the 16-bit driver, not a HAL one.
- Why a power of two costs so much here - render-cache set conflicts or
  DRAM bank conflicts - is not established; either fits.

## Next

The same experiment on the colour side: program the 1024x576 mode with a
2112-byte stride, so the primary, the back buffers and the Z buffer are all
off the power of two, and compare the table again. If the per-pixel cost
reaches the 640x480 figure, pitch aliasing was the whole of it.
