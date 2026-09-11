# Cirrus Logic GD5430 baseline, aperture and VBE, on the card

BringupKit evidence behind
[The GD5430 reads its own memory size, and its BIOS mode table needs reading carefully](../../decisions/2026-09-11-cirrus-gd5430-memory-and-mode-table.md).
FX-6300 Debian host `bringup-target`, `1013:00a0` rev 47 at `0000:04:06.0`, no
kernel driver bound, boot `2cce947a-3d63-4cfa-8e6e-0a99a1df4acb`, captured
2026-09-11T08:07:41Z.

Copied out of `bringupkit\runs\cirrus-handoff` because that tree's paths get
reused; the ViRGE/DX bundle was re-run at the same path within the hour.
Identify this run by boot id, never by path.

| File | What it carries |
|---|---|
| `baseline.json` | the only copy of the full 256-index sweep of all three banks, under `raw.vendor.sweep` |
| `capture.ndjson` | the `register_sweep` event: standard range only, indices restored |
| `cirrus-aperture-open3.ndjson` | the aperture probe with the Cirrus linear window opened and restored: `sr07_before: 0`, `sr07_enabled: 0x10`, `sr07_after: 0`, `cirrus_linear_restored: true`, and `alias_period_bytes: 131072` |
| `cirrus-vbe-3.ndjson` | the `vbe_info` event under trust `emulated-int10`: 16 mode records, `total_memory_blocks: 16`, the 4F06h attempt, the 4F15h EDID attempt |
| `report.md` | the baseline verb's own coverage and side effects |
| `bundle-README.md` | the kit's write-up, verbatim |
| `sha256sums.txt` | covers the seven evidence files of the original bundle; the six copied here all verify |

`rom.bin` is not copied. The shadow image is 32768 bytes, sha256
`0020fc81418f82a3f34b4269fbb2d0366a6932e2a19fae788062ba5b64714609`, PCIR
naming `1013:00a0`, `CL-GD5440 VGA BIOS Version 1.00`, archived
content-addressed at
`bringupkit\roms\1013-00a0\cirrus-logic-gd5430-alpine-0020fc81418f.rom`.
It is the copy firmware shadowed at `0xC0000`, which POST may have patched,
not the ROM as shipped.

## The readings that matter

```
BAR0   0xFB000000   16 MiB aperture
CR11   0x9E         bit 7 set: CRTC write-protect on, as the BIOS left it
SR06   0x12         extension registers unlocked - the manual's readback value
SR07   0x00         bits 7:4 zero, so the linear aperture is closed as found
SR0F   0x11         bits 4:3 = 0b10 -> 1 MiB, 32-bit bus, on the '5430/'40 row
```

Alias periods, recomputed here from the sweep rather than taken from the
write-up: CRTC modulo 64, sequencer modulo 32, graphics modulo 64, each exact
across all 256 indexes. Above the standard range each bank holds 18 registers
reading non-zero, so this card's indexed vendor space is established by
content and not by inference from the modulus — which is what the NVIDIA
bundles could not do.

`SR06` is why that is a stronger statement here than there. It reads back
`0x12` when the bank is unlocked and `0x0F` when it is locked, so a bank of
zeros could not be confused with a locked one. The NV5's CRTC has no such
tell, which is why
[its record](../../decisions/2026-09-11-nvidia-nv4-and-nv5-baselines.md)
leaves the question open.

## One thing in the bundle's write-up is wrong

Its scan-line pitch warning names modes `0x110`, `0x113` and `0x112` as ones
where "a driver computing width x bytes-per-pixel will be wrong". Two of the
three are the arithmetic working correctly, and six modes that do break it are
not named. Recomputed from the mode records in `cirrus-vbe-3.ndjson`:

| Mode | bpp | Reported pitch | width x ceil(bpp/8) | |
|---|---|---|---|---|
| `0x110` 640x480 | 15 | 1280 | 1280 | flagged, but agrees |
| `0x113` 800x600 | 15 | 1600 | 1600 | flagged, but agrees |
| `0x112` 640x480 | 24 | 2048 | 1920 | flagged, and genuinely padded |
| `0x102` 800x600 | 4 | 100 | 800 | not flagged |
| `0x104` 1024x768 | 4 | 128 | 1024 | not flagged |
| `0x106` 1280x1024 | 4 | 256 | 1280 | not flagged |
| `0x014`, `0x109`, `0x10a` 132-col | 4 | 264 | 132 | not flagged |

A 15bpp pixel occupies two bytes; the warning computes one. The modes that
actually break the naive product are the planar 4bpp ones, where
bytes-per-scanline is per plane, and `0x112`, which is padded to a 2048-byte
boundary.

Two smaller ones. `SR0F` bits 4:3 are written as `0x10` in the write-up, which
is the binary value carrying a hex prefix — `0x10` is 16 and does not fit a
two-bit field; the value is `0b10`. And mode `0x102` is listed twice with no
comment, though the evidence records `duplicate_modes: [258]`: the BIOS really
does repeat it, and a driver enumerating modes will see it.

## Two things that look like contradictions and are not

`report.md` lists `vbe.controller` and `vbe.modes` as `unsupported
(no-real-mode-vbe)` while `bundle-README.md` prints a full VBE mode table. The
baseline verb did not do VBE; a separate run did, and its evidence is
`cirrus-vbe-3.ndjson`. The write-up merges the two and the report does not.

The BIOS reports 1 MiB of memory over 4F00h while the aperture probe measures
a 131072-byte alias period. Those measure different things, and the write-up
is right not to reconcile them: `SR07` bit 0 is clear, so the card is not in
extended 256-colour mode, and the Cirrus manual makes that the condition for
the aperture to map one-to-one onto display memory. The period belongs to the
text-mode mapping.

## What this capture did not do

No mode set, and so no measurement of installed memory — that needs a
256-colour mode to persist across two verbs, which no verb here does. No write
characterization: every writable column in the register table is empty. No
EDID; 4F15h was called and did not answer. 4F06h was called in text mode and
declined with `0x014F`, which settles nothing about a graphics mode.
