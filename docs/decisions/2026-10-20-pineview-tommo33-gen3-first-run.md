# Pineview on TOMMO33: the Gen3 engine runs; probes pass as on the 945, and 3DMark 2000 scores 755 with nothing refused

Date: 2026-10-20
Status: **measured once, one boot.** Phase 3 of
`docs\plans\intel-pineview-gma3150.md`, first run.

Machine: TOMMO33 (`M-BWCR7F`), Atom N455, `8086:A011`, Windows 98 SE.
Build: `bce8b693`, main after Phase 1 (`4de921fe` cherry-picked) on top of
the other session's `6b9b4b71`. Driver, HAL and trace agree
(`BuildsAgree=1`). Evidence: `docs\probe\tommo33-pineview-gen3-2026-10-20\`.

## Install

`build\win98se-intel-gma` was pushed to `C:\V9XIGMA` and installed by Have
Disk over the tier-0 driver. The INF matched `PCI\VEN_8086&DEV_A011`
exactly: "Velocity9x Intel GMA 3150 (Pineview)". Setup staged the in-use
`V9XDISP.DRV` as `v9xdisp.001` (115,084 bytes, the package's size) behind
plain `WININIT.INI` renames, with no `NUL=` line. One agent reboot followed:
boot 11 to 12, reconnected, desktop ready.

## What ran

- **Boot**: `enable-ok` at 640x480x8. The Gen3 MMIO fingerprint `PASS`es:
  - pipe B is live (`LivePipe=1`, `LivePlane=1`), as on the netbook;
  - panel timing 1024x600 (totals 1386x652);
  - the pipe source is 640x480, so the panel fitter scales;
  - `PGTBL_CTL` reads `00000001`;
  - the ring is idle and disabled at boot.
- **Ring plan** (`INTELRNG.TXT`): reserve, ring and heap end at
  `0x6F0000`, status page at `0x700000`, physical `7FEF0000`, the layout the
  Phase 1 host test pins. Before Phase 1 this part would have stopped at
  `ring-layout-mismatch`.
- **Mode switch**: `V9XMSW /set:1024x600x16` went live. The desktop draws at
  the panel's native mode.
- **V9XDDP**: `COMPLETE`. `V9XDD.INI` has 92 `Ok=1` and 21 `Ok=0`;
  `V9XDD2.INI` has 114 and 1. The Direct3D HAL device enumerates.
- **Snapshot** (`V9XSNAP.INI`), whole boot:
  - `EngineStatusEnable=d3d-claimed`, `Direct3DMode=hardware`;
  - `EngineCaps=0x00022614`: D3D, flip, ring flip, async submit, and
    `I9XX_NO_FW_BLC`;
  - 599 draws, 0 refused;
  - 613 breadcrumb submits, 0 timeouts;
  - the status-page self-test passed, needing 0 polls;
  - 0 ring-space waits or timeouts.

The ring and the status page at the moved reserve work. That is Phase 1's
item 1 measured.

## The watermark

`WmWrites=0`. The HAL computed `FW_BLC` `0x0314011A` against the
`0x03060106` it found, at 54,180 kHz, and did not write it, because
`I9XX_NO_FW_BLC` was set. That is item 2 measured. The register reads
`0x03060106`, `FW_BLC2` `0x00000306`, `FW_BLC_SELF` `0`. What the DSPFW
self-refresh fields hold was not captured.

## Failures

Against the GMA 900's run of 0.21.0
(`docs\probe\tommo33-gma900-0.21.0-2026-10-20\ddp\`), five `Ok=0` checks
appear here and not there.

- **`Tex8A8888HalvesOk`, `Tex8X8888HalvesOk`.** These are new checks:
  `6b9b4b71` added them to V9XDDP for Haswell's 32-bit textures. Gen3
  offers no 8888 texture format; the surface comes back unreadable
  (`LeftRaw=65535`), and `D3dTextureRefusedFormat=4`. This is expected on
  every Gen3 part and is not a Pineview finding.
- **`ChainBOk`, `ChainCOk`, `ChainRowsOk`.** The probe's mip chain colours
  level 0 red (`63488`), level 1 green (`2016`) and level 2 blue (`31`).
  - On the GMA 900, samples at sweep points 17 to 27 read green, and blue
    from 30.
  - Here they read blue from 17; level 1 is never seen.
  - **This is not a Pineview finding.** The netbook's 945GSE reads the same
    on every run that has the checks:
    - `netbook-915gm-build-check-2026-10-08`;
    - `netbook-gma950-0.19.0-2026-10-18`;
    - `netbook-gen3-d3d-32bpp-2026-10-18`;
    - `netbook-gen3-z24-2026-10-18`.
  - Every `Chain*`, `Mip*`, `Lod*` and `PerspW*` key matches the netbook's
    boot 211 report (`V9XDD-boot211.INI`), with two exceptions:
    - texture addresses;
    - three trilinear `MipTri_*_Raw` values a few LSBs apart.
  - Pineview's `Ok=0` set equals the netbook's plus the two 8888 checks.
  - So the parts placed in the 945 layout (945GSE, Pineview) read level 2
    where Direct3D's nearest level is 1. The 915 placed stacked does not.
    The cause is open on both.
  - The first version of this record called it Pineview's, with two
    hypotheses; the netbook comparison disposes of that.
- `FlipPixelOk=0` in `V9XDD2.INI` fails on the GMA 900 as well.

## OpenGL, Glide and 32 bpp (same boot, later)

Probes built from `d3fb1b2c` and run from `C:\V9XIGMA` on the 1024x600x16
desktop:

- **V9XGLP** (OpenGL ICD): `Result=PASS`, 30 checks `Ok=1`, none `Ok=0`.
  The renderer string reads "Velocity9x GMA 950": the name is chosen from
  the stacked-mip bit alone, so Pineview gets the 945's name. This is
  cosmetic, and not fixed.
- **V9XGLIDP** (Glide 2): `Result=PASS failures=0`.
- **V9XD32**: 640x480x32 `Result=PASS`, `Failures=0`, `T4Mismatches=0`.
  The `/bpp16` control at 640x480x16 also passes. The desktop came back at
  1024x600x16.
- **Snapshot after** (`V9XSNA1-after-probes.INI`, same boot):
  - 788 draws, 891 breadcrumb submits;
  - 0 breadcrumb, ring-space or flip-wait timeouts;
  - `WmWrites=0`.

## 3DMark 2000 (same boot, later)

3DMark 2000 v1.1 was installed from Michael's copy, registered Pro, and
run as a custom benchmark. Its default needs 1024x768x32 and 16 MB and is
refused here. 3DMark chose 1024x600, 16-bit colour, 16-bit textures and
Z, triple buffered, all tests, on "Velocity9x Intel GMA 3150 (Pineview)".
It reports 7,067 KB of video memory, 5,867 KB of texture memory, and one
texture blending stage. Screens are in `3dmark2000\`.

**755 3DMarks**, CPU 62. Recorded, not compared.

| Test | Result |
|---|---|
| Helicopter low / medium / high | 15.2 / 10.3 / 5.3 fps |
| Adventure low / medium / high | 13.2 / 10.5 / 8.4 fps |
| Fill rate single / multi | 73.4 / 73.4 MTexels/s |
| High polygon 1 / 4 / 8 lights | 721 / 658 / 617 KTriangles/s |
| Texture rendering 8 / 16 / 32 / 64 MB | 42.0 / 21.9 / 11.4 / 5.7 fps |
| Bump mapping (emboss 1-3 pass, environment) | not supported |

Both game scenes were seen drawing textured and correct in mid-run
screenshots (`game1-helicopter.png`, `game2-adventure.png`).

The snapshot after the run (`3dmark2000\V9XSNA-after-3dmark.INI`), whole
boot:
- 849,002 draws, 854,811 breadcrumb submits;
- 0 draw refusals of any kind;
- 0 breadcrumb, ring-space or flip-wait timeouts;
- `WmWrites=0`.

The texture refusals are the six V9XDDP left before the run. 47,207 mip
trees were declined for room (reason 4): the 7 MB heap of 8 MiB stolen
memory, against texture tests sized at 16 to 64 MB.

## 24-bit depth, every mode, and 3DMark 2000 at 32 bpp (same boot)

- **V9XD32 `/z24`**: `PASS` at 640x480x32 and with `/bpp16`, 0 failures
  (`V9XD32-bpp32-z24.txt`, `V9XD32-bpp16-z24.txt`). The HAL offers 16- and
  32-bit Z (`HalZDepth=0x500`).
- **V9XMSW `/set`** through all nine modes the BIOS describes: 640x480,
  800x600 and 1024x600 at 8, 16 and 32 bpp. Every one ended
  `Result=PASS`, then back to 1024x600x16 (`modes\`).
- **3DMark 2000 at 800x600x32**, 16-bit textures and Z, double buffered
  (`3dmark2000-32bpp\`). 3DMark does not offer 1024x600x32; the likely
  reason, not checked, is that it sizes the mode for triple buffering.
  - **851 3DMarks**, recorded and not compared. 3DMark gives no CPU score
    off 16 bpp.
  - Helicopter 18.2 / 12.7 / 5.7 fps; Adventure 14.5 / 11.1 / 8.6.
  - Fill rate 105.1 / 106.3 MTexels/s; high polygon 715 / 653 / 605
    KTriangles/s.
  - Texture rendering at 8 / 16 / 32 / 64 MB: 46.1 / 23.8 / 12.1 / 5.7 fps.
  - Bump mapping not supported.
- **Snapshot after both runs** (`V9XSNA-clocks-after-32bpp.INI`):
  - 1,492,355 draws, none refused;
  - 0 breadcrumb, ring-space or flip-wait timeouts;
  - `WmWrites=0`;
  - 77,659 mip trees declined for room.

## Mode-set state and video BIOS

V9XTRACE built from `6377f101` (`-clk`) adds the live pipe's clock and
panel registers, Pineview's DSPFW words, and an opt-in `-vbios` image of
`C000:0000`. It was run as `V9XTRC2.EXE -vbios`. Read-only:

| Register | Value |
|---|---|
| Live pipe | B |
| `DPLL_B` | `0x98046000` |
| `FPB0` / `FPB1` | `0x00100077` / `0x00200074` |
| `PP_STATUS` / `PP_CONTROL` | `0xC0000008` / `0x00000001` |
| `PP_ON_DELAYS` / `PP_OFF_DELAYS` / `PP_DIVISOR` | `0x025807D0` / `0x01F407D0` / `0x00209D05` |
| `BLC_PWM_CTL` | `0x65ED65EC` |
| `DSPFW1` / `DSPFW2` / `DSPFW3` | `0x3F830F0F` / `0x0000030F` / `0x0F200100` |

The DPLL fits Linux's Pineview formula exactly. The fields
(i915_reg.h, `pnv_calc_dpll_params`):
- `FPB0` N field `0x10`, one-hot n = 4; M2 = `0x77`, so m = 121.
- `DPLL_B` P1 field `0x08`, so p1 = 4.
- LVDS mode with P2 bit 24 clear, so p2 = 14.
- Reference input `3 << 13`, spread spectrum, where i915 uses the VBT's
  SSC frequency.

At 100 MHz that gives vco 3,025 MHz and a 54.0 MHz dot clock. Against the
pipe's measured 1386x652 totals that is 59.8 Hz. At 96 MHz it would be
57.4 Hz. 100 MHz is inferred from the fit, not read from the VBT.

Watermarks: `DSPFW1`'s self-refresh field (bits 31:23) is `0x7F`, and
`DSPFW3` bit 30 (`PINEVIEW_SELF_REFRESH_EN`) is clear. **The BIOS leaves
CxSR off**, so the self-refresh watermarks are inert at this mode. Writing
none of them (item 2) loses nothing here. If CxSR is ever turned on,
`pnv_update_wm`'s arithmetic is the one to apply.

The image is kept outside the tree, since the repository is public and the ROM is Intel's and Lenovo's, in `claude\personal\v9x-testers\tommo33-2026-10-20\vbios\gma3150-lenovo-17AA-3908-V9XVBIOS.BIN`, SHA-256 `dd7509459e79bb2be9f786affd960dd7929ba398f0e72c2ddb5ecd9e8efe6041`. It is 55,808 bytes (header `0xDA00`), signature `AA55`, "PineView
Graphics Chip Accelerated VGA BIOS", one `$VBT` and `BIOS_DATA_BLOCK`. That
is the same size as the Acer NAV50's image in its survey; the contents
have not been compared.

## Not done

- No game beyond 3DMark 2000.
- The VBT has not been decoded.
- The netbook has not run `bce8b693`, so the `vbe-size-unread` change on the
  945 is still unmeasured.

## Corrected record

The tier-0 record said Setup renamed the `glide2x.dll` and `glide3x.dll`
already in `C:\WINDOWS\SYSTEM` to `.001`. That is backwards. The
`WININIT.INI` lines are `dest=src`: Setup copied Velocity9x's DLLs in as
`.001` because the originals were in use, and the rename put ours in their
place. Tom's original Glide DLLs were replaced, not kept. Both are now
Velocity9x's (84,992 and 85,504 bytes).
