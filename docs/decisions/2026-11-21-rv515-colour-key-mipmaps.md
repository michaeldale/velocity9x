# RV515 colour key and mipmaps: the RS690's key, 32-byte mip rows, Forsaken runs

Date: 2026-11-21
Machine: remote-help `M-5CAPM3`, Radeon X1300 `1002:7146`, boots 46-47.
Builds: the V9XR53D probe from the tree at `44a4cf1b` on boot 46 (driver
`72d05d70`); ati `44a4cf1b` (DRV, VXD, HAL, SETP, GL by WININIT rename)
on boot 47. After the reboot the staging folder was empty and all five
installed hashes matched the package.
Reports in
[`docs/probe/m-5capm3-rv515-2026-11-21/mips-boot46-47/`](../probe/m-5capm3-rv515-2026-11-21/mips-boot46-47/).
Plan: [`ati-rv515-x1300.md`](../plans/ati-rv515-x1300.md), phase 5.

## Mip rows are 32 bytes on the R500

The first extended run of V9XR53D on boot 46 failed MipSelect: levels 2-4
showed level 1's colour. MipLayout measured where the sampler reads each
level of a 32x16 chain:

| Level | RS690 offset / stride (16 bpp) | X1300 offset / stride (16 bpp) | X1300 (32 bpp) |
|---|---|---|---|
| 0 | 0 / 64 | 0 / 64 | 0 / 128 |
| 1 | 1024 / 64 | 1024 / 32 | 2048 / 64 |
| 2 | 1536 / 64 | 1280 / 32 | 2560 / 32 |
| 3 | 1792 / 64 | 1408 / 32 | 2688 / 32 |
| 4 | 1920 | 1472 | 2752 |

The rule that fits every row: each level's rows are width x bytes rounded
up to 32, and each level starts right after the last. The RS690 rounds to
64 ([`2026-11-06-rs690-mipmaps.md`](2026-11-06-rs690-mipmaps.md)). The
32 bpp column shows rows of 16 bytes (level 3, 4 texels) still step 32,
so it is alignment, not a minimum of one row per 32 texels at 16 bpp.
The measurement covers only chains, which run without TXPITCH_EN. A single
level is drawn with TXPITCH_EN at the pitch given, and TexQuadrants (an
8x8 ARGB1555 texture at a 64-byte pitch) passes on this card at both
depths. So single levels keep the 64-byte pitch; whether 32 also works
for them was not tested.

The change (`44a4cf1b`): `struct v9x_rs690_3d_texture` gains `row_align`
(0 = the RS690's 64), with aligned forms of `v9x_rs690_mip_pitch` and
`v9x_rs690_mip_offset`. `rv515_3d.c` passes `V9X_RV515_TEX_ROW_ALIGN`
(32) to the shared builder, and the HAL places and resolves X1300 mip
chains at 32. Host tests hold the measured offsets.

The rerun with that probe, boot 46: MipSelect PASS, 0 mismatched bytes,
at 800x600x16 and 800x600x32. MipLinear passes at both depths too. 33 of
33 judged tests pass at each depth.

## Colour key: the same as the RS690

The `Chroma*` tests (key = texel (5, 3), drawn 1:1 nearest, REPLACE) give
the same result as the RS690 record
([`2026-11-06-rs690-colour-key.md`](2026-11-06-rs690-colour-key.md)),
pixel for pixel, at both depths:

| Test | 16 bpp | 32 bpp |
|---|---|---|
| ChromaForceRaw (key as stored) | 1 killed | 1 killed |
| ChromaForce (565 widened, replicated, alpha FF) | none | 1 killed (the key is the raw texel at 32 bpp) |
| ChromaForceZeroFill | none | 1 killed |
| ChromaForceRawFF (top byte FF) | none | none |
| ChromaBlend | none killed | 1 pixel changed, none killed |
| ChromaNearest2x | 4 killed | 4 killed |
| ChromaLinear2x | 16 killed | 16 killed |

So FORCE with the raw texel, top byte zero, is the key on the R500 as on
the RS690, and the HAL's RS690 mapping applies unchanged. No keyed game
draw has been checked by eye on this card.

## Boot 47: the HAL with XRGB8888 and the 32-byte chains

- `V9XD32` at 800x600x16: `Result=PASS`, `Failures=0`. T22X, the
  XRGB8888 texture that failed on boot 46, now draws all four pixels
  right (`T22XOk=0x0F`) after `d244e7a3` offered the format in the shared
  RS690 HAL. Not rerun at 32 bpp.
- Forsaken (`ForsakenHW.exe`), left for about 100 s from launch, then
  closed with WM_CLOSE. `V9XTRACE` (`V9XSNA5.INI`) after it shows:
  - driver and HAL both `44a4cf1b`, `EngineType=18`;
  - `M64Draws=4634612`, `M64TextureDraws=4633016`, `M64Refused=0`,
    `M64Unrenderable=0`;
  - `D3dMipChainChecks=4`, `D3dMipChainLevels=6`: the game's chains were
    found where the HAL placed them;
  - `FlipHandled=5991`, `FlipDeclined=0`.
  The agent's screenshot shows only the GDI window behind the exclusive
  full-screen surface, so **nobody has seen the frames**. Afterwards the
  desktop came back intact.
- `BootCounter` stayed 47 throughout.

## Not done

- A person looking at 3DMark and a game: asked of the owner in chat.
- V9XD32 at 32 bpp on the `44a4cf1b` HAL.
- The RS690 has not been rerun against the shared-code changes
  (`row_align` defaults to its own 64; XRGB8888 is new for it too). No
  RS690 machine is reachable.
