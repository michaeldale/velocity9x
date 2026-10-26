# The HD 5450 draws: Linux 3.9's textured copy is byte-exact on 1D-tiled surfaces, and wrong on linear ones

Date: 2026-10-27

Status: **measured, one card, six runs on one boot.** M-BXMFA8's HD 5450
(`1002:68F9`, Cedar), boot 7, the `ati` driver with DirectDraw on the DMA
engine running. Phase 2 of
[ati-evergreen-cedar-3d.md](../plans/ati-evergreen-cedar-3d.md). New tool:
`V9XEG3D.EXE`, sharing `EGCP.VXD` with V9XEGCP
(`tools\diag\evergreen_3d_win32.c`, `scripts\build-evergreen-cp-probe.ps1`).
Captures:
[`../probe/m-bxmfa8-cedar-3d-2026-10-27/`](../probe/m-bxmfa8-cedar-3d-2026-10-27/).

## What the probe does

It reads the CP's registers and every register Linux
`evergreen_gpu_init` touches, then in one run:

1. **Window.** Saves the window at BAR0 + 241 MiB and writes into it a
   source image (a 64x8 RGBA pattern), a guarded destination, the ring,
   and the shaders and vertices.
2. **GPU configuration.** Writes `evergreen_gpu_init`'s configuration for
   Cedar: 45 registers, every value derived in the source from Cedar's
   numbers, read-modify-write where Linux reads first.
   - `GB_BACKEND_MAP` is 0: `r6xx_remap_render_backend` gives 0 for one
     render backend whatever the fuses say, so the RCU straps are not read.
   - The context registers Linux writes (28000h and up) are left to
     `CLEAR_STATE` and packets.
   - `DMIF_`, `HDP_ADDR_CONFIG` and `DMA_TILING_CONFIG` keep the BIOS's
     values, because the display, the host path and the running DMA
     engine use them.
3. **CP start.** Loads the microcode, resets, sends `ME_INITIALIZE` and
   `evergreen_cp_start`'s clear-state preamble, then Linux's ring test.
4. **The draw.** Linux 3.9's blit sequence, using its
   `evergreen_default_state`, VS and PS (AMD, MIT) and checked here
   dword for dword against the Linux file:
   - `set_default_state`, with the state inline rather than through an
     indirect buffer;
   - `set_shaders`, `set_tex_resource`, `set_render_target`,
     `set_scissors`, `set_vtx_resource`;
   - a RECTLIST of three vertices;
   - the CB surface sync, then `SCRATCH_REG0` written through the ring.
5. **Teardown.** Waits for GUI idle, halts the CP, resets the CP blocks
   and puts every register back. It then flushes HDP, reads the
   destination back and restores the window.

## Results

| Run | Surfaces | CP init, preamble, ring test | Draw fence | Destination |
|---|---|---|---|---|
| 1, 2, 6 | linear aligned | pass | pass | **384 of 512 dwords wrong**, guard intact |
| 3, 4, 5 | 1D tiled, as Linux drew | pass | pass | **byte-exact**, guard intact |

In every run the CP's read pointer reached the end of the draw (`290h`
dwords), the GUI went idle, and the CP was halted again afterwards. V9XDDP
on the DMA engine passed after the six runs.

So the 3D engine runs Linux's pipeline on this card: the configuration,
clear state, the vertex fetch, both shaders, the texture fetch, the
colour buffer and the CB flush.

## The linear failure

In every 8-pixel group of every row, destination dwords 0 and 1 hold
source dwords 4 and 5, dwords 6 and 7 hold source 6 and 7 (correct), and
dwords 2 to 5 are never written. The same in all three linear runs (the
`[Map]` section of the run 2 and run 6 captures; run 1 predates the map
and shows the same first dwords). The unwritten
dwords put the fault on the colour-buffer side, not the texture fetch.

Not explained yet. The hypotheses, none tested:

- Linear-aligned colour buffers need a field this configuration leaves
  at its clear-state value. Candidates are `CB_COLOR0_ATTRIB`, the info
  word's number type or endian fields, or slice and pitch rules beyond
  `evergreen_cs.c`'s 64-pixel pitch.
- The two-pipe address configuration (`GB_ADDR_CONFIG` = `02010001`,
  Linux's golden value) differs from the `02011003` the BIOS left in
  `HDP_ADDR_CONFIG`, and linear colour writes still pass through pipe
  routing.

DirectDraw's surfaces are linear, so this has to be solved before
Direct3D can render into them. The alternative is tiled render targets
with a tiled-to-linear copy.

## Not measured

- Any draw other than a textured copy: no Gouraud, no depth buffer, no
  blending.
- The CP or 3D engine across a mode set or a DOS box.
- Whether the configuration needs every one of Linux's 45 writes; none
  was left out to find out.
