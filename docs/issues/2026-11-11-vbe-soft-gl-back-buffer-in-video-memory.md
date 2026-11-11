# vbe: the software OpenGL back buffer lives in video memory, so every windowed present reads the frame buffer

Date: 2026-11-11. Status: open. Mechanism measured on one machine (the
R3E under `vbe`), consistent with the GTX 970 field log; nothing changed.

Raised by [GitHub issue 25](https://github.com/michaeldale/velocity9x/issues/25):
GLQuake windowed at 800x600 on the software path spent `present-ms=5895` on
4 swaps, about 1.5 s per frame
([`2026-11-08-vbe-no-pat-write-combining.md`](2026-11-08-vbe-no-pat-write-combining.md)).
The screen shadow plan asked whether that present reads the screen
([`../plans/vbe-shadow-framebuffer.md`](../plans/vbe-shadow-framebuffer.md),
Phase 0).

## What the code does

`v9x_gl_drawable_make` (`src\opengl\gl_surface.c:351-360`) creates the back
buffer and the depth buffer with `DDSCAPS_VIDEOMEMORY` on every family. On
`vbe` there is no 3D engine: the software rasterizer draws them with the CPU,
and `v9x_gl_drawable_present` blits the back buffer to the primary, then to
the front copy. The HAL does both blits with the CPU (`blt_cpu.c`), so each
present reads the back buffer out of video memory twice. The shadow does not
change which surface is read: the source is an off-screen surface, not the
primary.

## Measured, M-JJN1ZZ (R3E) under the vbe package, 1024x768x16

dispbench, the same boot (`../probe/vbe-shadow-open-items-2026-11-11/`):
DirectDraw video-to-video 512x512 4.7 MB/s, system-to-video 2,469 MB/s.

V9XGLP (a 360x280 window, `V9XGL.LOG` `time` lines):

| | Shadow off (boot 30) | Shadow on (boot 31) |
|---|---:|---:|
| present-ms / swaps | 3,703 / 22, 4,670 / 37 | 1,234 / 17, 2,655 / 42 |
| per present | 126-168 ms | 63-73 ms |

Two reads of about 200 KB each per present, at 5 MB/s or less, is about the
time measured. At 800x600x32 that is 1.9 MB a read, which at the GTX 970's
1.5 s per present is about 2.5 MB/s: the same mechanism on a slower read.
The GTX 970's own log was not re-read here; it is attached to the issue.

The shadow halves the R3E's figure, because the copy into the shadow writes
system memory instead of interleaving reads and writes on the bus. It does
not remove the reads.

## What would fix it

Back and depth buffers in system memory when the drawable is drawn by the
CPU, so the present is a system-to-video blit (2,469 MB/s on the R3E, under
a millisecond for this window). The HAL's `D3DSoftSysMem=refused` on `vbe`
is a sibling decision for software Direct3D and was not looked at here.
Not done: decided and measured nowhere yet.

## Release decision

2026-11-11 (Michael): 0.46.0 ships without the fix. It needs the software
rasterizer to accept system-memory targets and a HAL system-to-video blit,
and it touches the HAL every family shares. Full-screen software GL on
`vbe` probably presents the same way; that is the first thing to measure
when the work starts.
