# TNT2 Model 64 (NV5) baseline, on the card

BringupKit evidence behind
[Two NVIDIA cards baselined, and what the VGA ports are not hiding](../../decisions/2026-09-11-nvidia-nv4-and-nv5-baselines.md).
FX-6300 Debian host `bringup-target`, `10de:002d` rev 15 at `0000:04:06.0`,
**no subsystem id at all** — subvendor and subdevice both read `0x0000` — no
kernel driver bound for the capture, boot
`2f78d05d-f028-47f6-9a91-6b4c44e84904`, captured 2026-09-11T07:01:35Z.

Copied out of `bringupkit\runs\tnt2-handoff` because that tree's paths get
reused; the ViRGE/DX bundle was re-run at the same path within the hour.
Identify this run by boot id, never by path.

| File | What it carries |
|---|---|
| `baseline.json` | the only copy of the full 256-index sweep of all three banks, under `raw.vendor.sweep`. The alias periods in the decision doc are computed from here and cannot be recomputed from anything else in this directory |
| `capture.ndjson` | the `register_sweep` event: standard range only, 25 CRTC, 5 sequencer, 9 graphics, indices restored |
| `tnt2-aperture-3.ndjson` | BAR1 at `0xF6000000`, 32 MiB decoded, `access_width: 4`, ten write offsets to 16 MiB, no wrap and no decode edge |
| `rom-capture.ndjson` | the shadow read, chunk by chunk, and the configuration state before and after |
| `report.md` | the kit's section coverage, side effects and uncertainty list |
| `bundle-README.md` | the kit's write-up, verbatim |
| `sha256sums.txt` | covers the six evidence files of the original bundle; the five copied here all verify |

## The readings that matter

```
BAR0        0xFB000000    16 MiB, MMIO register space
BAR1        0xF6000000    32 MiB aperture, >=32 MiB installed, 32-bit round-trip
CR11        0x8E          bit 7 set: CRTC write-protect on, as the BIOS left it
CR1F        0x00          NVIDIA's extended-CRTC lock register, unlock not attempted
```

CRTC indexes `0x19`-`0x7F` that read non-zero: `CR22=20` and `CR26=20`, the
standard read-only latch and attribute-index registers. **Nothing else in the
extended CRTC range reads anything but zero on this card**, where NV4 shows
fourteen live registers there. `SR05`-`SR07` and `GR09`-`GR0F` read zero on
both cards.

## The write-up was re-exported, and it is now wrong the other way

`bundle-README.md` here is the 11 September re-export. The first export
concluded from the alias modulus that this card has no indexed vendor register
space; the re-export withdraws that, correctly, and replaces it with "CR19–CR7F
are 103 distinct registers above the standard range, 103 of them reading other
than 0xFF: vendor space", and the same sentence for `SR05`-`SR07` and
`GR09`-`GR0F`.

The counts are right and check out against the sweep. The criterion is not:
`0x00` reads "other than 0xFF", so every one of those indexes qualifies while
reading nothing. And "distinct registers" takes a write to establish, which
this run did not do. See
[the decision doc](../../decisions/2026-09-11-nvidia-nv4-and-nv5-baselines.md).

The re-export also changed the rebind line in the operator notes from
`echo 0000:04:06.0 | sudo tee /sys/bus/pci/drivers/nouveau/bind` to
`echo 0000:04:06.0 > /sys/bus/pci/drivers/nouveau/bind`. **The new form does
not work.** The redirect is performed by the calling shell, so it fails on
permissions as a normal user, and `sudo echo ... > file` fails the same way
because sudo never sees the redirect. Use the `tee` form.

## The ROM is the shadow, and it is not in this directory

46080 bytes, sha256
`11fd3f198e315c7d016a9cc3c019635a86c2646f8bcde9e3effe9338c443d3a9`, PCIR
naming `10de:002d`, last image, so it is this card's BIOS: `NVIDIA TNT2 Model
64 VGA BIOS`, `Version 3.05.00.10B25`, `Chip Rev B1`, `Copyright (C) 1996-2000
NVidia Corp.`

It is the copy firmware shadowed at `0xC0000` for the POSTed adapter, which is
a RAM image POST may have patched. It is what int10 executes. It is not the
ROM as shipped, and no PROM-window read was done on this card to compare.
Archived content-addressed at
`bringupkit\roms\10de-002d\nvidia-tnt2-nv5-04-06-0-11fd3f198e31.rom`.

## Two things about this card's history

Reseated on 11 September 2026. Before the reseat, firmware skipped it
entirely: not POSTed, the bridge forwarded 1 MiB windows, and BAR0 answered
roughly half of reads — correct value or master abort, nothing in between.
That intermittency was the whole fault. After the reseat firmware POSTs it as
primary VGA, the windows are 32 MiB + 32 MiB, and nouveau brings it up
unaided.

The ROM read cleared the declared bridge's master-abort latch
**irreversibly**, and left the Expansion ROM BAR programmed with decode
disabled. Both are recorded in `report.md` under side effects.

## What this capture did not do

No mode set, no VRAM access, no vendor unlock, no write characterization. VBE,
EDID and the DOS surveys are all `unsupported` for this run, not absent from
the card. Every writable column in the bundle's register table is empty for
that reason.

nouveau was unbound for the capture. Rebind with
`echo 0000:04:06.0 | sudo tee /sys/bus/pci/drivers/nouveau/bind`.
