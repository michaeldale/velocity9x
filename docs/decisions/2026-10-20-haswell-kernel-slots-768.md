# HD 4600: kernel slots were too small for specular with fog; 768-byte slots end 3DMark 2001 SE's refused batches, and the Lobby mosaic is older than pipelining

Date: 2026-10-20. Machine: the i5-4570 with DirectX 9.0c (Dec 2005)
installed this day (it had DirectX 7), boots 45 to 50. Follows report
V9X-D9VZS6
(`docs/issues/2026-10-20-haswell-3dmark2001se-artifacts-field.md`).
Evidence:
[`../probe/hsw-4570-3dmark2001se-2026-10-20/`](../probe/hsw-4570-3dmark2001se-2026-10-20/).

## Setup

- DirectX 9.0c from `directx_dec2005_redist.exe` (`DXSETUP /silent`;
  log: "Installation succeeded need restart"). The restart was the
  agent's `reboot`, through Windows' own restart path, in 62 s. That
  is the first real Disable on a pipelined build: it did not hang. The
  engine close's return is not recorded anywhere that survives a
  reboot.
- 3DMark 2001 SE, patched to build 330. Unregistered for boots 46 to
  49, registered (Pro) for boot 50.
- Every run used the default benchmark settings: 1024x768x32,
  double-buffered, 24-bit Z, 16-bit textures (the HAL offers no 32-bit
  texture format), D3D software T&L.

## The refused batches

| Boot | Build | Refused batches | Reason |
|---|---|---|---|
| 46 | `a3825fa3-dirty` | 5,191 | 13 (`REFUSE_STATE`) |
| 47 | `1a4c72ce-dirty` with 13 split by block | 4,979 | 13, the kernel itself: no surface, sampler, blend, CC or depth block failed |
| 48, 50 | `1a4c72ce-dirty` with 768-byte slots | **0** | |
| 49 | `45f42a9c-dirty` (Phase 2, synchronous) | 4,453 | (that build predates the reasons) |

On the host, every description `v9x_intel_hsw_ps_kernel_build` accepts
was built at d3d_hsw.c's old capacity of 96 dwords (512-byte slots
less 128 bytes of prefetch margin). 21 of the 48 failed:

- every Direct3D stage with specular and fog needs 112 dwords;
- diffuse with specular and fog needs 100;
- the two-unit kernels need up to 140.

A refused batch draws nothing, so each specular-and-fog draw was a
hole. 0.20.0, the tester's build, has the same slots.

The fix:
- `intel_hsw.h`: 48 slots of 768 bytes (160 dwords), one per
  description (`v9x_intel_hsw_pipe_kernel_slot`, now in `hsw_pipe.c`).
  The state area grows from `0xC000` to `0xD000`.
- The batch area moves to `0x11000` and halves to two 2 KB slots. The
  largest draw batch fits with room to spare: the host test holds it
  under 256 dwords.
- `test_every_kernel_fits` builds every description at the new
  capacity and checks each has a slot of its own. It failed first, on
  the same 21.

## The Lobby mosaic is not the pipelining

Game 3 (Lobby) draws its walls and floors as a mosaic of wrong texels;
Dragothic's buildings show it too. It appears on every build here:
- the pipelined builds (boots 46, 48);
- the synchronous Phase 2 build (boot 49, `run-frames-boot49-sync.png`).

So it predates pipelining, and the refused batches were only the holes.
Its cause is not established. Counters that bear on it, boot 48:
- 53,694 mip chains with a level out of place (`MipGapOffset`); each is
  sampled at level 0 with no mip filter;
- 7,038 mip trees declined, the last for heap room.

One scene of the vertex-shader test is cut by a diagonal edge
(`shot13-cut.png`) on every build, with no refusal behind it.

## The registered run, boot 50

Score 801, marked by 3DMark as not run at default settings (16-bit
textures). Recorded, not compared:

| Test | Result |
|---|---|
| Game 1 Car Chase, low / high | 8.0 / 3.9 fps |
| Game 2 Dragothic, low / high | 8.9 / 2.4 fps |
| Game 3 Lobby, low / high | 26.7 / 11.9 fps |
| Game 4 Nature | not supported by hardware |
| Fill rate, single / multi | 181.3 / 179.4 MTexels/s |
| High polygon count, 1 / 8 lights | 0.7 / 0.7 MTriangles/s |
| Environment / DOT3 bump mapping | not supported by hardware |
| Vertex shader | 5.7 fps |
| Pixel shader, advanced pixel shader | not supported by hardware |
| Point sprites | 0.2 MSprites/s |

1,298,264 batches went out without waiting and 3,377 synchronously,
with no refusals, timeouts or resets. 6.7% of slot waits stalled.

From boot 48's timings, over 402 s:
- Direct3D calls: 159.7 s, of which engine draw 148.0 s (114 µs a
  batch, 68 triangles each);
- the vertex copy into the uncached aperture: 71.8 s;
- locked surfaces held by the application: 57.7 s;
- Flip and its status polls: 34.9 s;
- the remaining ~150 s: the runtime's software T&L and 3DMark itself.
