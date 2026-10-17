# Haswell Phase D: the first triangles, constant and interpolated, match the reference

2026-10-17, i5-4570 (`8086:0412` rev 06), Windows 98 SE, with the B2 driver
running the desktop. `V9XHSWMM.EXE /draw` with `HSWMM.VXD` DIOC 3 (now four
segments, two render targets copied back), as designed in
`docs\plans\intel-haswell-direct3d.md` Phase D. Evidence:
`docs\probe\hsw-4570-draw-d-2026-10-17\`.

## Run 1 (boot 8): the pixel shader never retired

The ring store, batch A and the init batch completed as in Phase C. The
fourth segment (draw 1, then draw 2) timed out after 1,000,000 polls.
ACTHD `018226B0` is dword 172 of the draw 1 batch, and IPEHR `7A000003` is
the `PIPE_CONTROL` (RT flush, CS stall) right after the primitive: the
command streamer was waiting for the 3D pipeline to drain. INSTDONE read
`FFDFFFFF` (bit 21 not done). Both targets still held only the guard.

The cause was the render target write descriptor. Vol 7's render cache
data port table puts the message type at bits 17:14 (`1100b` = Render
Target Write) and message-specific control at 13:8. The encoder
(`2026-10-17-haswell-eu-and-3d-encoders.md`) had put it at 16:13, read
from a neighbouring column of the same table. The hardware therefore
decoded `0110b`, a reserved type, and the kernel's EOT send never
completed. Fixed in `hsw_eu.c`. The tests were changed to the corrected
layout first and failed against the old encoder before the fix.

With RCS wedged, the BCS blitter kept running: a V9XDDF fill and copy
passed. The `reset` that followed hard-locked the machine; the next boot
needed a power cycle. A wedged render engine therefore costs a power
cycle, not a `reset`, until shown otherwise.

## Run 2 (boot 9, after the power cycle): both draws pass

Status `000104FB`, all four segments, `StoresSeen=1FF`,
`Verdict=both-draws-completed`, EIR 0. Draw 1 and draw 2 breadcrumbs
(`C0DE0041`, `C0DE0051`) and the stores after them landed, with 99 polls
for the last segment.

Triangle (8.25, 8.25), (55.75, 10.25), (10.25, 55.75) in a 64x64
B8G8R8A8 target, judged at pixel centres:

| | D1 constant | D2 interpolated |
|---|---|---|
| inside, judged | 1103 | 1103 |
| outside, judged | 2927 | 2927 |
| within 1/64 px of an edge, not judged | 66 | 66 |
| coverage mismatches | 0 | 0 |
| colour mismatches | 0 | 0 (tolerance 2) |
| largest channel error | 0 | 1 |
| near vertex 0 (12,12) | `FFFF3399` | `FFD31616` |
| near vertex 1 (48,14) | `FFFF3399` | `FF0FD718` |
| near vertex 2 (14,48) | `FFFF3399` | `FF0F18D7` |

Afterwards every RCS ring register read 0, the BCS ring was idle and
enabled, forcewake was still held, and a V9XDDF fill and copy passed.

## Findings

- **The 3D pipeline draws** from state built entirely by host-tested
  builders. VF writes the VUE with the VS disabled (header, position,
  colour). CLIP is off. SF runs with its viewport transform off, so the
  vertices are already in screen space, as D3D TL vertices are. Cull mode
  is NONE. SBE has one attribute, read offset 1 and length 1. WM dispatches
  with perspective pixel barycentrics. The PS is SIMD8, 64 threads, sample
  mask 1, setup data from g4.
- **Both kernels execute.** `mov` with a float immediate, `pln` against a
  `<0;1,0>` plane and the g2-g3 barycentrics, and a header-less SIMD8
  render target write from g112 with EOT.
- **Coverage at pixel centres matches** the reference with no exceptions
  away from edges. Interpolation is within 1/255 of exact linear.
- **The RT write message type is 17:14.** Any encoder output older than
  this record has it wrong.
- **A wedged RCS leaves BCS working.** A `reset` after it hard-locked the
  machine.

## Not measured

- The 66 edge pixels: the fill convention on shared edges is not judged
  yet.
- Perspective-correct interpolation with w other than 1, depth, blending,
  textures, and more than one triangle per primitive.
- RC6 after drawing, since the driver's forcewake was held throughout.
- Why the hard lock followed the reset (the GT not reset by CF9h, or the
  boot path meeting a wedged GT) was not determined.

## Next

Phase E, the pixel shader set (modulate, decal, add, alpha test, fog, two
stages), and then wiring a draw path into the HAL. Each needs its own
design.
