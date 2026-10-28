# Carrizo draws its first rectangle: a stale shader cache and a constant-bus violation were in the way

Date: 2026-10-28

Status: **measured, one machine, 11 runs in one session; the solid
rectangle draws pixel-exact, twice in a row.** M-XP7053's Radeon R7E
(`1002:9874`). Phase 3 of [the 3D plan](../plans/ati-carrizo-gfx8-3d.md),
after [gpu_init](2026-10-28-carrizo-gpu-init.md). Reports, in run order, in
[`../probe/m-xp7053-carrizo-first-rectangle-2026-10-28/`](../probe/m-xp7053-carrizo-first-rectangle-2026-10-28/).

## The method

`vi_shader.c` gained probe variants of the two shaders. Each one first
stores markers, through FLAT stores and `s_waitcnt 0`, to a 96-byte area
beside the target (window offset A000h), prefilled with the `5A5A5A5A`
guard. Then it runs the solid shader.
- The VS stores its VertexID, then per vertex its VertexID, the x and y
  it computed, v2, and VCC as its first compare sets it.
- The PS stores its red.
- Store addresses take `VertexID & 3`, so a wrong ID stays inside the
  area.

New encodings (FLAT store, `s_waitcnt`, `v_add_u32`, `v_lshlrev_b32`,
`v_and_b32`) are checked against LLVM's GFX8 assembler tests
(`gfx8_asm_flat.s` added to `build\external\carrizo\llvm-mc\`). Each
shader change had its host test written first.

`CZCP.VXD` can also now read `GRBM_STATUS_SE0` and the RB and shader-array
harvest registers.

## What ran

| Run | Change | Result |
|---|---|---|
| 1 | probe shaders | VS marker `00000000`, PS marker guard |
| 2 | + `VGT_MAX_VTX_INDX` FFFFFFFFh | same |
| 3 | VS marker per vertex at +4 x ID | **nothing** stored |
| 4 | run 1's fixed store kept, per-vertex one masked | **nothing** stored |
| 5 | + SURFACE_SYNC before the draw | VertexIDs 0, 1, 2; PS guard |
| 6 | + positions stored | every vertex (8.0, 4.0) = (x1, y1) |
| 7 | + v2 and VCC stored | v2 = 40.0 (x2); VCC set on vertex 1 only |
| 8 | VGPR operands in both `v_cndmask`s | **draws**: 256 pixels, nothing outside |
| 9 | run 8 without the VGT write | draws |
| 10, 11 | the tree as committed | draws |

Every pixel inside reads `BF4080FF` for (R 1.0, G 0.5, B 0.25, A 0.75).
So in memory R is the low byte, then G, B, A: `COLOR_8_8_8_8` with
`COMP_SWAP` 0 writes RGBA. The fence landed every time.

Harvest registers, every run:
- `CC_RB_BACKEND_DISABLE` = `00000001`; its BACKEND_DISABLE field
  (23:16) is 0;
- `GC_USER_RB_BACKEND_DISABLE` = 0;
- `CC_GC_SHADER_ARRAY_CONFIG` = `FFC00001`;
- `GC_USER_SHADER_ARRAY_CONFIG` = 0.

## What the evidence says

1. **The SQ caches held earlier runs' code.** The shaders go to the same
   addresses every run (VS 4000h, PS 4100h). The end-of-pipe event
   invalidates TC but not the SQ's instruction or constant caches, and
   nothing else uses the GPU. Run 4's first store is the same
   instruction that wrote run 1's marker, yet it stored nothing until run
   5 added Linux's `gfx_v8_0_emit_mem_sync` (SURFACE_SYNC: I$, K$, TC L1
   and L2, L2 writeback). From then on every marker landed. Runs 1-4 are
   therefore not evidence of what their own code did. Run 1's `00000000`
   came from some earlier code or was partly fresh; it cannot be told
   which. Linux emits this before every IB, and the HAL must do the same
   whenever it rewrites a shader.
2. **`v_cndmask_b32 v1, s0, v2, vcc` returned s0 on every lane.** VCC was
   correct (run 7), and so was v2, yet the select took its first operand
   everywhere. The instruction reads two scalar operands, s0 and VCC.
   GCN 3 VALU instructions have one constant-bus read. So the rectangle
   had no area, the scan converter made no pixels, and no PS wave ever
   ran. With both operands moved to VGPRs it draws. The encoder has no
   check for this; the shader builders must keep to the rule themselves.
3. **`VGT_MAX_VTX_INDX` was not the cause.** `clearstate_vi.h` leaves it
   at 0, which suggested the VGT clamps every index. Run 9 drew without
   the write, with IDs 0-2. Whether an auto-indexed draw is clamped at
   all was not tested further. The write stays, as radeonsi has it,
   because indexed draws will need it.
4. **The gpu_init record's draw was not a fair test.** It ran the
   constant-bus-violating VS, so it says nothing about whether gpu_init
   was needed. That remains untested: the configuration stays in the
   hardware until a reboot.

## Not checked

- A draw on a freshly booted machine with this code, before and without
  the gpu_init list.
- The desktop after the runs. The agent kept answering, but no screenshot
  was taken.
- Any target but linear-aligned 32 bpp, any format but 8_8_8_8, and
  `COMP_SWAP` for DirectDraw's ARGB.
- The per-run cost of SURFACE_SYNC.
