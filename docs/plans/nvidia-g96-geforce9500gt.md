# NVIDIA GeForce 9500 GT (G96, 10DE:0640): the GeForce 210's Tesla path for a G9x chip

Date: 2026-11-20

Status: **Phases 0 to 3 done: the 2D engine
(`docs\decisions\2026-11-28-g96-2d-engine-first-run.md`) and flips
(`docs\decisions\2026-11-28-g96-flips-from-the-vbios.md`, paced at the
retrace, picture not seen). Phases 4 and 5: Direct3D on NV84_3D with
Z24S8 (`docs\decisions\2026-11-28-g96-direct3d.md`); V9XD32 passes but
for T5 (by design); DXT is NVIDIA's own decode
(`docs\decisions\2026-11-28-g96-dxt-decode.md`); games and 3DMark not
run.** GitHub issues 29
and 30: the owner whose HD 2500 runs 0.59.0
(`docs\decisions\2026-11-20-hd2500-field-report.md`) also has this card,
256 MiB, in the same ASRock B75M R2.0 board. Since 2026-11-28 the machine
is on remote help as M-Z2T02D, with the 9500 GT as the boot adapter on
Microsoft's Standard VGA driver. Phase 2's probe:
`docs\decisions\2026-11-28-g96-register-probe.md`.

## Why this card, and why not as an alias

The G96 is a Tesla chip, nouveau's NV50 family like the GeForce 210's
GT218, which the `nvidia` package runs: DirectDraw and GDI on the 2D
engine, page flips, Direct3D with pixel shader 1.1, OpenGL. Most of that
path is the NV50 family's, and some of it already speaks G9x: the context
program generator ported from nouveau (`nv50_grctx.c`) takes the chipset
from PMC_BOOT_0 and has branches for chipsets below A0h.

It is not a drop-in, because several pieces name the GT218:

| Piece | GT218 (A8h) | G96 (96h) | Where |
|---|---|---|---|
| Mini-VDD BAR0 map | accepts chipset A8h only | refused | `loader.asm` `V9X_NV50_BOOT_CHIPSET_GT218`, 4128 |
| Read-only probe V9XN5MM | refuses anything but A8h | refused | `nv50_mmio_probe.asm` `N5_BOOT_CHIPSET_GT218` |
| 3D class | `0x8597` (NVA3_3D) | `0x8297` (NV84_3D) | `nv50_chan.h` `V9X_NV50_CLASS_3D`; Mesa `nv50_screen.c` picks NV84_3D for the 80h and 90h families |
| 3D methods | 81 named `V9X_8597_*` | NV84_3D's set | `nv50_3d.h`; each must exist in NV50/NV84_3D, not only NVA0/NVA3 |
| Context program | IS_NVA3F branches | the `< 0xa0` branches | `nv50_grctx.c` (already parameterised; not run on a G9x) |
| Row blocks (tiled kinds) | measured on M-BXMFA8 | not read | `nv50_rblock.c`, from 1540h/100204h/100250h |
| Flips | VGA CRTC CR0C/0D/35/34 behind CR3F = 57h, the GT218 VBIOS's 4F07 | this VBIOS's 4F07 not read | `2026-11-16-gt218-flips-through-vga-crtc.md` |
| Device row | `gt218_hw16.c`, 10DE:0A65 | none | `nvidia` family manifest |

Mesa's class gates (`nv50_state.c`, `nv50_shader_state.c`,
`nv50_vbo.c`, `nv50_state_validate.c`) put independent blend, sample
shading, seamless cubes and stream output behind NVA0 or NVA3; the
Direct3D translator uses none of them that I know of, which Phase 5
checks method by method rather than assumes.

## The survey

`docs\probe\references\asrock-b75m-gt9500-g96-0640-vgasurv-2026-11-20-a5156b16.ini`,
V9XSURV `/rom` from the 0.58.0 tool:

- `10DE:0640` rev A1 on bus 1 (the PCIe slot), subsystem `0000:0000`.
- BAR0 `F6000000` (registers), BAR1 64-bit prefetchable `E0000000` (the
  VRAM aperture), BAR3 64-bit `F4000000`, I/O `E000`, ROM BAR `F7000000`.
