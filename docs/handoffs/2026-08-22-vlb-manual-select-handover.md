# VLB manual-select handover: the model installs, the devnode will not start

Date: 2026-08-22
Branch: `vlb-manual-select-inf`, 10 commits, `716420d`..`a1709d0`, not merged.
Working tree clean. `run-checks` green across all four families.
Machine: **486VLB is healthy** — desktop up, agent reachable, boots unattended.

Supersedes the running notes in
[the first-driver-run handoff](2026-08-21-vlb-first-driver-run.md), which was
written and corrected as the session went and is kept only for the reasoning
trail. Where the two disagree, this one is right.

Scope: [the manual-select INF plan](../plans/vlb-manual-select-inf.md), both
parts. Part A is done. Part B got the driver installed, proved it correct, and
stopped one step short of a working desktop.

---

## 1. The one-paragraph version

The INF work is finished and validated on real hardware, and the driver itself
is now correct on Win95 — loaded by hand it passes its DIB Engine inquiry and
all six mode validations. What blocks a working desktop is not the driver: it
is the Win95 devnode, which reports **Code 24, `CM_PROB_DEVICE_NOT_THERE`**, and
while it does, Display Properties offers no modes at all, so the desktop stays
pinned on the 4-bpp `vga.drv` fallback row our own INF supplies. Three fixes
were tried against Code 24 and none moved it. Section 5 has the three
untried options and what each risks.

## 2. What is done, and what proves it

**Part A — the manual-select INF model.** Complete, gated, and verified on the
486 rather than only on the host.

| Claim | Evidence |
| --- | --- |
| A model with no hardware ID is offered on a machine with no PCI bus | Have Disk on Win95 4.00.950 listed `Velocity9x S3 (VLB manual select)` and it installed |
| The pruned 2 MiB mode list is what lands | Registry `MODES\16` has no `1280,1024`; `MODES\32` has no `1024,768` |
| The install writes our driver | `Display\0000`: `InfSection=Velocity9x.Install.Manual`, `DEFAULT\drv=v9xdisp.drv`, `minivdd=v9xmini.vxd` |
| The compatible ID binds | With `,, *PNP0913`, "show compatible devices" filtered three models to ours alone |

Also in: `Get-V9xFamilyManualSelectModes` as the single source of the derived
list, schema validation for the whole `ManualSelect` block, cross-family
description uniqueness, `LogConfig` emission, and the `Assert-V9xInf`
assertions that pin all of it. Documented in
[the manifest spec](../specifications/family-manifest.md).

**Two real driver bugs fixed.**

1. **The register unlock** (`4a8f5aa`) — the one that mattered.
   `identify_without_pci` was the only S3 accessor in `s3_regs16.c` that did not
   unlock CR38/CR39, defended by a real measurement that generalised one lock
   state into a rule. Under Windows the state differs and the rule is false:
   CR38/CR39 read `96h/52h` and **CR2D/CR2E both read `5Ah`** where DOS read
   `88h/11h`. Those are read-only chip-id registers, so the locks were the only
   variable. Before: `V9X16LD.EXE` reported "a supported mode was rejected".
   After, same machine same state: *"passed its DIB Engine inquiry and all six
   mode validations"*. Cost: identification now writes two registers on an
   unidentified card, which is the bet `read_video_memory` already makes in the
   same file, saved and restored.
2. **The silent refusal** (`42dfc1e`) — `ValidateMode` gates every mode on
   `v9x_hardware_acceptable` and recorded nothing when it said no, which is why
   finding bug 1 took a hand-loaded DRV, a registry export and a survey
   re-read. It now writes `fail-validate-no-identify-hook`,
   `fail-validate-pci-bios-present` or `fail-validate-identify-declined`, plus
   `IdentifyRead`, `IdentifyLockedRead`, `IdentifyPort` and `IdentifyLocks`.
   That diagnostic found bug 1 on its first run.

**Machine infrastructure.** Both fixed, verified by a fully unattended reboot
that reconnected with its own token.

* The agent had **no autostart at all** — `C:\V9XREMOTE` only ever received the
  two EXEs and the INI, so its own `INSTALL.BAT` never ran there. Now in
  `HKLM\...\CurrentVersion\Run`.
* It never *could* start, because the machine stopped at a network logon prompt
  and never reached the shell. `PrimaryProvider` was `"Microsoft Network"`;
  changed to Windows Logon through the Network applet.

## 3. Where it actually stands, with numbers

```
V9XBOOT.INI      Stage=SENTINEL         (the DRV was not loaded at all)
V9XHW.INI        absent                 (never written; that happens at Enable)
screen           640x480, 4 bpp         (our MODES\4\640,480 -> vga.drv row)
Config Manager   Problem = 0x18         (24, CM_PROB_DEVICE_NOT_THERE)
                 Status  = 0x0EE7       (DN_DRIVER_LOADED set, DN_STARTED clear)
                 Allocation = 3B0-3BB, 3C0-3DF, A0000-AFFFF, B8000-BFFFF
Display\0000     InfPath=OEM2.INF, DriverDesc + pruned MODES all correct
devnode          Driver="Display\0000", ConfigFlags=0, LogConfig subkey present
```

Display Properties shows **no Desktop area slider and Font size greyed**, and
puts up "your display adapter is not configured properly" — so 256 colours
cannot be selected by hand either. That dialog at logon, not the password, is
what used to block unattended boots.

## 4. Three things tried against Code 24 that did not work

Recorded so nobody spends the afternoon again.

