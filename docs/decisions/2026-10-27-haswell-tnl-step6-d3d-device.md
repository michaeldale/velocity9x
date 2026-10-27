# HD 4600: the Direct3D 7 T&L device, built and not yet offered

Date: 2026-10-27. Machine: the i5-4570 (W9QI5A83, HD 4600), boots 99 to
103, 800x600x16 desktop. The runtime is DirectX 9.0c. Step 6 of
[`../plans/hardware-tnl.md`](../plans/hardware-tnl.md); follows
[`2026-10-27-haswell-tnl-step4-lighting.md`](2026-10-27-haswell-tnl-step4-lighting.md).
Evidence:
[`../probe/hsw-4570-tnl-step6-2026-10-27/`](../probe/hsw-4570-tnl-step6-2026-10-27/).

The device draws correctly but is slower than the runtime transforming on
the CPU. It is committed with `V9X_D3D_TNL_DEVICE_OFFERED` 0 in
`d3d_core.c`: no DDI 7 process is told about it, so programs keep the HAL
device and the runtime's own T&L, as at boot 97. This reverses Decision 1
of the plan for step 6 until the device is faster (Michael, 2026-10-27).

## What changed

- **Caps, behind the switch.** A DDI 7 process, which is offered only on
  an engine that runs T&L, would get:
  - `D3DDEVCAPS_HWTRANSFORMANDLIGHT`;
  - eight active lights;
  - vertex processing caps for material sources, vertex fog,
    directional and positional lights, and the local viewer;
  - no texture generation, user clip planes or blend matrices.
- **State per context** (`d3d_tnl.c`, pure and host-tested). It keeps:
  - SETTRANSFORM's world, view and projection;
  - SETLIGHT and CREATELIGHT (32 light slots);
  - SETMATERIAL, VIEWPORTINFO and ZRANGE;
  - the T&L render states: LIGHTING, AMBIENT, SPECULARENABLE,
    LOCALVIEWER, NORMALIZENORMALS, COLORVERTEX, the four material
    sources, and vertex and table fog.
  - The walker now shows VIEWPORTINFO and ZRANGE to the DX7 sink.
  - The block is in the creating process's memory (VirtualAlloc), since
    the HAL's statics are shared.
- **Key and constants.** Direct3D's row-vector matrices become the
  block's rows, with world x view x projection to clip.
  - Lights go into eye space through the view matrix, a directional one
    as its reversed direction.
  - A spot's cone is the half-angles of theta and phi (radians).
  - A colour source the vertex lacks falls back to the material.
  - The infinite viewer is (0, 0, -1). T18 agrees with the runtime here.
  - Table fog is drawn as eye-depth fog under a W-friendly projection,
    and not at all otherwise.
- **Untransformed vertices** (D3DFVF_XYZ with an optional normal,
  colours and up to eight coordinate sets) go into a T&L batch of up to
  400 vertices and 1200 indices, one `draw_tnl` each:
  - each pool vertex once, with its normal and stage 1's pair, through a
    64-entry cache keyed by the pool address;
  - drawn at the end of each DrawPrimitives2 call, before a T&L record
    (transform, light, material, viewport) or a T&L render state that
    changes, and before a pre-transformed run, so the stream's order is
    kept.
- **What is not drawn.** Points and lines with untransformed vertices are
  counted and dropped. So is a batch the engine declines, since nothing
  transforms object-space vertices on the CPU yet.

## Correctness (boot 99, device offered)

- V9XD32: `Result=PASS`, with T12-T16 and T18 PASS and no FAIL line.
- T18 draws three scenes (directional, positional and spot lights) on the
  HAL device and on the T&L device and compares 36 samples: worst channel
  difference 2, all within tolerance. T18 found the device by its IID
  (`T18TnlDevCaps=0x0009A651`).
- 3DMark 2000 and 2001 SE ran to their scores on the T&L device with
  nothing declined (third `tnl-summary` column 0 in every context).

