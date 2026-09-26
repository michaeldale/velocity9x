# The floppy transfer folder no longer fits once the OpenGL ICD is packaged

Found 2026-09-26 while making V9XGL.DLL part of every package. Closed
2026-09-27: one family per disk (below), built.

## Symptom

`scripts/run-checks.ps1` stops at the floppy step:

```
The floppy folder is 2,413,283 bytes, over the 1,457,664 usable on a
1.44 MB floppy.
```

`scripts/build-floppy-package.ps1` copies the whole ATI, S3 and VBE
packages (the families whose manifests set `Floppy.Include`) onto one
disk. Before the ICD each was about 480 KB and the disk was already
close to full; with V9XGL.DLL each is about 800 KB:

| Floppy folder | Bytes |
|---|---|
| ATI | 800,708 |
| S3 | 805,102 |
| VBE | 801,830 |

## Why the DLL is large

V9XGL.DLL is 321,024 bytes. Its link map (`build/opengl/v9xgl.map`):

| Segment | Size |
|---|---|
| _TEXT | 36,674 |
| CONST + CONST2 + _DATA | 9,938 |
| _BSS | 268,224 |

Zero-initialised statics account for about 262 KB of the file: the
16-entry context table (each context holds the pipeline's 64-triangle
batch, the held batch of the same size, the matrix stacks and the
texture tables), the 2,048-entry texture-copy table and the log's state
tables. The linker writes _BSS into the image rather than leaving it
virtual, so the file carries the zeros.

## Options

1. Allocate the context table (and the other large tables) from the
   process heap at attach, so the image carries only code and constants:
   about 50 KB. Keeps one package layout for the floppy and the network.
2. Make the linker emit _BSS as uninitialised (a section flag or segment
   class change), with the same result and no code change, if Open
   Watcom's `format windows nt` link allows it.
3. Leave the ICD out of the floppy copy. Rejected as the first choice:
   the INF copies `v9xgl.dll`, so an install from the floppy would fail
   when SetupX cannot find it, unless the floppy got its own INF.

Option 1 or 2 alone brings each package back to about 530 KB, which is
still over the 1.44 MB disk for three families (about 1.6 MB). So the
floppy also needs one of: fewer families per disk, the diagnostic tools
(V9XDDP, V9XTRACE, V9XGDI, V9XMSW, V9XPAL, V9XPWR, V9XWND, V9XPROBE,
V9X16LD, V9XSTAGE) shared once at the disk root instead of per family,
or a second disk.

## Decision (Michael, 2026-09-26)

One family per disk. The floppy build will produce one disk image per
family with `Floppy.Include`, each holding that family's full package,
the ICD included, instead of every family on one disk. At about 800 KB a
package (530 KB once the ICD's tables come off the image) that fits with
room to spare, and the INF on each disk can copy `v9xgl.dll` as it does
from the network package. Shrinking the ICD stays worth doing, but is no
longer needed for the floppy to fit.

## Resolution (2026-09-27)

`scripts/build-floppy-package.ps1` now makes one disk per family under
`build/floppy/<Folder>`: the package in its folder, `RECOVER.TXT`, and a
`README.TXT` naming that family's chips and hardware IDs. Each disk is
checked against the 1,457,664 usable bytes on its own; `-Zip` makes one
archive per disk. Measured on the first build: ATI 806,874, S3 811,342
and VBE 808,044 bytes. The packaging change went in with it, and the
full gate passes. The ICD's zero-filled image tables are still worth
moving to the heap, for a smaller package, but the disks no longer
depend on it.
