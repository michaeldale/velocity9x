# intel-gma bring-up runbook (physical hardware)

Provenance: originally written on the archived `intel-gma-tier0` branch and
salvaged to main in 2026-08. The strict `8086:27AE` family was restored on
2026-09-12 for the hardware-Direct3D investigation. Its display remains the
VBE tier-0 path; the Intel-specific hardware operations through Phase 3 are
read-only MMIO, GTT and ownership-event captures.

Status: current, 2026-09-12
Applies to: the `intel-gma` family on the HP Mini 110-1000 (945GSE, GMA 950,
`8086:27AE`), Win98 tier-0 bring-up and hardware-D3D Phases 1 through 3

## Why this document exists

`run-vm-mode-matrix.ps1` refuses this family by design - `Vm.Emulator = 'none'`,
because nothing emulates any Gen3 Intel part. There is no automated gate, so
this checklist *is* the gate, and it has to be followed by hand and written up
as a decision record afterwards (precedent:
`docs\decisions\2026-08-11-millennium2-physical-candidate9.md`).

Two rules that apply throughout:

- **Record what you see, not what you expected.** A green checklist that skipped
  a step is worse than a red one, because the next person believes it.
- **D5 discipline.** Every automated check in this project is GDI-side and so
  self-consistent with whatever the driver decided; six modes once passed on the
  Mach64 while the monitor showed noise. On this family the non-GDI evidence is a
  photograph and the VGA output. Take them.

## Phase 2 - Windows 98 SE on the netbook

### 2.0 Stop-the-line check, before anything else

Enter the BIOS and confirm it offers a **legacy/IDE (compatibility) mode** for
the ICH7-M SATA controller. If it is AHCI-only with no compatibility setting,
**stop**: Win98 cannot see the disk, and no amount of driver work changes that.
Record the BIOS version and the exact setting name either way.

### 2.1 Disk

Swap in a scratch 2.5" disk; the Windows 10 disk goes on the shelf intact (the
project's preservation rule - the golden VM images are treated the same way).
Partition a single FAT32 primary **inside the first 137 GB (LBA28)**, active.

### 2.2 Install, then cap the cache before the first full boot

