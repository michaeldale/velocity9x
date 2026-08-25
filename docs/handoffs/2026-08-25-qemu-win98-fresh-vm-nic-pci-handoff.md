# Fresh Win98 QEMU VM handoff — NIC and PCI findings, 2026-08-25

Date: 2026-08-25
Branch: `dynamic-vbe-stage0`

This continues [`2026-08-23-qemu-win98-stage1-guest-handoff.md`](2026-08-23-qemu-win98-stage1-guest-handoff.md)
(read it and its 2026-08-25 addendum first). The decision from this session:
**abandon the `win98-clean-agent061b.qcow2` overlay and build a fresh guest**
from the untouched clean base, applying the fixes below in the right order.
The Stage 1 exit gate (installed-guest dump vs the QEMU DOS inventory
fixture) is still unfulfilled.

## Root cause discovered this session

The guest could never reach the network because **Windows 98 was not
enumerating the PCI bus at all**:

- Device Manager → System devices → **Plug and Play BIOS** was broken with
  **Code 24** ("device not present / not working"). While that devnode is
  dead, none of its PCI children are ever enumerated — so QEMU's PCI NIC
  (`ne2k_pci`, and later `rtl8139`) never appeared, no New Hardware wizard
  ever fired, and the Network control panel refused to add the Realtek
  adapter manually ("You have selected a Plug and Play adapter…").
- Fix that worked: Plug and Play BIOS → Properties → Driver → Update
  Driver → "Display a list…" → **Show all hardware** → Manufacturer
  **(Standard system devices)** → Model **PCI bus** → accept the
  "not written specifically for this hardware" warning → copy source
  `D:\SETUP\WIN98` (the wizard first tries drive E: and errors — cancel that
  and type the path; it specifically wanted `pcimp.pci`).
- After Finish, Windows asks to restart. This session answered **No** and did
  a clean Start → Shut Down instead (per the no-warm-reset rule).
- On the next cold boot the guest **BSOD'd once (continue option), then hung;
  a later cold boot reached the desktop** but the boot chain never became
  reliable again (QEMU exited/crashed at least once right after reaching the
  desktop). That instability is why the overlay is being abandoned rather
  than debugged further.

Better plan for the fresh VM: apply the PCI-bus driver fix (and let Windows
do its restart *by its own choice* — or shut down and cold boot) **before**
installing anything else, so the re-enumeration storm happens on an
otherwise clean system.

## NIC decision

