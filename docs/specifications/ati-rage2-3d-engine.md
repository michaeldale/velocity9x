# mach64 3D RAGE (GT / GTB / GT2C) 3D engine: programming notes from primary sources

Target: 3D Rage IIC, 264GT2C, PCI 1002:4757 (no setup engine). Extracted 2026-10-02 for
[the Rage IIC plan](../plans/ati-rage-iic-hardware-3d.md); nothing here is measured yet.
The `r/` paths are the text extracts made for the
[register survey](../decisions/2026-10-02-rage-iic-register-survey.md), not kept in the tree.
Sources, abbreviated in citations:

- **RRG**: RRG-G02700 Rev 0.10 (1996), `r/rrg02700.txt`. Cited as page (e.g. p.4-46) plus txt line (L3897).
- **PRG**: PRG-215R3 (2000), `r/prg215.txt`.
- **CIF**: SDK-C02700 Rev 1.30 (1997), `r/cif.txt`.
- **atiregs**: `r/atiregs.h` (xf86-video-mach64); **mach64.h**, **mesa_reg** (`mesa_mach64_reg.h`), **atyfb** (`atyfb_base.c`, `atyfb.h`), **m64accel** (`mach64_accel.c`).

Markers: **[S]** means the source states it (citation follows). **Inference:** means I worked it out. **Not found** means no source covers it.

Text-extraction caveat: the RRG bit-layout diagrams came out as filler characters (`AAAA…`). Bit **positions** therefore come from (a) aliasing statements in the RRG text, (b) the order of the field letters in the diagram row, and (c) atiregs/mesa_reg. Each position below says which of these it comes from.

---

## 0. Executive summary (15 lines)

1. Wait for FIFO space. FIFO_STAT 0_C4 (0x710) is one bit per used entry; atyfb treats the GT2C FIFO as 16 entries (atyfb.h L349-356). [S]
2. Write DST_OFF_PITCH 0_40 (0x500), DP_PIX_WIDTH 0_B4 (0x6D0), DP_WRITE_MASK 0_B2 (0x6C8) = 0xFFFFFFFF, DP_MIX 0_B5 (0x6D4) with FRGD_MIX = 7 (SRC), SC_LEFT_RIGHT 0_AA (0x6A8) and SC_TOP_BOTTOM 0_AD (0x6B4). [S, RRG p.4-87…4-97]
3. Flat 2D colour: DP_FRGD_CLR 0_B1 (0x6C4) = colour, DP_SRC 0_B6 (0x6D8) with FRGD_SRC = 1 and MONO_SRC = 0, SCALE_3D_CNTL 0_7F (0x5FC) = 0. Shaded or textured spans instead need FRGD_SRC = 5 (Scaler/3D), plus SCALE_3D_FCN = 3 (shade) or 2 (texture). [S, RRG p.4-97 L6080; p.6-4 L8527]
4. DST_CNTL 0_4C (0x530): DST_X_DIR (bit 0), DST_Y_DIR (bit 1), TRAIL_X_DIR (bit 13), TRAP_FILL_DIR (bit 14), the sign bits 11/15, and BRES_SIGN_AUTO (bit 17). [S, RRG p.4-48; bit positions from atiregs L1406-1423]
5. DST_Y_X 0_43 (0x50C) or its alias 0_4D (0x534): X in bits 31:16, Y in bits 15:0. This is the leading-edge start. [S, RRG p.4-58]
6. Leading-edge Bresenham terms go in DST_BRES_ERR/INC/DEC, 0_49/4A/4B (0x524/528/52C). Trailing-edge terms go in TRAIL_BRES_ERR/INC/DEC, 0_4E/4F/50 (0x538/53C/540). [S, RRG p.4-43…4-45, 4-59]
7. **Kick-off:** write DST_BRES_LNTH 0_48 (0x520), or its alias LEAD_BRES_LNTH 0_51 (0x544), with bit 31 = 1, bit 15 = 1 (DRAW_TRAP), bits 28:16 = TRAIL_X (trailing-edge start X), and bits 14:0 = leading-edge length. [S, RRG p.4-46 L3897-3920]
8. The trailing-edge X start is a separate field (TRAIL_X, also reachable as DST_HEIGHT[12:0]). It is not implied by the leading edge. [S, RRG L3903, L4140]
9. Interpolators (S.8.12 for colour and alpha, S.16.12 for Z): *_START is the value "at the beginning of trapezoid". *_X_INC applies per X step in DST_X_DIR. *_Y_INC applies per Y step along the leading edge. [S, RRG p.6-11…6-14]
10. S/T are not perspective-divided by hardware: there is no W register on GT/GTB. They use 2nd-order forward differences: S_START (10.11), S_XINC_START and S_Y_INC (S.11.16), S_X_INC2, S_Y_INC2 and S_XY_INC2 (S.10.16). [S, RRG p.6-8…6-10; atiregs L1667-1686]
11. Unknown: how the leading-edge length is counted (scanlines or Bresenham steps); whether X-major edges are stepped per scanline; whether span ends are inclusive. [Not found]
12. Unknown: bit placement inside the interpolator registers. Only width and format are known. Probe with write-all-ones / read-back; the registers are documented R/W. [Not found]
13. Unknown: whether span stepping uses DST_X_DIR or TRAP_FILL_DIR as the sign for *_X_INC. Also unknown: whether START accumulators and edge state survive into the next trapezoid, and whether DST_X/DST_Y change after a trapezoid. [Not found]
14. Unknown: GT-era Z_CNTL bit positions. The RAGE PRO layout is Z_EN = 0, Z_SRC = 1, Z_TEST = 6:4, Z_MASK = 8 (mesa_reg L375-386); assuming GT matches is an inference. [Not found for GT]
15. Unknown: the TEX_PAL_WR (0_DF, 0x77C, tagged GTB) data format, and the GT Bresenham-term formulas for trapezoids. The RRG gives only the VT line formulas. [Not found]

---

## 1. Register-offset table (everything used below)

Convention: block-0 index i maps to byte offset 0x400 + 4·i, and block-1 index i maps to 4·i. The RRG puts block 1 at aperture + 0x7FF800 and block 0 at + 0x7FFC00, 1 KB apart (RRG p.2-3, L640-662). mach64.h uses block-0 byte offsets without the +0x400 (for example SCALE_3D_CNTL = 0x01FC, mach64.h L277). mesa_reg includes it (MACH64_SCALE_3D_CNTL = 0x05fc, mesa_reg L222).

