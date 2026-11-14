# Advanced Pixel Shader on Intel: a missing wide-surface cap, then the GMA 950's memory

Date: 2026-11-12. Machines: the netbook (W9QI2BE2, GMA 950, 945GSE, 7.7 MB
of stolen memory), boots 311-313; the Haswell i5-4570 (W9QI5A83,
HD 4600), boot 214-215. Drivers: netbook main as built 2026-11-12 15:41
(`V9XHAL.DLL` SHA-256 831DFCEC..., equal to `build/win98se-intel-gma`);
4570 `V9XHAL.DLL` SHA-256 A8CBD313... (no local build matches; the
boot-200 intel-gma set per its machine notes). Both agents hot-updated
to v9xra 0.16.0 mid-session (no Windows restart).

Follows [`2026-11-11-gen3-texture-size-2048.md`](2026-11-11-gen3-texture-size-2048.md),
where Advanced Pixel Shader created two 2048x2048 chains on the GMA 950,
reported "Not supported by hardware" and drew nothing.

The question: does the GMA 950 need dynamic video memory (the
[GTT pool plan](../plans/intel-gen3-system-memory-textures.md)) to run
it, or does something else stop it?

## Method

The boot-307 d3d8.dll proxy, extended (version 2, session 07165792
scratchpad `d3d8log2/`). It logs resource creation, `GetDeviceCaps` on
both interfaces (full `D3DCAPS8` when it changes), `CheckDeviceType`,
`CheckDeviceFormat`, `CheckDepthStencilMatch`,
`CheckDeviceMultiSampleType`, `GetAvailableTextureMem`, `ValidateDevice`
with the stage state it judged, shader creation, failed
`Set*`/`Draw*`/`BeginScene` calls, and draw calls and primitives per
device at its last release. Copies in 3DMark's folder and `e2driver\`.
Both machines share the same system d3d8.dll (SHA-256 EFC4726B...).
3DMark 2001 SE at 640x480, 32-bit textures, 24-bit Z, Pro tests off.

## Results

| Run | Machine | Colour | Advanced Pixel Shader |
|---|---|---|---|
| boot 311 | GMA 950 | 16-bit | loading screen only (817 calls, 12,206 primitives); no call fails; overall 527 |
| boot 214 | HD 4600 | 16-bit | loading screen only (804 calls, 12,024 primitives); no call fails; overall 3927 |
| boot 313 | GMA 950 | 32-bit, Pixel Shader tests only | `CreateTexture 1024x512 usage RENDERTARGET X8R8G8B8 pool DEFAULT -> 88760827` (`D3DERR_DRIVERINTERNALERROR`), then "An error has occurred in 3DMark" |
| boot 215 | HD 4600 | 32-bit, all tests | the same call, the same `88760827`, the same error, after Game 1-4 and the other tests ran |

At 16-bit colour both chips run the same sequence call for call: every
texture (both 2048x2048x12 chains included) and every shader (vs 1.0/1.1,
ps 1.0/1.1) is created `S_OK`, `GetDeviceCaps` is read 22 times with one
answer, and the test never calls `GetAvailableTextureMem`,
`ValidateDevice` or `CheckDeviceFormat`, then quits. The format answers
the two drivers give are identical: at a 16-bit desktop the only
render-target texture either accepts is R5G6B5, at a 32-bit desktop
X8R8G8B8. The test's 32-bit run asks for an X8R8G8B8 render-target
texture, so it is read as needing one, and skipping at 16-bit colour on
any card here. Not proven: nothing names the gate.

At 32-bit colour the failing call is the same on a chip with 27 MiB of
texture memory free (`GetAvailableTextureMem` 28,311,552 on the HD 4600)
and on one with 3 MiB (3,145,728 on the GMA 950). A shortage would
answer `D3DERR_OUTOFVIDEOMEMORY` (`8876017C`), as the GMA 950 did for
the 48th 128x128 X8R8G8B8 render-target texture of Game 1 at 32-bit
colour on boot 312. `DRIVERINTERNALERROR` is a driver
fault in creating a 1024x512 X8R8G8B8 render-target texture, in code the
two Intel engines share or both copy.

