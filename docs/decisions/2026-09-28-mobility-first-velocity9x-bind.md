# Rage Mobility-M: first Velocity9x bind, and what it measured

Date: 2026-09-28. Machine: Gateway Solo 2150 (10.0.1.22), ATI Rage
Mobility-M `1002:4C4D`, subsystem `107B:2150`, revision `64`, Windows 98 SE.
Evidence: [`../probe/ati-rage-mobility-m-first-bind-2026-09-28/`](../probe/ati-rage-mobility-m-first-bind-2026-09-28/).

The Phase 0-4 work ran with ATI's stock driver bound (`ati2drab.drv` /
`ati2vxab.vxd`) and the diagnostics loaded beside it. This is the first boot
with Velocity9x bound to the device. It was installed remotely through
Display Properties, Adapter, Change, Have Disk, operated with the agent's
input injection. The stock class key was exported first
(`STOCK-DISPLAY-CLASS.REG`).

## 1. Have Disk ignores an INF with long section names

The generated INF was refused with "The specified location does not contain
information about your hardware", even with Show all hardware selected.
That wording means setup found no display INF at all, not a model mismatch.
The same INF with its per-chip sections shortened was read at once and
listed the model as compatible (`HAVE-DISK-SHORT-SECTIONS.png`). The section
names were:

| Section | Length | Result |
|---|---:|---|
| `Velocity9x.Install.rage-mobility-m` | 34 | ignored |
| `Velocity9x.Registry.rage-mobility-m` | 35 | ignored |
| `V9x.Install.mobility` / `V9x.Registry.mobility` | 20 / 21 | read |
| `Velocity9x.Install.mach64-vt2` (VT2 guest, earlier) | 29 | read |

The exact limit was not bisected; it lies between 29 and 34. The generator
now names multi-chip sections `V9x.Install.<chip>` and
`V9x.Registry.<chip>`, and `Assert-V9xInf` refuses any section name longer
than 29 characters. The patched copy also led with the SUBSYS-qualified
id. The manifest now declares `SubsystemId = '2150107B'`, but this session
did not isolate whether the bare id alone would have matched.

## 2. PCIRebalance let Windows move the BARs, and the VBE base went stale

The generator wrote `PCIRebalance=1` for every family, and ATI's stock
class key has no such value. At the first Velocity9x boot (boot 12),
Windows reassigned the device's resources:

| | Stock driver, and boot 13 | Boot 12, `PCIRebalance=1` |
|---|---|---|
| BAR0 (framebuffer) | `F5000000` | `0B000000` |
| BAR2 (MMIO) | `F4100000` | `0C020000` |

The VBE mode information still reported `PhysBasePtr = F5000000`, and the
ATI family takes its aperture from VBE because it has no `read_aperture`
hook. The driver therefore mapped an address the card no longer decoded.
GDI read back all ones, so every agent screenshot was white
(`BOOT12-WHITE-READBACK.png`), while the registers at the new BAR2 still
read `CONFIG_CHIP_ID 64004C4D` (`ATIMM-BOOT12-RELOCATED.TXT`).

Setting `PCIRebalance="0"`, by a one-value REGEDIT `/S` import with no
deletes, and rebooting put both BARs back where the BIOS placed them. The
driver enabled at `F5000000` and the desktop drew normally
(`ATIMM-BOOT13-NOREBALANCE.TXT`, `BOOT13-DESKTOP.png`).

`PCIRebalance` tells Windows that the driver can follow moved resources.
Only a family that reads its aperture from the PCI BAR can, so the
generator now emits it only when `MiniVddVbeCollect = $false`, which is
exactly the families with a `read_aperture` hook. S3 keeps it. ati, vbe
and intel-gma no longer write it. Existing installs keep whatever their
registry already holds, because AddReg does not delete.

## 3. What a bare VBE boot leaves in the engine

The read-only fingerprint under Velocity9x (boot 13), compared with the
stock-driver capture of 2026-09-27:

| Register | Stock driver | Velocity9x |
|---|---|---|
| `GEN_TEST_CNTL` | `000000A0` | `00000000` |
| `BUS_CNTL` | `7B33A001` | `7333A001` |
| `CRTC_GEN_CNTL` | `03002400` | `4B002400` |

`GEN_TEST_CNTL` `A0` is `GEN_CUR_EN` plus a GPIO bit. `GEN_GUI_RESETB` is
clear in both, so it is not the difference. `BUS_CNTL` bit 27 is
`BUS_EXT_REG_EN`, which enables register block 1, where the setup engine
lives. X.Org sets it for every chip from 264VT onward. The stock driver had
set it, which is why every Phase 3-4 scene worked. After a bare VBE boot it
is clear. The HAL now sets it once, after the chip identity check, in
`eng_mach64.c`'s validate. See the companion issue for why this matters.
