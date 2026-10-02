# DirectDraw refuses the HAL on any configuration without the D3D capability

Priority: high. On the measured machine, a chip or setting without the D3D
capability gets no DirectDraw HAL: `SetInfo` fails, and DirectDraw runs on
its software emulation with no video-memory heap. On the reading below,
every release since 0.8.0 carries the cause.

Date: 2026-10-02. Machine: A8U4I5, ATI 3D Rage IIC AGP `1002:4757`, ati
family tier-0 (commit `397da71`), 1024x768x16, boots 126 and 127.
Evidence: [`../probe/a8u4i5-rage-iic-registers-2026-10-02/`](../probe/a8u4i5-rage-iic-registers-2026-10-02/)
(`BOOT126-V9XDDH.INI`, `BOOT126-ddp-V9XDD.INI`, `BOOT127-d3dsoft-*`).

## Measured

| | Boot 126, `Direct3D=0` (resolves to none) | Boot 127, `Direct3D=2` (software) |
|---|---|---|
| `V9XDDH.INI` | `Stage=setinfo-fail`, `LastGoodStage=newcallbackfns` | `Stage=setinfo-ok` |
| `GblNoHardware` | 1 | 0 |
| `GblNumHeaps`, `GblHalVidMemTotal` | 0, 0 | 1, `00280000` |
| `VideoStageHr` (video-memory surface) | `88760233` DDERR_OUTOFVIDEOMEMORY | - |
| `D3DHalFound` | 0 | 1 |

The only difference between the two boots is the one `SYSTEM.INI` value.
It was set by hand and then restored. In boot 126, mode sets, the flip
chain, fills, flips and overlapping blits all still passed, through
DirectDraw's emulation.

## Cause, by reading

`DriverInit` in `src\display32\ddhal_core.c` sets
`DDHALINFO_GETDRIVERINFOSET` in `info.dwFlags` unconditionally. That has
been so since `e33c6d6` (2026-09-20), which is in v0.8.0 through v0.9.2.
`V9xDdCreateDriverObject` in `src\display16\dd16.c` handles a family whose
engine does not claim D3D by narrowing the DGROUP copy before `SetInfo`. It
sets `GetDriverInfo = 0` and clears the D3D pointers, but leaves `dwFlags`
alone. DDRAW16 is then told a GetDriverInfo entry exists and handed none.

That `SetInfo` rejects exactly this combination has not been proven. What
is measured is that the narrowed branch fails and the un-narrowed one
succeeds on the same machine.

## Affected, by the code path

Any boot where `engine_caps` lacks `V9X_DD_ENGINE_CAP_D3D` when DirectDraw
creates its driver object:

- ati `5654` (VT2) and `4757` (Rage IIC), always;
- s3 Trio32/64, unless software Direct3D is on;
- any family with `Direct3D=1` (disabled).

Only the Rage IIC was measured. The vbe family defaults to software
Direct3D at 16 bpp, so it takes the un-narrowed path and is not affected by
default.

## Fix to try

Clear `V9X_DDHALINFO_GETDRIVERINFOSET` from `v9x_dd_info16.dwFlags` in the
narrowed branch. Then verify on A8U4I5 with `Direct3D=0`: `setinfo-ok`, a
heap, and `VideoStageHr` 0.
