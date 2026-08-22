# VLB manual-select handover: the model installs, the devnode will not start

Date: 2026-08-22
Branch: `vlb-manual-select-inf`, `716420d` through the branch tip, not merged
(12 commits at this update; `018e536` is titled "Update plan." but actually
revised this handover).
No other tracked changes at handoff; `.claude/` is intentionally untracked.
`run-checks` green across all four families.
Machine: **486VLB is healthy** — desktop up, agent reachable, boots unattended.

Supersedes the running notes in
[the first-driver-run handoff](2026-08-21-vlb-first-driver-run.md), which was
written and corrected as the session went and is kept only for the reasoning
trail. Where the two disagree, this one is right.

Scope: [the manual-select INF plan](../plans/vlb-manual-select-inf.md), both
parts. Part A is done. Part B got the driver installed, proved its pre-Enable
inquiry and mode-validation paths correct, and stopped before Windows started
the devnode and called Enable.

---

## 1. The one-paragraph version

The INF work is finished and validated on real hardware. Loaded by hand, the
driver passes its DIB Engine inquiry and all six mode validations, but its
Enable, aperture-map and real mode-set paths remain unproved on Win95. At boot
Windows does not start the display devnode: it reports **Code 24,
`CM_PROB_DEVICE_NOT_THERE`**, Display Properties offers no modes, and the
desktop stays pinned on the 4-bpp `vga.drv` fallback row our own INF supplies.
Code 24 is correlated with the failure, not yet proved to cause it. Three
attempted fixes did not move it. Section 5 starts with the stock-driver control
that distinguishes a causal fault from normal `DetFunc` display-devnode state.

## 2. What is done, and what proves it

**Part A — the manual-select INF model.** Complete, gated, and verified on the
486 rather than only on the host.

| Claim | Evidence |
| --- | --- |
| A model with no hardware ID is offered on a machine with no PCI bus | Have Disk on Win95 4.00.950 listed `Velocity9x S3 (VLB manual select)` and it installed |
| The pruned 2 MiB mode list is what lands | Registry `MODES\16` has no `1280,1024`; `MODES\32` has no `1024,768` |
| The install writes our driver | `Display\0000`: `InfSection=Velocity9x.Install.Manual`, `DEFAULT\drv=v9xdisp.drv`, `minivdd=v9xmini.vxd` |
| The compatible ID binds | With `,, *PNP0913`, "show compatible devices" filtered three models to ours alone. This is diagnostic-only: it did not fix Code 24 and should not merge unless later evidence proves it necessary |

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

Captured after the `a1709d0` reboot, the branch's last — the same boot section
4.3 describes, on which the DRV was not loaded at all.

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

## 5. What to try next, in order

Michael has confirmed that the 486 is available. Make one state change per
reboot and stop at each decision point below.

### 5.1 Capture a reversible baseline

Before changing the installed driver, export or otherwise capture:

* the active `Display\0000` key, including `InfPath`, `InfSection`, driver
  filenames and all `MODES` rows;
* `ROOT\*PNP0913\0000`, including `Driver`, `ConfigFlags`, `LogConfig` and any
  hardware-profile state;
* the dynamic Config Manager `Problem`, `Status` and `Allocation`;
* the current Display Properties state and screen depth;
* `SYSTEM.INI [boot]`, in particular `display.drv=` — the 2026-08-21 baseline
  read `pnpdrvr.drv`; if anything since has reset it to `vga.drv`, that alone
  produces the 4-bpp desktop with no devnode involvement at all, and section
  3's "our MODES\4 row" reading would be an unproven inference;
* `C:\BOOTLOG.TXT` from a logged boot, which separates "the DRV load was never
  attempted" from "attempted and failed" — the sentinel alone cannot tell a
  load that was skipped from one that died before stage 1. A logged boot needs
  the boot menu, so it is a request to Michael at the keyboard;
* the exact recovery route back to `C:\V9XPKG`.

Prime `C:\V9XBOOT.INI` with the sentinel immediately before every reboot. Do
not infer the live INF from the newest `OEM<n>.INF`; read `Display\0000\InfPath`.

### 5.2 Run the stock-driver control first

Temporarily put Win95's in-box `s3.drv` back on the **same detected devnode**,
perform one proven reboot, then capture the same evidence again. Confirm both
the Config Manager state and whether the known pre-install 800x600x8 desktop,
mode slider and error-free Settings tab return.

