# ViRGE/DX extended registers and aperture, on the card

BringupKit evidence behind
[CR36 and the aperture base, confirmed on a physical ViRGE/DX](../../decisions/2026-09-11-virge-dx-registers-confirmed-on-silicon.md).
FX-6300 Debian host, `5333:8a01` at `0000:04:06.0`, no kernel driver bound,
boot `8d292e50-3ff0-4a29-85dd-20a857b0100c`.

Copied out of `bringupkit\runs\handoff-virge-dx` because that tree's paths get
reused - the Matrox bundle at a sibling path was overwritten between two runs
on the same day. Identify a run by boot id, not by path.

| File | What it carries |
|---|---|
| `capture.ndjson` | the `s3_extended_read` event: CR2D/2E/2F/30/31/36/37/40/58/59/5A and six sequencer registers, read under `descriptor:s3-virge-dx-v1` and relocked |
| `aperture.ndjson` | BAR0 at `0xD8000000`, 64 MiB decoded, `access_width: 4`, `readback_faithful: true`, and the 4 MiB decode edge |
| `modeprobe.ndjson` | `0x117` set and restored, and the 4F06h subfunction results in a graphics mode |
| `vbe.ndjson` | VBE 1.2, the OEM string, and the `scan_line` event whose `"called": false` refutes the bundle's own "does not implement 4F06h" |
| `sha256sums.txt` | covers the 13 files of the original bundle, not this subset |

## The readings that matter

```
CR36 0x12   bits 7:5 = 0 -> 4 MiB by v9x_s3_virge_decode_memory_size
            decode edge measured at 4194304 bytes, all-ones past it
CR59 0xD8   BAR0 base 0xD8000000 - the aperture base registers hold the BAR
CR5A 0x00
CR58 0x03   bit 4 clear: aperture disabled, as expected in text mode
CR2D 0x8A   chip id, matching the PCI 8A01
CR2E 0x01
```

The extended read unlocks with SR08=0x06, CR38=0x48, CR39=0xA5 and relocks
both CR39 and CR38 to zero afterwards; `relocked: true`, indices restored.

## Do not trust the bundle's 4F06h section

Its headline says this BIOS does not implement 4F06h. `vbe.ndjson` says
`"called": false` for that probe, and `modeprobe.ndjson` then uses 4F06h
successfully in mode `0x117`. The decision record explains both.