| Register | MM | Byte off | Tag (atiregs) | RRG page |
|---|---|---|---|---|
| DST_OFF_PITCH | 0_40 | 0x500 | all | 4-53 |
| DST_X / DST_Y / DST_Y_X | 0_41/0_42/0_43 | 0x504/0x508/0x50C | all | 4-55/4-57/4-58 |
| DST_WIDTH / DST_HEIGHT | 0_44/0_45 | 0x510/0x514 | all | 4-54/4-51 |
| DST_HEIGHT_WIDTH / DST_X_WIDTH | 0_46/0_47 | 0x518/0x51C | all | 4-52/4-56 |
| DST_BRES_LNTH | 0_48 | 0x520 | all | 4-46 |
| DST_BRES_ERR (LEAD_BRES_ERR) | 0_49 | 0x524 | all | 4-44 |
| DST_BRES_INC (LEAD_BRES_INC) | 0_4A | 0x528 | all | 4-45 |
| DST_BRES_DEC (LEAD_BRES_DEC) | 0_4B | 0x52C | all | 4-43 |
| DST_CNTL | 0_4C | 0x530 | all | 4-48 |
| DST_Y_X alias | 0_4D | 0x534 | GT | 4-58 (L4440) |
| TRAIL_BRES_ERR / INC / DEC | 0_4E/0_4F/0_50 | 0x538/0x53C/0x540 | GT | 4-59 |
| LEAD_BRES_LNTH (alias of 0_48) | 0_51 | 0x544 | GT | 4-46 (L3935) |
| Z_OFF_PITCH | 0_52 | 0x548 | GT | 4-60 |
| Z_CNTL | 0_53 | 0x54C | GT | 4-61 |
| SRC_CNTL | 0_6D | 0x5B4 | all | 4-62 |
| TEX_0_OFF … TEX_10_OFF | 0_70…0_7A | 0x5C0…0x5E8 | GT | 6-8 |
| S_Y_INC (alias of 0_D4; = SCALE_Y_PITCH) | 0_7B | 0x5EC | GT | 6-2, 6-9 |
| RED_X_INC (alias of 0_F0; = SCALE_X_INC) | 0_7C | 0x5F0 | GT | 6-2 |
| GREEN_X_INC (alias of 0_F3; = SCALE_Y_INC) | 0_7D | 0x5F4 | GT | 6-2 |
| SCALE_VACC | 0_7E | 0x5F8 | GT | 6-3 |
| SCALE_3D_CNTL | 0_7F | 0x5FC | GT | 6-4…6-6 |
| SC_LEFT_RIGHT / SC_TOP_BOTTOM | 0_AA/0_AD | 0x6A8/0x6B4 | all | 4-84/4-87 |
| DP_BKGD_CLR / DP_FRGD_CLR (= DP_FOG_CLR) | 0_B0/0_B1 | 0x6C0/0x6C4 | all | 4-88/4-89 |
| DP_WRITE_MSK / DP_CHAIN_MSK | 0_B2/0_B3 | 0x6C8/0x6CC | all | 4-90/4-91 |
| DP_PIX_WIDTH / DP_MIX / DP_SRC | 0_B4/0_B5/0_B6 | 0x6D0/0x6D4/0x6D8 | all | 4-92/4-95/4-97 |
| CLR_CMP_CLR / MSK / CNTL | 0_C0/0_C1/0_C2 | 0x700/0x704/0x708 | all | 4-98… |
| FIFO_STAT | 0_C4 | 0x710 | all | 4-101 |
| GUI_TRAJ_CNTL | 0_CC | 0x730 | all | 4-104 |
| GUI_STAT | 0_CE | 0x738 | all | 4-105 approx. (L6580) |
| S_X_INC2 / S_Y_INC2 / S_XY_INC2 | 0_D0/0_D1/0_D2 | 0x740/0x744/0x748 | GTB (GTPro reuses) | 6-8/6-8/6-9 |
| S_XINC_START / S_Y_INC / S_START | 0_D3/0_D4/0_D5 | 0x74C/0x750/0x754 | GTB | 6-9 |
| T_X_INC2 / T_Y_INC2 / T_XY_INC2 | 0_D6/0_D7/0_D8 | 0x758/0x75C/0x760 | GTB (GTPro: W_*) | 6-9/6-10 |
| T_XINC_START / T_Y_INC / T_START | 0_D9/0_DA/0_DB | 0x764/0x768/0x76C | GTB | 6-10 |
| TEX_SIZE_PITCH | 0_DC | 0x770 | GTB | 6-11 |
| TEX_PAL_WR | 0_DF | 0x77C | GTB (GTPro: TEX_PALETTE) | not in RRG |
| RED_X_INC / RED_Y_INC / RED_START | 0_F0/0_F1/0_F2 | 0x7C0/0x7C4/0x7C8 | GTB | 6-11 |
| GREEN_X_INC / _Y_INC / _START | 0_F3/0_F4/0_F5 | 0x7CC/0x7D0/0x7D4 | GTB | 6-12 |
| BLUE_X_INC / _Y_INC / _START | 0_F6/0_F7/0_F8 | 0x7D8/0x7DC/0x7E0 | GTB | 6-12/6-13 |
| Z_X_INC / Z_Y_INC / Z_START | 0_F9/0_FA/0_FB | 0x7E4/0x7E8/0x7EC | GTB | 6-13/6-14 |
| ALPHA(FOG)_X_INC / _Y_INC / _START | 0_FC/0_FD/0_FE | 0x7F0/0x7F4/0x7F8 | GTB | 6-14 |
| BUS_CNTL | 0_28 | 0x4A0 | all | (atiregs L961) |
| GEN_TEST_CNTL (GEN_GUI_EN = bit 8) | 0_34 | 0x4D0 | all | (atiregs L1167, L1185) |

Sources for MM indices: the RRG per-register page headers (`MM: 0_xx` lines; L3752, L3788, L3829, L3871, L4450-4486, L8376-9209). These were cross-checked against atiregs L1391-1504 and L1667-1731 and against mach64.h L200-230 and L355-420; all agree. The RRG "Registers by Address" table (L860-1100) did not survive extraction (columns are shuffled), so do not use it.

---

## 2. Trapezoid trajectory

### 2.1 What the sources say

- The trapezoid is a new trajectory on the 3D RAGE. "The standard line engine is used to walk the leading edge … a new line engine is used to walk the trailing edge." The pixels drawn are those on the scan lines between the two edges. [S, RRG p.4-43, L3743-3746]
- The trapezoid trajectory can be used for the destination, the texture-map sources, the 2D source and the Z source. [S, RRG L3746-3747; p.1-3 L387-388]
- New registers: TRAIL_BRES_ERR/INC/DEC. "DST_BRES_LENGTH is expanded in the 3D RAGE to include the span length of the trapezoid and also to kick off trapezoidal operations." DST_CNTL and SRC_CNTL are also affected. [S, RRG L3747-3750]
  - Note: the text says "span length", but the field definition (below) calls it "trapezoid leading edge length". Inference: "span" here means the leading-edge extent, not the per-scanline width. The per-scanline width comes from the two edges.
