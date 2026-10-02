# DirectDraw refuses the HAL on any configuration without the D3D capability

Status: Resolved, 2026-10-02, commit `fc1ed35`, verified on A8U4I5 boot 131.

Priority was high. On a chip or setting without the D3D capability,
`DDHAL_SetInfo` returned FALSE, so DirectDraw ran its own emulation with
no video-memory heap. The cause has shipped in every release since 0.7.0.

Date: 2026-10-02. Machine: A8U4I5, ATI 3D Rage IIC AGP `1002:4757`, ati
family tier-0, 1024x768x16, boots 126-131.
Evidence: [`../probe/a8u4i5-rage-iic-registers-2026-10-02/`](../probe/a8u4i5-rage-iic-registers-2026-10-02/)
(`BOOT126-*`, `BOOT127-d3dsoft-*`, `BOOT128-fix-*`, `BOOT130-*`,
`BOOT131-fix2-*`).

## Symptom

| | Boot 126, `Direct3D=0` (resolves to none) | Boot 127, `Direct3D=2` (software) |
|---|---|---|
| `V9XDDH.INI` | `Stage=setinfo-fail`, `LastGoodStage=newcallbackfns` | `Stage=setinfo-ok` |
| `GblNoHardware` | 1 | 0 |
| `GblNumHeaps`, `GblHalVidMemTotal` | 0, 0 | 1, `00280000` |
| `VideoStageHr` (video-memory surface) | `88760233` DDERR_OUTOFVIDEOMEMORY | - |

Software Direct3D sets the D3D capability, and that skips the block in
`V9xDdCreateDriverObject` (`src\display16\dd16.c`) that narrows the
`DDHALINFO` copy handed to `SetInfo`. The narrowing was therefore the
suspect. Mode sets, flips, fills and blits still passed in boot 126,
through the runtime's emulation.

## A hypothesis that did not survive

`DriverInit` sets `DDHALINFO_GETDRIVERINFOSET` for every family, since
`e33c6d6`, and the narrowing nulled `GetDriverInfo` without clearing it.
Commit `76fd810` cleared the flag. Deployed at boot 128 it changed
nothing: `setinfo-fail` again, with the new driver confirmed loaded
(`V9XMODES.INI` build `46e2b3c-dirty`, 46,456 bytes). Zeroing
`dwZBufferBitDepths`, which `a87677b` had made inconsistent with the
narrowed `ddsCaps`, made no difference either (mask `3F` below).

## Bisect

A temporary 16-bit driver, never committed, applied each narrowing step
under a bit in `C:\V9XDIAG\V9XNARR.INI`. `SetInfo` runs when each
DirectDraw program creates its driver object, so each mask cost one
`V9XDDP` run and no reboot (boot 130):

| Bit | Narrowing step |
|---|---|
| `01` | clear `GETDRIVERINFOSET`, null `GetDriverInfo` |
| `02` | null the D3D global data, D3D callbacks and execute-buffer callbacks |
| `04` | `ddCaps.dwCaps` = GDI, BLT, BLTCOLORFILL |
| `08` | `ddCaps.ddsCaps` = offscreen, flip, primary, complex |
| `10` | rewrite `surface_callbacks.dwFlags` to a fixed list |
| `20` | zero `dwZBufferBitDepths` (new) |

| Mask | `SetInfo` | Heaps | `VideoStageHr` | D3D HAL |
|---|---|---|---|---|
| `1F` (as shipped) | fail | 0 | `88760233` | 0 |
| `3F` | fail | 0 | `88760233` | 0 |
| `00` (no narrowing) | ok | 1 | 0 | 1 |
| `1E`, `1D`, `1B`, `17` (one step each left out) | fail | 0 | `88760233` | 0 |
| `0F` (all but the callback flags) | **ok** | 1 | 0 | 0 |

(`BOOT130-NARROW-BISECT.TXT` and the per-mask INIs.)

## Cause

The fixed list in step `10` was the surface-callback set of August 2026.
`ea25f58` (2026-09-03, in v0.7.0 onward) later added `SETCOLORKEY`, with
its `SetColorKey` pointer, to the shared set in `ddhal_core.c`. The
rewrite therefore dropped exactly that one flag and left the pointer
standing. `SetInfo` returns FALSE for that description. Why DDRAW16
rejects it, whether a pointer without its flag or a colour-key callback
without `DDCAPS_COLORKEY`, was not isolated.

The 2026-08-14 Trio64 issue
([DirectDraw accepts SetInfo but reports DDCAPS_NOHARDWARE](2026-08-14-directdraw-hal-nohardware.md))
is a different failure: there `SetInfo` returned TRUE and the runtime
discarded the HAL afterwards.

## Fix

`fc1ed35`: the narrowed branch no longer touches the surface callbacks.
None of them is a Direct3D entry point. The other narrowing steps stay,
including `76fd810`'s flag clearing, so the shipped description is mask
`0F`, the one measured.

## Verification, boot 131

Build `76fd810-dirty`, which is `fc1ed35`'s content: `V9XDISP.DRV`,
46,446 bytes, installed by a single WININIT rename. `Direct3D=0`.

| | Result |
|---|---|
| `V9XDDH.INI` | `setinfo-ok` |
| `GblNoHardware`, heaps | 0, one heap of `00280000` |
| `ReportedCaps` | `0x04000440` (GDI, BLT, BLTCOLORFILL), the value the 2026-08-14 Trio64 fix verified |
| Video-memory surface, source copy, four overlapping copies | all `S_OK`, pixels correct |
| 20 flips | 0 ms (505 ms under the emulation) |
| `D3DHalFound` | 0, as it should be for this chip |
| `V9XGDI /auto` | PASS |
| `V9XMSW /cycle:10`, `/depth:10` | PASS, PASS |
| `V9XDDP /modestress` | 32 of 32 |

`FlipPixelOk=0` is expected once real flips run: GDI reads the fixed
GDI page (2026-08-14 issue, Verification).

## Not verified

The other configurations this affected were not run on the fix: ati VT2,
Trio32/64 without software Direct3D, and any family at `Direct3D=1`. The
code path is the same. A stale `C:\V9XDIAG\V9XNARR.INI` remains on
A8U4I5; the shipped driver does not read it.
