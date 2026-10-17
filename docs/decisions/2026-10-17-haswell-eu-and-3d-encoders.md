# Haswell EU and 3D encoders: what is encoded, from which tables, and the traps on the way

2026-10-17. Phases E and F groundwork of `docs\plans\intel-haswell-direct3d.md`.
Pure C, host-tested, nothing submitted to hardware. Files:
`include\velocity9x\intel_hsw_eu.h`, `src\chipsets\intel\hsw_eu.c`,
`include\velocity9x\intel_hsw_3d.h`, `src\chipsets\intel\hsw_3d.c`,
`tests\host\test_intel_hsw_eu.c`, `tests\host\test_intel_hsw_3d.c`.

## Encoded

**EU instructions** (`hsw_eu.c`), from HSW PRM Vol 7 "Instruction Machine
Formats": DW0 operation, DW1 destination and the five type/file fields, DW2
source 0 with the flag selection at bits 26:25, DW3 source 1 / immediate /
send descriptor, and the separate 3-source layout (DW1 3-src, DW2 and DW3
3-Source).

- 1- and 2-source, direct addressing, Align1 and Align16; immediate as the
  last source only.
- 3-source (`mad`, `lrp`), Align16 only, dword sub-register numbers with
  source 1's split across DW2 31:30 and DW3 0.
- `send`: SFID in DW0 27:24 (Vol 2a SFID), MsgDescpt31 in DW3 30:0 with EOT
  in bit 31; the general descriptor (message length 28:25, response length
  24:20, header 19, function control 18:0, Vol 2d MsgDescpt31); the render
  target write control (Vol 2d "Message Descriptor - Render Target Write":
  last RT 12, slot group 11, type 10:8) under the data port's Render Target
  Write message type 1100b at 16:13 (Vol 7 render cache table); the sampler
  control (Vol 7: SIMD mode 18:17, type 16:12, sampler 11:8, BTI 7:0).
- A decoder for all of the above; every test encoding round-trips.

**Commands** (`hsw_3d.c`): a 40-entry table of subtype, opcode, sub-opcode
and total length, each read from the command's own DWord 0 table in Vol 2b,
plus MI_NOOP, MI_BATCH_BUFFER_END, MI_STORE_DATA_IMM and
MI_LOAD_REGISTER_IMM. Builders with fields: PIPELINE_SELECT, STATE_BASE_
ADDRESS, PUSH_CONSTANT_ALLOC_*, URB_*, the disabled VS/HS/TE/DS/GS/
STREAMOUT, the state pointer commands, DRAWING_RECTANGLE, VERTEX_BUFFERS
(one buffer), VERTEX_ELEMENTS, 3DPRIMITIVE, PIPE_CONTROL. CLIP, SF, SBE, WM
and PS are in the table with their lengths but have no field builders yet;
they are Phase D's first job.

**State** (Vol 2d): RENDER_SURFACE_STATE for a linear 2D surface,
SAMPLER_STATE (filters, LOD, border pointer; DW3 raw), BLEND_STATE,
DEPTH_STENCIL_STATE (depth only), COLOR_CALC_STATE. Surface format codes
(Vol 2a SURFACE_FORMAT), topologies (3DPRIM_*), vertex component controls
(VFCOMP_*).

**Checker**: walks a batch, refuses unknown commands, wrong fixed lengths,
malformed variable lengths, truncation, a missing MI_BATCH_BUFFER_END and
an odd dword count.

## Traps found in the reading

- **SCS_ZERO is 0.** Haswell's RENDER_SURFACE_STATE DW7 has a shader
  channel select per channel; an all-zero DW7, the obvious default, samples
  every channel as 0.0 and keeps a render target from being written. The
  builder sets R, G, B, A to SCS_RED..SCS_ALPHA (4-7).
- **Align16 destination horizontal stride** is "a don't care for Align16,
  HW needs this to be programmed as 01."
- **Non-present operands**: a 1-source instruction's src1 "must be set to
  null and also with the same type as the first source operand src0"; the
  encoder does this itself rather than leaving the field to the caller.
- **Instruction prefetch**: the EU may read "up to 8 instructions (128
  bytes) beyond the end of the kernel program"; kernel placement must pad
  or keep the following page valid.
- **Render target write refuses BTI 255** (stateless); the encoder refuses
  it too.
- **Haswell's 3DPRIMITIVE needs Instance Count 1**, per its own note.

## How the tables were read, and what is not from the PRM

`pdftotext -layout` misaligns the enumeration tables in Vol 2a (SFID,
3DPRIM, SURFACE_FORMAT): values and names sit a row apart. The same tables
extracted with `-raw` pair correctly and are what the constants use; the
two readings agree in order for SFID and 3DPRIM.

Not from the public PRM, and marked as such in the code:

- 3DSTATE_MULTISAMPLE's length (4 dwords) is from Vol 7's render-context
  table; Vol 2b prints no default for it.
- The checker's whole-qword rule is i915's batch discipline; the excerpts
  read here do not state it.

## Testing

Expected dwords in the tests were worked by hand from the field positions,
so they pin the code to the tables as read; they cannot show the reading
is right. Two expectations were broken deliberately to confirm both new
test files fail when wrong, then restored. Gates: build-host, run-checks.
