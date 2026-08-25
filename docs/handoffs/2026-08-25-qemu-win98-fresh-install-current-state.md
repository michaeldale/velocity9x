# Fresh Windows 98 SE QEMU VM — current state and rebuild handoff

Date: 2026-08-25  
Workspace: `C:\everything\velocity9x`  
VM state at handoff: **shut down; do not launch or modify it as part of this handoff**

## Outcome

A genuinely fresh Windows 98 SE VM was installed on a new independent qcow2
disk. PCI enumeration was repaired, the QEMU RTL8139 NIC was installed, DHCP
worked, and V9x Remote Agent 0.6.1 answered through the host port forward.

The Velocity9x VBE Stage 1 package was then associated with QEMU Standard VGA.
Its exact driver files are present and the PCI display devnode points to the
Velocity9x display class entry, but Windows still boots through the 4-bpp VGA
fallback. The Stage 1 exit gate is therefore **not complete**.

## Active disk and backups

Active independent system disk (`C:` in the guest):

`C:\QemuVMs\Win98SE-QEMU-StdVGA-Clean\win98se-stage1-fresh.qcow2`

This disk has no backing file. It was created as a blank 2 GiB qcow2; it is not
an overlay of the old clean image.

Backups:

1. Initial installed-system backup:
   `C:\QemuVMs\Win98SE-QEMU-StdVGA-Clean\win98se-stage1-fresh-backup-20260825.qcow2`
   SHA-256:
   `114FFDF2918851872D929EA533FA2CB703B28AF4A37CC018E051A29703E950C7`
2. Post-PCI/RTL8139 backup, before the Velocity9x display association:
   `C:\QemuVMs\Win98SE-QEMU-StdVGA-Clean\win98se-stage1-fresh-post-rtl8139-20260825.qcow2`
   SHA-256:
   `5B04A95FF4BAA4326997A59C206BBD6961C4F7B26F54902ABDCFF97177BB4D2E`
3. After Velocity9x was associated but before its first boot:
   `C:\QemuVMs\Win98SE-QEMU-StdVGA-Clean\win98se-stage1-pre-vbe-firstboot-20260825.qcow2`
   SHA-256:
   `E107578BD8D9A1260B5C679162BCD08D6D999029109392E654926B2D542C810A`

Each backup was created with `qemu-img convert -O qcow2` while QEMU was stopped
and verified against the active disk with `qemu-img compare`.

The untouched historical base remains:

`C:\QemuVMs\Win98SE-QEMU-StdVGA-Clean\win98-clean.qcow2`

It was not used as a backing file for this fresh installation and must remain
unchanged.

## Launcher and VM configuration

Launcher:

`build\vm-clean\launch-fresh-install.ps1`

Phases:

- `Setup`: attaches the historical setup-source disk through `snapshot=on`,
  startup floppy as `A:`, and RTL8139 driver floppy as `B:`.
- `Runtime`: system disk only; no secondary IDE disk, therefore no guest `D:`.
  The RTL8139 driver floppy remains `A:`.
- `Transfer`: attaches `build\vm-transfer-stage1-agent` as guest `D:` and the
  RTL8139 driver floppy as `A:`.

Normal Stage 1 continuation command, when work resumes:

```powershell
.\build\vm-clean\launch-fresh-install.ps1 -Phase Transfer
```

Validated VM settings:

- QEMU 4.2.0 from `C:\QemuVMs\Tools\qemu-4.2.0`.
- Machine: explicit `pc-i440fx-4.2` (i440FX + PIIX3).
- `-no-acpi`.
- CPU: `pentium2`.
- RAM: 128 MiB.
- Primary storage: IDE qcow2 at index 0.
- Display: QEMU Standard VGA (`-vga std`).
- NIC: PCI RTL8139 with fixed MAC `52:54:00:98:13:39`.
- Network backend: QEMU user networking, IPv4 on and IPv6 off.
- Host forward: `127.0.0.1:9872` to guest TCP port `9869`.
- HMP monitor: `127.0.0.1:55559`.
- RTC: local time.
- Visible SDL display by default.
- Transfer disk: QEMU `vvfat:rw`; never change its host files while QEMU has it
  mounted.
- Serial log: `build\vm-clean\fresh-install-com1.log` (QEMU truncates it on
  every launch).

## Reproducible fresh-install procedure

### 1. Blank disk and DOS setup

