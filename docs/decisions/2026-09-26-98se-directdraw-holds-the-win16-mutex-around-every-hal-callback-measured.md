# DirectDraw 6.1a holds the Win16 mutex around every HAL callback measured on 98SE, at depth 1 everywhere and depth 2 inside Lock

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`, Phase 0.2 (second half)
Guest: 86Box `Win86SE` (port 9869), Windows 98 SE, DDRAW.DLL 4.06.03.0518,
S3 ViRGE/DX in hardware Direct3D mode, 1024x768x16
Instrument: `src/display32/win16lock.c`, resolving `_ConfirmWin16Lock`
(KERNEL32 #96) through the export walk in `src/common/pe_export.c` at
DriverInit and sampling it at nine HAL entry points, read back with V9XTRACE
Evidence: `2026-09-26-98se-win16-callbacks-run1-V9XSNA7.ini` (boot 631,
first instrument), `2026-09-26-98se-win16-callbacks-run2-V9XSNA7.ini`
(boot 632, refined instrument)
Workload: `V9XDDP.EXE`, the DirectDraw/Direct3D probe, one full run
(`Result=COMPLETE`), which creates five Direct3D contexts, three render
targets, 41 textures and issues 587 `DrawPrimitives` and 4
`DrawOnePrimitive` calls, plus the presentation rungs

## Why

Since the first Direct3D engine the HAL has relied on DirectDraw holding
the Win16 mutex around its callbacks (`d3d_i9xx.c:2010`,
`docs/plans/gdi-acceleration.md:29`), and the OpenGL plan's render interface
is designed around taking that same mutex from an ICD. Nothing had measured
the assumption. The calibration probe of the same day established that
`_ConfirmWin16Lock()` can be called safely and answers non-zero only when
the calling thread holds the mutex.

## Measured

**Run 2** (the instrument as it now stands; `Win16Resolved=0x80FF`, every
step and all four ordinals resolved through the walk, and bit 0x100 clear:
`GetProcAddress` by ordinal fails from the HAL's process too):

| Entry point | Calls | Held (non-zero) | Unheld (zero) | Deepest |
|---|---|---|---|---|
| `V9xHalBlt` | 18 | 18 | 0 | 1 |
| `V9xHalLock` | 1,112 | 1,112 | 0 | **2** |
| `V9xHalCreateSurface` | 64 | 64 | 0 | 1 |
| `V9xHalDestroySurface` | 74 | 74 | 0 | 1 |
| `V9xHalFlip` | 69,813 | 69,813 | 0 | 1 |
| `V9xD3dDrawPrimitives` | 587 | 587 | 0 | **2** |
| `V9xD3dDrawOnePrimitive` | 4 | 4 | 0 | 1 |
| `V9xD3dDrawOneIndexedPrimitive` | 0 | - | - | - |
| `V9xD3dRenderPrimitive` | 0 | - | - | - |

The last answer at every sampled site was 1, except Lock, where it was 2.

**Run 1** is the same workload under the first instrument, which counted an
answer of exactly 1 as "held". It reported Lock as 0 of 1,112 held with a
last answer of `0x00000002`, and the merged Direct3D draw site as 586 of
591. That is what showed the answer to be a **recursion depth**, not a
boolean: DirectDraw enters the mutex once around the application's call
and again inside its Lock path, and some `DrawPrimitives` arrive at depth 2
as well. Run 2 counts zero and non-zero separately and keeps the deepest
answer; the 5 draws of run 1 were depth-2 draws, not unheld ones.

Run 1 also carried a fault of its own: the first HAL build tested the whole
of KERNEL32's `SizeOfImage` with `IsBadReadPtr` and stopped at the headers
step (`Win16Resolved=0x8001`) with every counter zero, because that mapping
has a page inside the image the test refuses. The instrument now tests only
the 40-byte export directory and the function table, which is why
`v9x_pe_export_table` exists.

## What this settles

- **On 98SE with DirectX 6.1a, every HAL callback this workload exercises
  runs with the calling thread holding the Win16 mutex.** The tree's
  assumption is measured, for Blt, Lock, CreateSurface, DestroySurface,
  Flip, DrawPrimitives and DrawOnePrimitive.
- **The answer to `_ConfirmWin16Lock` is the depth.** A reader must test
  for non-zero. `V9xHalLock` always sees 2; `DrawPrimitives` sees 1 or 2.
- **The OpenGL plan's lock design is consistent with what DirectDraw
  does:** an ICD entering the mutex through #97 around a render call takes
  the same lock, on the same thread discipline, that DirectDraw takes
  around these callbacks. Re-entry from a DirectDraw callback into the HAL
  export would nest rather than deadlock, as the depth-2 Lock shows the
  runtime already does.
- **The export walk resolves from inside the HAL** (`0x80FF`), so the
  Phase 3 route needs no `GetProcAddress`.

## What this does not establish

- `DrawOneIndexedPrimitive` and `RenderPrimitive` (execute buffers): not
  exercised by this probe. A DirectX 5 execute-buffer title or a strip/
  indexed workload (3DMark 99, Final Reality) would sample them.
- Other threads: the probe is single-threaded. A runtime that flushed
  Direct3D from a worker thread would show as unheld or as a different
  owner; nothing here rules that out for other applications.
- Windows ME's DirectX 7 runtime, original Windows 98's DirectX 5, or any
  Windows 95.
- Hold times. The instrument counts; it does not time.

## Standing

Phase 0.2 is measured on 98SE for this workload. The instrument stays in
the HAL: it costs one call per callback into a function that returns a
number, and the counters are the evidence the next runtime or title is
checked against. The remaining Phase 0 measurements on this guest are 0.3
(clipped Blt), 0.4 (INT to LCL from an application), 0.5 (mixed-engine
colour and Z) and 0.9 (the minimal ICD probe).
