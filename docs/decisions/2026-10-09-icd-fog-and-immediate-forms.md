# OpenGL fog and the remaining immediate-mode forms draw right on the software engine and the Mach64

Date: 2026-10-09
Machines: `Win98SE-Fast-D3D` (86Box, port 9878, boot 608, software
engine, "Velocity9x Software"); A8U4I5, ATI Rage XL PCI, 800x600x16,
boot 346 ("Velocity9x Mach64"); the netbook (Intel GMA 950, wifi at
10.0.1.254, 1024x576, boot 129, "Velocity9x GMA 950").
Driver: each machine's installed display driver and HAL (render-interface
ABI 4), with V9XGL.DLL swapped for one built from this change. 9878 was
left on the new ICD. A8U4I5 (CRC 2F18703A) and the netbook (its 0.13.0
ICD, CRC 632D4946) were put back on their own.
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

| key | expected | before (stubs) | soft, after | Mach64, after | Gen3, after |
|---|---|---|---|---|---|
| FogLinear (0..1) | (128,0,127) | 0xFF0000 | 0x84007B, Ok | 0x84007B, Ok | 0x84007B, Ok |
| FogExp (density 2) | (94,0,161) | 0xFF0000 | 0x5A00A5, Ok | 0x5A00A5, Ok | 0x5A00A5, Ok |
| FogExpAtEye | (255,0,0) | Ok | Ok | Ok | Ok |
| FogQueryOk | 1 | 0 | 1 | 1 | 1 |
| RectColor3us (glRecti, glColor3us) | green | black | Ok | Ok | Ok |
| Vertex2sColor4b (glVertex2s, glColor4b) | yellow | black | Ok | Ok | Ok |
| EdgeFlagQueryOk | 1 | 0 | 1 | 1 | 1 |
| ErrorAfterFog / ErrorAfterVariants | 0 | 0x502 | 0 | 0 | 0 |

The "before" column held on all three machines. All three engines read
the same pixels, and none dropped fog (`fog-dropped=0`). The Mach64 has
no software fallback, so it fogged in hardware. On the netbook the ICD
saw no refusal (`hw-refused=0`). Gen3's software fallback sits inside
the render interface, and nothing counts its use, so the counters cannot
say which drew the fogged quads. Reading the code says Gen3 did. The
fallback runs only when `v9x_d3d_i9xx_accepts` refuses, and that function
has no fog condition. The probe's untextured quad meets every condition
it does check: full write mask, a full-drawable scissor, no blend and no
alpha test. That is code reading, not a measurement.

Every other V9XGLP key on 9878 and on the netbook is identical before
and after. On
A8U4I5 two keys differ: `ScissorInsidePixel` and `ScissorOutsidePixel`,
GDI `GetPixel` reads taken just after SwapBuffers. The baseline run read
0xFFFFFF/0x00FF00, the window background and the green clear in the
wrong place. Both runs with the new ICD read the expected
0x00FF00/0xFF00FF, as every recorded V9XGLP run in this directory does.
That scene only clears and swaps, which this change does not touch. The
baseline value is unexplained, and was not reproduced.

## A game: Half-Life on the netbook

Half-Life 1.1.1.0, `hl.exe -console -condebug -gl`, fullscreen at
640x480, ran on the new ICD (netbook, boot 129). It played `timedemo
mwd5` three times in one console session: 15.836, 17.774 and 17.421 fps,
393 frames each. These are recorded, not compared: earlier records
used other builds, boots and launch methods. A `playdemo mwd5`
screenshot mid-demo shows the Xen map drawn right, with lightmapped
textures, sky, viewmodel and HUD. Half-Life quit cleanly from its
console. Over the whole session the ICD logged `stubs=0`, `hw-refused=0`,
`fog-dropped=0`, no failed draws, and every texture surface freed at
exit (`hwtex live=0`). The console's `Tracker Error: TrackerUI.dll
invalid` is Half-Life's friends UI and does not involve OpenGL.

This is a regression run. It does not exercise the new code: the
netbook's V9XGL.LOG history shows that no game run there (GLQuake,
Quake 2, Half-Life) ever called a fog or new-variant stub. The ICD counts
calls only to stubs, so a run cannot show which implemented slots a game
used. The config.cfg Half-Life rewrites on quit was put back.

## Not measured

- The fog-drop path. The ViRGE guest (9869) runs an older probe ICD, and
  the drop was not exercised anywhere.
- A count proving Gen3, not its fallback, drew the netbook's fog
  (above; the render interface counts no fallback draws).
- Fog in a game. None of the games installed on the test machines has
  been seen calling glFog.
