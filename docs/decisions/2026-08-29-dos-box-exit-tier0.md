# Tier-0 breaks the same way, on a different chip, and the stock driver does not

Date: 2026-08-29
Issue: [`docs/issues/2026-08-28-dos-box-entry-hang-gma950.md`](../issues/2026-08-28-dos-box-entry-hang-gma950.md)
Previous day's measurements: [`docs/decisions/2026-08-28-dos-box-exit-ninth-dot.md`](2026-08-28-dos-box-exit-ninth-dot.md)

Evidence: `claude\personal\v9x-86box-dosbox\2026-08-29-tier0\` - host-side
captures, the install-step screenshots, the DOS VBE inventory and the serial
captures.

## Why this run existed

Every measurement of the previous day was the **s3** family on an emulated S3
ViRGE. The fault was reported on the **vbe** tier-0 package on a GMA 950, so
the cross-family claim in the issue rested on "striping in both photographs" -
and the day's own measurement made that weaker, not stronger: the guest showed
a legible *text page* with a character-cell artefact, while the netbook
photograph is a corrupt *hi-res desktop*. The chip-specific fix that was
proposed (S3 register save/restore in the mini-VDD) is worth nothing if the
fault is chip-independent, so the family had to be varied on hardware that can
be measured.

## The machine

`Win98SE-VBE-Tier0` 86Box 6.0 guest, `ym430tx`, Pentium MMX 200, 128 MiB,
**ATI Mach64 VT2** (`gfxcard = mach64vt2`, 4 MB), Windows 98 SE, agent on host
port 9872, COM1 to the named pipe `native-s3-com1`.

Disk and NVR backed up first to
`86Box VMs\Velocity9x Backups\Win98SE-VBE-Tier0-pre-v9x-20260829\`.

The guest was on **ATI's own driver** (`display.drv=pnpdrvr.drv`,
"ATI Graphics Pro Turbo PCI (atim64 - VT)") with no Velocity9x install history:
no `C:\V9XDIAG`, no `V9XDISP.DRV`, no OEM INF.

### Tier-0 does drive this card

The vbe manifest warns that the ViRGE/DX BIOS ignores the generic
linear-framebuffer bit, so the card had to be checked before the install was
worth doing. `build\vbe-inventory\v9xvbe.exe` run from DOS reported VBE 2.0,
`Attributes=00BB` (bit 7, linear framebuffer) and `PhysicalBase=62400000` for
the 8bpp modes.

Worth keeping: **the same BIOS reports different memory at different times.**
From DOS it said `TotalMemory64K=8` - 512 KB - and refused a linear framebuffer
for every mode above 480 KB. At Windows time the mini-VDD's collection got
`mem=64` (4 MB) and *every* mode came back `a=00bb` with a base. The DOS-time
inventory understates what tier-0 actually gets on this card; do not use it to
rule a card out.

Installed through Have Disk with the INF's manual entry, "Velocity9x
VBE-generic display (any VESA VBE 2.0+ adapter)", which is what INSTALL.TXT
prescribes for a card that is not the QEMU std-vga the auto-bind ids name. The
expected "not written specifically for the selected hardware" warning appeared
and was accepted. Cold start, then:

```
V9X-MINI vbe-collect done
V9X-DRV load build=vbeTrace1
V9X-DRV lfb=0xE6000000 bytes=0x00400000
V9X-DRV enable-ok mode=1024x768x16 lfb-mapped
```

22 modes published, `Stage=enable-ok`, clean 1024x768 desktop.

## Measured: tier-0 fails identically

`vbeTrace1` (vbe family, `-DosBoxTrace`), desktop 1024x768x16.

| Leg | Result |
|---|---|
| Windowed DOS box | fine |
| `ALT+ENTER` in | **clean** - 720x400 text at 70 Hz, 0 lit columns, agent up |
| `ALT+ENTER` out | **80 lit columns, gap `9 x 79`**, DOS text legible, no desktop, agent dead |

The exit signature is the same number the s3 family produced on the ViRGE:
80 columns at the 9-pixel character cell. `C:\V9XDIAG\V9XBOOT.INI` carries
**no `DosBox=` key**, so as on s3, none of the display driver's nine trace
points fired on either leg.

The desktop mode is not a variable either: s3 measured this at 640x480x16 and
tier-0 at 1024x768x16, and the count and period are identical.

## The control: ATI's own driver survives the same round trip

Same VM, same card, same emulator, same 1024x768x16 desktop, driver switched
back to "ATI Graphics Pro Turbo PCI (atim64 - VT)" and cold started.

- Windowed box: fine.
- Full screen: clean, 0 lit columns.
- **Exit: the desktop comes back.** The emulator window returns to 1024x768,
  the DOS box is a window on an intact desktop
  (`captures\ctrl-ati-returned3.png`), and the agent answered `ping` on all
  five checks after the return.

## What this establishes

| Driver | Family | Chip | Entry | Exit |
|---|---|---|---|---|
| Velocity9x | s3 | S3 ViRGE/DX | clean | 80 cols @ 9 px, no return, wedge |
| Velocity9x | vbe tier-0 | ATI Mach64 VT2 | clean | 80 cols @ 9 px, no return, wedge |
| ATI 4.02 | - | ATI Mach64 VT2 | clean | desktop returns, agent alive |

1. **The fault is ours.** The stock driver completes the round trip on the same
   emulated hardware, so 86Box's VGA emulation is not producing the artefact.
   This reproduces the netbook's standard-VGA control on a machine that can be
   driven from a script.
2. **It is family-independent and chip-independent** across the two chips that
   can be measured here. Not S3-specific, not tier-0-specific, not
   Intel-specific.
3. **The chip-specific explanation is dead.** An artefact identical to the
   pixel on an S3 ViRGE and an ATI Mach64 cannot be "S3 extension registers
   left in a hi-res configuration" - a Mach64 has no such registers. The
   character cell is standard VGA text state, which is what the *main* VDD
   restores, and the same wrong picture on two unrelated chips says the cause is
   in the shared path.
4. **So the s3 register save/restore proposal loses most of its value.** It was
   recommended here yesterday on a mechanism this measurement rules out. It may
   still be worth doing for the s3 family on its own merits, but it is not a
   candidate fix for this issue.

## What it does not establish

**That this is the netbook's fault.** Both measured cases are 86Box, and the
netbook's photograph is a corrupt hi-res desktop with a moving mouse cursor -
not a legible text page with a ninth-dot artefact. One machine still has to
confirm the exit leg and the period, and it needs a person at the keyboard.

## Where a fix can live now

The shared path is what is left, and the parts of it that are ours are small:

- What the driver tells the main VDD at registration -
  `VDD_DRIVER_REGISTER`'s flags, `VDD_SAVE_DRIVER_STATE`, and the
  re-registration on a live mode switch. One flag has been measured and was
  null (see the previous day's experiment 4).
- The VESA linear-framebuffer mode itself, which the VDD did not set and knows
  nothing about. Structural to tier-0 and not changeable.
- Preventing the switch from outside the VDD, which needs no chip knowledge and
  is the only route that protects tier-0.
