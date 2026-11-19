# Haswell texture pool, Phase 0: the BIOS's PTEs are uncached, the pool's entries are junk the VBIOS leaves alone, and 32 MiB of fixed pages allocates in 5 ms

Date: 2026-11-19. Machine: the i5-4570, HD 4600 (`8086:0412`), boot 285,
2 GiB RAM, intel-gma `0787c4d3` installed. Plan:
[intel-haswell-system-memory-textures.md](../plans/intel-haswell-system-memory-textures.md),
Phase 0, nothing written to the device. Evidence:
[`../probe/hd4600-texture-pool-phase0-2026-11-19/`](../probe/hd4600-texture-pool-phase0-2026-11-19/).

## The tool

`G3PL.VXD` + `V9XG3PL.EXE` (`tools/diag/gen3_pool_probe*`), the Gen3 and
GM965 Phase 0 probe, taught Gen7: Haswell 0412/041E and Ivy Bridge
0162/0166 are accepted, the GTT is read at GTTMMADR + 2 MiB (`F7A00000`
here), GGC is host 50h with GMS in bits 7:3 counting 32 MiB, and the
pool policy (`v9x_i9xx_pool_size`, unchanged) is given the HAL's 64 MiB
mapping. The first 65,536 entries map the 256 MiB GMADR as on Gen3, so
the same 256 KiB is read. The run allocates 8,192 fixed, zeroed pages,
copies their page table and frees them.

## Results

| | |
|---|---|
| GGC, stolen | `0209h`, 32 MiB; BDSM `DD200000` |
| The plan | pool at GMADR 32 MiB (`02000000h`), 32 MiB, entries `2000h`-`3FFFh`; reason 0 |
| BIOS entries over stolen | `frame \| 001`, e.g. `E1FFE=DF1FE001`: valid, cache bits 3:1 and 11 zero (uncached); the last, `1FFFh`, is `4C3A92CD`, as on 2026-10-20 |
| The pool's entries | random values, `E2000=4525F8CA` on; 4,069 of the 8,192 have bit 0 set (the span's 4,100 valid less the 31 stolen ones shown) |
| Allocation | 8,192 pages in 5 ms (6 ms the second time), frames `151F2000`-`DB7D0000`, none below 16 MiB, 5,989 runs (longest 10 pages), `CopyResult` non-zero; free pages back to the same count after the free |
| Policy check on the frames | `ACCEPTED` both runs |
| After 3DMark 2001 SE's mode sets (1024x768x32 and back) | the span's hash `F4B9A0EE` and valid count unchanged |

## What this settles

- **Decision 2 matches the BIOS:** its own entries over stolen memory are
  uncached; the pool's will be `frame | 1`, the same form.
- **Nothing owns the pool's entries,** and they are junk with the valid bit
  often set, so the map must write all 8,192 and read each back. Gen3's
  "uniform value before" check cannot apply here; the driver's map needs a
  Gen7 rule of its own (Phase 1).
- **Mode sets leave them alone,** as on Gen3: no re-apply after a mode
  change.
- **32 MiB of fixed pages is cheap on this machine** and lands well clear
  of stolen memory (BDSM `DD200000`) and below 4 GiB, so PTE bits 11:4
  stay zero.

## Not measured

- Car Chase's texture working set in bytes. The counters say how often
  textures are refused, not how much they total; whether 32 MiB covers
  it is left to Phase 4.
- Allocation at `Device_Init` rather than from a running Windows (the
  Gen3 driver does it at boot; Phase 3 will show it here).
- The tester's 64 MiB-stolen board.
