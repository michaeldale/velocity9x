# Voodoo4 Direct3D throughput: stop treating every batch as synchronous

Date: 2026-10-29. Status: proposed; nothing coded, nothing measured.

Machine: A8U4I5 (Pentium III 1 GHz, 440BX), Voodoo4 4500 PCI.
Benchmark: Half-Life `timedemo mwd5`, Direct3D, 640x480x16 full screen
([procedure](../../tests/benchmarks/hl1/README.md)).
Engine: [d3d_tdfx.c](../../src/display32/d3d/d3d_tdfx.c) over the
builders in [tdfx_3d.c](../../src/chipsets/tdfx/tdfx_3d.c). Register
facts: [vsa100-registers.md](../specifications/vsa100-registers.md).

## The gap

From [the Half-Life record](../decisions/2026-10-12-voodoo4-half-life-direct3d-on-velocity9x.md)
(boots 430 and 431):

| Driver | mwd5 fps |
|---|---|
| 3dfx 1.04.00 | 58.9 |
| Velocity9x `f419ed1` | 25.7 |

The Velocity9x run drew 10.4 million triangles in 885,234 batches, about
12 triangles a batch, with nothing refused. 3dfx's 58.9 may be a 60 Hz
vsync cap; that is not established, so the real gap may be larger.

At 12 triangles a batch, anything paid once per batch costs as much as
the drawing. The engine pays a lot per batch, by design. The netbook's
Gen3 ring had the same problem
([2026-09-25](../decisions/2026-09-25-where-the-hal-time-goes-on-the-netbook.md):
43 % of 3DMark 99 spent waiting for the ring head), and the HD 4600 got
most of its Half-Life gain from not waiting
([pipelined submission](intel-haswell-pipelined-submission.md): 60.5 to
79.3 fps).

## What the engine does per batch and per triangle

Read from the code on 2026-10-29. None of it has been timed.

**Per batch** (`v9x_d3d_tdfx_draw`):

1. **A full idle before the first triangle** (`d3d_tdfx.c`, "Idle first"):
   a `nopCMD`, then at least three uncached `status` reads with bit 9
   clear. The CPU waits for the previous batch to finish drawing before
   it builds the next, so the CPU and the engine never overlap.
2. **The whole state, every time**: `renderMode`, the colour and aux
   buffers, the clip pair, `fbzMode`, `fbzColorPath`, `alphaMode`,
   `fogMode`, then `textureMode`, `tLOD` and `texBaseAddr`: 11 to 13
   writes. The file header gives the reason: Glide reprograms the engine
   while it holds the card, so nothing may assume a register kept its
   value.
3. **`texBaseAddr` written twice** (`tdfx_3d.c`), a different base then the
   real one. This invalidates the texture cache (§20.3 p.234), which is
   needed only after the CPU has written texels. It happens on every
   batch, whether or not anything was downloaded.
4. **The mapping done twice**: `accepts()` and then `draw()` each run
   `resolve_texture`, `map_draw` and the mip-chain walk on the same batch
   (`d3d_core.c`, `v9x_d3d_dispatch_engine`). This is CPU-only and small,
   but it is paid 885k times.

**Per triangle** (`v9x_tdfx_3d_build_triangle`, textured and Z-buffered):

5. **37 uncached register writes**: `sSetupMode`, then for each vertex X,
   Y, four float colours, Z, Wb, W0, S, T and a begin or draw command.
   - Wb and W0 carry the same 1/W. With fog off, the pixel pipeline does
     not read Wb, so one of them is redundant.
   - The four colour floats could be one `sARGB` word (§8.2). The builder's
     own comment says how the setup unit treats packed colour under direct
     writes is not established (D37), so this needs a probe first.
6. **A blocking `status` read every 16 writes** (`v9x_d3d_tdfx_emit`), about
   three per triangle. A read crosses PCI and stalls the CPU until it
   returns. §8.1 records that the spec describes back-pressure by PCI
   retry and never requires polling; we poll because retry is not
   established on this card.
7. **Strips and fans arrive as separate triangles**: three vertices each,
   where the setup unit draws a strip or fan for one vertex per extra
   triangle (§8.2, "Strips and fans").

**Transport:**

8. **Direct register writes over PCI, one 32-bit write at a time, uncached.**
   §8.1: the chip can also be driven through a command FIFO in video
   memory (§7). With that FIFO in a write-combined mapping, the CPU writes
   at frame-buffer speed (the SiS 6326 went from 49.9 to 89.3 MB/s, the
   Rage 128 to 241 MB/s on the same 440BX), the engine fetches the words
   itself, and the CPU reads the chip only when the FIFO runs short of
   room. `v9x_vsa100_device` leaves `write_combine` at 0, and the SiS
   record shows why write-combining alone would not help here: "3D does
   not move" while the engine is fed by direct register writes.

## Steps

In order. Each step is measured on A8U4I5 with mwd5 before the next starts.
Every step keeps the refusal contract (a refused batch writes nothing) and
the Glide handover (`V9X_DD_ENGINE_VALID`).

### Step 0: time it

Put the HAL's TSC buckets on the engine, as the MGA has them
(`d3d_mga.c`, `V9X_TIME_*`):