Two things to know before starting. The 2026-08-21 baseline never recorded the
stock devnode's `Problem`/`Status` — that missing datum is why this control
needs a reboot at all. Michael recalls Device Manager was clean under stock
`s3.drv` before the first install (recollection, not a captured record), which
leans toward Code 24 being material; run the control anyway and make it a
record. The route back is Change Driver against `MSDISP.INF`, which may prompt
for the Win95 source files: **the cabs are on the 486 at `C:\WIN95`**, so the
prompt is answerable remotely.

The result decides the rest of the investigation:

* **Stock S3 works and still reports Code 24:** Code 24 is normal here, not the
  cause. Do not try `ForcedConfig` or remove-and-redetect. Diff the working
  stock `Display\0000`, mode-selection and driver-loading state against
  Velocity9x and investigate why Windows offers no Velocity9x modes.
* **Stock S3 works and Code 24 clears:** Code 24 is material. Focus on the
  Velocity9x association/start transition, then continue to section 5.3.
* **Stock S3 also fails:** stop. The control is invalid or the machine state
  has changed independently; recover the known-good stock display before
  making another Velocity9x change.

After collecting the control, reinstall Velocity9x from `C:\V9XPKG` only when
the selected branch requires it, and capture the new live `InfPath`.

### 5.3 Remove and redetect only if the control implicates Code 24

Remove only the display adapter in Device Manager and let Windows redetect it;
do not run the full Add New Hardware Wizard. This is the route `RECOVER.TXT`
already uses. The compatible `*PNP0913` ID gives the diagnostic package a
chance to rebind, but Windows may select its own `S3.DRV` because `MSDISP.INF`
claims the same ID for six models. Recover by reinstalling from `C:\V9XPKG`.

Record which INF and model redetection selected before manually overriding it.
If the devnode starts, skip section 5.4 and go straight to the exit criteria in
section 5.5.

### 5.4 Treat `ForcedConfig` as a last, gated experiment

Do not set an assumed `ConfigFlags` "bit 1", and do not synthesize a
`ForcedConfig` blob by copying `LogConfig`. Before this experiment, obtain the
exact Win95 definitions and logical-configuration format from an authoritative
Win95 source, or make Win95 create a forced configuration through its own UI
and diff the registry. The flag names and meanings vary across Windows
releases, and an incorrect bit can disable or otherwise change the devnode.

If the format is established, export the complete affected registry state,
make one reversible forced-configuration change, reboot once, and compare
`Problem`, `Status`, `Allocation` and driver-load evidence with the baseline.

### 5.1 and 5.2 as executed, 2026-08-22

**5.1 done.** Baseline captured to the host and to `C:\V9XBASE` on the guest:
`BASE-CLASS.TXT`, `BASE-ENUM.TXT`, `BASE-DYN.TXT`, plus `SYSTEM.INI`, a
screenshot and the package inventory. Findings that matter:

* **`SYSTEM.INI [boot] display.drv=pnpdrvr.drv`, unchanged, `*DisplayFallback=0`.**
  So section 3's reading survives the check 5.1 demanded: the 4-bpp desktop is
  *not* SYSTEM.INI having been reset to `vga.drv`, and the registry path really
  is involved.
* Live INF read from `Display\0000\InfPath`, not inferred: **`OEM2.INF`**.
* The devnode's `LogConfig\0` blob decodes to our full resource set — both
  register windows, both apertures, all four ROM alternatives — so the INF's
  `LogConfig` did apply. It is also visibly a *different* structure from
  `BootConfig`, which is concrete support for 5.4's warning not to synthesize
  one by copying the other.
* Recovery route verified: `C:\V9XPKG`, 22 files, INF 4135 bytes. Win95 source
  cabs confirmed present: **19 `.CAB` files in `C:\WIN95`**.
* Still missing: a logged-boot `BOOTLOG.TXT`. Needs the boot menu, so it stays
  a keyboard request.

**New evidence found while picking the control model.** In `MSDISP.INF` the
in-box entry that actually covers this device puts the ID in the
**hardware-ID** field:

```
%*PNP0913.DeviceDesc%=S3, *PNP0913      ; Mfg.S3 - hardware-id position
%GE64%=S3,, *PNP0913                    ; Mfg.Actix - compatible position
```

