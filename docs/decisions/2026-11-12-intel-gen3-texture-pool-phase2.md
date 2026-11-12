# Intel Gen3 texture pool, Phase 2: the pool maps, the aperture reaches its pages, and a mode set leaves the PTEs alone

Date: 2026-11-12
Machine: MICHAEL-NETBOOK (945GSE `8086:27AE` rev 03), boot 317, running
the Phase 0 measurement build (`6b3e1bd6-dirty`).
Plan: `docs/plans/intel-gen3-system-memory-textures.md`, Phase 2.
Gate: `2026-11-12-intel-gen3-texture-pool-decisions.md` (Phase 2 on this
machine only).
Evidence: `docs/probe/gen3-texture-pool-phase2-2026-11-12/`.

The first GTT writes this project has made above the stolen run, and the
first CPU traffic through the aperture to pages that are not stolen
memory. No GPU register, ring or command was touched.

## The tool

`G3PM.VXD` + `V9XG3PM.EXE` (`tools/diag/gen3_pool_map*.{asm,c}`,
`scripts/build-gen3-pool-map.ps1`), a dynamic VxD driven in four calls on
one handle:

1. **ALLOC.** The VxD reads the pool's 8,192 entries (`800`-`27FF`) and
   goes on only if all hold one value, which it keeps to restore. It
   allocates the pages fixed and zeroed. The publisher sorts the frames
   and runs the driver's `v9x_i9xx_pool_check` on them.
2. **MAP**, only with that acceptance and the VxD's own check that every
   page is present and outside stolen memory. The steps: `WBINVD`, one PTE
   per page (frame | valid, cache type 0) in page-table order, the last read
   back to post them, every PTE compared. Then a pattern
   (`index*4 ^ 5A3CC3A5`) through the pool's 32 MiB of aperture, a locked
   operation to drain the write-combining buffers, and a read back through
   the aperture. Last, `WBINVD` and a read through the pages' own linear
   mapping, then `WBINVD` again.
3. **Mode changes**: `ChangeDisplaySettings` to 640x480 at the current
   depth and back to 1024x576, twice.
4. **CHECK**: every PTE and the whole pattern again, then **UNMAP**: the
   saved PTE written back to every entry and compared, the pages freed. The
   VxD's exit path does UNMAP's work too, for a publisher that dies between
   calls.

Each step's name reaches the INI before the step runs.

## Result: PASS

| | |
|---|---|
| entries before | all `7FFBE001`, the VBIOS scratch PTE (8,192 of 8,192) |
| pages | 8,192 in 34 ms, `0BDEF000`-`0DE0D000`; policy accepted |
| PTEs after writing | 8,192 read back as written; last `0DE0D001` |
| pattern write, 32 MiB through the aperture | 32 ms (write-combined, about 1 GB/s) |
| read back through the aperture | 1,312 ms, 0 mismatches |
| read back through the pages' linear mapping | 0 mismatches |
| two mode cycles | both switches succeeded both times |
| PTEs and pattern after them | 0 mismatches each |
| restore | 8,192 entries back to `7FFBE001`; pages freed |

`V9XG3PL.EXE` run afterwards: span `7E0`-`281F` all `7FFBE001`, hash
`C5F0C605`, the same as before any write (Phase 0). The machine stayed up
and the desktop came back at 1024x576.

## What this establishes

- **The GTT can map system RAM above the stolen run, and the CPU path
  through the aperture reaches exactly those pages.** The pattern read
  through the RAM's own mapping is the one written through the aperture,
  dword for dword, so aperture page *i* is page-table page *i*. No
  chipset flush (the Intel Flush Page) was needed for the CPU's own
  writes to arrive.
- **A VBIOS mode set does not rewrite PTEs above the stolen run.** Phase 0
  could not tell this apart from a rewrite to the same value; here the
  entries held the pool's frames through two cycles. Plan item 6's
  re-apply after every mode change is not needed. The decision stays open
  for DOS-box returns and anything else that re-posts the VBIOS, which
  were not run (the DOS-box return hard-locked this machine before).
- **Reads through the aperture are slow (about 25 MB/s) and writes are
  fast.** That suits textures, which the CPU writes and the GPU reads, and
  argues against any path that reads pool memory back with the CPU.

## Not established

- That the GPU samples the pool correctly, or that erratum 12 stays quiet
  when it does. That is Phase 3.
- Allocation at `Device_Init`: this was a dynamic VxD after boot.
- A depth change, a DOS box or suspend while the pool is mapped.
