# RIVA 128ZX under VBE: PGRAPH off, PFB stale, RAMIN reads zero

Date: 2026-11-02

Machine: M-7V636C, boot 10, the 0.34.0 `vbe` package at 800x600x16.
Plan: `docs/plans/nvidia-riva128.md`, Phase 2. Tool: V9XN3MM.EXE with
N3MM.VXD (build `23f4b53f-dirty`, branch `nvidia-riva128`), read-only.
Files: `docs/probe/m7v636c-riva128-registers-2026-11-02/`.

## What was read

BAR0 (the 16 MiB register window at `1F000000`) at the offsets the probe
allowlists, each twice; PFIFO only once PMC_ENABLE showed it on; PGRAPH
skipped because PMC_ENABLE showed it off. BAR1 (`21000000`) sampled every
100h bytes in two 32 KiB windows, the top of an 8 MiB card and BAR1 +
C00000h, then, with `/dense`, every dword of BAR1 + C00000h at 0-0FFFh,
1C00h-1FFFh and 4700h-48FFh. Two runs; the machine stayed up and the
desktop was undisturbed.

## Results

| Register | Value | Reading |
|---|---|---|
| PMC_BOOT_0 | `00030122` | Chip 3, bits 11:8 = 1, low byte 22h = the PCI revision: the layout the sources give, confirmed |
| PMC_ENABLE | `01100100` | PFIFO (bit 8) on, **PGRAPH (bit 12) off**; bits 20 and 24 also set |
| PFB_BOOT_0 | `00000004` | Bit 5 clear, so Linux's 128ZX size decode does not apply; size not decoded. VBE's 8 MiB stands |
| PFB_CONFIG_0 | `00001114` | Width 14h x 32 = 640, 1 byte per pixel, tiling off: **not the current 800x600x16 mode** |
| PFB_CONFIG_1 | `C1016293` | Not decoded |
| PSTRAPS | `00000DB3` | Not decoded |
| PRAMDAC general control | `00101100` | rivafb writes `00100100` on NV3; bit 12 differs (hypothesis: the 5:6:5 select) |
| PRAMDAC VPLL / MPLL / select | `00025307` / `00017708` / `10010100` | Recorded |
| PFIFO RAMHT / RAMFC / RAMRO | `00000000` / `00001C00` / `00001E00` | RAMHT at RAMIN 0, 4 KiB; RAMFC 1C00h; RAMRO 1E00h, 512 bytes. Not rivafb's layout (RAMFC 2200h, RAMRO 2000h): the BIOS's own |
| PFIFO CACHES, CACHE1 push/pull | all 0 | The FIFO is not running |
| CACHE1 PUT = GET | `017C` | Something went through the FIFO, and drained |
| CACHE1 context 0-7 | `00C50479`, `0000017C` x3, `00C20478`, `00C50479` x3 | Object contexts left from POST: type 45h at instance 479h, 42h at 478h (rivafb's encoding: 40h + type 5 clip, + type 2 ROP) |
| USER channel 0 free count | 0 | |
| PTIMER | all 0 | The timer is not running |
| BAR1 + C00000h, every dword read | 0 | Including the whole RAMHT and the instances the contexts name |
| Top 32 KiB of VRAM, every 100h | 0 | |

## What this settles

- **The identity check works.** PMC_BOOT_0 is what envytools says, so the
  probe's chip-3 test is sound and the revision sits in the low byte.
- **The 2D engine is off under VBE.** Using it starts with setting
  PMC_ENABLE bit 12, as rivafb's `LoadStateExt` does (it toggles PFIFO and
  PGRAPH off and on).
- **The engine's view of the screen is not maintained by the VBE.**
  PFB_CONFIG_0 still describes a 640-wide 8 bpp surface after an 800x600x16
  mode set. The driver has to write it after every mode set, as rivafb does
  before it loads RAMIN.
- **The BIOS used the engine at POST and set up its own FIFO tables.** The
  driver cannot rely on any of it: RAMIN reads zero where those tables say
  their contents are.

## Hypotheses this leaves

- RAMIN reads zero at BAR1 + C00000h because the VBE mode set cleared
  video memory, RAMIN included, and CACHE1's contexts are stale. Or BAR1 +
  C00000h does not show RAMIN on this card, and RAMIN is somewhere this
  probe did not read (the emulator's reversed top-of-VRAM layout puts RAMIN
  0 at the last 16 bytes, which the sparse top-window read missed). A read
  cannot tell these apart: the next probe writes a pattern into the top of
  VRAM through the frame buffer and looks for it at BAR1 + C00000h. That
  writes video memory only, no register.
- PRAMDAC general control bit 12 selects 5:6:5. Checked when a 5:5:5 mode
  is set, or by the 16 bpp decision.