Create a new independent disk, not an overlay:

```powershell
& 'C:\QemuVMs\Tools\qemu-4.2.0\qemu-img.exe' create -f qcow2 `
  'C:\QemuVMs\Win98SE-QEMU-StdVGA-Clean\win98se-stage1-fresh.qcow2' 2G
```

The installation used:

- `win98se-boot.img` as startup floppy `A:`;
- `rtl8139-drivers.img` as driver floppy `B:`;
- `setup-source.qcow2` as IDE `D:` using `snapshot=on`, because QEMU 4.2 IDE
  disks cannot be exposed with `readonly=on`;
- the RTL8139 PCI device present from the beginning of Setup.

From the startup floppy:

1. Run `FDISK`, enable large-disk support, create a maximum-size primary DOS
   partition, and make it active.
2. Exit FDISK and perform a full cold boot rather than a warm reset.
3. Format and make the new partition bootable:

   ```bat
   FORMAT C: /S /V:WIN98
   ```

4. Copy the Windows installation files locally so later CAB prompts do not
   depend on the temporary source disk:

   ```bat
   MD C:\WIN98
   COPY D:\WIN98\*.* C:\WIN98
   C:
   CD \WIN98
   SETUP /IS
   ```

5. Once graphical Setup began, the boot floppy was ejected through HMP. Setup's
   later boots therefore fell through to `C:`.
6. The user supplied their licensed Windows 98 SE product key.

### 2. PCI enumeration repair

The newly installed guest inherited the known Windows 98 Code 24 condition on
**System devices -> Plug and Play BIOS**. While this devnode was broken, Windows
could not enumerate PCI children, including QEMU Standard VGA and RTL8139.

Repair steps:

1. Device Manager -> System devices -> Plug and Play BIOS -> Properties.
2. Driver -> Update Driver.
3. Display a list -> Show all hardware.
4. Manufacturer `(Standard system devices)`.
5. Model `PCI bus`.
6. Accept the compatibility warning.
7. When Windows requests `pcimp.pci`, use `C:\WIN98` as the copy source.
8. Decline Restart and shut Windows down normally.
9. Cold boot. Windows then enumerated the i440FX bridge, PIIX devices, QEMU VGA,
   RTL8139, and PCI IRQ holders.

### 3. RTL8139 and networking

During PCI re-enumeration:

- RTL8139 driver files came from `A:`.
- Windows networking/CAB files came from `C:\WIN98`.
- The installed adapter identifies as `Realtek RTL8139 C+ Fast Ethernet NIC`.
- `WINIPCFG` showed DHCP address `10.0.2.15`.
- QEMU user networking uses gateway `10.0.2.2` and DNS `10.0.2.3`.

A clean shutdown and cold boot completed the network setup. A post-RTL8139
backup was then taken (listed above).

### 4. V9x Remote Agent 0.6.1

The VM was booted with `-Phase Transfer`, which mounted the project transfer
tree as `D:`. In a Windows DOS box:

```bat
D:
CD \AGENT
INSTALL.BAT
```

It is important that `INSTALL.BAT` runs with `D:\AGENT` as the current
directory.

After a clean shutdown and cold boot, the host check passed:

```powershell
& 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' `
  -NoProfile -ExecutionPolicy Bypass `
  -File 'C:\everything\claude\personal\v9x-remote-agent\scripts\v9xctl.ps1' `
  ping -Host 127.0.0.1 -Port 9872 -Json
