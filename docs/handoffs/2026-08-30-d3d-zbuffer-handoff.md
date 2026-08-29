# ViRGE Z-buffer handoff — implemented, host-tested, never exercised on a guest

Date: 2026-08-30
Branch: `d3d-zbuffer`, three commits ahead of `main`, **unpushed**
Plan: [`docs/plans/final-reality-101-hardware.md`](../plans/final-reality-101-hardware.md) lines 122-131

The driver has advertised depth testing it did not implement since the first
D3D work: `D3DPRASTERCAPS_ZTEST`, all eight `dwZCmpCaps`,
`dwDeviceZBufferBitDepth = DDBD_16`, `DDSCAPS_ZBUFFER`, and a fully validated
attached depth surface — followed by an engine that wrote `Z_BASE = 0`,
`Z_STRIDE = width * 2`, set no depth bits in the command word, and never read
`context->zbuffer`.

That is now implemented. **Whether it works is unknown**, and this document
exists mostly to say why, because the reason is not in the depth code.

## Read this before anything else

**Nothing in this branch is evidence that depth testing works.** The depth
registers, the command word's compare and update fields, and the 1.31 clamp
have never executed on any guest. Two attempts at a pixel test both failed to
reach the driver at all. Do not merge this believing it is validated.

## What is genuinely proven

**The 1.31 conversion, by a test written first and watched failing.**
`tests/host/test_d3d_zfixed.c` was red before the clamp existed, on exactly
the case that motivates it:

```
FAIL test_d3d_zfixed.c:55: depth(1.000000) = -2147483648, negative
FAIL test_d3d_zfixed.c:61: depth(1.000000) is the x87 indefinite
```

`sz = 1.0` is ordinary geometry — a cleared depth buffer's far plane, any
unprojected background quad — and it scales to exactly 2^31. x87 `fistp`
stores `0x80000000` for out-of-range operands of *either* sign, and 86Box
forms the pixel depth as `(start << 1) >> 16`, so `0x80000000 << 1` is zero:
the far plane becomes the near plane and the background occludes the scene.
The clamp is in the float domain because a post-conversion clamp reads the
indefinite as a large negative and clamps it to the wrong end.

**The register facts, from two independent sources that agree.** The Windows
98 DDK's own ViRGE driver at `C:\98DDK\src\display\mini\s3v\` (`VIRGE1.H`,
`GENTRI.C`, `D3DRENDR.C`, `D3DSTATE.C`, `D3DCB2.C`) and 86Box's model, in the
tree at `build/reference-vid_s3_virge.c`. Compare function at command bits
22:20, update enable at 23, buffer mode at 25:24; `dZdX 0xb554`,
`dZdY 0xb558`, `ZS02 0xb55c`. The compare encoding is **not** the D3DCMP
order — `LESS` is 4, `GREATER` is 1, six of eight differ from `func - 1` — so
it is a table, and its default arm is `ALWAYS` because zero is the encoding
for `NEVER` and a fallthrough would discard every pixel silently.

**The surface plumbing, measured on the guest.** A 16-bit depth surface
allocates in video memory — `D3DZSurfaceHr=0`, pitch 128, caps carry
`ZBUFFER|VIDEOMEMORY` and not `SYSTEMMEMORY` — attaches to a render target
(`D3DZAttachHr=0`), and the driver's own validation accepts it
(`D3DZDeviceHr=0`).

**The existing baseline is intact**: `D3DTrianglePixelRaw=31744`,
`Tex4444Raw=992`, with the deployed `V9XHAL.DLL` hashed against the build and
the `Build=` key checked.

## The blocker, and it is not in the depth code

Two probe designs tried to exercise depth. Both produced `S_OK` from every
call and reached the driver on none of them. The driver's own counters say
why. Across **every** run, including ones whose pixels are correct:

```
CountD3dContextCreate=2   CountD3dRenderState=11
CountD3dRenderPrimitive=8 CountD3dTextureCreate=3
```

There is no `CountD3dSetRenderTarget`, no `CountD3dDrawPrimitives`, no
`CountD3dDrawOnePrimitive`. Absent means zero, in runs that render correctly.

**This runtime drives the driver exclusively through the legacy
execute-buffer callbacks.** `IDirect3DDevice2::SetRenderTarget` never reaches
`V9xD3dSetRenderTarget`. `DrawPrimitive` is decomposed by the runtime into
`RenderPrimitive` calls rather than reaching the `DrawPrimitive` family. Both
return `S_OK` regardless, which is what made two attempts look like driver
faults when they were not.

The consequence is the useful part:

> The only route by which this driver can be told about a depth surface is
> `lpDDSZ` in `D3DHAL_CONTEXTCREATEDATA`, at context creation. There is no
> after-the-fact attachment on this path.

