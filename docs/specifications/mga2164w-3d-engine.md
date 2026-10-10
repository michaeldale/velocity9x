# MGA-2164W Developer's Specification: 3D facts for the D3D HAL

Source: the Matrox MGA-2164W Developer's Specification, doc 10568-XX-0100,
18 Aug 1997, 355 PDF pages, as published at
<https://vintage3d.org/doc/matrox/2164spec.pdf> and on bitsavers
(`components/matrox/_dataSheets/MGA-2164w_199708.pdf`). Not in this tree.
Citations are printed page numbers with the PDF page in brackets, e.g. "3-70 [102]".
Extracted 2026-10-10 with `pdftotext`, figures read from rendered pages.
Nothing in this file has been measured on a 2164W; section 10 lists the
emulator's hypotheses for what the document leaves out.

## 0. Summary: the texture engine has been removed from this document too

The 2164W spec is redacted the same way as the 1064SG spec. **No texture register, no texture
opcode, no textured-trapezoid section, no texel format, and no bus-master/primary-DMA register
appears anywhere in it.** Items 2, 3, 4 and 8 of the request cannot be answered from this
document. The evidence that material was cut, not absent from the silicon:

| Trace | Where |
|---|---|
| The feature list has five orphaned sub-bullets ("Perspective correct", "Monochrome and true color lighting", "Decal", "Texture wrapping and clamping", "16-bit true color or 8- or 4-bit palletized"). Their parent bullet (presumably "texture mapping") has been deleted. Rendered page checked. | 1-5 [15] |
| The block diagram has a "Texture Mapper" block. | Fig 1-2, 1-7 [17] |
| "Matrox Fast Texture Architecture ... texture compression model saves on memory usage." | 1-2 [12] |
| Section 4.5.5 introduction says it covers "constant and Gouraud shaded, patterned, and textured trapezoids". The subsections are 4.5.5.1 to 4.5.5.5 (slope, constant, patterned, Gouraud, host data). None is textured. | 4-34 [234] |
| DWGCTL opcod table: `0110` is not listed at all, not even as Reserved. Only `1011` is marked Reserved. | 3-55 [87] |
| Register map Part 7 jumps from CACHEFLUSH 1FFFh to "2C38h-2C4Ch Reserved". 2C00h-2C34h is not listed at all, not even as Reserved, although Table 2-3 declares 2C00h-2DFFh as DWGREG1. | 2-11 [31] |
| Register map Part 3: OPMODE 1E54h is followed by "1E60h-1E7Fh Reserved". 1E58h-1E5Fh is not listed. | 2-7 [27] |
| MACCESS.tlutload (bit 29): "Texture LUT load". This is the only texture-related field left in the document. | 3-70 [102] |
| 5.2 says "The MGA-2164W-PCI can also act as a master on the PCI bus - refer to Section 4.1.9". Section 4.1 ends at 4.1.8 Host Pixel Format; there is no 4.1.9. | 5-2 [278]; contents [4] |
| DEVCTRL: bit 2 (PCI bus-master enable) is neither described nor listed as reserved (reserved list is "<20:9> <24>"). memwrien (bit 4), rectargab (bit 28) and recmastab (bit 29) are described in bus-master terms. | 3-8/3-9 [40-41] |

Not from this document (memory, unverified, mark as such in any C header): XFree86's `mga_reg.h`
places TMR0-TMR8 at 2C00h-2C20h, TEXORG 2C24h, TEXWIDTH 2C28h, TEXHEIGHT 2C2Ch, TEXCTL 2C30h,
TEXTRANS 2C34h, TEXTRANSHIGH 2C38h, TEXCTL2 2C3Ch, PRIMADDRESS 1E58h, PRIMEND 1E5Ch, and uses
`MGADWG_TEXTURE_TRAP = 0x06`. These fit the holes listed above exactly. TEXFILTER (2C58h) and
TEXBORDERCOL (2C5Ch) are G200 registers. This document lists 2C58h/2C5Ch as Reserved, so treat
them as absent on the 2164W. Every bit-field layout for the texture registers still has to come
from another source (XFree86/Mesa `mga` code, a Matrox SDK, or a probe on the A8U4I5 card). This
document cannot supply them.

## 1. BARs and control aperture

### Configuration space (Table 2-1, 2-2 [22]; descriptions 3-15..3-17 [47-49])

The layout extraction of Table 2-1 is shifted by one row. The raw extraction and the per-register
pages agree:

| Config offset | Name | Meaning | Field |
|---|---|---|---|
| 10h (BAR0) | **MGABASE2** | Frame buffer aperture, 16 MB, **prefetchable** (bit 3 = 1, reset value ...1000b) | mgabase2 <31:24> |
| 14h (BAR1) | **MGABASE1** | Control aperture, 16 KB, not prefetchable | mgabase1 <31:14> |
| 18h (BAR2) | **MGABASE3** | 8 MB ILOAD / pseudo-DMA window, not prefetchable | mgabase3 <31:23> |

