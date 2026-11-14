# Intel GM965 texture pool in the driver: mapped at boot 56, the probes pass, mip trees sample from it

Date: 2026-11-14
Machine: TOMMOHP (`M-KYHT4Q`, Tom Rossell's HP laptop, GM965 GMA X3100
`8086:2A02`, subsystem `103C:30C0`), through remote help, boot 56.
Build: `eae139be-dirty` on the `gm965-texture-pool` branch (dirty by
untracked evidence only), all eight intel-gma files by
`run-release-checks.ps1`, 8 of 8 hashes matching, `reset`.
Gate: `2026-11-14-intel-gm965-texture-pool-decisions.md`.
Evidence: `docs/probe/gm965-texture-pool-driver-2026-11-14/`.

Boot 1 of the ten the gate's conditions ask for.

## What the boot shows

| | |
|---|---|
| `V9XBOOT.INI` | `Stage=enable-ok`; `DpmiMap=04000000:ok`; `MtrrWrite=ok s=3 b=d0000001 m=fc000800` (64 MiB write-combined at `D0000000`) |
| `[IntelPool]` | `Result=kept`, `StateBefore=2`, `PlanOffset=00800000`, `PlanBytes=02000000`: mapped by an earlier Enable in this boot and kept by the one that wrote the record |
| GTT inventory | entries `0`-`77E` stolen memory, `77F`-`7FF` the scratch page, **`800`-`27FF` one linear run from `0450E000`** (the pool, 32 MiB physically contiguous), `2800` up the scratch page |
| heap | unchanged, `vram=7798784` |

## What ran on it

- The release probe set: V9XD32 default, V9XGLP, V9XGLIDP (0 failures),
  V9XR3DP, V9XWPRS, V9XFBCMP pass; then V9XD32 `/bpp16` and `/z24` pass.
- `V9XHEAP.EXE`: textures see `0247C000` (36.5 MiB) where off-screen and
  Z surfaces see `0047C000` (4.5 MiB). 4,096 64x64 textures created, the
  probe's own limit, without a refusal; the highest at `E7989000`, inside
  the pool's range (`E5E07000` up, the address Phase 2's probe mapped it
  at). No off-screen or Z surface above `E5C75000`.
- V9XD32's T21 samples four levels of a mip tree and reads each back
  exactly (`F800`, `07E0`, `001F`, `FFFF`) at both depths. The Gen4 HAL
  places mip trees itself through `d3d_place.c`, which tries the pool
  first; so those levels were sampled from pool memory. That is by the
  placement's order: no address was logged to show it.

## Differences from Gen3 seen here

- DirectDraw places a plain (single-level) texture itself on Gen4, and it
  fills heap 0, stolen memory, before heap 1: V9XHEAP's first texture is
  at the start of heap 0, where the netbook's was at the pool's first
  byte. Small textures therefore still compete with targets and Z for
  the 4.5 MiB until it is full. Not changed.

## Not established

- A GPU read of the pool proved by address, or by a texture known to be
  DirectDraw-placed there.
- Erratum 4's corruption under load: no game, no 3DMark on this build.
- A mode change, a full-screen DOS box, and nine more boots.
- The blue screen of the first Phase 2 probe run remains unexplained.

## State left behind

TOMMOHP runs this build at boot 56 with the pool mapped. `V9XHEAP.EXE` is
in `C:\V9XREL`.
