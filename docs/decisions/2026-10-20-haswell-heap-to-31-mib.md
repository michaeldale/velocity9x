# HD 4600: the heap runs to 30.8 MiB, and 3DMark 2001 SE declines no mip tree for room

2026-10-20, i5-4570, Windows 98 SE, boot 54, build `e3c647fb-dirty`
(driver, HAL, mini-VDD and V9XTRACE from one package, deployed by
WININIT rename). This is Phase 2 and Phase 3 of
[the heap plan](../plans/intel-haswell-heap-beyond-16-mib.md). Evidence:
[`../probe/hsw-4570-heap-phase3-2026-10-20/`](../probe/hsw-4570-heap-phase3-2026-10-20/).

## What changed

- The HD 4600 maps 64 MiB of GMADR (`V9X_HW16_DEVICE.map_bytes`). Gen3
  keeps the family's 16 MiB.
- At the first Enable, `hsw_hw16.c` reads GGC and `1082C0h` (the new
  mini-VDD verb `HSW_STOLEN_RESERVED`). It then plans the layout with
  `v9x_intel_hsw_protected_bytes` and `v9x_intel_hsw_plan_heap`, both
  host-tested. Anything it cannot read or does not understand keeps the
  old layout, a 16 MiB heap with the reserve at `1800000h`, and
  `HeapLayout=` in `V9XHW.INI` says why.
- `HSW_ENGINE_OPEN` takes the reserve offset in ESI. An open ring at
  another offset is closed and reopened.
- The HAL reads the offset from `engine.reserve_offset`, new in shared
  ABI 2026102001, and no longer from a constant.
- `V9XHSWMM.EXE` finds the live reserve from the BCS ring's START.
  `/bcs` refuses while the ring is on, and `/rcs` uses the probe region
  after the reserve.

Gates: `check-tree.ps1`, `build-host.ps1` and `run-checks.ps1` passed.

## Boot 54

`V9XHW.INI`: `HeapLayout=chip`, `StolenBytes=02000000`,
`Reg1082C0=DF100017`, `ProtectedBytes=00100000`,
`ReserveOffset=01ED0000`, `HeapEnd=01ED0000`,
`EngineStatusEnable=open-reused`. The heap ends 30.8 MiB into the
aperture, up from 16 MiB. The reserve sits 192 KB below the protected
megabyte: 128 KB of reserve, then the 64 KB probe region.

## Gates

| Gate | Result |
|---|---|
| Boot, engine open at the planned offset | passed |
| V9XDDP, fresh boot | `COMPLETE`, 207 `Ok=1`, the same 17 `Ok=0` as the 0.21 control |
| 3DMark 2001 SE, default benchmark | ran to its score, 0 engine timeouts or resets |
| Mode switches 1024x768x32, then 800x600x16 (V9XMSW) | both `PASS`; engine reused at `01ED0000` |
| V9XDDP after the switches | `COMPLETE`, 207 and the same 17 |

The 3DMark 2001 SE snapshot, against boot 51 (before this change, same
benchmark):

| | boot 51 | boot 54 |
|---|---|---|
| `MipTreeDeclined` | 7,366 (last reason 4, no room) | **0** |
| `D3dMipChainGaps` | 46,211 | 78 |
| `MipTreeAllocs` | 17,608 | 925 |
| `BatchesEngineRefused` | 0 | **8** |
| flips handled / declined | 3,634 / 0 | 4,321 / 0 |

The score was 1094 (recorded, not compared). 3DMark still says the run
is not at default settings, because textures are 16-bit on this chip.

## Not explained

- **8 refused batches**, all mapping-policy refusals: 1 texture
  (`M64Policy03`), 2 texture operation (`04`) and 5 colour key (`07`).
  Colour key has been refused by design since F2a. Boot 51 had none.
  The likely difference is that textures which used to fail placement
  now reach these paths, but nothing measured that, and which test they
  fall in is not known.
- `MipTreeAllocs` fell from 17,608 to 925 while the declines went to 0.
  Fewer trees freed and remade would fit, but this was not checked.
- 78 mip chains still have a level out of place.

## Gen3: a tester's GMA 900, same build

The Gen3 build changed too: the new device fields are 0 for Gen3,
`hsw_config` is linked into the 16-bit driver, and the ABI stamp moved.
It was run on TOMMO33 (915GM, GMA 900, 640x480x16) through remote help
(`M-NGMX0T`), with the same WININIT rename and a Windows restart.
Evidence:
[`../probe/tommo33-gma900-heap-build-2026-10-20/`](../probe/tommo33-gma900-heap-build-2026-10-20/).

- Boot: `Stage=enable-ok`, no Haswell keys in `V9XHW.INI`.
- V9XDDP: `COMPLETE`, 207 `Ok=1`, and the same 17 `Ok=0` as this
  machine's 0.21.0 run. `GblHalVidMemTotal=0x0061A000`, unchanged.
- V9XTRACE: driver, HAL and trace all `e3c647fb-dirty`
  (`BuildsAgree=1`), Gen3 engine stamped with its ring, 0 timeouts or
  resets.

## Not run

- Half-Life mwd5, and a DOS box round trip on the HD 4600. The mode
  switches exercise Enable and the engine's reuse, but not Disable's
  close and reopen. The GMA 900's Windows restart did run Disable, on
  the Gen3 path only.
- The netbook (945GSE). The GMA 900 run covers the Gen3 build, but not
  this chip.
- A board where `1082C0h` reads differently. The tester's 64 MiB board
  has not reported back.
- Michael's 3DMark 2001 SE flicker
  ([issue](../issues/2026-10-20-haswell-3dmark2001se-flicker.md)). Not
  looked at; this run was not watched.
