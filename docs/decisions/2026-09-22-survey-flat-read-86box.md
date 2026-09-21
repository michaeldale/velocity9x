# The survey's flat read works: a window at E0000000h read, where the BIOS service would have declined

Date: 2026-09-22
Status: **measured.** The unreal-mode path runs unattended in 86Box, proves
itself against the BIOS ROM, reads the window and leaves the machine intact.
The refusal branches are not exercised and the window read was not live.

Follows the Trio64V+ VLB report submitted to the Vogons thread on 2026-08-28
(`V9XDIAG_S3TRIO64VP_VLB_onDIGIS486EL`), where CR59/CR5A put the window at
62000000h and the aperture step returned
`Status=skipped, Reason=base-above-int15h-ah87h-16mb-limit`. INT 15h AH=87h
builds a 24-bit descriptor base, so it can never reach where VLB windows are
actually placed. That tester's `V9XAPER.INI` was V9XSURV run with `/aperture`
and `/out`, not the dedicated aperture tool, so the mechanism that could reach
his window was never on his disk.

## What was run

A Win98SE DOS boot floppy (`build/vm-clean/win98se-boot.img`) copied and
injected with `V9XSURV.EXE` and an `AUTOEXEC.BAT` reading

    A:\V9XSURV.EXE /aperture /rom /out:A:\V9XSURV.INI

so the run needs no keystrokes and the report comes back inside the floppy
image. The guest is 86Box `ym430tx`, Pentium P55C, 128 MiB, `virge_dx_pci`,
**no hard disk attached** - the boot floppy is the only writable medium, so the
run cannot touch the Win98 image the other profiles use. The report is beside
this note as `2026-09-22-survey-flat-read-virgedx-86box.ini`.

Real mode throughout: no CONFIG.SYS on that floppy, so no HIMEM and no EMM386.
`[Platform]` confirms it - `Cpu386Probe=yes`, `ProtectedOrV86=no`,
`XmsPresent=no`, `EmsPresent=no`.

## The result

| Key | Value |
|---|---|
| `Method` | `unreal-mode-flat-read` |
| `Base` | `E0000000` |
| `SelfTestStatus` | `ok` |
| `SelfTestFarPointer.00` | `C3000000000000000000000000000000` |
| `SelfTestFlatSegment.00` | `C3000000000000000000000000000000` |
| `Data.00`, `Data.10` | all `FF` |

`BAR0:memory@E0000000` in the same report corroborates the base, which is
3.5 GiB up - 224 times the ceiling the BIOS service could have reached. Under
the old build this is precisely the case that returned `skipped`.

The two self-test readings agree and are not a single repeated byte, which is
what the check requires: a flat read that silently returned a constant would
have been caught by the comparison, and one that agreed only because both
readings were uniform is refused separately. This BIOS makes a weak target -
fifteen of the sixteen bytes are zero - but one differing byte is enough to
discriminate, and the guard for the uniform case is the reason that is true.

DOS survived the excursion. The tool went on to write a 98 KB report to the
floppy after leaving unreal mode, which is a stronger statement about FS having
been restored than any register dump would be.

## What this does not show

**The window was not live.** `CR58=03` has bit 4 clear, so linear addressing is
off and all-`FF` is the honest answer rather than a defect. The survey reads and
never writes, so it cannot enable the window to find out; that is what
`tools/diag/vlb_aperture_dos.c` exists for, and its banked cross-check remains
the only way to prove the bytes are video memory. The report says as much in
`LimitationNote`, and `parse-vga-survey.ps1` renders the verdict as
`nothing-decodes` with the no-mode-set caveat attached.

**The refusal branches are untested.** `base-above-16mb-and-no-386-for-unreal-mode`
and `base-above-16mb-and-cpu-in-protected-or-v86-mode` were not reached: the
guest is a Pentium in real mode. A run from a Windows DOS box or under EMM386
would exercise the second, and should, before this build goes to a tester whose
machine boots with a memory manager.

**No VLB hardware was involved.** The window here is a PCI BAR. The case that
motivated the change - a VL card whose window the chipset may not decode at all
- is still only reachable on the physical 486 or a VLB guest.

## What it cost

The change relaxed a deliberate safety contract: `lgdt` and a write to `cr0`
were banned outright in `scripts/build-vga-survey.ps1`, with the gate self-test
proving they were caught. Both are now permitted for this one sequence. In
exchange the gate pins what the ban was protecting - the `cli`, the clearing of
PE, the `leave_unreal` that takes the 4 GB limit back off DOS, and the self-test
comparison are each asserted present, a store through the flat segment is newly
refused, and `lidt`, CR1-4 and the debug registers stay refused. Eighteen
mutations are rejected where fifteen were before.

That trade was made deliberately and is recorded here so it is not rediscovered
as an accident. The alternative considered and rejected was shipping
`V9XAPER.EXE` alongside the survey and leaving the survey's contract untouched.
