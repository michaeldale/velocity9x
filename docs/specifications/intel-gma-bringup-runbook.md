# intel-gma bring-up runbook (physical hardware)

Provenance: originally written on the archived `intel-gma-tier0` branch and
salvaged to main in 2026-08. The strict `8086:27AE` family was restored on
2026-09-12 for the hardware-Direct3D investigation. Its display remains the
VBE tier-0 path; the only Intel-specific hardware operation is the Phase 1
read-only MMIO fingerprint.

Status: current, 2026-09-12
Applies to: the `intel-gma` family on the HP Mini 110-1000 (945GSE, GMA 950,
`8086:27AE`), Win98 tier-0 bring-up and hardware-D3D Phase 1

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

- `C:\V9XBOOT.INI`
- `C:\V9XHW.INI`
- `C:\V9XDIAG\V9XMODES.INI`
- `C:\V9XDIAG\INTELMM.TXT`

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
Its own clean and corrupted fixtures run in `run-checks.ps1`.

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
