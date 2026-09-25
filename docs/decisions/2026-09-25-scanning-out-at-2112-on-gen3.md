# Scanning 1024x576x16 out at 2112 bytes on Gen3: 244 to 585 3DMarks

2026-09-25, MICHAEL-NETBOOK, boots 12-14. Follows
`2026-09-25-padding-the-z-pitch-on-gen3.md`, where padding the Z pitch off
2048 cut a third of the per-pixel cost and left the colour buffers' shared
2048-byte pitch as the remaining suspect.

## What changed

- **The stride policy** (`v9x_mode_pitch_unaliased`, `src\common\vbe_modes.c`,
  host-tested): a 16-bpp power-of-two pitch of 1024 bytes or more gains 64.
  A family opts in through an appended `V9X_HW16_OPS.unalias_pitch`; only
  Gen3 sets it. `modes16.c` applies it to the committed runtime rows, after
  the BIOS scan's own stride has been taken, so GDI, DirectDraw's mode list
  and Enable all see 2112.
- **Programming it**: `v9x_vbe_default_pitch` asks 4F06h for `pitch / bytes`
  pixels (1056) instead of the width, and for a widened pitch sets it every
  time rather than trusting the get. The aperture check accepts a table
  pitch wider than 4F01h's only as the stride 4F06h just confirmed
  (`v9x_vbe_mode_matches_widened`, host-tested). A packed pitch behaves
  exactly as before in both.
- **The DIB Engine's stride**: `CreateDIBPDevice` derives it from biWidth,
  so for an unaliased family `deWidthBytes` and `deDeltaScan` are set to the
  mode's pitch after creation, as the Millennium II's own record builder
  does.
- **Flip-chain back buffers**: the HAL asks DirectDraw for a
  display-pitch-by-height block with `DDHAL_PLEASEALLOC_BLOCKSIZE`, as the
  DDK's S3 driver does (S3_DD32.C:3211-3222), when the display pitch differs
  from the packed row. Every other mode allocates as before.

## What each boot showed

- **Boot 12** (policy and 4F06h only): Enable succeeded, 4F06h accepted
  1056 pixels, and the panel was sheared
  (`...-first-boot-sheared.jpg`): `Surface=pitch=2112 dwb=2048 dds=2048` -
  the DIB Engine was still drawing at 2048.
- **Boot 13** (DIB stride fixed): desktop correct,
  `Surface=pitch=2112 dwb=2112 dds=2112`. 3DMark99 showed a sheared frame
  between screens (`...-screen-switch-sheared.jpg`) and bars at the top and
  bottom of the game frames (`...-3dmark-bars-{1,2}.jpg`). The flip chain
  was at 0x000000, 0x129000 and 0x249000: the second gap is 2048 x 576, so
  DirectDraw had sized the first back buffer by the packed row while giving
  it the 2112 pitch, and its last 17 rows were the third buffer's first.
  The per-pixel costs already matched 640x480 (`...-boot13-report.txt`).
- **Boot 14** (back-buffer blocks, unconditional 4F06h set): the chain is at
  0x000000, 0x129000, 0x252000 - 2112 x 576 apart. The operator reports the
  sheared frames between screens **gone**, the bars at the top and bottom
  **still present**, and the white rectangles, which predate this change,
  perhaps improved but not fixed. **585 3DMarks.**

## Measured, boot 14 (`...-3dmark1024-report.txt`)

| | stride 2048 | Z padded | **stride 2112** | 640x480 |
|---|---|---|---|---|
| 3DMarks | 244 | 280 | **585** | 692 |
| head wait, % of run | 36.2 | 21.4 | **6.7** | 2.7 |
| head wait per batch | 880 us | 580 us | **104.5 us** | 91.5 us |
| 10K-50K px batch | 1.9-2.7 ms | 1.3-2.1 ms | **162-327 us** | 170-320 us |
| 200K-1M px batch | 23-67 ms | 21-49 ms | **2.3-5.6 ms** | 2.3-5.7 ms |

**The power-of-two pitch was the whole of the tenfold per-pixel penalty at
1024x576**: with every rendered surface at 2112 the cost per pixel is the
640x480 figure. Which mechanism - render-cache sets or DRAM banks - is not
established. The CPU side is now the larger share the HAL can account for:
CPU fills 7.0% and application Lock writes 4.6%.

## Open

- **The bars**, with the flip chain now spaced correctly and the fills
  writing at each surface's own pitch (`blt_cpu.c:109`). Not explained;
  `docs\issues\2026-09-25-bars-at-the-top-and-bottom-at-stride-2112.md`.
- Whether the VBIOS answers 4F06h's get from the register or from what it
  last set is not measured; the unconditional set is correct either way.
- The white rectangles and black tests are older and separate.