## The next thing to pull

The first probe design did the right thing architecturally — create target,
create depth surface, `AddAttachedSurface`, *then* `CreateDevice` — and
`CreateDevice` returned `S_OK` while creating **no driver context at all**
(`CountD3dContextCreate` stayed at 2, which is the legacy Direct3D 1
QueryInterface device plus the main device). A HAL device that creates no
context is the contradiction to resolve. Start there, not in the engine.

Suggested order:

1. Instrument `V9xD3dContextCreate` to record `lpDDSZ` and the surface it
   resolves to, so "the runtime never passed a depth surface" and "the driver
   rejected it" stop looking alike.
2. Re-run the *first* design (private target + depth + device, attached before
   `CreateDevice`) with that instrumentation. It is preserved in the history
   of `tools/diag/ddraw_probe_win32.c` at commit `a87677b`.
3. If the runtime still creates no context, the question moves to which device
   `CreateDevice` actually returned — enumerate the device GUID it hands back
   rather than trusting the one requested.

## Traps already paid for — do not re-pay them

**Pick the control by what it does, not where it sits.** Diagnosing the
regression below took four guest bisects that all pointed at the headers,
which seemed absurd. They were believable only because the first
"known-good" control was built from `764ed5a` — the commit *before* the
publish fix, so a known-**broken** build. Rebuilt from `91bac19` it renders
correctly, repeatably, before and after the failing runs.

**The gate's three hygiene rules are load-bearing** (see
[`2026-08-29-d3d-core-engine-split.md`](../decisions/2026-08-29-d3d-core-engine-split.md)):
delete `C:\V9XDIAG\V9XDD.INI` before the run, check the `exec` exit code, and
require the `Build=` key to match. The handoff of 2026-08-13 documents the
result at `C:\V9XDD.INI`; that path is stale and a file still sits there from
an old build, ready to be mistaken for a result.

**Positional initialisers over a struct of arithmetic types.**
`V9X_D3D_ENGINE_LIMITS` is initialised positionally — C89 has no designated
form. `depth_bits_per_pixel` was first added *above* `coordinate_limit` while
its value was appended at the *end* of the initialiser, so the two swapped
silently: `coordinate_limit` became `16.0f`, the software clipper refused
every vertex more than sixteen pixels from the origin, and **all** Direct3D
rendering went black with every HRESULT still reporting success. The struct
now carries an append-only note recording this. Open Watcom says nothing:
the count matches and both members are arithmetic.

**Linking `d3d_zfixed.c` into the HAL costs nothing.** Builds with and
without it produce identical image sizes — the linker discards it when
unreferenced. A +512 byte difference chased for a while was entirely the
larger context array.

## Open, unexplained

`D3DTrilinearRaw` read **960** on the final run where every previous run read
**495**. It is measured before the depth block and on code that run did not
touch, so it is either flaky on this guest or disturbed by something earlier
in the probe. **It needs a repeat run before it is called either.** If it is
reproducible it is a regression and this branch should not merge until it is
understood.

## What is where

| Commit | |
|---|---|
| `a87677b` | The implementation. Its message says "not yet run on a guest" — true when written, superseded by the two below. |
| `8856722` | The struct-field fix and its evidence. |
| `635002d` | Ladders moved onto the working device; the execute-buffer finding. |

Files: `src/display32/d3d/d3d_zfixed.{c,h}` (the clamp, host-compiled),
`tests/host/test_d3d_zfixed.c`, the depth fields and validation in
`d3d_core.c` / `d3d_internal.h`, the registers, compare table and command
word in `d3d_virge.c` / `ddhal_internal.h`, `dwZBufferBitDepths` in
`ddhal_core.c`, and the ladders in `tools/diag/ddraw_probe_win32.c`.

Guest: `Win86SE`, 86Box, agent port 9869. Controller at
`C:\everything\claude\personal\v9x-remote-agent\scripts\v9xctl.ps1`;
`V9X_AGENT_CTL` is **not** set in the environment.

`run-checks` is green. Nothing is pushed.

## Deferred, deliberately

- **`DDBLT_DEPTHFILL`.** No depth-fill path exists in `src/`; DirectDraw
  emulates the clear on the CPU, so correctness does not depend on it — but a
  per-frame software depth clear will show in Final Reality's polygon rate.
- **The `DEST_BASE` 8-byte alignment hole**, same shape as the `Z_BASE` one
  that this branch closed, and predating it.
- **Final Reality re-run** (step 4 of the plan of record). Blocked on the
  depth path working, and separately on the fact that the plan records FR's
  *results* but not the *procedure* for driving it.
