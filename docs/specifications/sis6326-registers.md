# SiS 6326: register reference reconciled against the datasheet

Target: SiS 6326, PCI `1039:6326`, the first SiS chip in the tree. Extracted
2026-10-04 for [the SiS 6326 plan](../plans/sis-6326-family.md); nothing here
is measured yet. Sources,
abbreviated in citations:

- **DS**: *SiS6326 AGP/PCI Graphics & Video Accelerator*, Preliminary V1.0,
  12 May 1997, covering Rev. Ax/Bx only (DS L7). Supplied as `6326.rtf`;
  converted to text with Word and cited by line of that extract (`r/6326.txt`,
  not kept in the tree — the datasheet is SiS copyright, DS L6165).
- **REF**: the 2026-10-04 research consolidation
  `sis6326-register-reference.md`, which draws on Mesa 7.5.1
  `sis6326_reg.h` and xf86-video-sis. Cited by its section number. It
  predates access to DS.

Markers: **[S]** the datasheet states it (citation follows). **[REF]** only
the consolidation states it, from Mesa/Xorg. **Inference:** worked out here.
**Disputed:** DS and REF disagree; the probe settles it. **Not found:** neither
source covers it.

DS describes Rev. Ax/Bx. Xorg handles later 6326 revisions (8 MiB boards,
REF §13, §40). Where DS and Xorg disagree the difference may be a revision
difference, not an error — record the PCI revision with every capture.

---

## 0. Executive summary

1. Identification: config 00h = `63261039h`, class `030000h`, revision `Ax`
   for Rev. A (DS L3595-3631). [S]
2. Resources: BAR0 (10h) is the 4 MiB linear framebuffer; BAR1 (14h) a 64 KiB
   MMIO window; BAR2 (18h) a 16-byte I/O range for VMI and relocated VGA I/O
   (DS L3632-3646, L2291-2294). [S]
3. Extended registers are locked until SR5 = 86h; SR5 then reads back A1h,
   and 21h when locked (DS L1676-1682). [S]
4. MMIO through BAR1 needs SRB D[6:5] = 11 (DS L1770-1774). Engine registers
   need SR27 D6 = 1 (DS L2102). The 3D engine needs SR39 D2 = 1 (DS L2397). [S]
5. Rev. Ax/Bx addresses at most 4 MiB of frame buffer (DS L73, L454). The
   2D engine's 22-bit addresses cover all of it. REF §41's 8 MiB question
   applies only to later revisions. [S]
6. 2D engine: 12 registers at 8280h-82ABh plus 128 bytes of pattern RAM at
   82ACh-8328h. A word write to 82AAh (Command 0 + Command 1) starts the
   operation (DS L2615). [S]
7. Busy: 82ABh D6 = 1 means "engine busy or hardware queue not empty" (DS
   L2595-2597). This is the 4000h bit REF §8.1 polls with a word read of 82AAh.
   [S]
8. Pitches, widths, heights and clip coordinates are 12-bit fields (DS
   L2483-2551), not the 16-bit fields REF §5.3 assumes. [S]
9. 24 bpp ("16.8M colour") limits the 2D engine to source/destination BitBlt,
   pattern/destination BitBlt and colour expansion (DS L349-352). Rev. Ax/Bx
   has no 32 bpp mode at all (DS L548, L917). [S]
10. 3D vertices: X, Y, Z, U, V and W are 32-bit floats. ARGB and fog/specular
    are packed 8-bit integers (DS L4629-4762). The setup engine "can accept
    vertex values directly in floating point format" (DS L412). [S]
11. 3D primitive: 89F8h selects point/line/triangle, the vertex order, the
    shading mode and which register write fires the engine. 89FCh is fire on
    write and status on read (DS L4769-4863). [S]
12. Destination, Z, alpha, clip, blend and texture fields are all fully
    specified in DS §7.14 (sections 6-10 below). REF §35, §36 and §43 listed
    them as unknown. [S]
13. 2D and 3D share one hardware queue and one Turbo Queue, and only one
    engine is active at a time, so order between them is preserved (DS
    L409). [S]
