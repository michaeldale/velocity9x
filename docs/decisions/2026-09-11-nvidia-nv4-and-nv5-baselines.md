# Two NVIDIA cards baselined, and what the VGA ports are not hiding

Date: 2026-09-11. Two BringupKit runs on the FX-6300 Debian host
`bringup-target`, both cards at `0000:04:06.0`, no kernel driver bound for the
capture.

- Riva TNT (NV4), `10de:0020` rev 04, subsystem `1092:0550` — a Diamond Viper
  V550, BIOS 1.93E. Boot `355018cc-c1e1-4658-951d-1a42017c9c6f`, 07:33:01Z.
  Evidence in
  [`docs/probe/nvidia-riva-tnt-nv4-2026-09-11`](../probe/nvidia-riva-tnt-nv4-2026-09-11/).
- TNT2 Model 64 (NV5), `10de:002d` rev 15, no subsystem id at all, BIOS
  3.05.00.10B25, Chip Rev B1. Boot `2f78d05d-f028-47f6-9a91-6b4c44e84904`,
  07:01:35Z. Evidence in
  [`docs/probe/nvidia-tnt2-model64-nv5-2026-09-11`](../probe/nvidia-tnt2-model64-nv5-2026-09-11/).

Neither card is supported by this driver and no code changes with this record.
It exists so the next NVIDIA session starts from measurements instead of from
the bundles' summaries, one of which is arguing past its evidence.

## Both cards leave the CRTC write-protected

`CR11` reads `0x8E` on both. Bit 7 is set, so writes to `CR00`–`CR06` are
discarded silently — no error, no effect — until it is cleared. `CR11` itself
is writable. Any mode-set path has to clear it first. This is the same trap
already handled for S3 parts, and it is not chip-specific.

## The apertures are lower bounds, not sizes

| | NV4 | NV5 |
|---|---|---|
| BAR0 (MMIO) | `0xFB000000`, 16 MiB | `0xFB000000`, 16 MiB |
| BAR1 (aperture) | `0xFA000000`, 16 MiB | `0xF6000000`, 32 MiB |
| Widest faithful access | 4 bytes | 4 bytes |
| Alias period | none observed | none observed |
| Decode edge | none observed | none observed |

Every probed offset holds distinct storage that round-trips 32-bit accesses:
to 8 MiB on the NV4, to 16 MiB on the NV5. So installed memory is at least
16 MiB and at least 32 MiB respectively — and no more than that can be said,
because the wrap that would fix the size exactly lies outside the aperture in
both cases. This is the opposite situation to the ViRGE/DX, where a decode
edge inside the aperture pinned the memory at 4 MiB.

The NV5 figure has one independent check: nouveau's own read of the memory
controller reports 32 MiB SDRAM. The NV4 figure has none.

The 4-byte round-trip matters for the same reason it did on the 2064W and the
ViRGE/DX: the DIB engine writes dwords.

## The alias moduli, recomputed

Recomputed here from the 256-index sweeps in each bundle's `baseline.json`,
not taken from the bundle write-ups:

| Bank | NV4 | NV5 |
|---|---|---|
| Sequencer | aliases modulo 8, exactly | aliases modulo 8, exactly |
| Graphics | aliases modulo 16, exactly | aliases modulo 16, exactly |
| CRTC | modulo 128 at 255 of 256 indexes | aliases modulo 128, exactly |

`SR08` is `SR00` and `GR10` is `GR00`. What that establishes is the width of
the index the chip decodes, and nothing about what sits inside the period.

## The bundles said no vendor space, then said the opposite; neither claim is measured

The first export of both write-ups concluded "there is no vendor register
space behind this port pair" from the alias modulus. Both were re-exported on
11 September against a corrected exporter, which withdraws that and calls the
same indexes vendor space: on the NV5, "CR19–CR7F are 103 distinct registers
above the standard range, 103 of them reading other than 0xFF", and the same
sentence for `SR05`–`SR07` and `GR09`–`GR0F` on both cards.

The withdrawal is right. An alias modulus of 128 says `CR80` mirrors `CR00`;
it says nothing at all about `CR19`–`CR7F`, which lie inside the period. That
was the bad inference and it is gone.

The replacement does not survive either, because "reading other than 0xFF" is
satisfied by `0x00`. Recomputed from the sweeps:

| Range | NV4 | NV5 |
|---|---|---|
| `CR19`–`CR7F`, 103 indexes | 102 non-`0xFF`, **16 non-zero** | 103 non-`0xFF`, **2 non-zero** |
| `SR05`–`SR07`, 3 indexes | 3 non-`0xFF`, **0 non-zero** | 3 non-`0xFF`, **0 non-zero** |
| `GR09`–`GR0F`, 7 indexes | 7 non-`0xFF`, **0 non-zero** | 7 non-`0xFF`, **0 non-zero** |