**This differs from the 2064W/1064SG.** On the 2164W, BAR0 is the frame buffer and BAR1 the
control aperture. The 2164W doc states the order on 2-2 [22] and on 3-15/3-16 [47-48]. The
2064W/1064SG order is from my knowledge of those parts, not from this document.

- DEVID: device 051Bh for -PCI, 051Fh for -AGP (3-10 [42]).
- If the apertures overlap, precedence is BIOS EPROM > control aperture > pseudo-DMA window >
  VGA FB > MGA FB (3-15 [47]).
- MGA_INDEX (44h, index <13:2>) and MGA_DATA (48h) give config-space access to the control
  aperture. They "cannot be used in Pseudo-DMA mode" (3-13/3-14 [45-46]).
- OPTION 40h: powerpc <31> byte-swaps MGABASE1+1C00h..1EFFh **and +2C00h..2DFFh**, which
  confirms DWGREG1 is a register range (3-19 [51]). Other fields: vgaioen <8>,
  memconfig <13:12> (00 = 32-bit DAC single / 64-bit split; 01 = 64-bit single / 128-bit
  split; 10 = 128-bit single; 11 reserved), rfhcnt <19:16>, eepromwt <20>, nogscale <21>,
  productid RO <28:24>, noretry <29>, biosen <30>. Reserved <23:22><15:14><11:9><7:0>
  (3-18/3-19 [50-51]).

### Control aperture (Table 2-3, 2-4 [24]; the layout extraction is garbled, use the raw form)

| MGABASE1 + | Attr | Name |
|---|---|---|
| 0000h-1BFFh | W / R | DMAWIN 7 KB pseudo-DMA window (ILOAD write / IDUMP read) |
| 1C00h-1DFFh | W | DWGREG0, first set of drawing registers (reads not decoded) |
| 1E00h-1EFFh | R/W | HSTREG host registers |
| 1F00h-1FFFh | R/W | VGAREG |
| 2000h-2BFFh | - | Reserved |
| **2C00h-2DFFh** | **W** | **DWGREG1, second set of drawing registers** (reads not decoded) |
| 2E00h-3BFFh | - | Reserved |
| 3C00h-3C1Fh | R/W | DAC |
| 3C20h-3DFFh | - | Reserved |
| 3E00h-3FFFh | R/W | EXPDEV expansion |

### Documented DWGREG1 content (2-11 [31], 3-39..3-41 [71-73])

| Offset | Name |
|---|---|
| 2C00h-2C34h | *not listed (redacted)* |
| 2C38h-2C4Ch | Reserved |
| 2C50h / 2C54h | DR0_Z32 LSB / MSB |
| 2C58h, 2C5Ch | Reserved |
| 2C60h / 2C64h | DR2_Z32 LSB / MSB |
| 2C68h / 2C6Ch | DR3_Z32 LSB / MSB |

All are WO and FIFO. DR0_Z32 is DYNAMIC; DR2_Z32 and DR3_Z32 are also marked DYNAMIC on their
pages.

### GO alias

- Register map footnote (6): "1D00h-1DFFh: Same mapping as 1C00h-1CFCh. Accessing a register in
  this range instructs the drawing engine to start a drawing operation" (2-7 [27], 2-11 [31]).
  This matches the known +100h alias.
- For DWGREG1, the map does not list a 2D00h alias. Two places imply one:
  - Table 2-3 sizes DWGREG1 as 2C00h-2DFFh, the same 512 bytes as DWGREG0 including its alias.
  - ILOAD Step 2 says "The last register you program must be accessed in the 1D00h-1DFFh or
    **2000h-2DFFh** range in order to start the drawing engine" (4-49 [249]).
  "2000h" is almost certainly a typo for 2D00h. **Ambiguous: assume 2C00h+100h = GO alias for
  DWGREG1, and verify on hardware.**
- Section 4.5.3: "It is recommended that the last register programmed be in the range 1d00h to
  1DBFh" for future-product compatibility (4-27 [227]). Every programming section repeats that the
  last register must be in 1D00h-1DFFh (4-27, 4-34, 4-42 [227, 234, 242]).
- Only dword accesses may be used to initialise the drawing engine, directly or by pseudo-DMA
  (4-25 [225]).

### Host registers (unchanged from the facts already known, details for the header)

- **FIFOSTATUS 1E10h** (3-63 [95]): fifocount <5:0>, bfull <8>, bempty <9>. Reset value
  0x00000240. **Ambiguity:** the text says bempty "is identical to fifocount<6>" and that the
  FIFO count resets to 64. 64 does not fit in <5:0>, and reset 0x240 has bit 6 set. So fifocount
  is effectively <6:0>, and the field table's <5:0> is wrong. The BFIFO has 64 entries (4-2
  [202]). Polling is not required because the chip retries the PCI write when the FIFO is full.
  Some chipsets error after a few retries, though, and then "you must check the BFIFO flag"
  (4-3 [203]).
- **STATUS 1E14h** (3-81 [113]): pickpen <2>, vsyncsts <3>, vsyncpen <4>, vlinepen <5>,
  extpen <6>, dwgengsts <16>. dwgengsts stays busy until the BFIFO is empty, the engine has
  finished, and the memory controller has finished its last access.