- The Z buffer destination "will always track the normal destination in X and Y, but with its own pitch and offset". [S, RRG p.4-60 L4521-4530]

### 2.2 Leading edge

| Item | Register | Format | Source |
|---|---|---|---|
| Start X | DST_X (0_41) or DST_Y_X[31:16] | signed 13-bit | RRG p.4-55 L4320; DST_Y_X layout from m64accel L194, atimach64exa L282 |
| Start Y | DST_Y (0_42) or DST_Y_X[15:0] | signed 15-bit | RRG p.4-57 L4401 |
| Error term | DST_BRES_ERR (LEAD_BRES_ERR) 0_49 | signed 18-bit | RRG p.4-44 |
| Increment | DST_BRES_INC 0_4A | signed 18-bit, must be positive; added when the error is negative | RRG p.4-45 |
| Decrement | DST_BRES_DEC 0_4B | signed 18-bit, must be negative; added when the error is positive | RRG p.4-43 |
| Length | DST_BRES_LNTH[14:0] (LEAD_BRES_LNTH) | "Bresenham line length and trapezoid leading edge length"; aliases DST_WIDTH[14:0] | RRG p.4-46 L3897-3898 |

- Stepping rule: if the error is negative, take an axial step and add INC. Otherwise take a diagonal step in the major-axis direction and add DEC. [S, RRG p.4-44 L3798-3801]
- VT line formulas (stated for the VT only): ERR = 2·min(|dx|,|dy|) − max(…), INC = 2·min, DEC = 2·(min − max), LNTH = max(|dx|,|dy|) + 1. [S, RRG L3766, L3805, L3846, L3928]
- GT trapezoid-specific formulas: **Not found.**
- DST_Y_X also has an alias at 0_4D on the 3D RAGE. [S, RRG L4440]

### 2.3 Trailing edge

| Item | Register | Format | Source |
|---|---|---|---|
| Start X | TRAIL_X = DST_BRES_LNTH[28:16]; also DST_HEIGHT[12:0] ("aliased with TRAIL_BRES_X") | 13 bits | RRG p.4-46 L3903-3905; p.4-51 L4140 |
| Start Y | none; implicitly the same scanline as DST_Y | n/a | Inference: no trailing-Y register exists, and the spans lie "between the two edges" on each scan line |
| Error / Inc / Dec | TRAIL_BRES_ERR / INC / DEC (0_4E/4F/50) | signed 18-bit each; INC positive, DEC negative | RRG p.4-59 L4460-4505 |
| Direction | TRAIL_X_DIR @ DST_CNTL | 0 = right to left, 1 = left to right | RRG L4014 |
| Zero sign | TRAIL_BRES_SIGN @ DST_CNTL | 0 = a zero error counts as positive | RRG L4022 |

- TRAIL_BRES_INC and TRAIL_BRES_DEC are described as being "added to the DST_BRES_ERR term" (RRG L4483, L4504). Inference: this is a copy/paste slip and means TRAIL_BRES_ERR.
- No trailing-edge length register exists. Inference: the trailing edge runs for as many scanlines as the leading edge.
- The trailing edge has no Y_MAJOR flag. **Not found:** how an X-major trailing edge (more than one X step per scanline) is handled.

### 2.4 DST_BRES_LNTH / LEAD_BRES_LNTH bit layout (GT)

How the positions were derived: the letter order on the diagram row is "g f e d c" (L3884-3885). The text gives bit 31 and bit 15 explicitly, and the aliases with DST_WIDTH[15:0] and DST_HEIGHT[14:0] fix the rest.

| Bits | Field | Meaning | Source |
|---|---|---|---|
| 14:0 | DST_BRES_LNTH | line length / trapezoid leading-edge length; aliases DST_WIDTH[14:0] | RRG L3897-3898 |
| 15 | DRAW_TRAP | "To initiate a trapezoid, set to 1"; aliases DST_WIDTH[15] (write-only, reads back 0, RRG L4262-4264) | RRG L3900-3901 |
| 28:16 | TRAIL_X | trapezoid trailing-edge location; aliases DST_HEIGHT[12:0] | RRG L3903-3905 (position by alias, Inference) |
| 30:29 | reserved | aliases DST_HEIGHT[14:13] | RRG L3907 |
| 31 | DST_BRES_LNTH_LINE_DIS (W) | see table below; not stored | RRG L3891-3920 |

Write semantics [S, RRG L3910-3920]:

| bit31 | bit15 | Effect |
|---|---|---|
| 0 | 0 | Line draw. TRAIL_X and LNTH loaded. |
| 0 | 1 | **Trapezoid draw.** TRAIL_X *not* updated; LNTH loaded. |
| 1 | 0 | TRAIL_X and LNTH loaded; no draw. |
| 1 | 1 | **Trapezoid draw.** TRAIL_X and LNTH loaded. |

- Writing DST_BRES_LNTH also overwrites DST_WIDTH, and the reverse holds. [S, RRG L3925, L4280]
- DST_WIDTH bits [15:13] alias DST_BRES_LENGTH[15:13] "and are used for trapezoid draw operations". [S, RRG p.4-54 L4262-4265] So writing DST_WIDTH with bit 15 set, and DST_WIDTH_FILL_DIS (bit 31) clear, may also start a trapezoid. Inference, supported by the next point.
- A context load with CONTEXT_LOAD_CMD = 3 starts a line or trapezoid draw, and draws a trapezoid "if bit 15 of DST_BRES_LNTH or DST_WIDTH is set". [S, RRG p.4-103 L6371-6373]
- **The triggering write** is DST_BRES_LNTH (0_48) or LEAD_BRES_LNTH (0_51) with bit 15 = 1. [S] Every other trajectory and interpolator register must already be written. Inference: the command FIFO keeps write order (RRG p.4-101 L6283-6291), so "already written" means earlier in the FIFO stream.
- Inference about the 31 = 0 / 15 = 1 form: it starts a trapezoid that reuses the trailing-edge X already held in the engine. That would fit the second half of a triangle whose long edge is the trailing edge. Whether the trailing Bresenham state and accumulators carry over is **Not found**.

### 2.5 DST_CNTL (0_4C) bits relevant to trapezoids

Bit positions are from atiregs L1406-1426. Meanings are from RRG p.4-48/4-49 (L3971-4060) and the GUI_TRAJ_CNTL copy (p.4-104, L6451-6494).

