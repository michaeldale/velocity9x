# Ivy Bridge 2D on W9QI4E14: intel-gma claims 8086:0162, and DirectDraw blits run on the BCS ring

Date: 2026-10-21
Status: **measured once at each depth.** This is step 4 of
`docs\plans\intel-ivybridge-gen7.md`, the identity, forcewake and blitter
part, at 8, 16 and 32 bpp. The pitch unit is not measured on its own,
and Direct3D and flips are not claimed.

Captures: `docs\probe\w9qi4e14-ivb-hd4000-igma-2026-10-21\`. Package
`build\win98se-intel-gma`, build `0eea67e6-dirty`: the commit that carries
this doc, with these changes uncommitted at the time.

## What changed

- **Identity.** `intel-gma` claims `8086:0162` as `hd4000-ivb`:
  - `V9X_PCI_DEVICE_HD4000_IVB` and the backend's strict list;
  - the registry table and the manifest entry;
  - `v9x_ivb_hd4000_device` in `hsw_hw16.c`, on the Haswell path.
  - The Gen3 gate is now `v9x_intel_is_gen7` and answers for either
    device, so no Gen3 hook runs on Ivy Bridge.
- **Caps.** Engine type `INTEL_GEN75` with `SOLID_FILL | SCREEN_COPY`
  only.
  - No D3D: Gen7's SURFACE_STATE DW7 and 3DSTATE_PS DW4 differ from
    Gen7.5's.
  - No FLIP: linear panning is `PRI_LINOFF`.
  - Direct3D word `not-advertised`. `d3d_select.c` already returns none for
    GEN75 without CAP_D3D, and the HAL flips only with CAP_FLIP.
- **HSW_ENGINE_OPEN takes the chip in EDI.**
  - `V9xMiniHswEngineOpen` gains a `chip` argument (`retf 32`).
    `V9XMAPI.INC` names `V9X_HSW_CHIP_HASWELL` 0 and
    `V9X_HSW_CHIP_IVYBRIDGE` 1, and any other value is refusal 1.
  - For Ivy Bridge the open follows i915 and the GT probe
    (`2026-10-21-ivybridge-gt-forcewake.md`): acknowledge `130040h`;
    `FORCEWAKE` cleared and posted through ECOBUS; `A188h` = `FFFF0000`;
    the acknowledge awaited clear (refusal 5 only if it never clears);
    bit 0 set and awaited; thread status awaited C0, bounded and not a
    refusal.
  - The release clears, waits, and on Ivy Bridge sets bit 0 again when
    the open found it set. That returns the GT to the BIOS's hand-off
    state for the VBIOS and DOS boxes.
  - Haswell's sequence is unchanged: acknowledge `130044h`, refusal 5 on
    one already held, and no restore.
- **Heap layout.** The heap layout is Haswell's. i915 decodes `1082C0h`
  the same way on Gen7 and Gen7.5.

## Boot 6 (Have Disk over tier-0 `vbe`, then `reset`)

The install offered "Velocity9x Intel HD Graphics 4000 (Ivy Bridge)" by
hardware ID. `WININIT.INI` held three plain renames (the two Glide DLLs
and the in-use `v9xdisp.drv`). The agent's `reset` returned: boot 5 to 6,
desktop ready at 640x480x8 (`boot6-desktop.png`).

`boot6-V9XHW.INI`:

| Key | Value |
|---|---|
| Adapter / DeviceId | Intel HD Graphics 4000 (Ivy Bridge) / 0162 |
| Acceleration / Direct3D | directdraw-fill-blt / not-advertised |
| EngineStatusEnable | `open-reused` (the descriptor ran twice; the second call found the open ring) |
| EngineCaps | `00000003` |
| HeapLayout | `chip` |
| StolenBytes | `04000000` (64 MiB) |
| Reg1082C0 | `DF900007` → ProtectedBytes `00100000` (1 MiB) |
| ReserveOffset / HeapEnd | `03ED0000` / `03ED0000` |

`boot6-V9XBOOT.INI`: `Stage=enable-ok`, aperture mapped from BAR2
(`Aperture=m=5 bar=2 b=e0000000`), VBE 3.0 `mem=1023`.

The open's own checks passed at the reserve, aperture `3ED0000h`:
- all 32 PTEs map BDSM + offset;
- a CPU write through the aperture reads back.

So the BIOS's linear stolen mapping reaches there, consistent with the
read-only GTT copy, which mapped stolen through entry `3FFE`.

## DirectDraw on the BCS ring

`V9XDDF.EXE` at 640x480x8 (`boot6-V9XDDF.TXT`) passed all seven stages:
create, cooperative level, an off-screen surface, GetBltStatus, a colour
fill read back, then a rectangle fill and a copy read back against a CPU
reference. The fill and the copy read back with 0 mismatches.

The HAL's snapshot after it (`boot6-after-ddf-V9XSNAP.INI`, from
`V9XTRACE.EXE`):

- `EngineStamp=type=00000009 caps=00000003 ring=E5B34000`: GEN75, the
  two 2D caps, the reserve mapped.
- `CountBlt=3`, `CountBltEngine=3`: **all three blits executed on the
  engine**, none on the CPU.
- No engine FIFO, idle or reset timeouts.

So `XY_COLOR_BLT`, `XY_SRC_COPY_BLT` and the `MI_STORE_DATA_IMM`
sequence run unchanged on Gen7's BCS ring. The machine stayed up.

## 16 and 32 bpp, by a depth change without a restart

Display Properties: colours to High Color, then True Color, each applied
with "Apply the new color settings without restarting". Windows disables
and re-enables the driver for that, which runs the Disable hook
(`v9x_hsw_engine_close`) and then a fresh descriptor.

| Depth | `V9XDDF` | Surface pitch | Fill pixel | Copy pixel | Mismatches | CountBltEngine / CountBlt |
|---|---|---|---|---|---|---|
| 8 | PASS | 64 | `5A` | `A5` | 0 / 0 | 3 / 3 |
| 16 | PASS | 128 | `07E0` | `F81F` | 0 / 0 | 3 / 3 |
| 32 | PASS | 256 | `0012A5C3` | `00ED5A3C` | 0 / 0 | 3 / 3 |

Files: `boot6-16bpp-*` and `boot6-32bpp-*`. The agent inventory reported
16 and then 32 bpp, and the desktop draws at 32 bpp
(`boot6-32bpp-desktop.png`). `EngineCaps=3` after each change, with no
engine timeouts or resets.

So the blitter's three depth encodings and byte pitches hold on Gen7.
The machine was left at 640x480x32.

What this does not show: `EngineStatusEnable` read `open-reused` after
each change, the last of the descriptor's calls. Whether the Disable
hook's close ran in between, or the reuse path found the ring still open,
is not distinguishable from these files.

## Not measured, or not done

- An overlapping or larger copy. The pitch unit is exercised only at
  64x16 surfaces.
- Whether the engine close runs on Disable (above), and a DOS box.
- GMS `11h` (1 GiB stolen), which the decode still refuses. This board is
  at 64 MiB.
- The IVB PTE encoder: nothing in the 2D path writes a PTE.
- Flips and Direct3D: steps 5 and 6.