- ICLEAR 1E18h: pickiclr <2>, vlineiclr <5>. IEN 1E1Ch: pickien <2>, vlineien <5>,
  extien <6> (3-67/3-68 [99-100]).
- VCOUNT 1E20h <11:0>; use word or dword reads (3-82 [114]).
- DMAMAP30/74/B8/FC 1E30h-1E3Ch, RST 1E40h (softreset <0>, hold at least 10 us),
  OPMODE 1E54h, DWG_INDIR_WT0-15 1E80h-1EBCh (2-7 [27]). See section 8.

## 2. Texture-mapped trapezoid

**Not documented.** The DWGCTL opcod table (3-55 [87]) lists only:

| Value | Mnemonic |
|---|---|
| 0000 | LINE_OPEN |
| 0001 | AUTOLINE_OPEN |
| 0010 | LINE_CLOSE |
| 0011 | AUTOLINE_CLOSE |
| 0100 | TRAP |
| 0101 | TRAP_ILOAD |
| 0111 | ILOAD_HIQH |
| 1000 | BITBLT |
| 1001 | ILOAD |
| 1010 | IDUMP |
| 1011 | Reserved |
| 1100 | FBITBLIT |
| 1101 | ILOAD_SCALE |
| 1110 | ILOAD_HIQHV |
| 1111 | ILOAD_FILTER |

`0110` is missing from the table, as noted in section 0. Section 4.5.5 has no textured
subsection (4-34..4-41 [234-241]).

The documented primitive closest to texturing is **TRAP_ILOAD** (opcod 0101, 4.5.5.5, 4-41
[241]). It is a Gouraud-style trapezoid whose pixel colours come from the host through the ILOAD
path, while the hardware still does Z compare and write and the trapezoid edge walking. It could
be a software-texel fallback with hardware Z. Its requirements:

- bltmod must be BU32BGR, BU32RGB, BU24BGR or BU24RGB.
- atype must be ZI or I.
- pwidth must not be PW24.
- OPMODE must select DMA BLIT Write.
- The host must send **exactly** the expected number of pixels. Fewer deadlocks the engine; extra
  pixels are parsed as register writes.
- DWGCTL pattern from the figure (bit 31 to bit 0):
  `0 0 0 ++++ 0 #### 1100 0 1 0 0 0 ### 0 +++ 0101`
  - bltmod <28:25> as above
  - trans <23:20> free
  - bop = 1100
  - shftzero = 1, sgnzero = 0, arzero = 0, solid = 0
  - zmode free
  - linear = 0
  - atype ZI/I
  - The RECT variant sets sgnzero = 1 and arzero = 1.
- Registers: DR0/DR2/DR3 Z (or their _Z32 forms) "only if zmode <> NOZCMP or atype = ZI", plus
  FCOL alpha.
- DR4 through DR15 are "not used, and will be corrupted" for TRAP_ILOAD (3-45..3-53 [77-85]).
- Untested idea, not a documented recommendation.

## 3. Texture registers

**None documented.** TMR0-TMR8, TEXORG, TEXWIDTH, TEXHEIGHT, TEXCTL, TEXCTL2, TEXTRANS,
TEXTRANSHIGH, TEXFILTER, TEXBORDERCOL and any palette/LUT data registers are absent from the
register map (2-5..2-11 [25-31]), from chapter 3, and from the field index (pages 3xx, [335-355];
a full list of field names was checked).

The only texture fact in the document:

- **MACCESS.tlutload <29>** (MACCESS 1C04h, 3-70 [102]): "Texture LUT load. When this bit is set
  to `1' during an ILOAD or BITBLT operation, the destination becomes the texture LUT rather than
  the frame buffer."
- Nothing says how the LUT is indexed by LEN, YDST or the X registers, how many entries it has
  (4-bit vs 8-bit palettes per the 1-5 bullet), or what the entry format is. Neither the ILOAD
  section (4-49..4-53 [249-253]) nor the BITBLT section mentions tlutload. **Unknown:** LUT size,
  entry format (presumably 16-bit, given "16-bit true color or 8- or 4-bit palletized"), and
  addressing. Must come from another source or a probe.

## 4. Texture capabilities

The document says only this (1-5 [15], sub-bullets of the deleted parent):

- perspective correct
- "monochrome and true color lighting"
- decal
- texture wrapping and clamping
- "16-bit true color or 8- or 4-bit palletized" texels

It does not give encodings, maximum size, power-of-two rules, TEXORG alignment, per-axis
wrap/clamp bits, filter modes, mipmapping, colour key, how lighting is combined with texels,
texel alpha, or whether textures must be local. **Nothing here supports or rules out bilinear
filtering or mipmapping.** Treat both as unknown, not absent.

## 5. Z buffer

- **MACCESS 1C04h** (3-70 [102]), WO FIFO STATIC, reset 0. For C constants:

| Bits | Field | Values |
|---|---|---|
| <1:0> | pwidth | 00 PW8, 01 PW16, 10 PW32, **11 PW24** |
| <3> | **zwidth** | 0 ZW16 (16-bit Z), **1 ZW32 (32-bit Z)** |
| <15> | memreset | keep 0 except in the power-up sequence of 4.3.3 |
| <29> | tlutload | see section 3 |
| <30> | nodither | 0 = dither unformatted ILOAD, ZI and I trapezoids; 1 = no dither |
| <31> | dit555 | 0 = 5:6:5, 1 = 5:5:5 (16 bpp only; affects dithering and shading) |

  Reserved: <2>, <14:4>, <28:16>.
  - **Difference from the 1064SG facts:** the 2164W has 32-bit Z (zwidth) and the 48-bit
    DRn_Z32 registers. The 1064SG facts you have list only 17.15 DR0/2/3.
  - Global init says MACCESS sets "Z precision (16 or 32 bits)" (4-27 [227]).
- **ZORG 1C0Ch** (3-91 [123]), WO FIFO STATIC:
  - zorg <23:0> is a **byte** address. The nine LSBs must be 0 (**512-byte alignment**). Bits
    <31:24> are ignored. It must not overlap the intensity (colour) buffer.
  - "Setting ZORG to 200000h or 400000h will yield the fastest performance for primitives using
    the Z buffer." These are 2 MB bank boundaries (Fig 4-1, 4-16 [216]): Z in a different WRAM
    bank from the colour buffer.
  - Equation table: `zorg = Z depth origin - ydstorg * 2` when zwidth = 0, and
    `zorg = Z depth origin - ydstorg * 4` when zwidth = 1.
- **Z pitch:** not stated in words. The ZORG equation implies the Z address is
  `zorg + linear_pixel_address * (2 or 4)`, where the linear pixel address already includes
  YDSTORG and is formed with PITCH. So the Z buffer uses the colour buffer's PITCH, at 2 or 4
  bytes per pixel. This is an inference; verify with a probe.
- **Value format:**
  - 16-bit Z: DR0 is signed 17.15 two's complement for TRAP and LINE (3-42 [74]).
  - 32-bit Z: DR0_Z32 <47:0> is signed **33.15**, split as LSB at 2C50h and MSB at 2C54h; bits
    <63:48> are ignored (3-39 [71]). DR2_Z32 (2C60h/2C64h) and DR3_Z32 (2C68h/2C6Ch) are the same
    (3-40, 3-41 [72-73]).
  - Aliasing: "Bits 31 to 16 of DR0 map to bits 15 to 0 of DR0_32MSB; bits 15 to 0 of DR0 map to
    bits 31 to 16 of DR0_32MSB [sic]. Writing to this register clears bits 15 to 0 of
    DR0_32LSB" (3-42 [74]). The DR2 and DR3 pages say "...map to bits 31 to 16 of DR2_32**LSB**"
    (3-43, 3-44 [75-76]), so the DR0 wording "MSB" is a typo for LSB.
  - Net effect: a 17.15 write to DR0 equals the 48-bit 33.15 value `DR0 << 16`. A 16-bit Z setup
    produces the top 16 integer bits of a 32-bit Z. In ZW16 the stored value is presumably the
    16 integer bits of the 17.15 accumulator (bit 16 being sign/overflow). That is an inference;
    the document does not state the stored range or any clamping.
  - D3D mapping: scale z in [0,1] to 0..0xFFFF for ZW16, or 0..0xFFFFFFFF for ZW32. Unverified.
- **Compare modes:** zmode <10:8> = 000 NOZCMP (always), 001 Reserved, 010 ZE (=), 011 ZNE (<>),
  100 ZLT (<), 101 ZLTE (<=), 110 ZGT (>), 111 ZGTE (>=) (3-56 [88]). This matches the known
  facts.
- **atype:**
  - ZI (011) = depth mode with Gouraud; updates Z.
  - I (111) = Gouraud "with depth compare". Comparison follows zmode, but "the depth is never
    updated" (3-56 [88], footnote 2).
  - Footnote 1: BLK forces RPL.
- **Z write mask:** zmask is ZI vs I. PLNWT "is not used for z cycles" (3-75 [107]).
- **Memory requirement table** (Table 4-1, 4-14 [214]): Z16 and Z32 columns exist for 8, 16 and
  32 bpp. **24 bpp with Z is "-" (unsupported)**, which matches "pwidth must not be PW24" for
  Gouraud/ZI trapezoids and depth lines (4-32, 4-40, 4-41 [232, 240, 241]).

## 6. Blending, translucency, fog, dither

- **No alpha blending, fog or specular** registers or fields exist in the document.
- "Alpha" is only a value written into the unused bits of the pixel:
  - FCOL forcol<31:24> supplies alpha bits 31:24 in 32 bpp.
  - forcol<31> supplies bit 15 in 16 bpp 5:5:5 (3-62 [94]).
  - The Gouraud trapezoid and depth-line tables list "FCOL: Alpha value, only if pwidth = 32, or
    pwidth = 16 and dit555 = 1" (4-32, 4-40 [232, 240]).
- **Translucency = screen-door stipple:** DWGCTL.trans <23:20>, "realized by writing one of `n'
  pixels" (3-59 [91]).
  - The figure was rendered and decoded. Rows are listed top to bottom, columns left to right,
    in a 4x4 grid; X = written (opaque), . = skipped.
  - The document does not say which screen x/y the grid's row 0 / column 0 maps to. YDST.sellin
    <31:29> (three LSBs of y, loaded during linearisation) drives "dithering, patterning, and
    transparency", so the rows follow y mod 4 (3-87 [119]). x presumably follows x mod 4.
    Unverified.