14. Disputed: SR0C memory decode (REF §13.1), SR0E[1:0] DRAM type (REF §14.1),
    several REF texture and pitch masks (section 11). [S vs REF]
15. Not found: whether the rectangle width and height are programmed as
    *n* or *n*-1; the units of the 11-bit texture pitch; the PLL reference
    frequency. [Not found]

---

## 1. PCI configuration

| Offset | Field | Value / meaning | Source |
|---|---|---|---|
| 00h | Device/Vendor | `63261039h` | DS L3595 |
| 04h | Command/Status | Default `02200004h`; bus master fixed on (D3); 66 MHz capable (D21); medium DEVSEL | DS L3600-3625 |
| 08h | Class/Revision | `030000h`; revision `Ax` for Rev. A | DS L3626-3631 |
| 10h | BAR0 | 32-bit memory, 4 MiB linear framebuffer; default `00000008h` (prefetchable) | DS L3632-3636 |
| 14h | BAR1 | 32-bit memory, 64 KiB MMIO | DS L3637-3641 |
| 18h | BAR2 | 32-bit I/O, 16 bytes, reserved for VMI; also the relocated VGA I/O base when SR33 D5 allows it | DS L3642-3646, L2291 |
| 2Ch | Subsystem | write-once | DS L3647-3652 |
| 30h | ROM BAR | default `000C0000h` | DS L3653-3660 |
| 3Ch | Interrupt | pin = 01h only when the INTA# strap (SRE D3) is set | DS L3661-3670 |
| 34h, 50h-5Ch | AGP capability | present only when AGP is strapped on (SRD D4) | DS L3673-3719 |

Inference: on a PCI board with AGP strapped off, the capabilities bit
(04h D12) reads 0 and 34h is not decoded.

## 2. Extended sequencer registers (3C4h/3C5h)

Index range 05h-3Ch (DS L1673-1675). The rows below are the ones the driver
reads or writes during bring-up; the full list is in DS §7.7.

