# The Rage XL runs a 5:5:5 desktop: its Direct3D draws 1555 as it draws 565

Date: 2026-10-14

Status: Measured on A8U4I5 (ATI 3D Rage XL PCI, 1002:4752, Rage Pro
class), boots 466–469. Phase 3 of
[ati-555-desktop.md](../plans/ati-555-desktop.md). Evidence:
`docs/probe/a8u4i5-rage-xl-555-2026-10-14/`.

## The change

- **`mach64_policy.c`** accepts an XRGB1555 target.
- **`mach64_engine.c`** sets DP_PIX_WIDTH's destination field to code 3
  for one: `0x40040443` instead of `0x40040444`. That is the code ATI's
  driver uses for the Rage II's 1555 targets. Every 3D builder inherits
  the value from `v9x_m64_build_flat_state`, and the texture builder
  replaces only the top nibble.
- **`d3d_mach64_map.c`** carries the request's target format into the
  draw state.
- **Untagged XRGB1555 textures**, as the Rage II takes them
  ([2026-10-14](2026-10-14-rage-iic-555-textures-and-glide.md)):
  - The classifier names them.
  - The policy accepts them without texel alpha.
  - The texture builder samples them as 1555 without TEX_MAP_AEN.
- **`dd16.c`**: `v9x_dd_engine_wants_555` is true for the Mach64 engine
  type, so the Rage Pro class chooses 5:5:5 under hardware Direct3D.

Host tests were written first for each step, and each failed before its
fix: 4 checks for the target, 6 for the textures.

## The A/B

V9XDDP, comparing every key ending `Ok`:

| Boot | Layout | Build | Pass |
|---|---|---|---|
| 467 | `565-auto` | target change only | 205/222 |
| 468 | `555-ini` (`HighColor=15`) | target change only | 204/222 |
| 469 | `555-auto` (no `HighColor`) | with texture fix and `wants_555` | 205/222 |

- **Boot 468** differed from 467 in one key, D3DUntaggedTexture (raw `0`
  against `0x03E0`). The Rage Pro classifier then refused XRGB1555
  textures.
- **Boot 469** passes exactly the same keys as boot 467: the untagged
  texture reads `0x03E0`, and the target masks are `7C00/03E0/001F`.
- **The 17 keys failing at both layouts** are unchanged by this work:
  - AlphaCurveF1–F4, ChainB/C
  - D3DShapes, D3DZPCompare, D3DZPWriteMask
  - FlipPixel, MipTriDegraded, Mixed*, Targets, TexMatrix

Unlike the Rage IIC (boot 461), BlendModulate passes at 5:5:5 here.

## Diablo II (Glide 3)

Boot 469, default layout:
- Glide opened at 640x480, 640x480 and 800x600, then 640x480 for the
  town, each with `device: target 555=1`.
- 1,100,234 triangles drawn, 11,776 refused (1.1%). Every logged refusal
  is `storage/format=0203`, the translucent keyed sprites the Rage XL
  refused at 565 (~0.8%, 2026-10-10).
- The first attempt quit at the main menu: the scripted clicks ran one
  screen early and hit "Exit Diablo II".
- Michael watched the town on the monitor: "looks good".

## NFS II SE demo (Glide 2)

Michael ran it on the monitor: "looks good". On the Rage IIC this demo's
two 565 write locks went through the LFB conversion. Its log was not read
for this run, so the lock path taken on this card is not recorded here.

## Half-Life (Direct3D)

Michael ran Half-Life's Direct3D renderer on the 5:5:5 desktop and
reported that it works (2026-10-14). That is an eyes-on check; no log
was read.

## Not done

- **The Gateway's Rage Mobility** gets the same choice through the same
  engine type and has not booted it. The decision follows the
  same-design rule.
- **The 2D engine** keeps DP_PIX_WIDTH code 4, as on the Rage IIC: fill
  and copy are bit-copies.

Gates: build-host, run-checks.