## Speed

| Run | T&L device (boot 99) | Segmented batch (boot 100) | CPU T&L, DDI 7 (boots 92, 97) |
|---|---|---|---|
| 3DMark 2000 Pro, default | 1558 | 1292 | 2322 |
| 3DMark 2001 SE, saved settings | 722 | 700 | 1120 |

The CPU column is from
[`2026-10-27-haswell-ddi7-default.md`](2026-10-27-haswell-ddi7-default.md).

**Batch size.** In 3DMark 2000 one context drew 855,305 batches and 83.6
million triangles, about 98 triangles a batch. Each batch builds a key,
packs up to 1440 bytes of constants and is one engine draw.

**The segmented batch (boot 100) did not help.** Following step 3b, a
batch was held across DrawPrimitives2 calls with up to 16 state segments
sharing its vertices; a world, light, material or texture change closed a
segment instead of drawing. Measured:

- about 1.9 segments a batch, and 56,709 batches with one segment only;
- 3DMark 2000 fell to 1292 and 2001 SE to 700;
- V9XD32's log ends in T13's strip stage
  (`Stage=10e-t13-volume-strip`). That may be a partial read rather than
  a fault (see below); it was not re-run.

It was reverted to the boot 99 design before this commit, since it was
slower; the log is kept as `V9XD32-boot100-segmented-stopped.txt`.

**Hypotheses for the gap, none measured yet:**

- per-batch cost (key, constants, state emission) at about 100
  triangles a batch, which the segmented build did not cut;
- the vertex cache's 64 entries, too few for 3DMark's pools, so
  vertices are converted more than once;
- the kernel cost of eight-light lighting on the EUs, never measured in
  isolation.

## With the device not offered (boots 101-103)

**A fault the boot 99 run missed.** On boots 101 and 102 the first
build with the device off stopped V9XD32 early (after T9, then after T1),
and DDHELP faulted in V9XHAL.DLL at `0177:B04224BA`,
`cmp dword [edx+14B4h],0` with EDX `02B00000`
(`ddhelp-fault-boot102.png`). That is `v9x_d3d_tnl_pending` reading a
context's T&L block, an address in V9XD32's memory, from DDHELP: a
DirectDraw call's flush reaches `v9x_d3d_dp2_draw_run` for whichever
context the batch sink names, in whichever process makes the call.

- The fault does not need the device: every DDI 7 context has a block.
- Boot 99 passed with the same code.
- The segmented build checked the process there, so this does not
  explain its T13 stop.

The fix checks the context's process before touching the block, in
`v9x_d3d_tnl_pending` and in ContextDestroy, which DDHELP also calls for
a process that has died. A batch is drawn before each DrawPrimitives2
call returns, so no other process has one to draw.

**Boot 103, with the fix:**

- V9XD32 `Result=PASS`. T18 reports `NO-TNL-DEVICE`: the runtime refuses
  the T&L device (`T18TnlDeviceHr=0x8007000E`), as it should when the
  cap is not set.
- 3DMark 2000 Pro default: 2334, against 2322 at boot 92.

**A harness trap, not a driver fault.** The agent's `exec` of V9XD32
returned after 4 seconds (`GuiWindow` true) while the program kept
running. A `get` of `V9XD32.TXT` straight after it read a partial file
that ended at T5 on boot 103; the same file read again after a second
run was complete. The boot 101 and 102 stops came with the DDHELP fault
and are real. The boot 100 stop in T13 had no fault dialog recorded and
may be this trap; it was not re-run.

## Not done

- **Texture generation and texture transforms.** The TCI flags and
  TEXTURETRANSFORMFLAGS are ignored: coordinates pass through.
- **Vertex blending.** Ignored and counted.
- **No CPU fallback.** Object-space points and lines, and anything the
  engine declines, are dropped.
- **Not run:** the DX7 SDK samples, which are not on the machine, and a
  DX7 title chosen for its T&L device.
