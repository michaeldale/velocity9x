# The Gen6 initialising batch runs on the HD 3000

Date: 2026-11-20
Status: **measured once, M-NZHWBR boot 63.** Step 4 of
`docs\plans\intel-sandybridge-direct3d.md`. Build `e444d2b0-dirty`
(branch `intel-snb`). Evidence:
`docs\probe\m-nzhwbr-snb-hd3000-init-2026-11-20\`, files `boot63-*`.

## What was run

The first build in which the Sandy Bridge engine descriptor stamps
`CAP_D3D` and the new identity bit `CAP_GEN6_SNB` (`EngineCaps=80080017`
in `boot63-V9XHW.INI`, `Direct3D=hardware-gen6`, `Direct3DMode=hardware`).
The HAL's `ready()` sees the bit, writes the Gen6 init state into both
pipeline slots and submits `v9x_intel_snb_init_workload` on the render ring,
waiting for its breadcrumb. It then answers no, so every draw entry point
declines: the setup batch is the only Gen6 work the GPU is given.

The batch is the host-tested one (`snb_3d.c`, checker passing): the
post-sync non-zero pair and a flush, PIPELINE_SELECT, STATE_BASE_ADDRESS,
an invalidate, one `3DSTATE_URB` of 256 one-row VS entries, VS and GS
off with crocus's post-VS flush, zero CLIP/SF/WM, the depth
stall/flush/stall and a NULL depth buffer, the pointer commands, an RT
flush, the breadcrumb. The post-sync writes go to the status page's Ivy
Bridge workaround qword.

`V9XD32.EXE` (default, 1024x768x32) made the first Direct3D call.

## Result

`boot63-after-d32-V9XSNAP.INI`, V9XTRACE after V9XD32:

| Counter | Value |
|---|---|
| `SyncSubmits` | 1 |
| `RenderDrainWaits` | 1 |
| `RenderDrainStalls` | 0 |
| `EngineIdleTimeouts` | 0 |
| `EngineFifoTimeouts`, `EngineResets` | 0 |

One batch was submitted synchronously and its breadcrumb stored back
inside the wait: the render engine executed the whole Gen6 batch, the
PIPE_CONTROL post-sync writes included, and did not quarantine. The
machine stayed up (boot 63 throughout). `V9XTRACE.INI` is boot 60's file,
unchanged: no fault was recorded.

V9XD32 ran all its stages and reported `Result=FAIL` (`Failures=1Ah`):
every drawing test (T4, T13, T14, T16) found the target undrawn, as it
must with every draw declined. T15, which draws nothing, passed. T18 and
T19 report no T&L device and no pixel shaders, as for Ivy Bridge.

## What this settles

- The Gen6 command table's opcodes and lengths for every command in the
  batch are ones the command streamer accepts (an unknown or misparsed
  command would have hung the ring before the breadcrumb).
- The post-sync non-zero pattern and the depth stall/flush/stall run.
- The `3DSTATE_URB` allocation is accepted.

## Not settled

- Anything that draws: CLIP, SF and WM were zero, so no thread was
  dispatched and the Gen6 pixel kernel has not run.
- The i915 render-ring workaround registers were not written; the batch
  ran without them.
- While this build is installed, Direct3D programs on M-NZHWBR start and
  draw nothing.