| Bit | Name | Meaning |
|---|---|---|
| 0 | DST_X_DIR | 0 = right to left, 1 = left to right |
| 1 | DST_Y_DIR | 0 = bottom to top, 1 = top to bottom |
| 2 | DST_Y_MAJOR | Y-major flag "for bresenham lines" (RRG L3979). Whether it applies to the trapezoid leading edge is **Not found**. |
| 3/4 | DST_X_TILE / DST_Y_TILE | rectangles only |
| 5 | DST_LAST_PEL | "affects only destination line trajectories" (RRG L4086). Trapezoid behaviour **Not found**. |
| 6 | DST_POLYGON_EN | polygon outline / fill; not for trapezoids (Inference) |
| 7, 10:8 | DST_24_ROT_EN, DST_24_ROT | packed 24 bpp; the 3D/Scaler path does not support packed 24 bpp (RRG L5636) |
| 11 | DST_BRES_SIGN (DST_BRES_ZERO on CT) | 0 = a zero error is positive; 1 = negative. The RRG lists it twice as j and k (L4002, L4006); GUI_TRAJ_CNTL lists it once (L6476). |
| 12 | DST_POLYGON_RTEDGE_DIS | disables the right-edge pixel of a polygon fill |
| 13 | TRAIL_X_DIR | trailing-edge X direction (GT) |
| 14 | TRAP_FILL_DIR | 0 = right to left (trailing edge is left of the leading edge); 1 = left to right (trailing edge is right of the leading edge) (RRG L4018-4020) |
| 15 | TRAIL_BRES_SIGN | zero-sign for the trailing error (GT) |
| 17 | BRES_SIGN_AUTO | 1 = ignore bits 11 and 15. A zero error is then positive "for X Major lines whose Y_DIR is 0 or for Y Major lines whose X_DIR is 0" (RRG L4051-4057) |
| 19, 20 | ALPHA_OVERLAP_ENB, SUB_PIX_ON | GTPro only (atiregs L1424-1425); **not on GT2C** |

- Inference on which edge is left: neither edge is fixed as the left edge. The leading edge starts at DST_X, and TRAP_FILL_DIR says which side the trailing edge is on, so the span runs from the leading edge toward the trailing edge.
- **Not found:** whether the pixels at the leading and trailing X are included, and whether a scanline with trailing X equal to leading X draws one pixel or none.
- Worked example of a trapezoid: **Not found** in any source (the RRG has none; PRG says "low level 3D operations are not discussed in this guide", L663-666).

### 2.6 SRC_CNTL (0_6D) note

SRC_TRACK_DST (bit 7, VTB/GTB, atiregs L1471) makes the source "track the trajectory which the Dst FIFO is using", so source X/Y equal destination X/Y and SRC_X/SRC_Y are ignored. [S, RRG p.4-62 L4652-4656] Inference: this is how the 2D source follows a trapezoid. It is not needed for interpolated or textured spans, which take their data from DP_SRC = 5.

---

## 3. Per-pixel interpolators

### 3.1 Colour, alpha/fog, Z [S, RRG p.6-11…6-14, L9003-9220]

| Attribute | X_INC | Y_INC | START | Format |
|---|---|---|---|---|
| Red | RED_X_INC 0_F0 (alias 0_7C) | RED_Y_INC 0_F1 | RED_START 0_F2 | S.8.12 |
| Green | GREEN_X_INC 0_F3 (alias 0_7D) | GREEN_Y_INC 0_F4 | GREEN_START 0_F5 | S.8.12 |
| Blue | BLUE_X_INC 0_F6 | BLUE_Y_INC 0_F7 | BLUE_START 0_F8 | S.8.12 |
| Z | Z_X_INC 0_F9 | Z_Y_INC 0_FA | Z_START 0_FB | S.16.12 |
| Alpha / Fog | ALPHA_X_INC (FOG_X_INC) 0_FC | ALPHA_Y_INC (FOG_Y_INC) 0_FD | ALPHA_START (FOG_START) 0_FE | S.8.12 (X_INC printed "S. 8.12", L9191) |