| trans | rows (top to bottom) | coverage |
|---|---|---|
| 0000 | XXXX XXXX XXXX XXXX | 16/16 |
| 0001 | X.X. .X.X X.X. .X.X | 8/16 |
| 0010 | .X.X X.X. .X.X X.X. | 8/16 (complement of 0001) |
| 0011 | X.X. .... X.X. .... | 4/16 |
| 0100 | .X.X .... .X.X .... | 4/16 |
| 0101 | .... X.X. .... X.X. | 4/16 |
| 0110 | .... .X.X .... .X.X | 4/16 |
| 0111 | X... .... ..X. .... | 2/16 |
| 1000 | .... .X.. .... ...X | 2/16 |
| 1001 | ...X .... .X.. .... | 2/16 |
| 1010 | .... ..X. .... X... | 2/16 |
| 1011 | .... X... .... ..X. | 2/16 |
| 1100 | .X.. .... ...X .... | 2/16 |
| 1101 | .... ...X .... .X.. | 2/16 |
| 1110 | ..X. .... X... .... | 2/16 |
| 1111 | .... .... .... .... | 0/16 |

  - The four 4/16 patterns tile one 25% grid. The eight 2/16 patterns tile a 12.5% grid, but
    1000 and 1101, for example, are not complements. Pick fixed pairs for 50% (0001/0010) and
    rotate among same-coverage sets for lower opacity.
  - trans is allowed (`####`) in Gouraud ZI/I trapezoids and depth lines (4-32, 4-40 [232, 240]).
  - trans must be 0000 with atype BLK (4-36, 4-37 [236-237]).
- **Dithering:** MACCESS.nodither <30> and dit555 <31> as above. Dithering applies to
  "unformatted ILOAD, ZI, and I trapezoids" (3-70 [102]). The DPU lists "Dithering circuitry"
  (1-8 [18]).
- **Transparency colour key:** DWGCTL.transc <30> with FCOL as bltckey and BCOL as bltcmsk.
  It is documented for blits, linestyle vectors and patterned trapezoids only (3-61 [93]).
  It is not a texture colour key.

## 7. Interpolant setup and trapezoid conventions

### Gouraud trapezoid (4.5.5.4, 4-40 [240])

- atype must be ZI or I; PW24 is not allowed.
- DWGCTL pattern from the figure (bit 31 to bit 0):
  - TRAP: `0 0 0 0000 0 #### 1100 0 1 0 0 0 ### 0 +++ 0100`
    - transc = 0, pattern = 0, bltmod = 0000
    - trans free
    - bop = 1100 (bits 19:16; no other value shown)
    - shftzero = 1, sgnzero = 0, arzero = 0, solid = 0
    - zmode free
    - linear = 0
    - atype ZI/I
    - opcod = TRAP
  - RECT: the same, but sgnzero = 1 and arzero = 1.
- Register table:
  - DR0 (or DR0_Z32): "The z start position"
  - DR2: "The z increment for x"
  - DR3: "The z increment for y"
  - DR4/DR6/DR7: red start / inc x / inc y
  - DR8/DR10/DR11: green
  - DR12/DR14/DR15: blue
  - FCOL: alpha
  - Z registers only "if zmode <> NOZCMP or atype = ZI".
- DR4, DR8, DR12 are signed 9.15 in <23:0>; <31:24> must be 0 (3-45..3-53 [77-85]).

### Left edge vs dF/dy: not fully stated

- DR0 / DR4 / DR8 / DR12 are "used to scan the left edge of the trapezoid and must be initialized
  with its starting ... value" (3-42, 3-45, 3-48, 3-51 [74, 77, 80, 83]). These are DYNAMIC
  registers.
- The y-increment registers (DR3, DR7, DR11, DR15) hold "the ... increment value along the
  y-axis" (3-44, 3-47, 3-50, 3-53 [76, 79, 82, 85]).
- The x registers hold "the increment value along the x-axis".
- The document does not say whether the hardware adds the y increment once per scanline to the
  left-edge accumulator, or also adds the x increment times the left-edge x step. It also gives
  no worked numeric example.
- The wording ("scan the left edge", DYNAMIC start registers, one y increment) is consistent with
  **dF/dedge = dF/dy + (dx_left/dy)·dF/dx folded into DRy by software**, if the hardware does only
  `F_left += DRy` per line. It is equally consistent with a pure dF/dy if the hardware tracks the
  Bresenham x steps.