`a1709d0` used the compatible position. That is weaker than what Windows itself
uses for this devnode, and is a candidate explanation for a binding that
satisfies the Have Disk filter but not the devnode start. It does not license
moving ours to the hardware-ID field — that is the strongest possible claim on
every 801/805/928 board and is exactly what the plan forbade — but it belongs in
the record.

**5.2 started, and incomplete.** Stock `s3.drv` was installed on the same
devnode and verified before rebooting: Adapter Type `S3`, Manufacturer `S3`,
Current Files `s3.drv,*vdd,*vflatd,s3.vxd` — the 2026-08-21 baseline exactly. No
source-file prompt appeared. The sentinel was primed and a proven reboot issued.

**The machine did not come back.** ICMP answers and `486VLB<03>` is registered,
so it booted and reached logon, but the agent is absent after ~19 minutes and
the control's `Problem`/`Status` could not be read. Nothing further was changed,
per this section's third branch.

Two things follow for whoever picks this up:

* **The 486 is currently on Win95's own S3 driver**, which is the known-good
  configuration — a safer state than it was in, not a worse one. The route back
  to Velocity9x is Have Disk from `C:\V9XPKG`.
* **Why the agent did not return is now itself unknown**, and it is the second
  time a driver-change boot has swallowed it. It may be a modal dialog at
  logon, as the "not configured properly" one was. Someone has to look at the
  screen; that single observation unblocks both the control and the question.

### 5.2 result: Code 24 is material, and the diff narrows it to two values

The control completed on the boot after the one that swallowed the agent.
**Stock `s3.drv` works and Code 24 clears**, which is 5.2's second branch.

| | stock `s3.drv` | Velocity9x |
| --- | --- | --- |
| `Problem` | **`0x00000000`** | `0x18` (24) |
| `Status` | `0x0ACF` — `DN_STARTED` **set**, `DN_HAS_PROBLEM` clear | `0x0EE7` — `DN_STARTED` **clear**, `DN_HAS_PROBLEM` set, `DN_NEED_TO_ENUM` set |
| screen | 640x480 at **8 bpp** | 640x480 at 4 bpp |
| `V9XBOOT.INI` | `SENTINEL` (correctly not loaded) | `SENTINEL` / `libmain` |

So Code 24 is not normal here and it is not cosmetic. The device starts under
the in-box driver and does not under ours.

**The devnode is not where the difference lives.** Stock and Velocity9x devnodes
are structurally identical — same `HardwareID`, `DetFunc`, `DetFlags`, `Driver`,
`ConfigFlags=0`, and both carry a `LogConfig` subkey whose blob begins
identically. Only `DeviceDesc` and `Mfg` differ. That rules out a whole class of
theories, including everything section 5.4 was written to gate.

**The difference is in `Display\0000\DEFAULT`, and it is two values:**

```
stock                          Velocity9x
minivdd = s3.vxd               minivdd = v9xmini.vxd
CHIPID  = hex:11,00,00,00,00,00   (absent)
```

Everything else there is the same or ours-only-and-harmless (`drv2`,
`RefreshRate`, `PCIRebalance`, `ExtModeSwitch`).

**`minivdd` is the leading candidate.** A Win9x display devnode starts by way of
the VDD loading the mini-VDD named here; if `v9xmini.vxd` fails to load or fails
its init, the device does not start, which is precisely `DN_STARTED` clear with
Problem 24. This family's mini-VDD also has form on this exact class of card:
`Build.MiniVddVbeCollect = $false` exists only because boot-time VBE collection
hung a physical Trio64 (`docs/issues/2026-08-18-trio64-minivdd-boot-hang.md`).

`CHIPID` is the weaker candidate — `0x11` is the Trio64's id byte, written by
Win95's own S3 detection, and read by `s3.drv`. Our driver identifies the chip
itself, so it should not need it; but nothing has proved the *class installer*
does not.

**Next, and cheap:** reinstall Velocity9x, set `DEFAULT\minivdd` to an empty
string, and take one reboot. Every other value is then identical to a
configuration already measured at Problem 24 twice, so if Problem clears the
mini-VDD is the cause. It is registry-only and reversible, and it needs no
guesses about undocumented flag bits — which is what 5.4 was protecting against.
A logged `BOOTLOG.TXT` would answer it outright by showing whether
`v9xmini.vxd` loads, and remains the better evidence if someone is at the
keyboard.

