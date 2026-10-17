# Hardware Direct3D on Haswell (Gen7.5): the ladder from first write to textured triangles

Status: **in progress.** Phase A done 2026-10-17: forcewake acknowledged,
the GT reads zero because it is in RC6 and is otherwise unprogrammed
(`docs\decisions\2026-10-17-haswell-phase-a-forcewake.md`). Phase B1 done the
same day: BCS runs a store and a fill from a ring at aperture `1800000h`; the
blit pitch is bytes; the top megabyte of stolen memory is not CPU-writable
(`docs\decisions\2026-10-17-haswell-phase-b1-bcs-ring.md`). Phase B2 done
the same day: the driver runs DirectDraw fills and copies on BCS at 8, 16
and 32 bpp, pixel-exact, through VBE mode sets and a `reset`; Close is not
yet measured (`docs\decisions\2026-10-17-haswell-phase-b2-directdraw-on-bcs.md`).
Phase C done the same day: the render ring ran the initialising batch through
`MI_BATCH_BUFFER_START`, and its `PIPE_CONTROL` breadcrumb landed with EIR 0
(`docs\decisions\2026-10-17-haswell-phase-c-render-ring-init.md`). Phase D
done the same day: a constant and an interpolated triangle match the
reference at every judged pixel, after the render target write's message
type was moved to bits 17:14
(`docs\decisions\2026-10-17-haswell-phase-d-first-triangle.md`). Phase E
done the same day: eight texture-stage combinations, textured, fogged and
alpha-tested, match the reference within 1/255
(`docs\decisions\2026-10-17-haswell-phase-e-pixel-shader-set.md`). Phase F1a
done the same day: bilinear, wrap, mirror and alpha blend match; the VF
cache must be invalidated per batch
(`docs\decisions\2026-10-17-haswell-phase-f1a-filter-wrap-blend.md`). F1b
done the same day: a D32_FLOAT depth buffer, cleared by drawing, resolves
three overlapping triangles (`docs\decisions\2026-10-17-haswell-phase-f1b-depth.md`).
F2a done the same day: Direct3D draws on the render ring through the HAL,
with the clipper's perspective divide off for pre-transformed vertices
(`docs\decisions\2026-10-17-haswell-phase-f2a-direct3d-engine.md`). F2b
done the same day: Y-tiled D16 Z buffers, filled by a depth-only draw
whose colour writes are masked. A zero WM and PS hung the render engine
twice. V9XDDP's Z and private-device Z checks pass; Mixed fails on the
recorded Lock limit
(`docs\decisions\2026-10-17-haswell-phase-f2b-depth.md`). F3 done
2026-10-17/18: flips through plane A's double-buffered base. 3DMark 99
drew nothing into the presented buffer, against a third of its batches
before, and Michael saw no flicker in either game
(`docs\decisions\2026-10-17-haswell-phase-f3-flips.md`). G (games) next.
The machine is the i5-4570 (`8086:0412`), on the `intel-gma`
family's `hd4600-hsw` chip since B2 (tier-0 was the `vbe` family at
640x480x8, `docs\decisions\2026-10-17-haswell-tier0-enable-ok.md`), with a
read-only MMIO and GTT snapshot in hand
(`docs\decisions\2026-10-17-haswell-phase2-read-only-mmio.md`).