- VBIOS 62.94.2C.00.91, "HWEA GeForce 9500 GT", 11/09/09, checksum good,
  56,832 bytes, dumped in the survey.
- VBE 3.0, "G96 Board - 09770001", **14 MiB**, and every mode's linear
  framebuffer at **`F5000000` = BAR3 + 16 MiB**: the GT218's layout
  exactly (14 MiB at BAR3 + 16 MiB, `2026-11-11-gt218-register-probe.md`).
  So the GT218's answer should apply: map BAR1 and size the heap from the
  chip, not from VBE. Unmeasured here.
- Modes 320x200 to 1600x1200 at 8, 16 and 32 bpp.

## Phases

### Phase 0: survey (done)

Above. Open: PMC_BOOT_0 (chipset and stepping), the memory size from PFB
10020Ch, whether the BIOS leaves the BAR VM off (1704h/1708h/170Ch), and
the BAR windows. None of those are visible from DOS without a register
read.

### Phase 1: ask the owner about remote help

Every later phase needs boots with files read back. Over GitHub comments
that is one round trip per boot, and the GT218 took dozens. Ask whether
they can install the remote-help agent on this machine; the plan is the
same either way, only slower without it.

### Phase 2: read-only register probe

Let V9XN5MM accept chipset 96h and have the owner run its default,
read-only pass under the `vbe` package (its `/startmove` and `/starthigh`
modes write the CRTC and wait for Phase 3). Read: PMC_BOOT_0, PFB 10020Ch (size), the BAR VM
registers, 1540h (units), 100204h/100250h (row blocks), and BAR1 + 0
against the VBE window, as `2026-11-11-gt218-bar-windows.md` did. Record
it. Nothing in the driver changes yet.

### Phase 3: tier-0 claim, 2D and flips

A `g96` device row on the GT218's 2D path: the mini-VDD map accepts 96h
beside A8h, the same BAR1 mapping and heap sizing, the 2D engine for
DirectDraw fills and copies (class `0x502d`, the same on both), GDI on the
engine at logon. Before flips: read this VBIOS's 4F07 handler from the
survey's ROM dump and confirm it uses the GT218's CRTC sequence (CR3F =
57h, then CR0C/0D/35/34); if it differs, the flip path follows the ROM.
Gate: V9XDDF, V9XDDP flips, V9XGDI, on the owner's machine.

### Phase 4: the PFIFO channel and PGRAPH on a G9x

The engine (`eng_nv50.c`) brings PFIFO and PGRAPH up itself with the
generated context program. Run the `< 0xa0` branches of `nv50_grctx.c`
in the host test against nouveau's G9x expectations before any boot, then
V9XN5CH's 2D channel probe on the card (it would need the same 96h
acceptance as V9XN5MM).

### Phase 5: Direct3D

The 3D class becomes per chip: `0x8297` for 96h, `0x8597` for A8h,
chosen from PMC_BOOT_0 or a device id now that the engine block carries
one (shared ABI 2026112001, `engine.pci_device`). Audit each of the 81
`V9X_8597_*` methods against Mesa's `nv50_3d.xml.h` for NVA0/NVA3-only
offsets and give each a G9x answer. Then V9XD32 at 16 and 32 bpp, PS 1.1
(the GT218's translator; its known T19 rows come along), 3DMark 2000 and
2001 SE.

### Not planned

Glide's textured draws and the GT218's open issues (undrawn parts of
3DMark 2001 SE frames, Quake 3 wall streaks) are the GT218's; they come
with the path and are fixed there, not here.

## Sources

- nouveau (Linux): `nvkm/engine/gr/ctxnv50.c` (ported), `nvkm/subdev/fb`
  for the G9x row-block and memory reads, `nvkm/engine/disp` for G94-class
  display, none vendored under `build\external\` yet; fetch what Phase 2
  and 4 need.
- Mesa nouveau (`build\external\audit\mesa\src\gallium\drivers\nouveau\nv50`):
  the 3D class choice and the class-gated features above.
- The GT218 records and plans: `docs\plans\nvidia-gt218*.md`.
