# Intel Gen3: what the Mach64 Quake 2 work suggests

Status: assessment, 2026-09-29. Nothing below has run on the netbook since
the change it refers to.

Quake 2 already ran at 28 fps on the GMA 950 (0.9.0), with hardware
non-square textures and batches held across glEnd. The Mach64 work that got
it from 4 to 8.4 fps on the Rage Mobility-M (commit `81c2e86`) was partly in
the ICD, which Gen3 shares, and partly Mach64-only. This is which parts reach
Gen3, and which might be worth copying.

## Already in effect on Gen3 (shared ICD code), untested there

- **Fan triangles taken as computed.** A triangle of three unclipped vertices
  no longer goes through the clipper and windowing again. Quake 2's world
  polygons are fans, so this cuts the ICD's per-vertex work on every engine;
  on the Gateway it took Quake 2 from 63 to 74 frames per 10 s. The netbook's
  28 fps was measured before it. Re-measure with `timerefresh` at demo1's
  spawn point, as the 0.9.0 figure was.
- **Alpha tests that cannot fail are dropped.** Gen3 tests the fragment's
  alpha fully, so this only removes work. Check that no draw changes.
- **The ICD's `time` line** (frames, and ms in present, draws, uploads per
  10 s in `V9XGL.LOG`) is available on the netbook now.
- **Square copies** do not apply: Gen3 describes powers of two, square or
  not, and its minimum is 1.

## Candidates

### Texture placement at page granularity

Gen3 places every texture and every mip tree on its own 4 KiB page
(`V9X_I9XX_SANDBOX_PAGE_BYTES`, `v9x_d3d_i9xx_place_block`). A 16x16 texture
of 512 bytes holds a page. The Mach64 had the same waste. There it left
Quake 2 thrashing (26,204 texture creates in one run), and packing on 64
bytes fixed it.

The 0.9.0 changelog records Serious Sam outgrowing the netbook's ~5 MB of
free video memory, with LRU eviction thrashing. Whether Gen3 can bind a
texture below page alignment depends on `MAP_STATE`'s address field. The
code comments call it page aligned ("MAP_STATE's address is"). That should
be checked against the Intel 915/945 documentation before anything is tried:
if it is architectural there is nothing to gain, and if it is only this
driver's choice, the same measure-first route applies (probe texture scenes,
then Serious Sam's create and evict counters).

### Primitive type: fans and strips instead of lists

Gen3 emits every batch as `PRIM3D_TRILIST`, 7 dwords per textured vertex and
3 vertices per triangle (`i9xx_vertex.c`). A fan as `PRIM3D_TRIFAN`, or
indexed vertices, sends each vertex once. That is the Gen3 form of the
Mach64's register-reuse idea (`docs/plans/mach64-vertex-register-reuse.md`).
Gen3 feeds a DMA ring, not MMIO, so the saving is ring bytes and CPU copy
time, not bus stalls, and may be small. On the Mach64 itself register reuse
cut the writes about 2.7 times and changed nothing measurable
(`../decisions/2026-09-29-mach64-vertex-register-reuse-physical.md`), so
measure before building it here. The HAL's Gen3 timing buckets
(`TimeRingWrite*` in V9XTRACE) will say how much time is in writing the ring
before anyone restructures emission. The render interface would also need to
carry fan structure, which it does not today: the ICD sends triangles.

### Code generation

Only the Mach64 HAL sources and the ICD are built `-ox`. Gen3's per-batch
decode and allowlist (`TimeDecode*`) are C that runs per dword. If the
buckets show them significant, `-ox` on the Intel sources is a cheap trial.
It needs the netbook's full gate before and after, since every Gen3
measurement so far was taken unoptimised.

## Not applicable

- **Engine copy for Blt.** Gen3's blitter has served DirectDraw Blt since
  0.8.1.
- **Mip filter naming.** Gen3 was corrected to the DDK's reading on
  2026-09-25; the Mach64 had the same mistake and took the same fix.
- **The register shadow search.** Gen3 has no MMIO register shadow on the
  draw path.
