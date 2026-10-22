# OpenGL at Direct3D's speed

Date: 2026-10-22. Status: proposed, nothing measured under this plan yet.
Michael's request: a large release whose subject is OpenGL performance,
with Direct3D's frame rate as the bar.

## The gap, as recorded

Half-Life 1.1.1.0 draws the same demo through either API, so `timedemo
mwd5` at 640x480 is the comparison. These figures come from different
versions and boots; Phase 0 replaces them with same-boot pairs.

| Machine | CPU | Direct3D | OpenGL | Record |
|---|---|---|---|---|
| HD 4600 | i5-4570 | 79.3 fps (2026-10-20) | 67.7 fps (2026-10-18, before the pipelined engine; not rerun) | [D3D](../decisions/2026-10-20-haswell-pipelined-gdi-rule.md), [GL](../decisions/2026-10-18-haswell-render-interface-step4.md) |
| GMA 950 (netbook) | Atom N280 | 46.0 fps | 20.8 fps (0.19.0); about 17 on 0.24.0 and 0.25.0 | [0.19.0](../decisions/2026-10-18-netbook-gma950-0.19.0.md), [0.25.0](../decisions/2026-10-22-netbook-hl1-arb-vs-sgis.md) |
| Rage Mobility-M (Gateway) | ~450 MHz PII-class | 13.07 fps | 6.65 fps | [2026-10-01](../decisions/2026-10-01-hl1-mwd5-on-the-gateway.md) |
| Millennium II (A8U4I5) | P3 | 8.49 fps | about 11.6 fps | [plan](matrox-mga2164w-render-interface.md) |

What the records say about why:

- **Where the chip is the limit, OpenGL is not behind.** The Millennium
  II's OpenGL is ahead of its Direct3D; its engine is the limit
  ([parked plan](matrox-mga2164w-performance-parked.md)). The HD 4600's
  two figures straddle the pipelined engine (2026-10-20), which sped up
  Direct3D from 60.5 to 79.3 fps; whether OpenGL gained with it is not
  known.
- **On slow CPUs it is half.** On the netbook in 2026-10-01, most of the
  OpenGL frame was outside the HAL
  ([CPU costs](../decisions/2026-10-01-hl1-opengl-icd-cpu-costs.md)).
  `glVertex*` alone cost 2.3 µs a vertex in Quake 2, cut to 1.86 µs
  ([vertex path](../decisions/2026-10-01-gl-vertex-path-profile-quake2-netbook.md)).
  Under Direct3D that work is the game's and Microsoft's runtime's, not
  ours.
- **Vertex arrays go through the immediate-mode path.** `glDrawElements`
  calls `v9x_gl_arrays_element` once per index (`gl_varray.c`), so a
  vertex shared by several triangles is fetched, transformed and tested
  once per use. Quake 3 draws this way, and its census asked for
  `GL_EXT_compiled_vertex_array`, which exists to let a driver keep
  transformed vertices across draws
  ([issue 11 record](../issues/2026-10-22-github-11-quake3-glitches-intel.md)).
  How much this costs has not been measured.
- **The netbook lost about a fifth of its OpenGL rate between 0.19.0
  and 0.24.0**, unexplained.

## Target

Proposed, to be confirmed after Phase 0:

- On the netbook, the Gateway and A8U4I5: Half-Life `mwd5` through
  OpenGL at **80 % or more of Direct3D**, same boot, best of three.
  Parity is not promised: on these CPUs Direct3D's transform is
  Microsoft's tuned code.
- On every machine: no OpenGL title slower than on 0.25.0, and nothing
  drawn differently. V9XGLP passes as it does today, and the golden
  pipeline hash moves only where a change is meant to alter output.
- Quake 3 `demo001` and Quake 2 recorded at every step. Neither has a
  Direct3D twin, so they are tracked, not compared with D3D.

## Phases

### 0. Baselines, same boot

On each machine: `mwd5` three times through Direct3D, then three through
OpenGL, in one boot, with the ICD's TSC buckets and the HAL's
submission profile both on. Quake 3 `demo001` and Quake 2 `timedemo`
where installed. One record per machine, giving:

- the ratio;
- OpenGL's frame split into the ICD front end (transform, clip, assemble,
  window), the batch sink, the render interface's draw, the HAL and the
  engine wait;
- Direct3D's HAL share, for the same split from the other side.

Gate: the profile says where the time is. If it is not in the ICD, the
phases below change before any code does.

### 1. The 0.19.0 to 0.24.0 drop on the netbook

Bisect by whole driver sets, not the ICD alone: render ABI 6 came in
between, so an older ICD does not run on a newer HAL. A record with the
commit that lost the rate and why; fix it if it is ours to fix.

### 2. Vertex arrays: one transform per vertex per draw

- `glDrawElements` and `glDrawArrays` transform and clip-test each array
  element once, into a per-draw vertex cache, and assemble triangles from
  the cache by index. Fallback to the element path where the arrays can
  change mid-draw (none can, between Begin and End of one call).
- `GL_EXT_compiled_vertex_array`: `glLockArraysEXT` and
  `glUnlockArraysEXT`. Transformed vertices are kept while the arrays are
  locked and the matrices unchanged, so Quake 3's passes over the same
  geometry transform it once.
- **Host tests first:** the cached path's sunk triangles are
  byte-identical to the element path's over the golden pipeline's
  scenes, drawn as arrays; lock and unlock state and errors as the
  extension specifies.
- Gate: Quake 3 `demo001` on the netbook and the HD 4600, and V9XGLP's
  array sections.

### 3. The immediate-mode vertex

Half-Life and Quake 2 draw in immediate mode. Phase 0's split decides
which step to work on: the inside test and clipping cost more than the
transform in 2026-10's profile. Each change byte-identical on the golden
pipeline, measured per vertex before and after.

### 4. Per batch and per state change

If Phase 0 shows the sink, describe or interface cost per batch matters:
fewer, larger batches, and state compared rather than re-described. Same
rule: identical output, measured.

### 5. Write-combining the Intel aperture

[Stage B](write-combining-stage-b.md), parked since 2026-10-02: the
uncached ring write was 9.1 % of Half-Life Direct3D and 5.6 % of Quake 2
on the netbook. It helps both APIs, so it does not close the gap. It is
in this release only if its own open decisions are taken.

## Gate for the release

Phase 0's procedure rerun on every machine against the shipped build:
the ratios, every recorded title at or above 0.25.0, V9XGLP passing,
and the frames of Half-Life, Quake 2 and Quake 3 looked at.

## Decisions owed

- The 80 % target, or another, once Phase 0's ratios are in.
- Which machines gate: the netbook, the Gateway and A8U4I5 are proposed.
  The netbook's current address is not known (10.0.1.254 is now the
  HD 4600).
- Whether SIMD is allowed in the ICD. The front end is x87 and C89 today.
  Whether Windows 98 saves SSE state across task switches has not been
  checked on our machines; nothing here assumes it does.

## Not in scope

Hardware transform and lighting (no chip here has it). OpenGL lighting,
display lists and the rest of the requirements inventory's missing
entry points. `GL_EXT_texture_env_add`: it needs a render interface
change, and its gain is unmeasured.
