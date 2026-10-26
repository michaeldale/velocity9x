# Radeon HD 5450 registers, read: 1 GiB at GPU address 0, the DMA engine idle and out of reset

Date: 2026-10-26

Status: **measured, read-only, one card.** M-BXMFA8's HD 5450
(`1002:68F9`, Cedar), Phases 1 and 2 of
[ati-evergreen-cedar.md](../plans/ati-evergreen-cedar.md).
New tool: `V9XEGMM.EXE` + `EGMM.VXD` (`tools\diag\evergreen_mmio_probe*`,
`scripts\build-evergreen-mmio-probe.ps1`). Captures:
[`../probe/m-bxmfa8-cedar-registers-2026-10-26/`](../probe/m-bxmfa8-cedar-registers-2026-10-26/).
Machine state: boot 3, the 0.30.1 `vbe` package installed (not the
`ati` build with the new claim, which has not been installed).

## Phase 1: the claim

`1002:68F9` is a fifth chip, `cedar`, in the one `ati` family:
`src\chipsets\ati\evergreen\cedar_hw16.c`, a `V9X_HW16_DEVICE` with no
hook, the id in `v9x_ati_device_ids[]`, the manifest and the backend
registry. Nothing reads a BAR; the VBE sets modes and the CPU draws.
`run-checks` passed with it. It has not been installed on the card.

## The tool

The EXE finds the card by a PCI scan (mechanism #1, CF8h restored) and the
VxD maps BAR2, the 128 KiB register window, and reads an allow-listed set
of 220 offsets twice each. It writes nothing to the card. BAR2 is a 64-bit
BAR; the VxD reads its high half at 1Ch and refuses to map if it is not
zero.

Evergreen has no copy of configuration space in its register file that a
source to hand names, so the identity check is weaker than the Rage 128
probe's: `CONFIG_MEMSIZE` must read between 1 and 4096. It read 1024.

Refused: the index/data pairs at 0000h-001Bh and 08F8h, the HDP flush
trigger at 5480h, and the palette data ports. Only the four display
controllers Cedar has are read (Linux: `num_crtc = 4`).

## What it read

PCI configuration:

| | Value | Reading |
|---|---|---|
| Location | bus 1, device 0 | behind the Q45's PCIe root port |
| BAR0 | `D000000C`, high `00000000` | 64-bit prefetchable, at D0000000 |
| BAR2 | `FEAE0004`, high `00000000` | 64-bit, at FEAE0000 |
| BAR4 | `0000D001` | I/O at D000 |
| ROM | `FEAC0000` | |
| Command | `0007` | I/O, memory and bus master on |

Memory controller and engines, at 1024x768x16:

| Register | Value | Reading |
|---|---|---|
| `CONFIG_MEMSIZE` | `00000400` | **1024 MiB**. VBE reports 16 MiB. BAR0 shows 256 MiB of it |
| `MC_SHARED_CHMAP` / `MC_ARB_RAMCFG` | `00001FE0` / `0000129D` | 2 channels of 32 bits: a 64-bit bus |
| `MC_VM_FB_LOCATION` | `003F0000` | VRAM at GPU address 0 to 3FFFFFFFh |
| `MC_VM_AGP_TOP/BOT/BASE` | 0, 0, 0 | not the disabled pattern Linux writes (TOP and BOT 0FFFFFFFh); what TOP = BOT = 0 decodes was not tested |
| `MC_VM_SYSTEM_APERTURE_*` | 0 | |
| `VM_L2_CNTL` / `VM_CONTEXT0_CNTL` | `0005C002` / 0 | GART off |
| `HDP_NONSURFACE_*` | base 0, info `00020080`, size `7FFFFBFF` | set up by the BIOS |
| `GB_ADDR_CONFIG` | 0 | never programmed: the BIOS does not do Linux's `evergreen_gpu_init` |
| `DMA_TILING_CONFIG` | `02011203` | programmed |
| `GRBM_STATUS` | `00003828` | GUI idle |
| `CP_ME_CNTL` | `14000000` | **ME and PFP halted**: no microcode loaded |
| `SRBM_SOFT_RESET` | 0 | **the DMA block is not held in reset** |
| `DMA_RB_CNTL` / `DMA_RB_BASE` | 0 / 0 | **no ring**, enable bit clear |
| `DMA_STATUS_REG` | `44C83D57` | **bit 0 set: idle** |
| `DMA_CNTL` | `38421000` | |
| `DMA_MODE` | 0 | Linux writes 1 on RV770 and later |

Display controller D1 against the VBE mode set, three modes:

| VBE mode | `GRPH_CONTROL` | depth / format | `GRPH_PITCH` | viewport | surface |
|---|---|---|---|---|---|
| 640x480x8 | `00108008` | 8 / 0 | 640 | 640x480 | GPU 0 |
| 1024x768x16 | `00108109` | 16 / 1 (565) | 1024 | 1024x768 | GPU 0 |
| 1280x1024x32 | `0010800A` | 32 / 0 | 1280 | 1280x1024 | GPU 0 |

D1 is the only controller enabled. Bits 22:20 of `GRPH_CONTROL` read 1,
which Linux names linear-aligned. `CRTC_STATUS_HV_COUNT` moved between
the two reads, so the reads see a live controller.

## What this settles for the plan

- **The scanout address is a GPU address, and the VBE's framebuffer is at
  GPU 0 = BAR0 offset 0.** A flip writes `GRPH_PRIMARY_SURFACE_ADDRESS`
  with the surface's offset into VRAM, nothing added.
- **The DMA engine is usable as the BIOS left it**, as far as reads can
  say: out of reset, idle, no ring. Bringing it up is Linux's
  `r600_dma_resume` sequence with the ring in VRAM. Whether a fill
  actually lands is the write probe's question.
- **There is 1 GiB of VRAM**, 256 MiB of it CPU-visible through BAR0.
  The `read_video_memory` hook has a number to read.
- The CP is halted: nothing on this card draws through the shader core
  without AMD's microcode.

## Not measured

- Anything written: no ring has been started.
- What the DOS box or a mode set does to these registers beyond the three
  modes above. VBE 0103h (800x600x8, pitch 832) was not read.
- Whether BAR4's I/O ports alias the registers (MM_INDEX/MM_DATA). The
  probe refuses to write MM_INDEX, so it cannot tell.
