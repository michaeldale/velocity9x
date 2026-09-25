# 98SE's OPENGL32 loads the ICD, lists its pixel format first, creates every context through DrvCreateLayerContext with layer 0, and calls the 336-slot table it is handed

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`, Phase 0.9 (and the reference half of 0.6)
Guest: 86Box `Win86SE` (port 9869), Windows 98 SE, OPENGL32.DLL 4.00, S3
ViRGE/DX, 1024x768x16, driver build `cb5e39a-dirty` answering the discovery
escape with "Velocity9x"
Instrument: `V9XGL.DLL` (`src/opengl/gl_icd.c`, built by
`scripts/build-opengl-icd.ps1` from the dispatch table
`scripts/lib/gl-dispatch.ps1` generates out of `src/opengl/gl_entrypoints.psd1`),
a probe ICD that renders nothing, offers one 565/16-bit-depth format, and
logs every entry point and every first call of every slot to
`C:\V9XDIAG\V9XGL.LOG`; and `V9XGLP.EXE` (`tools/diag/gl_probe_win32.c`),
a client that does GLQuake's startup through the system OPENGL32 and
reports what it got
Evidence: `2026-09-26-98se-glprobe-baseline-9869.ini` (no ICD registered),
`2026-09-26-98se-icd-run1-V9XGL.LOG` and `2026-09-26-98se-glprobe-icd-9869.ini`
(first ICD run), `2026-09-26-98se-icd-run2-V9XGL.LOG` (second),
`2026-09-26-98se-icd-run3-pipes-V9XGL.LOG` (3D Pipes)

## Why

Everything in the plan's Phase 3 rests on a contract read from ReactOS,
Mesa and vmdisp9x: that 9x OPENGL32 finds an ICD through a registry value
named by the display driver's escape, requires sixteen exports, numbers the
ICD's formats ahead of its own, hands the ICD's context a 336-entry table
and calls through it. The plan said to measure that with a disposable
probe before the render core is refactored. This is that probe.

## Measured

**Baseline, no registry value.** `V9XGLP` finds 24 formats, all
`PFD_GENERIC_FORMAT`, none `PFD_GENERIC_ACCELERATED`; GLQuake's request
(24 colour, 32 depth, double-buffered) is answered with format 3 (16
colour, 32 depth, 8 stencil); the context reports `Microsoft Corporation` /
`GDI Generic` / `1.1.0` with `GL_WIN_swap_hint GL_EXT_bgra
GL_EXT_paletted_texture`; clear and swap succeed. The generic renderer is
usable as the plan's reference on this guest.

**Run 1, `Velocity9x=V9XGL.DLL` under `OpenGLDrivers`** (a plain REG_SZ
value, written with `REGEDIT /S`). OPENGL32 loaded the DLL and called, in
order: `DrvValidateVersion(1)`, which is the `driver_version` the escape
returned; `DrvSetCallbackProcs(3, procs)`; `DrvDescribePixelFormat(1, 0,
NULL)` about fifty times for the count and `(1, 40, pfd)` three times for
the description; `DrvSetPixelFormat(hdc, 1)`; then, for
`wglCreateContext`, **`DrvCreateLayerContext(hdc, 0)` and never
`DrvCreateContext`**. The probe answered that with 0 and the application's
context failed. The client saw **25 formats, the ICD's as #1** with flags
`0x425` (`DRAW_TO_WINDOW | SUPPORT_OPENGL | DOUBLEBUFFER | SWAP_COPY`) and
neither generic flag, and `ChoosePixelFormat` chose it for GLQuake's
request over all 24 generic ones.

**Run 2, layer 0 treated as a context.** The same sequence, then
`DrvSetContext(hdc, 1, setproc)`, four `glGetString` calls through the
table's replaced slot, `glClearColor` (slot 206) and `glClear` (203)
through their stubs, **`glFinish` (216) called by OPENGL32 itself before
`DrvSwapBuffers`** and `glFlush` (217) after it, then `DrvReleaseContext`
and `DrvDeleteContext`. The client reported `Velocity9x` / `Velocity9x
probe ICD` / `1.1 Velocity9x-probe`, no errors, a successful swap and a
successful delete. Four slots were called, four times in all.

**Run 3, 3D Pipes.** The screensaver loaded the ICD, went through
`DrvValidateVersion`, `DrvSetCallbackProcs` and one description of the
format, and **created no context**: its own pixel-format request was not
matched by a 16/16 format and `ChoosePixelFormat` gave it a generic one,
which never touches the ICD. It rendered through the generic path.

## What this settles

- **The contract holds on 98SE.** Discovery by escape and registry value,
  the sixteen exports plus the two optional ones, ICD formats first, the
  table handed over at `DrvSetContext` and called through by slot: all as
  the research record inferred, now measured.
- **`DrvCreateLayerContext` is the context factory on 9x**, with layer 0
  meaning the main plane. `DrvCreateContext` must still be exported (the
  runtime looks it up) but was never called. The plan's Phase 3 context
  code treats both entries as one.
- **`DrvValidateVersion` receives the escape's `driver_version`.** The two
  numbers are one contract; the plan keeps them in one header.
- **OPENGL32 brackets `SwapBuffers` with `glFinish` before and `glFlush`
  after**, through the table. The ICD's swap can rely on `finish` having
  run; `finish` must therefore really wait.
- **The format list decides which applications reach the ICD.** One
  16-bit format was enough for GLQuake's request and not for 3D Pipes'.
  Phase 3's format list needs at least the combinations the target
  applications ask for, and Phase 0.6's reference comparison needs the
  same scene to pick the same class of format both ways.
- The generated table is right: typed stubs at the slots the reference
  headers name, and OPENGL32's own calls (`glFinish` 216, `glFlush` 217)
  landed on the slots the manifest gives those names.

## What this does not establish

- What `DrvSetCallbackProcs`'s three procedures are for, or whether
  `DrvGetProcAddress` is consulted (nothing asked for an extension).
- Anything about `DrvCopyContext`, `DrvShareLists` or the layer-plane
  entries beyond being exported.
- What 3D Pipes' request was: the probe does not log `ChoosePixelFormat`'s
  input, only the ICD's side. A second format set, or a client that logs
  its request, would say.
- Original 98, ME or 95.

## Standing

Phase 0.9 is measured on 98SE and the answer is the one the plan needs.
The probe ICD stays in the tree as `src/opengl/gl_icd.c`, the seed of the
real one: its exports, logging and generated table are the skeleton Phase
3 fills. The registry value is left set on `Win86SE`; while the ICD offers
no rendering, any application that picks its format draws nothing, which
is the reason it is not packaged.
