# HD 4600, UT2004 demo: a DXT5 cube map is refused at creation and the game stops at its first frame

Date: 2026-11-13. Seen on the i5-4570 (HD 4600, W9QI5A83), boots 231 and
234, on the HAL of `e9553ba8` over the boot-218 intel-gma set (main's
`746c0a42` code), DirectX 9.0c.

Status: **open**. The refused call is measured. Nothing is changed in
source. What the game draws once the cube exists is not known.

## The program

Unreal Tournament 2004 demo, build 3334 (`UT2004-NewDemo.exe`,
296,049,152 bytes, MD5 `91a884ceb079d0ba5d6b023b6cf76fc2`, from the
Internet Archive item `UnrealTournament2004Demo`), installed with its
defaults to `C:\UT2004Demo`. Its `UT2004.ini` as shipped: the Direct3D 8
renderer (`D3DDrv.D3DRenderDevice`), 800x600 full screen,
`UseHardwareTL=True`, `UseHardwareVS=True`, `UseStencil=False`,
`UseCompressedLightmaps=True`.

Run from `C:\UT2004Demo\System`:

```
UT2004.exe dm-rankin?spectatoronly=1?numbots=12?quickstart=1?attractcam=1
    -benchmark -seconds=77 -exec=..\Benchmark\Stuff\botmatchexec.txt -nosound
```

`botmatchexec.txt` is not in the demo; it holds the two console commands
`showhud` and `ship`.

## What happens

Twice in two runs, about 7 s after launch:

1. The game creates its device: 800x600, back buffer format 22
   (X8R8G8B8), depth format 77 (D24X8), at DDI 8 (`gdi2-dxversion
   00000802` in `V9XD7.LOG`).
2. It precaches DM-Rankin. The D3D8 proxy logs 2D textures in DXT1, DXT3,
   DXT5 and A8R8G8B8, 32x32 to 1024x1024 with full mip chains, every one
   created (`-> 00000000`). `V9XPS.LOG` shows them placed.
3. At the first frame, drawing a static mesh:
   `CreateCubeTexture 128 levels 8 usage 0 fmt 894720068 pool 1 ->
   8876086c`. The format is `35545844h`, DXT5; the pool is managed; the
   result is D3DERR_INVALIDCALL.
4. Unreal shows its own "Critical Error" dialog,
   `CreateCubeTexture failed(D3DERR_INVALIDCALL)`, with the history
   `FD3DTexture::Cache <- FD3DRenderInterface::CacheTexture <-
   FD3DRenderInterface::SetShaderMaterial <- ... <- RenderStaticMesh`.
   No frame is benchmarked and `Benchmark\Results` stays empty.

The game does not ask `CheckDeviceFormat` for a DXT5 cube in the proxy's
last 120 lines before the call; whether it asked earlier in the run was
not read.

## Cause

`v9x_d3d_get_driver_info2`'s GETFORMAT answer in
[d3d_core.c](../../src/display32/d3d/d3d_core.c) adds
`D3DFORMAT_OP_CUBETEXTURE` to every texture format where cubes are
claimed, except DXT1, DXT3 and DXT5: "whose cube layout nothing here has
measured". With `D3DPTEXTURECAPS_CUBEMAP` set and the operation absent
for DXT5, the runtime refuses the create itself. That the call never
reached the HAL is read from the log tails (no surface line follows the
last 2D texture), not from a trace of the create path.

The same proxy log holds 3DMark 2001 SE's earlier cubes on this machine,
`CreateCubeTexture 256 levels 1 fmt 21` (A8R8G8B8), all created.

## What is not known

- Whether a DXT cube's faces can be placed by the Haswell engine as the
  2D block trees are. Nothing has measured it.
- What follows the create. The comment at the cube caps in `d3d_core.c`
  says the fixed-function draw refuses a cube (step 5c of the DDI 8
  plan); UT2004's environment maps are fixed-function, so the next stop
  may be an undrawn mesh. Not run.
- Whether the game runs on the cards that claim no cube maps. Not run on
  any other machine.
- The OpenGL and Direct3D 9 renderers named in the demo's INI. Not run.

## Evidence

[docs/probe/hsw-ut2004-demo-2026-11-13](../probe/hsw-ut2004-demo-2026-11-13/):
`UT2004-boot231.log` (the game's log of the first run) and
`d3d8-proxy-tail-boot234.txt` (the last 120 lines of
`C:\V9XDIAG\D3D8LG3.TXT` after the second run: device creation, the
textures and the refused cube).

The D3D8 logging proxy (`d3d8log3`, measurement only) and the real
`d3d8.dll` as `D3DR.DLL` are in `C:\UT2004Demo\System` on the machine.