- Switch from `ne2k_pci` to **`rtl8139`** (`-device rtl8139,netdev=n0`).
  Confirmed as the standard working Win98 NIC for QEMU (see
  https://gist.github.com/brunocastello/bd6b4daa13165251bf0419c5209d2644).
- Win98 SE has **no in-box RTL8139 driver**. The driver floppy is already
  downloaded and verified:
  `C:\QemuVMs\Win98SE-QEMU-StdVGA-Clean\rtl8139-drivers.img`
  (1,474,560 bytes, from
  https://archive.org/download/rtl-8139-full-drivers/PCI_100M_ethernet_drivers.img).
  Attach it as A: with
  `-drive 'file=C:\QemuVMs\Win98SE-QEMU-StdVGA-Clean\rtl8139-drivers.img,format=raw,if=floppy,index=0'`.
  Its Win98 driver layout was not inspected — browse A:\ in the wizard
  (may be in a subdirectory).
- Keep the user-net hostfwd unchanged: host `127.0.0.1:9872` → guest `9869`.

## Launcher state

`build\vm-clean\launch-stage1.ps1` was updated this session and is the
reference command line:

- Now parameterized: default is a **visible SDL window**; `-Headless`
  restores `-display none` plus `-S` (paused; resume with HMP `c`).
- `-Nic` parameter, default `rtl8139`.
- The RTL8139 driver floppy is attached as A:.
- Everything else unchanged from the Stage 1 handoff: QEMU 4.2.0 at
  `C:\QemuVMs\Tools\qemu-4.2.0`, `-machine pc -no-acpi -cpu pentium2 -m 128`,
  `-vga std`, HMP telnet on 127.0.0.1:55559, serial to
  `build\vm-clean\stage1-com1.log`, vvfat transfer disk
  `build\vm-transfer-stage1-agent` as D:.

For the fresh VM, create a new overlay over the untouched base (do NOT touch
`C:\QemuVMs\Win98SE-QEMU-StdVGA-Clean\win98-clean.qcow2`):

```powershell
& 'C:\QemuVMs\Tools\qemu-4.2.0\qemu-img.exe' create -f qcow2 `
  -b 'C:\QemuVMs\Win98SE-QEMU-StdVGA-Clean\win98-clean.qcow2' `
  'C:\QemuVMs\Win98SE-QEMU-StdVGA-Clean\win98-clean-fresh1.qcow2'
```

then point the `-drive file=` in `launch-stage1.ps1` at the new overlay.

Note: the clean base was installed with the same `-machine pc -no-acpi`
QEMU 4.2 setup, and its PnP BIOS devnode is presumably Code 24 there too —
expect to need the PCI-bus fix on any overlay of it.

## Recommended install order on the fresh VM

1. Cold boot, let Windows settle at the desktop.
2. Fix **Plug and Play BIOS → PCI bus** (steps above, source
   `D:\SETUP\WIN98`). Shut down cleanly. Cold boot.
3. Expect one messy re-enumeration boot (possible one-time BSOD → continue →
   if it hangs, cold boot again). Windows should now detect PCI devices,
   including the RTL8139 — feed it drivers from **A:** and CABs from
   `D:\SETUP\WIN98`.
4. Verify Network control panel: RTL8139 present, TCP/IP bound to it
   (DHCP default is fine — QEMU user-net serves 10.0.2.x). WINIPCFG should
   show the Realtek adapter, not just PPP Adapter.
5. Install V9x Remote Agent 0.6.1: run `INSTALL.BAT` **with `D:\AGENT` as
   the current directory** (not from the Run dialog directly).
6. Shut down cleanly, cold boot headless, verify from the host:

   ```powershell
   & 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' `
     -NoProfile -ExecutionPolicy Bypass `
     -File 'C:\everything\claude\personal\v9x-remote-agent\scripts\v9xctl.ps1' `
     ping -Host 127.0.0.1 -Port 9872 -Json
   ```

7. Then continue at "After the agent responds" in the Stage 1 handoff
   (agent-driven V9XSTAGE.EXE, VBE package install from `D:\DRIVER`,
   `C:\V9XBOOT.INI` vs the QEMU DOS inventory fixture).

## Faults hit this session (avoid repeating)

- **Blind sendkey driving is fragile.** Keyboard focus in Win9x dialogs is
  unpredictable (Tab landed on the *Remove* button in Device Manager; Enter
  on a tree item pressed the default OK and closed the whole dialog; a
  down-arrow was swallowed before a driver-database rebuild and Enter picked
  the wrong list entry). Screenshot-verify between every keystroke, or
  better, have Michael drive the visible window for GUI-heavy steps.
- In Device Manager the **Properties button responds to Alt+R** (and Alt+R
  is NOT Refresh); Refresh is reached with Tab/arrow + Space.
- The guest **hung once mid-session** (taskbar clock frozen) while blind
  driving; cold-relaunch was the only recovery.
- Shutdown was blocked by a **"V9x Remote Agent not responding"** End-Task
  prompt — the agent runs at startup even with no working TCP/IP; expect
  that prompt on every shutdown until the NIC works.
- `hmp-keys.ps1` cannot send `@` (QEMU key name is `shift-2`); it also maps
  `<` incorrectly to `comma` (harmless so far).
- QEMU `screendump` writes PPM; `scratchpad\ppm2png.ps1` (this session's
  scratchpad) converted them — recreate it or view PPMs directly if needed.
- The agent ping error mode matters: "connection refused" = QEMU not
  running/no forward; "did not properly respond / forcibly closed" = QEMU up
  but guest TCP/IP not reachable on 9869.
- Do not modify files under `build\vm-transfer-stage1-agent` while QEMU has
  the vvfat disk mounted.

## Disk inventory update

- `win98-clean-agent061b.qcow2` — **abandoned** as of this session (PCI
  re-enumeration instability after the PnP BIOS fix). Keep for forensics or
  delete.
- `win98-clean.qcow2` — untouched clean base, the parent for the fresh
  overlay. Never modify.
- `rtl8139-drivers.img` — new, keep.
- Other overlays (`win98-clean-qemu42-test`, `win98-clean-agent061`,
  `setup-source.*`) — unchanged from the Stage 1 handoff; still do not
  continue from them. `build\vm-clean\setup-source.raw` (8 GiB) remains
  deletable once the network install is confirmed.