```

Verified response facts:

- `Success=true`
- `AgentBuild=rel-061`
- Agent version 0.6.1
- Host endpoint `127.0.0.1:9872`

## Velocity9x Stage 1 work completed

The transfer package at `D:\DRIVER` is the VBE Stage 1 package, despite stale
S3-oriented wording in its generic `INSTALL.TXT`. Its `MANIFEST.TXT` correctly
states:

- Build `stage1`
- Target `PCI\VEN_1234&DEV_1111`
- QEMU/Bochs VBE Standard VGA
- Boot trace enabled

The preflight was run through the remote agent:

`D:\DRIVER\V9XSTAGE.EXE`, working directory `D:\DRIVER`

It displayed:

> PASS: the VxD and Win16 display DRV loaded together and unloaded cleanly. No
> display mode was changed.

Velocity9x was installed through Device Manager -> Display adapters -> Update
Driver -> Have Disk from `D:\DRIVER`, selecting:

`Velocity9x VBE-generic display (QEMU std-vga)`

The installed guest files were retrieved and hash-compared. They are exact
matches for the Stage 1 package:

- `C:\WINDOWS\SYSTEM\V9XDISP.DRV`
- `C:\WINDOWS\SYSTEM\V9XMINI.VXD`

Registry evidence also confirms the PCI association:

- PCI devnode:
  `PCI\VEN_1234&DEV_1111&SUBSYS_11001AF4&REV_02\BUS_00&DEV_02&FUNC_00`
- `DeviceDesc=Velocity9x VBE-generic display (QEMU std-vga)`
- `Driver=Display\0001`
- `Mfg=Velocity9x`
- `ConfigFlags=0`
- `Display\0001\DEFAULT\drv=v9xdisp.drv`
- `Display\0001\DEFAULT\minivdd=v9xmini.vxd`
- Registered mode rows exist for 640x400, 640x480, 800x600, and 1024x768 at
  8 bpp, plus 640x480, 800x600, and 1024x768 at 16 bpp.

## Current unresolved display state

Windows continues to boot at 640x480 in 4 bpp (16 colors). Display Properties
shows:

`QEMU Monitor on Standard Display Adapter (VGA)`

and offers no 256-color choice.

The active hardware-profile settings were corrected using the same pattern as
`scripts\run-vm-mode-matrix.ps1`:

```text
HKLM\System\CurrentControlSet\Services\Class\Display\0001\DEFAULT
  Mode=8,640,480

HKLM\Config\0001\Display\Settings
  BitsPerPixel=8
  Resolution=640,480
  RefreshRate=0
  UpgradeToDefaultMode deleted
