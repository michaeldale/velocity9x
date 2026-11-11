# Evergreen OpenGL: a draw whose non-square mipmapped texture cannot be created is dropped (Quake 2 on the HD 5570 lost 42 % of its triangles)

Date: 2026-11-11. Status: open. Measured from one field log (0.40.0);
the cause of the refusal is not confirmed and nothing changed.

Machine: the reporter's HD 5570 (Redwood, `1002:68D9`) on
[GitHub issue 16](https://github.com/michaeldale/velocity9x/issues/16),
`ati` package 0.40.0 (`5977e375`), with their report of Quake 2 OpenGL
drawing dark, with models missing and textures corrupt. Files: the
reporter's `All Tests.zip` (attached to the issue, 2026-11-10), `Open GL
test\V9XGL.LOG`, Quake 2 session `pid=FFE1F985` (2026-11-10 01:28).
Kept outside the repository, as their first files were.

## What the log shows

At the session's end:

```
paths batches/triangles untextured=493/1595 hw=73046/2388838 cpu=0/0
      cpu-nonsquare=36952/1716466 hw-refused=0/0 ...
counters hwtex live=0 creates=75533 create-failed=39545 evictions=35984
      uploads=37217 partial=628 upload-kb=958492 squared=0 ...
      failed r4=36952 ...
```

- 36,952 batches, 1,716,466 of 4,106,899 triangles (42 %), took the
  `cpu-nonsquare` path, and exactly as many draws failed with result 4,
  `V9X_R3D_RESULT_UNSUPPORTED` (`include\velocity9x\r3d_abi.h:78`). Those
  triangles were not drawn.
- They took that path because their texture could not be made a hardware
  texture. The log's last refusals are all the same request:
  `hwtex create 00400010 levels=7 hr=80070057` - a 64x16 texture with a
  seven-level chain, refused with `E_INVALIDARG` by `CreateSurface`
  (`v9x_gl_hwtex_create`, `src\opengl\gl_surface.c:509-543`).
- 39,545 creates failed and were retried, with 958 MB of uploads and
  35,984 evictions in one session.

Missing models and corrupt surfaces fit draws that are dropped. The dark
picture is not explained by this.

## Why non-square, and why only some engines

OpenGL's mip chain for 64x16 runs to 1x1: seven levels. The ICD asks for
that many. An engine that samples only squares
(`V9X_R3D_ABI_HWTEX_SQUARE`) never asks: the ICD squares the image first.
Evergreen and later take non-square textures, so they get the full
chain.

## What is not known

Our R3E (Mullins, `ati`, current `main`) made the same kind of texture on
2026-11-11: V9XGLP's 64x16 seven-level test drew correctly on the engine
with `create-failed=0` and `cpu-nonsquare=0`
(`../probe/0.46.0-release-checks-2026-11-11/jjn1zz/`). The ICD code is the
same as 0.40.0's. So the refusal depends on something that differs
between the two machines. The DirectX runtime is the first candidate: the
reporter was told DirectX 7 was not needed, so their runtime may be
older, and an older DirectDraw may refuse a chain that outlasts the
shorter side. That was not checked: the reporter's `DDRAW.DLL` version
would settle it. The HAL's own refusal is the other candidate.

Either way the ICD could ask for no more levels than the shorter side
allows, or retry with fewer, and then draws would not depend on it.

## Reproduction attempt, 2026-11-11

No Evergreen machine was available. M-BXMFA8, the HD 7470 (Caicos) on
record, now holds an NVIDIA GeForce 210 (`10DE:0A65`) and runs the `vbe`
package. The `ati` 0.46.0 set swapped in refused it
(`Stage=fail-hardware-present`, boot 79), which left `BitsPerPixel=4`.
The `vbe` files and 1024x768x16 were put back (boot 81, `enable-ok`).
M-D7TFW4 (Trinity) was offline. The R3E has the same `DDRAW.DLL` size
(359,424 bytes) as M-BXMFA8 and creates the chain. The reporter's
`DDRAW.DLL` version is the next thing to ask for.
