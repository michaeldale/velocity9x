# Write-combining the HD 4600's aperture through the page attribute table

Date: 2026-10-28

Status: implemented for the Haswell and Ivy Bridge devices (`hsw_hw16.c`:
HD 4600 0412, HD 4400 041E, HD 4000 0162) and measured on the HD 4600
only: W9QI5A83, Dell i5-4570, Win98 SE, boots 105 (control) and 106 (WC).
The HD 4400 and HD 4000 take it by the same-design rule and have not run it.
Evidence: [`../probe/haswell-pat-write-combining-2026-10-28/`](../probe/haswell-pat-write-combining-2026-10-28/).
Gates: `build-host` (with `tests\host\test_pat.c`; breaking the PTE edit
failed two of its checks), `run-checks`.

## Why

The 2026-10-27 review of the HD 4600 put its 3DMark 2000 deficit in the
batch-bound tests and, inside a batch, in one cost: the HAL's vertex copy
into the uncached engine reserve, 60 us of a 112-162 us batch, at the
66 MB/s the uncached aperture takes stores
([intel-haswell-batch-cost.md](../plans/intel-haswell-batch-cost.md),
[2026-10-25-intel-gen3-write-combining.md](2026-10-25-intel-gen3-write-combining.md)).
The Ironlake went from 1246 to 2909 when its aperture was write-combined.
The 4570's BIOS uses all eight MTRR pairs and covers GMADR with an explicit
UC pair (`Mtrr3=e0000000 e0000800`), so the MTRR plan refuses (`r=11`,
ALREADY_COVERED). That record named PAT as the route and Michael held it
until the netbook's numbers; they are in, and on 2026-10-27 he asked for it.

## The change

- **Policy, host-tested** (`include\velocity9x\pat.h`, `src\common\pat.c`):
  taken only when the MTRR plan refused for ALREADY_COVERED or NO_SLOT;
  the CPU must report CPUID, MSRs and the PAT; the range must be whole
  pages at or above 2 GiB (the private arena's PDEs change per process);
  PAT entry 4 must read WB (its power-on value, unused by Windows 98, which
  never sets PTE bit 7) or WC (ours, after a restart without a CPU reset).
  The plan changes entry 4 to WC and nothing else.
- **Ring 0** (`loader.asm`, intel-gma image only, V9XMINI_FN_PAT_INFO and
  PAT_COVER in `V9XMAPI.INC`): reports IA32_PAT; on PAT_COVER re-checks
  the CPU, the request (entry 4 to WC, entries 5-7 unchanged), the range,
  and that entry 4 still reads as planned. It finds the page tables through
  the page directory's entry for itself, located by mapping CR3's page and
  searching it, not assumed at FF800000h, and checks that window against
  `_CopyPageTable` for the first page. Every page's PDE must be present and
  not 4 MiB, every PTE present, before any is edited. Then, interrupts off:
  the PAT write with the SDM's sequence (no-fill cache mode, WBINVD, TLB,
  the write, WBINVD, TLB; vol. 3, 11.12.4 / 11.11.8), read back, PTE bit 7
  set and PCD/PWT cleared on every page, one TLB flush. WRMSR sits in two
  procedures now; `check-tree.ps1` holds both.
- **Where it applies**: enable16.c's MTRR step, on a refusal, calls the
  chip's new `write_combine_fallback` hook with the DPMI mapping (64 MiB
  on the HD 4600). `hsw_hw16.c` plans and covers it, and covers the
  mini-VDD's 256 KiB engine-reserve mapping (ring words and every vertex
  the HAL copies) on the next HSW_ENGINE_OPEN, since on a boot the engine
  opens before the aperture's plan. `C:\V9XDIAG\NOWC.TXT` still turns it
  off (`PatWrite=off-marker`).
- **No new drain**: the Haswell BCS and RCS submits and the flip already
  drain WC buffers before telling the GPU (`eng_hsw.c`, `d3d_hsw.c`,
  `ddhal_core.c`), which the Gen3 record found and left in place.
- The intel-gma driver's `_TEXT` was 59 bytes over its 57,344-byte budget
  with this in it. A third code segment does not come out: wlink packed
  `HSWCODE` into `I9XXCODE`'s physical segment. So `v9x_pat_pte_wc` is
  host-only (the mini-VDD does the edit) and the reserve reuses the
  aperture's plan instead of making its own.

## Boots

