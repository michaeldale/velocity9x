# Ironlake EDID from Windows: the BIOS read fails, and GMBUS gets NAKs

Date: 2026-10-22
Status: **measured on one machine; the probe has no positive control
yet.** W9QI2F4F (`M-J0F0SJ`), boot 56, merged main `cd04a7b8`.
Captures: `docs\probe\w9qi2f4f-ironlake-edid-2026-10-22\`.

## The question

Tier-0 and every intel-gma boot record `Edid=none`. The 2026-09-28 DOS
survey of this board read a valid EDID through VBE 4F15h (Samsung
Odyssey G5, 2560x1440). Either DDC fails from Windows on this BIOS, or
the monitor no longer answers.

## The VBE read

Every boot's `VbeCache` line in V9XBOOT.INI ends `f=0407`. The status
bits are `V9X_VBE_ST_EDID_FAILED` (0400h) plus the controller, list and
cache bits. So the mini-VDD's 4F15h BL=00h (capabilities) succeeds and
BL=01h (read block 0) does not. The returned AX is not recorded.

## The GMBUS read: V9XG5DD

`tools\diag\gen5_ddc_probe.asm` (G5DD.VXD) and
`gen5_ddc_probe_win32.c` (V9XG5DD.EXE) read 128 bytes at I2C address
50h, index 0, through the PCH's GMBUS. No BIOS is involved. The sequence
is Linux 6.6 intel_gmbus.c's, checked against the Ironlake PRM Vol 3
Part 3 2.2.3:

1. Take the INUSE semaphore.
2. Reset the controller locally.
3. Write GMBUS0 = pin pair.
4. Start an index, no-stop, wait cycle of 128 bytes.
5. Read 32 dwords on HW_RDY.
6. Generate a STOP.
7. Restore GMBUS0 and the semaphore.

It writes only GMBUS0-2 and asks pin pairs 2-6:

- 2, the analog monitor's DDC;
- 3, LVDS;
- 4-6, ports C, B and D, Reserved in the PRM and used by i915.

| Pair | Outcome | GMBUS2 after |
|---|---|---|
| 2 (analog) | NAK | 8600h |
| 3 (LVDS) | NAK | 8600h |
| 4 (port C) | NAK | 8600h |
| 5 (port B) | NAK | 8600h |
| 6 (port D) | NAK | 8600h |

Nothing acknowledges address 50h on any pair. The machine stayed up and
the desktop was unaffected.

The display runs CPU pipe A to PCH transcoder A to the CRT DAC (ADPA)
(`2026-10-22-ironlake-gen5-read-only-mmio.md`). A Samsung Odyssey G5 has
no VGA input, so the monitor is reached through a VGA adapter or is not
the same monitor. Either would explain a silent DDC: many active
VGA-to-digital adapters do not pass DDC through.

## What this does not show

- That the probe reads a monitor that does answer. The intended control
  was W9QI4E14 (Ivy Bridge, Panther Point PCH, GMBUS at the same C5100h,
  a monitor that answered 4F15h on 2026-10-21). Its IGD config space now
  reads FFFFFFFFh at 00:02.0, and the known-good V9XIVMM reads the same,
  with GGC 0003h where its audit read 0211h. That board has changed,
  perhaps a discrete card with the IGD disabled, and the control was not
  run. The VxD accepts 0162h/0166h for when it can be.
- Why the 2026-09-28 DOS read worked. Nobody has asked what monitor and
  cable are attached now.

## Next

Ask the tester what is plugged into which port. If it is VGA through an
adapter, the NAKs are the adapter's. Run V9XG5DD on any PCH machine with
a known-good EDID before treating its NAKs as proof.