1. **`LogConfig`** (`006c3c5`). The theory was that a no-ID model leaves the
   devnode with no resources. The `LogConfig` is right and worth keeping —
   every display model in `MSDISP.INF` has one — but it was **not** the cause:
   the devnode already had an `Allocation` with the VGA ranges, because
   `DETECTS3801` supplies a `BootConfig` for what it detects.
2. **Stale `InfName`.** The devnode recorded `InfName="MSDISP.INF"` while its
   `DeviceDesc` was ours. Set to the live OEM INF by hand and rebooted: Problem
   stayed `0x18`.
3. **Binding `*PNP0913` as a compatible ID** (`a1709d0`). It genuinely binds —
   the device list filtered to our model alone — but Problem stayed `0x18`, and
   on that boot the DRV was not loaded at all.

**Declined, deliberately:** Windows offers the Add New Hardware Wizard as its
remedy. On a 486 that is a full redetect of every device including sound and
network — out of proportion to one display devnode, and not something to run
unattended on the only 486. It was opened, identified, and cancelled.

## 5. What to try next, in the order I would try it

1. **`ForcedConfig` on the devnode.** Win9x pins resources for a legacy device
   the Configuration Manager will not start on its own by writing a forced
   configuration and setting `ConfigFlags` bit 1
   (`CONFIGFLAG_NETWORK_CARD`/forced-config semantics vary by release, so read
   the existing `LogConfig` subkey's format first and mirror it). This is the
   mechanism aimed exactly at "device is really there, CM disagrees", it is a
   registry-only change, and it does not risk the driver association.
2. **Remove the display adapter in Device Manager and let it redetect.** What
   `RECOVER.TXT` prescribes, and with the compatible ID in place the redetect
   has a real chance of binding our model. **Risk:** it may come back on
   Windows' own `S3.DRV`, since `MSDISP.INF` claims `*PNP0913` for six models
   and is the in-box INF. Recoverable by reinstalling from `C:\V9XPKG`, so this
   is a decision rather than a danger.
3. **Ask whether Code 24 predates all of this.** Nobody looked at Device
   Manager before the first install — the pre-install evidence is a working
   800x600x8 desktop on Win95's `s3.drv` and a Display Properties Settings tab
   with no error. If Win95's own S3 driver ran *with* the devnode at Code 24,
   then Code 24 is normal for a `DetFunc` display devnode here and the real
   question is only why Windows will not offer modes. Testing that means
   putting `s3.drv` back for one boot and reading `Problem`. It is the cheapest
   experiment on this list and it could invalidate items 1 and 2.

Do item 3 first if the machine is free. It is one reinstall and one reboot, and
it tells you whether you are chasing a fault or a normal state.

## 6. Gotchas this session paid for

* **A reinstall does not overwrite `OEM<n>.INF`.** SetupX writes a new file
  each time; `C:\WINDOWS\INF` now holds `OEM0.INF`, `OEM1.INF` and `OEM2.INF`,
  only one of which is live. **Always read `Display\0000\InfPath` before
  concluding anything about what is installed.** Judging by file size alone
  nearly produced a wrong answer here.
* **`LogConfig` is applied by SetupX at install time.** Replacing driver files
  on disk does not revisit the devnode, so an INF change needs the Have Disk
  install re-run.
* **Win95 `REGEDIT.EXE` cannot delete.** Neither `"value"=-` nor `[-Key]` in a
  `REGEDIT4` file does anything, and both exit 0. The agent's own `REMOVE.REG`
  and `UNINSTALL.BAT` therefore do not uninstall on Windows 95 — they report
  success and leave the entry in place. Setting a value to `""` is the
  workaround used for the `RunServices` entry.
* **`PrimaryProvider` for "Windows Logon" is the empty string.** Not a value to
  guess; drive the Network applet and let Windows write it.
* **Agent-absent says nothing about how far Windows booted.** It has no
  autostart history and stopped at a logon prompt. `486VLB<03>` in `nbtstat -A`
  is the Messenger name and registers after logon, which is the signal to read
  instead. An earlier conclusion of "Windows never reached the shell" was wrong
  for exactly this reason.
* **The agent refuses `screenshot`, `reboot` and `update` while *any* exec slot
  is busy**, reporting `execution active`. A DOS box on an `Abort, Retry, Fail?`
  prompt holds one for the full 125-second timeout. `C:\V9XREMOTE\AGENT.LOG`
  names the culprit; `V9XWND.EXE` reports window state with no GDI and no
  screenshot when screenshots are unavailable.
* **The guest clock is unreliable** — it read 11:39 PM then 1:45 AM eight
  clicks later. Do not correlate anything by guest timestamps.
* **Prime `C:\V9XBOOT.INI` with a sentinel before every reboot.** It is the
  only way to tell "the DRV did not load" from "the DRV loaded and stopped at
  the same stage as last time". That distinction is what section 3 rests on.

## 7. Still open elsewhere

* The schema-2 survey regression on the 86Box PCI targets. Untouched,
  independent of all of this.
* `-ForceModeIndex` 7 and 10 can no longer build for the s3 family: the
  effective default mode must be one the manual model advertises, and those two
  are the pruned rows. Those packages were never coherent — `DEFAULT,Mode` is
  written by the shared registry section the manual model also reads — but it is
  two fewer diagnostic builds.
* The 486 has a `[DX7]` directory and `DX7A.EXE` at the root that nobody in
  this session put there. DirectX was **not** installed, per the plan.