So the sequencer and graphics "vendor space" on both cards, and the CRTC
vendor space on the NV5, rest entirely on indexes that read `0x00`. The two
non-zero CRTC reads on the NV5 are `CR22=0x20` and `CR26=0x20`, the standard
read-only latch and attribute-index registers, which are not vendor space at
all.

Nor is "distinct registers" measured. Telling 103 separately decoded registers
apart from one undecoded region that returns zero takes a write and a
read-back, and no write characterization was run on either card.

The one place the evidence does carry the claim is the NV4's CRTC, which reads
fourteen non-zero registers in that range:

```
CR1A=3D CR1B=83 CR1C=18 CR1F=03 CR20=20 CR21=FE CR23=04 CR27=01
CR2C=14 CR34=2D CR39=FF CR3B=20 CR3E=2E CR3F=32
```

That is positive evidence: the range is populated on NV4 silicon and reads
back content. The NV5 shows nothing there. Believing NVIDIA emptied the range
between NV4 and NV5 is no better supported than the alternative below.

Both sweeps ran with no unlock attempted. Each `baseline.json` records
`raw.vendor.unlock_method`, `unlock_sequence` and `lock_state` as `null`: the
kit matched no descriptor for family `10de`, so it swept the banks as found.

**Hypothesis, not measured:** NVIDIA parts gate the extended CRTC behind
`CR1F` — `0x57` unlocks, `0x99` locks — which is the convention in the public
nouveau and xf86-video-nv sources, not something this run observed. NV4 reads
`CR1F=0x03` and NV5 reads `CR1F=0x00`, neither of which is the unlocked value.

The test that settles it: write `0x57` to `CR1F` on the NV5, re-sweep
`0x19`–`0x3F`, restore. If those indexes come alive, the re-export's
conclusion is right and its stated reason is not. If they stay zero, the range
is empty or gated some other way. Either result is worth more than both
exports of the write-up, because it is the first write this silicon will have
taken.

Until then: **the NV4 has CRTC vendor space, the NV5's is unmeasured, and
neither card has measured sequencer or graphics vendor space.**

## One NV4 index breaks the modulo-128 alias, and one sample cannot say why

`CR34` reads `0x2D` and `CRB4` reads `0x40`. Every other index in the bank
matches its `+128` mirror. Two readings fit:

- `CRB4` is separate storage from `CR34`.
- They are one register whose value changed between the two reads, i.e. it is
  live rather than static.

A single sweep cannot distinguish them, and the second is the cheaper
explanation given the other 255 indexes alias cleanly. The test is to read
`CR34` twice in succession without touching anything else.

## What the evidence kills

**"The NV4 card is faulty."** It is not. Firmware POSTs it as primary VGA, the
bridge forwards both BARs, the aperture round-trips 16 MiB, and the ROM read
through the chip's PROM window is intact — `0x55AA`, PCIR at `0x3B53` naming
`10de:0020`, last-image indicator set. What fails is the legacy shadow copy at
`0xC0000`, which has no signature; the kernel's own shadow read returns EIO
and nouveau follows the PCIR pointer out of a short buffer and gives up with
`bios ctor failed -22`. Untested suggestion from the operator:
`nouveau.config=NvBios=PROM`.

**"The TNT2 card is faulty."** It was a seating fault. Before the reseat on
11 September firmware skipped the card, the bridge forwarded 1 MiB windows,
and BAR0 answered roughly half of reads with a master abort. After reseating,
nouveau brings it up unaided.

**"The alias modulus tells you whether vendor space exists."** It does not,
and both exports of both write-ups turned on believing it did — first to deny
the space, then to assert it. The modulus bounds the index width. What is
inside the period is a separate question that a read-only sweep answers only
where it finds non-zero content.

What is *not* killed: an NVIDIA backend may still turn out to need MMIO
through BAR0 rather than the indexed-port shape that `src/chipsets/s3`,
`matrox` and `ati` are built on. The NV4's fourteen CRTC registers point the
other way, though, and nothing here has driven either card. This is an open
architectural question, not a finding.

## What is still unknown

No mode set, no VRAM write characterization, no vendor unlock, no VBE, no
EDID, no DOS survey — all `unsupported` for these runs rather than absent from
the cards. Every writable column in both register tables is empty. Neither
card has been in a Windows 9x guest.

No family manifest is proposed on this evidence, and none should be until at
least one write to this silicon has been characterized.

## Gates

Documentation only; no source file changed. `scripts/check-tree.ps1` run.