| Reg | Bits | Meaning | Source |
|---|---|---|---|
| SR5 | 7:0 | Write 86h to unlock (reads A1h); anything else locks (reads 21h) | DS L1676-1682 |
| SR6 | 7 | Linear addressing enable | DS L1687 |
| SR6 | 6 | Hardware cursor enable | DS L1690 |
| SR6 | 4/3/2 | True-colour / 64K / 32K graphics mode | DS L1696-1704 |
| SR6 | 1 | Enhanced graphics mode | DS L1705 |
| SR7 | 1 | High-speed DAC; set when DCLK > 135 MHz | DS L1734-1737 |
| SRA | 7:4 | Screen offset bits 11:8 | DS L1758 |
| SRA | 3:0 | Vertical overflow bit 10 (retrace start, blank start, display end, total) | DS L1759-1762 |
| SRB | 7 | True-colour byte order: 0 = RGB, 1 = BGR | DS L1767-1769 |
| SRB | 6:5 | MMIO window: 00 off, 01 A0000h, 10 B0000h, 11 PCI BAR1 | DS L1770-1774 |
| SRB | 0 | CPU-driven BitBlt enable | DS L1787 |
| SRC | 7 | Graphics-mode 32-bit memory access | DS L1794 |
| SRC | 5 | Read-ahead cache | DS L1800 |
| SRC | 2:1 | Memory configuration (section 3) | DS L1807-1811 |
| SRD | 7:0 | Strap readback: ROM size, clock source, AGP 2X, AGP enable, NTSC/PAL, VGA disable, 3C3h/46E8h (read-only) | DS L1815-1840 |
| SRE | 7:5 | DRAM speed straps MD[31:29] (read-only) | DS L1845-1849 |
| SRE | 4:2 | VMI, INTA#, ROM-decode straps (read-only) | DS L1850-1858 |
| SRE | 1:0 | Reserved | DS L1859 |
| SR11 | 1:0 | DDC data / clock, bit-banged | DS L1887-1900 |
| SR12 | 4:0 | Horizontal overflow: blank end bit 6; retrace start, blank start, display end and total bit 8 | DS L1914-1918 |
| SR13 | 7 / 6 | MCLK post-scale bit 2 / VCLK post-scale bit 2 | DS L1923-1924 |
| SR14-SR19 | 5:0 | Cursor colours 0/1, 6 bits per gun | DS L1940-1975 |
| SR1A-SR1F | | Cursor position and preset | DS L1976-2012 |
| SR1E | 7:4 | Cursor pattern select | DS L2002 |
| SR20/SR21 | | Linear base bits 31:19; SR21 D[6:5] aperture 512K/1M/2M/4M | DS L2013-2028 |
| SR23 | 5 | EDO DRAM enable | DS L2045 |
| SR26 | 5 | PCI burst-write enable | DS L2081 |
| SR27 | 7 | Turbo Queue enable | DS L2099 |
| SR27 | 6 | Graphics engine register programming enable | DS L2102 |
| SR27 | 5:4 | Logical screen width: 00 = 1024 (8 bpp) / 512 (16 bpp), 01 = 2048/1024, 10 = 4096/2048 | DS L2105-2109 |
| SR27 | 3:0 | Screen start address bits 19:16 | DS L2110 |
| SR28/SR29 | | MCLK synthesizer (section 4) | DS L2111-2141 |
| SR2A/SR2B | | VCLK synthesizer, banked by SR38 D[1:0] (section 4) | DS L2142-2230, L2377-2385 |
| SR2C | 6:0 | Turbo Queue base address | DS L2231-2236 |
| SR2D | 3:0 | DRAM page size | DS L2237-2248 |
| SR2F | 4 | Fast page flip enable (SR30-SR32 hold the address; writing SR32 latches it) | DS L2262-2282 |
| SR33 | 5 / 4 | Relocated VGA I/O decode / standard VGA I/O decode disable | DS L2291-2298 |
| SR33 | 3, 2, 1, 0 | 1-cycle EDO; SGRAM CAS latency; SGRAM mode write; SGRAM timing | DS L2299-2311 |
| SR34 | 0 | Hardware command queue threshold low | DS L2327 |
| SR35 | 7 | Hardware MPEG enable | DS L2334 |
| SR38 | 7:4 | Cursor address bits 21:18 | DS L2371-2372 |
| SR38 | 1:0 | VCLK register bank: 00 internal, 01 25 MHz, 10 28 MHz | DS L2377-2385 |
| SR39 | 2 | 3D accelerator enable | DS L2397 |
| SR3C | 2 | PCI 66 MHz timing | DS L2435 |
| SR3C | 1:0 | Turbo Queue split: 00 = 2D 32K / 3D 0; 01 = 16/16; 10 = 8/24; 11 = 4/28 | DS L2438-2442 |

SR36/SR37, SRF, SR10 and SR25 are scratch registers reserved for the video
BIOS (DS L1860-1869, L2068-2072, L2357-2366). REF §14.2 reads SR37 as a
memory-timing flag; DS gives it no hardware meaning, so any meaning is a BIOS
convention. [S]

## 3. Memory size and type

**SRC D[2:1]** (DS L1807-1811): [S]

| D[2:1] | Configuration |
|---|---|
| 00 | 1 MiB, 1 bank |
| 01 | 2 MiB, 2 banks |
| 10 | 4 MiB, 2 or 4 banks |
| 11 | 1 MiB, 2 banks |

**Disputed — REF §13.1.** Xorg forms a 3-bit code from SR0C bit 4 and bits
2:1, and maps codes 3 and 4 to "reserved". DS marks SRC D4 reserved (DS L1803)
and defines code 3 as 1 MiB / 2 banks. Bit 4 is probably the later-revision 8
MiB extension. The probe should log all of SRC; size the VRAM from D[2:1] when
D4 = 0, and treat D4 = 1 as unexplained until measured.

