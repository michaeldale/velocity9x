# Rage IIC: ATI's driver sampled under Half-Life, and the texture cache we kept off

Date: 2026-10-03. Machine: A8U4I5 (Rage IIC AGP `1002:4757`, P3 1002 MHz,
Win98SE). Evidence: `docs/probe/a8u4i5-rage-iic-registers-2026-10-02/`
`swap-to-ati/` (class keys, ATI's caps `V9XDD-ATI.INI`), `hl-sample-v9x/`,
`hl-sample-ati/`, `hl-sample-ati-2/` (`ATIRX /sample`), `phase7-texcache/`
(`/fillcache`, `/texcache`), `hl-d3d-mwd5-texcache/`, `phase7-texcache-q2*/`.

Michael asked for the swap to ATI's driver, to find what it does that
makes Half-Life mwd5 run at 6.63 fps against our 4.29.

## The swap

`Display\0006\DEFAULT` names the driver (`SYSTEM.INI` has
`display.drv=pnpdrvr.drv`): `drv`/`drv2` `ati2ddad.drv`, `minivdd`/
`minivdd2` `ati2ddad.vxd`, from the key exported before Velocity9x went
on (`display-class-before-velocity9x.reg`). The current key was exported
first (`display-class-velocity9x-before-swap.reg`), the ATI values
imported with `REGEDIT /s`, a warm restart. ATI's community 4.11.2611
came up at 1024x768x16 with its tray icon. The way back was the saved
key re-imported and `"minivdd2"=-` (Win98's REGEDIT4 deletes a value
that way), a warm restart, our HAL answering `V9XTRACE`. No DOS, no
WININIT.

## Half-Life under ATI's driver, measured

mwd5, same procedure: 6.090, 6.593, **6.605** fps, as the baseline's 6.627.

`ATIRX /sample` (new, read-only): 6,000 samples 5 ms apart during the
demo, each reading `GUI_STAT` eight times and the 3D state once.

| | Velocity9x | ATI 4.11.2611 |
|---|---|---|
| engine busy (`GUI_ACTIVE`) | 71% | 78% |
| `SCALE_3D_CNTL` low bits | `0A0`: bit 5 set | `28F`: bit 5 clear; 0-3, 9 set |
| `MIP_MAP_DISABLE` | set | set in every sample: ATI does not mip-map Half-Life |
| textured, Z disabled (`Z_CNTL` `120`) | not seen | 1,962 of 6,000 samples |
| `DP_PIX_WIDTH` target | 565 | 555 |

ATI's D3D caps (`V9XDDP`, `V9XDD-ATI.INI`) against ours: misc `0x73`
(adds MASKPLANES, MASKZ) against `0x70`; raster `0xB7` (adds DITHER,
ROP2, XOR, SUBPIXEL) against `0x90`; dev caps adds SORTINCREASINGZ;
alpha compare **0** against our `0xF0`; texture adds TRANSPARENCY;
filters `0x1F`, as ours now.

## The texture cache

Bit 5 of `SCALE_3D_CNTL` is `TEX_CACHE_DIS`. Phase 4 measured with it set
and the policy kept it; no record says why. `ATIRX /fillcache`, the
`/fill` scenes with bit 5 cleared after the HAL's builder (engine clocks
a pixel):

| Scene | Cache off | Cache on | + ATI's bits 0-3, 9 |
|---|---|---|---|
| bilinear 64x64, Z | 17.1 | **11.0** | 11.0 |
| bilinear 64x64, Z, lightmap blend | 19.4 | **12.3** | 12.3 |
| bilinear 64x64 | 12.4 | 9.5 | 9.5 |
| bilinear 256x256 | 15.4 | 10.7 | 10.7 |
| point 64x64, Z | 7.8 | 6.3 | 6.3 |
| bilinear 256x256 at 4:1, level 0 | 18.8 | 18.6 | 18.6 |
| bilinear 64x64 at 1:1 (that chain's level 2) | 12.2 | 9.0 | 9.0 |

Dithering and bit 9 cost nothing; the cache is the difference. Minified
level 0 does not cache, so with the cache on mip-mapping now halves a
minified bilinear pixel.

`ATIRX /texcache`: the 32x32 map drawn, the CPU then rewrites every
texel's blue, the map drawn again - with the full texture state, with the
trapezoid alone, after `TEX_5_OFF` alone, after toggling bit 5, from a
second map and back. Every one of the eight scenes read the texels as
last written (256 of 256 pixels each), the trapezoid-alone ones too: the
cache is refilled at least at every trapezoid trigger. The HAL re-emits
the texture state every batch besides.

The policy now leaves the cache on (`rage2_draw.c`; `test_policy` and
`test_mip` assert it).

| Run (640x480) | Before | Cache on |
|---|---|---|
| Half-Life mwd5 | 4.294 | **5.573** (5.216, 5.573, 5.562) |
| Quake 2 demo | 3.5-3.6 | **4.4** |
| Quake 2, 320x240 window | 6.8 / 7.1 | 7.3 / 7.5 |

Pictures checked correct in both. ATI's 6.63 is now 16% ahead, not 54%.

## Hypotheses the evidence killed

- **ATI mip-maps Half-Life.** `MIP_MAP_DISABLE` is set in all 6,000 ATI
  samples.
- **The remaining gap is fill the chip cannot avoid.** A third of it was
  one bit of ours.
- **The texture cache needs flushing on a texture change.** It never
  returned a stale texel, with or without state between draws.

## Open

- Half-Life draws textured, non-blended passes without Z under ATI's
  driver (1,762 samples of `0B6C028F` with `Z_CNTL` `120`), and not under
  ours. **Not MASKZ**: advertised from the same day (we serve it with
  `Z_CNTL` bit 8), mwd5 ran 5.500, 5.596, 5.583 fps against 5.573, and
  `/sample` (`hl-sample-v9x-maskz`) shows only the alpha-masked state
  `51400080` gaining a Z-off share (692 samples); the bilinear state stays
  Z-on (`0B400080` `121`, 2,785). The other cap differences - SUBPIXEL,
  DITHER, SORTINCREASINGZ, TRANSPARENCY, no alpha compare - are untested.
- CPU setup is now a larger share (15-24 k cycles a piece against 23-36 k
  of emit).