- **Ambiguous; settle with a probe.** Two candidate probes:
  - Draw a sheared trapezoid with dF/dy = 0 and dF/dx ≠ 0. If the colour at the left edge varies
    down the edge, the hardware does not compensate for the x steps.
  - Read the XFree86/Mesa mga setup code.
- For the depth line analogue, DR2 is the "major axis" increment and DR3 the "diagonal"
  increment (Bresenham steps), so the line engine works in Bresenham terms (4-32 [232]).

### Trapezoid edge registers (4-35 [235]; AR pages 3-23..3-29 [55-61])

| Register | Content |
|---|---|
| AR0 | dYl = yl_end - yl_start (18-bit signed) |
| AR1 | errl = (sdxl == XL_NEG) ? dXl + dYl - 1 : -dXl (24-bit signed) |
| AR2 | -\|dXl\| = -\|xl_end - xl_start\| |
| AR4 | errr = (sdxr == XR_NEG) ? dXr + dYr - 1 : -dXr (18-bit) |
| AR5 | -\|dXr\| |
| AR6 | dYr = yr_end - yr_start |
| SGN | edge directions |
| FXBNDRY | fxright <31:16> \| fxleft <15:0>, 16-bit signed |
| YDSTLEN | yval <31:16> \| length <15:0>; must use YDST + LEN when ylin = 1 |

- Edges are **integer Bresenham**. There are no fractional/subpixel edge registers. Subpixel
  correction must be folded into the error terms and into the start values of the interpolants
  by software.
- **SGN 1C58h** (3-77/3-78 [109-110]):
  - scanleft <0> must be 0 for TRAP. This bit is shared with sdydxl.
  - sdxl <1> is the left-edge x sign (1 = negative).
  - sdy <2> "should be programmed to zero for TRAP".
  - sdxr <5> is the right-edge x sign.
  - Reserved: <4:3>, <31:6>.
- **Fill convention** (4-34 [234], Fig 4-4):
  - Top and left edges are drawn; bottom and right edges are "just beyond the object's extents"
    (top-left rule).
  - A triangle is a trapezoid with one zero-length edge (FXRIGHT = FXLEFT at the apex).
  - Joined triangles can be drawn by "changing only one edge for each subsequent triangle", using
    the continuity points.
  - The document does not describe splitting a triangle into two trapezoids with gradients. The
    usual approach: draw the top half with LEN = y_mid - y_top, then reload only the edge that
    changes and LEN, and fire again. The interpolant accumulators presumably continue, because
    they are DYNAMIC. That is an inference.
- **XDST:** "For trapezoids with depth, this register is automatically loaded from fxleft. For
  trapezoids without depth, xdst will be loaded with the larger of fxleft or cxleft" (3-83 [115]).
  With Z/Gouraud, the left clip therefore does **not** advance the start x. Presumably the
  interpolants then stay correct and the clipper discards pixels.

### Limits

- x and y coordinates (FXLEFT, FXRIGHT, XDST, YDST xy form) are 16-bit signed (3-65, 3-66, 3-83,
  3-87 [97, 98, 115, 119]).
- ydst is 23 bits, sellin <31:29> (3-87 [119]).
- CXLEFT and CXRIGHT are 11-bit unsigned (0..2047) (3-32, 3-33 [64-65]). Negative x is always
  clipped. "There is no way to disable clipping."
- YTOP and YBOT are linearised 24-bit values (line × pitch + ydstorg) and must be multiples of 32
  (3-86, 3-90 [118, 122]).
- PITCH iy <11:0> is in pixels, a multiple of 32, at most 2048. Hardware linearisation supports
  only 512, 640, 768, 800, 832, 960, 1024, 1152, 1280, 1600, 1664, 1920 and 2048. Otherwise set
  ylin <15> and linearise in software: ydst = y × pitch >> 5 (3-74, 3-87 [106, 119]).
- YDSTORG <23:0> is in pixels, with an alignment that depends on pwidth and memconfig (block-mode
  restriction):
  - PW8: 64 / 128 / 256
  - PW16: 32 / 64 / 128
  - PW24: 64 / 128 / 256
  - PW32: 32 / 32 / 64
  for memconfig 00 / 01 / 10 (3-89 [121]).
- LEN length <15:0> unsigned; beta <31:28> is used only for ILOAD_HIQHV (3-69 [101]).
- DWGCTL bit 7 = **linear** (0 = xy blit, 1 = linear blit) (3-56 [88]). It is not in the 1064SG
  facts list you gave. It must be 0 for trapezoids. Reserved bits are <15>, <24>, <31> (3-61 [93]).
  Everything else matches your 1064SG DWGCTL facts exactly.
- SHIFT x_off/y_off are only for TRAP without depth. SRC0-3 are used internally for TRAP/LINE
  with depth (3-79, 3-80 [111-112]). Do not leave pattern state assumed across Z draws.

## 8. Bus mastering and DMA

- **No bus-master command-list registers are documented.** PRIMADDRESS/PRIMEND or equivalents are
  absent. The register map has an unlisted gap at 1E58h-1E5Fh, and the referenced section 4.1.9
  does not exist (see section 0).
