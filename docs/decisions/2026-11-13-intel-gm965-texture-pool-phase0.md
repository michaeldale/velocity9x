# Intel GM965 texture pool, Phase 0: 32 MiB allocates in one run, 64 MiB of GMADR maps, the pool's entries hold the scratch page

Date: 2026-11-13
Machine: TOMMOHP, `M-KYHT4Q` (Tom Rossell's HP laptop, GM965 GMA X3100
`8086:2A02` rev 0C, Core 2 Duo T7250, 2 GiB RAM, all of it seen by
Windows), through remote help; boots 49 and 50.
Plan: `docs/plans/intel-gen3-system-memory-textures.md`, Phase 0, run on
the first Gen4 chip. The Gen3 record it repeats:
`2026-11-12-intel-gen3-texture-pool-phase0.md`.
Evidence: `docs/probe/gm965-texture-pool-phase0-2026-11-13/`.

Nothing was written to the GTT. The pool is still gated to the netbook's
945GSE: boot 50's `INTELGTT.TXT` says `[IntelPool] Result=not-gated-device`.

## Why this machine

Its BIOS steals 8 MiB, the most its GMS field offers (`GGC` 0030h), the
same limit the netbook's pool was built against, with 2 GiB of RAM behind
it. The Gen3 decisions record authorises the pool on the netbook only;
this record is what a GM965 decision would be taken from.

## The tools

- **`G3PL.VXD` + `V9XG3PL.EXE`** (`tools/diag/gen3_pool_probe*.{asm,c}`),
  extended for the GM965. It has no GTTADR BAR: config 1Ch is GMADR's high
  dword, and the GTT is GTTMMADR (10h) + 512 KiB, as
  `V9xPciReadIntelGttConfig` reads it (datasheet 316273 21.1.10). For a
  256 MiB GMADR that GTT is the same 65,536 four-byte entries in 256 KiB,
  so the VxD's read and bounds are unchanged; the publisher passes that
  address as the base (`GttFrom=GTTMMADR+512K`) and the VxD's device list
  gains `2A02`. The policy is the driver's own (`src/common/i9xx_pool.c`).
- **The driver's own GTT inventory** (`INTELGTT.TXT`/`.BIN`, written at
  every boot), read back from boot 49 and boot 50.
- **A measurement DRV, not committed**: the GM965's device row
  (`gma950_hw16.c`) given the GMA 950 row's `0x04000000` map size. Built
  as `df486bef-dirty`, all eight intel-gma files installed by
  `run-release-checks.ps1` with a one-machine list (WININIT renames, 8 of 8
  hashes match), `reset`, boot 50; ESC on the network-password box.

## Findings

**1. `_PageAllocate` gives 8,192 fixed pages in 28 ms, physically
contiguous.** Two runs on boot 49:

| | Run 1 | Run 2 |
|---|---|---|
| free pages before / with the pool / after freeing | `6B89A` / `6987E` / `6B886` | `6B886` / `6987E` / `6B886` |
| allocation | 28 ms | 28 ms |
| frames | `14BAD000`-`16BAC000`, 1 run of 8,192 | the same frames |
| below 16 MiB / not present | 0 / 0 | 0 / 0 |
| `v9x_i9xx_pool_check` | accepted | accepted |

Run 1 left 20 pages fewer free than it found; run 2 gave back every page.
The 20 are not explained; loading the VxD the first time is the guess, not
measured. The netbook's 32 MiB came in 152 runs; here it is one, so on
this machine the pool's GTT entries would be a single linear run. Like the
netbook's, this was a dynamic VxD after boot, not `Device_Init`.

**2. The pool's entries all map the scratch page, and the GTT reads the
same after a boot that maps 64 MiB.** The policy places the pool at GMADR
8 MiB (entries `800`-`27FF`), as on the netbook. The VxD's span `7E0`-`27FF`
reads `7FF7E001` in every entry (valid, all equal, hash `90EB6205` in both
runs). The driver's inventory: entries `0`-`77E` map stolen memory linearly
from `7F800000`, `77F`-`FFFE` the scratch page `7FF7E000`, `FFFF` the page
after it; all 65,536 present and uncached; hash `15FE7A05` on boot 49 and
on boot 50. The GTT itself is stored at `7FF80000` (`PGTBL_CTL`
`7FF80001`), the top 512 KiB of stolen memory, which is why VBE reports
7.4 MiB of the 8. As on the netbook, a VBIOS that rewrote these entries
would leave them looking the same; whether a mode set touches the pool's
own PTEs is a Phase 2 question.

**3. The DPMI host maps 64 MiB of GMADR here too.** Boot 50:
`DpmiMap=04000000:ok`, `Stage=enable-ok`, heap unchanged
(`VddReserve ... vram=7798784`). The release probes pass on it: V9XD32
default, V9XGLP, V9XGLIDP (0 failures), V9XR3DP, V9XWPRS and V9XFBCMP.
No MTRR was written (`Mtrr=... r=0`): the GM965 row does not ask for
write-combining, so the aperture is uncached on this chip today, with or
without the pool. Not looked into here.

**4. Not repeated: the runtime's texture-only heap.** Findings 4 and 5 of
the Gen3 record are about DirectDraw's allocator on 98 SE, not the chip;
they hold for any driver that publishes the heap the same way.

## What this means for a GM965 pool

The policy's sizing and check accept this machine as they stand. What the
Gen3 code does not yet do for the GM965, and Phase 2/3 would:

- the mini-VDD's pool functions, the DRV's `intel_pool16.c` and its gate
  take the GTT from BAR3; the GM965's is GTTMMADR + 512 KiB;
- Gen4 PTEs carry physical address bits 35:32 in bits 7:4; every frame
  here is below 4 GiB, so the entries would read as on Gen3, but the
  writer should check rather than assume;
- the Gen4 HAL's texture binding has never sampled pool memory;
- an errata gate for the GM965, as the Gen3 decisions record has for the
  945 (this chip hung on alpha test with depth writes before 0.48.0 drew
  them in two passes).

## Not measured

- Allocation at `Device_Init`.
- Whether a mode set rewrites PTEs above the stolen run.
- Write-combining on the GM965's aperture (finding 3).

## State left behind

TOMMOHP runs the measurement build (`df486bef-dirty`, 64 MiB map), boot
50. The probes pass on it; the next intel-gma install there replaces it.
`G3PL.VXD` and `V9XG3PL.EXE` are in `C:\V9XREMOTE\JOBS\G3PL`. The source
change was not committed.
