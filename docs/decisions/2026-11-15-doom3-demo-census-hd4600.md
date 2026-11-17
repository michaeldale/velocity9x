# Doom 3 demo on the HD 4600: two exe patches get it to R_InitOpenGL, and opengl32 then picks its generic pixel format over the ICD

Date: 2026-11-15

Status: measured on the i5-4570 (HD 4600, W9QI5A83, 10.0.1.254), boot
268, on the intel-gma set already installed (nothing of ours changed).
Desktop 800x600 at 16 bpp, and at 32 bpp for the second run. Step 0 of
[`../plans/opengl-doom3-arb-path.md`](../plans/opengl-doom3-arb-path.md).

Evidence: the two `qconsole.log` files and `V9XGL.LOG` are quoted
below. The logs themselves stay out of the repository: the console log
names the install path and nothing else of interest, and is reproducible
from the steps here.

## What was installed

- Doom 3 demo, archive.org item `Doom3_201405`, `Doom 3.exe`, 483,603,646
  bytes, MD5 `5172537e1ea114ec857766f6af06a211`. It is a Wise installer.
  Michael accepted id's demo licence in chat.
- Installed to `C:\Program Files\Doom 3 Demo`: `Doom3.exe` 1.0.0.1
  (5,099,520 bytes, dated 09-10-2004), `gamex86.dll`, and
  `demo\demo00.pk4` (483,534,533 bytes, 12,234 files).
- Run from `C:\`, the installer stops at its last step with "The file
  'C:\\d3demo.ini' could not be opened". It builds the path from its own
  folder plus a backslash. Every game file had already been copied
  (`INSTALL.LOG`); rollback was declined. Run it from a subfolder next
  time.
- The original `Doom3.exe` is untouched. The patched copies sit beside it
  as `DOOM3P.EXE` and `DOOM3Q.EXE`.

## Patch 1: GlobalMemoryStatusEx (the guide's patch, at a different offset)

The demo imports four `...Ex` functions from KERNEL32. Of those, Windows
98 lacks only `GlobalMemoryStatusEx`. The guide zeroes the "Ex" of that
import name, so the loader binds `GlobalMemoryStatus` instead. In the
demo the name sits at file offset **0x324D0E** (the retail exe in the
guide: 0x374F1E). `DOOM3P.EXE` differs from the original in those two
bytes only.

The game still passes a MEMORYSTATUSEX, so `GlobalMemoryStatus` fills
the old 32-byte layout into the 64-bit fields. On this machine
`ullTotalPhys` comes out as TotalPhys plus AvailPhys shifted up 32 bits,
and the console prints:

```
-16775168 MB of System memory
...
This system qualifies for Low quality.
```

That matches the arithmetic exactly: TotalPhys and AvailPhys are both
0x7FFFF000 here, and the low 32 bits of the sum divided by 1 MB, rounded
up to 16, give -16,775,168. The guide's machine had 4 GB and
different numbers. The game goes on regardless, but sizing that reads
system RAM is reading garbage. A correct fix needs a real
`GlobalMemoryStatusEx` (a shim), not a renamed import. Not done.

## Patch 2: idMatX::Test faults at startup

`DOOM3P.EXE` faulted within two seconds, before `qconsole.log` was
opened:

```
DOOM3P caused an invalid page fault in module DOOM3P.EXE at 0177:005e8693.
EAX=00000001 EBX=00000000 ECX=00000000 ESI=00000000 EDI=00000000
EBP=0304f098 ESP=0304f02c
```

The fault and its stack were the same on a second run with
`+set com_forceGenericSIMD 1`. That cvar is not read until later in
startup, so this does not rule SIMD out.

Ghidra's walk up the call graph (host, headless):

- The fault is at `005E8693`, `mov edx,[ebx]` with ebx = 0, in the
  routine called from `005F8359`.
- `005F8240` is the caller. It allocates a vector on the stack with
  `_alloca`, calls through the SIMD processor's table (`+0xA4`), and
  then calls `005E8690` with an object pointer read back from
  `[ebp-10h]`. The function starts by dereferencing that same object,
  so it was not null on entry: something overwrote the local.
- Its only caller is `005F8900`, which references every
  "idMatX::...failed" string, i.e. `idMatX::Test()`. That in turn is
  called from idLib's init (`006044D0`, followed by
  `--- Common Initialization Complete ---`). id's GPL release of the
  engine does not run this test; this demo build does.

The routine with the fault is presumably the `LDLT_UpdateDecrement`
step, from its shape and its place in the test. That is not confirmed.
**Why** the local is overwritten on Windows 98 is not known. The same
exe runs on XP, so something Win98-specific is involved. Candidates not
measured: the `_alloca` probe, x87 state at process start, or a stack
overrun the OS lays out differently. idMatX's LDLT and LCP code is what
Doom 3's physics uses, so this may come back in play rather than at
startup.

`DOOM3Q.EXE` is `DOOM3P.EXE` plus five NOPs over the `call 005F8900` at
VA 0x6044E9 (file offset 0x2044E9). With the test skipped, startup goes
through the filesystem, decls, sound and input:

```
Running in restricted demo mode.
using ARB renderSystem
	0.00 GHz CPU
	64 MB of Video memory on an optimal video architecture