- **AGP part** (3-4..3-6 [36-38]):
  - AGP_STS reports rate_cap = 00 (no AGP transfer modes), sba_cap = 0 and rq = 0.
  - AGP_CMD has data_rate <2:0>, agp_enable <8>, sba_enable <9>, rq_depth <31:24>.
  - 5.3: the -AGP part "does not use the AGP sideband signals nor the PIPE/ mechanism"; it runs as
    a 66 MHz PCI device (5-2 [278]).
- **Pseudo-DMA, general purpose** (4.5.1, 4-25/4-26 [225-226]): as you already have, with these
  2164W details.
  - Packet: dword 0 = indx3:indx2:indx1:indx0 (bytes 3..0), followed by 4 data dwords; repeat.
  - Each 8-bit index = address bit 13 (selects DWGREG1) and bits 8:2. So
    `index = ((addr >> 2) & 0x7F) | (addr in 2C00h-2DFFh ? 0x80 : 0)`.
    - Example: DR0_Z32LSB 2C50h → 0x94.
    - Example: the DWGCTL GO alias 1D00h → 0x40.
    - The 1064SG facts may predate the DWGREG1 bit; **this bit is new information for the
      2164W.**
  - Only FIFO-attribute registers can be written this way.
  - Entered by writing DMAWIN (MGABASE1+0000h-1BFFh) or MGABASE3.
  - Aborted by writing byte 0 of OPMODE, which "will terminate the current DMA sequence and
    initialize the machine for the new mode (even if the value did not change)". Use this to break
    an incomplete packet (3-71 [103], 4-25 [225]). DMAPAD 1C54h is the padding target (3-38 [70]).
  - The MGA_INDEX/MGA_DATA backdoor cannot be used in pseudo-DMA mode.
- **OPMODE 1E54h** (3-71/3-72 [103-104]):

