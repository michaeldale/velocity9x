# Hardware Direct3D on Haswell (Gen7.5): the ladder from first write to textured triangles

Status: **in progress.** Phase A done 2026-10-17: forcewake acknowledged,
the GT reads zero because it is in RC6 and is otherwise unprogrammed
(`docs\decisions\2026-10-17-haswell-phase-a-forcewake.md`). Phase B next. The
machine is the i5-4570 (`8086:0412`), running the `vbe` family at
640x480x8 (`docs\decisions\2026-10-17-haswell-tier0-enable-ok.md`), with a
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

### C. The render context

RCS ring, then the dummy context of Vol 7's table submitted once, then a
`PIPE_CONTROL` post-sync write as the breadcrumb. No draw yet. Measures
whether the render engine accepts a complete state and returns.

### D. First triangle

Pre-transformed vertices (DDI 5 hands the HAL screen-space TL vertices), so
the VS stage is disabled and vertices go from VF to clip and SF directly;
clip in guard-band or disabled. One PS kernel: interpolated colour to the
render target write message. One `3DPRIMITIVE` of three vertices into an
off-screen target, read back through the aperture, compared by CRC - the
Gen3 Phase 5 method.

### E. The pixel shader set

Kernels for the D3D texture-stage combinations Gen3 already covers
(`i9xx_fragprog.c`, `i9xx_multitex.c`, `i9xx_fog.c` define the list):
modulate, decal, add, alpha test, vertex fog, two stages. Each is a short
EU program; the encoder is host-tested against PRM bit layouts and each
kernel against a decoder.

### F. Textures and state

`SURFACE_STATE` for linear textures in the heap, `SAMPLER_STATE` filtering
and wrap modes, `BLEND_STATE`, `DEPTH_STENCIL_STATE`, depth buffer. Then
the `V9X_D3D_ENGINE_OPS` table, and Haswell claims `Direct3D =
'hardware-gen75'`.

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
