# Pineview on TOMMO33: the Gen3 engine runs, and mip levels sample one too deep

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
  - Here they read blue from 17. Level 1 is never seen; where level 1 is
    expected, level 2 comes back.
  - Chains A, D and E, and the linear-filter chain, pass.
  - Two hypotheses, neither tested:
    - the level-1 offset the HAL writes differs from where Pineview fetches
      it;
    - Pineview selects LOD one level higher for the same derivatives.
  - Mesa sets `is_i945` for Pineview, which argues against a layout
    difference but does not rule one out.
  - A Mesa comment in `i915_texstate.c` (`gen3-source-references`) records
    that Pineview, "DevBLB derived", sampled wrongly until a layout was
    aligned to 4. It concerns cube maps.
- `FlipPixelOk=0` in `V9XDD2.INI` fails on the GMA 900 as well.

## Not done

- No game and no V9XD32 run.
- No 32 bpp run.
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