**Disputed — REF §14.1.** REF reads the DRAM type from SR0E D[1:0]. DS marks
those bits reserved: the DRAM-type straps MD[25:24] are not reflected there
(DS L1859). DS exposes the active type only through timing enables: SR23 D5
(EDO), SR33 D3 (1-cycle EDO), SR33 D0 (SGRAM) (DS L2045, L2299, L2309).
**Inference:** classify from those three bits and record SR0E raw alongside.

Strap meanings for MD[25:24]: 00 SGRAM/SDRAM, 01 2-cycle EDO, 10 1-cycle EDO,
11 fast page (DS L445-449). [S]

The VGA BIOS programs memory timing. The driver must not write SR23, SR26,
SR28/29, SR33-SR35 or SR3C D[2] during bring-up (REF §14.2 agrees).

## 4. Clock synthesizer

DS formula (DS L526-530): `fd = fr × (Numerator / DeNumerator) × (Divider / PostScale)`. [S]

Register fields (MCLK shown; VCLK SR2A/SR2B are identical): [S]

- SR28 D7: divider, 0 = ×1, 1 = ×2 (DS L2115-2117). D[6:0]: numerator − 1
  (DS L2118-2119).
- SR29 D7: VCO gain, 1 = high frequency. D[6:5]: post-scale 1/2/3/4, or
  reserved/reserved/6/8 when the extended post-scale bit is set. D[4:0]:
  denominator − 1 (DS L2125-2140).

**Disputed within DS.** The SR29 text names SR13 D6 as the MCLK post-scale
extension (DS L2129) and the SR2B text names SR13 D7 for VCLK (DS L2178), but
SR13 itself defines D7 as MCLK and D6 as VCLK (DS L1923-1924). Trust SR13's
own definition; confirm against a BIOS mode dump.

**Inference — reference frequency.** DS does not state `fr`. With
`fr` = 14.31818 MHz, REF §15's 66 MHz entry (SR28 = 5Ah, SR29 = E4h) decodes as
14.31818 × 91/5 × 1/4 = 65.1 MHz, and its 45 MHz entry (2Bh, 26h) as
14.31818 × 44/7 × 1/2 = 45.0 MHz. Both agree, so the decode and `fr` are
consistent with Xorg's table. VCLK programming is not needed while modes come
from the BIOS.

## 5. 2D engine (MMIO 8280h-8328h)

### 5.1 General-function register format (DS L2453-2621) [S]

| Offset | Bits | Field |
|---|---|---|
| 8280h | 21:0 | Source start linear address (bytes) |
| 8284h | 21:0 | Destination start linear address (bytes) |
| 8284h | 24 / 25 / 26 | Pattern bank select for 8 bpp copy / high-colour copy / colour expansion (registers 64-127) |
| 8284h | 31 | Enhanced colour-expansion busy (read-only) |
| 8288h | 11:0 | Source pitch |
| 828Ah | 11:0 | Destination pitch |
| 828Ch | 11:0 | Rectangle width |
| 828Eh | 11:0 | Rectangle height |
| 8290h | 23:0 / 31:24 | Foreground colour / foreground ROP3 |
| 8294h | 23:0 / 31:24 | Background colour / background ROP3 |
| 8298h-829Fh | 63:0 | Mono mask (8×8) |
| 82A0h / 82A2h | 11:0 | Clip left / top |
| 82A4h / 82A6h | 11:0 | Clip right / bottom |
| 82A8h | 4:0 | Free hardware-queue entries (when the Turbo Queue is off) |
| 82A8h | 15:0 | Turbo Queue head (write) / tail (read) index (when it is on) |
| 82AAh | | Command 0 |
| 82ABh | | Command 1 |
| 82ACh-8328h | | Pattern registers 0-127 |

Write the destination address with bits 31:22 clear: bits 24-26 select
pattern banks and are not address bits.

### 5.2 Command 0 (82AAh) [S, DS L2562-2587]

| Bits | Meaning |
|---|---|
| 7 | Clip mode: 0 = draw inside, 1 = draw outside |
| 6 | Clip enable |
| 5 | Y direction: 1 = increment |
| 4 | X direction: 1 = increment |
| 3:2 | Pattern source: 00 BG colour, 01 FG colour, 10 pattern registers, 11 reserved (Direct Draw mode, 5.5) |
| 1:0 | Source: 00 BG colour, 01 FG colour, 10 video memory, 11 CPU |

