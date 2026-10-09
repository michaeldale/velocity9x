# OpenGL fog and the remaining immediate-mode forms draw right on the software engine and the Mach64

Date: 2026-10-09
Machines: `Win98SE-Fast-D3D` (86Box, port 9878, boot 608, software
engine, "Velocity9x Software"); A8U4I5, ATI Rage XL PCI, 800x600x16,
boot 346 ("Velocity9x Mach64").
Driver: each machine's installed display driver and HAL (render-interface
ABI 4), with V9XGL.DLL swapped for one built from this change. 9878 was
left on the new ICD; A8U4I5 was put back on its own ICD (CRC 2F18703A).
Evidence: [`../probe/icd-fog-immediate-forms-2026-10-09/`](../probe/icd-fog-immediate-forms-2026-10-09/)

## The change

`glFogf/fv/i/iv` were stubs, as were 97 integer and double forms of
glColor, glTexCoord, glVertex and glNormal, glRect, glEdgeFlag and the
colour-index calls. The ICD now implements 204 of the 336 slots, from 103.

Fog is vertex fog: each vertex's factor (LINEAR, EXP, EXP2, from the
coordinate c = |z_e|) goes into the specular alpha, and the engine
interpolates it. GL 1.1 3.9 allows both approximations. Clipped vertices
take an interpolated c. An engine that refuses a fogged batch, and has no
software fallback, draws it unfogged and counts `fog-dropped`, as the
Glide wrapper does. Without that the ViRGE, whose S3D refuses fog, would
draw nothing. The two-unit split declines fogged batches.

## Measured

V9XGLP's new fog scene: a red quad at eye distance 0.5 under blue fog,
read back with glReadPixels. The probe tolerance is +-8.

| key | expected | before (stubs) | soft, after | Mach64, after |
|---|---|---|---|---|
| FogLinear (0..1) | (128,0,127) | 0xFF0000 | 0x84007B, Ok | 0x84007B, Ok |
| FogExp (density 2) | (94,0,161) | 0xFF0000 | 0x5A00A5, Ok | 0x5A00A5, Ok |
| FogExpAtEye | (255,0,0) | Ok | Ok | Ok |
| FogQueryOk | 1 | 0 | 1 | 1 |
| RectColor3us (glRecti, glColor3us) | green | black | Ok | Ok |
| Vertex2sColor4b (glVertex2s, glColor4b) | yellow | black | Ok | Ok |
| EdgeFlagQueryOk | 1 | 0 | 1 | 1 |
| ErrorAfterFog / ErrorAfterVariants | 0 | 0x502 | 0 | 0 |

Both engines read the same pixels. The Mach64 fogged in hardware: its
ICD counters show `fog-dropped=0`.

Every other V9XGLP key on 9878 is identical before and after. On
A8U4I5 two keys differ: `ScissorInsidePixel` and `ScissorOutsidePixel`,
GDI `GetPixel` reads taken just after SwapBuffers. The baseline run read
0xFFFFFF/0x00FF00, the window background and the green clear in the
wrong place. Both runs with the new ICD read the expected
0x00FF00/0xFF00FF, as every recorded V9XGLP run in this directory does.
That scene only clears and swaps, which this change does not touch. The
baseline value is unexplained, and was not reproduced.

## Not measured

- The fog-drop path. The ViRGE guest (9869) runs an older probe ICD, and
  the drop was not exercised anywhere.
- Gen3, which also fogs in hardware. The netbook was offline.
- Fog in a game. Quake 2 and Half-Life were not run.
