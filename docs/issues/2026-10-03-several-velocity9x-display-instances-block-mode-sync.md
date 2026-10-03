# Several marked Velocity9x display instances stop the mode-list sync

Date: 2026-10-03. Reported by MarxVeix on an ATI 3D Rage XL AGP
(0.10.0). Evidence:
`docs/probe/rage-xl-agp-marxveix-2026-10-03/V9XSYNC.INI`. Status: open.

## Symptom

`V9XSYNC.INI`: `Status=no-op`, `Reason=multiple-marked-instances`.
`v9x_find_marked_instance` (`tools/diag/settings_syncmodes.c`) found more
than one `Display\NNNN` class key with `V9xFamily=ati`. It then
refuses to choose, so the probed mode table (`V9XMODES.INI`, 19 rows)
is not written into the registry, and Display Properties keeps whatever
list it had.

## Probable cause

The driver installed more than once: a reinstall, or Windows
re-detecting the card, leaves an older instance key behind. The same
proliferation of `OEM<n>.INF` and instance keys has been seen before
(memory: Win9x INF/SetupX traps). Not confirmed with the reporter.

## Next

- Ask the reporter how many times the driver was installed, and for the
  `HKLM\System\CurrentControlSet\Services\Class\Display` subkeys.
- Decide whether the sync should choose the instance bound to the
  running devnode, rather than refuse when there are several, and test
  that choice before changing it.
