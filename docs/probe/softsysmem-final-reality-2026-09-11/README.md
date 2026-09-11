# D3DSoftSysMem against Final Reality

Evidence behind
[D3DSoftSysMem buys Final Reality nothing](../../decisions/2026-09-11-d3dsoftsysmem-buys-final-reality-nothing.md).
All from `Win98SE-Fast-D3D`, agent port 9878: Celeron 533, Voodoo3 3500 AGP
on the `vbe` package, `Direct3D=2`, Robots only with every other test cleared
and the five-times repeat off.

| File | The run it came from |
|---|---|
| `fr-robots-off-boot587-0.67.png` | setting off, first run after that boot - the outlier |
| `fr-robots-on-boot588-0.53.png` | setting on |
| `fr-robots-on-boot588-0.54.png` | setting on, second run in the same boot |
| `fr-robots-off-boot589-0.53.png` | setting off again, and the reading that killed the apparent regression |
| `v9xsnap-sysmem-off-boot587.ini` | the counters that matter: 349 textures, every refusal counter zero **with the option off** |
| `v9xsnap-sysmem-on-boot588.ini` | the same with the option on: `EngineCaps=0x00000130` |
| `v9xhw-sysmem-allowed.ini` | `D3DSoftSysMem=allowed`, `ModeSwitching=vbe-lfb` |
| `trio-probe-sysmem-on-boot588.ini` | the probe on that boot: `SysMemTexRaw=2016`, so the engine does sample one |

The off-boot counter file is the load-bearing one. A system-memory texture
offered while the option is off is refused and counted; none was, across a
whole run of the scene, so DirectDraw never chose that placement for Final
Reality and the option had nothing to act on.

## Reproduce

Put `D3DSoftSysMem=1` under `[Velocity9x]` in the guest's `SYSTEM.INI`,
reboot, and check `C:\V9XDIAG\V9XHW.INI` says `allowed` before believing a
run. Then FR, Advanced Options, `Clear all tests`, tick `Robots`, untick
`Run all tests 5 times`, and read the 3D tests page.

**Run each setting at least twice, and discard the first run after a boot.**
One run either side of the switch reads as a 21% regression that does not
exist.
