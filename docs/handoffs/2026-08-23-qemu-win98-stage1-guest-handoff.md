# Dynamic VBE Stage 1 guest handoff — QEMU 4.2, Win98 SE, remote-agent setup

Date: 2026-08-23  
Branch: `dynamic-vbe-stage0`  
Stage 1 implementation commit: `43fcb3a Implement bounded mini-VDD VBE enumeration`

This handoff continues
[`2026-08-23-dynamic-vbe-stage0-handoff.md`](2026-08-23-dynamic-vbe-stage0-handoff.md).
The Stage 1 host/build implementation is committed and its package preflight
passes in Windows 98. The remaining Stage 1 exit gate is still the installed
guest dump versus the DOS QEMU inventory. Work stopped while finishing TCP/IP
for V9x Remote Agent 0.6.1.

No Windows product key is recorded in this document.

## Current live state

- QEMU is running now (`qemu-system-i386.exe`, observed PID 67040) with its HMP
  monitor on `127.0.0.1:55559`.
- The guest is at the Windows 98 **Network** control-panel dialog. Its component
  list currently contains Client for Microsoft Networks, Microsoft Family
  Logon, Dial-Up Adapter, and TCP/IP. It does **not** contain the Realtek
  adapter. `WINIPCFG` consequently offers only `PPP Adapter` with `0.0.0.0`.
- The QEMU device is `ne2k_pci`, which Windows 98 identifies as
  **Realtek RTL8029(AS) PCI Ethernet NIC**.
- Host TCP `127.0.0.1:9872` is forwarded to guest TCP `9869`.
- V9x Remote Agent **0.6.1** is successfully installed under
  `C:\V9XREMOTE`, but it is not reachable until the physical adapter is added
  to the Network control panel and TCP/IP is bound to it.
- The current guest overlay is:
  `C:\QemuVMs\Win98SE-QEMU-StdVGA-Clean\win98-clean-agent061b.qcow2`.
  It is disposable and backed by the untouched clean base.

The screenshot proving the exact stopping point is generated and ignored at
`build\vm-clean\netcpl.png`.

## Exact next action

Continue in the already-open **Network** dialog:

1. Choose **Add**.
2. Choose **Adapter**, then **Add**.
3. Select manufacturer **Realtek** and model
   **Realtek RTL8029(AS) PCI Ethernet NIC**.
4. If Windows asks for its CD, enter `D:\SETUP\WIN98` as the copy source.
5. Confirm the Network component list now includes the Realtek adapter and
   TCP/IP is bound to it, then accept the dialog.
6. Shut Windows down cleanly. Do not warm-reset it: this guest has hung at the
   Windows splash after warm `system_reset`. Quit QEMU only after Windows has
   reached its powered-off black screen, then cold-launch it again with the
   same command below.
7. Verify the agent from the host:

   ```powershell
   & 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' `
     -NoProfile -ExecutionPolicy Bypass `
     -File 'C:\everything\claude\personal\v9x-remote-agent\scripts\v9xctl.ps1' `
     ping -Host 127.0.0.1 -Port 9872 -Json
   ```

Once `ping` succeeds, use the agent for the driver installation and collection
instead of QEMU `sendkey`.

## Current cold-launch command

Run this with the required permission to access the VM directory:

```powershell
& 'C:\QemuVMs\Tools\qemu-4.2.0\qemu-system-i386.exe' `
  -L 'C:\QemuVMs\Tools\qemu-4.2.0' `
  -machine pc -no-acpi -cpu pentium2 -m 128 `
  -drive 'file=C:\QemuVMs\Win98SE-QEMU-StdVGA-Clean\win98-clean-agent061b.qcow2,format=qcow2,if=ide,index=0,media=disk' `
  -drive 'file=fat:rw:C:\everything\velocity9x\build\vm-transfer-stage1-agent,format=raw,if=ide,index=1,media=disk' `
  -vga std `
  -netdev 'user,id=n0,hostfwd=tcp:127.0.0.1:9872-:9869' `
  -device 'ne2k_pci,netdev=n0' `
  -monitor 'telnet:127.0.0.1:55559,server,nowait' `
  -serial 'file:C:\everything\velocity9x\build\vm-clean\stage1-com1.log' `
  -boot c -rtc base=localtime -display none