```

The correction was exported and verified before shutdown. It also remained in
the registry after the following cold boot, but Windows still selected 4 bpp.
Therefore this is not a failure to persist the requested mode.

Current diagnostics:

- Agent screenshot source depth: 4 bpp.
- Agent screen size: 640x480.
- `C:\V9XBOOT.INI` contains only:

  ```ini
  [Velocity9x]
  Stage=query-ok
  ```

- No `VbeCache`, `VbeController`, or indexed `VbeModeNN` records exist.
- `build\vm-clean\fresh-install-com1.log` is empty for these boots.
- `SYSTEM.INI` correctly contains `display.drv=pnpdrvr.drv`.
- The active hardware profile remains at `BitsPerPixel=8` even though the live
  desktop is 4 bpp.
- `C:\BOOTLOG.TXT` shows Windows loading and starting the root fallback devnode
  `ROOT\*PNP0900\0000` as `Standard Display Adapter (VGA)`, then loading
  `vga.drv`. It also shows `Display1` device-init success, but no named
  `V9XMINI.VXD` load line and no Velocity9x serial trace.

Collected host artifacts:

- `build\vm-clean\cannot-select-256.png`
- `build\vm-clean\guest-PCI.REG`
- `build\vm-clean\guest-DISPLAY.REG`
- `build\vm-clean\guest-HKCC.REG`
- `build\vm-clean\guest-SYSTEM.INI`
- `build\vm-clean\BOOTLOG-stage1.TXT`
- `build\vm-clean\V9XBOOT-firstboot.INI`
- `build\vm-clean\V9XBOOT-stage1.INI`
- `build\vm-clean\stage1-force-640x480x8.reg`
- `build\vm-clean\stage1-mode-verify.reg`
- `build\vm-clean\stage1-mode-afterboot.reg`
- retrieved guest copies `guest-V9XDISP.DRV` and `guest-V9XMINI.VXD`

The next investigation should explain why the root fallback display devnode and
`vga.drv` remain active even though the PCI QEMU VGA devnode is correctly bound
to `Display\0001`. Do not repeatedly boot or reinstall before studying the
registry exports and `BOOTLOG-stage1.TXT`.

## Resolution addendum — same day, later session

The "Current unresolved display state" above is resolved. Root cause: the root
fallback devnode `ROOT\*PNP0900\0000` (Standard Display Adapter (VGA),
`Display\0000`) started at every boot, held the VGA I/O ranges, and left the
PCI Velocity9x devnode with runtime **Problem 12 (resource conflict)** in
`HKEY_DYN_DATA\Config Manager\Enum`. Windows normally deletes that root
devnode when a PCI display driver is installed; here it survived.

What did not work:

- Device Manager -> Remove on the root devnode: the Confirm Device Removal
  dialog reproducibly ignored both mouse clicks and Enter (three attempts,
  one of which left the shell unstable and required a cold boot).
- Setting `ConfigFlags=1` (CONFIGFLAG_DISABLED) via a `.REG` import: the
  devnode did report Problem 22 (disabled) on the next boot, but its boot
  allocation was retained and the PCI devnode still had Problem 12.

What worked — real-mode registry deletion via AUTOEXEC.BAT:

```bat
@ECHO OFF
IF NOT EXIST C:\DELPNP.FLG GOTO SKIP
C:\WINDOWS\REGEDIT /D HKEY_LOCAL_MACHINE\Enum\Root\*PNP0900
DEL C:\DELPNP.FLG
:SKIP
```

with `C:\DELPNP.FLG` created as the one-shot trigger. Real-mode REGEDIT /D
deletes Enum keys that protected-mode regedit refuses. After the next cold
boot the key was gone (verified by a failed export), the flag was consumed,
and the guard block in `C:\AUTOEXEC.BAT` is now inert (it can be removed at
leisure).

Result on the following boot:

- `C:\V9XBOOT.INI`: `Stage=enable-ok`,
  `VbeCache=s=1835 l=92 q=74 c=64 p=0 f=0047`, `VbeController` and 64
  `VbeModeNN` records, `VbeDetail=ok`.
- The desktop runs on the Velocity9x driver at 640x480x8 (agent screenshot
  source depth 8 bpp).
- **Stage 1 exit gate comparison: PASS.** All 64 guest records match the DOS
  inventory fixture (`personal/v9x-qemu-stdvga/QSTDVGA.INI`) exactly — mode,
  attributes, geometry, bpp, memory model, LFB pitch, base `FD000000`, RGB
  masks — with BIOS list order preserved. The 29 absent DOS records are the
  legacy text/planar modes, the banked 4-bpp VESA modes, the seven 15-bpp
  modes plus mode 0013, and the tail dropped at the 64-record cache cap with
  the overflow reported in the flags. One footnote: the mini-VDD reports
  `l=92` listed where the DOS tool counted 93; every record still matches.
- `V9XGDI.EXE` PASS: "display writes, BitBlt and pixel readback are
  coherent", color bars correct. (First shell paint after the driver took
  over showed artifacts in the Welcome window's dithered watermark; the GDI
  test passes, so treat that as a Welcome-bitmap rendering quirk to keep an
  eye on, not a framebuffer fault.)
- The same gate had already passed earlier the same day on the UTM (Mac)
  Win98 VM at 10.0.1.250:9869, whose SeaBIOS lists 93 modes — same 64
  records, zero mismatches.

Note for the packaging backlog: Win98's Have Disk refused the generated INF's
bare `PCI\VEN_1234&DEV_1111` id on the UTM machine (HardwareIDs carry
`SUBSYS_11001AF4&REV_02`); the guest copy there was hand-patched. The INF
generator should emit the SUBSYS-qualified id plus the bare id.

Stage 1 is complete on both guests. Next: Stage 2 (runtime table consumed by
GDI) per `docs\plans\dynamic-vbe-pipeline.md`.

## Operational cautions

- Never use guest Restart or QEMU `system_reset`. Warm restarts repeatedly hang
  at SeaBIOS. Use Start -> Shut Down, close QEMU, then cold-launch.
- QEMU 4.2 occasionally exits before guest execution or stalls at SeaBIOS. An
  empty serial log plus no guest progress indicates a host-side startup failure;
  one cold retry has worked. Do not confuse that with a Velocity9x boot failure.
- Windows shutdown sometimes leaves QEMU running. Confirm the guest is at the
  powered-off black screen before issuing HMP `quit` or terminating a lingering
  QEMU process.
- Do not edit `build\vm-transfer-stage1-agent` while `-Phase Transfer` is
  running because it is mounted through `vvfat:rw`.
- Preserve the three qcow2 backups above. The post-RTL8139 backup is the clean
  rollback point before any Velocity9x display association.
- The Stage 1 exit gate remains open until an installed-guest
  `C:\V9XBOOT.INI` contains the bounded VBE controller/status/mode records and
  those records match the established QEMU DOS inventory fixture
  record-for-record.
