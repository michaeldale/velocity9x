# VSA-100 (Napalm) register fact sheet

Date: 2026-10-11

Status: desk extraction for Phase 0 item 2 of
[the VSA-100 plan](../plans/3dfx-vsa100-family.md). Nothing here was
measured by Velocity9x. Measured values come from VelocityNT's Mini-Me v2
(single VSA-100, 64 MiB, PCI, Windows 2000) and are marked with their
record. For the retail Voodoo4 4500 (32 MiB) every value below is a
hypothesis until Phase 1 measures it; board-level values (BAR sizes, memory
size, tile boundary, straps) are expected to differ.

## Sources

| Key | Source | Licence / use |
|---|---|---|
| **Napalm** | *Napalm Graphics Engine*, rev 1.13, 6 Oct 1999, `C:\everything\claude\personal\3dfx driver research\sux6000-docs\voodoo45-spec.pdf`, SHA-256 `06b5041d…5315e5e`, 249 pages | "3dfx Confidential". The databook for VSA-100 register writes (plan, 2026-10-11). Facts only; no text or tables copied. |
| **Glide** | Open Glide h5, `…\artifacts\3dfx_glide\h5\incsrc\`: `h3regs.h` (SHA-256 `c4c702cf…c56bd`), `h3defs.h` (`fb1da053…41aa0`), `h3gdefs.h` (`b901ed43…dcd229`) | 3DFX GLIDE Source Code General Public License. Cross-check of layouts only; no code. Cited `file:line`. |
| **VNT** | VelocityNT `docs\reference\m5*.md`, `docs\decisions\2026-10-1*-mini-me-*.md` | The author's own driver. Measured values and register semantics only; their citations into other trees were not followed. Cited by record name. |
| **Avenger** | **Read against this sheet 2026-10-18.** From bitsavers `components/3dfx/`, in `C:\everything\claude\personal\3dfx driver research\`: *Avenger (a.k.a. Voodoo3) High Performance Graphics Engine* `Voodoo3_Spec_r1.0_199911.pdf` (SHA-256 `f1d5d1b6…68790d5`); *Banshee High Performance Graphics Engine* `Voodoo2_Banshee_Spec_r1.1_199811.pdf` (`4857703a…40b2037`); *Voodoo Banshee Universal Access 2D Databook* `Voodoo2_Banshee-2D_Databook_r1.0_199806.pdf` (`f24f485d…9bca16d`); `Voodoo3_Data_Book_r1.0_199908.pdf` (`4a978226…d9eb3ea`); `Voodoo3_Programming_Guide_r1.4a_199906.pdf` (`d2a51f3f…3db8375`). Also a public *Napalm* databook r1.12, `Voodoo45_Napalm_Databook_r1.12_200003.pdf` (`b0d0eb15…f6d83f7`), a pin and electrical book whose only register content is the strap tables. Cited "Avenger §x.y p.N" with the body heading and printed page (which equals the PDF page in this edition); the Avenger spec's tables extract with shifted columns like Napalm's, so each value was read in row order from the per-register page. | Several are watermarked confidential, as Napalm is: facts only. The Banshee documents were not needed: every Avenger note below is settled by the Avenger spec. |

Not opened: `C:\everything\3dfx-driver` (standing rule), and
`C:\everything\retro-3dfx\3dfx Driver Code\`, which the file search showed
to be a second copy of the same leaked H5 tree (it contains
`H5\DOCS\avenger_spec.doc`; that file is part of the leak, not a public
spec, and was not read).

### Citation conventions

- "Napalm §x.y p.N" uses the **body** heading numbers and the PDF page,
  which equals the printed footer. The spec's table of contents is stale:
  it lists the init registers as §16 on p.208, the body has §17 on p.210;
  most chapters from §8 on are off by one or two. Do not cite TOC numbers.
- Tables in the PDF were checked against rendered pages where text
  extraction misaligned columns (pp.25–26, 40–41, 53–64, 67–70, 86–87, 157,
  159–162, 185, 195, 200, 210–223).
- **Avenger** column: written 2026-10-11 from Napalm's change lists (§3.5–3.7
  pp.23–24, revision history §28 pp.244–249) alone. Since 2026-10-18 each
  note is checked against the Avenger spec and marked **confirmed**
  (Avenger defines what the note says, cited), **corrected** (the note was
  wrong; both readings given) or **not covered**. "Changed rev x" still
  cites Napalm's revision history.
- **D*n*** refers to the numbered list in [Disagreements](#disagreements).
  Where spec prose and its bit table differ, the plan follows the bit
  table.

---

## 1. Address map

### PCI BARs

| BAR | Type | Napalm | Measured (Mini-Me) |
|---|---|---|---|
| 0 `memBaseAddr0` (cfg 0x10) | MMIO, non-prefetchable (bit 3 = 0) | Size strapped by FB_DATA[3:0]: 0 = 128 MiB … 4 = 64 MiB, 5 = 32 MiB … 8 = 4 MiB; resizable through `cfgPciDecode[3:0]` (§16.11 p.195). Rev 1.00 made 128 MiB the default (§28 p.245). | `0x40000000`, 128 MiB span (2026-10-10-mini-me-live-identity; plan "Identity and apertures") |
| 1 `memBaseAddr1` (cfg 0x14) | Frame buffer, prefetchable (bit 3 = 1) | Same strap table on FB_DATA[7:4]; `cfgPciDecode[7:4]` (§16.12 pp.196–197) | `0x50000008`, 128 MiB span (same records) |
| 2 `ioBaseAddr` (cfg 0x18) | I/O | 256/512/1024/2048 bytes by FB_DATA[9:8]; `cfgPciDecode[9:8]` (§16.13 p.197) | `0x1000–0x10FF`, 256 bytes (VNT m5b-timing.md) |

Glide cross-check: h3regs.h:475–477, decode codes h3defs.h:1922–1950
(agree). The VNT identity record lists the two 128 MiB spans and says "BAR
indices remain unconfirmed until configuration-space discovery"; the
BAR0/BAR1 assignment in the plan is not stated in the records read here.

**BAR1 size is not VRAM size.** BAR1 offsets below the tile begin page are
linear; offsets at or above it are the tiled view (§18.1 p.224, §17.4
pp.212–213). Linear and tiled are each up to 64 MiB within 128 MiB (§28
p.245, rev 1.00). See D29 for the plan's "upper half" wording.

32 vs 64 MiB boards: the spec gives no per-board BAR size; the straps set
it. The Mini-Me (64 MiB) decodes 128 MiB per BAR. The retail 4500 (32 MiB)
is not established.

### BAR0 sub-spaces (Napalm §4 p.25; Glide h3defs.h:2089–2101)

| Offset | Size | Contents | Glide |
|---|---|---|---|
| `0x0000000` | 512 KiB | I/O register remap (init, PLL/DAC, video; same layout as BAR2) | `SST_IO_OFFSET` |
| `0x0080000` | 512 KiB | CMD/AGP transfer/misc (CMDFIFO) registers | `SST_CMDAGP_OFFSET` |
| `0x0100000` | 1 MiB | 2D registers | `SST_2D_OFFSET` |
| `0x0200000` | 4 MiB | 3D registers (Glide adds an alternate map at `0x400000`) | `SST_3D_OFFSET`, `SST_3D_ALT_OFFSET` |
| `0x0600000` | 2 MiB | TMU0 texture download alias | `SST_TEX0_OFFSET` |
| `0x0800000` | 2 MiB | TMU1 texture download | `SST_TEX1_OFFSET` |
| `0x0A00000` | — | Reserved for BIOS programming (`0xA00000–0xA9FFFF`), VGA memory remap (`0xAA0000–0xABFFFF`, test, `vgaInit0[7]`), reserved | `SST_RESERVED_OFFSET` |
| `0x0C00000` | 4 MiB | YUV planar space (Y/U/V at +0, +1 MiB, +2 MiB; §19 p.226–227) | `SST_YUV_OFFSET` |
| `0x1000000` | 16 MiB | 3D LFB space (pixel-pipeline LFB, `lfbMode` formats) | `SST_LFB_OFFSET` |
| `0x2000000` | 32 MiB | Reserved | `SST_RESERVED2_OFFSET` |
| `0x4000000` | 64 MiB | TMU0 texture download (64 MiB port, rev 1.09) | `SST_TEX2_OFFSET` |

BAR0 is not the frame buffer. Desktop pixels go through BAR1.

### BAR2 (I/O) map

The I/O Base0 table (§4 pp.25–26) mirrors BAR0 `0x00–0xFF` register for
register, plus VGA registers at I/O `0xB0–0xDF`, which are "only in I/O
space, not memory mapped" (D17). §5.2 p.27: a VGA register is at I/O base +
(legacy port − `0x300`). Measured: sequencer `0xC4/0xC5`, misc output read
`0xCC`, colour CRTC `0xD4/0xD5` on the Mini-Me while it was the primary VGA
(VNT m5b-timing.md).

---

## 2. PCI configuration extensions

Napalm §16 map pp.192–193; Glide h3regs.h:463–513. Offsets in bytes.

| Offset | Register | Use / fields | Napalm | Measured |
|---|---|---|---|---|
| `0x00` | Vendor/Device ID | `121A`; Napalm device IDs 6, 7, 8, 9 (lsb = packaging, standard bondout 1; pair chosen by strap, D27) | §16.1–16.2 p.193 | `121A:0009` (2026-10-10-mini-me-live-identity) |
| `0x08` | Revision | — | §16.5 p.194 | `01` (same) |
| `0x2C` | Subsystem vendor/ID | Loaded from the last ROM bytes after reset; writable only with `miscInit1[3]` | §16.14–16.15 p.198, §22.1 p.237 | `121A:0002` (same) |
| `0x40` | FabID [7:0] + `cfgInitEnable` [31:8] | 8 init-register write enable (default 1); 9 PCI FIFO write enable (default 1); 10 enable writes to upper BAR bits; 11–14 snoop enables; 24:15 snoop address; 25–27 SLI swap sync; 28 multi-function (strap FB_DATA_19); 29 disable LFB read cache; 30 snooped init writes. Driver reads only. | §16.24 pp.199–200 | not read |
| `0x44` | ACPI Reset | bit 0 = internal acpi_reset (RO) | §16.23 p.199 | — |
| `0x48` | `cfgPciDecode` | 3:0 BAR0 decode, 7:4 BAR1 decode, 9:8 I/O decode (codes as §1), 13:10 / 17:14 snoop decode, 27:18 snoop address #1 | §16.25 pp.200–201 | not read |
| `0x4C` | `cfgStatus` | Alias of the MMIO status register (§3) | §16.31 p.207 | not read |
| `0x50` | `cfgScratch` | Scratch | §16.32 p.207 | — |
| `0x54–0x64` | AGP cap/status/command, ACPI cap/ctrl | Not touched (plan: no AGP setup) | §16.33–16.38 pp.207–209 | — |
| `0x80/0x84/0x88` | `cfgVideoCtrl0/1/2` | SLI/AA video unit; names only | §16.26 p.201 | — |
| `0x8C` | `cfgSliLfbCtrl` | SLI LFB masks; names only (Glide h3defs.h:1859–1869) | §16.27 p.204 | — |
| `0x90` | `cfgAaDepthBufferAperture` | names only | §16.28 p.204 | — |
| `0x94` | `cfgAaLfbCtrl` | names only | §16.29 p.205 | — |
| `0x98–0xA8` | `agpTestCtrl`, `agpTestData0–3` | names only | §16 p.193 | — |
| `0xAC` | `cfgSliAaMisc` | names only | §16.30 p.206 | — |

Avenger: `cfgPciDecode`, `cfgVideoCtrl*`, `cfgSli*`, `cfgAa*`, `agpTest*`
added in rev 1.10; `cfgSliAaMisc` in 1.11; `cfgInitEnable` changed in 1.10
(§28 pp.247–248). **Confirmed, with one correction** (Avenger §15 map
p.180): Avenger has nothing at 0x44–0x4B or 0x80–0xAC; `cfgStatus` 0x4C,
`cfgScratch` 0x50 and the AGP/ACPI capabilities 0x54–0x64 are the same.
**Corrected:** `cfgInitEnable` is not a changed register. Avenger's 0x40
is `fabID` 3:0 (1 = TSMC) with 31:4 a read/write scratch pad (Avenger
§15.22 p.185; Banshee the same), and neither spec documents any init-write
enable. Napalm's bits 31:8 sit where Avenger had scratch. Disagreement:
D26 (Glide's SST-1 config constants).

---

## 3. Init registers (BAR0 `0x00–0x4F`, also BAR2 `0x00–0x4F`)

All MMIO register accesses must be 32-bit (§21.1 p.234). Napalm §17 table
p.210; Glide `SstIORegs` h3regs.h:121–194.

### `status` — 0x00, 32-bit, R (write clears PCI interrupt)

Aliases: 2D `0x100000`, 3D `0x200000`, config `0x4C`.

| Bits | Meaning |
|---|---|
| 4:0 | PCI FIFO free entries, `0x1F` = empty |
| 5 | PCI FIFO busy |
| 6 | Vertical retrace, **0 = in retrace**, 1 = not |
| 7 | FBI (3D) engine busy |
| 8 | TREX busy |
| **9** | **Napalm busy: any unit or any FIFO** — the idle bit |
| 10 | 2D busy |
| 11, 12 | CMDFIFO 0, 1 busy |
| 13, 14 | TMU 0, 1 busy |
| 30:28 | Swap buffers pending |
| 31 | PCI interrupt generated (§8.1 p.87: "not implemented, always 0") |

- Napalm §17.1 pp.210–211; §8.1 p.87; §7.2.1 p.55.
- Glide h3defs.h:98–111 (bits agree; `SST_FIFOLEVEL 0x3F` follows the old prose, D3).
- Idle procedure, Napalm §21.2 pp.234–235: write `nopCMD` (3D) first, then
  require bit 9 clear on repeated reads ("three times"; the sample code
  needs four, D4).
- Measured: idle as three consecutive clear reads of bit 9, 10 µs apart,
  at most 256 polls, all-ones = device gone; no NOP issued; last value
  `0x5F` (2026-10-11-mini-me-retained-route; VNT m5c-aperture-transition.md).
- Avenger: inherited; "cleaned up status bit descriptions" rev 1.08 (§28 p.247).
  **Corrected:** bits 0–12 and 28–31 are Avenger's, but Avenger reserves
  27:13, so bits 13 and 14 (TMU 0/1 busy) are new in Napalm (Avenger §16.1
  p.188).
- Disagreements: D3, D4.

### `pciInit0` — 0x04, 32-bit, R/W, default `0x01800040`

| Bits | Meaning |
|---|---|
| 6:2 | PCI FIFO empty-entries low-water mark (never 0) |
| 8 | Read wait states (0 = 1 ws, 1 = 2 ws). §21.8 p.237: always set it — a race with 1 ws can return wrong read data |
| 9 | Write wait state |
| 11, 12 | Disable I/O, memory retries |
| 17:13 | Retry interval |
| 18, 19 | PCI interrupt enable, interrupt timeout enable |
| 25:20 | PCI FIFO read threshold (default `0x18`) |
| 26 | Force PCI/CMD frame-buffer accesses high priority |
| 28:27 | PCI FIFO → LFB write timeout (64/128/196/256 MCLK) |

- Napalm §17.2 pp.211–212.
- Glide h3defs.h:1443–1456 (bits 8, 9, 11, 12, 18, 19, 26 agree; D5).
- Measured: not read. Velocity9x reads it in Phase 1 to see whether bit 8
  is set by the BIOS; it does not write it.
- Avenger: watermarks "programmable" since Avenger (§3.5 p.23); snoop note rev 1.10.
  **Confirmed:** Avenger has every field above at the same bits, and the
  same default `0x01800040` (Avenger §16.2 p.189).

### `lfbMemoryConfig` — 0x0C, 32-bit, selector-multiplexed

Two registers behind one address: `lfbMemoryTileCtrl` and
`lfbMemoryTileCompare`.

| Bits (written word) | Meaning (bit table) |
|---|---|
| 31 | Register to write: 0 = TileCtrl, 1 = TileCompare |
| 30 | 1 = selector-only write: updates only bit 29, no tile data |
| 29 | Register returned by the next read: 0 = TileCtrl, 1 = TileCompare |
| 28:0 | Data for the register chosen by bit 31 (when bit 30 = 0) |

Protocol (bit table; measured): read control = write `0x40000000`, read;
read compare = write `0x60000000`, read. Write compare = `0x80000000 |
data`; write control = `0x00000000 | data`.

`lfbMemoryTileCtrl`:

| Bits | Meaning |
|---|---|
| 12:0 | Tile aperture begin page [12:0], 4 KiB pages (default `0x200`) |
| 15:13 | Tile aperture stride (PCI→XY): 0 = 1 KiB … 4 = 16 KiB |
| 22:16 | Number of SGRAM tiles in X (default `0xA`) |
| 24:23 | Begin page [14:13] |

`lfbMemoryTileCompare`:

| Bits | Meaning |
|---|---|
| 14:0 | Begin page for the linear/tiled comparison, 4 KiB pages |
| 15 | 1 = compare uses bits 14:0 above; 0 = compare uses TileCtrl's begin page |

First tiled byte = page × 4 KiB, page = compare[14:0] if compare[15] else
`(ctrl & 0x1FFF) | ((ctrl >> 10) & 0x6000)`.

- Napalm §17.4 pp.212–213; §18.1 p.224; tiles are 128 bytes × 32 lines (§18.3 p.225).
- Glide h3defs.h:1469–1503 (agree with the bit table); h3regs.h:127 calls
  the register "more or less a dummy"; h3regs.h:192–193 list
  `lfbMemoryTileCtrl/Compare` after `0xFC` with a comment that they do not
  exist in I/O space — the spec gives no such direct address (not established).
- Measured: after BIOS mode set, control `0x00003FFF`, compare `0x00000000`
  → first tiled byte `0x01FFF000`: linear BAR1 view ≈ 32 MiB of 64
  (2026-10-11-mini-me-post-mode-capture, 2026-10-11-mini-me-upper-memory-aperture).
  Selector writes changed no tile data. Writing compare `0x8000C000` (bit 15,
  page `0x4000`) moved the boundary to 64 MiB; restore with `0x80000000 |
  saved` worked (VNT m5c-aperture-transition.md;
  2026-10-11-mini-me-upper-memory-sparse, -offscreen-pages, -retained-route).
- Avenger: TileCompare and the two-register form added rev 1.11 (§28 p.248);
  128 MiB tiled/linear selection added rev 1.00 (p.245). **Confirmed:**
  Avenger's `lfbMemoryConfig` is one register, 12:0 begin page, 15:13
  stride, 22:16 tiles in X, default `0xA2200`; no 24:23, no selector bits,
  no TileCompare (Avenger §16.4 p.190).
- Disagreements: D6 (known case: prose on bit 30), D29.

### `miscInit0` — 0x10, 32-bit, R/W

| Bits | Meaning (bit table) |
|---|---|
| 0 | FBI graphics reset |
| 1 | FBI FIFO reset (PCI FIFO and packer) |
| 3:2 | Reserved |
| 4 | Video timing reset |
| 5 | 2D reset |
| 6 | Memory timing reset |
| 7 | VGA video timing reset |
| 16:8 | TV-out / monitor sync delays |
| 29:18 | Y-origin swap value (used when `renderMode[2]` = 0) |
| **30** | **`miscInit1` alias: 0 = 0x14 is `miscInit1`, 1 = 0x14 is `vip2vmiCtrl`** |
| 31 | VMI host port control |

- Napalm §17.5 p.214; §17.7 p.217 (`vip2vmiCtrl`).
- Glide h3defs.h:1506–1524 (bits 0–29 agree; 3:2 and 31:30 are Avenger swizzle bits, D7).
- Measured: `0x00000000` (2026-10-11-mini-me-post-mode-capture). VNT refuses
  any vector with bit 30 set or a reset bit set (m5b-aperture.md).
- Rule for Velocity9x: read `miscInit0` before reading 0x14; if bit 30 is
  set, 0x14 is not `miscInit1`.
- Avenger: 3:2 and 31:30 swizzle removed rev 1.09; 30, 31 added rev 1.11 (§28 pp.247–248).
  **Confirmed:** on Avenger 3:2 are register-write byte/word swizzle and
  31:30 non-modal LFB byte/word swizzle; resets 7:0, delays 16:8 and the
  Y-origin value 29:18 are the same, 17 reserved (Avenger §16.5 p.191).

### `miscInit1` — 0x14 when `miscInit0[30]` = 0, 32-bit, R/W

| Bits | Meaning |
|---|---|
| 0 | `invert_clut_address` — prose: "should be set to 1 for proper operation" |
| 2:1 | Triangle iterator mode |
| 3 | Enable subsystem ID writes |
| 4 | Enable ROM writes |
| 5 | Alternate triangle address map |
| 6 | Disable texture mapping |
| 7–11 | Power down CLUT, DAC, video PLL, GRX PLL, memory PLL |
| 14:12, 15 | 2D block-write threshold, disable 2D block write |
| 16, 17 | Disable 2D-stall-on-3D / 3D-stall-on-2D (test only) |
| 18 | Disable TMU-write broadcast in 2-ppc mode |
| 19 | Command stream reset (only when idle; restore to 0) |
| 21:20, 22 | BAR1 address-based byte/word swizzle select, enable |
| 28:24 | Straps: fast device, BIOS 64 KiB, 66 MHz, AGP enabled, device type |
| 29, 31:30 | pd pin value, pd pin drive |

- Napalm §17.6 pp.215–216.
- Glide h3defs.h:1526–1556 (agree to bit 28).
- Measured: `0x27008001` (2026-10-11-mini-me-post-mode-capture). Per the bit
  table: bit 0 set, bit 15 (2D block write off) set, straps 24/25/26 set,
  27 (AGP) and 28 (device type) clear, bit 29 (pd pin) set. VNT refuses power-down,
  command-stream reset or swizzle enable (m5b-aperture.md).
- Avenger: 22:20 added rev 1.09, 18 rev 1.08, 31:29 rev 1.11 (§28).
  **Confirmed:** Avenger reserves 18 and 23:20 and ends at the 28:24 straps;
  bits 0–17 and 19 are the same (Avenger §16.6 pp.192–193). Avenger's prose
  says bits 17 and 18 disable the cross-pipe stalls; its table, like
  Napalm's, says 16 and 17.

### `dramInit0` — 0x18, 32-bit, R/W

| Bits | Meaning |
|---|---|
| 22:0 | SGRAM/SDRAM timing (tRRD, tRCD, tRP, tRAS, tRC, CAS, …) — read only for Velocity9x |
| **26** | **Number of SGRAM/SDRAM chip sets: 0 = 1, 1 = 2** (strap VMI_DATA_5) |
| **29:27** | **Part type: 0 = 8 Mbit, 1 = 16, 2 = 32, 3 = 64, 4 = 128 Mbit** (straps) |
| 30 | Internal banks: 0 = 2, 1 = 4 (strap TV_DATA_4) |

- Napalm §17.8 pp.217–218.
- Glide h3defs.h:1563–1596 (`SST_SGRAM_NUM_CHIPSETS` bit 26, `SST_H5_SGRAM_TYPE` 29:27).
- Measured: `0x607EADF9` → bit 26 = 0, type 4 (128 Mbit), bit 30 = 1. VNT
  decode: bit 26 0 = 4 parts, 1 = 8 parts; bytes = parts × Mbit × 128 KiB →
  4 × 128 Mbit = 64 MiB; paired-page sweep verified [1 MiB, 64 MiB)
  (2026-10-11-mini-me-offscreen-pages).
- Avenger: type/bank fields widened rev 1.00 and 1.09 (§28 pp.245, 247).
  **Confirmed:** Avenger ends at bit 27, a one-bit part type (0 = 8 Mbit,
  1 = 16 Mbit); 26 is the chip-set count as here, and 25:0 the same
  timings (Avenger §16.7 p.194).
- Disagreements: D8, D9.

### `dramInit1` — 0x1C, 32-bit, R/W

| Bits | Meaning |
|---|---|
| 0, 9:1 | Refresh enable, refresh load value |
| 11 | Triple buffer enable |
| 24:13 | Read sample / output delay controls |
| 29:25 | Memory-controller test controls |
| **30** | **`mctl_type_sdram`: 0 = SGRAM, 1 = SDRAM** (rev B0 and later) |

- Napalm §17.9 p.219. Glide h3defs.h:1599–1619.
- Measured: `0x40240031` → bit 30 = 1, SDRAM (2026-10-11-mini-me-offscreen-pages).
- Avenger: bit 12 reserved rev 1.01. **Confirmed:** on Avenger bit 12 is
  "dither passthrough" and bit 10 video arbitration priority; bit 30
  `mctl_type_sdram` (rev B0 on) is already Avenger's (Avenger §16.8 p.195).
- Disagreements: D8.

### `agpInit0` — 0x20; `tmuGbeInit` — 0x24

Read only for Phase 1 survey; never written (plan: no AGP setup).
`agpInit0`: 0 request priority, 6:4 request-FIFO threshold, 10:7 read-FIFO
threshold, 31:27 outstanding requests (RO) (§17.10 p.220). `tmuGbeInit`:
texture/pixel FIFO watermarks 11:0, TV-out clock delay 19:15, AA clock delay
24:20, 27:25 memory port config (Napalm2 only) (§17.11 p.220). Glide
h3defs.h:1621–1642. Not measured. Avenger (§16.9–16.10 pp.195–196): same
offsets; `agpInit0` 3:1 was the maximum AGP request length, reserved in
Napalm; `tmuGbeInit` (named `textureGRXBackend` in Avenger's map) has the
watermarks 11:0 and TV-out delay 19:15, so the AA delay and memory-port
bits are Napalm's.

### `vgaInit0` — 0x28, 32-bit, R/W

| Bits | Meaning |
|---|---|
| 0 | VGA disable (strap FB_DATA_21) |
| 1 | External video timing (also needed for 0x3DA vsync in genlock, §9 p.157) |
| 2 | VGA CLUT width: 0 = 6-bit, 1 = 8-bit (default 1) |
| 6 | Enable VGA extensions (CR1A–CR1F) |
| 7 | VGA memory remap (test) |
| 8 | Wake-up select 0x46E8 / 0x3C3 |
| 9 | Disable legacy VGA memory/I/O decode |
| 10 | Alternate config read-back through CR1C (0 = enable) |
| **12** | **Extended video shift-out: 1 = VGA makes no memory accesses while the video processor scans out.** Set in extended (non-VGA) modes. |
| 21:14, 24:23 | VGA base, 64 KiB units |
| 22 | Disable SGRAM refresh on HBLANK |
| 26:25 | VBE read aperture bits 11:10 |

- Napalm §17.12 pp.220–222.
- Glide h3defs.h:1644–1675 (bits 0, 1, 2, 6, 8, 9, 10, base agree; bit 12 not defined).
- Measured: not read. The plan's "~10% on blits" for bit 12 is not in the spec.
- Avenger: bits 3, 9:8 defaults, 24:23, 26:25 changed rev 1.00/1.10 (§28).
  **Confirmed, plus one:** Avenger's VGA base is 21:14 only ("anywhere on a
  64K byte boundary within 16M bytes") and 31:23 are reserved, so 24:23 and
  26:25 are new; so is bit 7 (VGA memory remap), reserved on Avenger, with
  5:3 (Avenger §16.11 pp.196–197).
- Disagreements: D19, D32.

### `vgaInit1` — 0x2C, 32-bit, R/W

9:0 VBE write aperture, 19:10 VBE read aperture (32 KiB units), 20 chain-4,
21–28 VGA timing locks, 30:29 write aperture bits 11:10 (§17.13
pp.222–223). Read only. Glide h3defs.h:1677–1679 defines nothing useful
here. Not measured. Avenger §16.12 p.199 has 9:0, 19:10, 20 and the 21–28
locks; 30:29 are Napalm's.

### `0x30`, `0x34` — 2D command / srcBaseAddr aliases (Glide: `dramCommand`, `dramData`)

Write-only aliases of the 2D `command` and `srcBaseAddr`, for SGRAM mode
register initialisation (§17.14–17.15 p.223). Velocity9x never writes them.
D1.

### `strapInfo` — 0x38, 32-bit, selector

Write bit 0 to choose which word later reads return: 0 = `strapInfo0`, 1 =
`strapInfo1` (§17.16 p.223; bits §23 pp.238–240). **A read after any
selector write is stateful**; the Phase 0 probe must save and restore the
selector or not read it. `strapInfo1` 14:10 is the chip ID, 17:15 the PCI
function number. Glide h3defs.h:1685. Not measured. D1, D27. Avenger's map
gives `strapInfo` as one 12-bit read (Avenger §16 p.188), so the selector
and `strapInfo1` are Napalm's. The Napalm databook r1.12 (Table 4.2)
agrees with the `strapInfo1` fields above, read in row order: BAR0, BAR1
and I/O size straps 3:0, 7:4, 9:8, chip ID 14:10, function 17:15.

### `iMatchCtrl` — 0x48 (§17.17 pp.223–224)

AGP 4X impedance control. Collides with `pllCtrl2` at 0x48 (D2). Not read.

---

## 4. PLL, DAC and clock select

### `pllCtrl0` (video) — 0x40; `pllCtrl1` (GRX) — 0x44; `pllCtrl2` — 0x48

| Bits | Meaning |
|---|---|
| 1:0 | K, post divider |
| 7:2 | M, input divider |
| 15:8 | N, multiplier |
| 16 | Test: clock = VCO output |

f_out = 14.31818 MHz × (N + 2) / ((M + 2) × 2^K).

- Napalm §9 and its sub-section "1.1 PllCtrl0, PllCtrl1" p.157. The note on
  p.158 (device ID 4: GRX M forced to `0x18`, 141 MHz max) concerns Avenger
  IDs 4/5 (§3.5 p.23); Napalm's IDs are 6–9.
- Limits: none given for VCO or M/N ranges. §27.8 p.243 (requirements, not
  a spec): video ≥ 135 MHz, M and N 8 bits each.
- `pllCtrl2` appears only in the §4 map; §9 does not describe it (D30).
- Glide h3defs.h:1168–1174 (agrees); h3regs.h:144–146.
- Measured: `pllCtrl0` = `0x00000C01` (N = 12, M = 0, K = 1), **not the
  active clock**: misc output `0xE3` selects clock 0 (2026-10-11-mini-me-first-mode-timing).
- Avenger: PLL programming by device ID is an Avenger note (§3.5, §3.7).
  **Confirmed:** Avenger §9.1 p.144 has the same K/M/N fields and formula,
  and the device-ID-4 note is Avenger's own text. Avenger names `pllCtrl2`
  the AGP PLL and documents it (§9.2 p.145; see D30).
- Disagreements: D10.

### Misc output clock select (VGA 0x3CC read / 0x3C2 write)

Bits 3:2: 0 = 25.175 MHz, 1 = 28.322 MHz, 2 = 50 MHz, 3 = programmable PLL
(Napalm §5.5.5 p.32). Measured `0xE3` → 0, 25.175 MHz; 800×525 total,
59.940 Hz (2026-10-11-mini-me-first-mode-timing).

### `dacMode` — 0x4C

| Bit | Meaning |
|---|---|
| 0 | DAC mode 2:1 or 1:1 (polarity not stated; Glide `SST_DAC_MODE_2X` = 1, h3defs.h:1178) |
| 1, 2 | DPMS on vsync, force vsync value |
| 3, 4 | DPMS on hsync, force hsync value |

Napalm §10.1 p.159; Avenger §10 p.146 is identical, as are `dacAddr` and
`dacData` below. 2× mode also needs `vidProcCfg[26]` and halved
horizontal CRTC timing, all horizontal timing divisible by 16 pixels (§5.4
p.27). Measured `0` (2026-10-11-mini-me-first-mode-timing).

### `dacAddr` — 0x50; `dacData` — 0x54

`dacAddr` 8:0: CLUT index, 512 entries, **no auto-increment** (§10.2
p.159). `dacData` 23:0: red 23:16, green 15:8, blue 7:0 (§10.3 p.159).
Entries 0–255 / 256–511 are chosen per surface by `vidProcCfg[12]`/`[13]`.
`miscInit1[0]` (invert CLUT address) affects host CLUT access. Glide
h3defs.h:1184–1194. Not measured. D11.

---

## 5. Video processor and CRTC extensions

Offsets per §11 table p.160 and §4 p.26; Glide h3regs.h:154–188.

### `vidProcCfg` — 0x5C, 32-bit, R/W

| Bits | Meaning |
|---|---|
| **0** | **Video processor on (VGA off)**; 0 = VGA mode. Re-toggle 0→1 after any `vidScreenSize` change (§11.1.18 p.173) |
| 1 | Cursor mode: 0 = Windows, 1 = X11 |
| 3 | Use 1555 alpha for chroma key (desktop format 4 only) |
| 4 | Half mode |
| **7** | **Desktop surface fetch enable** |
| 8 | Overlay fetch enable |
| **10** | **Desktop CLUT bypass** |
| 11 | Overlay CLUT bypass |
| 12 | Desktop CLUT select (upper 256) |
| **20:18** | **Desktop format: 0 = 8 bpp palettised, 1 = RGB565, 2 = RGB24 packed, 3 = RGB32, 4 = RGB1555** |
| 23:21 | Overlay format |
| **24** | **Desktop in tile space** |
| **26** | **2× mode (two pixels per video clock)** |
| **27** | **Hardware cursor enable** |
| 28, 29 | Disable memory optimisation for desktop, overlay requests |
| 31 | Overlay deinterlace |

- Napalm §11.1.3 pp.161–162.
- Glide h3defs.h:1197–1246 (agree to bit 27; D15).
- Measured: `0x30040C81` after VBE `4111h` → bits 0, 7, 10, 11, 18 (RGB565),
  28, 29 (2026-10-11-mini-me-post-mode-capture). Clearing only bit 7 paused
  desktop fetch; restoring the word worked (VNT m5c-aperture-transition.md).
- Avenger: bit 3 and formats 4 (desktop) / 0, 2, 3 (overlay) added rev 1.01 (§28 p.245).
  **Confirmed:** Avenger's desktop formats stop at 3 and overlay formats 0,
  2, 3 are reserved; its bit 3 is "interlaced video out enable", not
  supported and to be left 0. Bits 28/29 are the same memory-optimisation
  disables, 30 reserved, 31 backend deinterlace (Avenger §11.1.3
  pp.148–149). Every video register in this section sits at the same
  offset in Avenger's map; its address fields are 23:0 (§11 p.147).
- Disagreements: D15.

### `hwCurPatAddr` — 0x60; `hwCurLoc` — 0x64; `hwCurC0` — 0x68; `hwCurC1` — 0x6C

- `hwCurPatAddr` 25:0: byte address of a 64×64 two-plane pattern, linear,
  16-byte line stride (plane 0 in the low 64 bits) (§11.1.4 pp.163–164).
  Widened to 26 bits rev 1.00.
- `hwCurLoc`: X 10:0, Y 26:16, **bottom-right corner**, 0–2047 (§11.1.5 p.164).
- `hwCurC0/C1`: blue 7:0, green 15:8, red 23:16 (§11.1.6–11.1.7 p.164).
- Windows mode: (0,0) C0, (1,0) C1, (0,1) screen, (1,1) inverted screen (table p.164).
- Glide h3defs.h:1250–1261. Not measured. D12.

### `vidSerialParallelPort` — 0x78 (DDC/I2C)

| Bits | Meaning (bit positions per table) |
|---|---|
| 18 | DDC port enable |
| 19, 20 | DDC DCK, DDA write (0 = drive low, 1 = tri-state) |
| 21, 22 | DDC DCK, DDA state (RO) |
| 23 | I2C port enable |
| 24, 25 | I2C SCK, SDA write |
| 26, 27 | I2C SCK, SDA state (RO) |
| 17:0 | VMI host port — not touched |

Napalm §11.1.9 pp.167–168. Glide h3regs.h:162 (no bit defines). Not
measured. D16.

### `vidScreenSize` — 0x98

Width 11:0 pixels; height 22:12 lines; 23 `desktop_addr_fifo_enable`
(§11.1.18 p.173). Width > 1280 needs 2× mode (same section; D34). Glide
h3defs.h:1399–1405. Measured `0x001E0280` = 640×480
(2026-10-11-mini-me-post-mode-capture). D13.

### `vidDesktopStartAddr` — 0xE4

25:0 byte address of the desktop surface (§11.1.25 p.174). Widened to 26
bits rev 1.00. Glide h3defs.h:1423–1424. Measured `0`.

### `vidDesktopOverlayStride` — 0xE8

Desktop stride 14:0 (bytes if linear, tiles across the tile region if
`vidProcCfg[24]`); overlay stride 30:16; 31 overlay fetch-width msb
(§11.1.26 pp.174–175). Glide h3defs.h:1425–1431 (overlay 1428–1430).
Measured `0x00000500` (1280 bytes, linear). D14.

### `vidCurrOverlayStartAddr` — 0xFC (RO)

25:0 overlay address in use (§11.1.31 p.176). Rev 1.10 (§28 p.247): writing
bit 31 of 0xFC then reading 0xFC returns `vidCurrDesktopStartAddr` — a
selector on this offset. Not measured.

### VGA CRTC extensions (I/O 0x3D4/0x3D5 or BAR2 + 0xD4/0xD5)

Active only when `vgaInit0[6]` = 1.

| Index | Bits | Meaning | Napalm |
|---|---|---|---|
| CR1A | 0 HT bit 8; 2 HDE bit 8; 4 HBS bit 8; 5 HBE bit 6; 6 HRS bit 8; 7 HRE bit 5 | Horizontal extension | §5.6.27 p.40 |
| CR1B | 0 VT bit 10; 2 VDE bit 10; 4 VBS bit 10; 6 VRS bit 10 | Vertical extension | §5.6.28 p.41 |
| CR1C | 7:0 | PCI config read-back index/data, or scratch, by `vgaInit0[7:6]` | §5.6.29 p.41 |
| CR1D–CR1F | 7:0 | Scratch | §5.6.30–5.6.32 pp.41–42 |
| CR20/CR21 | | Vertical counter preload (test) | §5.6.33–5.6.34 p.42 |
| CR22 | | Latch read-back | §5.6.35 p.42 |
| CR24 | 7 | Attribute controller index/data state (RO) | §5.6.36 p.42 |
| CR26 | | Display bypass / attribute index | §5.6.37 p.43 |

Measured: CR1A = CR1B = `00`; CRTC `5F/4F/0B/3E/40/DF/E3` for 640×480
(2026-10-11-mini-me-first-mode-timing). D18, D19, D31.

---

## 6. 2D engine (BAR0 `0x100000`)

Register map §7.1 pp.53–54; Glide `SstGRegs` h3regs.h:242–278. All 2D
registers read back the value the next operation will use (§7.2 p.55).
Avenger: the 2D core is inherited; Napalm changes are base addresses widened
to 26 bits (rev 1.00) and `commandExtra[28]` (rev history; highlighted in
the table). **Confirmed** (Avenger §7.1 p.55, §7.2.3 p.59, §7.2.6 p.60,
§7.2.9 p.64): every offset below is the same; base addresses are 23:0
plus bit 31 tiled; `commandExtra` defines bits 3:0 only; `srcFormat` and
`dstFormat` fields and codes are the same; clipping is min-inclusive,
max-exclusive.

| Offset | Register | Fields used | Napalm |
|---|---|---|---|
| `0x00` | `status` | Alias of §3 | §7.2.1 p.55 |
| `0x04` | `intCtrl` | not used | §7.1 p.53 |
| `0x08`, `0x0C` | `clip0Min`, `clip0Max` | x 11:0, y 27:16; min inclusive, max exclusive | §7.2.10 pp.62–63 |
| `0x10` | `dstBaseAddr` | 25:0 byte address; 31 tiled | §7.2.6 pp.58–59 |
| `0x14` | `dstFormat` | 13:0 stride (bytes linear, tiles if tiled, 6:0); 18:16 bpp: 1 = 8, 3 = 16, 4 = 24, 5 = 32 | §7.2.9 pp.60–62 |
| `0x18–0x24` | `srcColorkeyMin/Max`, `dstColorkeyMin/Max` | 23:0; unused while colour keying is off | §7.2.11 pp.63–64 |
| `0x28`, `0x2C` | `bresError0/1` | not used | §7.2.15 p.67 |
| `0x30` | `rop` | ROP1 7:0, ROP2 15:8, ROP3 23:16 (colour-key results select ROP0–3) | §7.2.12 p.64 |
| `0x34` | `srcBaseAddr` | 25:0; 31 tiled. Screen-to-screen only | §7.2.6 pp.58–59 |
| `0x38` | `commandExtra` | 0 src colour key, 1 dst colour key, 2 wait for vsync, 3 pattern row 0, **28 preserve 1555 alpha** (idle or 3D NOP before changing; 16 bpp only) | §7.2.3 p.57 |
| `0x3C`, `0x40` | `lineStipple`, `lineStyle` | not used | §7.2.13–7.2.14 pp.64–67 |
| `0x44`, `0x48` | `pattern0Alias`, `pattern1Alias` | = `colorPattern[0]`, `[1]` (mono pattern rows) | §7.1 p.54, §7.2.5 p.58 |
| `0x4C`, `0x50` | `clip1Min`, `clip1Max` | as clip0, selected by `command[23]` | §7.2.10 p.63 |
| `0x54` | `srcFormat` | 13:0 stride; 19:16 format: 0 = mono, 1 = 8, 3 = 16, 4 = 24, 5 = 32, 8 = YUYV, 9 = UYVY; 20, 21 host byte, word swizzle; 23:22 packing: 0 = stride, 1 byte, 2 word, 3 dword | §7.2.9 pp.60–62 |
| `0x58` | `srcSize` | width 12:0, height 28:16 (stretch only) | §7.2.7 p.59 |
| `0x5C` | `srcXY` | x 12:0, y 28:16, signed; host blits: x gives first-pixel alignment (mono 4:0, colour 1:0) | §7.2.8 pp.59–60 |
| `0x60`, `0x64` | `colorBack`, `colorFore` | Destination format (8: 7:0; 16: 15:0; 24: 23:0; 32: 31:0). `colorFore` is the fill colour | §7.2.4 pp.57–58 |
| `0x68` | `dstSize` | width 12:0, height 28:16 | §7.2.7 p.59 |
| `0x6C` | `dstXY` | x 12:0, y 28:16, signed | §7.2.8 p.60 |
| `0x70` | `command` | see below | §7.2.2 pp.55–56 |
| `0x74–0x7C` | reserved | "Do not write" | §7.1 p.54 |
| `0x80–0xFF` | `launchArea` | see below | §7.3 pp.67–70 |
| `0x100–0x1FC` | `colorPattern[0..63]` | 8×8 pattern, destination format or mono | §7.2.5 p.58 |

Glide offsets agree for every row (h3regs.h:243–274); bit cross-checks in
h3gdefs.h:37–163.

### `command` — 0x70

| Bits | Meaning |
|---|---|
| 3:0 | 0 NOP (wait for idle), 1 screen-to-screen blit, 2 stretch blit, 3 host-to-screen blit, 4 host stretch, 5 rectangle fill, 6 line, 7 polyline, 8 polygon fill, 13/14/15 SGRAM mode/mask/colour writes |
| **8** | **Initiate: 1 = start on this write; 0 = start on a launch-area write** |
| 9 | Reversible lines |
| 10, 11 | After blit/fill, dstX += width, dstY += height |
| 13 | Pattern is mono |
| **14** | **X direction: 1 = right to left** |
| **15** | **Y direction: 1 = bottom to top** (also flips host blits) |
| 16 | Mono transparent |
| 19:17, 22:20 | Pattern X, Y offset |
| 23 | Clip select (0 = clip0, 1 = clip1) |
| **31:24** | **ROP0** (used when colour keying is off) |

With bits 15:14 set, `srcXY`/`dstXY` name the corner the copy starts from:
01 upper right, 10 lower left, 11 lower right (§7.2.8 p.60). Direction
bits apply to plain screen-to-screen blits only; no colour keying or
format conversion right to left (§7.2.2 p.56).

Glide h3gdefs.h:39–69 (codes 0–8, GO bit 8, bits 9–16, 23, ROP 31:24 agree).

### Launch area — 0x80–0xFF

- Screen-to-screen blit and stretch: one write = source x 12:0, y 28:16 →
  `srcXY` (§7.3.1–7.3.2 pp.67–68).
- Rectangle fill: one write = destination x 12:0, y 28:16 → `dstXY`;
  colour from `colorFore`, size from `dstSize` (§7.3.5 p.70).
- Host-to-screen: each write is 32 bits of packed source data; surplus is
  discarded or used by the next host blit; too little aborts the blit
  (§7.3.3 p.68). Spec example 1 (§7.3.3.1 pp.68–69): `command`
  `0xCC000003`, `dstFormat` `0x00030800` (16 bpp, 2048-byte stride),
  `srcFormat` `0x00400000` (mono, byte packed).
- Host blits always run left to right; "Wait for Vsync" must not be used for
  non-DMA host blits (§7.2.3 p.57).

### 2D errata

- **≤16-pixel blit hang (NOP first):** not in the Napalm spec. Searched
  for "hang", "errat", "16 pixel", "workaround": no match. Source: research
  notes only (the plan's "Facts from documents only"). Unchecked.
- **Fence every 64 KiB of FIFO writes:** not in the spec. The spec requires
  a fence after the last CMDFIFO write and before the bump or the jump
  (§13.1.1.1–13.1.1.2 p.179).
- **12 NOPs when leaving 2-ppc mode:** confirmed, §21.6 p.236 (3D,
  `combineMode[29]`, NOPs directed at the TMUs).
- **NOP before a compressed texture:** not found. §20.3 p.234 requires a 2D
  NOP flush around texture downloads.
- **`vgaInit0[12]`:** function confirmed (§17.12 pp.221–222); the "~10%"
  figure is not in the spec.

---

## 7. Command FIFO (BAR0 `0x80000`)

Napalm §14 p.185 (map), §15 pp.187–191 (registers), §13 pp.178–184
(transport). Glide `SstCRegs`/`CmdFifo` h3regs.h:196–239, h3defs.h:1780–1840,
h3gdefs.h:165–283.

| Offset | Register | Fields | Napalm |
|---|---|---|---|
| `0x00–0x14` | `agpReqSize`, `agpHostAddressLow/High`, `agpGraphicsAddress/Stride`, `agpMoveCMD` | AGP moves — not used | §14.1–14.6 pp.186–187 |
| `0x20` | `cmdBaseAddr0` | 23:0 base in 4 KiB pages (frame buffer or AGP) | §15.1 pp.187–188 |
| `0x24` | `cmdBaseSize0` | 7:0 size in 4 KiB pages minus one (0 = 4 KiB); 8 enable; 9 1 = AGP; 10 disable hole counter | §15.2 p.188 |
| `0x28` | `cmdBump0` | W: 15:0 words to add (software management) | §15.3 p.188 |
| `0x2C` | `cmdRdPtrL0` | 31:0 byte read pointer; init = base as bytes | §15.4 p.188 |
| `0x30` | `cmdRdPtrH0` | 3:0 upper bits; init with base | §15.5 p.188 |
| `0x34` | `cmdAMin0` | 24:0 byte address; init base − 4; **reads back 4 more than written** | §15.6 p.188 |
| `0x38` | reserved | | §14 p.185 |
| `0x3C` | `cmdAMax0` | 24:0; init base − 4; reads back +4 | §15.7 p.189 |
| `0x40` | `cmdStatus0` (RO) | 31 AGP xfer, 30 unpacker busy, 24 on-chip FIFO empty, 2:0 packet type, … debug only | §15.8 p.189 |
| `0x44` | `cmdFifoDepth0` | 19:0 unexecuted words | §15.9 p.189 |
| `0x48` | `cmdHoleCnt0` | 15:0 unwritten locations between AMin and AMax | §15.10 p.189 |
| `0x50–0x78` | CMDFIFO 1 set | same layout | §15.11–15.20 pp.189–191 |
| `0x80` | `cmdFifoThresh` | {11, 4:0} fetch threshold, 10:5 high-water mark | §15.21 p.191 |
| `0x84` | `cmdHoleInt` | 21:0 hole timeout, 22 enable | §15.22 p.191 |
| `0x100` | `yuvBaseAddress` | 25:0 | §15.23 p.191 |
| `0x104` | `yuvStride` | 13:0; 31 tiled | §15.24 p.191 |

Constraints from the spec:

- A frame-buffer CMDFIFO must lie in the low 16 MiB (§21.8 p.237).
- Two management modes: software (`cmdBump`) for AGP or frame buffer;
  hardware (hole counting) for frame buffer only (§13.1.1 pp.178–179).
- Hardware mode: write sequentially; at least four entries before a jump to
  the top; fence before the first write to the top (§13.1.1.2 p.179).
- Byte and 16-bit writes are not allowed in the CMDFIFO (§13.1.2 p.179).
- Two streams, first come first served (§13.1.10 p.184).
- Not measured on any VSA-100. The plan's ten-store bring-up order is from
  the prior-work plan, not from this spec.
- Avenger (§13 pp.171–172 map, §14 pp.175–178): every offset in the table
  is the same, read with the map's name column shifted a row. Its
  `cmdFifoThresh` is 4:0 fetch threshold and 8:5 high-water mark, Glide's
  layout (D25). `cmdHoleInt` is the same; `yuvStride` is 12:0.

### Packet headers (bits 2:0 = type)

| Type | Layout (word 0) | Use | Napalm |
|---|---|---|---|
| 0 | 5:3 function: 0 NOP, 1 JSR, 2 RET, 3 JMP local frame buffer, 4 JMP AGP (+1 word); 28:6 address [24:2]; 31:29 zero | Wrap to start, subroutines | §13.1.3 pp.179–180 |
| 1 | 31:16 word count (> 0); 15 increment; 14:3 register base (bit 14 = 2D, 13:11 chip field, 10:3 register number) | Burst register writes; host blits | §13.1.4 p.180 |
| 2 | 31:3 mask of up to 29 2D registers starting at `clip0Min` (2D 0x08) | 2D state | §13.1.5 pp.180–181 |
| 3 | Triangle setup: 5:3 command, 9:6 vertex count, 21:10 parameter mask, 27:22 setup mode, 28 packed colour, 31:29 pad | 3D | §13.1.6 pp.181–182 |
| 4 | 28:15 14-bit general register mask; 14:3 register base; 31:29 pad words | Sparse register writes | §13.1.7 p.183 |
| 5 | 21:3 word count; 25:22, 29:26 byte enables (active low) for last/second word; 31:30 space (LFB, YUV, 3D LFB, texture); word 1 = 27-bit address | Frame-buffer data in order with commands | §13.1.8 pp.183–184 |
| 6 | 20:5 byte count; 4:3 space; four further words (AGP source, stride/width, FB offset, dest stride) | AGP → frame buffer; **broken with AGP CMDFIFOs** (§21.8 p.237) | §13.1.9 p.184 |

Glide h3gdefs.h:182–283 agrees on types and field positions. D25, D33.
Avenger has the same seven types (§12.1.3–12.1.9 pp.166–171); type 0's
jump address is [24:2] (AGP [35:25] in word 1) and type 1's register base
is header 14:3 with the 2D/3D select and chip field as here.

---

## 8. 3D and SLI registers

BAR0 `0x200000` + offset. Napalm §8 map pp.82–86; Glide `SstRegs`
h3regs.h:289–458.

| Offset | Register | One line | Napalm |
|---|---|---|---|
| `0x000` | `status` | Alias of §3 | §8.1 p.87 |
| `0x120` | `nopCMD` | Any write flushes the pipeline; bit 0/1 clear pixel/triangle counters. The idle procedure's NOP | §8.17 p.94 |
| `0x128` | `swapbufferCMD` | Swap; needs `swapPend` first | §8.19 p.95 |
| `0x1E0` | `renderMode` | 1:0 16/15/32 bpp, Y-origin, write masks, 2-ppc bands; new in Napalm (rev 1.01) | §8.26 p.121 |
| `0x1EC` | `colBufferAddr` | 25:4 base; 31 selects primary/secondary (AA) | §8.40 p.130 |
| `0x1F0` | `colBufferStride` | 13:0 linear bytes or 6:0 tiles; 15 tiled | §8.41 p.130 |
| `0x1F4` | `auxBufferAddr` | as `colBufferAddr` | §8.42 p.131 |
| `0x1F8` | `auxBufferStride` | as `colBufferStride` | §8.43 p.131 |
| `0x208` | `combineMode` | bit 29 = 2-ppc; bit 30 = new combine unit | §21.6 p.236 |
| `0x20C` | `sliCtrl` | render mask 7:0, compare mask 15:8, scan mask 23:16, log2 chips 25:24, enable 26 | §8.29 p.125 |
| `0x210` | `aaCtrl` | Primary/secondary vertex offsets, 28 AA enable | §8.30 p.125 |
| `0x214` | `chipMask` | Per-chip write enables, default all on | §8.31 p.126 |
| `0x218` | `leftDesktopBuf` | Desktop swap address (rev 1.10) | §8 map p.84 |

Glide offsets agree (h3regs.h:366, 388–404; `sliCtrl` bits h3defs.h:546–554,
`aaCtrl` h3defs.h:568–673). Avenger's 3D map (Avenger §8 pp.85–86) has
`nopCMD` 0x120 and `swapbufferCMD` 0x128 as here, reserves 0x1E0–0x1E8
(so `renderMode` is new, **confirmed**) and 0x208–0x248 (so
`combineMode`, `sliCtrl`, `aaCtrl`, `chipMask` and `leftDesktopBuf` are
new too), and gives `colBufferAddr`/`auxBufferAddr` 23:0 and the strides
15:0 at the same offsets. Note: the spec prints two 3D maps that differ
for `0x40–0x7C` (pp.83 and 86); the second is the alternate triangle map
(`miscInit1[5]`).

The detail below is for [3dfx-vsa100-d3d.md](../plans/3dfx-vsa100-d3d.md).
Extracted 2026-10-12 from the spec PDF, whose bit tables extract with
misaligned columns. Where prose fixes the order it is followed; where only
Glide's headers do (h5 `h3defs.h`/`h3regs.h`/`h3gdefs.h`), the row says so.
Nothing here is measured.

### 8.1 Addressing and direct writes

- **The CPU may drive the chip by direct writes or through a memory-backed
  FIFO** (§3.1 p.14). The §8 map (pp.81–82) marks which 3D registers go
  through the PCI FIFO; most do, `intrCtrl` does not (§8.2 p.88).
- **The 4 MiB at BAR0 + 200000h decodes** byte 1:0, register 9:2, chip
  13:10, wrap 19:14, swizzle 20, alternate map 21 (§8 p.81).
  - Chip 0 means everyone; the lsb is FBI, then TREX0, TREX1; 6 means both
    TMUs (§8.21 p.102). So TMU0 alone is +800h and TMU1 alone is +1000h —
    derived from the field, not printed.
  - Reads come from FBI; 32-bit accesses only (§8 p.81, §21.1 p.234).
- **The PCI FIFO has 32 entries**, free count in `status[4:0]`, `1Fh` empty
  (§3 p.19, §17.1 p.211; D3). The spec never says software must poll free
  space before writing; it describes PCI retry (`pciInit0` 11, 12, 17:13)
  and a low-water mark that "should never be 0" (§17.2 p.211). Not
  established; Velocity9x polls.
- **A register read returns the current value and ignores queued writes**
  (§8 p.82). LFB reads block until the FIFO is empty (§8.84.2 p.157).

### 8.2 Triangle setup unit

Offsets from Glide's `SstRegs` order; the spec's register numbers ×4 agree
(§8.59–8.75 pp.136–140).

| Offset | Register | Format |
|---|---|---|
| `0x260` | `sSetupMode` | 19:0, below |
| `0x264` / `0x268` | `sVx` / `sVy` | IEEE float |
| `0x26C` | `sARGB` | packed A 31:24, R 23:16, G 15:8, B 7:0 |
| `0x270`–`0x27C` | `sRed`, `sGreen`, `sBlue`, `sAlpha` | float, 0.0–255.0 |
| `0x280` | `sVz` | float |
| `0x284` | `sWb` (Glide `sOowfbi`) | float, global 1/W to FBI and every TMU |
| `0x288`–`0x290` | `sWtmu0`, `sS/W0`, `sT/W0` | float, to every TMU |
| `0x294`–`0x29C` | `sWtmu1`, `sS/Wtmu1`, `sT/Wtmu1` | float, TMU1 only |
| `0x2A0` / `0x2A4` | `sDrawTriCMD` / `sBeginTriCMD` | any write |

`sSetupMode` (§8.59 p.136; Glide `SST_SETUP_*` agrees): 0 RGB, 1 A, 2 Z,
3 Wb, 4 W0, 5 S0/T0, 6 W1, 7 S1/T1 (a clear bit reuses the previous
value); 16 fan (0 = strip), 17 cull enable, 18 cull sign negative, 19
disable strip ping-pong sign flip (D35).

Strips and fans (§8.60 pp.136–137): vertex then `sBeginTriCMD`, vertex then
`sDrawTriCMD`, and the third `sDrawTriCMD`-terminated vertex draws; each
further vertex and `sDrawTriCMD` draws one more. A fan keeps the first
vertex.

The older gradient interface remains: `vertexAx..Cy` 008h–01Ch (12.4),
`startR..` 020h–, gradients 040h–07Ch, `triangleCMD` 080h (bit 31 the
area's sign), float copies 088h–0FCh, `ftriangleCMD` 100h (§8.3–8.16
pp.88–94). With subpixel correction (`fbzColorPath[26]`) the start values
must be resent for every triangle (§21.3 p.235). Not used.

### 8.3 Pixel pipeline

| Offset | Register | Fields | Napalm |
|---|---|---|---|
| `0x104` | `fbzColorPath` | 1:0 RGB select (0 iterated, 1 TMU, 2 `color1`); 3:2 alpha select; 4 local select; 6:5 alpha local select; 7 local from texture alpha; 8 zero other; 9 subtract local; 12:10 multiply select (0 zero, 1 local, 2 other alpha, 3 local alpha, 4 texture alpha, 5 texture RGB); 13 reverse blend; 14 add local; 15 add local alpha; 16 invert; 17–25 the same for alpha; 26 subpixel; **27 texture enable**; 28 clamp | §8.20 p.96 |
| `0x108` | `fogMode` | 0 enable; 1 add; 2 mult; 3 alpha from iterated; 4 from Z; 5 constant; 6 dither; 7 zones | §8.22 p.102 |
| `0x10C` | `alphaMode` | 0 alpha test; 3:1 function (0 never … 7 always); 4 blend; 11:8 / 15:12 RGB source/destination factor; 19:16 / 23:20 alpha factors; 31:24 reference | §8.23 pp.105–108 |
| `0x110` | `fbzMode` | 0 clip; 1 chroma key; 2 stipple; 3 W-buffer; 4 depth enable; 7:5 depth function; 8 dither; 9 RGB write; 10 depth/alpha write; 16 depth bias; 17 Y origin at bottom; 18 aux as alpha (0 at 15 and 32 bpp); 20 compare to `zaColor`; 21 float depth | §8.25 pp.116–120 |
| `0x114` | `lfbMode` | 3:0 format (0 565, 1 x555, 2 1555, 4 x888, 5 8888, 12–14 Z16+colour, 15 Z16×2); 8 through the pipeline; 13 Y origin | §8.24 pp.109–115 |
| `0x118` | `clipLeftRight` | left 27:16, right 11:0; left inclusive, right exclusive | §8.44 p.131 |
| `0x11C` | `clipTopBottom` | top 27:16, bottom 11:0 (Glide `clipBottomTop`, same layout) | §8.44 p.131 |
| `0x124` | `fastfillCMD` | clears the clip rectangle with `color1` and `zaColor`, gated by `fbzMode` 9/10; bit 0 no dither | §8.18 p.94 |
| `0x12C` | `fogColor` | RGB 23:0 | §8.32 p.127 |
| `0x130` | `zaColor` | depth 23:0, alpha 31:24 | §8.33 p.127 |
| `0x144` / `0x148` | `color0` / `color1` | ARGB | §8.36–8.37 p.128 |

Depth and blend function codes: 0 never, 1 less, 2 equal, 3 less-equal, 4
greater, 5 not-equal, 6 greater-equal, 7 always. Blend factors: 0 zero, 1
source alpha, 2 colour, 3 destination alpha, 4 one, 5 1−source alpha, 6
1−colour, 7 1−destination alpha, 8 same colour, 9 1−same colour, `F`
saturate (source) / pre-fog colour (destination).

`renderMode` (1E0h, §8.26 pp.121–123): 1:0 0 = 16 bpp 565, 1 = 15 bpp
1555, 2 = 32 bpp 8888; 17–20 per-channel write masks (32 bpp); 21
guard-band clip; 25 dither rotation (off for fastfill). Depth is 16 bits at
15/16 bpp, 24 bits with 8 stencil at 32 bpp (§8.83.3 p.154, §21.4).

Colour and aux buffers (§8.40–8.43 pp.130–131): base 25:4, 16-byte
aligned; stride bit 15 tiled; linear stride in bytes 13:0, a multiple of
16; tiled stride in tiles 6:0, a tile being 128 bytes × 32 lines (§18.3
p.225). **The spec does not require tiled render targets.** Retro-3dfx's
notes (FINDINGS.md) add a field rule the spec lacks: colour and depth on
opposite 4 KiB page parity, and a hard hang from a mismatched pair (D36).
For linear buffers the 4500 does not need it (D36).

### 8.4 Texture unit

Two TMUs per VSA-100, each with its own cache, sharing memory (p.11, §3.5
p.23, §20.3 p.234).

| Offset | Register | Fields | Napalm |
|---|---|---|---|
| `0x300` | `textureMode` | 0 perspective; 1 min bilinear; 2 mag bilinear; 3 clamp W; 4 LOD dither; 6/7 clamp S/T; 11:8 format (5 P8, **10 RGB565, 11 ARGB1555, 12 ARGB4444**, 15 ARGB8888, others); 12–20 colour combine; 21–29 alpha combine; 30 trilinear; 31 compressed | §8.76 pp.141–143 |
| `0x304` | `tLOD` | 5:0 LOD min; 11:6 LOD max; 17:12 bias; 20 S wider; 22:21 aspect; 24 multi-base; 28/29 mirror; **30 big (2048)** | §8.77 pp.147–148 |
| `0x308` | `tDetail` | detail and filter separation | §8.78 p.149 |
| `0x30C` | `texBaseAddr` | **0 tiled; 1 address bit 25; 24:4 address; 31:25 tile stride** | §8.79 p.150 |
| `0x310`–`0x318` | `texBaseAddr1`, `2`, `38` | multi-base LOD bases | §8.79 |

- A single base points where a full-size level 0 would start; smaller
  levels follow packed, so the base may sit below the data, with `tLOD`
  limiting the levels (§20 p.228).
- After a CPU download, invalidate the texture cache by rewriting
  `texBaseAddr` (§20.3 p.234).
- Combine recipes, from Glide's composite defines (the spec's diagrams did
  not extract):
  - modulate: multiply by local, reverse blend;
  - replace: zero other, add local;
  - add: replace plus add local.

### 8.5 Idle

Write `nopCMD`, then read `status` bit 9 clear three times (§21.2 p.234;
D4). `nopCMD` flushes pending commands and frame-buffer and texture writes
(§8.83.1 p.153).

---

## Disagreements

Listed, not resolved by majority. "Plan follows" says which side Velocity9x
code uses.

1. **I/O map 0x30–0x3C.** §4 p.25 names 0x30/0x34 `dramCommand`/`dramData`
   ("see 2D offset 0x70" / "0x064") and 0x38 reserved, with no 0x3C row.
   §17.14–17.16 p.223: 0x30 = 2D `command` alias, 0x34 = 2D `srcBaseAddr`
   alias (not `colorFore` 0x64), 0x38 = `strapInfo`. §11 p.160 and Glide
   h3regs.h:138–141 put `vidTvOutBlankVCount` at 0x3C. Plan follows §17;
   none of these are written. Avenger sides with §17 and §11: 0x30/0x34
   are the 2D `command`/`srcBaseAddr` aliases, 0x38 `strapInfo` (Avenger
   §16 p.188, §16.13–16.14 p.199), 0x3C `vidTvOutBlankVCount` (§11
   p.147).
2. **0x48.** §17 table p.210 and §17.17 p.223: `iMatchCtrl`. §4 p.25 and
   Glide h3regs.h:146: `pllCtrl2`. Unresolved; neither is written. On
   Avenger 0x48 is `pllCtrl2` (Avenger §9 p.144), and Avenger has no
   `iMatchCtrl`: the §4 map and Glide carry the inherited name. Which one
   Napalm decodes there is still not established.
3. **status FIFO bits (known case).** §8.1 p.87 prose: bits 5:0 free
   space, 64 entries, empty = `0x3F`; bits 27:12 memory FIFO. Bit tables
   (§8.1, §17.1): 4:0 free (`0x1F`), 5 busy, 13/14 TMU busy, 27:15 reserved.
   §17.1 p.211 prose says bits 11:10 for the CMDFIFOs; the table says 11, 12.
   Glide has both `SST_FIFOLEVEL 0x3F` and `SST_PCIFIFO_FREE 0x1F`
   (h3defs.h:98–99). Plan follows the table and uses only bit 9 (VNT
   measured the same choice). Inherited: Avenger has the same split, its
   3D status page saying 5:0, 64 entries, `0x3F` and a memory FIFO in 27:12
   (Avenger §8.1 p.89) and its init page 4:0, 32 entries, `0x1F`, 27:13
   reserved (§16.1 p.188), with the same "bits 11:10" slip in the
   CMDFIFO prose.
4. **Idle procedure.** §21.2 p.234 asks for a NOP before polling and three
   clear reads; its sample code returns idle only after four. VNT's
   measured procedure uses three reads and no NOP. Whether the NOP matters
   on the 4500 is not established.
5. **pciInit0.** Glide bit 0 `STALL_ENABLE`, bit 7 `HARDCODE_BASE`
   (h3defs.h:1444, 1447); spec: 1:0 reserved, 7 reserved default 1 (§17.2
   p.211). Spec prose says bits 25:20 group LFB writes; the table says PCI
   FIFO read threshold. Plan: read only.
6. **lfbMemoryConfig (known case).** Prose (§17.4 p.212): "any time the
   incoming data to bit 30 is 1 will either [register] be updated"; also
   reads selected by "bits 30:29 = 2 / 3". Bit table: bit 30 = selector-only,
   no tile data. Measured on the Mini-Me: selector writes `0x40000000` /
   `0x60000000` changed no tile data; the compare write `0x8000C000` took
   effect. Prose also gives tiles-in-X as 21:16; table 22:16 (Glide
   h3defs.h:1497 agrees with the table). Plan follows the table.
7. **miscInit0 bits 3:2 and 31:30 (known case).** Prose (§17.5 p.214) and
   Glide (h3defs.h:1509–1510, 1523–1524): byte/word swizzle enables. Bit
   table: 3:2 reserved, 30 = `miscInit1`/`vip2vmiCtrl` alias, 31 = VMI host
   port; revision history 1.09/1.11 (pp.247–248) records the change. Plan
   follows the table. Measured `miscInit0` = 0: no evidence either way.
8. **SDRAM sizing.** §17.9 p.219 prose: with SDRAM only 16 Mbit parts and a
   16 MB frame buffer are supported, and the type/chip-set fields are
   ignored. Mini-Me: `dramInit1[30]` = 1 (SDRAM), type 4 (128 Mbit), 64 MiB
   verified by a full paired-page sweep. The prose is wrong for this chip;
   plan follows the fields.
9. **Parts per chip set.** Spec: `dramInit0[26]` = number of chip sets (0 =
   1, 1 = 2); it never says how many parts form a set. VNT decodes 0 = 4
   parts, 1 = 8 parts. The 4-part figure is not in the databook; the Mini-Me
   result (64 MiB) is consistent with it. Phase 1 should record both fields
   with the measured size.
10. **PLL M = 0.** The plan lists "M = 0 refused" among document facts. The
    databook says nothing about M = 0; VNT m5b-timing.md attributes it to a
    source not followed here. Separately, the device-ID-4 note gives GRX M
    as `0x18` (p.158) and as `0x24` (§3.7 p.24). Napalm IDs are 6–9; whether
    either note applies is not established.
11. **dacAddr width.** Spec 8:0, 512 entries (§10.2 p.159); Glide
    `SST_DAC_ADDR` `0xFF` (h3defs.h:1186). Plan follows the spec. Avenger
    agrees with the spec: 8:0, 512 entries, no auto-increment (Avenger
    §10.2 p.146).
12. **Cursor colour order.** Spec `hwCurC0/C1`: blue 7:0, green 15:8, red
    23:16 (§11.1.6–11.1.7 p.164), matching `dacData`. Glide: red shift 8,
    green shift 16 (h3defs.h:1258, 1260). Plan follows the spec; Phase 1 or
    the cursor work measures it.
13. **vidScreenSize height.** Spec 22:12 with bit 23 =
    `desktop_addr_fifo_enable` (§11.1.18 p.173). Glide height mask
    `0xFFF << 12` overlaps its own bit-23 define (h3defs.h:1403–1405); VNT
    uses 23:12. Plan follows the spec (22:12).
14. **Desktop stride width.** Spec 14:0 (overlay 30:16, bit 31 overlay
    fetch msb; §11.1.26 pp.174–175). Glide 13:0 / 29:16 (h3defs.h:1426); VNT
    13:0. Plan follows the spec; values under 16 KiB are unaffected.
15. **vidProcCfg bits 3, 29, 30.** Spec: 3 = 1555 alpha chroma key, 29 =
    disable overlay memory optimisation, 30 reserved. Glide: bit 3 also
    `INTERLACED_EN`, 29 `OVERLAY_EACH_VSYNC`, 30 `OVERLAY_STRIDE_ADJUST`
    (h3defs.h:1204–1205, 1245–1246); §11.3 p.177 says interlaced output is
    not implemented. The Mini-Me BIOS value sets bit 29; its meaning is not
    established. Plan preserves bits 28–31 as found. Avenger (§11.1.3
    pp.148–149): bit 3 is "interlaced video out enable", not supported
    (Glide's `INTERLACED_EN` is this inherited name; Napalm reused the bit),
    28 and 29 disable desktop and overlay memory optimisation as Napalm's
    spec says, 30 reserved. Glide's `OVERLAY_EACH_VSYNC` and
    `OVERLAY_STRIDE_ADJUST` match neither spec. Both specs read the
    Mini-Me's bits 28/29 as the two optimisation disables.
16. **vidSerialParallelPort cross-references.** The DDC write rows (19, 20)
    say the line state is "reflected in bit 26/27"; the I2C write rows (24,
    25) say "bit 21/22". The table itself puts DDC state at 21/22 and I2C
    state at 26/27 (§11.1.9 p.167). Plan follows the table's bit positions;
    DDC is unmeasured.
17. **VGA registers in memory space.** §4 p.26: I/O `0xB0–0xDF` VGA
    registers exist in I/O space only. Glide places `vgaRegister[12]` there
    in the memory-mapped struct (h3regs.h:178). Plan uses BAR2 only, as VNT
    measured.
18. **CR1A base-index column.** It gives bit 4 (horizontal blank start bit
    8) a base of CR03; CR02 is start horizontal blanking (§5.6.4 p.35). Plan
    follows the register names.
19. **CR1C enable.** The CR1C table decodes `vgaInit0[7:6]`, with bit 7 =
    "extended registers disabled" (§5.6.29 p.41); §17.12 p.222 defines
    `vgaInit0[7]` as VGA memory remap (test). Not used.
20. **2D clip select.** `command` prose says bit 31 selects clip0/clip1; the
    table and Glide (h3gdefs.h:67) say bit 23 (§7.2.2 p.56). Plan follows
    the table.
21. **Host swizzle bits.** Prose: `srcFormat` bits 19 and 20 (§7.2.9 p.61);
    table and Glide: 20 and 21 (p.62; h3gdefs.h:92–93). Plan follows the
    table. Inherited: Avenger's prose says 19 and 20 and its table 20 and
    21 (Avenger §7.2.9 pp.62, 64).
22. **2D map widths.** §7.1 p.54 lists `dstFormat` as 17:0 and `srcFormat`
    as 18:0; the §7.2.9 tables use 18:16 and 23:16. Plan follows §7.2.9.
    Inherited: Avenger's map gives the same 17:0 and 18:0 against the same
    tables (Avenger §7.1 p.55, §7.2.9 p.64).
23. **commandExtra 31:29.** Spec table: reserved (§7.2.3 p.57); revision
    1.05 (p.246) added SLI there; Glide defines SLI bits 29–31 and an
    `EN_CLUT88` bit 7 (h3gdefs.h:52, 77–79). Not used single-chip.
24. **Host blit example 2.** §7.3.3.2 p.69 repeats example 1's prose (mono,
    byte packed) for `srcFormat` `0x00240000` (24 bpp, word swizzle) and
    writes `dstXY` where `dstSize` is meant. Use the tables, not this
    example.
25. **CMDFIFO fields.** Size field: spec 7:0 (§15.2 p.188) and Glide
    h3gdefs.h:176 `0xFF`, but Glide h3defs.h:1798 `0xF`. `cmdFifoThresh`:
    spec {11, 4:0} + 10:5 (§15.21 p.191, more bits per rev 1.10); Glide 4:0
    + 8:5 (h3defs.h:1836–1838). The §14 map prints `cmdFifoThresh` as
    "0x080(96)". Glide calls 0x40 `unusedB` where the spec has `cmdStatus0`
    (h3regs.h:205). `cmdFifoDepth1` is "freespace" on p.185 and depth on
    pp.190–191. Plan follows §15. Avenger explains most of this: its
    `cmdBaseSize0` size is 7:0, its `cmdFifoThresh` is exactly Glide's 4:0
    + 8:5 (so Glide describes Avenger, and Napalm widened it), it has
    `cmdStatus0` at 0x40, and its map carries the same "0x080(96)" typo and
    "freespace" label (Avenger §13 pp.171–172, §14.2, §14.21 pp.175, 178).
26. **Config space constants.** Glide h3defs.h:1705–1728 define
    `SST_BUS_SNOOP0` 0x44, `SST_BUS_SNOOP1` 0x48 and init-enable bits 0–11
    (SST-1 layout). Spec: 0x44 ACPI reset, 0x48 `cfgPciDecode`,
    `cfgInitEnable` in bits 31:8 of 0x40 with FabID in 7:0 (§16 p.192,
    §16.24 pp.199–200). Glide's struct (h3regs.h:488–492) agrees with the
    spec. Plan follows the spec.
27. **Device-ID strap.** §16.2 p.193: TV_DATA_6 picks {6,7} or {8,9}. §23
    p.238: `strapInfo0[19]` = TV_DATA_7 = `pci_device_id`, `[18]` = TV_DATA_6
    = force `strapInfo1` to 0; the text below it names TV_DATA_5 for that.
    Rev 1.11 (p.248) says TV_DATA_6. Read-only; recorded only. The Napalm
    databook r1.12 (March 2000, later than the spec) adds a third reading:
    its Table 4.1, read in row order through shifted columns, puts the
    device-ID pair select at `strapInfo0[18]` = TV_DATA5 and the
    `strapInfo1` force at `[17]` = TV_DATA4, while its own prose names
    TV_DATA5 for the force. Still unresolved; Avenger has no `strapInfo1`.
28. **Stale LFB text.** §8.84 p.155 ("FIX THIS"): LFB at an 8 MiB offset,
    4 MiB long (SST-1). §4 p.25 and Glide (h3defs.h:2099): 3D LFB at
    `0x1000000`, 16 MiB. Plan follows §4.
29. **"BAR1 upper half is tiled" (plan wording).** The spec defines the
    tiled view as starting at the tile begin page, wherever that is (§17.4,
    §18.1). On the Mini-Me it starts at `0x01FFF000`, inside the lower half
    of BAR1. "Upper half" is true only when the boundary is set to 64 MiB.
30. **pllCtrl2.** In the §4 map and Glide, absent from §9, which documents
    only the video and GRX PLLs; §3.5 p.23 says the separate memory clock
    domain was removed in Avenger, yet `miscInit1[11]` still powers down a
    "memory PLL". Its function is not established; never written. On
    Avenger, `pllCtrl2` is the AGP/PCI PLL: bits 1:0 select a test mux, and
    with the PCI_66MHz and PLL_BYPASS straps they choose between bypass and
    PLL clocking, only three combinations being allowed (Avenger §9.2
    p.145). That is likely what Napalm's map name refers to; not
    established for Napalm, and still never written. Avenger's
    `miscInit1[11]` also reads "power down memory PLL".
31. **VNT HDE formula.** VNT m5b-timing.md writes horizontal display as
    `CR01 + CR1a bit2*64`; that is 256 only if bit 2 is taken as its masked
    value (4). The spec says HDE bit 8. Unexercised (CR1A = 0).
32. **vgaInit0 bit 2.** Prose (§17.12 p.221): set 0 (6-bit DAC) for VGA
    compatibility; table default 1 (8-bit). Not used.
33. **Packet type 4 limits.** "Maximum of 22 words" and "N ranges from 0 to
    16" with a 14-bit mask (§13.1.7 p.183). Internally inconsistent.
34. **2× mode threshold.** Width > 1280 (§11.1.18 p.173); pixel clock >
    135 MHz (§5.4 p.27); "any display larger than 1280×1024" (§11.3 p.177).
    Not reconciled; matters only for native mode setting.
35. **Setup fan bit.** §8.60 prose: write `sSetupMode` bit 0 to choose fan.
    §8.59 table and Glide `SST_SETUP_FAN`: bit 16 (bit 0 is RGB). Plan
    follows the table.
36. **Colour/depth page parity.** Not in the spec. Retro-3dfx FINDINGS.md:
    a render target outside the matching tiled colour slot hard-hung a
    Voodoo3, and Glide gives linear buffers bit-12 parity, colour even and
    aux odd. **Measured not needed for linear buffers:** a linear Z16
    buffer on the colour buffer's own parity drew and compared exactly on
    the Voodoo4 4500 (TDFX3D /parity, A8U4I5 boot 428). Tiled buffers are
    not covered.
37. **Packed or separate colour under direct writes.** Only the command
    FIFO's type 3 packet has a packed-colour bit (28); the spec's sample
    code uses an undefined `PACKEDCOLOR` flag (pp.139–140). How the setup
    unit chooses with direct writes is not established; the probe measures
    it.
38. **combineMode multiply-select width.** Spec 8:6, Glide 7:6. Not used in
    Phase 5.

---

## Not established

- Any value on the retail Voodoo4 4500: BAR sizes, `dramInit0/1`, tile
  control/compare after the BIOS mode set, `vidProcCfg`, PLL, straps.
- Whether the 4500 BIOS leaves its tile boundary at or below 32 MiB, and
  whether it moves it for larger modes.
- The BAR index of each memory span on the Mini-Me (the VNT identity
  record leaves it to configuration-space discovery; the plan states it).
- Parts per chip set for `dramInit0[26]` (D9).
- Polarity of `dacMode[0]`; whether `dacMode[0]` and `vidProcCfg[26]` must
  both be set for 2× mode.
- PLL M/N/K limits and VCO range; any M = 0 restriction; `pllCtrl2`.
- Cursor colour byte order (D12); `vidScreenSize[23]` (D13).
- `vidProcCfg[29]` meaning (D15).
- A direct MMIO address for `lfbMemoryTileCtrl/Compare` (Glide's 0x100/0x104
  placement).
- Whether the status-polling NOP (§21.2) is needed; the ≤16-pixel blit hang
  and the 64 KiB fence rule (research notes only).
- `pciInit0` as left by the BIOS, in particular bit 8 (2 read wait states,
  §21.8).
- `vgaInit0[12]` effect on 2D throughput.
- CMDFIFO behaviour of any kind on a VSA-100; frame-buffer FIFO placement
  below 16 MiB on a 32 MiB card.
- DDC/I2C through `vidSerialParallelPort`.
- `strapInfo1` contents and the stateful selector at 0x38 on a live card.
- Which registers are truly shared with Avenger: settled from the
  documents on 2026-10-18 (the **confirmed**/**corrected** notes above).
  The init, video, DAC, PLL, 2D, CMDFIFO and packet layouts are Avenger's
  at the same offsets, with the Napalm additions each note names. Two
  notes were wrong: `status` bits 13/14 are new, and `cfgInitEnable` has
  no Avenger ancestor. Whether the silicon matches either document is not
  established.