### 5.3 Command 1 (82ABh) [S, DS L2588-2615]

| Bits | Meaning |
|---|---|
| 7 | Hardware queue empty (read) |
| 6 | Busy: engine busy or hardware queue not empty (read) |
| 5 | Enhanced colour expansion (monochrome source in off-screen memory, m×n format) |
| 4 | Enhanced font expansion (8×n format) |
| 3 | Line: 1 = skip last pixel |
| 2 | Line: 1 = X major |
| 1:0 | Command: 00 BitBlt, 01 BitBlt with mask, 10 colour/font expansion, 11 line |

A word write to 82AAh (Command 0 + Command 1) starts the engine (DS L2615).
REF §8.2's dummy read of 82A8h after the fire is Xorg practice; DS does not
require it. Keep it until a probe shows it is unnecessary.

### 5.4 Open points

- **Measured 2026-10-05: *n*-1.** REF §5.4 (Xorg) writes height-1 and
  width-in-bytes-1; DS says only "Rectangular Width/Height" (DS L2491-2502).
  A fill holding 31 and 3 wrote exactly 32 bytes by 4 rows at 8 and 16 bpp
  ([decision](../decisions/2026-10-05-sis6326-2d-engine-writes.md)).
- **[S, inference]** The width is in bytes. The register is 12 bits wide and
  1280×16 bpp needs 2560 bytes, so 4095 is enough for every DS mode except 1600
  wide at more than 8 bpp, which DS does not offer (DS L548).
- **Measured 2026-10-05: the last byte.** A right-to-left copy started on
  the last byte of its last pixel (REF §10, from Xorg) lands exactly at 8 and
  16 bpp (same decision).
- 24 bpp: only source/destination BitBlt, pattern/destination BitBlt and
  colour expansion (DS L349-352). Solid fill at 24 bpp is a pattern/destination
  BitBlt with the pattern taken from the FG colour.

### 5.5 Line and Direct Draw formats

Line drawing reuses the block (DS L2627-2776): X start 8280h, Y start 8284h,
major-axis count 828Ch, K1/K2 8298h/829Ah (14 bits), error term 829Ch (14
bits), line style 829Eh (16 bits). [S]

Direct Draw mode (Command 0 D[3:2] = 11) turns the colour and mask registers
into colour-key ranges and alpha controls: source key high 8290h, S_Alpha
8293h D0, destination key high 8294h, D_Alpha 8297h D0, source key low 8298h,
D_Rop 829Bh D[3:0], destination key low 829Ch (DS L2782-2856). The engine then
runs read-modify-write; S_Alpha/D_Alpha select source, destination or their
average (DS L383-387). [S]

## 6. 3D engine: vertices, primitive, status

### 6.1 Vertex registers (DS L4572, L4624-4762) [S]

Vertex A at 8800h, B at 8820h, C at 8840h; each 32 bytes:

| +off | Name | Format |
|---|---|---|
| +00h | TSFS | D[31:24] fog factor, D[23:16] specular R, D[15:8] specular G, D[7:0] specular B; 8-bit integers |
| +04h | TSZ | float |
| +08h | TSX | float |
| +0Ch | TSY | float |
| +10h | TSARGB | A, R, G, B, 8-bit integers, A in D[31:24] |
| +14h | TSU | float |
| +18h | TSV | float |
| +1Ch | TSW | float |

**Inference:** "float" is IEEE-754 single precision. DS says the setup engine
is a 32-bit floating-point engine "specially designed to fit all the data
formats in Microsoft Direct3D" (DS L412), which accepts D3D TLVERTEX values
unconverted. **Not found:** whether Z is expected in [0, 1] and whether W is
RHW (1/w) or w. D3D supplies RHW; the first textured triangle will show which.

### 6.2 Primitive setting, 89F8h (DS L4769-4845) [S]

