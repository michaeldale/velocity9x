# ATI Mach64 / Rage Mobility hardware audit

Status: accepted (as a reference document), 2026-08-16
Scope: informs the `ati` family (phase 10) and the `eng_mach64.c` engine stage

## Why this exists

The physical target is a Gateway Solo 2150 with an **ATI Rage Mobility-M AGP**
(`PCI 1002:4C4D` rev 0x64, Mach64 "LM"), BAR0 LFB `F5000000`, BAR1 I/O `0x2000`,
BAR2 MMIO `F4100000`, BIOS `MACH64LMPCIMTSDU`, fixed 1024x768 internal LCD, no
EDID. The stock ATI Windows 9x driver already scores 100% visual appearance in
Final Reality, so the goal is not parity - it is **more performance and more
stability than ATI's own driver**. That requires knowing what the silicon can do
and where its documented hazards are, rather than inferring from what a 1999
Windows driver happened to implement.

Three sources were audited. All are licence-compatible with this project
(GPL-3.0), one-way:

| Source | Licence | Role |
|---|---|---|
| `xf86-video-mach64` (X.Org / X11Libre) | MIT/X11, **partly copyright ATI Technologies** | Primary hardware reference |
| FreeBE/AF `mach64` (`C:\everything\freebs12\freebe\mach64`) | "may be distributed and modified without restriction" | Bare-metal DOS cross-check |
| 86Box `vid_ati_mach64*.c` | GPL-2.0-**or-later** | Defines what our dev target implements |

**Usage rule.** Register offsets, bit fields and hardware programming sequences
are *facts about the hardware* and are recorded here as such. Velocity9x source
remains independently written, per `docs\ddk-inputs.md`. This document is the
interface between the two: implement from this, not by transcribing driver code.

---

## 1. The addressing model

Every Mach64 register has three addresses. Getting this wrong is the most common
way to "work in the emulator, fail on silicon".

- **MMIO offset** = `(index & 0xFF) * 4`; **block** = `index >> 8`.
- **Block (dense) PIO port** = `IObase + index*4` - with our BAR1, `0x2000 + n`.
- **Sparse (legacy) PIO port** = `(sparse_index << 10) | CPIOBase`, `CPIOBase` in
  {`0x02EC`, `0x01CC`, `0x01C8`}. This is where the classic `0x42EC`/`0x6EEC`
  ports come from.

**Use MMIO or block I/O. Never sparse.** 86Box maps only 32 sparse blocks and
does not map the GUI engine there at all - unmatched ports silently alias
register 0x000, so a sparse-I/O engine access returns garbage rather than
faulting. FreeBE's `get_mach64_port()` shows both models side by side and is a
useful sanity check on the arithmetic.

All Mach64 non-VGA registers are **little-endian regardless of access path**.

## 2. Register apertures - and a divergence between our two venues

Two 1 KiB blocks. **Block 0** is the classic set (CRTC, DAC, engine, config);
**Block 1** is overlay/scaler/capture, present only on `Chip >= 264VT`.

X.Org's placement logic: prefer the dedicated MMIO BAR, else fall back to the
tail of the linear aperture.

| | Block 1 | Block 0 |
|---|---|---|
| **Real Rage Mobility** - BAR2 `F4100000` | `F4100000` | **`F4100400`** |
| **86Box VT2** - no MMIO BAR, 8 MiB LFB | `F57FF800` | **`F57FFC00`** |

**This is a real divergence, not a detail.** 86Box exposes no separate MMIO BAR
(its BAR1 is the block-I/O BAR and reaches only registers `0x000-0x0FF`, not the
engine). So the emulator must use the in-aperture window and the laptop should
prefer BAR2. Two further traps:

- 86Box **aliases** the register page at `+0x000` and `+0x400` as well as the
  hardware-correct `+0xC00`. Those aliases do not exist on real silicon. Point
  `regbase` at `+0x7FF400` and it works perfectly in the VM and fails on the
  laptop. **Always use `+0x7FFC00`.**
