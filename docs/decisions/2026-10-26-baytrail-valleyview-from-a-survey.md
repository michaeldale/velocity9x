# Bay Trail's Valleyview graphics (8086:0F31) from one DOS survey: Ivy Bridge's layout with 32-bit BARs, so the Haswell decode would refuse it

Date: 2026-10-26
Machine: no Bay Trail is in the fleet and nothing below has run on one.
Evidence: one V9XSURV report, build `39efca9e`, from a Wyse 3030 LT thin
client (Celeron N2807, by the file name), committed unmodified:
[`wyse3030lt-baytrail-n2807-vgasurv-2026-10-26-39efca9e.ini`](../probe/references/wyse3030lt-baytrail-n2807-vgasurv-2026-10-26-39efca9e.ini).
The report's own date, 2010-01-02, is the machine's unset clock.

No code changes. `0F31` is not bound and this document does not bind it.

## Which chip it is

The IGD is `8086:0F31` rev 0E, subsystem `8086:7270` (Intel's default, not
Dell's). The ROM is "Intel(R) VLV Mobile/Desktop PCI Accelerated SVGA BIOS",
build 1024 of 02/05/2015, VBT `VALLEYVIEW`. Host bridge `0F00`, LPC `0F1C`;
CPU signature `00030678` (family 6, model 37h, Silvermont). That is Bay
Trail: the CPU is Atom-class, the graphics is a Gen7 render core (Ivy
Bridge's generation, not Haswell's 7.5) behind Valleyview's own display
engine. The only other device of note is an RTL8168 on bus 1.

The machine booted DOS 7.10 through the firmware's legacy (CSM) path; a
Dell option ROM at `CE80` is that firmware's, not the graphics'.

## What the survey establishes

| | This board (VLV) | HD 4000 (W9QI4E14) | What the Haswell decode expects |
|---|---|---|---|
| BAR0 (GTTMMADR) | `90000000`, **32-bit** | 64-bit | 64-bit (MEMTYP 10b) |
| BAR2 (GMADR) | `80000008`, **32-bit** prefetchable | 64-bit | 64-bit |
| BAR4 (I/O) | `2021` | I/O | |
| GGC (50h) | `0211` | | |
| MSAC (62h) | `02`, 256 MiB | 256 MiB | |
| BDSM (5Ch) | `7B000001` | | |
| 70h | `7AE00001` | | |
| ASLS (FCh) | `794E8018` | | |
| VBE TotalMemory64K | 991 (`0x3DF0000`) | | |

Read from the raw `Config.*` lines, not only the decoded `Bar*` keys:
offsets 10h-1Bh are `90000000 00000000 80000008`, so the type bits really
are 00b on both BARs and the zero dword at 14h is BAR1, not a high half.

- GGC `0211`: lock set, GMS (bits 7:3) 2, which at Gen6/7's 32 MiB unit is
  64 MiB stolen; GGMS (bits 9:8) 2, a 2 MiB GTT. Both are inside the ranges
  `hsw_config.c` accepts. A 2 MiB GTT reaches 2 GiB, more than the 256 MiB
  aperture.
- BDSM `7B000000` (lock bit set). Stolen runs to `7F000000`.
- The dword at 70h, `7AE00001`, is exactly BDSM less 2 MiB: where the GTT's
  stolen block belongs if it sits directly below BDSM, as BGSM places it
  on Haswell. That it is a BGSM-like register in the IGD's own config space
  is an inference from the arithmetic; no Valleyview document was read for
  it. On Haswell the driver reads BGSM from the host bridge.
- E820 marks `7AE00000`-`80000000` reserved, which covers the 2 MiB GTT,
  the 64 MiB stolen, and 16 MiB above it the survey does not explain.
- GMADR `80000000` + 256 MiB ends at `90000000`, where GTTMMADR starts. The
  survey is query-only and does not size BARs, so GTTMMADR's length, and
  with it the GTT at +2 MiB, is assumed from Linux (below), not read.
- VBE reports 64 MiB less 2112 KiB. The heap plan takes the lowest ceiling,
  so a short VBE figure is safe; why it is 2 MiB + 64 KiB short is not
  known.
- ASLS lies inside the ACPI NVS range E820 reports at `79444000`.

## Why the Haswell path would refuse it as it stands

`v9x_intel_hsw_decode_layout` sets `V9X_INTEL_HSW_LAYOUT_BAR0_TYPE` and
`..._BAR2_TYPE` when either BAR is not 64-bit (`hsw_config.c:116`). This
board's BIOS assigns both as 32-bit, so a `0F31` descriptor on that path
would stop at the layout decode before any MMIO is mapped. That refusal is
correct for what it guards (a device that is not the one the Haswell
datasheet describes) and the right change, if Bay Trail is ever taken on,
is a Valleyview arm that expects 32-bit BARs, not a relaxed Haswell check.
The PCI reader in `runtime.asm` (`V9xPciReadHswConfig`) would need the
same; it was not read for this document.

Beyond the BARs, three differences are known from Linux 6.6 i915 and not
from anything this survey measured:

- **Display base.** Valleyview's display block sits at MMIO + `180000h`
  (`VLV_DISPLAY_BASE`), not at the PCH-split offsets Ivy Bridge and Haswell
  use. Every plane, pipe and scanline register the Haswell path writes
  (`hsw_display.c`'s DSPSURF split, the flip and scanline code) moves.
- **PTE format.** `byt_pte_encode` writes bit 1 as "writeable" and bit 2 as
  "snooped by CPU caches"; Haswell's encode (`v9x_intel_hsw_encode_pte`)
  puts a cache-control field in bits 3:1. A Haswell PTE with bit 1 clear
  would map a page the GPU cannot write. There is no LLC.
- **Render core.** Gen7 with fewer EUs and its own URB sizing; Mesa treats
  it as Ivy Bridge with `is_baytrail` exceptions. Whether the Ivy Bridge
  render path (`hd4000-ivb`) holds there is not checked.

## Not an alias of the HD 4000

Copying `v9x_ivb_hd4000_device` (`hsw_hw16.c`) with `0F31` in it would
not run. Besides the BAR types, the Ivy Bridge path differs from what
Linux 6.6 i915 does for Valleyview in five places. None is measured.

| | Ivy Bridge path today | Valleyview in i915 | Effect of a plain alias |
|---|---|---|---|
| BAR types | 64-bit required (`hsw_config.c:116`) | 32-bit on this board (measured) | Layout decode refuses; nothing mapped |
| Forcewake | MT request `A188h`, legacy `A18Ch` (`loader.asm`) | `FORCEWAKE_VLV` `1300B0h`, ack `1300B4h`; a separate media well | Engine open times out or pokes absent registers |
| Display registers | PCH-split offsets | MMIO + `180000h` (`VLV_DISPLAY_BASE`) | Flips, plane split and scanline reads hit the wrong registers |
| PTE | Cache field in bits 3:1 (`v9x_intel_hsw_encode_pte`) | Bit 1 writeable, bit 2 snooped (`byt_pte_encode`); no LLC | A PTE with bit 1 clear maps a page the GPU cannot write |
| Stolen reserved | `1082C0h`, Gen7/7.5 decode | `vlv_get_stolen_reserved` | Heap top may land in the reserved block |

What carries over: GGC and MSAC decode, BDSM, the 2 MiB GTT and 256 MiB
aperture all fall inside what `hsw_config.c` accepts; VBE's framebuffer is
in BAR2 as on the HD 4000, and pitch is packed except `0103h`. Mode setting
stays with VBE. The render core is Gen7, so the Ivy Bridge builders are
the starting point, but Mesa's `is_baytrail` exceptions (EU count, URB
sizing, thread limits) were not compared against them.

The work, in order:

1. A Valleyview arm in the layout decode and in `V9xPciReadHswConfig`
   that expects 32-bit BARs, with host tests from this survey's words.
   The Haswell check stays as it is.
2. A third forcewake choice in the mini-VDD next to Haswell's and Ivy
   Bridge's.
3. Valleyview display offsets, PTE encode and stolen-reserved decode, each
   host-tested where it is arithmetic.
4. The Ivy Bridge fill engine and Direct3D path, checked against the Bay
   Trail exceptions.

Steps 1 and the arithmetic of 3 can be written and host-tested without a
machine. Forcewake, the display base and the PTE bits cannot: the first
boot should be a read-only MMIO audit. Without a Bay Trail in the fleet
this is a plan for `docs/plans/`, not a change to make blind.

## What else the report shows

- VBE 3.0, no protected-mode interface. 18 modes describe: 640x480 to
  1920x1200 at 8, 16 and 32 bpp, no 24 bpp. 21 listed modes (`0160h`-`0171h`,
  `013Ch`, `014Dh`, `015Ch`) fail 4F01h with `014F` rather than returning
  attributes 0 as the G4x and G33 BIOSes do. Not checked here whether the
  runtime VBE scan skips a mode whose query fails.
- Mode `0103h` (800x600x8) has a pitch of 832, the same as on the G33
  ([2026-10-26](2026-10-26-g33-gma3100-from-a-survey.md)); every other mode's
  pitch is width x bytes per pixel.
- EDID read through VBE/DDC: a Dell U2412M, 1920x1200 preferred. The VBT
  carries LFP entries, but `FlatPanelInfo=0000` and the box has no panel.
- The shadowed ROM is 57 KiB against a PCIR length of 64 KiB and its
  checksum does not balance, as on the other Intel BIOSes; Intel trims the
  shadow after POST. The ROM carries the string "For Evaluation Use Only"
  in a 2015 production image.
- E820 truncated at 16 entries, the survey's limit, so RAM above
  `80000000` is not listed. Usable RAM below 4 GiB ends at `77C75000`,
  about 1.87 GiB, with the usual 1 MiB hole at `20000000`. Windows 98 at
  that size needs the known large-memory settings; that is setup, not the
  driver.
- `[Chipset]` and `[Aperture]` are absent: no Intel port probe, no
  aperture switch, so nothing here was read from MMIO.

## Not measured

All of it on silicon: whether Windows 98 boots on this board at all, the
BAR sizes, the GTT offset in BAR0, the meaning of 70h, the display base,
the PTE format, forcewake, the stolen-reserved register, and any engine.
The read-only audit above follows the one Ivy Bridge had
([2026-10-21](2026-10-21-ivybridge-w9qi4e14-audit.md)).
