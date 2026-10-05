# SiS 6326: OpenGL through the render interface

Date: 2026-10-05

Status: started 2026-10-05 at Michael's request ("work on OpenGL next").
Steps 1-4 done the same day
([record](../decisions/2026-10-05-sis6326-opengl.md)):
- the scissor measured exact;
- V9XGLP passes on the SiS;
- Half-Life OpenGL runs at 6.0-6.5 fps (0.8 on the CPU) and Quake 2's
  `timerefresh` at 7.8 fps, both on the engine.

Target: A8U4I5, SiS 6326 card 2 (rev 0Bh), the `sis` family with the
Direct3D engine of [sis-6326-hardware-3d.md](sis-6326-hardware-3d.md).
Follows the ICD plan, [opengl-1.1-icd.md](opengl-1.1-icd.md), Phases 4-5,
as Gen3, ViRGE, Mach64 and Rage IIC did.

## Where it stands

Half-Life's OpenGL renderer ran at 0.8 fps on A8U4I5, and only on the CPU
engine
([evidence](../probe/a8u4i5-sis6326-halflife-2026-10-05/README.md)). With
the hardware engine selected the ICD draws nothing, for three reasons:

1. **`v9x_d3d_sis_map_draw` refuses every explicit draw**
   (`V9X_D3D_SIS_REFUSE_EXPLICIT`). Every render-interface draw is
   explicit (`r3d.h`, `explicit_state`).
2. **The render interface's describe has no SiS case.** It reports engine
   0 and `hw_texture_size_max` 0, so the ICD never makes VRAM textures and
   sends CPU levels, which no hardware engine samples.
3. **There is no software fallback.** It exists for Gen3 only, the one
   engine measured to agree with the CPU on colour and depth. The SiS
   cannot share it as it stands: Z16 holds z x 2^15 here and z x 65535 in
   the software engine.

## What an explicit draw asks of the SiS, and what is already there

| Explicit state | SiS today | Work |
|---|---|---|
| Perspective-correct texturing | always on when textured | none |
| Blend factor pair | the measured factors; the rest refused | none (refusals become UNSUPPORTED) |
| Alpha test | mapped | none |
| Vertex fog | mapped | none |
| Depth test and write | mapped, 15-bit | the ICD's depth clear must be halved too |
| Scissor (half-open rows/columns) | not mapped | clip registers 8A30h/8A34h, inclusive: **measure a sub-rectangle clip first** |
| Write mask | not mapped | refuse anything but RGB, as every engine does |
| CPU texture levels | not sampled | refuse; publish hardware-texture limits so the ICD sends surfaces |
| Surface texture | `resolve_texture` reads DirectDraw surfaces | expected to work as for Direct3D |

## Steps

1. **Host.**
   - Clip fields in `struct v9x_sis3d_state`.
   - The mapping takes explicit draws on the terms above.
   - The describe publishes the SiS: engine id 6, power-of-two textures 1
     to 512, all three formats.
   - The render-interface clear converts its depth value through the
     engine's depth-fill rule.

   Each change test-first where it is host-testable.
2. **Probe the clip.** SIS3D draws a triangle larger than a clip
   rectangle that starts inside the target, and reads the edges.
3. **V9XGLP on A8U4I5,** against its software-engine result.
4. **Applications:** the Quake 2 demo (`C:\Q2Demo`, `timerefresh`), then
   Half-Life `-gl` `timedemo mwd5` at 640x480. Scores recorded, not
   compared.

## Not in scope

The software fallback for the SiS. It would need the Phase 0.5 mixed-frame
measurement and a depth scale both engines share.