- Block 1 access additionally requires `BUS_EXT_REG_EN` (0x08000000) set in
  `BUS_CNTL` (block 0 MMIO `0xA0`).

**Stability opportunity.** `BUS_CNTL` bit `BUS_APER_REG_DIS` (0x00000010, VTB+)
disables the in-aperture MMIO window. Because we have a real BAR2 on the
Mobility, we can turn it off - which reclaims the top 2 KiB of VRAM (X.Org
otherwise does `AcceleratorVideoRAM -= 2`) **and removes an entire class of
"the blitter scribbled over its own registers" corruption.**

### Safe probe

X.Org's documented sequence, worth copying as a method:

1. Map the candidate aperture, read `BUS_CNTL`, **mask `BUS_HOST_ERR_INT_EN` and
   ack `BUS_HOST_ERR_INT`** before touching anything else.
2. Save `SCRATCH_REG0` (block 0 MMIO `0x80`), write `0x55555555`, read back,
   write `0xAAAAAAAA`, read back, restore. Both must match.
3. Read `CONFIG_CHIP_ID` and cross-check against the PCI device id.

`CONFIG_CHIP_ID` = block 0 MMIO **`0xE0`**, block PIO `0x20E0`. **Bits [15:0] are
literally the PCI device id as an ASCII pair** - `0x4C4D` = `'LM'`, `0x5654` =
`'VT'`. Bits [23:16] class; [31:24] subdivided on 264xT into version [26:24],
foundry [29:27], revision [31:30]. Our rev `0x64` decodes to version 4, foundry
4, revision 1. 86Box VT2 returns `0x40005654`.

This confirms the stage-4 plan: `CONFIG_CHIP_ID` is a genuine oracle for "am I
talking to the right window".

## 3. Video memory size - the open question, answered

`MEM_CNTL` = block 0 MMIO **`0xB0`**. **The decode differs by chip generation and
the two tables disagree for every code >= 2:**

