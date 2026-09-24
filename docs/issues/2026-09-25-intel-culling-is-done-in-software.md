# Intel back-face culling is done in the core's software, not by the hardware

Filed: 2026-09-25
Status: open - review and replace
Machine: MICHAEL-NETBOOK (945GSE / GMA 950)

## What is there now

Since `2026-09-25-back-face-culling-in-the-d3d-core.md`, the Direct3D core
drops back faces with a signed-area test (`src\display32\d3d\d3d_cull.c`)
before the Intel engine sees them, and the engine advertises `CULLCW` and
`CULLCCW` on the strength of that. The GPU still runs with
`S4_CULLMODE_NONE`, programmed by `v9x_i9xx_emit_pipeline` in
`src\chipsets\intel\i9xx_3d.c` and pinned to exactly that value by the
allowlist decoder in `src\chipsets\intel\i9xx_3d_decode.c`.

It works (all three 3D WinBench 98 cull tests Capable on the netbook), but
it is the CPU doing a job the setup engine has a field for, per triangle,
on a 1.66 GHz Atom. The operator's view: it should be the hardware.

## Why it was done in software first

- Which Direct3D winding each S4 encoding removes is unmeasured on this
  part. Mesa's i915 flips `S4_CULLMODE_CW`/`CCW` with the framebuffer's y
  orientation (`i915_update_cull` / `i915_state.c`), so the encoding cannot
  be read off another driver and trusted.
- The decoder pins S4 exactly; widening it is a change to the one check
  that stands in for the retired CRC gate, and was not what that session
  was for.

Neither is a reason to keep it this way.

## To do

1. Name the S4 cull encodings in `include\velocity9x\intel_gen3_3d.h`
   (`S4_CULLMODE_BOTH` 0, `NONE` 1, `CW` 2, `CCW` 3 at bits 13-14). Two
   trees agree, read 2026-09-25: Mesa gallium
   `src\gallium\drivers\i915\i915_reg.h` and xf86-video-intel
   `src\sna\gen3_render.h:371-375`. What they call CW still needs the
   measurement in step 3.
2. Make the cull field a parameter of the runtime state builder, as S2 and
   S6 already are, and let the decoder accept exactly the three values the
   builder can produce - and require the value the limits say, like S6.
3. **Measure the mapping on the netbook** with 3D WinBench 98's Cull
   Clockwise and Cull Counterclockwise tests and the software cull gated
   off for Intel: which S4 value removes the CW squares in screen space.
   Record it in a decision note with the captured frames, including the
   encoding that turned out wrong.
4. Map `D3DRENDERSTATE_CULLMODE` to the measured S4 value in `d3d_i9xx.c`
   and stop the core culling for this engine - e.g. an engine limit saying
   "culls in hardware", so `v9x_d3d_cull_honoured` is not consulted for it
   while the ViRGE and software engines can still opt in to the core path.
5. Re-run the three cull tests and a 3DMark99 pass; compare counters.

The core path should stay for engines with no cull hardware - the ViRGE's
S3D unit and the software rasterizer - where it is the only way to offer
the caps.