| Bucket | Covers |
|---|---|
| `ENGINE_DRAW` | the whole `v9x_d3d_tdfx_draw` |
| `DECODE` | mapping, state build, triangle build |
| a new idle bucket, or `HEAD_WAIT` reused | `v9x_d3d_tdfx_wait_idle` before a batch |
| `RING_SPACE_WAIT` | `v9x_d3d_tdfx_wait_fifo` |
| `RING_WRITE` | the register writes |

Acceptance: one mwd5 run of three, with the buckets dumped by V9XTRACE and
converted by `report-hal-timing.ps1`. The record says which bucket is
largest. If the idle wait is not large, steps 1 and 2 are re-ranked before
any code changes.

### Step 1: no idle before each batch

Wait for idle only where something outside the 3D stream needs the engine
finished:

- before Lock, Flip, a surface destroy, and a 2D blit or fill;
- before the engine is handed to Glide, and the first batch after it comes
  back;
- before a batch whose texture the CPU wrote since the last batch (see
  step 2's download flag).

The SiS drains added with write-combining (`v9x_sis_emit`,
`v9x_d3d_sis_draw`) are the pattern. The 2D engine (`eng_tdfx.c`) already
waits before each operation, so a blit after a 3D batch stays correct.

Gates: V9XDDP against its last Voodoo4 pass, V9XGLP, a Glide session
handing the card back, and Half-Life's frames watched for tearing or
stale textures.

### Step 2: send only the state that changed

Keep the last state written (the `v9x_tdfx_3d_state` that produced it) and
a generation number. Discard the copy when `V9X_DD_ENGINE_VALID` drops, on a
mode set, and when another process's context draws (the HAL statics are
shared across processes:
[issue](../issues/2026-10-06-hal-statics-shared-across-processes.md)).

Compare per group rather than per register. The Millennium II tried a
per-register cache and lost about 8 % to its lookups
([parked plan](matrox-mga2164w-performance-parked.md)). Here the groups
are: buffers and clip; `fbzMode`, `fbzColorPath`, `alphaMode` and
`fogMode`; and the TMU0 texture words. One comparison of the whole state
struct can short-circuit all three when nothing changed, which is the
common case inside one Half-Life surface.

Write `texBaseAddr` twice only when a download flag is set. Set the flag
from the paths where the CPU writes texels: Unlock of a texture surface,
a system-to-video Blt into one, and a texture created since the last
batch. Otherwise write it once when the base changes, or not at all.

Gates: as step 1, plus V9XDDP's texture-update checks.

### Step 3: fewer words per triangle

- Drop Wb when fog is off: clear `sSetupMode` bit 3 and send W0 only
  (§8.2, a clear bit reuses the previous value). Confirm with V9XDDP's
  perspective checks.
- Packed `sARGB`: a TDFX3D probe scene drawn both ways, then a decision
  record closing D37. Use it only if the pixels match.
- Do the mapping once per batch: `accepts()` hands its result to `draw()`,
  or the core skips `accepts()` for an engine whose `draw()` refuses
  cleanly anyway.

Expected: 37 writes a triangle to 26 with both changes. That is a
prediction, not a measurement.

### Step 4: strips and fans as strips and fans

The core expands every primitive to a triangle list before the engine sees
it. Carrying the primitive type and the shared vertices to `draw()` is a
change to the engine interface (`V9X_D3D_ENGINE_OPS` is append-only), so
it needs its own decision. It is the bigger change, and it lets the setup
unit draw a fan at about a third of the vertex traffic. A census of how
much of mwd5 arrives as strips and fans comes first. The
[record-merging plan](d3d-drawprimitives-record-merging.md) and the
[in-call merging plan](r3d-in-call-batch-merging.md) touch the same
records.

### Step 5: the command FIFO in write-combined video memory

Its own plan before any code. §7 has the registers and the packet types:
packet 3 sends a whole triangle, strip or fan with a single header word,
a parameter mask and packed colour; packet 1 sends state as a register
burst. The open questions are:

- **Where the FIFO lives.** It must be in the low 16 MiB (§7), and Glide's
  own FIFO is already there
  ([issue](../issues/2026-10-12-glide-cmdfifo-overlaps-the-desktop.md)).
  Velocity9x's FIFO, Glide's FIFO and the desktop must not overlap, and
  the handover must stop ours before Glide starts its own.
- **Management mode.** Software (`cmdBump`) or hole counting (§7). Either
  needs a fence before the wrap jump.
- **Write-combining for the Voodoo4.** Set `write_combine` on
  `v9x_vsa100_device` and measure it on its own first, with dispbench, as
  the SiS and Rage 128 were. The 3D registers stay in BAR0 and uncached.
- **2D on the same FIFO**, or kept on direct writes with a drain between
  the two.

Acceptance for the whole plan: mwd5 within 15 % of 3dfx's 58.9 on the same
machine, or a record of the bucket that stops it.

## Not in this plan

- Mipmap filtering, trilinear, fog, specular and the second TMU. They
  change what the frames look like, not how fast they come.
- Vsync. Whether 3dfx's 58.9 is a 60 Hz cap is worth one run with vsync
  forced off on both drivers (the [vsync setting](vsync-off-setting.md)
  covers ours) before step 5 is judged against it.
- The other engines fed by direct register writes (SiS 6326, Mach64,
  MGA). Steps 1 and 2 probably apply to them too. Each needs its own
  measurement.
