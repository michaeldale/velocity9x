# GeForce 210, 3DMark 2001 SE Car Chase High Detail: the ground near the robots draws near-white once 3DMark uses compressed textures

Date: 2026-11-25. Machine: W9QI0DAD (GT218 `10DE:0A65`, Windows 98 SE,
1024x768x16). Status: open, cause not found. Deferred to 1.1
(Michael, 2026-11-26): he expects the white ground may be how the scene
draws with DXTC anyway. Not checked against another driver.

## What is seen

DXT was published on the GT218 on 2026-11-25
([DXT record](../decisions/2026-11-25-gt218-dxt.md)). Since then 3DMark
2001 SE reports `DXTC` in place of `T32` and loads compressed textures.

In Car Chase High Detail:
- the road and hills draw as before up to about 16 s on the test clock;
- from about 18 s, in the stretch by the robots, the ground is near
  white, near the camera and far from it alike, with shadows and objects
  drawn over it.

The run before DXT was published (`T32`) shows that stretch dark and
textured. Every other game test looked right in the frames taken:
Dragothic, Lobby, and Nature with its mountains and lake. No texture or
draw was refused in any run (`M64Policy10` 0, `Nv50PoolRefused` 0), and
there were no engine faults.

Frames, in
[`../probe/gt218-cube-levels-dxt-w9qi0dad-2026-11-25/obs-car-chase-white/`](../probe/gt218-cube-levels-dxt-w9qi0dad-2026-11-25/obs-car-chase-white/):
- `gt218-cube-14.png` (13 s) and `-17.png` (16 s): textured;
- `-20.png` (18.5 s) and `-23.png` (21 s): white;
- `dxt-run-gt218-dxt-19.png`: the first DXT run, a pale patch at 17 s;
- `t32-run-2001se-vsync-06.png`: before DXT, at 19 s, dark ground.

## Ruled out

- **TEXBLT:** it never reaches the driver. In that run's `V9XD7.LOG` the
  only DP2 ops seen were 1Ch, 29h and 2Ah, with no `texblt` lines. So
  `v9x_d3d_ddi7_texblt` neither marking the pool copy stale nor stepping
  DXT rows by the source's lPitch is the cause here (it may still be
  wrong for DirectX 7 programs).
- **Pool overlap or the tiled window:** the pool lies alone between the
  heap's end and the 2 MiB reserve (`v9x_gt218_heap_end`), and the
  kind-70h window maps 512 MiB. The pool peaked at 19,998,208 of
  33,423,360 bytes.
- **DXT5 decoding:** a new V9XD32 T19 row, `Dxt5` (8 x 8, eight-value
  alpha, every index used), passes on the card first time: 0 mismatches,
  worst channel 9 of 9 (boot 158). DXT1 and DXT3 pass too.
- **The cube change:** the pale patch is in the DXT run, before cubes
  changed.

## Not yet tested

- **A mipmapped DXT chain on the card.** T19's DXT textures are single
  8 x 8 levels. T21's chain is RGB565. The white starts near the camera,
  which points at level 0 or the copy of the whole chain rather than at
  the smallest levels, but that has not been measured.
- **Large DXT textures** (512 or more a side), and DXT levels written
  through a LockRect with a sub-rectangle. A sub-rectangle lock would be
  offset by the 64-byte pitch while the packed copy is laid out at the
  packed pitch.
- **A DirectDraw Blt into a DXT surface.** `v9x_srccopy_body` sizes every
  copy by the desktop's pixel size. Only a video-to-video Blt would reach
  it; 3DMark's managed textures come from system memory.
- **Which texture the ground is**, and its format, size and level count.
  Nothing logs DXT creates yet.

## Next

A T19 or T21 row with a DXT1 chain of three or more levels, judged at a
size that reads levels 0 and 1. Failing that, a log line per DXT create
(format, size, levels, pool offset) and per draw that samples one, read
at the 18 s mark.

## 1.0 re-read, 2026-11-26

Stays open for 1.0 and is named in the README's known issues. Deferred
to 1.1 by Michael on 2026-11-25; the README's GeForce 210 entry names
it.
