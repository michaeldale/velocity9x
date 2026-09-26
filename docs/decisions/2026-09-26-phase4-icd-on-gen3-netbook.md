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

## Standing

The plan's answer to a capability refusal is the software engine on the
same surfaces after the shared drain. Phase 0.5 measured that the Gen3
engine and the CPU agree on colour and depth encodings and ordering in a
shared target (all three cells pass), so the fallback is licensed on Gen3.
It is not licensed on the ViRGE's 565 target (the S3D writes 1555).

Not established: the ViRGE, which offers no ICD format on its 565 guest;
textures on any hardware engine; Gen3 lifetime and thread cases.
