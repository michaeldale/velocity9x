# Phase 4: the ICD on Gen3 - everything but textures draws and reads back

Date: 2026-09-26. Machine: MICHAEL-NETBOOK (945GSE / GMA 950), Windows 98
SE, 1024x576 RGB565 desktop, agent at 10.0.1.248 (its new wired address).
Driver, HAL and mini-VDD from the intel-gma package at fd81435, deployed
by WININIT rename (boot 42; sizes checked against the package). The ICD
(`v9xgl.dll`, CRC 2729161D) is the one measured on the software guest at
fd81435, registered under `OpenGLDrivers` with REGEDIT.

A `WININIT.INI` was already waiting on the netbook: one line deleting
StarCraft's installer flag (`NUL=C:\PROGRA~1\STARCR~1\NOBOOT.DAT`). It was
kept and the driver renames added after it.

## Measured

Evidence beside this record:

- `2026-09-26-phase4-icd-gen3-V9XR3DP.ini` - the render-interface probe,
  built at 70aa4e2. Negotiation, describe (engine 3, "Velocity9x GMA 950",
  target formats 565 only, texture size limit 256), clear, and the three
  depth-tested quads all exact on the GPU. The explicit draws that name a
  CPU texture with a scissor, and a colour mask, answer UNSUPPORTED (4),
  as `accepts` is written to; nothing was drawn for them.
- `2026-09-26-phase4-icd-gen3-V9XGLP.ini` and `...-V9XGL.log` - V9XGLP
  through the system OPENGL32. The ICD offers format 1 (16-bit colour,
  16-bit depth, double-buffered, not generic). Correct on Gen3: the clear,
  the scissored clear (bottom left inside, top right outside), the
  depth-tested geometry, glReadPixels after each (so the Lock's drain now
  follows GPU work, and the rows come back the right way up), the
  queries, and the vertex-array scene (yellow indexed quad, cyan
  interleaved strip). No GL error anywhere.
- **Textures draw nothing.** The three readback points of the textured
  scene are black, the clear colour. The ICD log has exactly one refused
  batch: `draw result=4 triangles=3 submitted=0`. The ICD sends textures
  as CPU levels, and Gen3 cannot sample system memory, so it refuses them
  before submitting anything.

## What the evidence disputes

The software guest's pass said nothing about Gen3; this is the first
evidence that the GL pipeline's output (surface coordinates, depth, the
explicit fragment state) is what the Gen3 engine draws correctly. It also
shows the gap is not in GL: a refused draw is lost silently. The
application gets no error and an empty frame.

## Later the same day: the software fallback, and textures draw

The render interface's draw now answers a Gen3 refusal by draining the
engine and drawing the whole request with the software engine on the same
surfaces (`v9x_r3d_fallback` in `d3d_core.c`; Gen3 only, for the reason in
Standing below). Validation and describe take the software engine's
texture limit, 512, since it draws what the hardware refuses for size.
Direct3D does not pass through this path and is unchanged.

Deployed the same way (boot 43; the guest's V9XHAL.DLL hash-identical to
the package). Evidence: `2026-09-26-phase4-icd-gen3-fallback-V9XR3DP.ini`,
`...-V9XGLP.ini`, `...-V9XGL.log`.

- V9XR3DP: describe reports texture size 512. The CPU-texture draw with a
  scissor now answers OK and is exact (top row the texture's magenta
  inside the scissor, bottom row the untouched blue), and so is the
  red-only masked draw; both raw values (F81F, FFE0) equal the software
  guest's. Clears and the three depth quads unchanged.
- V9XGLP: the textured quad now draws, perspective-correct: red at 10%
  and at 42% of the width, blue at 60%, as on the software guest. It was
  drawn by the CPU into a back buffer the GPU had cleared, and the GPU
  drew the following scenes into the same buffer; every scene before and
  after it reads back as before. The ICD log records no refused batch.

This is the first evidence of the CPU and Gen3 drawing into one GL
surface in both orders within a frame sequence. It does not time the
fallback, and a scene mixing the two engines inside a single frame with
overlap was measured only by Phase 0.5's rung, not through GL.

## Standing

The plan's answer to a capability refusal is the software engine on the
same surfaces after the shared drain. Phase 0.5 measured that the Gen3
engine and the CPU agree on colour and depth encodings and ordering in a
shared target (all three cells pass), so the fallback is licensed on Gen3.
It is not licensed on the ViRGE's 565 target (the S3D writes 1555).

Not established: the ViRGE, which offers no ICD format on its 565 guest
and has no fallback; textures sampled by the Gen3 hardware itself (they
are all drawn by the CPU); the fallback's cost; Gen3 lifetime and thread
cases.