2 GB of RAM kills stock Win98 during or just after setup ("insufficient memory
to initialize Windows"). Have the fix ready to apply from DOS the moment setup
copies files, before the first GUI boot. In `C:\SYSTEM.INI`, `[386Enh]`:

```
MaxPhysPage=40000
MaxFileCache=262144
```

Also useful in `[386Enh]` if the machine is unstable: `ConservativeSwapfileUsage=1`.
Record whether the machine booted before or after the cap - that is a fact the
next Atom-class target will want.

### 2.3 Transfer path

Expect no Win98 driver for the Atheros-class NIC, so assume sneakernet: BIOS
**USB legacy support on**, and `nusb33e` (or equivalent) for mass storage once
the desktop is up. Note in the write-up which route actually worked - if the NIC
does come up, physical testing gets much cheaper and the deferred question about
a physical-agent mode for `run-vm-mode-matrix.ps1` becomes worth revisiting.

### 2.4 Baselines from real DOS, not a DOS box

Before installing our driver, boot **real DOS** and re-run `v9xintl.exe`
(`scripts\build-intel-survey.ps1`). A DOS box under a running Windows returns
artefacts - measured on the ati family, where 4F00h reported 512 KiB instead of
4 MiB. Compare against `docs\decisions\2026-08-17-intel-gma-phase0-dos-evidence.md`;
anything that disagrees with the Phase 0 capture is a finding, not a nuisance.

## Phase 3 - tier-0 bring-up

### 3.0 Install correctly, or the result is meaningless

Install **via this family's own INF** from the `INTELGMA` package. Do not copy
binaries over another family's binding: that produces `query-ok` and an enable
that never runs, which is the second half of defect D3 and cost a day once
already.

**Install on PCI function 0, not function 1.** The 945GSE IGD is two PCI
functions: `8086:27AE` (function 0, VGA class, the one Windows lists as
"Standard PCI Graphics Adapter (VGA)" under Display adapters) and `8086:27A6`
(function 1, class 0380, which Win98 shows as an "(Unknown Device)" with no
driver). Measured 2026-09-12 on the netbook: a Have Disk install forced onto
the Unknown Device node warns "not written for the selected hardware", creates
`Display\0004` bound to function 1, and leaves function 0 on the stock VGA
binding with no `minivdd`. The driver then loads, `V9XBOOT.INI` stops at
`Stage=libmain` with `VbeDetail=minivdd-no-api` and `Aperture=... b=00000000`,
Windows falls back to the INF's 4-bpp `vga.drv` row, and a later resolution
change garbles the panel. Update the driver on the VGA-class node instead; the
INF's `VEN_8086&DEV_27AE` is in that node's compatible-id list, so no forcing
is needed. Remove the Velocity9x binding from function 1 before rebooting.

This netbook has no usable serial port and no Win98 network path. Diagnostics
must survive to disk and return by USB mass storage. On a boot that fails before
the desktop, photograph the visible state before recovery; there is no serial
artefact to reconstruct it later.

### 3.1 What the driver should report

After the first successful enable, collect both files whole:

`C:\V9XBOOT.INI` - expect `Stage=enable-ok`, plus `VbeDetail` and `VbeCache`.
`VbeCache` should report a nonzero V86 scratch segment, a nonzero dynamic mode
count, and a collected controller block. The exact count belongs to the VBIOS
mode list rather than a generated fixed cache. Use `C:\V9XDIAG\V9XMODES.INI`
to confirm that admitted `0160`/`0161` rows exist for the native panel modes.

`C:\V9XHW.INI` - expect, from the Phase 0 measurements:

| Key | Expected | Meaning if different |
|---|---|---|
| `Adapter` | `Intel GMA 950 (945GSE)` | wrong chip module matched |
| `VendorId` / `DeviceId` | `8086` / `27AE` | wrong device bound |
| `VbeVramBytes` | **8060928** (7.69 MiB) | BIOS steals a different amount; not an error, but re-check the heap arithmetic |
| `DrawPitch` | 1024 / 2048 / 640 / 1280 per mode | pitch disagreement - the stage-9 refusal path |
| `VbeScanBytes` | must equal `DrawPitch` | the card is scanning a different stride than we draw |
| `ClockStatus` | `unavailable` | expected; the fingerprint does not infer a clock |
| `Acceleration` | `none` | expected at tier-0 |

### 3.2 Phase 1 read-only MMIO fingerprint

Use the freshly built `build\win98se-intel-gma` directory as the USB transfer
folder. Its 22 files total less than 400 KiB. After installing its INF and
rebooting into a stable 1024x576 mode, copy these files back before changing
mode or package:

- `C:\V9XDIAG\V9XBOOT.INI`
- `C:\V9XDIAG\V9XHW.INI`
- `C:\V9XDIAG\V9XMODES.INI`
- `C:\V9XDIAG\INTELMM.TXT`
- `C:\V9XDIAG\INTELGTT.TXT` and `C:\V9XDIAG\INTELGTT.BIN` (Phase 2; the
  pair must be copied together, the validator recomputes the text from the
  binary)

All driver diagnostics live under `C:\V9XDIAG` since 2026-09; the earlier
root-level paths in older records are historical. The live USB stick is a
plain FAT32 volume, so the simplest return path is to shut down, pull the
stick and read `\V9XDIAG` on the development host.

In `INTELMM.TXT`, require `Access=read-only`,
`BarProvenance=PCI-BAR0-runtime`, a plausible aligned `Bar0`, and
`Result=PASS`. Every `RnnD` repeat delta must be zero. `Flags` must contain the
Phase 1 relationship mask `0000003F`; `00000040` is the separately reported
ring-quiescent fact used by Phase 3. Confirm the decoded source, timing width
and height, plane bpp and stride against the live mode rather than accepting
the verdict alone.

`REVIEW` is evidence, not permission to continue. Preserve the entire file and
explain the missing relationship. `CAPTURE-FAILED`, `CONTRACT-FAILED`, or
`DECODE-FAILED` stops the phase. Do not proceed to the GTT inventory until a
dated decision record contains the physical capture and explains its BAR and
decoded relationships.

On the development host, validate the copied file with:

```powershell
.\scripts\check-intel-mmio-capture.ps1 `
    -Path <usb-copy>\INTELMM.TXT `
    -ExpectedWidth 1024 -ExpectedHeight 576 `
    -ExpectedBitsPerPixel 16 -ExpectedPitch 2048
```

The validator independently checks the allowlist order, all repeat-read
deltas, BAR bounds/alignment, Phase 1 flags, decoded geometry and plane format.

### 3.2b Phase 2 read-only GTT inventory

Phase 1 passed on this machine on 2026-09-12
(`docs\decisions\2026-09-12-intel-phase1-physical-capture.md`). The same
package publishes the Phase 2 artefacts on every enable, immediately after
`INTELMM.TXT`. Field meanings are in `hardware-diagnostics.md`.

Require `Result=PASS`, `HashA`, `HashB` and `HashStream` equal, `Flags`
containing `0000007F`, `UnknownAttrs=00000000`, and `GttStorage` equal to
the `PgtblCtl` page. Read the run map before believing the verdict: the
expected shape from Phase 0 and Phase 1 is one linear run from entry 0 mapping
`Bsm` upward for the VBE-reported size, then a scratch-page run. Anything else
is a finding to record. `SAMPLE-REFUSED` means the table was captured but
entry 0 or the reservation entry did not decode as present and BSM-linear;
the inventory keys are still written, so preserve the file.

Validate both files together on the host:

```powershell
.\scripts\check-intel-gtt-capture.ps1 -Path <usb-copy>\INTELGTT.TXT
```

It recomputes every count, run, hash and sample relationship from the binary.
The Phase 2 done-criterion needs this to pass on at least two cold boots with
identical hashes, so take the second boot before writing the decision record.
Its own clean and corrupted fixtures run in `run-checks.ps1`.

### 3.2c Phase 3 read-only ownership event matrix

Phase 2 passed twice on this machine on 2026-09-12. Install the Phase 3
package, cold boot once, and exercise this sequence before copying evidence:

1. Switch from 1024x576x16 to 640x480x16 and back. Measured 2026-09-12:
   each change is one ReEnable rebuild, so this yields two `mode-switch`
   (kind 4) records and nothing else. Every ReEnable also drains the journal.
2. Run `V9XPWR`. Measured 2026-09-12: it produces **no** DPMS record and
   the panel state should be noted by eye, because the mini-VDD advertises
   D0-only power capabilities and Windows therefore never requests a
   low-power state. `V9XPWR`'s `PASS` is the broadcast returning, nothing
   more. The DPMS row is inert until that capability changes.
3. Change resolution and back once more, so anything retained in the
   mini-VDD is drained.
4. **Last, and only with everything above already copied off:** open a
   full-screen DOS box and return to the desktop, for the `disable` (kind 3)
   and `mode-restore` (kind 5) records. On 2026-09-12 the return hard-locked
   the netbook (`docs\issues\2026-09-12-netbook-dos-box-return-hardlock.md`);
   the Disable record is flushed to disk before the handoff, so a lock still
   leaves evidence. Photograph the screen before power-cycling.

Because of step 4, `READY` may not be reachable on this machine until the
lock is understood. A `CAPTURED` file with boot, mode-switch and both DPMS
records is the deliverable for the mode and power rows; the DOS box row is
its own finding.

Copy `C:\V9XDIAG\INTELEVT.TXT`. Require `Result=READY`, `Dropped=00000000`,
and `Flags=0000003F` in every event section. Then validate it on the host,
pinning the Phase 2 baseline hash:

```powershell
.\scripts\check-intel-event-capture.ps1 `
    -Path <usb-copy>\INTELEVT.TXT `
    -ExpectedInitialGttHash 4D8707C5
```

The validator checks chronology, coverage, repeat-read/hash stability, ring
idle/disabled state and PGTBL validity, then prints every ownership-field
change between adjacent events. A reported change is evidence to explain, not
an automatic failure: this phase exists to discover which firmware events
change ownership state. Preserve partial `CAPTURED` files too, but they do not
complete the phase.

### 3.2d Phase 4 no-write command-plan capture

The Phase 4 capture boot must be unarmed, even when using a package that
contains the guarded first-write executor. The errata gate was opened by the
2026-09-13 risk decision, but an empty `IntelArmOnce` still prevents any Intel
MMIO write. The package reserves the top 128 KiB from
DirectDraw and writes `C:\V9XDIAG\INTELRNG.TXT` after the Phase 2 inventory.
The file includes two fresh, read-only PCI BIOS reads of host bridge D0:F0
offset `60h` (`FlushPageCfg0` and `FlushPageCfg1`). `FlushPageRead=STABLE`
means both reads succeeded and matched. Bit 0 of the raw dword indicates
whether the BIOS enabled the Intel Flush Page; bits 31:12 give its configured
physical page address. Neither the probe nor the ring-plan publisher writes
PCI configuration or the flush page.

Copy that file and validate it before any later package opens the write gate:

```powershell
.\scripts\check-intel-ring-plan.ps1 -Path <usb-copy>\INTELRNG.TXT
```

Require `Access=no-hardware-writes`, `TokenMover=READY`, `ErrataGate=0`,
`FlushPageRead=STABLE` and `Result=ERRATA-GATED`. The validator independently
checks the two `60h` reads and reconstructs the 64 KiB
ring, HWS and scratch placement, checks that all ten dwords are the exact
reviewed MI/BLT streams, and recomputes both the ten-dword `ArmPacketCrc`
and `ArmExecutionCrc` over the probe, exact 16,382-dword NOOP wrap, repeated
probe and BLT. Only the latter is a candidate for `IntelArmCrc` on the later
armed boot. On the measured Phase 2
layout it should report ring `00790000`, HWS `007A0000` and scratch `007A1000`.
Any difference is a capture to understand, not permission to arm.

### 3.2e Phase 4 two-boot first-write test

Use the exact package whose build ID appears in the unarmed capture. Do not
install arm keys during package deployment. Boot once on AC power, collect
`V9XDIAG`, and require the no-write validator above, `TokenMover=READY`, and
Phase 1/2/3 PASS captures. Then arm the stick from the host. Do not hand-edit `SYSTEM.INI`: the arm
script re-validates the whole capture set, cross-checks the stick's package
build against the capture, refuses a stick whose `IntelInFlight` is still set,
writes the keys, and reads them back.

```powershell
.\scriptsrm-intel-phase4.ps1 -Capture <usb-copy>\INTELRNG.TXT -StickRoot E:
.\scriptsrm-intel-phase4.ps1 -Capture <usb-copy>\INTELRNG.TXT -StickRoot E: -Confirm
```

The first form is a dry run that prints the exact block. The keys it installs
are `IntelAccelDefault`, `IntelArmOnce`, `IntelArmCrc`, `IntelArmBuildId`,
`IntelInFlight`, `IntelEnableThisBoot` and `IntelLastResult`; the token
defaults to `p4-<date>-a` and `-Token` overrides it. The previous
`SYSTEM.INI` is kept as `WINDOWS\SYSTEM.V9X`. `-Disarm -Confirm` returns the
stick to an unarmed boot.

Boot the same package a second time on AC, leave the desktop idle, photograph
it, shut down, and collect `V9XDIAG`. Validate the armed capture with:

```powershell
.\scripts\check-intel-ring-plan.ps1 -Path <usb-copy>\INTELRNG.TXT -Armed
```

This checks the exact command stream and whole-execution CRC, ordered ring
readbacks and bounded polls, scratch guard, unchanged errors, and the full
pre/post MMIO fingerprint. If the machine hangs, photograph it and power-cycle;
the next load is unarmed because `IntelInFlight` remains set. Collect the
partial capture before considering the single identical retry allowed by the
risk decision. The arm script refuses a stick in that state, and
`-AcknowledgeIncomplete` is the deliberate override for the one retry; never
clear `IntelInFlight` by hand or substitute a new token.
The detailed ordering and retry limits are in
`docs\plans\intel-phase4-first-write-design.md`.

### 3.3 Per-mode checklist

Four modes. Run every row for each; a mode is not "done" until all of them are
recorded.

| Mode | VBE | Panel expectation |
|---|---|---|
| 640x480x8 | 0101 | **scaled** - soft, mildly stretched (4:3 on a 16:9 panel). Expected-good. |
| 1024x576x8 | 0160 | **native, pixel-exact.** No excuses available for this one. |
| 640x480x16 | 0111 | scaled, as above |
| 1024x576x16 | 0161 | native, pixel-exact |

Per mode:

1. Set it from Display Properties; reboot if the applet asks.
2. `V9XGDI` framebuffer smoke - shapes, gradients, text.
3. `V9XPAL` palette animation and readback (8 bpp especially).
4. Mode switch away and back (`V9XMSW`).
5. Full-screen DOS box round trip - out and back, desktop intact.
6. **Photograph the panel** showing a known-geometry pattern. Label the photo
   with the mode and whether scaling is expected. This is the only non-GDI
   evidence that exists on this family.
7. **VGA output to an external monitor** for the same pattern - this is the
   unscaled truth, and the way to tell "the panel fitter stretched it" from
   "the driver got the geometry wrong".
8. Enable/disable soak: several cycles, watching for the DIB Engine selector
   bug's signature (`docs\issues\2026-08-14-hellbender-dibeng-gpf.md`).

Then, once at least one mode is solid: Ironfield RTS (the DirectDraw test),
Doom95 if a 640-wide mode will take it, and a monitor power-cycle check
(`V9XPWR`).

### 3.4 Triage - the failures worth pre-planning

| Symptom | First look |
|---|---|
| `Stage=fail-hardware-vbe-mode` (stage 9) | 4F02h refused the mode. If it is a `0160`/`0161` row, the OEM mode number is the suspect - re-run the survey and compare. This is the one Phase 0 item that could not be measured without booting. |
| `minivdd-no-mode` in `VbeDetail` | the mini-VDD cache lacks the mode. Check `VbeCache` says `m=9`; if it says `m=7`, an old `v9xmini.vxd` is installed. |
| `minivdd-no-api` | the mini-VDD is absent or not ours - check it loaded and its device id (`4F9Ch`). |
| `fail-hardware-aperture` (stage 3) | 4F01h gave a base we would not drive. Phase 0 measured `D0000000` on every supported mode, matching GMADR in both DOS and Windows; a different answer under Win98 is a real finding worth its own record. |
| `stride-disagrees` | the BIOS chose a pitch we do not expect. Compare `VbeScanBytes` against the survey's `BytesPerScanLine` for that mode. |
| Desktop appears, display is garbage | do **not** trust the mode matrix here (D5). Photograph it, then compare the VGA output; if VGA is clean and the panel is not, suspect panel scaling rather than the driver. |
| 800x600 or 1024x768 offered anywhere | a bug in our advertising - this VBIOS refuses them with the panel attached. See the dynamic-mode-availability erratum in the audit. |
| `INTELMM.TXT` is absent | wrong/old package, mini-VDD API mismatch, or Intel diagnostics did not run; preserve `V9XBOOT.INI` and `V9XHW.INI` before reinstalling |
| `INTELMM.TXT` says `REVIEW` | one or more stable/live/timing/source/plane relationships failed; inspect `Flags` and the raw `RnnA`/`RnnB` values, and do not guess new offsets by writing |

### 3.5 Exit gate

An unaccelerated desktop, stable across reboots and enable/disable cycles, on
all four modes, with a photograph and a VGA capture for each - and the write-up
committed as a decision record with the artefacts under `build\driver-results\`.

## Known gap, not a blocker

The DRV's compiled mode table (`src\chipsets\intel\intel_hw16.c`) and the
manifest's `Modes` are two hand-maintained lists of the same facts, and nothing
in the build asserts they agree - unlike the mini-VDD's cache, which is now
generated from the manifests. They were verified equal by hand for this family
(the four records are present in the linked image and the three retired ones are
absent). Worth automating in the family audit before a fifth chip is added.