| code | `CTL_MEM_SIZE` (3-bit, pre-VTB - incl. plain VT) | `CTL_MEM_SIZEB` (4-bit, VTB/GTB/LT/**Mobility**) |
|---|---|---|
| 0 | 512 KiB | 512 KiB |
| 1 | 1 MiB | 1 MiB |
| 2 | 2 MiB | **1.5 MiB** |
| 3 | 4 MiB | **2 MiB** |
| 4 | 6 MiB | 2.5 MiB |
| 5 | 8 MiB | 3 MiB |
| 6 | 12 MiB | 3.5 MiB |
| 7 | 16 MiB | 4 MiB |
| 8-15 | - | 5, 6, 7, 8, 10, 12, 14, 16 MiB |

Formula for the 4-bit table: `code < 8` -> `(code+1) * 512 KiB`; `8..11` ->
`(code-3) * 1 MiB`; `12..15` -> `(code-7) * 2 MiB`.

**Branch on the decoded chip, never on the PCI id.** Our Mobility uses
`CTL_MEM_SIZEB`. A plain `264VT` uses the 3-bit table, and 86Box's VT2 is
identified as `264VT` or `264VTB` depending on `CFG_CHIP_VERSION`.

Memory *type* is not in `MEM_CNTL` - it is `CONFIG_STAT0` (block 0 MMIO `0xE4`),
field `CFG_MEM_TYPE_T` = bits [2:0] on 264xT. It feeds the DSP calculation (§6)
and gates `BLOCK_WRITE_EN` (SGRAM only). Our part is an SDRAM board.

**None of this can be developed against 86Box.** Its `MEM_CNTL` is a plain
scratch register, unconnected to the configured VRAM size - whatever the ROM
dump wrote at POST is what you read. Worse, aperture-probe sizing behaves
*opposite* to reality: 86Box drops out-of-range writes, real Mach64 DRAM
**aliases**. An alias-detection algorithm gives inverted results.

**Consequence for the manifest:** keep `VideoMemoryBytes = 4194304` as the
declared mode-layout floor, and treat runtime detection as laptop-only work.

## 4. The LCD panel - no EDID needed

The Mobility reaches its LCD registers through an **index/data pair**, not
direct MMIO (that is the 264LT's model):

- `LCD_INDEX` = block 0 MMIO **`0xA4`** - write the 6-bit index in `LCD_REG_INDEX`
- `LCD_DATA` = block 0 MMIO **`0xA8`**

**Hazard: `LCD_INDEX` is stateful.** It also carries `LCD_DISPLAY_DIS`,
`LCD_SRC_SEL`, `LCD_CRTC2_DISPLAY_DIS` and the monitor-detect interrupt latch.
Read-modify-write it and restore it around every indexed access; never blind-write
the index.

Indices of interest: `0x00` `LCD_CONFIG_PANEL`, `0x01` `LCD_GEN_CNTL`, `0x04`
`LCD_HORZ_STRETCHING`, `0x05` `LCD_VERT_STRETCHING`, `0x06`
`LCD_EXT_VERT_STRETCH`, `0x08` `LCD_POWER_MANAGEMENT`.

### Learning the native panel size

**Path A - ask the hardware.** `HORZ_PANEL_SIZE` (0x0ff00000 in idx `0x04`):
width = `(value + 1) * 8`, all-ones means unknown. `VERT_PANEL_SIZE` (0x003ff800
in idx `0x06`): height, same +1 idiom. For our panel expect 127 and 767.

**Path B - scan the video BIOS.** ROM table pointer at BIOS offset `0x48`; if the
ROM table is >= `0x4A` long, LCD table at `0x78`; panel info pointer at
`LCDTable + 0x0A`. Panel entry layout: `+0x00` panel ID, `+0x01..+0x18` 24-byte
printable ASCII model name, `+0x19` width word, `+0x1B` height word. If the
pointers are junk, brute-force scan the ROM for {ID byte, 24 printable chars,
plausible dimensions}. This also recovers the panel's timings, which the driver
then substitutes wholesale - for a panel, only the mode's *active area* is used.

We already have the BIOS image at
`personal\v9x-ragepro\baseline\guest-files\VIDEOROM.BIN`, so **Path B can be
decoded offline, on the host, right now** - no laptop time needed.

### Decoded from our actual BIOS, 2026-08-16

The captured `VIDEOROM.BIN` was decoded offline with
`personal\v9x-ragepro\decode-panel-table.py`. **The pointer walk failed** - the
length byte was not where expected, so either that offset is wrong or this BIOS
does not populate the ROM-table LCD pointer. **The brute-force scan succeeded**,
which is exactly what X.Org's fallback exists for, and is a good argument for
implementing the fallback rather than only the pointer walk.

| | |
|---|---|
| Panel | **LG LP141XA** (14.1" XGA, consistent with a Solo 2150) |
| Panel ID | **0x03** |
| Native size | **1024 x 768** - the assumption is confirmed |
| Entry offsets | `0xBA90` and `0xBE96` - two copies of the whole structure |

So the registers should read `HORZ_PANEL_SIZE = 127` (0x7F) and
`VERT_PANEL_SIZE = 767` (0x2FF). That is a concrete first assertion for any
panel bring-up code, checkable before a single pixel is drawn.

**The entry carries a per-mode timing table**, which is the part worth having:

- At entry `+0x40`, a **15-entry word pointer table**, null-terminated.
- Records are **63 bytes (0x3F)** each, contiguous from entry `+0x60`.
- Each record begins with two words that are the **mode's resolution**, and at
  `+0x05` a pointer to its own `+0x1F` - the record is in two sections.
- The two ROM copies differ only in pointer form: `0xBA90` holds absolute ROM
  offsets, `0xBE96` holds entry-relative ones.

The 15 modes:

```
320x350   320x400   320x400   320x480   400x600
512x384   640x350   640x400   640x475   640x480
720x480   720x576   800x600   848x480   1024x768
```

Two things follow. **640x400 is present**, so the Doom95 mode the plan was
unsure about is supported by the panel. And the table is dominated by exactly the
**low-resolution DOS and VGA modes that erratum E4 covers** - the horizontal
blender under-stretches below roughly 440 pixels on a 1024-wide panel, and nine
of these fifteen modes are under that threshold. Panel stretch mode selection is
therefore not an edge case on this machine; it is the common case.

**Not yet decoded:** the individual timing fields inside the 63-byte record.
The structure is evident but naming the fields needs the X.Org sources read
locally rather than through a truncating fetch.

### Stretch vs centre

Centering = leave `HORZ_STRETCH_EN` / `VERT_STRETCH_EN` clear. Stretching uses
either **pixel replication** (`HORZ_STRETCH_MODE = 0`, with a stretch loop from
the table {10,12,13,15,16} and a `HORZ_STRETCH_RATIO` bitmask) or **blending**
(`HORZ_STRETCH_MODE = 1`). On the Mobility, `LCDVBlendFIFOSize = 1024` - exactly
our panel width - so vertical blending is permitted at 1024-wide modes only.

## 5. The 2D engine

Trigger register: **the last write of the command.** `DST_HEIGHT_WIDTH` (MMIO
`0x118`) for every rectangle primitive; `DST_BRES_LNTH` (`0x120`) for lines.
Packing: `DST_Y_X` is **X high word, Y low word**; `DST_HEIGHT_WIDTH` is **width
high, height low**.

Steady-state FIFO cost: **solid fill 2 slots, screen copy 4 slots, Bresenham line
6 slots** (axis-aligned lines are cheaper as 1-pixel rects, 3 slots).

**Overlap rule for copies:** direction comes from `DST_X_DIR`/`DST_Y_DIR` in
`DST_CNTL`, and when a direction bit is *clear* (decreasing) you must pass the
**bottom-right corner** as the start coordinate for both source and destination.
`SRC_WIDTH1` must be rewritten per blit - the engine wraps source X back to
`SRC_X` after `SRC_WIDTH1` pixels, which is also how tiling works.

> **Correction to an earlier note.** FreeBE's comment that "the mach64 only needs
> to reverse the Y direction" is **wrong as a general rule** - it reflects
> Allegro's usage, not the hardware. X.Org sets both `DST_X_DIR` and `DST_Y_DIR`
> from the computed direction and adjusts both start coordinates. Implement the
> general case.

Neither is `GUI_TRAJ_CNTL` (`0x330`) a trigger - it is an **aliased 32-bit view
of `src_cntl | dst_cntl | pat_cntl | host_cntl`**, letting four control registers
be set in **one FIFO slot instead of four**. That is a free performance lever on
every state change.

### Primitives available beyond fill and copy

| Primitive | Mechanism |
|---|---|
| Bresenham lines | `DST_BRES_ERR/INC/DEC` + `DST_BRES_LNTH`; `DST_LAST_PEL` gives the GDI "last pixel" rule in hardware |
| **Trapezoid / polygon fill** | `DST_POLYGON_EN`, plus a **second edge-walker** on GT-class (our LM): `TRAIL_BRES_ERR/INC/DEC` + `LEAD_BRES_LNTH`, `TRAP_FILL_DIR` |
| Hardware scissors | `SC_LEFT_RIGHT` (`0x2A8`), `SC_TOP_BOTTOM` (`0x2B4`) |
| Transparent blit | `CLR_CMP_CLR/MSK/CNTL` with `CLR_CMP_FN_EQUAL \| CLR_CMP_SRC_2D` - one-pass, no AND/OR pair |
| Mono expansion | `DP_MONO_SRC` = `PATTERN`, `HOST`, or **`BLIT` (1bpp bitmap already in VRAM)** |
| Colour patterns | `PAT_CLR_4x2_EN`, `PAT_CLR_8x1_EN`, and VTB+ `SRC_8X8X8_BRUSH` (full 8x8x8bpp brush) |
| Hardware cursor | 64x64, 2bpp interleaved src+mask, `CUR_OFFSET` (`0x68`), enable `GEN_CUR_EN` in `GEN_TEST_CNTL` |
| Fast fill / block write | `FAST_FILL_EN`, `BLOCK_WRITE_EN` (SGRAM only) in `SRC_CNTL`, VTB+ |
| Context DMA | `CONTEXT_LOAD_CNTL` - load a saved register context from VRAM and optionally execute |
| Bus-master blits | `BUS_MASTER_EN/SYNC/OP` + `BM_HOSTDATA`/`BM_ADDR`, VTB+ |
| Overlay / scaler | Block 1, YUY2/UYVY/YV12/I420 |

All 16 boolean ROPs map to hardware mixes; `DP_MIX` holds **two independent
5-bit mixes** (foreground [20:16], background [4:0]), so there are more than 16
codes - the extras are source-inclusive and blend ops, including an average
(`(S+D)/2`) mix that FreeBE also spotted.

Coordinate limits: X 4095, Y 16383. `DST_OFF_PITCH` encodes **offset in 8-byte
units** and **pitch in pixels/8**.

## 6. Synchronisation - and a second venue divergence

**Two different mechanisms, split at `264VTB`:**

- **Pre-VTB (a plain `264VT`, possibly our 86Box target):** `FIFO_STAT` (`0x310`)
  bits [15:0] are a **one-hot occupancy bitmap** that must be **population-counted**
  and subtracted from 16. `FIFO_ERR` (bit 31) means the engine is wedged.
- **VTB and later, including our Mobility:** ignore `FIFO_STAT`. `GUI_STAT`
  (`0x338`) gives both in one read - bit 0 `GUI_ACTIVE`, bits [25:16] `GUI_FIFO` =
  **free entry count** (10 bits, a far deeper FIFO than 16).

**The cost model, which is the whole performance story:**

1. **Cache the free count in software.** Decrement a driver-side counter on every
   write; re-read `GUI_STAT` only when it drops below the requested batch size.
   An MMIO *read* is a bus round-trip (hundreds of ns on this era); an MMIO
   *write* is posted and nearly free. **Polling status per register write is the
   single biggest performance mistake a Mach64 driver can make.**
2. **Request the exact batch size up front**, then fire that many writes blind.
3. **Never wait for idle for the correctness of the next drawing operation.** The
   FIFO handles ordering. Idle waits are needed only before CPU framebuffer
   access, reading back engine registers, mode set / power changes, and the copy
   erratum below.

This confirms the earlier conclusion and sharpens it: **modelling `eng_mach64.c`
on the Trio64's block-on-idle discipline would be actively wrong.** But note the
plan's stage-5 exit gate must change - see §8.

**Wedge recovery** (the Trio has none; the Mach64 does): mask
`BUS_HOST_ERR_INT_EN` and ack `BUS_HOST_ERR_INT` -> `BUS_FLUSH_BUF` -> pulse
`GEN_GUI_RESETB` (bit 0x100 in `GEN_TEST_CNTL`, block 0 MMIO `0xD0`, **active-low
on 264xT**) low then high -> replay the full engine state -> wait idle. VTB/GTB/LT
also have `GEN_SOFT_RESET` (0x200). FreeBE's `ResetEngine()` is the same
sequence in miniature and corroborates it.

## 7. Errata - ranked by likelihood of biting us

**E1 - VTB+ copy-commit race (applies to our LM).** X.Org's own words: the engine
"will randomly not wait for a copy operation to commit its results to video
memory before starting the next one", with probability rising with the
`GUI_WB_FLUSH` setting, bit depth and CRTC clock. Its mitigation is brutal - a
**full idle wait after every single screen-to-screen copy**. The knob is
`GUI_WB_FLUSH` (0xe0000000) in `MEM_BUF_CNTL` (block 0 MMIO `0x2C`). **This is
simultaneously the biggest stability hazard and the biggest opportunity to beat
ATI's driver** - a lower flush threshold may permit dropping the per-copy sync.
Measure it; do not guess.

**E2 - VTB+ CPU reads of VRAM return stale or zero data (applies to our LM).**
Any CPU read of the framebuffer after engine activity must first set
`INVALIDATE_RB_CACHE` (0x00800000) in `MEM_BUF_CNTL`. For a GDI driver this
touches *everything* that reads the screen: `GetPixel`, DIB read-back,
save-unders held in VRAM, and every screen-source blit. X.Org explicitly marks
the widely-copied "throw away one dummy read" workaround as **buggy** - do not
inherit it from other drivers.

**E3 - panel will not sync if shadow and non-shadow H sync disagree (chip bug).**
The Mobility drives the panel from a *shadow* copy of the CRTC registers
(`SHADOW_EN` / `SHADOW_RW_EN` in `LCD_GEN_CNTL`). Program both sets identically
except for `CRTC_H_DISP`. A black or rolling LCD after mode set is almost
certainly this.

**E4 - the horizontal blender under-stretches small modes.** Below roughly 440
pixels on a 1024-wide panel, blending misbehaves; use pixel replication instead.
This hits **320x200, 320x240 and 640x480** - precisely the DOS-box and game modes
a Win98 laptop actually uses.

**E5 - `SCRATCH_REG3` `DISPLAY_SWITCH_DISABLE`.** X.Org sets this deliberately to
stop the BIOS reprogramming registers behind the driver's back. On a laptop this
is the **Fn+F5 LCD/CRT toggle**, and it will otherwise fight the driver. This one
line plausibly fixes a whole class of "random corruption on a laptop".

**E6 - hardware cursor must never be disabled to hide it.** Toggling
`GEN_CUR_EN` causes display artifacts; draw a fully transparent image instead,
and do not disable the cursor while uploading a new one.

**E7 - DSP (display FIFO) parameters must be recomputed per mode, and must be
panel-aware.** Wrong values give screen sparkle and tearing under load. Inputs
come from `MEM_CNTL`'s timing fields (`CTL_MEM_TRP/TRCD/TCRD/TRAS` - **VTB+ only,
so LM-valid and VT-invalid**) plus the memory type, XCLK from the PLL. Critically,
**when stretching to the panel, the DSP must be programmed for the *panel* width,
not the mode width**, because the CRTC fetches at panel rate. 86Box stores
`DSP_CONFIG`/`DSP_ON_OFF` and ignores them entirely, so a completely wrong
calculation looks flawless in the emulator and tears on the laptop.

**E8 - host error interrupt masking: does NOT apply to us.** Resolved against the
local clone. The guard is `if (pATI->Chip < ATI_CHIP_264VT4)` in `atimach64.c:87`
and `atiprobe.c:145`, and the enum in `atichip.h` orders
`264VTB(13) < 264VT4(18) < MOBILITY(23)`. So the Rage Mobility is **past** that
cut and needs neither the host-error nor the FIFO-error interrupt masking that
`atiprobe.c:141` applies to pre-VTB parts. Keep the masking in the *probe* path
anyway if the probe must also work on a plain VT (our 86Box target **is** pre-VT4
and does need it).

The same enum ordering confirms the direction of everything else: the Mobility is
comfortably `>= 264VTB`, so **E1, E2 and E7 all apply**, and `GUI_CNTL` in
block 1 (a `>= 264VT4` register) exists on our part.

## 8. Consequences for the plan

1. **Stage 4's register-window question is answered.** Use BAR2 `F4100400` on the
   laptop, `LFB + 0x7FFC00` on 86Box, `CONFIG_CHIP_ID` as the cross-check, and
   consider `BUS_APER_REG_DIS` on the laptop. The `+0x400` alias is an
   emulator-only trap.
2. **Stage 5 must use FIFO free-count batching, not block-on-idle**, and the two
   venues use *different* status mechanisms (`GUI_STAT[25:16]` on LM,
   `FIFO_STAT` popcount on VT).
3. **The stage-5 exit gate as written is unachievable.** It required
   `V9XTRACE -inject=N` to raise `idle_timeouts` with `reset_count` flat, on
   86Box. The emulator's engine can never be observed busy (`accel.busy` is never
   exposed; `FIFO_STAT` is hardwired empty; register reads auto-drain the queue).
   **Move all engine sync, timeout and recovery gating to the laptop.**
4. **Add an explicit DSP/`MEM_BUF_CNTL` work item.** E1, E2 and E7 are all in
   this register neighbourhood, all affect stability, and none are testable in
   86Box.
5. **Panel work can start offline now** by decoding the captured `VIDEOROM.BIN`
   against the §4 Path B table format.
6. **Opportunities to beat the stock driver**, roughly in payoff order: a software
   MMIO write cache that skips redundant register writes; lazy scissor
   management; hardware left-edge clipping for text via `SC_LEFT_RIGHT`;
   `DP_MONO_SRC_BLIT` glyph caching in off-screen VRAM; `GUI_TRAJ_CNTL` 4-in-1
   control writes; hardware cursor (Velocity9x has none on any chip today); and
   the GT-class trapezoid engine.

## 9. Constant values (resolved from the local clone)

`xf86-video-mach64` is cloned at `C:\everything\xf86-video-mach64` (MIT, tip
`0f73197`). These are the values the earlier truncated fetches could not reach.

**Datapath sources** - `DP_SRC` fields `DP_FRGD_SRC` [10:8] and `DP_BKGD_SRC`
[2:0]:

| name | value |
|---|---|
| `SRC_BKGD` | 0 |
| `SRC_FRGD` | 1 |
| `SRC_HOST` | 2 |
| `SRC_BLIT` | 3 |
| `SRC_PATTERN` | 4 |
| `SRC_SCALER_3D` | 5 |

**Pixel widths** - `DP_PIX_WIDTH` dst [3:0] / src [11:8] / host [19:16]:

| depth | code |
|---|---|
| 1 bpp | 0 | 
| 4 bpp | 1 |
| **8 bpp** | **2** |
| 15 bpp | 3 |
| **16 bpp** | **4** |
| 24 bpp | 5 |
| 32 bpp | 6 |
| YUV422 | 7 |

**Mixes** - 5 bits (`MIX_MASK` = 0x1F), and there are **two ranges**:

| code | boolean (0x00-0x0F) | code | arithmetic (0x10-0x1F) |
|---|---|---|---|
| 0x00 | `NOT_DST` | 0x10 | `MIN` |
| 0x01 | `0` | 0x11 | `DST_MINUS_SRC` |
| 0x02 | `1` | 0x12 | `SRC_MINUS_DST` |
| 0x03 | `DST` | 0x13 | `PLUS` |
| 0x04 | `NOT_SRC` | 0x14 | `MAX` |
| 0x05 | `XOR` | 0x15 | `HALF_DST_MINUS_SRC` |
| 0x06 | `XNOR` | 0x16 | `HALF_SRC_MINUS_DST` |
| 0x07 | **`SRC`** | 0x17 | **`AVERAGE`** |
| 0x08 | `NAND` | 0x18 | `DST_MINUS_SRC_SAT` |
| 0x09 | `NOT_SRC_OR_DST` | 0x1A | `SRC_MINUS_DST_SAT` |
| 0x0A | `SRC_OR_NOT_DST` | 0x1C | `HALF_DST_MINUS_SRC_SAT` |
| 0x0B | `OR` | 0x1E | `HALF_SRC_MINUS_DST_SAT` |
| 0x0C | `AND` | 0x1F | `AVERAGE_SAT` |
| 0x0D | `SRC_AND_NOT_DST` | | |
| 0x0E | `NOT_SRC_AND_DST` | | |
| 0x0F | `NOR` | | |

**The arithmetic range is a bigger find than the earlier audit suggested.** FreeBE
spotted only `MIX_AVERAGE` (0x17). In fact the chip has saturating add, saturating
subtract in both directions, min, max, and half-difference variants - sixteen
arithmetic blend modes. GDI cannot express these, but DirectDraw blends and any
future compositing work can.

**Independent corroboration.** FreeBE's `mach64_mix[]` table, written by a
different author from hardware experimentation, maps REPLACE->7, AND->12, OR->11,
XOR->5, NOP->3. Those match `MIX_SRC`=0x07, `MIX_AND`=0x0C, `MIX_OR`=0x0B,
`MIX_XOR`=0x05, `MIX_DST`=0x03 exactly. Two independent sources agreeing on the
boolean block is good evidence the table is right.

**Memory types** - `CFG_MEM_TYPE_T`, bits [2:0] of `CONFIG_STAT0` (MMIO `0xE4`):

| code | type |
|---|---|
| 0 | none / disabled |
| 1 | DRAM |
| 2 | EDO DRAM |
| 3 | Pseudo-EDO DRAM |
| **4** | **SDRAM (1:1)** |
| 5 | SGRAM (1:1) |
| 6 | SGRAM (2:1) 32-bit |
| 7 | unknown |

Our BIOS string is `ATI MACH64 SDRAM BIOS 4.216`, so **`CFG_MEM_TYPE_T` should
read 4**. That is a third pre-flight assertion, alongside `HORZ_PANEL_SIZE`=127
and `VERT_PANEL_SIZE`=767. It also means `BLOCK_WRITE_EN` is **not** available to
us - that needs SGRAM.

## 10. Confidence

- **Verified from source and quoted:** memory-size decode, aperture placement,
  `CONFIG_CHIP_ID` layout and chip table, LCD index/data map and stretch
  algorithm, BIOS panel-table format, the 2D setup/subsequent sequences, the
  FIFO/poll model, the DSP calculation, and every erratum.
- **Derived, not transcribed:** most MMIO offsets in this document are computed
  from X.Org's `IOPortTag`/`BlockIOTag` macros. Five were spot-checked against
  independently known values (`0xE0`, `0x100`, `0x2D4`, `0x310`, `0x338`) and all
  matched - but treat the rest as derived.
- **Resolved 2026-08-16** by cloning to `C:\everything\xf86-video-mach64`: all
  the `MIX_*`, `SRC_*` and `PIX_WIDTH_*` values, the memory-type ordering, and
  the chip enum ordering are now in §9. Erratum E8 turned out **not** to apply to
  the Mobility.
- **Still outstanding:** the field layout inside the 63-byte per-mode panel
  timing records decoded from our BIOS (§4).
- **The 2D primitives are not on master - verified.** `src/atimach64accel.c` is
  397 lines on tip with no `SetupFor*`/`Subsequent*` functions at all, and
  nothing in the tree references XAA any more. At tag
  **`xf86-video-mach64-6.8.2`** the same file is 1068 lines and carries all
  twelve: solid fill, screen-to-screen copy, mono 8x8 pattern fill, scanline
  CPU-to-screen colour expand, solid lines and Bresenham lines. **Read that tag,
  not the tip:**

  ```bash
  git -C C:/everything/xf86-video-mach64 show xf86-video-mach64-6.8.2:src/atimach64accel.c
  ```