| Bits | Field | Values |
|---|---|---|
| 20:18 | TSHMD shading | 001 flat via top (line: start), 010 flat via middle, 011 flat via bottom (line: end), 100 Gouraud |
| 17:16 | TTFROM | top / line start / point vertex: 00 a, 01 b, 10 c |
| 15:14 | TMFROM | middle vertex: 00 a, 01 b, 10 c |
| 13:12 | TBFROM | bottom / line end vertex |
| 11:8 | TSETFIRE | engine fires after the write of: 0 TFIRE, 1 TSARGBa, 2 TSWa, 3 TSARGBb, 4 TSWb, 5 TSARGBc, 6 TSWc, 7 TSVc |
| 7 | TDRAWDIR | triangle: 0 left-to-right, 1 right-to-left; line: 0 horizontal, 1 vertical |
| 2:0 | TDRAW | 000 point, 001 line, 010 triangle |

The driver sorts the vertices by Y and states the order in this register; the
hardware does not sort. **Not found:** how TDRAWDIR relates to the sorted
vertices (presumably the side the middle vertex lies on). Probe with one
triangle per orientation.

### 6.3 Fire and status, 89FCh (DS L4847-4863) [S]

Write anything to fire (TFIRE). Read:

| Bits | Meaning |
|---|---|
| 27:16 | Free 3D queue space, in units of 8 bytes |
| 1 | T3IDLEQE: engine idle and queue empty |
| 0 | T3IDLE: engine idle |

TEND at 8AFFh is a dummy byte register marking the end of a primitive list
(DS L5772-5777). **Not found:** when it is required.

## 7. 3D state registers (DS L4577-4623, L4865-5134) [S]

| Offset | Name | Fields |
|---|---|---|
| 8A00h | Enable | D21 Z write, D20 Z test, D18 alpha write, D17 alpha test, D16 alpha buffer, D14 stipple, D13 stipple alpha, D12 line pattern, D11 primitive setup, D10 texture, D9 perspective, D8 texture transparency, D7 texture cache, D5 large cache, D4 specular, D3 fog, D2 blend, D1 transparency, D0 dither |
| 8A04h | Z set | D[21:20] format 00 Z8 / 01 Z16; D[18:16] test: 0 never, 1 <, 2 =, 3 ≤, 4 >, 5 ≠, 6 ≥, 7 always; D[13:0] pitch |
| 8A08h | Z base | D[22:0] in local memory, D[31:0] in system memory |
| 8A0Ch | Alpha set | D[29:28] format (11 = A8); D[26:24] test, as Z; D[23:16] reference; D[11:0] pitch |
| 8A10h | Alpha base | as Z base |
| 8A14h | Destination set | D[27:24] ROP2; D22 BGR order; D[21:20] 00 8 bpp, 01 16 bpp, 10 24 bpp, 11 32 bpp; D[19:16] sub-format; D[13:0] pitch |
| 8A18h | Destination base | as Z base |
| 8A1Ch | Line pattern | D[31:16] pattern, D[15:0] repeat factor |
| 8A20h | Fog | D24 mode 0 constant / 1 normal; D[23:0] fog colour RGB |
| 8A24h | Transparency low | D[23:0] RGB |
| 8A28h | Blend | D[31:28] destination factor; D[27:24] source factor; D[23:0] transparency high RGB |
| 8A30h | Clip top/bottom | D[25:13] top, D[12:0] bottom; s12 sign-magnitude |
| 8A34h | Clip left/right | D[25:13] left, D[12:0] right; s12 |

The ≤/≠/≥ glyphs in the Z and alpha test tables are Symbol-font fields that
the text extract dropped (DS L4945-4948); the RTF has SYMBOL 163, 185 and 179
there, i.e. ≤, ≠, ≥. The order matches REF §23.

