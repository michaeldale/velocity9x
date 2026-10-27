# HD 4600: DDI 7 by default, and the three faults found getting there

Date: 2026-10-27. Machine: the i5-4570 (W9QI5A83, HD 4600), boots 92 to
97. The runtime is DirectX 9.0c. Follows
[`2026-10-26-haswell-ddi7-step5-first-light.md`](2026-10-26-haswell-ddi7-step5-first-light.md)
(step 5 of [`../plans/hardware-tnl.md`](../plans/hardware-tnl.md)).
Evidence:
[`../probe/hsw-4570-ddi7-default-2026-10-27/`](../probe/hsw-4570-ddi7-default-2026-10-27/).

## Decision

On an engine that runs T&L (Haswell), every program that would get
DrawPrimitives2 gets DDI 7. A program listed `=6` (or `=5`) under
`[Velocity9x.Direct3DDdi]` keeps the older level. This is the plan's
decision 1 applied to step 5 once its gate passed. The DX7 SDK samples in
that gate are not on the machine and were not run.

## Gate results

Nothing was listed for any of these except V9XD32's own switch runs.

| Check | DDI 7 | DDI 6 |
|---|---|---|
| V9XD32, no switch (boot 97) | PASS, T1-T17; T16 sees DRAWPRIMITIVES2EX | - |
| V9XD32 `/ddi7` then `/ddi6` then `/ddi7` then `/ddi6` (boot 93) | PASS each | PASS each |
| DirectX 8 and 9 HAL caps (T17) | present, DevCaps 0008A650 | present, 00082650 |
| 3DMark 2000 Pro, default run (boot 92, listed =7) | 2322 | 2342 (boot 88) |
| 3DMark 2001 SE, saved settings (boot 97, default) | 1120 | 1109 (boot 88) |
| Half-Life Direct3D `timedemo mwd5`, windowed (boot 97) | 92.4 | 92.1, 91.8 |

- **Half-Life's first run after a reset.** Each first Direct3D run after a
  reset gave 76 to 78 at DDI 7 (boots 95, 96, 97). The GL path showed the
  same on boot 88 (78.5 against 94-96).
  - So it is the first run after a boot, not the interface.
  - Its cause is not known.
- **V9XD7.LOG on boot 97** holds only the first sighting of the two
  records that arrive, SETRENDERTARGET and CLEAR. No handle went
  unresolved and no record was handed back.

## Faults found on the way

1. **A DDI 6 process after a DDI 7 one could not make a DX7 device**
   (V9XD32 `/ddi6` T16, boot 92).
   - The Miscellaneous2 answer used the last object's level.
   - Like Callbacks3, Misc2 is asked only while an object is made, never
     on a reset: in every log the reset sequence asks neither.
   - Fix: it now latches the level from the process asking, whatever it
     is.
2. **The handle table filled.**
   - In 3DMark 2001 SE, 226 announcements were refused and 684 lookups
     found nothing, and textures went missing (boot 93):
     - DirectX 8's vertex, index and command buffers (execute-buffer
       caps) get handles too;
     - nothing names them by handle, since DP2 passes their LCLs;
     - fix: they are no longer entered.
   - Half-Life's Direct3D mode then passed handle 1,600 on its own
     (boot 95). 512 entries were all the ATI floppy's first disk could
     carry in the DLL's file.
   - Fix: one table per process, 4,096 entries, in that process's own
     memory. A local object and its handles are a process's own anyway.
     The HAL went from 559,104 to 549,376 bytes.
3. **A process ID is not proof of ownership.**
   - 3DMark 2001 SE faulted in V9XHAL reading the first word of a handle
     table at 02AE0000 (boot 96: `cmp dword [eax],0`, EAX 02AE0000).
   - The likely cause: Windows 9x gave 3DMark the ID of a V9XD32 that had
     just exited, and the slot's table was V9XD32's. That fits the fault
     and the order of runs; it was not traced further.
   - Fix: each table carries a mark and its slot's stamp, checked in the
     process before the table is used.
   - Boot 97's identical sequence completed.

## Not checked

- Direct3D titles beyond these: Thief II, the 3DCIF and Glide wrappers'
  hosts, and DX7 games that use TEXBLT or state blocks. None of TEXBLT,
  STATESET or GetDriverState has arrived yet.
- Two DDI 7 programs at once.
- DDHELP restoring a mode after a DDI 7 program dies.
