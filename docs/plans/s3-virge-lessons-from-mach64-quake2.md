# S3 ViRGE: what the Mach64 Quake 2 work suggests

Status: assessment, 2026-09-29. Nothing below has run on a ViRGE since the
change it refers to; read each item's evidence line.

The Mach64 work that got Quake 2 drawing and from 4 to 8.4 fps (commit
`81c2e86`, `docs/plans/ati-rage-mobility-hardware-3d.md`) split into
changes in the ICD, which every engine shares, and changes in the Mach64
path. This is which of them reach the ViRGE, and which might be copied.

## Already in effect on the ViRGE (shared ICD code), untested there

- **Square copies of non-square textures.** Describe has always marked the
  ViRGE square-only. The ICD now squares a non-square or too-small texture by
  repeating it (wrap) or its edge (clamp) and scales s and t, instead of
  keeping the CPU copy that the ViRGE, having no fallback, then refused. The
  0.9.0 changelog records non-square textures refused on the ViRGE and the
  Quake 3 demo's text not drawing; whether the one is the cause of the other
  was not established. Measure on A8U4I5.
- **Fan triangles taken as computed.** A triangle of three unclipped vertices
  no longer goes through the clipper and windowing again. Pure CPU saving; the
  ViRGE's host CPUs are slower, so it may matter more there.
- **Alpha tests that cannot fail are dropped**, and the texture's alpha op
  re-derived. On the ViRGE this changes which draws reach `accepts`; check
  the refusal counters before and after.
- **`-ox` on the ICD.** No effect measured on the Gateway, where the cost was
  elsewhere.

## Gap: the ViRGE's minimum texture size is not described

Describe reports `hw_texture_size_min` 1 for the ViRGE, but its limits say 4
(`v9x_d3d_virge_limits`), so the ICD still sends 1x1 and 2x2 textures that
the HAL then refuses. Setting it to `ops->limits->texture_size_min` in
`v9x_r3d_describe_body` would let the ICD grow them to 4 by repetition. It
was left at 1 on 2026-09-29 so that ViRGE behaviour did not change untested.
One line, then an A8U4I5 run with Quake 3's menu.

## Not applicable

- **Texture packing.** The ViRGE already binds at any 8-byte offset
  (`texture_align` 8); the Mach64's 4 KiB-per-level waste never existed
  here.
- **Engine copy for Blt.** The ViRGE has used its engine for copies since
  Ironfield (`v9x_virge_copy`).
- **Mip filter naming.** The ViRGE maps MIPLINEAR and LINEARMIPNEAREST the
  DDK's way already (`d3d_virge.c`), as the DDK's own ViRGE HAL is the source
  of that reading.
- **Vertex register reuse.** The S3D takes per-triangle edge slopes and
  gradients (`DXDY01/02/12`, `XEND*`, `DZDX`, ...), computed on the CPU, not
  vertices; there is nothing shared between a fan's triangles to keep.

## Worth measuring

- **Where a ViRGE frame goes.** The ICD's report now logs frames and the time
  in present, draws and uploads (`time ... swaps=` in `V9XGL.LOG`). Run the
  Quake 3 demo or GLQuake on A8U4I5 and read it before changing anything.
- **Register writes per triangle.** The ViRGE writes roughly 25 to 40
  registers a triangle depending on state. If draws dominate there as on the
  Mach64, the cost per write on that bus is the number to get first, the way
  the Mach64 buckets did.
- **`-ox` on the ViRGE HAL sources.** Only the Mach64 sources were optimised,
  deliberately. Per-triangle gradient maths is float-heavy C and may gain
  more than the Mach64's did. It is a code-generation change on a path whose
  measurements were all taken unoptimised, so it needs the full V9XDDP run on
  A8U4I5 before and after.

## The Trio64

No 3D. Its engine copy (`v9x_trio_copy`) predates this work. Nothing here
applies.