16 bpp destination sub-formats (D22 = 0): 0000 RGB555, 0001 RGB565, 0010
ARGB1555, 0011 ARGB4444. 32 bpp: 0000-0010 ARGB1888/2888/4888, 0011 ARGB8888,
0100 RGB0888 (DS L5014-5020). The Win16 display modes are 8, 15, 16 and 24 bpp,
so the useful D3D render targets are RGB555, RGB565 and RGB888. **Not found:**
the 24 bpp sub-format code; the table lists only 16 and 32 bpp.

Blend factors (DS L5067-5111). Destination: 0 ZERO, 1 ONE, 2 SRC_COLOR, 3
INV_SRC_COLOR, 4 SRC_ALPHA, 5 INV_SRC_ALPHA, 6 DST_ALPHA, 7 INV_DST_ALPHA.
Source: 0 ZERO, 1 ONE, 4 SRC_ALPHA, 5 INV_SRC_ALPHA, 6 DST_ALPHA, 7
INV_DST_ALPHA, 8 DST_COLOR, 9 INV_DST_COLOR, A SRC_ALPHA_SAT, B BOTH_SRC_ALPHA,
C BOTH_INV_SRC_ALPHA. DST_ALPHA needs the separate A8 alpha buffer; the colour
buffer holds no alpha at 16 bpp RGB565.

Local addresses are 23 bits in the 3D registers, 22 bits in the 2D engine;
with at most 4 MiB fitted, bit 22 is always 0 on Rev. Ax/Bx. [S, inference]

## 8. Texture registers (DS L4596-4623, L5135-5766) [S]

**8A38h texture set:**

| Bits | Field |
|---|---|
| 31:24 | Texel format (8.1) |
| 23:16 | Mapping: D16/D17 wrap U/V, D18/D19 mirror U/V, D20/D21 clamp U/V (priority wrap > mirror > clamp), D22 border colour for smoothing, D23 border colour outside the texture |
| 15 | YUV Cu/Cv signed |
| 14:12 | Texture blend mask bit: bit *n* of Atex used by the masked blend modes |
| 11:8 | Levels: 0 single texture; 1-9 MIP levels, ≤ max(log2 W, log2 H) |
| 5 | Texture in system memory |
| 4 | Clear texture cache |
| 3 | Magnification filter: 0 nearest, 1 linear |
| 2:0 | Minification: 0 nearest, 1 linear, 2 N-MIP-N, 3 N-MIP-L, 4 L-MIP-N, 5 L-MIP-L |

**8A3Ch texture blend:** D[31:26] colour mode, D[25:24] alpha mode (00 Atex
for ARGB/AL, 01 Apix, 10 Apix·Atex), D[23:0] transparency low RGB (DS
L5323-5415). Modes 00 0000 (Ctex), 00 0001 (Cpix) and 00 0100 ((1-Atex)·Cpix
+ Atex·Ctex, D3D DECALALPHA) are unambiguous. Modes 0010/0011, 0110/0111,
1000/1001 and 1100/1101 print identically in pairs, in the source RTF as well
as the extract (DS L5325-5343): the operator that tells each pair apart is
missing from the datasheet. **Inference:** 0010 is MODULATE (Cpix·Ctex).
**Not found:** what the odd member of each pair does; probe it before mapping
D3D texture-blend modes beyond those three.

**8A40h**: transparency high RGB. **8A44h-8A68h**: base addresses for levels
0-9, 23-bit local / 32-bit system. **8A6Ch-8A7Ch**: pitches, two levels per
register, even level in D[26:16], odd level in D[10:0], 11 bits each.
**8A80h**: D[31:28] log2 width, D[27:24] log2 height (0-9, so 1-512 texels),
D[23:0] mix-mode colour base. **8A84h/8A88h**: mix-mode colours 0/1.
**8A8Ch**: luminance colour Cr. **8A90h**: border ARGB. **8A94h-8AD3h**: 16
palette entries for index textures, **B in D[23:16], R in D[7:0]** (DS
L5639-5766). **8B00h-8B7Fh**: 32×32 stipple pattern.

**Not found:** the unit of the texture pitch (bytes would cap a level at 2047
bytes, which is less than 512 texels × 4 bytes).

### 8.1 Texel formats (D[31:24] of 8A38h, DS L5140-5266)

