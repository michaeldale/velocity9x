# The first Velocity9x driver run on the 486, and where it stopped

> **Superseded by
> [the 2026-08-22 handover](2026-08-22-vlb-manual-select-handover.md).** This
> file was written and then corrected as the session went, so it contains
> conclusions that were later disproved — kept for the reasoning trail, and
> because two of the wrong turns are worth not repeating. Where the two
> disagree, the handover is right.

Date: 2026-08-21
Branch: `vlb-manual-select-inf`
Machine state at handoff: **486VLB is powered, on the network, and not
reaching the shell.** It needs Michael at the keyboard. See section 4.

Scope: Part B of [the manual-select INF plan](../plans/vlb-manual-select-inf.md).
Part A of that plan is done, green through `run-checks`, and committed.

Related: [the VLB bring-up handoff](2026-08-21-vlb-bringup.md),
[the aperture answer](../decisions/2026-08-21-vlb-aperture-answered.md),
[Win95's own MSDISP.INF](../decisions/2026-08-21-win95-msdisp-486vlb.inf).

---

## 1. What the run proved

**The manual-select model works.** This was the open question Part A existed to
answer, and real Win95 SetupX answered it on the first attempt.

Driven through the remote agent: Display Properties, Settings, Change Display
Type, adapter Change, Have Disk, `C:\V9XPKG`.

* The compatible-devices list Win95 offers for this device is exactly the S3
  801/805/928 set `DETECTS3801` matches on `*PNP0913` - Orchid Fahrenheit,
  Paradise Bahamas, and so on. Confirms the plan's reason for not binding that
  ID: every one of those is a card we have no code for.
* The Have Disk list showed all three of our models, with
  **`Velocity9x S3 (VLB manual select)` first and pre-selected**. An INF model
  line with no hardware ID field is offered on a machine with no PCI bus. That
  is the whole claim of Part A, and it holds on 4.00.950.
* Accepting it rewrote the device: Adapter Type
  `Velocity9x S3 (VLB manual select)`, Manufacturer `Velocity9x`, Version
  `4.0`, Current Files `v9xdisp.drv,*vdd,*vflatd,v9xmini.vxd`. Exactly what
  `[Velocity9x.Registry]` writes. Before the install the same dialog read
  Manufacturer `S3`, Current Files `s3.drv,*vdd,*vflatd,s3.vxd`.

Baseline recorded before touching anything: `C:\V9XHW.INI` and
`C:\V9XBOOT.INI` both absent, desktop up at 800x600x8 on Win95's own
`s3.drv`, `display.drv=pnpdrvr.drv` in `SYSTEM.INI`.

## 1a. Correction to section 2, made 2026-08-22

**Section 2 below drew the wrong conclusion, and it is left standing as written
so the reasoning can be checked.** It reads "Windows running with no shell". It
was not. The machine boots to a working desktop every time.

Two independent problems produced one symptom:

* **The agent has never had an autostart.** Not in the StartUp group (empty),
  not in `Run` (only `SysTray`), and the `RunServices` key the agent's own
  `INSTALL.BAT` creates **did not exist at all** - `C:\V9XREMOTE` holds only
  `V9XAGNT.EXE`, `V9XSHOT.EXE` and `AGENT.INI`, so that installer never ran
  there. Every previous session's agent was started by hand. So the agent never
  survives *any* reboot, driver or no driver, and from outside that is
  indistinguishable from a machine that never reached the shell.
* **The driver fell back to VGA silently**, which needs no reboot to diagnose
  and never blanked anything.

What proves the desktop comes up: NetBIOS name `486VLB<03>` is registered, and
that is the Messenger name, registered after logon. Section 2 had that evidence
in hand and read it as "net init got far" when it actually means "logon
completed".

The lesson worth keeping: on this machine, agent-absent says nothing about how
far Windows booted. Check `486VLB<03>` before concluding anything about the
shell.

## 2. Where it stopped

The reboot did not come back.

* The agent's `reboot` was accepted and the old connection ended, so the
  machine did restart.
* It has not reconnected. Polled for roughly 25 minutes.
* **ICMP answers, NetBIOS port 139 is open, and the NetBIOS names `486VLB<00>`
  and `486VLB<03>` are registered.** So Windows is running and got far enough
  to bring up the whole TCP/IP and NetBIOS stack.
* Port 9869 is *actively refused* - a RST, not a timeout. The agent is not
  listening.
* No `<20>` server-service name, so there are no file shares to read the disk
  through. The agent is the only channel into this machine and it is the thing
  that is missing.

The agent starts at boot on this machine. Networking up and agent absent
therefore reads as **Windows running with no shell** - the boot got through
ring-0 and net init and stopped at or around the display driver's Enable. That
is consistent with a stage-1 refusal or a failed aperture map, and it is what
the plan's step 6 anticipated. It is not proof: a fully booted desktop with a
crashed agent looks identical from here, and nothing remote can tell them
apart.

**No diagnostics were collected, deliberately.** `C:\V9XBOOT.INI` should now
exist - boot tracing is on by default in this package - and it holds the
furthest lifecycle stage the driver reached. `C:\V9XHW.INI` may exist too. Both
need someone at the machine. Nothing was changed after the failure.

## 3. One thing worth knowing about the session

At 23:36:39, mid-install, another client ran a shell job on this machine:
`dir D:\`. There is no D: drive, so it sat on a DOS *Abort, Retry, Fail?*
prompt until the agent's 125-second timeout killed it, and while it held an
exec slot **every screenshot was refused** with `execution active` -
`v9x_execution_any_active` gates screenshot, power and update on *any* busy
slot. `AGENT.LOG` is what identified it; `V9XWND.EXE` from the package is what
kept the session moving, since it reports the window list without GDI and
without a screenshot.

Two things follow. Assume the 486 is not exclusively yours during a session,
and when screenshots start failing read `C:\V9XREMOTE\AGENT.LOG` before
assuming the guest is wedged.

Also: the guest clock is unreliable. It read 11:39 PM, then 1:45 AM eight
clicks later. Do not use guest timestamps to correlate anything.

## 4. What Michael needs to do, in order

Nothing here is automatable from a session.

1. **Look at the screen.** Whether it is blank, showing a Win95 display-error
   dialog, or showing a working desktop decides everything below. This single
   observation is the whole fork.
2. **If there is no desktop: collect the diagnostics before changing
   anything.** Clean-boot to DOS with **F5, not F8** - F8 with "command prompt
   only" still runs CONFIG.SYS on this machine - and copy off:
   * `C:\V9XBOOT.INI` - the furthest stage the driver reached. This is the
     answer to why it stopped.
   * `C:\V9XHW.INI` - whether `identify_without_pci` fired and what it decided
     the chip was.
   * `C:\BOOTLOG.TXT` - the driver load, from Windows' side.
3. **Then recover.** `C:\V9XPKG\RECOVER.TXT` is on the disk. Its steps say F8;
   use F5 on this machine. Safe Mode, Device Manager, remove the Velocity9x
   display adapter, reboot, let Windows redetect. `C:\V9XPKG\V9XFIX.BAT` is
   also there but expects the package on `D:\ACTIVE`, which is not where it
   was pushed - it is at `C:\V9XPKG`.
4. **If there *is* a desktop**, the driver worked and only the agent died.
   Restart the agent, then finish the plan's step 5 from a session: read
   `C:\V9XHW.INI`, confirm Display Settings offers the 10-mode list, and land
   one mode change.

## 4a. Why the driver does not work, established 2026-08-22

The chain is closed except for one measurement. Nothing here is guesswork
unless it says so.

**Symptom.** The desktop runs at 640x480 in **4 bpp** (`SourceBitsPerPixel=4`
from the agent's own capture). That is the `MODES\4\640,480 -> vga.drv` entry
our INF installs. Our DRV is loaded but drives nothing. `V9XBOOT.INI` reads
`Stage=libmain`, and `v9x_boot_trace` is last-write-wins, so nothing after
LibMain ran that boot.

**The DRV itself is fine on Win95.** Loaded by hand with `V9X16LD.EXE`, the
boot trace advances `libmain -> query-start -> query-mode-selected ->
query-ok`. The DIB Engine/GDIINFO inquiry works on 4.00.950.

**Where it dies.** `V9X16LD` then reports *"A supported mode was rejected"* -
its return 4, the `ValidateMode` loop over 640x480, 800x600 and 1024x768 at 8
and 16 bpp. `ValidateMode` (`ddi.c:956`) has exactly three rejection paths, and
two are eliminated:

* *Not in the table* - no: 640x480x8 is in the s3 table.
* *Out of memory* - **cannot fire for this family at all.**
  `v9x_vbe_vram_reported` is assigned only at `enable16.c:425` and `:514`, both
  inside the tier-0 VBE path, and the declaration comment says so outright:
  "Only the tier-0 path fills this in: a family with a `read_aperture` hook
  knows its own memory size and never calls 4F00h." The s3 family has that
  hook, so the value is permanently 0 and the check is inert.
* Therefore: **`v9x_hardware_acceptable()` is returning 0.**

That single fact explains everything observed. `ValidateMode` answers
`NO_WRONG_DRIVER` to *every* mode, so GDI is told a driver that loaded cleanly
supports nothing, never calls Enable, and uses the 4 bpp VGA row instead - and
the trace stays at `libmain` because `ValidateMode` writes no stage.

**Which half of `v9x_hardware_acceptable` (`enable16.c:132`).** On this machine
`V9xHardwarePresent()` is 0 (no PCI to scan) and `pci_match_optional` is 0 for
s3, so the answer rests entirely on the `identify_without_pci` branch. The hook
*is* wired (`s3_hw16.c`, last field). Two candidates remain, and they are
**not yet distinguished**:

1. `V9xPciBiosPresent()` returns non-zero under Win95, so the branch is skipped
   before the hook is ever called. It is `INT 1Ah AX=B101h`
   (`runtime.asm:721`), and under Windows that interrupt is not the BIOS's
   alone. The DOS survey recorded `[PciBios] Reason=int1a-b101-failed`, so it
   is 0 *in DOS* - including in V86 with EMM386 loaded - but that is not a
   measurement of the Win95 Win16 context.
2. The hook runs and its CR2D/CR2E read does not return `88h/11h` under
   Windows. The survey read exactly `LockedCR2D=88 LockedCR2E=11` from DOS,
   and `0x8811` is in this binary's device list, so the logic is right on this
   card. But a Win16 DRV that is not the display owner does port I/O through
   Win95's VDD, which virtualises VGA register access - so the DOS measurement
   does not carry over. Note this is `identify_without_pci`'s **first ever
   execution on real hardware**; everything before it was host tests and PCI
   guests where `V9xHardwarePresent()` answered 1 and this path never ran.

**The diagnostic gap that made this expensive, and worth fixing.**
`ValidateMode`'s hardware rejection is silent. `v9x_trace_hardware_failure()`
already exists and maps `V9xHardwareStage()` onto ten specific
`fail-hardware-*` stages, but it is only called from the Enable path
(`ddi.c:686`), and `v9x_hardware_acceptable()` sets no stage code of its own.
Give it one - "no PCI BIOS and the hook declined" versus "PCI BIOS present and
nothing matched" - and have `ValidateMode` write it, and this entire
investigation becomes one read of `V9XBOOT.INI`. It also distinguishes
candidates 1 and 2 above on the next boot, for free.

**A stale claim in the manifest, found on the way.**
`packaging/families/s3/family.psd1`'s `Vm.Modes` comment says that on the 2 MiB
physical Trio64 "1024x768x32 and 1280x1024x16 are expected to be refused by
ValidateMode". They are not and cannot be: the memory check is inert for this
family, as above. Which means Part A's INF-level pruning is not a belt-and-
braces duplicate of a runtime refusal - it is the only thing standing between
a 2 MiB card and two modes it cannot scan out.

## 4b. Code 24, and what it is not — 2026-08-22

The driver is now correct as far as anything can test it off the devnode:
`V9X16LD.EXE` passes the DIB Engine inquiry and all six mode validations after
the register-unlock fix. But at boot it still reaches only `libmain`, the
desktop stays on the 4-bpp `vga.drv` row, and Device Manager reports **Code
24**. Windows also puts up "your display adapter is not configured properly" at
logon, and *that* dialog — not the password — is what was blocking unattended
boots.

Authoritative state, from `HKEY_DYN_DATA\Config Manager\Enum`:

```
HardWareKey = "ROOT\*PNP0913\0000"
Problem     = 0x18   (24, CM_PROB_DEVICE_NOT_THERE)
Status      = 0x0EE7
Allocation  = ...3B0-3BB, 3C0-3DF, A0000-AFFFF, B8000-BFFFF...
```

`0x0EE7` has `DN_DRIVER_LOADED` set and **`DN_STARTED` clear**, with
`DN_HAS_PROBLEM`. The driver loads; the device never starts. That is exactly
what `Stage=libmain` means from the other side.

**Two hypotheses tested and eliminated, so nobody repeats them:**

1. *Missing resources.* Was the reason for adding `LogConfig`, and the
   `LogConfig` is real — `OEM1.INF` carries it and the class key's `InfPath`
   points there. But the devnode had an `Allocation` with the VGA ranges
   **before** that change, because `DETECTS3801` supplies a `BootConfig` for
   what it detects. So the `LogConfig` is correct and worth keeping — Windows
   gives every display model one — but it was never the cause.
2. *Stale `InfName`.* The devnode recorded `InfName="MSDISP.INF"` while its
   `DeviceDesc` was ours, which looked like Windows being unable to find our
   description in its own INF. Set to `OEM1.INF` by hand and rebooted: Problem
   stayed `0x18`. Eliminated. The value was left at `OEM1.INF` since it matches
   the class key and is right regardless.

**Watch for this trap:** re-running the install does **not** overwrite the
previous `OEM<n>.INF`. SetupX wrote a second file, so `C:\WINDOWS\INF` now
holds `OEM0.INF` (3899 bytes, no `LogConfig`) and `OEM1.INF` (4124, with it).
Always check `Display\0000\InfPath` to see which one is live before concluding
anything about what is installed.

**What is left.** The one substantial difference remaining between our INF and
Windows' own is the model line. Every S3 model in `MSDISP.INF` that covers this
device binds the ID in the **compatible-ID** field with the hardware-ID field
empty — `%GE64%=S3,, *PNP0913` — and the ID-less `SVGA` model exists only
alongside those. Ours claims nothing, so when the Configuration Manager
re-enumerates a `DetFunc`-detected `*PNP0913` at boot, there is nothing to
re-bind it to, and a device it cannot bind is a device that is not there. The
install works immediately because SetupX sets the association directly; it is
the *next* boot that loses it, which is precisely the observed pattern.

Testing that means binding `*PNP0913`, which
[the plan](../plans/vlb-manual-select-inf.md) explicitly forbade, for a reason
that still holds: it covers every S3 801/805/928 card `DETECTS3801` finds and
this driver has code for none of them. Not a decision to take quietly — see
section 5.

## 5. What is still open

* The three questions of the plan's step 5 are all still unanswered: whether
  `identify_without_pci` fires, whether `0x7F000000` maps from protected mode,
  and whether a mode set lands. The install is no longer what blocks them.
* The first boot was a **warm restart**, not the cold start `INSTALL.TXT`
  asks for. A cold power cycle needs someone at the machine, and the agent can
  only warm-reboot. If the failure turns out to be state left behind by
  Win95's `s3.drv`, that is the first thing to re-test - cold.
* The schema-2 survey regression on the 86Box PCI targets, unchanged and
  independent of all of this.

Added 2026-08-22:

* **The agent still has no working autostart, and neither registry mechanism
  starts it.** Both were tried and both failed, each verified present in the
  registry before its own reboot:
  * `RunServices` - no agent after 13 minutes. **This is also the likely cause
    of the fatal-exception-0E screen Michael saw** (`0137:BFF765A8`, and
    `BFF7xxxx` is KERNEL32's range on Win9x). The screen says "press any key to
    terminate the current application", which is a recoverable ring-3 fault,
    not a halt - so the boot continued to a desktop afterwards, which is
    exactly what happened. `AGENT.LOG` shows **no `agent-start` line near any
    boot**, consistent with the process faulting before it opens the log.
    Now neutralised (see below).
  * `Run` - also no agent, logon confirmed complete throughout by
    `486VLB<03>`. So the mechanism is not the problem.
  * **Working directory is ruled out.** Michael always starts it after
    `cd v9xremote`, but every path in the agent is absolute - `v9x_root`,
    `v9x_config_path`, `v9x_log_path`, `v9x_temp_root`, `v9x_jobs_root`,
    the screenshot helper and its capture files, all literal
    `C:\V9XREMOTE\...`. It cannot be cwd-dependent.

  What is left is why `V9XAGNT.EXE` faults when the *shell or the service
  loader* launches it but not when it is started from a DOS box. That needs the
  agent up to investigate, so it takes one more hand-started session.

* **Why neither autostart key fired, resolved 2026-08-22: the machine never
  reached the shell.** It stops at a logon prompt every boot, so `Run` never
  runs - and `RunServices`, which does run before the shell, is the one that
  faulted. The mechanisms were never the problem.

  `HKLM\Network\Logon` had `PrimaryProvider="Microsoft Network"`, i.e. Client
  for Microsoft Networks as the Primary Network Logon, which always prompts.
  Changed to Windows Logon through the Network applet rather than by writing
  the registry directly, because the value Windows writes for it is the **empty
  string** - `PrimaryProvider=""` - which is not a thing to guess. `UserProfiles`
  is absent, so profiles are off.

  That leaves the Windows password for `Michael Dale` as the last gate, and a
  `MICHAELD.PWL` exists. Its state cannot be read remotely and passwords are
  not something to automate: blanking it is Control Panel, Passwords, Change
  Windows Password, leaving the new password empty. Once boots reach the
  desktop unattended, the `Run` entry already installed should finally fire and
  the autostart problem closes with it.

* **Two Win95 bugs in the agent's own packaging, found here.** `REGEDIT.EXE` on
  4.00.950 silently ignores both deletion forms in a `REGEDIT4` file: neither
  `"value"=-` nor `[-HKEY...\Key]` removes anything, and both exit 0. So
  `REMOVE.REG` and `UNINSTALL.BAT` do not uninstall on Windows 95 - they report
  success and leave the entry running. A `.REG` import there can only add or
  set. The workaround used for `RunServices` was to set the value to an empty
  string, which Windows reads, fails to launch, and skips.
* **Distinguishing the two `v9x_hardware_acceptable` candidates** in section
  4a. The stage-code diagnostic is the cheap way and pays for itself.
* The manifest comment corrected in section 4a.
