# ATI Rage 128 in the ati family

Date: 2026-10-22

Status: Phases 0-2 done on A8U4I5's Rage Fury Pro (`1002:5046`):
ATI's driver measured as the reference, tier-0 enable-ok with GDI,
seven live mode switches and DirectDraw on the CPU
([record](../decisions/2026-10-22-rage128-ati-baseline-and-tier0.md)), and
the registers read under both drivers: ATI runs its command engine as a
bus-master ring even at the desktop, and the engine is idle in NONPM4 after
POST ([record](../decisions/2026-10-22-rage128-register-probe.md)).
Decisions 1 and 2 recommendations accepted by Michael (2026-10-22).
Phase 3 in progress: DirectDraw fill and copy run on the 2D engine
(ATI_RAGE128), byte-exact at 8/16/32 bpp in the write probe and
pixel-correct through the HAL at 16 bpp
([record](../decisions/2026-10-22-rage128-directdraw-on-the-2d-engine.md)).
Owed in Phase 3: GDI acceleration, the 32 MiB mapping, engine reset.
Decision 1 taken on measurement (2026-10-23): the CCE runs Linux's
microcode in PIO mode, packets to EVEN/ODD in pairs, and drew an
untextured triangle through `3D_RNDR_GEN_PRIM` once the 3D context
scissor was opened
([record](../decisions/2026-10-22-rage128-cce-pio-runs-packets-and-a-triangle.md)).
The focus of 0.26.

Target: the Rage 128 family as a fourth chip in the one `ati` family
(`one-family-per-vendor`), beside the Mach64 VT2, the Rage Pro class and
the Rage II class. The silicon shares nothing with the Mach64 below the
PCI/VGA level, so it gets its own module tree, `src/chipsets/ati/rage128/`,
and its own engine type, never `ATI_MACH64` or `ATI_RAGE2`.

## Sources

