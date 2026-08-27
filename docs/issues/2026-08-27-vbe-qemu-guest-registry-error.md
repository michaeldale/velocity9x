# The vbe QEMU guest will not boot into Windows: registry/configuration error

Date: 2026-08-27
Status: **open** - blocks the `vbe` enable gate; not a driver defect

## What happens

`Win98SE-QEMU-StdVGA` (QEMU 4.2.0, host `127.0.0.1:9872` forwarded to guest
9869) boots as far as the Windows 98 Startup Menu and stops there with the
default selection on **Command prompt only**:

```
Warning: Windows has detected a registry/configuration error.
Choose, Command prompt only, and run SCANREG.
```

The menu times out into that choice, so the guest lands at `C:\>` in DOS. The
agent never starts, and the forwarded port accepts a connection while nothing
answers behind it - which is why the symptom first looks like a network problem.
Screenshot:
[`docs/images/vbe-qemu-guest-registry-error-startup-menu.png`](../images/vbe-qemu-guest-registry-error-startup-menu.png),
taken through the QEMU monitor's `screendump`, which is the only way to see this
guest's console since it runs `-display none`.

## Why this is filed rather than fixed

The repair Windows itself suggests is `SCANREG /RESTORE`, which rolls the
registry back to one of its dated backups. On this particular guest that is not
obviously safe: the handoff
([`2026-08-23-qemu-win98-stage1-guest-handoff.md`](../handoffs/2026-08-23-qemu-win98-stage1-guest-handoff.md))
records that the NIC installation was completed in an untracked session, so a
restore point predating it would take the guest's networking - and with it the
agent, and with it any way to reach the machine remotely. Trading a guest that
cannot be reached for a guest that cannot be reached *and* has lost its only
recorded configuration step is not a repair.

Choosing a restore point wants someone who can watch the console interactively.

## What it blocks, and what it does not

It blocks the **`vbe` family enable gate**, which has not run in the
`gdi-accel-*` series. `scripts/run-family-enable-gate.ps1` refuses this target
by design - it launches 86Box profiles only - and says so plainly rather than
appearing to pass.

It does **not** block anything about the driver. `vbe` is an engine-less family:
its `EngineType` is `NONE`, so every GDI acceleration primitive declines at the
first gate, and the code path under test is the same one `ati` and `matrox-m2`
exercise. `ati/mach64-vt2` passes its enable gate and its full six-mode matrix on
the current build, which covers the engine-less behaviour on a guest that boots.

The `vbe` gate is therefore about coverage of that family's own packaging and
INF, not about the acceleration work.

## Note on an earlier over-claim

An earlier session recorded in `docs/BUILDING.md` that the `vbe` family was
blocked, on the strength of one QEMU image reporting "display adapter is not
configured properly". That was corrected at the time: the family had been tested
and was working, and the guest image is the suspect part. This issue is about
the **guest**, and should not be read as a statement about the family.