| Bits | Field | Values |
|---|---|---|
| <3:2> | dmamod | 00 general-purpose write, 01 BLIT write (ILOAD/IDUMP data), 10 vector write, 11 reserved |
| <9:8> | dmadatasiz | 00 little-endian / big 8 bpp, 01 big 16 bpp, 10 big 32 bpp, 11 reserved |
| <17:16> | dirdatasiz | same encoding, for direct FB access |

  Reserved: <1:0>, <7:4>, <15:10>, <31:18>.
  - Use a 16-bit write so dirdatasiz is not disturbed (4-50 [250]).
  - **Ambiguity:** 3-71 says "Normally, dmadatasiz is `00' for any DMA mode except DMA BLIT
    WRITE". 4-8 [208] says general-purpose and vector DMA "should set dmaDataSiz to `10'". The
    second sits in the big-endian discussion; for x86, use 00.
- **Vector pseudo-DMA** (4.5.4.5, 4-33 [233]):
  - OPMODE = 0008h (LE) or 0208h (BE).
  - Then a tag dword V31..V0 followed by 32 dwords of Yn:Xn.
  - Tag 0 means XYSTRT without GO; tag 1 means XYEND with GO.
- **DWG_INDIR_WT0-15 at 1E80h-1EBCh with DMAMAP30/74/B8/FC at 1E30h-1E3Ch** (3-34..3-37,
  3-54 [66-69, 86]):
  - Each map byte is `(byte address >> 2) & 0x7F` for DWGREG0, or `| 0x80` for DWGREG1.
  - Writing DWG_INDIR_WT<N> writes the register named in map byte N, so non-contiguous registers
    can be filled with sequential stores. This is useful for burst writes of a fixed per-triangle
    register set.
  - The maps are R/W STATIC.
  - Same scheme as the pseudo-DMA index, including the DWGREG1 bit.
- **Bursting** (4-3 [203]):
  - Bursts are supported for writes to the drawing-register range and to host registers plus
    DRWI, and for reads and writes in DMAWIN and MGABASE3.
  - Host-register reads/writes and DAC accesses do not burst.
  - Bursts disconnect every 32 aligned dwords (5-2 [278]).

## 9. Restrictions, "musts", and 2064W vs 2164W

- **PW24 is incompatible with Z/Gouraud:** not allowed for depth lines, Gouraud trapezoids or
  TRAP_ILOAD (4-32, 4-40, 4-41 [232, 240, 241]), and the Z memory table has "-" for 24-bit
  (4-14 [214]).
- For atype = BLK:
  - Only RPL is performed.
  - bop must be 1100 and trans must be 0000.
  - BLK is not allowed when memconfig = 10 (4-36/4-37 [236-237]).
  - YDSTORG and PITCH alignment rules apply (3-74, 3-89 [106, 121]).
- With RPL, bop is limited to 0000, 0011, 1100 or 1111 (4-36 [236]).
- Do not write AR0-AR6 while arzero = 1, or SGN while sgnzero = 1. Write a DWGCTL with the bit
  cleared first (3-23..3-29, 3-77 [55-61, 109]).
- Do not write SRC or PAT while solid = 1 (3-57 [89]).
- TRAP needs scanleft = 0 and sdy = 0 (3-77 [109]).
- ZORG must be 512-byte aligned and must not overlap the colour buffer (3-91 [123]).
- Order hints (2-11 [31], footnotes 3 and 5):
  - Write DWGCTL, MACCESS and PLNWT after the other drawing registers to avoid pipe stalls.
  - Write address-processor registers (AR*, SGN, FX*, YDST, LEN, PITCH, ...) first, so they
    overlap the previous primitive.
- Start registers DR0/4/8/12, AR* and FX* are DYNAMIC: they change during a draw, so reload them
  for every primitive. Increments DR2/3/6/7/10/11/14/15 are STATIC (chapter 3 attribute lines).
- The BFIFO has 64 entries. Unbounded PCI retries can trip some chipsets (4-3 [203]).
- Soft reset: hold RST.softreset at 1 for at least 10 us. It flushes the BFIFO and aborts the
  current draw; memory is preserved (3-76 [108]).
- MACCESS.memreset must stay 0 outside the power-up sequence (3-70 [102]).
- **2064W vs 2164W:** the only comparison in the document is "Based on the current award-winning
  MGA-2064W core" (1-4 [14]) and "same acceleration core as the Matrox MGA-1064SG" (1-2 [12]).
  The document does not state that the 2064W lacks texture mapping, 32-bit Z or any other feature.
  What this document shows beyond the 1064SG facts in the request:
  - zwidth / 32-bit Z with DR*_Z32 (33.15)
  - the DWGREG1 range and its pseudo-DMA bit 7
  - DWGCTL.linear
  - the swapped BAR order
  - tlutload

  Whether the 2064W has any of these is outside this document.

## Ambiguities to settle on hardware (A8U4I5 2164W)

1. Whether 2D00h-2DFFh is a GO alias for DWGREG1. The text says "2000h-2DFFh" on 4-49.
2. Left-edge interpolation: whether DRy is pure dF/dy, or dF along the left edge.
3. Stored Z bits for ZW16 (17.15 integer part, low 16 bits?), and whether Z clamps or wraps.
4. The x/y origin of the trans stipple grid.
5. FIFOSTATUS fifocount width (<6:0> in practice).
6. Every texture, LUT and primary-DMA detail. Get them from XFree86/Mesa `mga` sources (G200-era
   headers with 2164W paths) and confirm each with a probe and a decision doc, per CLAUDE.md.

## 10. What 86Box assumes for the missing texture interface

86Box's `src/video/vid_mga.c` (upstream `f566f9c`, 2026-08-08, in
`build/upstream-86box`) models the 2164W and implements TEXTURE_TRAP. It is
reverse-engineered emulator code, not a Matrox source, and it does not
gate texturing by chip: its 2064W would texture too, which the real 2064W
is not believed to do. Every line below is a hypothesis for the probe,
nothing more.

- **Opcode:** TEXTURE_TRAP = `0110` (`DWGCTRL_OPCODE_TEXTURE_TRAP`), with
  atype ZI or I only (any other is fatal in the model).
- **Registers:** TMR0-TMR8 at 2C00h-2C20h, TEXORG 2C24h, TEXWIDTH 2C28h,
  TEXHEIGHT 2C2Ch, TEXCTL 2C30h, TEXTRANS 2C34h - the same offsets as the
  XFree86 recollection in section 0.
- **TMR meaning:** TMR6/7/8 are s, t, q at the left edge. TMR0, TMR2, TMR4
  are added per pixel (d/dx); TMR1, TMR3, TMR5 per scanline (d/dy), and
  the left edge's x step is folded in as dx times the x gradient - the same
  rule the model applies to DR0-DR15. That answers ambiguity 2 above only
  for the model.
- **Perspective:** with TEXCTL.npcen (bit 21) set, s and t are linear,
  scaled by 2^(20 - log2 size). Clear, the model divides s and t by q, q
  taken as 16.16.
- **TEXWIDTH/TEXHEIGHT:** log2 size in <5:0>, wrap mask (size - 1) in
  <28:18>.
- **TEXCTL:** texformat <2:0> (TW4 0, TW8 1, TW15 2, TW16 3, TW12 4 =
  ARGB4444), palsel <7:4> (TW4 palette bank), tpitch <18:16> (pitch =
  8 << tpitch texels), npcen 21, azeroextend 23, decalckey 24, takey 25,
  tamask 26, clampv 27, clampu 28, tmodulate 29, strans 30, itrans 31.
  Bits 8-20 (tpitchlin, tpitchext) are G100+ in the model.
- **Combine:** decal (texel replaces), modulate (texel x Gouraud colour,
  `>> 8`), colour key on texels (TEXTRANS tckey <15:0>, tkmask <31:16>),
  with the TEXCTL strans/itrans/decalckey combinations selecting skip or
  substitute. Texel alpha below 255 stipples through a Bayer matrix, not a
  blend.
- **Filtering:** none on the 2164W in the model (TEXFILTER is G100+).
- **Z:** stored as the 17.15 value's integer part, negative clamped to 0;
  32-bit Z from the 33.15 DRn_Z32 pair when MACCESS.zwidth is set; Z
  address = zorg + linear pixel offset x 2 or x 4.
