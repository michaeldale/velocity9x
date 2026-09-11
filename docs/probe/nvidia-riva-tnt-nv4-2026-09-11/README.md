# Riva TNT (NV4) baseline, on the card

BringupKit evidence behind
[Two NVIDIA cards baselined, and what the VGA ports are not hiding](../../decisions/2026-09-11-nvidia-nv4-and-nv5-baselines.md).
FX-6300 Debian host `bringup-target`, `10de:0020` rev 04 at `0000:04:06.0`,
subsystem `1092:0550` (Diamond Viper V550), no kernel driver bound, boot
`355018cc-c1e1-4658-951d-1a42017c9c6f`, captured 2026-09-11T07:33:01Z.

Copied out of `bringupkit\runs\tnt-handoff` because that tree's paths get
reused; the ViRGE/DX bundle was re-run at the same path within the hour.
Identify this run by boot id, never by path.

| File | What it carries |
|---|---|
| `baseline.json` | the only copy of the full 256-index sweep of all three banks, under `raw.vendor.sweep`. The alias periods in the decision doc are computed from here and cannot be recomputed from anything else in this directory |
| `capture.ndjson` | the `register_sweep` event: standard range only, 25 CRTC, 5 sequencer, 9 graphics, indices restored |
| `tnt-aperture.ndjson` | BAR1 at `0xFA000000`, 16 MiB decoded, `access_width: 4`, nine write offsets to 8 MiB, no wrap and no decode edge |
| `rom-capture.ndjson` | the ordinary capture aborting at the bridge stage without reading a byte — this is why the bundle's `rom.image` says refused |
| `report.md` | the kit's section coverage, side effects and uncertainty list |
| `bundle-README.md` | the kit's write-up, verbatim |
| `sha256sums.txt` | covers the five evidence files of the original bundle; all five verify against the copies here |

## The readings that matter

```
BAR0        0xFB000000    16 MiB, MMIO register space
BAR1        0xFA000000    16 MiB aperture, >=16 MiB installed, 32-bit round-trip
CR11        0x8E          bit 7 set: CRTC write-protect on, as the BIOS left it
CR1F        0x03          NVIDIA's extended-CRTC lock register, unlock not attempted
```

`PMC_BOOT_0 = 0x20044001` is an operator statement in `bundle-README.md`, from
a run whose log is not in this bundle. Nothing in these five files reads it.

CRTC indexes `0x19`-`0x7F` that read non-zero, measured and unattributed:

```
CR1A=3D CR1B=83 CR1C=18 CR1F=03 CR20=20 CR21=FE CR22=20 CR23=04
CR26=20 CR27=01 CR2C=14 CR34=2D CR39=FF CR3B=20 CR3E=2E CR3F=32
```

`CR22` and `CR26` are the standard read-only latch and attribute-index
registers and are not vendor space. The other fourteen are — this is the one
place in either bundle where content, rather than an inference from an alias
modulus, shows vendor registers.

## The re-export left this card's CRTC bullet behind

`bundle-README.md` here is the 11 September re-export, which rewrote the
sequencer and graphics bullets and added a note withdrawing the earlier "no
indexed vendor register space" conclusion. The CRTC bullet was not rewritten
and still reads "no simple aliasing; 228 indexes above the standard range read
other than 0xFF". Two problems with it:

- It counts across `0x19`-`0xFF`, mixing the mirrored high half with the real
  low half, where the corrected bullets count only inside the decode period.
  Inside the period the figure is 103 indexes, 102 of them non-`0xFF`, 16
  non-zero.
- "No simple aliasing" rests on a single index. `CR34` and `CRB4` disagree;
  the other 127 mirrored pairs match. See the decision doc, which does not
  treat one sample as settling whether that is separate storage or a register
  that changed between two reads.

## The ROM is not in this directory

The card's ROM could not be read the ordinary way: there is no `0x55AA` in the
legacy shadow at `0xC0000`, the kernel's own shadow read fails with EIO, and
nouveau follows the PCIR pointer out of a short buffer and refuses the card
with `bios ctor failed -22`. The fault is the shadow copy, not the card.

It was read instead through the chip's own PROM window at `BAR0+0x300000`
(`0xFB300000`), and it is intact: `0x55AA`, PCIR at `0x3B53` naming
`10de:0020`, last-image indicator set, 131072 bytes, sha256
`34073870d7eb1ea22450b479b4ce7c98598f615c5cdc535340583f73dd0dc52e`. It is
archived content-addressed at
`bringupkit\roms\10de-0020\nvidia-riva-tnt-nv4-34073870d7eb.rom` with a
sidecar recording the capture. That path is stable in a way the run path is
not.

The PROM read and its restore are operator statements too; `rom-capture.ndjson`
here holds only the ordinary capture that aborted. Opening the PROM window
means clearing configuration bit `0x50` bit 0, which
also turns this card's video output off while it is clear. It was cleared and
restored inside the one run, verified through both configuration space and its
MMIO mirror at `0x1850`, and the card was left as found.

## What this capture did not do

No mode set, no VRAM access, no vendor unlock, no write characterization. VBE,
EDID and the DOS surveys are all `unsupported` for this run, not absent from
the card. Every writable column in the bundle's register table is empty for
that reason.
