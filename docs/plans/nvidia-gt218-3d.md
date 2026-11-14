# GT218: Direct3D on the Tesla 3D engine

Date: 2026-11-14

Status: phase 0 done (2026-11-14): the sources are fetched and the
method list for a first triangle is written down,
[gt218-tesla-3d-first-triangle.md](../specifications/gt218-tesla-3d-first-triangle.md).
It found that EXA's pipeline needs no stack, TLS or new shader encoding,
and that a pitch-linear colour buffer cannot have a depth buffer, which
shapes phase 4. Phase 1 passed on M-BXMFA8 boot 117: the 3D object
binds and takes EXA's setup without a fault
([record](../decisions/2026-11-14-gt218-3d-bind-and-setup.md)). Phase 2
passed the same boot: a solid rectangle through the 3D engine is exact
at 32 and 16 bpp, into a pitch-linear target, with no pixel outside it
touched; 5:6:5 keeps each channel's top bits
([record](../decisions/2026-11-14-gt218-3d-first-triangle.md)). Phase 3
under way: the encoder (8165fb01, EXA's four programs rebuilt word for
word) and Gouraud shading from our own programs, pixel-checked at 32 and
16 bpp
([record](../decisions/2026-11-14-gt218-3d-gouraud-from-the-encoder.md));
then a pitch-linear texture sampling exactly at both depths, with 5:6:5
keeping each channel's top bits, after a state leak between draws was
found and fixed
([record](../decisions/2026-11-14-gt218-3d-texture.md)). Z is planned in
[nvidia-gt218-3d-z.md](nvidia-gt218-3d-z.md): tiled surfaces through VM
alias windows with fixed kinds, presented by a 2D copy. Phase 6 of
[nvidia-gt218.md](nvidia-gt218.md). Michael asked for it on 2026-11-14,
after GDI and DirectDraw fill and copy ran on the 2D engine
([2026-11-14-gt218-gdi-engine-first-run.md](../decisions/2026-11-14-gt218-gdi-engine-first-run.md)).
He ruled out software Direct3D for this card earlier ("no point doing
software 3d"), so `Direct3D = 'not-advertised'` stays until the engine
draws.

## What exists to build on

The channel the HAL builds for 2D is the channel 3D needs. Nothing
about it changes:

- PFIFO and PGRAPH brought up by the measured list (`nv50_init.c`), one
  G84-style IB channel, its structures in the reserve
  (`nv50_chan.c`, `gt218_reserve.c`).
- **The PGRAPH context program and image are nouveau's, word for word**
  (`nv50_grctx.c`; the kmscube trace compared all 124 program words and
  945 non-zero image words,
  [record](../decisions/2026-11-13-gt218-nouveau-trace-pd-offset.md)).
  That image holds the 3D class's state as well as 2D's. **No vendor
  microcode is needed**: unlike the R600 and Evergreen CPs, Tesla PGRAPH
  runs on the context program we already generate.
- The VM maps the first 512 MiB of video memory as the identity, so a
  Direct3D render target, Z buffer or texture in the DirectDraw heap is
  addressed by its heap offset, as the 2D engine's surfaces already are.
- One kick at a time, idle = IB get at PFIFO's PUT with the pusher
  drained and PGRAPH idle, faults visible in 2100h, 400100h and 3228h.
  GDI and the HAL already share the ring this way.

## What is new

The 3D object is **class 8597h**: nouveau's `GT214_TESLA`, the class
`gt215_gr` lists for every GT21x chip, the GT218 included
(`nvkm_engine_gr_gt215.c`). Earlier notes that said 8297h were wrong;
8297h is G84 to G98.

A triangle on it needs, beyond the 2D engine:

1. **The object bound** to a subchannel beside 502D (a RAMHT entry and
   an object in the instance block, as `nv50_chan.c` writes for 502D).
2. **The 3D engine's own setup**, which neither the BIOS nor the context
   image does: Mesa's `nv50_screen_init_hwctx` sequence. That covers the
   code segment base for shaders, the TIC and TSC tables (texture image
   and sampler descriptors), the stack and local memory (TLS), the
   vertex runout, the viewport, and the many enables it writes once.
3. **Shaders.** Tesla has no fixed-function pipeline: a vertex program
   and a fragment program in the Tesla ISA, uploaded to the code
   segment. Direct3D 5-7's fixed function (Gouraud, texture stages, fog,
   specular, alpha test, colour key) has to become fragment-program
   variants, as Evergreen (`eg_shader.c`) and the R600 (`d3d_r6.c`)
   already do.
