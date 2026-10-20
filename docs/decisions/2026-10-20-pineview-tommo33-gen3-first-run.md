# Pineview on TOMMO33: the Gen3 engine runs; D3D, OpenGL, Glide and 32 bpp probes pass as on the 945

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

## Not done

- No game.
- No DSPFW capture.
- The netbook has not run `bce8b693`, so the `vbe-size-unread` change on the
  945 is still unmeasured.

## Corrected record

The tier-0 record said Setup renamed the `glide2x.dll` and `glide3x.dll`
already in `C:\WINDOWS\SYSTEM` to `.001`. That is backwards. The
`WININIT.INI` lines are `dest=src`: Setup copied Velocity9x's DLLs in as
`.001` because the originals were in use, and the rename put ours in their
place. Tom's original Glide DLLs were replaced, not kept. Both are now
Velocity9x's (84,992 and 85,504 bytes).
