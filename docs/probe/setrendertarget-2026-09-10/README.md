# Render-target switch evidence

Guest reports behind
[the render-target switch and the uncleared depth buffer](../../decisions/2026-09-10-the-render-target-switch-and-the-uncleared-depth-buffer.md),
which holds the machine, the method, the readings and the limits. All four
files are from one boot - `Win86SE`, 86Box ViRGE/DX, agent port 9869, boot 576
- with the rebuilt `V9XDISP.DRV` (45,548 bytes, CRC32 `5FD098D9`) and
`V9XHAL.DLL` (44,032, `1FC21BD0`) applied by the `WININIT.INI` rename route.

| File | The run it came from |
|---|---|
| `v9xdd-1-counters.ini` | the first run with the counters bracketing the switch: `ChainSetTargetCalls=0`, one context destroyed and one created, `ChainTargetOffsetPost=614400` |
| `v9xdd-2-two-by-two.ini` | the same, plus the fill control (`ChainFillRaw`) and the depth/viewport two-by-two |
| `v9xdd-3-solo-depth-off.ini` | the same, plus the `Solo_*` rung's blend repeated with depth off - `SoloNoZ_x12..x48`, the ramp |
| `v9xsnap-boot576.ini` | `V9XTRACE` snapshot taken in the same boot: `D3dTargetOffset=0x00096000`, `D3dTargetPitch=1280` |

The keys that matter, in the order the record reads them:

```
ChainSetTargetHr      0x00000000   the runtime said yes
ChainSetTargetCalls   0            the driver was never called
ChainCtxDestroys      1            it rebuilt the context instead
ChainCtxCreates       1
ChainTargetOffsetPre  1228800      the 64x64 offscreen target, pitch 128
ChainTargetOffsetPost  614400      the chain's back buffer, pitch 1280

ChainFillRaw          6371         0x18E3, so the read sees the fill
ChainWallRaw          6371         depth on, viewport as inherited: nothing
ChainWallNoZRaw       32767        depth off: it drew
ChainWallZRaw         6371         depth on again: nothing
ChainWallNoZViewRaw   32767        depth off, viewport re-set: it drew
ChainWallZViewRaw     6371         depth on, viewport re-set: nothing

Solo_x12..x48         992 x 7      depth on: the blend left no mark
SoloNoZ_x12..x48      930 806 682 620 464 341 217    depth off: the ramp
```

## Reproduce

```powershell
./scripts/build-ddraw-probe.ps1
& $env:V9X_AGENT_CTL put -Port 9869 `
    -Source "$PWD\build\ddraw-probe\v9xddp.exe" `
    -Destination C:\V9XDIAG\V9XDDP.EXE -Json
& $env:V9X_AGENT_CTL exec -Port 9869 -Application C:\V9XDIAG\V9XDDP.EXE `
    -WorkingDirectory C:\V9XDIAG -TimeoutSeconds 420 -Json
& $env:V9X_AGENT_CTL get -Port 9869 -Source C:\V9XDIAG\V9XDD.INI `
    -Destination "$PWD\build\ddraw-probe\v9xdd.ini" -Json
```

The counter keys need a driver new enough to fill the six fields
`probe_counts.h` gained on 2026-09-10; an older one leaves `ChainCountsOk=0`
and writes none of them, and the pixel keys still work. `V9XTRACE` in the same
boot gives the driver's own view of the last programmed target.
