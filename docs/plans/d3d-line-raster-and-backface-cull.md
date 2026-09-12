# Direct3D gaps: backface culling and line rasterisation

> Status: Open. The two capabilities remain unimplemented compatibility gaps.

Two capabilities the HAL does not have. Neither is a defect — nothing is
broken, and nothing regressed. They came out of a caps-level comparison
against L10GL (`linuxid10t/L10GL`), a Linux-console OpenGL framework whose
primary backend is the same ViRGE/DX: its frontend carries line primitives and
cull state, ours carries neither.

Filed here rather than in `docs/issues/` because neither is a bug found on
hardware. Anything measured about the ViRGE's line engine along the way gets
its own dated record in `docs/decisions/`.

## 1. Backface culling

### Where it stands

Both engines publish `D3DPMISCCAPS_CULLNONE` and nothing else
([d3d_virge.c:1369](../../src/display32/d3d/d3d_virge.c:1369),
[d3d_soft.c:411](../../src/display32/d3d/d3d_soft.c:411)). `CULLCW` and
`CULLCCW` exist as constants
([win9x_ddraw_abi.h:468](../../include/velocity9x/win9x_ddraw_abi.h:468)) and
are used nowhere.

There is no `V9X_D3DRENDERSTATE_CULLMODE` define at all, so the state is not
in the switch in
[`V9xD3dRenderState`](../../src/display32/d3d/d3d_core.c:998). An application
that sets a cull mode is not refused; it is ignored. Every triangle handed to
us is rasterised.

### What that costs

With Z testing on and a closed opaque mesh, back faces lose the depth compare
and the picture is right — the cost is fill rate, roughly double what the
geometry needs. Two cases are not merely slow:

- Alpha-blended geometry. A back face blends, then the front face blends over
  it. The result is wrong, not just expensive.
- Anything drawn with `ZENABLE` off in painter's order.

This is a candidate explanation for nothing currently open. It is a known
absence, not a diagnosis.

### Work

1. Define `V9X_D3DRENDERSTATE_CULLMODE` (22) and the three `D3DCULL_*`
   values; decode into the context state alongside the existing cases.
2. Signed area of the screen-space triangle, in the chip-neutral core before
   the engine op, so both backends inherit it. Pure arithmetic, no register
   access — the layering `src/common/mtrr.c` sets.
3. Host test in `tests/host/` over a fixture set: clockwise, counter-clockwise,
   zero-area, and coordinates at the 2048.0f converter limit. Degenerate
   triangles must be dropped under every mode.
4. Only then publish `CULLCW | CULLCCW` in both `describe_caps`.

### The trap

Direct3D screen space is y-down, so the sign of the cross product is inverted
relative to the y-up formula. Getting it backwards turns every mesh
inside out, and caps bits are what applications select a device on — an app
told we cull has no fallback if we cull the wrong face. The host test comes
before the caps change, not after.

### Acceptance

Host test green; then Final Reality or 3DMark 99 on the physical Trio3D/2X
with a pixel hash taken under `CULLNONE`, which must be identical to the hash
before the change.

## 2. Line rasterisation

### Where it stands

`dpcLineCaps` is left zeroed and `D3DDD_LINECAPS` unset deliberately
([d3d_virge.c:1359](../../src/display32/d3d/d3d_virge.c:1359)); the comment
records that populating it was measured against Hellbender and changed
nothing, per
[the software-fallback issue](../issues/2026-08-15-hellbender-software-fallback.md).

Both draw entry points accept `D3DPT_TRIANGLELIST` only and fail anything else
with `0x80070057` while still returning `DDHAL_DRIVER_HANDLED`
([d3d_core.c:1412](../../src/display32/d3d/d3d_core.c:1412),
[d3d_core.c:1461](../../src/display32/d3d/d3d_core.c:1461)).

**Open question, not measured:** whether `DRIVER_HANDLED` plus an error is the
right answer for a primitive we never advertised, or whether the runtime wants
`NOTHANDLED` so it can lower the line itself. Worth settling regardless of
whether lines are ever implemented, because it is the difference between an
application seeing a failure and seeing a slow path.

### What is known about the silicon, and what is not

- The ViRGE's **2D** BitBLT command set has a line command,
  `CMD_SET_COMMAND_LINE = 3 << 27`
  ([build/reference-vid_s3_virge.c:468](../../build/reference-vid_s3_virge.c:468)).
  That is an emulator's decode, not the databook. 2D means no Z buffer, no
  Gouraud interpolation along the span, no texture.
- The 3D command window this driver uses publishes triangle commands only in
  our vocabulary ([ddhal_internal.h:102](../../src/display32/ddhal_internal.h:102)).
  Whether the S3D unit has a line primitive of its own is **unknown** and
  needs the ViRGE databook.
- No line verb exists anywhere in this tree today, 2D or 3D.

Two candidate routes, neither measured: the 2D line engine, which cannot
depth-test; or two thin triangles per line through the existing S3D path,
which can.

### Work, in order

1. **Decide whether it is worth it before writing any register code.**
   Cheapest evidence: instrument the two rejection sites to log the rejected
   primitive type, then run the existing application set. If nothing asks for
   a line, close this as declined and leave the caps honest.
2. If something does ask: software path first
   ([d3d_raster.c](../../src/display32/d3d/d3d_raster.c)). It needs no
   hardware measurement and settles the state plumbing and the caps shape.
3. The thin-triangle route on the S3D after that, behind the engine op, as an
   experiment with a probe rung — not as a default.

### Acceptance

A probe rung drawing a known line set, compared against the software
rasterizer's output; a dated decision record for any hardware claim that comes
out of it.

## Priority

Below everything in [STATUS.md](../STATUS.md)'s open-work table. Culling is the
cheaper of the two and the one with a correctness argument behind it; lines
should not be started until step 1 above says an application wants them.
