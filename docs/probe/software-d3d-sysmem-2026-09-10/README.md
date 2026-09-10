# System-memory texture evidence

Guest reports behind
[the software engine can sample a system-memory texture](../../decisions/2026-09-10-software-d3d-system-memory-textures.md),
which holds the method, the readings and the limits. All from
`Win98SE-Trio64`, 86Box, agent port 9871, `Direct3D=2` so every Direct3D draw
is served by the CPU rasterizer.

| File | The run it came from |
|---|---|
| `trio64-sysmem-refused.ini` | `D3DSoftSysMem` absent: `SysMemTexRaw=65535`, white, the draw went untextured |
| `trio64-sysmem-allowed-erased.ini` | the setting on but the capability bit erased by the second stamp site: still 65535, with `SysMemTex_Dref=1` |
| `trio64-sysmem-allowed.ini` | both stamp sites fixed: `SysMemTexRaw=2016`, green, equal to `D3DExpectGreen` |
| `v9xsnap-refusal-counted.ini` | the `V9XTRACE` snapshot that named the middle row - `D3dTextureRefusedSysmem=1`, caps `0x1800` |
| `v9xsnap-after-fix.ini` | the same snapshot after the fix: every refusal counter zero |
| `trio64-hw-allowed.ini` | that boot's `V9XHW.INI`: `Direct3DMode=software`, `D3DSoftSysMem=allowed` |

The three probe reports are otherwise the same run of the same binary, so
`compare-probe.ps1` over any pair shows what the setting changed and nothing
else.

## Reproduce

```powershell
./scripts/build-ddraw-probe.ps1
# and the driver pair, which carries the setting and the counters
./scripts/run-checks.ps1
```

Install the pair in the guest by the `WININIT.INI` rename route, put
`Direct3D=2` and `D3DSoftSysMem=1` in `[Velocity9x]` of the guest's
`SYSTEM.INI`, reboot, then run `V9XDDP.EXE` and read `SysMemTexRaw`. Green -
2016 in this guest's 565 layout - is a pass; 65535 is the vertex colour and
means the texture was not sampled. `V9XTRACE.EXE` in the same boot gives the
refusal breakdown, which is what tells a refusal apart from a draw that
missed.

One caution about the third file: the video-memory `BlendModulate` cell reads
65535 in all three, and in both of that morning's pre-change runs. It is not
a texture refusal - the counters are zero on that run - and it is not
explained. See the record's limits.
