# A local GT218 under Linux nouveau: the channel that fails on Win98 works here

Date: 2026-11-13

Step 0 of [docs/plans/nvidia-gt218-linux-trace.md](../plans/nvidia-gt218-linux-trace.md).
Read over SSH from the BringupKit target (GA-78LMT-USB3, FX-6300, Debian
13, kernel 6.12.107+deb13-amd64) on its first boot with Michael's
GeForce 210 fitted. Nothing was written to the card by us; Debian loaded
nouveau by itself. The kit's collector was not run, because nouveau was
bound (see below).

## Measured

- `02:00.0` 10DE:0A65 rev A2, subsystem 1043:83F4 (ASUS), with its HDMI
  audio function `02:00.1`. Behind root port `00:02.0`. The same device
  id and revision as M-BXMFA8's card, another vendor's board.
- BAR0 `FB000000` 16 MiB, BAR1 `C0000000` 256 MiB, BAR3 `DE000000`
  32 MiB, I/O `BF00`. The same sizes as M-BXMFA8's.
- Not the boot VGA device (`boot_vga` 0; the onboard Radeon 3000 is 1).
  The firmware left its command register 0 ("enabling device (0000 ->
  0003)"), so whether the firmware POSTed it is not known; nouveau's init
  ran on it either way.
- nouveau's log: `NVIDIA GT218 (0a8280b1)`, the same PMC_BOOT_0 as
  M-BXMFA8's (`0A8280B1`); `bios: version 70.18.5f.00.06`; `fb: 1024 MiB
  DDR3` (M-BXMFA8's has 512 MiB).
- **nouveau's channels work on this chip**: `drm: MM: using COPY for
  buffer copies`, `Initialized nouveau 1.4.0`, `fb0: nouveaudrmfb`, and
  no PFIFO or PGRAPH error. It also logs `failed to create ce channel,
  -22`: the copy-engine channel is refused, and it falls back to the
  PGRAPH M2MF copy. The machine stayed up.
- `CONFIG_MMIOTRACE=y` in Debian's kernel. nouveau is not in the
  initramfs (`lsinitramfs` finds none), so a modprobe.d blacklist is
  enough to keep it from loading at boot.
- The network interface is renamed by the extra bus: `enp3s0` without the
  card, `enp4s0` with it. `/etc/network/interfaces` now has a DHCP
  stanza for each.

## Not measured, and why

- The root port's capabilities (AER or not): `lspci -vv` needs root, and
  the SSH account has passwordless sudo only for the kit's agent verbs.
- The kit's collector and `bar-read`: they refuse a device with a bound
  driver, by design.
- With `fbcon=map:0` and nouveau as fb0, the console is on the GT218
  this boot. The kit's notes record a machine hang from unbinding a
  driver in that state (bringupkit CARD-SWAP, the s3fb hang), so nouveau
  was not unloaded.

## What it means

The fault on M-BXMFA8 is in what the probe does, not in the chip
family: the same chip id runs nouveau's kernel channel here. The trace
of that load is the reference the probe needs. It has to come from a
boot where nouveau is blacklisted and then loaded under mmiotrace;
`tools/diag/gt218-nouveau-mmiotrace.sh` does that and needs root.
