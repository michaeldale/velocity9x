# Every package claims "GUEST ACTIVATION NOT YET TESTED", including ones activated on guests

Filed: 2026-08-30
Status: open
Affects: `scripts\build-active-package.ps1:250`, and therefore every
`MANIFEST.TXT` and every generated `releases\<version>\README.md`.

## What it says

```powershell
"Status: HOST-AUDITED; GUEST ACTIVATION NOT YET TESTED",
```

One hardcoded string, written into every family's package manifest. The
release index's `Tested` column is read straight back out of it, so it reaches
the download page too.

## Why that is now wrong

At 0.6.5 it is false for two of the four families:

- **s3** was installed and activated on `Win86SE` (ViRGE/DX) across roughly
  twenty boots and on `Win98SE-Trio64`, with the Direct3D pixel ladder, the
  DirectDraw probe and Final Reality all run against it.
- **ati** was installed and activated on `Win98SE-Mach64VT2`: `Stage=enable-ok`
  and a green DirectDraw probe.

`vbe` and `matrox-m2` have not been activated this cycle, so for those two the
string is accurate.

The error is in the safe direction - it understates what was tested rather than
overstating it - which is why this is an issue and not a defect. But a status
line that cannot be wrong in the dangerous direction also cannot be right, and
this project's whole discipline is that a published claim tracks evidence.

## What it should be

Per-family data in `packaging\families\<id>\family.psd1`, beside the other
per-family strings the packager already reads, so that changing what a family
claims requires editing that family's manifest and is visible in the diff.
`build-active-package.ps1` would read it rather than emitting a literal.

Deliberately not done as part of cutting 0.6.5: changing what goes inside the
published artifacts, on the same run that publishes them, is how a release ends
up not being the thing that was audited. The 0.6.5 release notes state the
actual guest testing instead.