The HD 4600 ran Advanced Pixel Shader on 2026-10-30 (27.5 and 35.3 fps,
[`2026-10-30-haswell-ps11-step5d-bump-formats.md`](2026-10-30-haswell-ps11-step5d-bump-formats.md),
[`2026-10-30-haswell-ps11-step6-nature.md`](2026-10-30-haswell-ps11-step6-nature.md))
at that machine's default settings, 1024x768x32 with compressed
textures. Whether the test asks for a different render-target size
there, or the fault is a regression since, is not measured.

Also seen: DOT3 is "Not supported by hardware" on both, consistent with
TextureOpCaps lacking `DOTPRODUCT3` (Gen3 `0000100F`, HD 4600
`0000104F`); on the HD 4600 at 32-bit its `ValidateDevice` calls answer
`88760819`.

## The cause: DDCAPS2_WIDESURFACES was never claimed

`ddCaps.dwCaps2` was never assigned, so DirectDraw was not told
`DDCAPS2_WIDESURFACES` (DDRAW.H:1840), and it refuses a video-memory
surface wider than the primary. 1024 is wider than 640; at 1024x768,
where the HD 4600 ran this test on 2026-10-30, it is not. The 2048-wide
16-bit level DirectDraw refused with `E_INVALIDARG` on the netbook
(2026-11-11 record, V9XTSHP) was wider than its 1024x576 primary. No
Windows 98 DDK sample sets the bit.

The dev build claims it in `ddhal_core.c` for every family (build of
2026-11-12 16:58, uncommitted, `V9XHAL.DLL` SHA-256 47232640...;
`run-checks` passed). The heap is linear, so a wide surface costs what
a tall one of the same bytes does. The 2D engines already decline a
surface not at the display pitch (the CPU path takes it), and each
Direct3D engine checks its own target pitch limit, so nothing new
reaches an engine that cannot draw it.

| Run | Machine | Build | `CreateTexture 1024x512 RENDERTARGET X8R8G8B8 DEFAULT` | Advanced Pixel Shader |
|---|---|---|---|---|
| boot 315 | GMA 950 | wide surfaces | `8876017C`, `D3DERR_OUTOFVIDEOMEMORY` | 3DMark error |
| boot 216 | HD 4600 | wide surfaces | `S_OK`, then its 1024x512 D24X8 depth surface `S_OK` | runs: 11,065 draw calls, 6,682,314 primitives |

Both runs at 640x480x32 with 32-bit textures, Pixel Shader tests only.
The HD 4600's frame rate was not read (the score page's list is not
readable through the agent), and the picture was not looked at.

## What this shows

- Dynamic video memory was not the first blocker for Advanced Pixel
  Shader: a missing DirectDraw cap was, on both Intel engines.
- With the cap, the GMA 950's blocker is memory: the same call answers
  out of video memory. At 32-bit colour it reports 3 MiB free, Game 1
  runs out of it as well, and Advanced Pixel Shader needs a 2 MiB
  render target, its depth surface, and two 21 MiB managed chains. That
  is the [GTT pool plan](../plans/intel-gen3-system-memory-textures.md)'s
  work.
- At 16-bit colour the test skips on both chips; that is unchanged by
  the cap (no 32-bit render-target texture at a 16-bit desktop).

## Not ruled out

- Whether the 16-bit skip is the render-target format or another answer
  the test holds.
- Video memory read through DirectDraw (the proxy sees Direct3D 8 only).

## Evidence

Session 07165792 scratchpad: `d3d8log2/` (proxy source and DLL);
`nb311-d3d8lg2.txt` (16-bit netbook run), `nb32b-d3d8lg2.txt` (both
netbook 32-bit attempts after line 13,619), `hsw-d3d8lg2.txt` and
`hsw32-d3d8lg2.txt` (4570 runs, 32-bit after line 15,960);
`nb311-*.png`, `hsw-*.png`, `wait-3dmark.ps1`; for the fix,
`nbwide-d3d8lg2.txt`, `hswwide-d3d8lg2.txt`, `deploy-nb.ps1` and the
replaced driver files in `nb-before-wide/` and `hsw-before-wide/`.

Left on the machines: both run the wide-surface dev build (DRV, VxD,
HAL and GL by WININIT rename). The proxy is installed in `3DMark2001
SE\` and `e2driver\` on both (`D3D8.DLL`, `D3DR.DLL`, logging to
`C:\V9XDIAG\D3D8LG2.TXT`); earlier copies on the netbook are parked as
`.DL_`, `.DL2`, `.DL3`. 3DMark 2001 SE is set to 640x480x32, 32-bit
textures, Pixel Shader tests only, on both.