### RESULT: the mini-VDD was the cause, and the driver runs

The one-value experiment landed. With `Display\0000\DEFAULT\minivdd` set to an
empty string and nothing else changed:

```
Problem     = 0x00000000          (was 0x18)
Status      = 0x0ACF              DN_STARTED set   (was 0x0EE7, clear)
screen      = 640x480 at 8 bpp    (was 4 bpp)
V9XBOOT.INI = Stage=enable-ok
              Surface=pitch=640 bpp=8 dwb=640 dds=640 w=640 h=480 debpp=8
```

**`C:\V9XHW.INI` exists for the first time**, and every field in it is right:

```
Adapter=S3 Trio32/64 86C764     VendorId=5333  DeviceId=8811
VideoMemoryBytes=2097152        VideoMemoryStatus=valid
ClockStatus=valid               CoreClockKHz=59957  MemoryClockKHz=59957
```

So all three of the plan's Part B questions are answered yes:
`identify_without_pci` fires and names the chip correctly on a bus with no PCI,
the aperture maps from protected mode, and a mode set lands. Display Properties
offers exactly our INF's depths — 16 Color, 256 Color, High Color (16 bit),
True Color (32 bit), no 24-bit — with a working resolution slider and no error
dialog.

**`v9xmini.vxd` is what stopped the devnode starting.** A Win9x display devnode
starts by way of the VDD loading the mini-VDD named in `DEFAULT\minivdd`; ours
does not load on Win95 4.00.950, so `DN_STARTED` never set, so Problem 24, so
Display Properties offered nothing, so the desktop stayed on the 4-bpp fallback.
Every earlier symptom hangs off that one failure.

Two things this is **not**:

* ~~It is **not a shippable fix.**~~ **Now shipped.**
  `Inf.ManualSelect.MiniVdd = $false` moves `HKR,DEFAULT,minivdd` out of the
  shared registry section into the per-chip ones, so the manual model gets no
  mini-VDD while every PCI model keeps one — the Win98SE targets are unchanged,
  checked in the emitted `ati` and `vbe` INFs too. By omission rather than an
  empty value written afterwards, so it does not depend on SetupX applying
  `AddReg` left to right. Re-verified on the 486 **from a clean Have Disk
  install with no hand edits**: `minivdd` absent from `DEFAULT` entirely,
  `Problem 0x00000000`, `Status 0x0ACF`, `Stage=enable-ok`, `V9XHW.INI` correct.
  Five new negative tests cover leaking it back into either section the manual
  model reads, dropping it from a chip section, and a non-boolean declaration.
* It is **not yet a diagnosis of why** the mini-VDD fails to load. Nothing has
  looked at that. A logged `BOOTLOG.TXT` would show it outright and is now the
  single highest-value piece of evidence outstanding.

**Also outstanding: the mode change does not persist.** Selecting 800x600 at 256
colours and clicking Apply dropped the agent connection — so something happened
— but the desktop returned to 640x480, and `Surface=` still reported 640x480. A
reboot did not carry it either. Win95 4.00.950 predates the live mode switching
this family's `ModeSwitching = live-any-depth` was validated against on Win98SE,
so a restart being required is expected; a restart *not* working is not. Suspect
the hardware-profile mode store (`HKLM\Config\...\Display\Settings`) against our
`DEFAULT\Mode`, and note the 4-bpp `MODES\4\640,480` row is still a candidate
target for anything that falls back.

### 5.5 Exit criteria and branch cleanup

A working desktop requires all of the original Part B evidence, not merely the
absence of Code 24:

* `V9XBOOT.INI` advances beyond the sentinel and records the real driver path;
* `V9XHW.INI` records the identified S3 chip and aperture diagnostics;
* the devnode is started, or a measured stock control has proved that its
  status is not the gating condition;
* Display Properties offers the 2 MiB-safe list and one selected Velocity9x
  mode lands successfully.

Before merging, remove the broad `*PNP0913` compatible binding from `a1709d0`
unless the experiments prove that it is necessary and the unsupported
801/805/928 match risk is explicitly accepted. Re-run the four-family checks
after any source or INF change; no re-run is needed for this handoff edit.

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
* The 486 has a `[DX7]` directory and `DX7A.EXE` at the root. Michael put them
  there; they are expected. DirectX was **not** installed, per the plan.