Collected 2026-10-22 into `build\external\rage128\` (outside version
control: the ATI manuals are marked confidential). `RESEARCH.md` there
cites every claim below by file and line.

- **Rage 128 VR/GL Register Reference Guide** (RRG-G04100) and **Rage 128
  PRO Register Reference Guide** (RRG-G04500 rev 1.01), from the bitsavers
  mirror. Configuration, PLL, CRTC, DAC, cursor and the 2D engine in full.
  Neither has the 3D or CCE chapters.
- **xf86-video-r128** (current and 6.10.2), **Linux aty128fb** and **DRM
  r128** (v6.2, and v2.6.27 with the CCE microcode inline), **Mesa 7.11
  r128**. The de facto 3D and CCE reference.
- Not public: ATI's Rage 128 Software Development Manual (SDK-G04000),
  which would describe the 3D setup registers.

## What the sources settle

| | Rage 128 | Consequence |
|---|---|---|
| BAR0 | 64 MiB prefetchable framebuffer (two 32 MiB copies) | Map 32 MiB at most |
| BAR1 | 256-byte I/O | Unused |
| BAR2 | 16 KiB registers, two 8 KiB copies | A new mini-VDD map; the Mach64 one is 4 KiB and checks `CONFIG_CHIP_ID` |
| Memory size | `CONFIG_MEMSIZE` (0x00F8), bytes, 1 MiB granularity | A `read_video_memory` hook, measured against VBE's 32 MiB |
| Identity through BAR2 | read-only copy of PCI config at 0x0F00 | The map's identity check reads vendor/device there |
| 2D engine | `DP_GUI_MASTER_CNTL`, `DST_Y_X`, `DST_WIDTH_HEIGHT`, `*_PITCH_OFFSET`; 64-entry FIFO in `GUI_STAT[11:0]`, busy bit 31 | Direct register writes with `PM4_BUFFER_CNTL` = NONPM4 |
| Pixel cache | `PC_NGUI_CTLSTAT` flush and busy | Flush before the CPU touches what the engine drew |
| Engine reset | `GEN_RESET_CNTL` soft reset with `MCLK_CNTL` forced clocks | The recover hook |
| Scanout start | `CRTC_OFFSET` (0x0224) | Flips |
| Palette | `PALETTE_INDEX`/`PALETTE_DATA`, `DAC_CNTL` bit 8 for 8-bit | Gamma |

## What they do not settle

**How to drive the 3D engine.** Every open driver submits 3D through the
CCE, a microcoded command processor fed packets. The chip also has direct
setup-engine registers (0x1B00-0x1BD4: `VERTEX_*`, `SETUP_CNTL`,
`DRAW_LINE_POINT`), but no available document gives their layout. The
CCE microcode is MIT-licensed (linux-firmware `WHENCE`, "Copyright 2000
Advanced Micro Devices"), so shipping it is open to us. The CCE has PIO
modes that take packets through MMIO writes to `PM4_FIFO_DATA_EVEN/ODD`
(0x1000/0x1004), with no bus mastering, but no modern code exercises them:
the Linux 6.2 kernel refuses every non-bus-master mode.

## Phases

### Phase 0: identify and baseline (done 2026-10-22)

The card, ATI's 4.13.7192 driver, 3DMark 2000 2404, Half-Life mwd5 D3D
34.7 cold / 59.2-59.5 warm and GL 61.1 cold / 70.1-70.3 warm. Reference
figures, not targets.

### Phase 1: tier-0 (done 2026-10-22)

`rage128_hw16.c` with no hook; the backend accepts `5046` by exact id.
Enable-ok at 800x600x16 on boot 519.

### Phase 2: read-only register probe (done 2026-10-22)

A Win32 probe (in the ATIRX / V9XMGAPC mould) maps BAR2 and reads, never
writes: `CONFIG_MEMSIZE`, `CONFIG_APER_SIZE`, `MEM_CNTL`, the PCI copy at
0x0F00, `GUI_STAT`, `PM4_BUFFER_CNTL`, `PM4_MICRO_CNTL`, `CRTC_GEN_CNTL`,
`CRTC_OFFSET`, `CRTC_PITCH`, `DAC_CNTL`, `DEFAULT_OFFSET`/`PITCH`, and
`CLOCK_CNTL_INDEX`. Run it twice: under ATI's driver (after Half-Life or
3DMark, so the CCE state ATI leaves is visible) and under ours. That
answers whether ATI runs the CCE and in which mode, which is the first
evidence for the 3D decision below.

Exit: a decision record with both dumps; the memory-size and identity
reads coded as a `read_video_memory` hook and a mini-VDD map, with a host
test for the pure decode.

### Phase 3: the 2D engine for DirectDraw and GDI

`ATI_RAGE128` engine type in the HAL: solid fill and screen copy with
direct writes (NONPM4), FIFO waits on `GUI_STAT`, pixel-cache flush, soft
reset in `recover`. The pitch/offset word, the direction bits and the
datatype per depth are pure functions with host tests. Measured on the
card at 8, 16 and 32 bpp with V9XDDF and V9XGDI `/accel`. Then the 32 MiB
mapping, so the heap sees the whole card.

### Phase 4: flips and the vertical blank

`CRTC_OFFSET` for flips, a vertical-blank source from the CRTC status, seen
on the monitor (Michael), as on the Rage IIC and the Mystique.

### Phase 5: Direct3D

Decision 1 below picks the submission path. Then the engine: TLVERTEX in,
two texture units, 16 and 32 bpp with Z16 and Z24/Z32 per the sources,
V9XD32, V9XDDP, 3DMark 2000 to its score, then Half-Life D3D.

First form done 2026-10-23
([record](../decisions/2026-10-23-rage128-direct3d-through-the-cce.md)):
`d3d_r128.c` over the CCE in PIO mode, one texture unit, Z16, 16 and 32
bpp. V9XD32 passes at both depths; 3DMark 2000 runs to 993; Half-Life D3D
mwd5 31.2 fps warm, frames unseen. Then the mip offset order fixed, flips
through `CRTC_OFFSET` (Phase 4's flip, measured in 3DMark) and the CCE
left running between batches: 3DMark 1105
([record](../decisions/2026-10-23-rage128-mip-order-flips-and-a-running-cce.md)).
Owed: V9XDDP; Half-Life watched on the monitor; Z24/Z32 and the second
unit; a bus-master ring in place of PIO; the power-on "one draw late"
retry.

### Phase 6: OpenGL, Glide and 3DCIF through the render interface

The render interface on the new engine, as on the Millennium II. Half-Life
GL is the game gate.

Probes done 2026-10-23
([record](../decisions/2026-10-23-rage128-render-interface.md)): the mixed
frame passes, refused draws fall back to the CPU, explicit draws go on the
engine with the scissor and write mask, and V9XR3DP, V9XGLP, V9XGLIDP and
V9XGL3P pass. Half-Life OpenGL then ran at 0.75 fps until the describe
offered surface textures; with them, 12.0 cold and 14.1 warm, every
textured batch on the engine. Owed: a Glide title; Half-Life GL watched
on the monitor.

## Decisions owed

1. **3D submission: CCE in PIO mode, CCE with a bus-master ring, or the
   direct setup registers.** Recommendation: CCE in PIO mode first. It
   needs no system-memory ring, no AGP/GART and no DMA from a VxD, and its
   packet format is fully shown in the X, DRM and Mesa sources. If PIO
   proves broken on silicon (nothing current uses it), a bus-master ring
   in locked system memory next. The direct registers only if Phase 2's
   dump or a trace of ATI's HAL shows ATI uses them. Phase 2 provides the
   evidence before this is taken.
2. **Ship the CCE microcode in the HAL.** MIT, with the notice in the
   package. Needs Michael's agreement before it is committed.
3. **The other Rage 128 ids.** 37 ids by the research (Rage 128 GL/VR,
   4X, Pro, Pro Ultra, Mobility M3/M4). Recommendation: bind them as
   aliases of `rage128` after Phase 3 passes on `5046`, except the
   Mobility M3/M4, whose panel and second CRTC registers are a separate
   question.

## Not in scope

The Rage Fury MAXX (two chips, AFR) beyond what one chip does; the
Mobility panels; video overlay and capture.