D31 selects BGR order; D[30:28] the class; D[27:24] the variant.

| Code | Format | Code | Format |
|---|---|---|---|
| 00h/01h/02h | Index 1/2/4 bpp | 40h-43h | RGB332, RGB233, RGB232, ARGB1232 |
| 10h | M4 | 50h | RGB555 |
| 16h | AM44 | 51h | RGB565 |
| 20h-23h | YUV422, YVU422, UVY422, VUY422 | 52h | ARGB1555 |
| 30h-33h | L1, L2, L4, L8 | 53h | ARGB4444 |
| 35h | AL22 | 57h | ARGB8332 |
| 38h | AL44 | 5Bh | ARGB8233 |
| 3Ch | AL88 | 5Fh | ARGB8232 |
| 63h | ARGB8565 | 73h | ARGB8888 |
| 67h | ARGB8555 | 74h | ARGB0888 |
| 68h | RGB888 | | |

The D3D texture formats that matter (RGB565, ARGB1555, ARGB4444, RGB555,
ARGB8888) all have codes.

## 9. Engine enables and queue

Order at enable time, all [S] except where marked:

1. SR5 = 86h; verify the read-back is A1h.
2. SRB D[6:5] = 11, so BAR1 decodes MMIO (DS L1774).
3. SR27 D6 = 1, engine register programming (DS L2102). SR27 D7 = 0: Turbo
   Queue off for bring-up, matching REF §8.3's Phase 1.
4. SR39 D2 = 1 before any 3D register access (DS L2397).
5. The hardware queue holds 42 dwords (DS L397). Poll 82A8h D[4:0] for free
   entries, or 82ABh D6 for idle.

The Turbo Queue is the last 32 KiB of off-screen memory at SR2C, split
between 2D and 3D by SR3C D[1:0] (DS L397-399, L2438-2442). It stays off until
the synchronous path is proven; REF §8.3 reports that Xorg found the
documented queue-free indication unreliable with it on.

## 10. Video overlay and TV-out

The video accelerator registers sit at CR80h-CRBDh behind CR80h = 86h, and
TV-out is reached through CRE0h/CRE1h (DS L551, L2951). Out of scope for
bring-up; listed so that a register dump of CR80h+ is not mistaken for CRTC
state.

## 11. Disagreements with REF

| REF | REF says | DS says | Resolution |
|---|---|---|---|
| §5.3 | Pitches are 16-bit fields | 12-bit (DS L2483-2490) | Mask to 12 bits |
| §13.1 | SR0C code 3 reserved; bit 4 part of size | D[2:1] = 11 is 1 MiB/2 banks; D4 reserved (DS L1803-1811) | Probe; log SRC raw |
| §14.1 | SR0E[1:0] is DRAM type | Reserved (DS L1859) | Use SR23 D5, SR33 D3/D0 |
| §18 | TSFS semantics unknown | Fog factor + specular RGB (DS L4629) | DS |
| §24 | Alpha pitch mask 3FFh | 12 bits (DS L4986) | DS |
| §26 | `MASK_SrcBlendMode` = F0000000h | D[31:28] is the destination factor (DS L5067) | REF's values are right, its mask names are swapped |
| §28 | ARGB8332/8233/8232 = 54h/55h/56h | 57h/5Bh/5Fh (DS L5212-5217) | Disputed; probe before exposing these formats |
| §29 | `MASK_TextureBlend` = 0F000000h; `TB_A_AF` = `TB_A_AFAS` | Colour D[31:26], alpha D[25:24]; 01 Apix, 10 Apix·Atex (DS L5406-5412) | DS |
| §32 | Odd-level pitch 10 bits | 11 bits (DS L5534) | DS |
| §33 | 512 maximum "B/C" | 1-512 stated (DS L5580-5603) | Settled |
| §35, §36, §43.2-5 | Formats unknown | Specified (sections 6-7) | Settled except as marked |
| §41 | 8 MiB addressing open | Rev. Ax/Bx maximum 4 MiB (DS L73) | Settled for Ax/Bx |