Deployed by WININIT rename (DRV, VxD, HAL, SETP, GL, Glide 2/3, CIF; the
boot 104 set is in this session's scratchpad), every file checked by
SHA-256 after each boot.

| Boot | Marker | Record |
|---|---|---|
| 105 | `NOWC.TXT` present | `MtrrWrite=refused`, `PatWrite=off-marker` |
| 106 | renamed to `NOWC.OLD` by WININIT | `MtrrWrite=refused`, `PatWrite=ok pages=00004000 hi=00070101`, `PatReserve=ok pages=00000040 hi=00070101` |

16,384 pages are the 64 MiB DPMI mapping, 64 the reserve. The high dword
read back is 00070101: entries 7-5 UC, UC-, WC as the BIOS left them and
entry 4 now WC, so this BIOS had already set entry 5 to WC, which the plan
neither needs nor touches.

## What was measured

dispbench f2dd1cd (branch `db3d-hal-split`: DB3D reads the HAL's time
buckets per test). 640x480x16 for DB3D, the 800x600x16 desktop for the
rest. One run each; 3.19 GHz TSC.

| | Control (105) | WC (106) |
|---|---:|---:|
| FbWrite MB/s | 66.0 | **9,238.1** |
| FbRead MB/s | 35.5 | 216.7 |
| Gdi.FillRect.800x600 cyc/px | 97.24 | 0.69 |
| Gdi.BitBltDibToScreen.800x600 cyc/px | 97.17 | 1.47 |
| Gdi.TextOut.FixedOpaque cyc/string | 472,679 | 20,718 |
| Dd.BltFast.SysToVid.Plain.512x512 cyc/px | 48.37 | 0.39 |
| Dd.BltFast.VidToVid.Plain.512x512 cyc/px (engine) | 2.445 | 2.436 |
| DB3D TriBatch.16.V32 cyc/tri | 5,861 | 854 |
| DB3D TriBatch.100.V32 cyc/tri | 5,441 | 637 |
| DB3D BatchCost.Tiny cyc/call | 6,095 | 791 |
| DB3D StateChange.Texture.Change.1 cyc/tri | 68,340 | 43,505 |
| DB3D TexUpload.Lock cyc/byte | 97.15 | **1.23** |
| DB3D FillFlat / FillAlphaBlend cyc/px | 8.07 / 16.37 | 8.04 / 16.31 |

DB3D's HAL buckets, microseconds per batch, control to WC:

| Test | EngineDraw | RingWrite (vertex copy) | RenderDrain | RingWait | Decode |
|---|---|---|---|---|---|
| TriBatch.16.V32 | 115.4 -> 13.5 | 96.1 -> 4.5 | 0.0 -> 6.2 | 3.1 -> 0.6 | 1.3 -> 0.0 |
| TriBatch.100.V32 | 170.6 -> 18.2 | 151.2 -> 7.2 | 0.0 -> 8.3 | 3.1 -> 0.6 | 1.3 -> 0.0 |
| BatchCost.Tiny | 115.2 -> 7.5 | 96.0 -> 4.5 | 0.0 -> 0.0 | 3.1 -> 0.6 | 1.4 -> 0.0 |
| StateChange.Texture.Change.1 | 19.9 -> 11.9 | 0.1 -> 0.1 | 0.0 -> 9.1 | 4.1 -> 0.6 | 1.4 -> 0.0 |

| | Control (105) | WC (106) |
|---|---:|---:|
| V9XD32 /ddi6, /ddi5 | PASS, PASS | PASS, PASS |
| V9XGLP, V9XGLIDP, V9XR3DP | PASS, PASS, COMPLETE | PASS, PASS, COMPLETE |
| 3DMark 2000 default (1024x768x16) | 2344 | **3719** |
| 3DMark batches | 1,496,459 | 3,299,467 |
| us per batch: EngineDraw | 162.0 | 58.1 |
| RingWrite (vertex copy) | 62.1 | 3.5 |
| RenderDrain | 29.7 | 31.3 |
| RingWait / Decode | 3.5 / 1.4 | 0.6 / 0.1 |
| flips handled, declined | 10,025, 0 | 14,206, 0 |

The 3DMark buckets are the difference of two V9XTRACE snapshots around the
run; 3DMark's tests run for fixed times, so the WC run drew more batches.

## Readings

- **CPU stores to the aperture go 140 times faster**, and everything made
  of them follows: solid GDI fills 141x, DIB-to-screen 66x, text 23x,
  system-to-video blits 123x, texture upload through Lock 79x. The
  engine's own work does not move (VidToVid, fills, the clear).
- **The vertex copy stops being the HAL's cost**: 62 to 3.5 us a batch in
  3DMark, 96 to 4.5 in DB3D. The ring wait and decode (both reads and
  writes of the reserve) drop with it.
- **The render engine is now the limit, as the plan predicted**: in 3DMark
  the CPU waits 31 of its 58 us a batch for a free slot. On DB3D's
  one-triangle state-change batches the drain appears (0 -> 9.1 us) for the
  same reason, and BatchCost.Tiny's 64 one-pixel triangles a batch leave
  the GPU idle. Together they bound the GPU's fixed cost a batch at about
  10 us; a 3DMark batch takes it about 58. That is the next lever
  ([intel-haswell-batch-cost.md](../plans/intel-haswell-batch-cost.md)).
- **Reads go 6 times faster** (FbRead 35.5 to 216.7 MB/s), more than the
  netbook's unchanged reads: Haswell's WC reads are uncached but
  streaming. Screen-to-screen GDI copies gain 4x.
- **Correctness**: V9XD32 at DDI 5 and 6 passes on both boots, the GL,
  Glide and 3DCIF probes pass, dispbench verifies every timed row on both,
  3DMark draws through, no flip declined.

## What the evidence disputes or leaves open

- **DBGDI died once on the WC boot** after its last test, with every
  section written and no `[Result]`; the rerun on the same boot completed.
  The netbook's DB3D died the same way on its uncached control boot and
  the Rage 128's on its WC boot, so a teardown death that WC alone does
  not explain. Cause not known.
- **The marker was cleared by a WININIT rename** to `NOWC.OLD`, not a
  delete.
- **Aliases**: the two typed mappings overlap (the reserve lies inside the
  64 MiB DPMI mapping) and both are now WC, so no UC/WC alias of the
  reserve exists. Any other mapping of GMADR (none known: the mini-VDD's
  MMIO and GTT windows are BAR0) would stay UC beside a WC one, which the
  SDM leaves undefined.
- **A remapped aperture** (a new DPMI mapping after Disable/Enable) is
  typed again by the next MTRR step only if the trace runs again; it runs
  once a boot. Not exercised: the mapping is reused across Enables today.
- **One machine, one boot each**, one 3DMark run each. The HD 4400 and
  HD 4000 have not run it; the HD 4000 board's IGD config read FFFFFFFF on
  2026-10-22 and has not been checked since.
- **No operator watched for corruption.** Correctness rests on the probes'
  read-backs, dispbench's verified rows and a 3DMark run that completed.