4. **Pipeline state per draw**: render target (`RT_*`), Z buffer
   (`ZETA_*`), blend, depth and stencil, cull, scissor, vertex format,
   and vertices. The simplest vertex path writes them inline in the push
   (`VERTEX_DATA` after a `VERTEX_BEGIN_GL`), with no vertex buffer.

## Sources

Collected (`build\external\nv50\`): nouveau's kernel PGRAPH, PFIFO, MMU
and context generator, the 2D, M2MF and channel class headers. **None
of the 3D side.**

Wanted, all MIT-licensed:

- Mesa `src\gallium\drivers\nouveau\nv50\`: `nv50_screen.c`
  (`nv50_screen_init_hwctx`, the setup), `nv50_3d.xml.h` (the method
  names and offsets), `nv50_state.c`, `nv50_state_validate.c`,
  `nv50_shader_state.c`, `nv50_program.c`, `nv50_vbo.c`, `nv50_tex.c`.
- Mesa `src\nouveau\codegen\nv50_ir_emit_nv50.cpp`: the Tesla
  instruction encodings, which a host-tested encoder would follow.
- xf86-video-nouveau `nv50_accel.c`, `nv50_exa.c`, `nv50_shader.h`:
  EXA's solid, copy and composite on the 3D engine with hand-assembled
  vertex and fragment programs. This is the minimal complete pipeline,
  the analogue of Linux 3.9's Evergreen blit that the Cedar work started
  from.
- envytools `rnndb\graph\g80_3d.xml` and the Tesla ISA notes.

Trace route on the local GT210 (bringupkit target), if the sources leave
a step unclear: Mesa under envytools' `valgrind-mmt`, decoded by
`demmt`, gives the exact method stream a GL program sends to class
8597h. The 2D work's mmiotrace only covered MMIO, not pushbuffers.

## Phases

0. **Sources.** Fetch the files above into `build\external\nv50\`, and
   write down from them the bind, the `init_hwctx` sequence and the EXA
   solid-fill pipeline as a method list. Desk work only.
1. **Bind and set up, no draw.** A probe stage (V9XN5CH, or a new
   `/3d` mode): bind 8597h on the HAL's live channel, replay the setup,
   and read 400100h, the trap words and 3228h. The 3D engine accepts its
   setup without a trap. This is the step most likely to fault, and a
   fault is now survivable and visible (runs 22 and later).
2. **A solid rectangle through the 3D engine**: EXA's constant-colour
   fragment program, two triangles inline, into a scratch surface,
   checked byte for byte at 32 and 16 bpp, as the 2D engine's fill was.
   This is the first triangle.
3. **Gouraud, texture and Z**: a host-tested stream builder
   (`nv50_3d.c`) and fragment-program encoder (`nv50_shader.c`), like
   `eg_shader.c`. Gouraud, one modulated texture, Z16 and Z24 depth
   tests, checked on the card.
4. **The HAL**: a `d3d_nv50.c` back end behind the same interface as
   `d3d_r6.c` and `d3d_eg.c`, CAP_D3D on the NV50 row, `d3d_select.c`
   choosing it, then V9XD32, V9XDDP's 3D checks and 3DMark 2000. OpenGL
   on the same back end after.

## Decisions owed (Michael's, before phase 3)

1. **Shader source.** Hand-assembled programs taken from EXA and
   extended by hand, or an encoder following Mesa's codegen. The
   Evergreen and R600 work chose the encoder; its host test rebuilt the
   reference shaders word for word, which is what made it trustworthy.
   Recommended: the encoder.
2. **Reserve size.** The reserve is 1 MiB, of which the channel uses
   704 KiB. Shaders, TIC and TSC, the stack and TLS, and any vertex ring
   need more. Recommended: grow it to 4 MiB at the top of the BAR1 map
   (the heap is 126.5 MiB, so this costs about 2%), sized from what
   phase 1 measures `init_hwctx` needs.

## Risks

- `init_hwctx` writes more than a hundred methods, and some take
  addresses (code, TLS, TIC, TSC). An address outside the VM faults; run
  23 showed one kind of bad address that froze the card rather than
  trapping. Phase 1 checks every address against the VM's 512 MiB before
  the kick.
- The stack and TLS sizes scale with the number of multiprocessors.
  The GT218 has the fewest of its line (PBUS 1540h `F3010001` on
  M-BXMFA8), and the sizes come from Mesa's formula, not a measurement,
  until phase 1.
- All of the above is read from source, and none of it has been
  measured on this card. Each phase's record says which parts it
  measured.
