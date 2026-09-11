# MGA-2064W: aperture, memory and a mode set

BringupKit evidence behind
[the 2064W's aperture opens with mgamode](../../decisions/2026-09-11-the-2064w-aperture-opens-with-mgamode.md).
FX-6300 Debian host, card at `0000:04:06.0`, no kernel driver bound, boot
`195e37e1-bf59-4176-a743-e60810f581ca`.

**Copied here because the source path gets reused.** These files came from
`bringupkit\runs\handoff-matrox-2064w-full`, which is the same path the
2026-09-10 record cites for a *different* run - boot
`adde426f-e25f-48b3-9a25-a9e9fb75468c`, 14 files. That run's evidence is gone.
Distinguish the two by boot id, not by path.

| File | What it holds |
|---|---|
| `aperture.ndjson` | the one that matters: CRTCEXT3 `0x00` → `0x80` → `0x00`, `access_width: 4`, `readback_faithful: true`, `aliased: false`, alias period 8388608 |
| `modeprobe.ndjson` | `0x117` set and restored through the card's own BIOS, and the 4F06h byte-versus-pixel results |
| `vbe.ndjson` | 4F00h and the 24-mode list, 20 of them with a linear framebuffer at `0xFD000000` |
| `ramdac.ndjson` | TVP3026 confirmed by its own ID register (`0x3f` reads `0x26`), plus the three PLLs as found |
| `report.md` | the kit's baseline report, whose `aperture.probe` and `vbe.*` sections are marked unsupported - the verbs above ran separately |
| `bundle-README.md` | the kit's own write-up of the run |
| `sha256sums.txt` | covers the 15 files of the original bundle, not this subset |

## The three readings worth quoting

```
aperture   mgamode_before false -> mgamode_set true -> mgamode_restored true
           access_width 4, readback_faithful true, aliased false
           alias_period_bytes 8388608
modeprobe  entry_mode 0x0003, requested 0x0117, set_status 0x4f,
           active_mode 0x0117, final_mode 0x0003, mode_restored true
           length_before 2048 bytes / 1024 pixels / 4096 scan lines
vbe        total_memory_blocks 128, oem "Matrox Graphics Inc.", vbe 2.0
```

Only the aperture figures are direct MMIO. The mode list and the mode set run
the card's firmware on an emulated CPU with I/O passed through, which the kit
labels `emulated-int10`.

## Side effects this run had

Framebuffer contents disturbed, CRTCEXT3 bit 7 written and restored, and the
declared bridge's master-abort latch cleared **irreversibly**. The VBE info
verb provoked a master abort while running. None of it is a mode the card was
left in: it is back in mode 3 with mgamode clear.
