# HD 4600: a DirectDraw heap beyond 16 MiB

Date: 2026-10-20. Status: approved 2026-10-20 with the four decisions
below, each as proposed. Phase 0 done the same day on the 4570: 16 to 31 MiB
writable, the top megabyte protected, and 1082C0h's base names it
(`../decisions/2026-10-20-haswell-stolen-layout.md`).
Phase 1 done the same day: `v9x_intel_hsw_plan_heap` in `hsw_config.c`,
29 new checks in `tests/host/test_intel_hsw.c`, seen failing against a
stub, then passing. Phases 2 and 3 done the same day on boot 54
(`../decisions/2026-10-20-haswell-heap-to-31-mib.md`). The heap ends at
`1ED0000h`, and 3DMark 2001 SE declined no mip tree for room. The 8
refusals first read as new were V9XDDP's by-design checks. Half-Life
mwd5 ran clean (79.3 fps best of three). The DOS box gate is dropped:
the full-screen DOS box is excluded on this chip, as on the netbook. The Gen3 build ran unchanged on a tester's GMA 900 (TOMMO33).

## Why

On every HD 4600 the DirectDraw heap ends at 16 MiB. 3DMark 2001 SE on
the i5-4570 declined 7,038 mip trees for room, and a tester's board
(report V9X-D9VZS6) declined 10,851. Its VBE 4F00h figure is 64 MiB
([issue](../issues/2026-10-20-haswell-3dmark2001se-artifacts-field.md)).
At 1024x768x32, three buffers and a 32-bit Z buffer take about 12 MiB,
leaving about 4 MiB for textures. The 32-bit texture item in
[STATUS.md](../STATUS.md) doubles every texture, so it waits for this
change.

## What sets 16 MiB today

- **The mapping.** `intel_hw16.c` sets `map_pages_hi:lo` to
  `00FF:FFFF`, so `V9xMapAperture` (`runtime.asm`) maps 16 MiB of GMADR.
  This is a family-wide field in `V9X_HW16_OPS`, stamped into DGROUP at
  LibMain (`ddi.c`), so the 945GSE and 915GM get the same value.
- **The clamp.** `v9x_vram_usable_bytes` (`enable16.c`) clamps the
  claimed size to the mapping. On the 4570, VBE claims 33,488,896 bytes
  (`V9XMODES.INI`), so the clamp gives 16 MiB.
- **The single heap.** `dd16.c` publishes one linear heap, from the end
  of the visible bytes to `vram_bytes - 1`. The shared block holds one
  `V9X_VIDMEM` (`win9x_ddraw_abi.h`, `heaps[1]`).
- **The reserve.** The engine's 128 KB reserve (rings, status page,
  state, batch, vertices) is at aperture `1800000h`
  (`V9X_INTEL_HSW_RESERVE_OFFSET` in `intel_hsw.h`, and
  `V9X_HSW_RESERVE_OFFSET` in `loader.asm`). The mini-VDD reaches it
  through its own mapping, not the heap's. The HAL checks `BCS_START`
  against the constant (`eng_hsw.c`) and builds GPU addresses from it
  (`d3d_hsw.c`, `V9X_D3D_HSW_GFX`). The heap stops below the reserve
  only because it stops at 16 MiB.
- **The probe region.** `V9XHSWMM.EXE`'s render probe uses `1820000h`
  onward (`hsw_mmio_probe_win32.c`).

Two measured limits on the 4570
([B1 record](../decisions/2026-10-17-haswell-phase-b1-bcs-ring.md)):

- Stolen memory is 32 MiB (BDSM `DD200000`, TOLUD `DF200000`).
- The top megabyte (`1F00000h` onward) did not read back a CPU write
  through the aperture.

VBE's figure is 64 KiB short of 32 MiB, so it includes that megabyte.
VBE's figure therefore cannot set the heap's top without a correction.

GTT entries 0-4095 and 6144-6161 were read and are linear from BDSM.
Entries 4096-6143 and 6162-8191 have not been read.

## Proposed design

1. **Map per chip, not per family.** Add `map_bytes` to
   `V9X_HW16_DEVICE`, where 0 means the family's `map_pages` as today.
   `V9xHardwareEnable` would apply it after the PCI scan and before
   `V9xMapAperture`. The HD 4600 sets 64 MiB. Gen3 keeps 16 MiB, so the
   945GSE and 915GM do not change. This adds a field to a shared 16-bit
   struct; every other family leaves it 0.
