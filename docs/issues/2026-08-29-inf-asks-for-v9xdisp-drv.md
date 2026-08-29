# The installer asks where to find V9XDISP.DRV

Date: 2026-08-29
Status: **open, not reproduced here. One question to the reporter would settle
the likeliest cause.**
Reported by CentaurHauls on the Intel Pineview machine, installing the vbe
family package.

## Reported

> the driver installer asks where to find V9XDISP.DRV it's either not listed in
> the INF file or the SourceDisks part is wrong

## What the generated INF actually contains

Both halves of that guess are checked and neither is wrong on its face:

```
[SourceDisksNames]
1="Velocity9x Windows 98SE driver-stage disk",,0

[SourceDisksFiles]
v9xdisp.drv=1
v9xmini.vxd=1
v9xsetp.dll=1
v9xhal.dll=1

[Velocity9x.Copy]
v9xdisp.drv,,,12
...
```

`v9xdisp.drv` is listed in `[SourceDisksFiles]` against disk 1, and it is in
the copy section. The `[SourceDisksNames]` line has no tag file, which looks
like an omission until you compare it with the DDK's own display sample -
`98DDK\src\display\mini\s3v\S3VSMP.INF` writes exactly the same shape:

```
1="S3 ViRGE Sample Windows 98 Display Driver",,0
```

And the built package is a single flat folder: `V9XDISP.DRV` sits beside
`VELOCITY9X.INF`, along with the other three copied files and the tools.

So the INF matches the reference and the file is present. Something else is
producing the prompt.

## The question that would settle it

**Was the install run from the folder copied onto the hard disk, or straight
off the floppy or CD?** The floppy instructions already carry this warning, in
the disk's own text:

> Copy the folder for your chip to the hard disk first - installing from a
> floppy works, but Windows may ask for the disk again later.

If the answer is "from the floppy", this is the documented behaviour rather
than an INF defect, and the fix is to make that warning harder to miss. If the
answer is "from a folder on the hard disk", the INF or the class installer is
at fault and this needs reproducing in a guest.

Also worth having: **the exact path the prompt offered**, which says where
Setup thought the source was.

## Not guessed at

The fourth field of the copy entries (`,,,12`) is a flag word, and whether it
is the right one for a display driver on SetupX is not established here.
Changing it on a hunch is the kind of INF churn that has cost this project time
before - see `docs\issues\...win9x-inf` history and the OEM`<n>`.INF
proliferation note - so it stays as it is until the question above is answered
and, if needed, the fault reproduces in a guest where an INF can be iterated
safely.
