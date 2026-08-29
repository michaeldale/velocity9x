# 86Box's Trio32 publishes 8811, so the shipping driver already drove it - and its BIOS is not the Trio64's

Date: 2026-08-29
Branch: `s3-device-id-aliases`
Preceding: [`2026-08-29-s3-device-id-survey.md`](2026-08-29-s3-device-id-survey.md)

Guest: `Win98SE-Trio32` (new, cloned from `Win98SE-Trio64`), 86Box 6.0,
`gfxcard = s3_trio32_pci`, 2 MiB, agent port 9875. Driver build
`485f516-dirty`.

## Why this run

The option-ROM survey established that a Trio32 BIOS claims `5333:8811`, the id
the `trio64` chip already binds. A ROM says what the board it came from
claimed; it does not say what an emulator publishes. This is the boot that
closes that gap, and it is the only one of the five ids touched by this branch
that any machine has executed.

## Measured

**The id.** `C:\V9XDIAG\V9XHW.INI`, with the guest running the emulated Trio32:

```
Adapter=S3 Trio32/64 86C764
VendorId=5333
DeviceId=8811
VideoMemoryBytes=2097152
VideoMemoryStatus=valid
ClockStatus=valid
CoreClockKHz=52798
MemoryClockKHz=52798
```

The PCI scan matched the `trio64` entry. **The `8810` alias added on this
branch did not fire and is still unexercised** - 86Box's Trio32 reports `8811`,
exactly as `86c732p.bin` does. The Trio32 was supported before this branch
existed; nothing here made it work.

CR36 decoded the configured 2 MiB correctly, so the memory decode is not
Trio64-specific either.

**The modes.** The nine declared modes that fit 2 MiB were run through
`run-vm-mode-matrix.ps1`, reboot path, with the GDI acceleration phase:

| Mode | Result |
|---|---|
| 640x480x8, 800x600x8, 1024x768x8, 1280x1024x8 | PASS |
| 640x480x16, 800x600x16, 1024x768x16 | PASS |
| 640x480x32 | PASS |
| **800x600x32** | **fell back to 640x480** |

The two modes larger than 2 MiB - 1280x1024x16 and 1024x768x32 - were not run.
That refusal is already measured on the physical 2 MiB Trio64 (BARRY,
[`2026-08-26-s3-physical-pipeline-inert.md`](2026-08-26-s3-physical-pipeline-inert.md)
§7) and re-measuring it here would add nothing.

## What the one failure is, and what it is not

`C:\V9XDIAG\V9XBOOT.INI` reports `Stage=fail-hardware-vbe-mode`. That is a
refusal at INT 10h 4F02h, after `ValidateMode` accepted the mode - and it
should have: 800x600x32 is 1,920,000 bytes and fits 2 MiB with room over.
`V9XMODES.INI` shows the row published normally, `Row0a=m=0115
g=800x600x32 p=3200 ... src=baseline`.

Two controls in the same run say what this is:

* **640x480x32 (VBE 0112h) passed**, so the part and the driver do 32 bpp.
* **800x600x16 (VBE 0114h) passed**, so 800x600 timing is fine.

So the failure is specific to VBE mode number **0115h**: 86Box's Trio32 ROM
(`86c732p.bin`) does not list it. This is exactly the case the shared S3 mode
table's comment already names - "a VRAM check catches a mode too large, not a
mode missing, so that row would validate and then fail at 4F02h" - previously
seen only as BARRY's missing 0100h. It is now measured on a second ROM, and it
is filed as [`docs/issues/2026-08-29-trio32-lacks-vbe-0115.md`](../issues/2026-08-29-trio32-lacks-vbe-0115.md).

**It is not a regression from this branch.** The Trio32 has bound to the
`trio64` chip entry, and inherited its twelve-row mode list, since the family
merge. Nothing about the aliases changed which modes this card is offered. What
changed is that somebody finally booted one.

**It is not an argument against the alias mechanism.** It is an argument for
the rule the mechanism encodes: an inherited mode list is a reason to bind an
id, not a measurement of what that silicon will do. The same doubt now applies,
unmeasured, to 8810, 8812, 8813, 8814 and 8901.

## The clone, for the record

Cloning notes in [`docs/vm-environment.md`](../vm-environment.md) list four
traps. Trap 1 - the "moved or copied" modal - behaved differently here and is
worth correcting: 86Box 6.0 draws its dialogs in Qt, so the modal has **no
native window text and no clickable native button**; enumerating the process's
windows returns class `Q` controls with no captions, and it cannot be answered
programmatically the way a Win32 dialog could.

What cleared it was giving the clone its own NE2000 MAC. The first (blocked)
start had already rewritten `mac` in the copied `86box.cfg`, and the next start
came up with the window title `Win98SE-Trio32 - 86Box 6.0` and no modal at all.
So the check is tied to the inherited network identity, and a clone that gets a
fresh MAC before its first start should not raise it.

Traps 2, 3 and 4 held as documented: fresh `uuid` plus a `vmm.ini` section,
`serial1_device` off `pipe` and onto a file of its own, and one pre-quoted
`-ArgumentList` string.

The guest still reports the parent's `ComputerName` (`WIN98-S3NATIVE`), like
every other clone in the fleet. Identify it by port.

## Not established

Whether physical Trio32, Trio64V+ or Trio64V2 silicon behaves as the emulator
does. This is one emulated part with one ROM.
