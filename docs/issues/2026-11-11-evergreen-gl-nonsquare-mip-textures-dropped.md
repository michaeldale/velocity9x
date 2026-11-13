# Evergreen OpenGL: a draw whose non-square mipmapped texture cannot be created is dropped (Quake 2 on the HD 5570 lost 42 % of its triangles)

Date: 2026-11-11. Status: open. Measured from one field log (0.40.0);
the cause of the refusal is not confirmed and nothing changed.
Update 2026-11-11: fixed on `main` (`aaf42da9`, not yet released); the
reporter's run of that change as issue16-test1 created every texture and
dropped no draws. Their DirectX is 6.1a. The Quake 2 crosshair is
missing, cause unknown.

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

## Test build issue16-test1, 2026-11-11

`v9x_gl_hw_texture` (`src\opengl\gl_icd.c`) now asks for no more levels
than a non-square texture's shorter side allows (five for 64x16). On the
R3E, the full `ati` test set (boot 34) and the test `V9XGL.DLL` alone over
the released 0.46.0 files each ran V9XGLP with every result equal to the
0.46.0 release check, including the non-square mip checks, and
`create-failed=0`. That shows no regression where the old request worked;
it cannot show the fix, since the R3E never refused. The R3E was put back
on its 0.46.0 files (boot 35, hashes checked).

`velocity9x-issue16-test1.zip` (`V9XGL.DLL` and a README, SHA-256
`f724bf09...d538`) was attached to
[issue 16](https://github.com/michaeldale/velocity9x/issues/16#issuecomment-6784580420)
with a request for a Quake 2 run, `V9XGL.LOG` and the `DDRAW.DLL`
version.

## Field result of issue16-test1, 2026-11-11

The reporter's
[reply](https://github.com/michaeldale/velocity9x/issues/16#issuecomment-6787434783)
attached `V9XGL.zip` (their whole `V9XGL.LOG`, 26 MB, from 2026-11-07
on) and two screenshots. Kept outside the repository, as before.

The log's last process is the test DLL (`version=0.47.0 V9XGL
build=issue16-test1`, Quake 2, `pid=FFFEFB85`, 2026-11-11 00:43). At its
end:

```
paths batches/triangles untextured=11361/26367 hw=2691987/100155334
      cpu=0/0 cpu-nonsquare=0/0 hw-refused=0/0 ...
counters hwtex live=0 creates=230 create-failed=0 evictions=0
      uploads=21994 partial=21234 upload-kb=73071 squared=0
      alpha-dropped=11473 ... failed r4=0 r7=0 r8=0 other=0
tsc frames=833 wall-ms=10000 ...
```

- No create failed and no `hr=` refusal appears in the session; every
  textured batch, 100 M triangles, went to the engine. Under 0.40.0 the
  same machine sent 42 % of its triangles to `cpu-nonsquare` and dropped
  them. 230 creates against 75,533, and 73 MB of uploads against 958 MB.
- About 83 frames a second in the last 10 s window. Recorded, not
  compared.
- `alpha-dropped` counts alpha tests that could discard nothing and were
  not sent (`src\opengl\gl_icd.c:2337`); it is not a dropped draw.

`DDRAW.DLL` is 4.06.03.0518 and DxDiag reports DirectX 6.1a, the Windows
98 SE runtime. That fits the first candidate above, a DirectDraw that
refuses a chain longer than the shorter side allows where the R3E's
newer runtime accepts it. Both runtimes were not tried on one machine,
so it stays the likely reading, not a measured one.

The reporter says Quake 2's models, enemies, walls and surfaces now draw
correctly and the darkness was their monitor (HDMI range on Auto picking
Limited; forcing Full fixed it), so the dark picture above is not a
driver fault. Two things are left:

- The Quake 2 crosshair does not appear with the option on. The log has
  no failure that accounts for it; not investigated.
- Quake 1 (GLQuake) is reported much improved, with a few artefacts at
  texture seams. No GLQuake process with the test DLL is in the log, so
  that is the reporter's account only.

They also ask whether DirectX 6.1 could cause their Virtua Cop 2
Direct3D flicker. Not checked; they are leaving the machine unchanged
until asked.

## Released (2026-11-13, 0.48.0)

The reporter ran the released 0.48.0 package: textures and models draw
correctly, so the fix holds outside the test build. The crosshair is
still missing with the first two of Quake 2's three crosshairs; the
third ("Angle") draws as one small dot, which looks like a crosshair
drawn wrongly rather than not drawn. Quake 2's crosshairs are small
pictures with transparent pixels, so alpha test or very small textures
on the Evergreen GL path are the first place to look; not checked. No
log came with this report.