```

## Run at 16 bpp

```
Windowed mode requires 32 bit desktop depth
...calling CDS: ok
...created window @ 0,0 (640x480)
...PIXELFORMAT 5 selected
...creating GL context: failed
```

## Run at 32 bpp (windowed, 640x480, r_shadows 0)

```
...PIXELFORMAT 5 selected
...creating GL context: succeeded
X..GL_ARB_multitexture not found
X..GL_ARB_texture_env_combine not found
X..GL_ARB_texture_cube_map not found
X..GL_ARB_texture_env_dot3 not found
X..GL_ARB_texture_env_add not found
  (and 15 more, every one not found)
ERROR: The current video card / driver combination does not support the necessary features.
```

The full list Doom 3 checks, in its order:
- multitexture, env_combine, cube_map, env_dot3, env_add
- non_power_of_two, texture_compression, EXT filter_anisotropic,
  EXT texture_lod, 1.4 texture_lod_bias
- EXT shared_texture_palette, EXT texture3D, EXT stencil_wrap
- NV register_combiners, EXT stencil_two_side, ATI separate_stencil,
  ATI fragment_shader
- ARB vertex_buffer_object, ARB vertex_program, ARB fragment_program

Its `wglGetProcAddress` asks for `wglGetExtensionsStringARB`,
`wglChoosePixelFormatARB` and the pbuffer set, and finds none of them.

**`GL_ARB_multitexture not found` means the context is not ours.** Our
ICD offers that extension on this engine. `V9XGL.LOG` for the Doom 3
process (pid FFFCDD1F) has only `DrvDescribePixelFormat index=1 bytes=0
-> count=2`, over and over. There is no `DrvSetPixelFormat` and no
`DrvCreateLayerContext`. OPENGL32 chose its own generic format 5 over
our two.

Doom 3 asks `ChoosePixelFormat` for 32 colour, 8 alpha, 24 depth and 8
stencil bits (id's GPL source, `GLW_ChoosePixelFormat`; the request
itself was not captured). Our formats offer XRGB8888 with no alpha and
no stencil (`v9x_gl_describe`). The generic formats have alpha and
stencil, and win.

## What this changes in the plan

- **A pixel format with 8 alpha and 8 stencil bits comes first.**
  Without it, none of the extension work is ever seen by Doom 3. The
  ICD can't claim bits it does not draw, so destination alpha and
  stencil are both needed before the format is honest.
  - Interim option: measure with a format that claims stencil while the
    stencil test passes everything (`r_shadows 0` never draws a
    volume). That is a measurement build only, not to ship.
- Patch 2 is a workaround, not a fix. Its cause is open.

## Machine state after

- Desktop back to 800x600x16 (V9XMSW).
- `DOOM3Q.EXE` killed after its error box (it does not close on
  WM_CLOSE).
- Installed driver files unchanged.
- Left on the machine:
  - `C:\D3DEMO.EXE`
  - the install
  - `DOOM3P.EXE` and `DOOM3Q.EXE`
  - `demo\qconsole.log` and `demo\config.spec`