- X_INC is the "interpolation value for steps in X in the DST_X_DIR direction along the leading edge of the trapezoid". [S, e.g. L9016-9017]
- Y_INC is the "interpolation value for steps in Y in the DST_Y_DIR direction along the leading edge". [S, L9030-9031]
- START is the "initial value … at the beginning of trapezoid". [S, L9045]
- Bit placement inside the 32-bit word: **Not found** (the diagram was lost). Inference: S.8.12 is 21 bits and S.16.12 is 29 bits, probably LSB-aligned. Verify by writing 0xFFFFFFFF and reading back.
- Inference on the stepping model: the engine holds each attribute's value at the current leading-edge pixel. It adds Y_INC on each Y step of the leading edge and X_INC on each X step of the leading edge, both in the DST_*_DIR sense. Along a span it adds X_INC for each pixel. So the driver should load X_INC = ∂a/∂x, Y_INC = ∂a/∂y, and START = a(DST_X, DST_Y), signed to match the DST_X_DIR and DST_Y_DIR conventions.
  - **Not found:** how the sign is applied when TRAP_FILL_DIR differs from DST_X_DIR (span stepping against the leading edge's X direction).
  - **Not found:** pixel-centre convention, i.e. whether START applies at the pixel centre or the corner.
- Fog colour comes from DP_FRGD_CLR, also called DP_FOG_CLR. [S, RRG p.4-89 L5697] With ALPHA_FOG_EN = 2, "use DP_FRGD_CLR register in place of Destination FIFO". [S, RRG p.6-4 L8554]
- Scaler aliasing: RED_START = SCALE_HACC, BLUE_START = SCALE_UV_HACC, RED_X_INC = SCALE_X_INC, GREEN_X_INC = SCALE_Y_INC, BLUE_X_INC = SCALE_XUV_INC. [S, RRG L8714, L8733, L8746, L8440, L8455] The rule "Accumulator registers should only be written to when SCALE_3D_FCN@SCALE_3D_CNTL bits are set to a non-zero value" (RRG p.6-7 L8762-8763) therefore covers RED_START and BLUE_START. **Ordering rule (Inference):** write SCALE_3D_CNTL with a non-zero FCN before the *_START registers.
- SCALE_VACC "is incremented during the scale operation, and so must be reloaded for every scale" (RRG L8485-8486). Inference: the START accumulators are also consumed by a draw and should be rewritten for each trapezoid unless measurement shows otherwise.
- The scaler DDAs are reused colour interpolators with 8.12 unsigned precision. [S, RRG L8360-8371]

### 3.2 Texture S/T (GT/GTB): second-order differences, no W [S, RRG p.6-8…6-10, L8797-8971]

| Register | MM | Format | RRG definition |
|---|---|---|---|
| S_START | 0_D5 | 10.11 | "Initial value of S coordinate address" |
| S_XINC_START | 0_D3 | S.11.16 | "Value of S_X_INC at beginning of trapezoid span" |
| S_Y_INC | 0_D4 (alias 0_7B) | S.11.16 | "Change of S address when stepping in Y along the leading edge" |
| S_X_INC2 | 0_D0 | S.10.16 | "Change of S_X_INC when stepping in X along the leading edge" |
| S_Y_INC2 | 0_D1 | S.10.16 | "Change of S_Y_INC when stepping in Y along the leading edge" |
| S_XY_INC2 | 0_D2 | S.10.16 | "Change of S_X_INC when stepping in Y along the leading edge" |
| T_START | 0_DB | 10.11 | as S |
| T_XINC_START | 0_D9 | S.11.16 | as S |
| T_Y_INC | 0_DA | S.11.16 | as S |
| T_X_INC2 / T_Y_INC2 / T_XY_INC2 | 0_D6/0_D7/0_D8 | S.10.16 | as S |

- There is no plain S_X_INC register on GT/GTB. Inference: the per-pixel S increment is a running value seeded from S_XINC_START.
  - On GTPro, 0_D3 is renamed S_X_INC (mach64.h L360) and 0_D6-D8 become W_X_INC, W_Y_INC and W_START (atiregs L1677-1682).
- Inference on the hardware model (2nd-order forward differencing, i.e. S quadratic in x and y):
  - Along a span, per pixel: `S += S_X_INC; S_X_INC += S_X_INC2`.
  - Along the leading edge, per Y step: `S_edge += S_Y_INC; S_Y_INC += S_Y_INC2; S_XINC_edge += S_XY_INC2`.
  - On a leading-edge X step: `S_edge += S_XINC_edge; S_XINC_edge += S_X_INC2`.
  - In formulas: with S(x,y) ≈ a + b·x + c·y + d·x² + e·xy + f·y² about the start point, S_XINC_START ≈ b + d, S_X_INC2 = 2d, S_Y_INC ≈ c + f, S_Y_INC2 = 2f, S_XY_INC2 = e. The exact offsets (½ terms) depend on whether the hardware adds before or after it uses the value, which is **Not found**.
- Perspective correction method: the hardware approximates s = u/w, t = v/w with quadratic interpolation over each trapezoid. Inference, based on there being no W/divide register on GT/GTB and the existence of the second-derivative registers.
  - The RRG feature list only says "Perspectively-correct texture mapping" (L306).
  - CIF offers ten values, C3D_ETPC_NONE…NINE (CIF L4338-4365), although the text says "seven" (CIF L1364). Higher levels cost frame rate (CIF L1366-1369, L4377-4379). Inference: the levels are the CPU subdividing the primitive into smaller quadratic patches.
  - CIF's API input is homogeneous s = u/w, t = v/w, 1/w (CIF L1400-1416). That is API-level and does not prove any hardware W.
- S/T units: "S coordinate address", 10 integer bits. Inference: S/T are in texels of the largest map (2^10 = 1024 = the maximum size), and repeat comes from integer truncation. Clamping is RAGE PRO only (CIF L1921, L2049-2056; TEX_CNTL clamp bits atiregs L2845-2846; TEX_CNTL itself is GTPro, L1688). CIF advises keeping texture coordinates ≤ 10.0 because larger values "require additional processing by the ATI3DCIF driver" (CIF L5360-5361). Inference: the driver range-reduces the coordinates.
- How the mip level is chosen: **Not found.** Inference: probably from the S/T increment magnitudes, since there is no W and no LOG_MAX_INC (LOG_MAX_INC at 0_D2 is GTPro, atiregs L1672).

---

## 4. SCALE_3D_CNTL (0_7F, 0x5FC)

How the positions were derived: the RRG letter row is "x w v u t s r q p o n m l k j i h g f e d c b a" (L8496/L8569/L8638). Field widths come from the value lists: g, k and t take 4 values (2 bits), q takes 4 (2 bits), o and p take 6 (3 bits). Totalling them fills exactly 32 bits, a = 0 … x = 31. Every position also matches the atiregs/DRI definitions (L2756-2823) except where noted.

| Bits | RRG field | Values (RRG) | atiregs/DRI (PRO-derived) |
|---|---|---|---|
| 0 | SCALE_PIX_EXPAND | 0 = zero extend, 1 = dynamic range correct | same |
| 1 | SCALE_DITHER | 0 = X error diffusion, 1 = 2D table dither | same |
| 2 | DITHER_EN | enable dither of the scaled/3D image | same |
| 3 | DITHER_INIT | 1 = reset the error at the start of each line | same |
| 4 | ROUND_EN | rounding instead of dither; truncate if both are off | same |
| 5 | TEX_CACHE_DIS | disable the texel cache (faster in latency-bound "NO BLEND" modes) | same |
| 7:6 | SCALE_3D_FCN | 0 = NOP, 1 = Scaling, 2 = Texture Mapping, 3 = Shading | same |
| 8 | SCALE_PIX_REP | 1 = replicate pixels during scale (stretchblt) | DRI: EDGE_ANTI_ALIAS (PRO) |
| 9 | NEAREST_TEX_VIS | 1 = texel-visibility test on the nearest texel only | DRI: TEX_CACHE_SPLIT (PRO) |
| 10 | APPLE_YUV_MODE | signed UV | same |
| 12:11 | ALPHA_FOG_EN | 0 = off, 1 = alpha blend, 2 = fog (DP_FRGD_CLR replaces the dest FIFO), 3 = reserved | same |
| 13 | COLOR_OVERRIDE | with TEX_MAP_AEN, use the interpolator colour instead of the texel colour (texel alpha still used) | DRI: ALPHA_BLEND_SAT (PRO) |
| 14 | RED_DITHER_MAX | RGB8: limit red so the colour range is 0-223 | same |
| 15 | SIGNED_DST_CLAMP | signed destination clamp (MPEG MC) | same |
| 18:16 | ALPHA_BLND_SRC | 0 = 0, 1 = 1, 2 = Dst colour, 3 = 1−Dst colour, 4 = As, 5 = 1−As | DRI adds 6/7 = DSTALPHA/INVDSTALPHA (PRO only, Inference) |
| 21:19 | ALPHA_BLND_DST | 0 = 0, 1 = 1, 2 = Src colour, 3 = 1−Src colour, 4 = As, 5 = 1−As | DRI adds 6/7 (PRO) |
| 23:22 | TEX_LIGHT_FCN | 0 = none (texel), 1 = modulate, 2 = alpha decal (texel·αt + interp·(1−αt); needs TEX_MAP_AEN; uses the interpolator alpha if alpha blending is on), 3 = reserved | same |
| 24 | MIP_MAP_DISABLE | 1 = only the largest present map is used | same |
| 25 | BILINEAR_TEX_EN | magnification: 1 = 2×2 blend in the largest map; 0 = nearest. **If BILINEAR = 0 and the min mode is a 2×2 mode, nothing is drawn under magnification.** | same |
| 27:26 | TEX_BLEND_FCN | min: 0 = nearest map, no blend; 1 = blend the two closest maps; 2 = 2×2 within the nearest map; 3 = 2×2 within the next-nearest map (multi-pass trilinear; alpha from the distance to the nearest map) | DRI names 2 = LINEAR, 3 = TRILINEAR |
| 28 | TEX_AMASK_AEN | use only the texel alpha LSB as a mask | same |
| 29 | TEX_AMASK_MODE | 0 = 3D-DDI (mask 0 = not drawn); 1 = blended edge | DRI: TEX_AMASK_BLEND_EDGE |
| 30 | TEX_MAP_AEN | texture has alpha (32-bit 8888, 1555, 4444) | same |
| 31 | SRC_3D_SEL | 0 = interpolators / texture FIFOs; 1 = host FIFO (no blending on host texels) | DRI: SRC_3D_HOST_FIFO |

Sources: RRG p.6-4…6-6, L8498-8708; atiregs L2756-2823.

- The texel format is **not** in SCALE_3D_CNTL. It is DP_SCALE_PIX_WIDTH, DP_PIX_WIDTH[31:28] (§5). [S, RRG p.4-93 L5875; atiregs L1588]
- Inference on flat versus Gouraud: there is no explicit flat/Gouraud bit. Flat shading means FCN = 3 with all colour X/Y increments set to 0, or the 2D path with DP_FRGD_CLR.
- RAGE PRO reset sequence for comparison: atyfb writes SCALE_3D_CNTL = 0xC0, then SETUP_CNTL = 0, then SCALE_3D_CNTL = 0 with delays. It does this only for chips flagged M64F_RESET_3D (GTPRO/LTPRO/XL/Mobility), **not GT2C** (m64accel L57-65; atyfb_base L376-381).

### 4.1 Z_CNTL (0_53) and Z_OFF_PITCH (0_52)

- Z_OFF_PITCH: Z_OFFSET is in 64-bit words and Z_PITCH is in pixels·8, the same form as DST_OFF_PITCH. [S, RRG p.4-60 L4521-4530] Inference on positions: the same as DST_OFF_PITCH, OFFSET 19:0 and PITCH 31:22 (atiregs L1392-1394).
- Z_CNTL fields [S, RRG p.4-61 L4559-4600]:
  - Z_EN enables Z testing.
  - Z_SRC has a duplicated "Enables use of Z functions" description; its meaning is unclear.
  - Z_TEST (3 bits): 000 never, 001 <, 010 ≤, 011 ==, 100 ≥, 101 >, 110 !=, 111 always.
  - Z_MASK: 1 enables writes to the Z planes.
  - "Z_MASK allows Z to apply to colors, even if Z itself is never written."
- Bit positions: **Not found for GT.** RAGE PRO (mesa_reg L375-386): Z_EN = bit 0, Z_SRC_2D = bit 1, Z_TEST = bits 6:4, Z_MASK_EN = bit 8. Inference: GT2C uses the same layout. The letter row "d c b a" (L4549) fits that order.
- Depth is 16 bits (CIF p.3-5 L1774). The Z buffer must be 8-byte aligned and have the same pitch in pixels as the drawing surface under CIF (CIF L1772-1781). Z_START being S.16.12 fits a 16-bit Z (Inference).
- Z buffering exists "on the ATI 3D RAGE II graphics accelerator or later" (CIF L892, L1548, L1771). The RRG nevertheless documents Z_CNTL under "GT". Inference: either Rage I silicon lacked a usable Z or the RRG is a preliminary revision. GT2C is a Rage II derivative, so this does not matter for this target.

### 4.2 Textures: TEX_n_OFF, TEX_SIZE_PITCH, palette

- TEX_k_OFF (0_70+k) is a "byte pointer" to the 2^k × 2^k map: TEX_0 = 1×1 … TEX_10 = 1024×1024. [S, RRG p.6-8 L8785-8795]
- They alias the scaler registers: TEX_0_OFF = SCALE_Y_OFF (bits 2:0 must be 0 for scaler use, L8388-8389), TEX_7_OFF = SCALE_WIDTH, TEX_8_OFF = SCALE_HEIGHT. [S, L8376-8422] Inference: texture offsets should be 8-byte aligned.
- TEX_SIZE_PITCH (0_DC) [S, RRG p.6-11 L8987-8997]:
  - TEX_PITCH is log2 of the pitch in pixels of the largest map (for a non-square map, the actual map).
  - TEX_SIZE is log2 of the largest map, programmed as the largest dimension.
  - TEX_HEIGHT is log2 of the height of the largest map.
  - Bit positions: TEX_PITCH 3:0, TEX_SIZE 7:4, TEX_HEIGHT 11:8. This is an inference from the "c b a" letter order and from `TEX_LEVEL(tsp) = (tsp & 0xf0) >> 2` (atiregs L2877-2880; mesa_mach64_ioctl L858). That macro reads TEX_SIZE from bits 7:4 and turns it into the byte offset of TEX_<size>_OFF.
- Mip chain: for a single non-mipmapped texture, drivers write only TEX_<TEX_SIZE>_OFF (atimach64accel L195-199). Inference: hardware level k reads TEX_k_OFF, and lower levels are used only if MIP_MAP_DISABLE = 0.
- TEX_CNTL (0_DD) is **GTPro only** (atiregs L1688). It does not exist on GT2C, so GT2C has no wrap/clamp bits, LOD bias or compositing.
- Palette:
  - On the 3D RAGE, "Pseudocolour-to-RGB conversion is done via a read of the RAMDAC palette" (RRG p.4-94 L5970).
  - atiregs lists TEX_PAL_WR at 0_DF tagged GTB (L1690), and TEX_PALETTE / TEX_PALETTE_INDEX as GTPro (L1668, L1691).
  - The TEX_PAL_WR data format is **Not found**.
  - CIF says CI4/CI8 textures and palettes are RAGE II+ (CIF L892, L1583-1592). Changing palettes per texture "physical palette is changed each time … may cause visual artifacts", so use at most one CI8 palette and 16 CI4 palettes (CIF L1663-1666).
- CI4 [S, RRG p.4-92/4-93; atiregs L1583-1587]:
  - DP_CI4_RGB_LOW_NIBBLE (bit 26) or DP_CI4_RGB_HIGH_NIBBLE (bit 27) means the texture is 4 bpp in bits 3:0 or 7:4 of each byte "when in CI8 → RGB texture lookup mode" (printed "C18").
  - DP_CI4_RGB_INDEX (bits 23:20) supplies the upper 4 index bits, selecting one of 16 palettes.
  - Inference: CI4 = DP_SCALE_PIX_WIDTH 2 (CI8) plus a nibble bit.
- Texel-key transparency [S]:
  - CLR_CMP_CNTL's CLR_CMP_SRC = 2 means texel (atiregs L1630).
  - "When color keying on the Texel source, the key is compared against the expanded (24 bit) source"; for 8-bit pseudocolour the key is in the low 8 bits (RRG p.4-98 L6116-6118).

---

## 5. DP_* and scissor setup for 3D draws

- DP_PIX_WIDTH (0_B4) [S, RRG p.4-92/4-93; positions atiregs L1574-1588]:
  - DST 3:0, SRC 11:8, HOST 19:16, CI4_RGB_INDEX 23:20, BYTE_PIX_ORDER 24, CONVERSION_TEMP 25, CI4 low/high nibble 26/27, **SCALE_PIX_WIDTH 31:28**.
  - GT DST codes: 0 mono, 1 4bpp, 2 8bpp pseudo, 3 1555, 4 565, 6 8888, 7 RGB332, 8 Y8, 11 YUV422, 14 aYUV444.
  - SCALE (texel) codes: 2 CI8, 3 aRGB1555, 4 RGB565, 6 aRGB8888, 7 RGB332, 8 Y8, 11 YUV422 (YUYV), 14 aYUV444, 15 aRGB4444.
  - The scaler/3D path does not support packed 24 bpp (L5636).
  - Allowed source→dest conversions are tabulated at RRG p.4-94 (L5945-5970). The table is garbled. Readable parts: RGB12/15/16/32 and YUV sources can go to RGB 8/15/16/32.
- DP_SRC (0_B6) [S, RRG p.4-97 L6070-6090; atiregs L1594-1600]:
  - BKGD 2:0, FRGD 10:8, MONO 17:16.
  - **Source 5 = "Scaler/3D data (3D RAGE)"** (also atiregs SRC_SCALER_3D = 5, L2705).
  - For shaded or textured spans, set FRGD_SRC = 5 and MONO_SRC = 0 (always 1). This is inference for 3D, but PRG states it for scaler blits: "DP_FRGD_SRC@DP_SRC = 5, to use the front end scaler data" (PRG p.8-14, L6517).
- DP_MIX (0_B5): FRGD 20:16, BKGD 4:0 (atiregs L1589-1593). MIX_SRC = 7 (atiregs L2719). Code 17h ((DST+SRC)/2) is reserved on the 3D RAGE (RRG L6008-6020, garbled table). "When Alpha Blending is enabled, the Destination Read FIFO is unavailable to the 2D engine. In this case, DP_MIX must not use the Destination." [S, RRG p.4-95 L6037-6038]
- DP_WRITE_MSK must be 0xFFFFFFFF when alpha blending is enabled. [S, RRG p.4-90 L5732-5733]
- DP_FRGD_CLR doubles as DP_FOG_CLR. [S, L5697]
- SC_LEFT_RIGHT holds left in bits 15:0 and right in bits 31:16; SC_TOP_BOTTOM holds top in bits 15:0 and bottom in bits 31:16 (atimach64accel L206-209). Both "must be set for all draw operations" (RRG p.4-87).
- DST_OFF_PITCH: offset in qwords at bits 19:0 and pitch in pixels/8 at bits 31:22 (RRG p.4-53 L4215-4220; atiregs L1391-1394; m64accel L105).
- PRG's scaler-blit sequence (the only documented scaler/3D-pipe sequence) [S, PRG p.8-13/8-14, L6485-6525]:
  1. Set SCALE_3D_FNC.
  2. (PRO only: ALPHA_TST_CNTL = 0, TEX_CNTL = 0.)
  3. Set the scaler source offset, pitch, width and height.
  4. Set SCALE_X_INC and SCALE_Y_INC.
  5. Set DP_FRGD_SRC = 5, DP_SCALE_PIX_WIDTH, DP_DST_PIX_WIDTH, DP_WRITE_MSK, DP_MIX and GUI_TRAJ_CNTL; then DST_X, DST_Y and DST_HEIGHT; "DST_WIDTH … this initiates the blt".

  Inference: the trapezoid version replaces the last step with the Bresenham terms and DST_BRES_LNTH (bit 15).

---

## 6. FIFO, idle and reset rules

- Only registers with DWORD index ≥ 0x40 go through the command FIFO; all others bypass it. [S, RRG p.4-101 L6290-6291] Every 3D register above is ≥ 0x40, so all of them are FIFOed and ordered.
- "Each grouping of register writes through the command FIFO must be preceded by a FIFO check." [S, RRG L6295] Overrunning the FIFO sets FIFO_ERR and locks the engine. [S, L6284-6288]
- FIFO_STAT has one bit per full entry in bits 15:0 (atiregs L1634-1636). For the 3D RAGE the RRG says it covers "the top half of the FIFO entries" and that the parameter FIFO is 48 entries, with an encoded free count in GUI_STAT.FIFO_CNT "less than or equal to 32" (RRG L6272-6277, L6613-6628). GUI_FIFO is bits 25:16 (VTB/GTB, atiregs L1664).
  - Conflict: atyfb gives GT2C no M64F_FIFO_32 flag and uses `16 - fls(FIFO_STAT & 0xffff)` (atyfb.h L349-356; atyfb_base L376).
  - Inference: use the 16-entry FIFO_STAT check, as PRG WaitForFifo does (`0x8000 >> n`, PRG p.5-1 L2140-2148).
- GUI_STAT bit 0, GUI_ACTIVE, means on the 3D RAGE "GUI engine is busy OR the 3D engine is busy OR the command FIFO is not empty OR context loading". [S, RRG L6601-6604] Idle = FIFO empty, then GUI_ACTIVE = 0 (atyfb.h L358-362).
- Engine reset [S, PRG p.5-1/5-2 L2155-2170, L2392-2398]:
  1. Clear, then set, GEN_GUI_EN (GEN_TEST_CNTL bit 8).
  2. Set BUS_CNTL BUS_FIFO_ERR_ACK and BUS_HOST_ERR_ACK (BUS_CNTL |= 0x00A00000).
- Write-ordering rules collected:
  1. SCALE_3D_CNTL with a non-zero FCN before any accumulator or *_START write (§3.1, RRG L8762).
  2. All trajectory and interpolator state before the DST_BRES_LNTH write with bit 15 = 1 (§2.4).
  3. Reload SCALE_VACC and, by inference, the START registers per draw (RRG L8485).
  - Other ordering constraints: **Not found.**

---

## 7. GT vs GTB (Rage II / II+ / IIC) vs GTPro

- atichip.c (L130-189):
  - `GT` with ChipVersion 0 → 264GT (Rage I); a non-zero version → 264GTB (Rage II/II+).
  - GV/GY → 264GT2C on PCI; GW/GZ → 264GT2C on AGP.
  - mach64.h L987-989: GV = 0x4756 "RAGE IIC, PCI", GW = 0x4757 "RAGE IIC, AGP", GZ = 0x475A.
  - PRG L1568-1572: 0x4757 = "3D RAGE IIC (BGA, AGP)".
  - **So 1002:4757 is the AGP Rage IIC**, not a PCI-bus part. It is still a PCI function ID.
- atiregs tags:
  - **GT:** TRAIL_BRES_*, LEAD_BRES_LNTH, Z_OFF_PITCH, Z_CNTL, TEX_0..10_OFF, S_Y_INC (0_7B), RED_X_INC/GREEN_X_INC (0_7C/7D), SCALE_3D_CNTL, and the DST_CNTL bits TRAIL_X_DIR, TRAP_FILL_DIR, TRAIL_BRES_SIGN, BRES_SIGN_AUTO.
  - **GTB:** S_X_INC2…T_START, TEX_SIZE_PITCH, TEX_PAL_WR, RED/GREEN/BLUE/Z/ALPHA_FOG X/Y/START at 0_F1-0_FE, DP_PIX_WIDTH SCALE/CI4/CONVERSION_TEMP fields, SRC_CNTL bits 5-14, GUI_STAT.GUI_FIFO, and DP_SET_GUI_ENGINE.
  - **GT2c/VT4:** DP_HOST_TRIPLE_EN.
  - **GTPro only (absent on 264GT2C):** ALPHA_TST_CNTL, SECONDARY_*, TEX_CNTL, TEX_PALETTE_INDEX, STW_EXP, LOG_MAX_INC, W_X_INC/W_Y_INC/W_START, the SPECULAR_*, VERTEX_n_*, ONE_OVER_AREA, SETUP_CNTL, and DST_CNTL ALPHA_OVERLAP_ENB/SUB_PIX_ON (L1425-1426).

  (atiregs L1406-1731, L2036-2099)
- Inference on the "GTB" tags: the RRG (written for GT, 1996) already documents S_X_INC2…ALPHA_START as 3D RAGE registers, so atiregs' GTB tags on them probably mean "present from GTB on, as far as the authors verified", not "absent on GT". It does not change anything for GT2C.
- RAGE PRO reuses 0_D0-0_D8 with different meanings. Code or headers written for the PRO (mach64.h, mesa_mach64_*, the DRI SCALE_3D_CNTL comments) must not be applied to GT2C at those indices. Bits 8, 9 and 13 of SCALE_3D_CNTL also differ (§4).
- The RAGE II "doubles 3D performance … over the 3D RAGE" and adds CI8/CI4 palettized textures and Z buffering (CIF L1543-1550).

---

## 8. Capability limits for Direct3D caps (CIF unless noted)

| Item | Value | Source |
|---|---|---|
| Texture size | power of two, ≤ 1024×1024, both dimensions | CIF L1026, L1592, L4914 |
| Non-square | supported via TEX_SIZE (max dim) / TEX_PITCH / TEX_HEIGHT | RRG L8987-8997 |
| Texture memory | video memory only | CIF L1024-1025 |
| Formats (RAGE II) | CI4, CI8, RGB1555, RGB565, RGB8888, RGB332, Y8, YUV422, RGB4444 | CIF L1122-1130; RRG p.4-93 adds aYUV444 |
| Filters | min: point / 2×2 / blend between maps / mode 3 (next-nearest map 2×2, multi-pass trilinear); mag: point or bilinear | RRG L8640-8671; CIF L1323-1340 |
| Texture lighting | none (decal), modulate, alpha decal | RRG L8608-8620; CIF L1350-1359 |
| Transparency | chroma key (texel compare), alpha-LSB mask (TEX_AMASK) | CIF L1381-1392; RRG L8673-8691 |
| Alpha blend src | ZERO, ONE, DESTCOLOR, INVDESTCOLOR, SRCALPHA, INVSRCALPHA | RRG L8592-8598; CIF L1437-1442 |
| Alpha blend dst | ZERO, ONE, SRCCOLOR, INVSRCCOLOR, SRCALPHA, INVSRCALPHA | RRG L8600-8606; CIF L1450-1455 |
| Dest alpha factors | none on GT2C (6/7 are DRI/PRO) | Inference |
| Blend ONE/ZERO | disables the blender; faster (CIF) | CIF L1458-1466 |
| Fog | per-vertex, via the alpha interpolator; constant colour DP_FOG_CLR; **mutually exclusive with alpha blending** | RRG L8551-8557; CIF L1472-1476 |
| Alpha test | none (ALPHA_TST_CNTL is GTPro) | atiregs L1435 |
| Z | 16-bit only; 8 compare functions; test with or without write | CIF L1774, L1793-1820; RRG L4567-4590 |
| Clamp / wrap modes | clamp is RAGE PRO only; wrap control not found (Inference: repeat is implicit) | CIF L1921, L2049 |
| Perspective | hardware quadratic (2nd-difference) S/T; no W | §3.2 |
| Dither | on/off, two algorithms | RRG L8502-8508 |
| Shading | Gouraud via interpolators; flat = zero increments | RRG p.6-11…; Inference |
| Coordinates | DST_X signed 13-bit, DST_Y signed 15-bit, TRAIL_X 13-bit, edge length 15-bit, Bresenham terms signed 18-bit | RRG L4320, L4401, L3903, L3897, L3762 |
| Sub-pixel | no sub-pixel registers on GT2C (SUB_PIX_ON is GTPro). Inference: sub-pixel edge placement must be encoded in the initial Bresenham error, with INC/DEC scaled within the 18-bit range. | atiregs L1425 |
| Colour precision | S.8.12 colour/alpha, S.16.12 Z, 10.11 S/T start, S.11.16 first and S.10.16 second differences | RRG p.6-8…6-14 |
| Benefit threshold (Rage I) | about 10 px per Gouraud triangle, about 30 px per textured triangle | CIF L5212-5216 |
| Max span length | **Not found** (the 13-bit X range implies ≤ 8191) | Inference |

---

## 9. Open items to measure on the A8U4I5 Rage IIC

1. Implemented bits in each interpolator, S/T and Z_CNTL register: write 0xFFFFFFFF and read back.
2. Draw a known trapezoid with flat colour (2D path). Record:
   - how LNTH maps to scanlines;
   - endpoint inclusion;
   - behaviour of X-major edges;
   - the effect of TRAP_FILL_DIR and DST_Y_DIR = 0;
   - DST_X/DST_Y after the draw.
3. Sign convention of the colour X_INC along a span when TRAP_FILL_DIR ≠ DST_X_DIR.
4. Whether a second trapezoid written with bit31 = 0 / bit15 = 1 continues the leading edge, trailing edge and accumulators.
5. The S/T forward-difference timing (pre- or post-add) that sets the ½ terms in §3.2.
6. The TEX_PAL_WR format.