2. **Move the reserve to the top of usable stolen memory.** The heap
   then runs as one block from the visible bytes up to the reserve. The
   layout would be:

   | From | To | What |
   |---|---|---|
   | 0 | visible bytes | the desktop |
   | visible bytes | `reserve` | the DirectDraw heap |
   | `reserve` | `reserve + 20000h` | engine reserve, as today |
   | `reserve + 20000h` | `usable_top` | the probe region |
   | `usable_top` | stolen top | not CPU-writable (1 MiB on the 4570) |

   `usable_top` = min(stolen bytes − protected top, mapped bytes, VBE
   figure), rounded down to 64 KB. `reserve` = `usable_top` − the probe
   region (64 KB; the render probe uses 60 KB) − 128 KB. This is pure arithmetic, so it goes in
   `hsw_config.c` with a host test.
3. **Hand the reserve offset down.**
   - The 16-bit side passes it to `HSW_ENGINE_OPEN`, as a fourth input
     beside BAR0, GMADR and BDSM. The mini-VDD checks those PTEs and
     opens both rings there.
   - The HAL takes it from the engine descriptor, replacing the
     constant. The `eng_hsw.c` check becomes "`BCS_START` equals the
     descriptor's offset". `V9X_D3D_HSW_GFX` adds the descriptor's
     offset.
   - Haswell's `reserve_video_memory` hook (`intel_hw16.c`) returns
     `reserve`, as Gen3's hook already returns its heap minus the
     sandbox.
4. **The probe follows the reserve.** `V9XHSWMM.EXE` reads `BCS_START`
   and places its region 128 KB after it. When the ring is off it
   refuses rather than guess, because a fixed `1820000h` would now be
   inside the heap.

Expected heap end: `1ED0000h` on the 4570, about 30.8 MiB (16 MiB today).
The tester's board would get `3ED0000h`, about 62.8 MiB, if its protected top is also 1 MiB. That
has not been measured.

### Rejected: two heaps around a fixed reserve

Keeping `1800000h` and publishing a second heap above it would leave the
reserve code alone. It costs:

- a shared-ABI change (`heaps[1]` to `heaps[2]`, plus the 16-bit copy in
  `dd16.c`);
- a split heap: on the 4570 no allocation could exceed 24 MiB minus
  the visible bytes, and the upper heap would be about 6.9 MiB;
- the same top-of-stolen correction anyway.

## Decisions

Taken 2026-10-20 by Michael: 1, 2 and 3 as proposed; 4 as proposed,
with Gen3's dynamic memory parked for this release, not dropped.

On 1, the 3dfx precedents were checked first. Neither puts an engine
reserve at the top, but both publish one contiguous heap and keep
engine-owned memory at one end of it:

- Velocity9x's tdfx family puts Glide's region at the bottom and the
  desktop and heap above it. The split is computed by a host-tested
  function from a size read from the chip (`v9x_tdfx_vsa100_desktop_offset`).
- VelocityNT's VSA-100 heap runs from the end of the primary to the end
  of memory (`src/chipsets/3dfx/vsa100/vram.c`).

The top is Haswell's end because its desktop is fixed at offset 0 by
the VBE mode set.

1. **Layout:** move the reserve to the top (proposed), or two heaps
   around a fixed reserve.
2. **Mapping size:** a fixed 64 MiB on the HD 4600 (proposed, and what
   every non-Intel family maps), or stolen-sized (Haswell's GMS goes to
   512 MiB). A larger mapping costs linear address space and VMM page
   tables in the shared arena, and no machine we know of has more than
   64 MiB stolen.
3. **The protected top:** read it from the chip if Phase 0 finds the
   register that reports it (proposed). Otherwise subtract a fixed 1 MiB,
   as measured on one machine. With the fixed rule, a board that reserves
   more than 1 MiB would get heap pages the CPU cannot write.
4. **Gen3:** leave it out of this plan (proposed). Its limit is 8 MiB of
   stolen memory, not the mapping, and
   [its own plan](intel-gen3-system-memory-textures.md) covers that.

## Phases

### Phase 0: measure, read-only, on the i5-4570

No driver change. One run of `V9XHSWMM.EXE`, extended with a read-only
`/stolen` switch, from the desktop:

- Read every GTT entry covering stolen memory (8,192 on the 4570), and
  record any entry that is invalid or not BDSM + offset, plus the first
  entry past stolen.
- Read the register reported to give Haswell's protected range at the
  top of stolen memory. The Linux i915 driver reads a "stolen reserved"
  register in the MCHBAR mirror at MMIO `1082C0h` on Gen7. This is from
  the i915 source, not a datasheet, and has not been read on this
  machine. Record its raw value and whether it names `DF100000`, 1 MiB.
- Read GGC (config `50h`), so the stolen size is decoded
  (`v9x_intel_hsw_decode_ggc`) rather than taken from VBE.
- Write a guard pattern to one dword in each MiB from 16 to 31 through
  the aperture and read it back. Restore the old value. This range is
  outside today's heap and reserve, apart from the reserve's 128 KB,
  which is skipped.

Record: `docs/decisions/2026-10-2x-haswell-stolen-layout.md`, with the
INI, and which of the hypotheses above survived.

### Phase 1: the arithmetic, host-tested

`v9x_intel_hsw_plan_heap(inputs, *plan)` in `hsw_config.c`. The inputs
are stolen, protected, mapped, VBE and visible bytes. It returns
`usable_top`, the reserve and probe offsets and `heap_end`, or a refusal. It refuses when
the visible bytes leave no room for the reserve, and when an input is
0. Tests are written first in `tests/host/`, and seen failing against a
stub:

- the 4570's figures;
- 64 MiB stolen;
- a protected top larger than 1 MiB;
- a mapping smaller than stolen;
- a desktop too large to leave room.

### Phase 2: the driver

- `V9X_HW16_DEVICE.map_bytes`, applied in `V9xHardwareEnable`.
- The HD 4600 device sets 64 MiB.
- The Haswell `reserve_video_memory` path returns the plan's `heap_end`.
- `HSW_ENGINE_OPEN` takes the reserve offset; `loader.asm` drops
  `V9X_HSW_RESERVE_OFFSET`.
- The engine descriptor carries the offset to the HAL; `intel_hsw.h`
  drops `V9X_INTEL_HSW_RESERVE_OFFSET` and `V9X_INTEL_HSW_STATUS_OFFSET`.
- `V9XHSWMM.EXE` places its region from `BCS_START`.
- `V9XHW.INI` publishes `HeapEnd=`, `ReserveOffset=` and
  `ProtectedTop=`, so a field report shows the layout.

Gates: `check-tree.ps1`, `build-host.ps1`, then `run-checks.ps1`.

### Phase 3: on the i5-4570

1. Boot. `V9XBOOT.INI` shows `vram=` at the planned heap end, and
   `EngineStatusEnable` and `EngineStamp` are as before.
2. V9XDDP's checks read as on 0.21.0.
3. 3DMark 2001 SE at its default settings (1024x768x32, Z24, 16-bit
   textures): no mip tree declined for room (`PlaceDeclined` reason 4 at
   0), and no new refusals. Scores are recorded only.
4. Half-Life mwd5 timedemo runs as on 0.21.0. This exercises Disable,
   re-Enable and the reserve's reopen at a new offset.
5. A DOS box round trip and a mode change: the reserve reopens at the
   same offset. It depends on stolen memory, not on the mode, and only
   the heap's start moves. (Dropped 2026-10-20: the full-screen DOS box
   has been excluded on the HD 4600 since Phase B2, as on the netbook,
   so it cannot gate this change. See the netbook's
   [issue](../issues/2026-09-12-netbook-dos-box-return-hardlock.md) and
   [plan](dos-box-vdd-virtualization.md). The mode change ran.)

The 945GSE (netbook) boots once with the new build: its `vram=` and
`EngineStamp` are unchanged.

## Risks

- **Memory above 24 MiB.** If any of it is not usable, the heap would
  hand DirectDraw a surface the CPU or the GPU cannot reach. Phase 0
  exists to find this before the driver changes.
- **The tester's board is not measured.** The design reads its layout
  from the chip, not from the 4570's numbers. The `V9XHW.INI` keys let
  the next field report confirm it.
- **Statics.** Moving the reserve per boot adds nothing per-process.
  The offset is the same for every process, as the constant was
  ([V9XHAL statics are global](../issues/2026-10-06-hal-statics-shared-across-processes.md)).