Prerequisite reading: `intel-haswell-bringup.md` (Phases 0-2, done),
`hardware-d3d-on-intel-gma950.md` (the Gen3 ladder this repeats), and the
HSW PRM in `C:\everything\claude\personal\intel driver research\
haswell-2013-gen7.5-prm\`, chiefly Vol 2a-2d (commands, structures), Vol 6
(command stream) and Vol 7 (3D pipeline and EU ISA).

## Why this is a new engine and not Gen3 with new opcodes

Gen3 is fixed-function: the 945's draw is a state block, a fragment program
of a dozen Gen3 instructions and inline vertices. Gen7.5 has no fixed pixel
pipe. A draw needs, at minimum:

- `PIPELINE_SELECT` (3D) and `STATE_BASE_ADDRESS`;
- URB partitioning (`3DSTATE_PUSH_CONSTANT_ALLOC_*`, `3DSTATE_URB_*`);
- every stage programmed, even the ones disabled (VS, HS, TE, DS, GS, SOL,
  CLIP, SF, SBE, WM, PS);
- binding tables and `SURFACE_STATE` for the render target and textures,
  `SAMPLER_STATE`, `BLEND_STATE`, `DEPTH_STENCIL_STATE`, `CC_STATE`,
  viewport and scissor state;
- `3DSTATE_VERTEX_BUFFERS` / `3DSTATE_VERTEX_ELEMENTS` and `3DPRIMITIVE`;
- a pixel shader kernel in EU machine code, because nothing writes a colour
  without one.

HSW Vol 2c also requires the render ring's first submission to initialise
the whole context with a valid "dummy" workload before power management may
save and restore it ("SW must dispatch workload (dummy context) to
initialize render engine with default state"). Vol 7 tabulates that
context; it is the Phase C deliverable.

What does carry over from Gen3, per the 2026-10-17 code survey: the HAL's
ring free-space and plan arithmetic (`i9xx_ring.c`), the submit/breadcrumb
discipline (`d3d_i9xx.c` 298-905), heap placement (`d3d_place.c`), the
`V9X_D3D_ENGINE_OPS` contract (`d3d_internal.h` 392-478) and the
arm-then-verify method. Not the command encoding, the fragment programs,
the PTE decode or the mini-VDD's ring verbs.

## Shape

- **Engine type** `INTEL_GEN75` in `engine_abi.h`, with the three
  `engine_type` switches (`d3d_select.c`, `d3d_core.c`, `ddhal_core.c`),
  `family.ps1`'s list and `test_family_matrix.c` updated together.
- **Family**: a `hsw-gt2` chip entry in `intel-gma` (one family per vendor),
  its own hw16 device and `Objects`, added only when Phase B works. Until
  then nothing claims `8086:0412` and the Gen3 engine cannot be armed on it.
- **Ring 0**: new mini-VDD verbs for the Haswell layout (MMIO and GTT in one
  BAR0, forcewake, ring enable on RCS and BCS). The Gen3 verbs hard-code
  BAR3 and 945 ring checks and stay Gen3's.
- **Pure, host-tested C** for everything that is encoding: packet builders,
  state structures, the EU instruction encoder and a decoder that checks a
  stream before it is submitted - the role `i9xx_3d_decode.c` plays for
  Gen3. Register pokes stay in the VxD and the HAL submit path.

## Phases

Each phase ends in a decision doc with the INI that proved it. A phase that
writes to the GPU for the first time gets its own design for approval
first, as Gen3's Phase 4 did.

### A. Forcewake: the first write

Request forcewake, read the acknowledge, read the ring and RC6 registers
again, release. Settles what Phase 2 could not: whether the GT's zeros are
an idle, unprogrammed GT or one asleep in RC6. **Design for approval
before anything is armed.**

Proposed design (not yet approved):

- **Where:** `HSWMM.VXD` gains a forcewake flag; V9XHSWMM.EXE passes it
  only with `/forcewake` on its command line. The default run stays
  read-only.
- **Sequence**, interrupts off, every step's value recorded:
  1. read the acknowledge (`130044h`); if bit 0 is already set, someone
     else holds forcewake - record it and write nothing;
  2. write `00010001h` to `A188h` (masked write: enable mask bit 16, set
     bit 0);
  3. poll the acknowledge for bit 0, bounded at a fixed iteration count,
     and record how many polls it took or that it timed out;
  4. read the same GT offsets as the read-only pass;
  5. write `00010000h` to `A188h` (masked clear of bit 0) - always, timed
     out or not;
  6. poll the acknowledge for bit 0 clear, bounded, and record the result.
- **Writes:** two, both to `A188h`, the second undoing the first. No ring,
  no GTT, no display register.
- **Source of the values:** the masked-write form, the bit and the
  acknowledge-poll handshake are i915's (`FORCEWAKE_MT`,
  `FORCEWAKE_ACK_HSW`). The public PRM names `0xA188` and documents
  `130044h` bits 15:0 as the multiple-forcewake status, which agrees, but
  does not spell out the request format. That is the gap this measures.
- **What each outcome means:**
  - acknowledged, GT registers now non-zero: the zeros were RC6, and every
    later phase holds forcewake around its register access;
  - acknowledged, still zero: the GT is awake and unprogrammed; Phase B
    can proceed;
  - never acknowledged: the request format or offset is wrong for this
    part; stop and look again before writing anything else.
- **Risk:** low. i915 performs this handshake on every boot of every
  Haswell. The probe runs from a desktop, not at boot, so a failure costs
  at most one reset.

### B. The blitter ring

BCS ring in stolen memory (the GTT already maps it linearly from BDSM),
`MI_NOOP`, `MI_STORE_DATA_IMM` into a status page, then `XY_COLOR_BLT` and
`XY_SRC_COPY_BLT`. DirectDraw fills and copies on BCS are useful on their
own, and BCS has no render context to initialise. Ends with Haswell in
`intel-gma` at `Acceleration = directdraw-fill-blt`.

**Phase B1 design, for approval (2026-10-17).** One run of the existing
`HSWMM.VXD` / `V9XHSWMM.EXE` with a new `/bcs` switch, from the desktop, no
reboot, every stage recorded and each stage gated on the one before. It
answers "does BCS execute from a ring we place, and does a fill land where
we computed" in one run; DirectDraw use is B2, after.

*Placement.* Three regions in the aperture above 16 MB, where nothing runs
on this machine: the `vbe` driver uses the first 16 MB only
(`V9XMODES.INI` `Vram=reported=33488896 usable=16777216`), stolen is 32 MB.

| Region | Aperture offset | Size |
|---|---|---|
| ring | `1800000h` | 4 KB (one page, CTL length 0) |
| status | `1801000h` | 4 KB |
| scratch | `1802000h` | 64 KB |

The first placement, `1F00000h` (the top megabyte of stolen), failed stage
2 on run 1: the CPU's write through the aperture did not read back. Moved
down to 24 MB on 2026-10-17 with approval; nothing else changed.

*Stages.*

1. **Check the pages.** Read the GTT entries for all 18 pages. Each must be
   valid and point at BDSM + offset (the linear stolen mapping measured for
   the first 4096 entries; these entries, 6144-6161, have not been read).
   Any other value stops the run with nothing written to the GPU.
2. **Check the CPU path.** Map GMADR + `1800000h` (64 KB + 8 KB) and write a
   pattern through it, then read it back; the 945 taught that stolen memory
   is reachable only through the aperture
   (`2026-09-14-cpu-writes-to-stolen-memory-need-the-aperture.md`). Fill the
   ring with `MI_NOOP`, the status page with `0`, and the scratch with a
   guard value `A5A5A5A5h`.
3. **Forcewake**, exactly as Phase A. Held until stage 8.
4. **Program BCS**: read `BCS_RING_BUFFER_CTL` and require 0 (ring off);
   write HEAD `0`, TAIL `0`, START `GMADR offset 1800000h` (a graphics
   address, Vol 2c 31:12), then CTL `00000001h` (length 0 = 4 KB, enable).
   Read each back. Any mismatch goes straight to stage 8.
5. **First command.** Ring dwords 0-3: `MI_STORE_DATA_IMM` (Use Global GTT)
   to status + 0 with `C0DE0001h`. TAIL = 16. Poll HEAD for 16 and the
   status dword for the value, bounded.
6. **Fill.** `XY_COLOR_BLT`, 32 bpp, ROP `F0h`, colour `11223344h`,
   destination = scratch base, rectangle (0,0)-(16,16), pitch field `256`.
   Then `MI_STORE_DATA_IMM` of `C0DE0002h`, padded to a qword; advance TAIL.
   Poll as in stage 5. The PRM calls the pitch "in DWords" next to a note
   about tiled surfaces; with 256 the fill lands at a 256-byte or a
   1024-byte stride, and the readback says which. Every guard dword outside
   both candidate footprints must still read `A5A5A5A5h`.
7. **Record**: HEAD, TAIL, BCS ACTHD (`22074h`) and EIR (`220B0h`, i915
   offsets), a checksum and a row-stride verdict over the scratch.
8. **Tear down, always:** CTL `0`, then HEAD, TAIL, START `0`, read back;
   release forcewake as Phase A.

*Writes, all of them:* `A188h` twice (stage 3 and 8); the four BCS ring
registers (set, then returned to 0); memory only inside the three regions
above, through the aperture. No display register, no GTT entry, no render
ring.

*Failure.* A poll that runs out is recorded with HEAD, ACTHD and EIR, and
the run still tears down. No GPU reset is attempted; a blitter left busy
costs a reboot (`reset`), not the display, which is the VBE mode on pipe A
and has nothing to do with BCS. The command bytes come from
`hsw_3d.c`'s MI builders, which the batch checker passes before
submission; `XY_COLOR_BLT` (client 2, opcode `50h`, length 4, Vol 2b) gets
its builder and a host test as part of this work.

**Phase B2 design, for approval (2026-10-17).** DirectDraw fills and copies
on BCS inside the driver, built on exactly what B1 measured
(`docs\decisions\2026-10-17-haswell-phase-b1-bcs-ring.md`). Software
Direct3D, no flips through the ring, no async submission: those are later
and separate.

*Family entry.* `intel-gma` gains one chip, `hd4600-hsw`, `8086:0412`
exactly (one family per vendor; other Haswell IDs only once one has been
measured). VBE mode switching as tier-0 today, `Acceleration =
directdraw-fill-blt`, a new engine type `INTEL_GEN75` (value 9 in
`engine_abi.h`, with `family.ps1`, `d3d_select.c`, `ddhal_core.c` and
`test_family_matrix.c` updated together), EngineCaps `SOLID_FILL` and
`SCREEN_COPY`. Its own hw16 device (`hsw_hw16.c`) beside the Gen3 ones;
none of the Gen3 engine, arm or ring code is reachable from it, and the
Gen3 device-id gates are left as they are.

*Ring 0: two new mini-VDD verbs*, Haswell-only and separate from the
Gen3 ring verbs:

- **Hsw_Engine_Open**, from the hw16 `fill_engine_descriptor`: map
  GTTMMADR (MMIO and GTT halves) and the reserve through GMADR; then B1's
  stages 1-4 unchanged - check the reserve's PTEs, check the CPU path,
  take forcewake, program BCS. Returns the MMIO, ring and status linear
  addresses, or a numbered refusal naming the stage and the values it
  compared.
- **Hsw_Engine_Close**, from Disable: B1's stage 8 - ring off and cleared,
  forcewake released.

*Reserve*, the B1 region: ring at aperture `1800000h`, 4 KB (the length
measured; the ring plan already wraps), status page at `1801000h`. Above
the 16 MB the DirectDraw heap uses, below the top megabyte that refused
CPU writes. The heap is unchanged.

*Forcewake for the engine's lifetime.* Taken at open, released at close.
The GT then never enters RC6 while the desktop runs - a power cost on a
desktop machine, chosen over a forcewake handshake around every TAIL write
from ring 3. Whether per-submission forcewake is needed or possible is a
later measurement, not this phase.

*HAL: `eng_hsw.c`*, a `V9X_ENGINE32_OPS` table selected for
`INTEL_GEN75`, shaped like `eng_i9xx.c`:

- `fill`: `XY_COLOR_BLT` at the surface's depth (BR13 colour depth 00 8
  bpp, 01 565, 11 32 bpp; byte mask only at 32 bpp), pitch in bytes.
- `copy`: `XY_SRC_COPY_BLT` (client 2, opcode `53h`, 8 dwords, Vol 2b),
  overlapping rectangles refused to the CPU path as Gen3 does. Its pitch
  fields carry the same "in DWords" text the fill disproved; the first
  boot measures it.
- Each submission ends with `MI_STORE_DATA_IMM` of a sequence number into
  the status page (the store B1 measured). `wait_idle` polls that number,
  bounded; running out marks the engine failed and every later blit takes
  the CPU path. No reset.
- Commands from `hsw_3d.c`'s builders; `XY_SRC_COPY_BLT` gets a builder
  and a host test.

*Failure is tier-0.* Any open refusal leaves the engine unadvertised and
the desktop exactly as it is today, with the refusal code in `V9XHW.INI`.

*The first boot answers, in order*, all from the desktop:

1. `V9XHW.INI` / `V9XBOOT.INI`: engine open accepted, the refusal code if
   not.
2. `V9XDDP.EXE`: DirectDraw fills and copies at 640x480x8 with the HAL's
   own readback against a CPU reference; `V9XTRACE` snapshot counts engine
   blits.
3. The same at 16 and 32 bpp through `V9XMSW.EXE` mode switches, then BCS
   ring registers read through `V9XHSWMM.EXE` (read-only) after each: does
   a VBE mode set disturb a live BCS ring? On the 945 it did not
   (`2026-09-12-intel-phase3-event-matrix.md`); here it is not known. The
   full-screen DOS box is excluded, as on the netbook.
4. A restart through `reset`, to see Close and the next Open.

*What this phase writes that B1 did not:* nothing new to the GPU beyond
more blit commands; the new thing is that it runs at every boot and for
the whole session.

### C. The render context

RCS ring, then the dummy context of Vol 7's table submitted once, then a
`PIPE_CONTROL` post-sync write as the breadcrumb. No draw yet. Measures
whether the render engine accepts a complete state and returns.

**Phase C design (2026-10-17, started on "Do phase c").** What Vol 2c asks
for is narrower than a context image. `RING_BUFFER_CTL`'s programming note
says the first enable of the render ring must "dispatch workload (dummy
context) to initialize render engine with default state", with "indirect
pointers used in 3D states" pointing at valid memory. The Vol 7 table is
the save layout for `MI_SET_CONTEXT`, which this ladder does not use. Ring
buffer scheduling without hardware contexts is how i915 ran Gen7 before
contexts existed. So the deliverable is an initialising batch, built and
host-tested as pure C, run once from a probe exactly as B1 ran BCS.

*Where.* `HSWMM.VXD` DIOC 3, `V9XHSWMM.EXE /rcs`, from the desktop, with
the B2 driver running. B1's BCS code is left as measured; the RCS
procedure is its own.

*Placement.* Aperture `1820000h`, clear of the driver's live BCS ring and
status page (`1800000h`-`1801FFFh`) and of B1's old scratch:

| Region | Offset | Size |
|---|---|---|
| ring | `+0000h` | 4 KB |
| status | `+1000h` | 4 KB |
| batches | `+2000h` | 8 KB |
| indirect state (all four bases) | `+4000h` | 8 KB |
| over-fetch page (Vol 2b, `MI_BATCH_BUFFER_START`) | `+6000h` | 4 KB |

*Stages,* each gated on the one before:

1. The seven PTEs are valid and linear from BDSM.
2. The CPU path works through the aperture. Then the ring is filled with
   `MI_NOOP`, the status page with 0, and the batches and state copied in.
3. Forcewake. The B2 driver already holds it (`A188h` = 1). The probe
   uses that hold and releases nothing it did not request. If nobody holds
   it, the probe requests it as Phase A did and releases it at the end.
4. Read `RCS CTL` (`203Ch`) and require 0. Read `GFX_MODE` (`229Ch`),
   `MI_MODE` (`209Ch`) and `INSTPM` (`20C0h`) for the record, writing
   none of them. Then program HEAD, TAIL, START and CTL as B1 did, and read
   them back.
5. **A store from the ring.** `MI_STORE_DATA_IMM` `C0DE0011h` to status
   + 0. Shows RCS fetches and executes at all.
6. **A trivial batch.** `MI_BATCH_BUFFER_START` (Vol 2b: opcode `31h`,
   length 0, first level, privileged, GGTT) to a batch holding one store
   (`C0DE0021h`, status + 8) and `MI_BATCH_BUFFER_END`; then a ring store
   of `C0DE0022h` (status + 12). Shows the batch mechanism and the return.
7. **The initialising batch,** then a ring store of `C0DE0032h` (status +
   24). The batch, in order:
   - `PIPELINE_SELECT` 3D, a CS-stall `PIPE_CONTROL`;
   - `STATE_BASE_ADDRESS`, all bases at the state region, with bounds;
     then a `PIPE_CONTROL` invalidating the state, constant, texture and
     instruction caches;
   - push constants VS 0-8 KB and PS 8-16 KB, with HS, DS and GS at size 0;
   - URB VS from 16 KB, 64 entries of 64 bytes (GT2's minimum, Vol 2b
     `3DSTATE_URB_VS`); HS, DS and GS 0 entries;
   - VS, HS, TE, DS, GS and STREAMOUT disabled; CLIP, SF, SBE, WM and PS
     all zero. Nothing is dispatched without a primitive, and none is sent;
   - `3DSTATE_DEPTH_BUFFER` NULL (D32_FLOAT), `3DSTATE_MULTISAMPLE` one
     sample, `SAMPLE_MASK` 1, `CONSTANT_PS` empty, a drawing rectangle at
     640x480;
   - pointers to valid zeroed state: CC and SF/CLIP viewports, scissor,
     blend, depth/stencil, CC, the PS binding table (one NULL surface) and
     the PS sampler;
   - Vol 2b's required end of a 3D batch: `PIPE_CONTROL` RT flush with CS
     stall, `3DSTATE_CC_STATE_POINTERS`, `PIPE_CONTROL` RT flush with CS
     stall;
   - the breadcrumb: a CS-stall and scoreboard `PIPE_CONTROL`, then a
     `PIPE_CONTROL` with a post-sync write of `C0DE0031h` to status + 16
     (GGTT, qword);
   - `MI_BATCH_BUFFER_END`.
8. Record, also after a timeout: HEAD, ACTHD (`2074h`), IPEIR (`2064h`),
   IPEHR (`2068h`), INSTDONE (`206Ch`), BBADDR (`2140h`), EIR (`20B0h`),
   and the status page's first 8 dwords.
9. Tear down, always: CTL 0, then HEAD, TAIL and START 0, read back.
   Forcewake is released only if stage 3 requested it.

The CC, blend and depth/stencil pointers set bit 0. Vol 2b prints it as a
reserved "MB0" bit; Mesa writes `offset | 1` there on Gen7, and that is
the reading taken.

*Writes:* the four RCS ring registers, set and then returned to 0;
`A188h` only if nobody held forcewake; memory only inside the region. No
GFX_MODE, MI_MODE, INSTPM, display or GTT write. The BCS ring is never
touched.

*Failure* costs a `reset`. A render engine left busy does not touch the
display (the VBE mode on pipe A) or the BCS ring, which keeps running the
desktop's DirectDraw blits.

### D. First triangle

Pre-transformed vertices (DDI 5 hands the HAL screen-space TL vertices), so
the VS stage is disabled and vertices go from VF to clip and SF directly;
clip in guard-band or disabled. One PS kernel: interpolated colour to the
render target write message. One `3DPRIMITIVE` of three vertices into an
off-screen target, read back through the aperture, compared by CRC - the
Gen3 Phase 5 method.

**Phase D design (2026-10-17, started on "Start phase d").** Phase C's probe
and placement, extended to four ring segments: the ring store, the
initialising batch, then two draws, each a self-contained batch ending in
its own breadcrumb; the two draws share the VxD's fourth segment. Two draws because a failed interpolation should not be
confused with a failed dispatch:

- **D1, constant colour.** The kernel is four `mov`s of a colour
  (R 1.0, G 0.2, B 0.6, A 1.0, which is `FFFF3399h` as B8G8R8A8) into
  g112-g115 and a `send` render target write with EOT.
- **D2, interpolated colour.** Four `pln`s of the colour attribute's planes
  against the perspective pixel barycentrics. Vertices are red, green and
  blue.

*Region,* at aperture `1820000h` as in C, grown to 15 pages: ring, status,
16 KB image (batches at `+2000h`, state at `+4000h`), RT1 at `+6000h`, RT2
at `+A000h` (each 64x64 B8G8R8A8, pitch 256, filled with guard
`A5A5A5A5h` by the CPU), and an over-fetch page at `+E000h`. The VxD copies
both targets back after the run.

*The draw batch* is the init batch's prefix, then:

- `3DSTATE_CLIP` zero (clipping off, so the triangle passes through);
- `3DSTATE_SF`: viewport transform off, because the vertices are already
  in screen space as D3D's TL vertices are; `CULLMODE_NONE`. The init
  batch's all-zero SF is `CULLMODE_BOTH` (Vol 2b), which discards every
  triangle;
- `3DSTATE_SBE`: one attribute, read offset 1 and read length 1 (256-bit
  units, past the VUE header and position);
- `3DSTATE_WM`: thread dispatch enable, perspective pixel barycentric;
- `3DSTATE_PS`: SIMD8 only, the kernel's offset from the instruction base,
  1 binding table entry, 64 threads (field 63; GT2 allows 7-203, odd),
  sample mask 1 (HSW, matching `3DSTATE_SAMPLE_MASK`), attribute enable,
  setup data from g4 (payload r0, r1, barycentrics r2-r3);
- the binding table naming the target's `RENDER_SURFACE_STATE`; blend
  disabled with every channel written; the drawing rectangle at 64x64;
- one vertex buffer of three vertices (x, y, then RGBA floats, 24 bytes)
  and three vertex elements: the VUE header as four zeros, position (x, y,
  0, 1.0), colour. The VS is disabled, so the VF writes the VUE;
- `3DPRIMITIVE` TRILIST, 3 vertices;
- the init batch's end and breadcrumb.

*Triangle:* (8.25, 8.25), (55.75, 10.25), (10.25, 55.75). There are no
integer vertices, so few pixel centres lie on an edge. Centres within 1/64
pixel of an edge are reported apart and not judged.

*The verdict, per target:* the tool rasterizes the same triangle at pixel
centres (Vol 2b `3DSTATE_MULTISAMPLE` Pixel Location 0, the pixel centre).
It counts pixels written inside, guard pixels left outside, mismatches
either way, and for D2 the largest channel error against linear
interpolation. D1 passes when every judged pixel matches exactly. D2 passes
when coverage matches and the error is at most 2/255.

*Writes:* what C wrote, plus the draws' memory writes into RT1 and RT2. No
register beyond the RCS ring's four.

*Failure:* a hung PS thread leaves RCS short of its tail. The poll times
out, the record is taken and the ring is torn down. Measured on run 1: the
`reset` after it hard-locked the machine, so a wedged RCS costs a power
cycle.

### E. The pixel shader set

Kernels for the D3D texture-stage combinations Gen3 already covers
(`i9xx_fragprog.c`, `i9xx_multitex.c`, `i9xx_fog.c` define the list):
modulate, decal, add, alpha test, vertex fog, two stages. Each is a short
EU program; the encoder is host-tested against PRM bit layouts and each
kernel against a decoder.

**Phase E design (2026-10-17, started on "Phase e go").** Every combination
but the first samples a texture, so E takes F's minimum along: linear 8x8
B8G8R8A8 textures, one SAMPLER_STATE (NEAREST, MIPFILTER_NONE, CLAMP;
Vol 2d/2a), and the SIMD8 `sample` message (Vol 7: SIMD mode 18:17 = 1,
type 16:12 = 0, sampler 11:8, u and v as the payload, response 4). Filtering
and wrap modes stay F's.

*One generator, not one kernel per mode.* `v9x_intel_hsw_ps_kernel_build`
takes a stage-0 operation (diffuse, texture, modulate, add, decal), a
stage-1 operation (none, or modulate by a second texture) and vertex fog
with a fog colour. It interpolates only what those need, samples into
g32 and g36, combines into g112-g115 and writes as in D. The register map
is fixed: barycentrics g2-g3, setup from g4 with two registers per
attribute.

*Vertices:* x, y, diffuse RGBA, specular RGBA (fog factor in alpha, as in
D3D), two texture coordinate pairs, 56 bytes. Four attributes, so SBE reads
length 2 and the VS URB entry is two rows. D's batch is unchanged by this
(its draw stays one attribute).

*Alpha test* is not a kernel. It is BLEND_STATE's Alpha Test Enable and
function (GREATER) with the reference in COLOR_CALC_STATE; the kernel only
supplies alpha.

*The probe:* `V9XHSWMM /shade:N`, N = 0-3, each run D's layout with two
targets. 0: texture, modulate. 1: add, decal. 2: modulate with alpha test,
diffuse with fog. 3: modulate then a second texture. Each target is judged
against an integer reference: barycentrics at pixel centres, nearest
texel, UNORM rounding. Pixels within 1/64 of an edge or of a texel
boundary are not judged. The tolerance is 2/255.

*Writes:* what D wrote, plus the textures' bytes inside the state region.
*Failure:* as in D. A wedged RCS costs a power cycle, not a `reset`.

**Phase F2 design (2026-10-17, on "Do Phase F ... then the
V9X_D3D_ENGINE_OPS table").** Direct3D draws through the measured batches,
in two steps.

*F2a: the engine, without depth.*

- **Mini-VDD.** `HSW_ENGINE_OPEN` maps a 128 KB reserve at aperture
  `1800000h` (it was 8 KB) and checks all 32 PTEs. It opens the render
  ring at `+2000h` beside the blitter's: the same ring-off check,
  programming and readback. Reuse requires both rings, and Close tears
  down both. The fresh-open zeroing covers the whole reserve.
- **Reserve layout** (offsets from `1800000h`):

  | Offset | Size | Use |
  |---|---|---|
  | `+0000h` | 4 KB | BCS ring |
  | `+1000h` | 4 KB | status: BCS sequences at dwords 0-1, RCS at 8-9, the render-initialised mark at 10 |
  | `+2000h` | 4 KB | RCS ring |
  | `+4000h` | 48 KB | state (all four bases): the init state, then each draw's surfaces, sampler, blend and CC state, and from `+4000h` within it the kernel cache, 64 slots of 512 bytes |
  | `+10000h` | 8 KB | the draw batch |
  | `+12000h` | 56 KB | vertices |

  The probe region at `1820000h` stays clear of it.
- **HAL** `d3d_hsw.c`, on the neutral `draw` entry:
  - D3D's TL vertices go into the vertex area unconverted. The vertex
    elements read `sx sy sz rhw` as the position (so w carries rhw for
    perspective), diffuse and specular as B8G8R8A8_UNORM, and tu tv
    twice (attributes 2 and 3).
  - Per draw, the kernel comes from E's generator, extended with a
    specular add and with a "modulate colour, texture alpha" mode for
    D3D's MODULATE on an alpha texture. A kernel's slot is a fixed
    function of its description, so every process writes the same bytes
    to the same slot.
  - Each draw is one synchronous batch on RCS, ending with a sequence
    store, after the BCS is idle. The batch is D/E's, generalised to a
    vertex count, the TL layout, flat shading (SBE constant
    interpolation) and per-draw blend, alpha test, sampler and target
    format (B5G6R5 or B5G5R5X1).
  - The init batch runs once per ring open, the first time a process
    finds the mark absent.
  - A timeout quarantines the engine and traces it, as the blitter does.
  - Refused: depth (no Z buffer is advertised), colour key, DECALALPHA,
    DECALMASK, MODULATEMASK, border addressing, ADD on an alpha texture,
    textures in system memory. Mip filters sample level 0.
- **Selection.** `INTEL_GEN75` with `CAP_D3D`, which `hsw_hw16.c` sets when
  the render ring opened, selects this engine. `Direct3D` becomes
  `hardware-gen75`.
- **Measured by** V9XDDP's Direct3D checks on the 4570 at 16 bpp, then a
  game.

*F2b: depth.* Gen7 depth is always Y-tiled, so a Z surface needs a pitch
in whole tiles (128 bytes), rows in whole tiles (32) and 4 KB alignment.
`create_surface` places Z buffers so. A depth fill must be the engine
drawing at the clear depth, as F1b measured, not the blitter's linear
fill.

**Phase F2b design, for review (2026-10-17).** Direct3D Z buffers on the
render engine, as D16_UNORM, the format DirectDraw's Z16 surfaces already
are.

*Why it is not just a flag.* Two paths in the HAL assume a Z buffer is a
linear array of 16-bit words:

- **Depth fills.** Both the DDBLT_DEPTHFILL Blt and Clear2's Z part
  (`v9x_hal_depth_fill`) go through `v9x_depthfill_body` in ddhal_core.c.
  It calls the engine32 `fill`, the blitter's linear XY_COLOR_BLT on this
  chip, and falls back to a CPU fill when the engine declines. On a
  Y-tiled surface both write the wrong bytes: a rectangle of linear words
  is not a rectangle of tiled pixels.
- **Locks.** A runtime or application Lock of the Z surface reads tiled
  bytes as if they were linear.

The design changes the first and records the second.

*Step 1: measure what F1b did not, in the probe (`V9XHSWMM /depth16`).*
Phase F1b's batch, with three changes:

- the buffer is D16_UNORM (format 5, Vol 2b `3DSTATE_DEPTH_BUFFER`),
  pitch 128, 64 rows;
- the clear is a **depth-only** draw: WM Thread Dispatch Enable off, no
  kernel, DEPTH_STENCIL ALWAYS with write, one quad (two triangles) over a
  sub-rectangle at z = value / 65535. This is the HAL's fill, so it is
  the shape measured;
- the scene runs under LESSEQUAL, with a triangle drawn twice at the same
  z, so that equality is tested.

Writes are what F1b wrote. A wedged RCS costs a power cycle.

*Step 2: the HAL.*

- **Placement.** `d3d_hsw.c` gains `create_surface`/`destroy_surface` for
  `DDSCAPS_ZBUFFER` in video memory. It uses `v9x_d3d_place_each` with
  4 KB alignment, pitch = width x 2 rounded up to 128, and rows = height
  rounded up to 32, and so reports that pitch to DirectDraw. Any other
  surface is left to DirectDraw's heap, as today.
- **Fills.** A new `int v9x_d3d_hsw_depth_fill(surface, rect, value)` in
  `d3d_hsw.c` builds the depth-only batch from step 1 and runs it
  synchronously, after the blitter is idle. `eng_hsw.c`'s `fill`, when its
  destination is a Z buffer, calls it instead of the blitter (declared in
  ddhal_internal.h, as the other cross-module helpers are).
- **No linear fallback.** A new `depth_tiled` field is appended to
  `V9X_D3D_ENGINE_LIMITS` (append-only, as that struct requires), set to
  1 here and 0 everywhere else. When it is set, `v9x_depthfill_body`
  returns the engine's answer and never reaches the CPU fill. A
  quarantined engine therefore fails the fill (DDERR_GENERIC) rather than
  corrupting the buffer.
- **Draws.** `v9x_hsw_draw` gains a depth format and drops the probe-only
  clear primitive when `depth_clear` is 0.
  - `d3d_hsw.c` maps the depth state: `depth_enable` on a bound Z
    surface; D3D's eight functions onto Gen's, as the alpha test's are;
    `depth_write`.
  - It writes a per-draw DEPTH_STENCIL_STATE and emits the depth buffer
    (with Vol 2b's stall, flush, stall before it) in every batch that has
    one. Batches without one keep the NULL buffer.
- **Caps.** ZTEST, all eight comparisons, `dwDeviceZBufferBitDepth =
  DDBD_16`, `depth_bits_per_pixel` 16.

*What a Lock of the Z buffer returns:* the tiled bytes. DirectX 5-6
titles clear Z by Blt and rarely read it back. Detiling on Lock is out of
scope for F2b and is recorded as a known limit, not hidden.

*Measured by:* the step-1 probe, then V9XDDP's `D3DZ*`, `D3DZP*` and
`Mixed*` checks (the 12 that F2a fails for want of Z), then a game.

*Result (2026-10-17).* Step 1's depth-only clear with WM and PS zero
hung RCS twice, with and without SBE's attribute. The fill now
dispatches the shader under a blend state that disables every channel,
and that passed. V9XDDP: `D3DZ*` and `D3DZP*` pass; `Mixed*` fails on
the tiled Lock, as recorded above. Only 8 of the "12" were F2a's: the
rest were never reached, because V9XDDP continues in `V9XDD2.INI`. No
game ran. Record: `docs\decisions\2026-10-17-haswell-phase-f2b-depth.md`.

### F. Textures and state

`SURFACE_STATE` for linear textures in the heap, `SAMPLER_STATE` filtering
and wrap modes, `BLEND_STATE`, `DEPTH_STENCIL_STATE`, depth buffer. Then
the `V9X_D3D_ENGINE_OPS` table, and Haswell claims `Direct3D =
'hardware-gen75'`.

### F3. Flips: a Haswell scanout

**Design, for review (2026-10-17).** Found by 3DMark 99
(`docs\issues\2026-10-17-haswell-3dmark99-flicker-and-game2.md`):

- `hd4600-hsw` publishes no `ENGINE_CAP_FLIP`, so `v9x_flip_body` declines
  every flip (77,714 in one run).
- DirectDraw then rotates the triple-buffer chain's addresses while the
  scanout stays at offset 0.
- So one frame in three is drawn on the visible page. That is the
  flicker, and `DrawsIntoPresented` was a third of the batches.

The fix is a scanout for the core's existing flip state machine, with
nothing new in the core.

*What the read-only snapshots already show (boot 14, unmeasured as
writes):*

- Pipe A is the live pipe: `PIPEACONF` (70008h) is `C0000010`, enabled
  and active.
- Plane A is the live plane: `DSPACNTR` (70180h) has bit 31 set.
- `VGA_CONTROL` (41000h) has bit 31 set, so the VGA plane is off and the
  VBE modes scan through plane A.
- `DSPASURF` (7019Ch), `DSPAOFFSET` (701A4h) and `DSPASURFLIVE` (701ACh)
  all read 0 with the desktop at framebuffer offset 0. So a framebuffer
  byte offset is the graphics address the plane wants, as on Gen3.
- `TRANS_VTOTAL_A` (6000Ch) reads `020C01DF` at 640x480: vactive - 1 in
  11:0, vtotal - 1 in 27:16. `PIPEADSL` (70000h) is the line.
- The 4570's VGA connector is the PCH's DAC, fed from pipe A through FDI,
  so transcoder A's timing is the one scanned. The timing registers
  matching the mode say so; no other transcoder is assumed.
- The HAL's MMIO mapping is 2 MB, so all of these are already reachable
  through `engine.control_linear_base`.

*The write.* A flip to framebuffer byte offset `o`:

- `DSPAOFFSET` = `(o & FFFh) / bytes per pixel` in x (15:0), 0 in y,
  written first;
- then `DSPASURF` = `o & ~FFFh`.

This is i915's Haswell primary-plane path (`intel_gen4_compute_page_offset`
for a linear surface, then `DSPOFFSET` and `DSPSURF` in
`ironlake_update_primary_plane`). The x in `DSPAOFFSET` can exceed the
width; i915 relies on that.

It is needed here because DirectDraw's back buffers are not page-aligned:
`D3dTargetOffset` was `001D4C00h` at 800x600x16. An offset whose
remainder is not a whole number of pixels is declined, as the other
scanouts decline what they cannot express.

*When it is taken.*

- Vol 2c, plane registers: the plane registers are double-buffered, and a
  `DSPASURF` write arms their update at the next vblank start. It is
  latched, never applied mid-frame, so no write window is needed:
  `v9x_scanout_writes_in_blank` is 0 and the window is always open.
- The flip is a hardware flip (`V9X_FLIP_WAIT_HW`). It is pending until
  `DSPASURFLIVE` reads the base just written. That register reports the
  base the plane is scanning now, so equality is "taken", and the old
  buffer is free from then.
- This is the Gen3 path's ISR-and-frame-counter rule, replaced by a direct
  readback that Gen3 does not have.
- `PIPEADSL` against vactive serves `v9x_in_vblank`.
- `v9x_scanout_vblank_available` is "pipe A and plane A are both enabled
  now". Anything else is declined, as Gen3 declines two live pipes.

*Unsynced flips.* A `DDFLIP_NOVSYNC` flip (3DMark 99's VSync Off) uses
the same write, because this plane has no unlatched write without the
asynchronous-flip mode, and that mode is not proposed.

The core's rule stands: an unsynced flip arms nothing, so the next Flip
may overwrite the pending base before the latch, and that frame is never
shown. With three buffers, the one being drawn after an unsynced flip is
the one shown two flips ago. So drawing into the visible page needs the
application to finish two frames inside one refresh. `DrawsIntoPresented`
is the measure, with VSync off and on.

*Layering.*

- **Arithmetic** in `src\chipsets\intel\hsw_display.c`, host-tested: the
  offset split, the blank test, and the pipe/plane resolution from the
  register values.
- **Mechanics** in `src\display32\engines\hsw_scanout.c`: register reads
  and writes only, as `m64_scanout.c` is.
- **Dispatch** through the existing entry points in `i9xx_scanout.c`
  (`v9x_set_display_start`, `_now`, `v9x_in_vblank`,
  `v9x_scanout_hw_flip`, `_hw_flip_pending`, `_vblank_available`,
  `_flip_window_open`, `_displayed_offset`, `_note_flip_done`).
- **The cap:** `hsw_hw16.c` adds `ENGINE_CAP_FLIP` to an open engine.
- FlipToGDISurface and SetExclusiveMode already call
  `v9x_set_display_start(0)`, which writes `DSPAOFFSET` 0 and `DSPASURF`
  0.
- A mode set rewrites the plane through the video BIOS. `v9x_scanout_reset`
  drops the remembered base.

*Step 1, measured before the HAL: `V9XHSWMM /flip`.* At the desktop mode,
the probe:

1. reads the plane;
2. writes `DSPAOFFSET` and `DSPASURF` for `o` = pitch x 383. That is not
   page-aligned at 1024x768x16: pitch 2112 gives `o` = `C57C0h`, so the
   base is `C5000h` and x is `7C0h` / 2 = 992. Row 384 would be aligned
   and test nothing;
3. polls `DSPASURFLIVE` and `PIPEADSL` until the base reads back, and
   records the polls, the line at the write and at the latch, and
   `DSPAOFFSET` after;
4. holds 5 s;
5. writes 0 back and waits for that to latch.

**Passes if:**

- `DSPASURFLIVE` reaches the new base within one frame, and reaches it at
  a line at or after vactive;
- the restore latches the same way;
- the screen shows the desktop moved up by exactly 383 rows with no
  horizontal shift for the 5 s, and then the desktop as it was.

GDI screenshots read the page at offset 0, not the scanout, so the
picture is Michael's eye or the capture card.

Display-only: the worst outcome is a wrong picture until the restore.

*Step 2, the HAL as above.*

**Measured by** 3DMark 99 at the same settings:

- `FlipHandled` should equal the flip count, with `FlipDeclined` and
  `DrawsIntoPresented` near 0 with VSync on;
- no visible flicker;
- then VSync off, recorded.

*Result (2026-10-18).*

- The probe: both writes latched within the frame they were written in.
  `PIPEADSL` read vactive - 1 at the latch, so the blank test adds one
  line, as i915's `scanline_offset` does.
- 3DMark 99: `FlipHandled` 6,799, `FlipDeclined` 0, `DrawsIntoPresented`
  0, every flip taken at done.
- 3DMark's own VSync-off setting never passed `DDFLIP_NOVSYNC` here:
  flips paced to 59 Hz.
- Watched on the monitor, same boot: both games look right, with no
  flicker.

Record: `docs\decisions\2026-10-17-haswell-phase-f3-flips.md`.

### G. Games

The Gen3 test set on this machine: 3DMark 99 scenes, Half-Life D3D, Final
Reality, Thief II.

## Risks

- **Forcewake and RC6** are named in the public PRM but not fully
  documented; offsets come from i915. Phase A measures before relying.
- **The dummy context**: if the render engine needs state the table omits,
  Phase C hangs the GT. Phase C's design must say how a hung RCS is
  detected and recovered without a cold boot.
- **EU encoding errors are silent** until a kernel runs. The encoder is
  tested against the PRM's bit tables; kernels are decoded before
  submission.
- **This machine boots in five minutes** and pre-desktop output is
  invisible; every armed boot is expensive. Phases are designed to answer
  as much as possible per boot, as Gen3's Phase 4 was.

## Order

A, B, C, D, E, F, G. E's encoder and F's state builders are pure C and can
be written and host-tested while A-C wait on hardware boots.

Progress: the EU encoder and the command/state builders with a batch
checker exist and are host-tested (`docs\decisions\2026-10-17-haswell-eu-and-3d-encoders.md`);
CLIP, SF, SBE, WM and PS field builders and the kernels themselves remain.