```

Important: the transfer disk is QEMU `vvfat:rw`. Do not change its host files
while QEMU has it mounted. Shut the VM down and quit QEMU first.

## What has been proved

### QEMU downgrade

- System QEMU 11.1.0 remains installed and untouched at `C:\Program Files\qemu`.
- QEMU 4.2.0 was extracted side-by-side to
  `C:\QemuVMs\Tools\qemu-4.2.0` from the Stefan Weil Windows archive linked by
  QEMU's official download page.
- The downloaded installer's SHA-512 matched its published checksum.
- `qemu-system-i386.exe --version` reports
  `QEMU emulator version 4.2.0 (v4.2.0-11797-g2890edc853-dirty)`.
- QEMU 11 repeatedly stalled during normal Windows 98 boot. QEMU 4.2 reached
  the Win98 password dialog and desktop on the same clean image.
- QEMU 4.2 uses `-machine pc -no-acpi`; it rejects the newer
  `-machine pc,acpi=off` syntax.

The external launcher and ignored workspace template were updated to prefer
QEMU 4.2:

- `C:\QemuVMs\Win98SE-QEMU-StdVGA-Clean\start-vm.ps1`
- `build\vm-clean\start-vm.ps1`

They currently launch the clean base without networking; use the full command
above for this agent-enabled overlay.

### Stage 1 package

The VBE package was rebuilt successfully with:

```powershell
$PSNativeCommandArgumentPassing = 'Legacy'
.\scripts\build-active-package.ps1 -Family vbe -BuildId stage1
```

The `Legacy` setting matters with the installed PowerShell/Open Watcom pair.
Without it, the quoted `V9X_BUILD_ID` define loses the intended native argument
quoting and Open Watcom fails on the first source with E1139, "Command line
contains more than one file to compile."

Output is at `build\win98se-vbe`. The resulting package was mounted and
`V9XSTAGE.EXE` reported:

> PASS: the VxD and Win16 display DRV loaded together and unloaded cleanly. No
> display mode was changed.

The proof screenshot is generated and ignored at
`build\vm-clean\preflight.png`.

### Remote agent

- Use the latest local 0.6 release, currently **0.6.1**, from:
  `C:\everything\claude\personal\v9x-remote-agent\build\install`.
- Do not use the older release folder
  `build\github-mirror\build\release\v9xremote-0.5.2`.
- The 0.5.2 installer was tried first and failed after creating
  `C:\V9XREMOTE`: it tested a directory with bare `IF EXIST`, which Win98
  `COMMAND.COM` reports incorrectly. Version 0.6.1 fixes this by testing
  `C:\V9XREMOTE\NUL`.
- Version 0.6.1 then installed successfully. Its proof screenshot is generated
  and ignored at `build\vm-clean\agentfresh.png`.
- Run `INSTALL.BAT` with `D:\AGENT` as the current directory. Launching the
  batch directly from the Run dialog leaves `C:\WINDOWS` as the working
  directory, so its relative package-file checks fail.

## Transfer disk contents

The single mounted transfer tree is:

`build\vm-transfer-stage1-agent`

It contains:

- `DRIVER\` — the Stage 1 `build\win98se-vbe` package;
- `AGENT\` — V9x Remote Agent 0.6.1 installer files;
- `SETUP\WIN98\` — 101 original Win98 SE setup files (about 127 MB), including
  the networking CABs.

The setup files were extracted from the preserved setup disk. QEMU became
unstable when that second qcow2 disk and the transfer disk were both attached,
so the files now live on the one transfer disk instead.

The temporary flattened raw setup disk is
`build\vm-clean\setup-source.raw` (8 GiB logical). It is no longer needed once
the network install is confirmed and can be removed later, but it was not
deleted during this session.

## VM disk inventory and recovery

- Clean, untouched installed base:
  `C:\QemuVMs\Win98SE-QEMU-StdVGA-Clean\win98-clean.qcow2`.
- Current good working overlay:
  `win98-clean-agent061b.qcow2`.
- `win98-clean-qemu42-test.qcow2` contains the first successful QEMU 4.2 boot,
  a successful Stage 1 preflight and Agent 0.6.1, but a canceled NIC copy left
  its network stack incomplete (`vnetsup.vxd`, `vredir.vxd`, `dfs.vxd` missing).
  Do not continue from it.
- `win98-clean-agent061.qcow2` was an abandoned fresh attempt made before the
  setup files were consolidated onto one transfer disk. Do not continue from
  it.
- `setup-source.qcow2` remains preserved and backed by the older VM disk.

Do not modify or replace `win98-clean.qcow2`.

## After the agent responds

1. Set the controller path for this session:

   ```powershell
   $env:V9X_AGENT_CTL = 'C:\everything\claude\personal\v9x-remote-agent\scripts\v9xctl.ps1'
   ```

2. Re-run `V9XSTAGE.EXE` through the agent if desired, then install the actual
   VBE package from `D:\DRIVER` (or push it into a dedicated
   `C:\V9XREMOTE\JOBS\stage1` directory first).
3. Preserve the current overlay before first driver association if another
   rollback point is wanted.
4. Cold-boot with serial capture.
5. Retrieve `C:\V9XBOOT.INI` and the serial log, and compare the Stage 1
   controller/status/mode records against the QEMU DOS inventory fixture
   record-for-record. This is the unfulfilled Stage 1 exit gate.
6. Only after that comparison passes, begin Stage 2 (runtime table consumed by
   GDI) from `docs\plans\dynamic-vbe-pipeline.md`.

## Repository state

Before adding this handoff, the only visible untracked path was the pre-existing
`.claude\` directory. The Stage 1 implementation itself is clean and committed
as `43fcb3a`. This handoff is intentionally left for the next session to review
and commit.
