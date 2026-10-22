# Ironlake Direct3D: the GM965's engine with Gen5's encodings

Date: 2026-10-22
Status: **measured on one machine with one probe.** Follows
`2026-10-22-ironlake-igma-flips.md` on W9QI2F4F (`M-J0F0SJ`). Captures:
`docs\probe\w9qi2f4f-ironlake-d3d-2026-10-22\`.

## What changed

The GM965's Gen4 engine (`g4_3d.c`, `g4_eu.c`, `d3d_g4.c`) now takes a
generation, and the Ironlake descriptor keeps D3D. Every Gen5 difference
below comes from the Ironlake PRM, Mesa's genxml `gen5.xml` and xf86's
`i965_render.c` / `.g4b.gen5` kernels, the last of which ran on this part:

| Item | 965 / GM965 | Ironlake |
|---|---|---|
| PIPELINE_SELECT | 61040000h | 69040000h (sub-type 1) |
| STATE_BASE_ADDRESS | 6 dwords | 8: Instruction Base in DW4, three upper bounds |
| 3DSTATE_DEPTH_BUFFER | 5 dwords | 6, DW5 the depth coordinate offset |
| VERTEX_BUFFER_STATE DW3 | maximum index | end address (last byte) |
| VERTEX_ELEMENT_STATE | destination offsets | MBZ; a zero element first fills the VUE header |
| VS_STATE URB entries | count | count / 4 |
| WM_STATE | 8 dwords | 11; binding and sampler counts written 0, as xf86 |
| EU `send` descriptor | SFID and EOT in DW3 | SFID DW2 31:28, EOT DW2 26 and DW3 31, mlen 28:25, rlen 24:20, header 19, sampler SIMD 17:16 |

The host test `test_encoder_against_xf86_gen5` checks the encoder's math,
URB, sampler and render-target sends against xf86's Gen5 words;
`test_batch_gen5` walks a Gen5 batch. `V9X_DD_ENGINE_CAPS_I9XX_G4_3D`
(GEN4 | GEN5) replaces the GEN4 bit at the gates that pick the Gen4 engine:
`d3d_select.c`, `d3d_i9xx.c` (Gen3 refuses it), and `eng_i9xx.c` (the
Y-tiled Z fill).

## Measured

All by `V9XD32.EXE`, the in-tree Direct3D probe. The new `V9XHAL.DLL` went in
by WININIT rename, and the machine was restarted with `reset`.

| Boot | Run | Result | Ring HEAD = TAIL |
|---|---|---|---|
| 49 | `/bpp16` (mode switch to 640x480x16) | PASS | 0 to 280h |
| 50 | `/bpp16`, from a fresh boot | PASS | 0 to 280h |
| 51 | 32 bpp, Z16, no mode switch | PASS | 0 to 280h |
| 53 | 32 bpp, Z16 (restored HAL, see below) | PASS | 0 to 280h |
| 53 | 32 bpp, Z24 | PASS | 280h to 4E0h |

PASS means each of T1 (flat), T2 (Gouraud corners), T3 (depth test),
T5 (depth precision), T6 (texture: four texels), T7 (blend), T4 (offscreen
device) read back as expected. After boot 49: `EngineCaps=00100414`,
`EngineStatusEnable=d3d-claimed`, `Direct3DMode=hardware`. `T7Average` at
16 bpp was 847D00h, the GM965's value. The Z read-back shows Y-tiled depth
(`ZTiledT3Front=4000h`, `ZLinear*` all FFFFh), as on the GM965.

## Open: ESR bit 0 after every Direct3D run

`V9XG5MM` reads ESR (20B8h) as **00000001** after every Direct3D run above.
In the PRM's hardware-detected error table, bit 0 is "Instruction Error":
an unsupported client ID, or a defeatured MI opcode. EMR (FFFFFF3Fh) masks
it, so EIR stays 0, and IPEIR and IPEHR (2064h, 2068h) read 0. Rendering
is correct and the ring drains.

The bit is cleared by a reset only. Where it does and does not appear:

- Every fresh boot reads 0: boots 50 to 54, before anything runs.
- Blits alone leave it 0: V9XDDF on this same build, boot 54; and on boot
  47, before D3D existed.
- `V9XD32` sets it with or without a mode switch (boots 50, 51).

So it comes from the 3D batch path.

`V9XTRACE.EXE` now dumps the ring and every batch the ring names, read
through the aperture (`boot50-after-d32-V9XTRACE-ring.INI`). All 160 ring
dwords are commands this driver emits: the status-page proof blit,
XY_COLOR_BLT fills, MI_FLUSH, MI_NOOP, and MI_BATCH_BUFFER_START 18800180h
with breadcrumb blits. Both batches (F32000h and F33000h) decode with
every header known and every length matching, ending in MI_FLUSH and
MI_BATCH_BUFFER_END. Nothing in the stored stream is an MI opcode or
client ID the PRM calls invalid.

**Hypothesis killed: MI_BATCH_BUFFER_START's 2 << 6.** The Ironlake PRM
(Vol 1 Part 3 1.3.3) defines bits 7:0 as DWord Length and bit 8 as
"Buffer Security and Address Space Indicator". It does not document the
965's address-space field at 7:6, so the 80h there read like a length
error. Boot 52 sent 18800100h on Gen5, with a host test written first and
watched fail. The ring stopped at the first batch: HEAD 68h, TAIL A8h,
IPEHR FFFFFFFFh, and V9XD32 failed (`Failures=7`). ESR was still 1. So
2 << 6 does select the GTT on Ironlake, as i915's
`MI_BATCH_GTT | MI_BATCH_NON_SECURE_I965` has it, and the PRM's field list
is incomplete. The change was reverted. Boot 53 is the restored HAL,
passing again.

**Not yet done.** Reading ESR after each batch inside the HAL would name
the batch. Nothing here says the bit costs anything.

## Not measured

- A game, 3DMark, or anything beyond V9XD32's seven tests.
- Hangs under load. The GM965's alpha-test-with-depth-write hang
  (`2026-10-21-gm965-*`) has not been tried here. The Gen4 engine still
  refuses that state to software on both parts.
- What the screen showed during the probe.
