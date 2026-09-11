# ViRGE/DX extended registers and aperture, on the card

BringupKit evidence behind
[CR36 and the aperture base, confirmed on a physical ViRGE/DX](../../decisions/2026-09-11-virge-dx-registers-confirmed-on-silicon.md).
FX-6300 Debian host, `5333:8a01` at `0000:04:06.0`, no kernel driver bound,
boot `b8ee9ffc-d9f9-4970-a6af-324bc6cb567c`.

Copied out of `bringupkit\runs\handoff-virge-dx` because that tree's paths get
reused. **That is not a hypothetical: this bundle was re-run at the same path
within the hour, and the record citing it had to be corrected.** The earlier
boot was `8d292e50-3ff0-4a29-85dd-20a857b0100c`; every register value and
aperture reading here is identical between the two, which is why the findings
are recorded as reproduced rather than merely re-cited. Identify a run by boot
id, never by path.

| File | What it carries |
|---|---|
| `capture.ndjson` | the `s3_extended_read` event: CR2D/2E/2F/30/31/36/37/40/58/59/5A and six sequencer registers, read under `descriptor:s3-virge-dx-v1` and relocked |
| `aperture.ndjson` | BAR0 at `0xD8000000`, 64 MiB decoded, `access_width: 4`, `readback_faithful: true`, and the 4 MiB decode edge |
| `modeprobe.ndjson` | `0x117` set and restored, and the 4F06h subfunction results in a graphics mode |
| `vbe.ndjson` | VBE 1.2, the OEM string, and the `scan_line` event whose `"called": false` is why the bundle no longer claims the BIOS lacks 4F06h |
| `bundle-README.md` | the kit's write-up, as corrected |
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

## One thing in the bundle's write-up is still wrong

Its 4F06h headline has been corrected and is now right: the calls did not
complete under emulation, which is a fact about asking in text mode and not
about the BIOS lacking the function.

Its `BL=02h` paragraph has not. It calls status `0x014f` a success and warns
of "a success code and an unchanged pitch", but `AH=0x01` is failure - that
text is carried over from the Matrox run, where the byte form genuinely did
return `0x004f` and do nothing. The driver calls the pixel form either way.
