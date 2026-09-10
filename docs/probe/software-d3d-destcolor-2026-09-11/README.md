# DESTCOLOR multiply evidence

Guest reports behind
[the lightmap pass now draws](../../decisions/2026-09-11-the-lightmap-pass-now-draws.md),
which holds the method, the readings and the limits. All from
`Win98SE-Trio64`, 86Box, agent port 9871, boot 349, `Direct3D=2` so every
Direct3D draw is served by the CPU rasterizer.

| File | What it holds |
|---|---|
| `trio64-destcolor.ini` | the probe report: `BlendMultiplyDstRaw=63519`, `BlendMultiplyRaw=0`, `BlendMultiplyOk=1`, and `D3DDevice2HwTriSrcBlend=274` |
| `v9xsnap-destcolor.ini` | the `V9XTRACE` snapshot for the same boot: `D3dBlendSkipped=0`, where the boot before this change read 1 |
| `trio64-bench-destcolor.ini` | `V9XSOFT` on the same boot, against the sampler-fix candidate: every rung within measurement noise |

The baseline for all three is the previous session's boot 346, saved as
`../software-d3d-sysmem-2026-09-10/trio64-blend-skipped.ini` and
`v9xsnap-blend-skipped.ini`.

## Reproduce

```powershell
./scripts/build-ddraw-probe.ps1
./scripts/run-checks.ps1
```

Install `V9XHAL.DLL` in the guest by the `WININIT.INI` rename route, put
`Direct3D=2` in `[Velocity9x]` of the guest's `SYSTEM.INI`, reboot, then run
`V9XDDP.EXE` and read `BlendMultiplyRaw`. Zero - black - is a pass. 63519 is
the magenta fill and means the draw was skipped; a green value means it was
drawn opaque. `BlendMultiplyDstRaw` has to read 63519 for any of that to mean
anything, because it is what says the fill landed.

The probe writes `C:\V9XDIAG\V9XDD.INI`. There is a stale `C:\V9XDD.INI` in
this guest's root from an older tool; reading that one costs an hour.
