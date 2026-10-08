# Every package installs GLIDE2X.DLL to SYSTEM, and SetupX keeps a 3dfx card's own

Date: 2026-10-09
Machines: 86Box `Win98SE-Fast-D3D` guest, boot 602; 86Box
`Win98SE-Trio64` guest, boots 359-361.
Evidence: [`../probe/86box-glide-inf-copy-flag-2026-10-09/`](../probe/86box-glide-inf-copy-flag-2026-10-09/),
[`../probe/86box-glide-have-disk-2026-10-09/`](../probe/86box-glide-have-disk-2026-10-09/)
Plan: [glide-2x-wrapper.md](../plans/glide-2x-wrapper.md), Phase 5.

## Why

Glide games load `GLIDE2X.DLL` by name from SYSTEM. Velocity9x's must sit
there to be found, but a Voodoo 1 or 2 is an add-on card beside the 2D
card a Velocity9x package installs for, and its own `GLIDE2X.DLL` is
already in SYSTEM. Overwriting it would take Glide away from the Voodoo.

Chosen with Michael: SYSTEM, without overwriting a 3dfx DLL, in every
family package.

## The copy flag

The other files use 12 (NOVERSIONCHECK | FORCE_FILE_IN_USE). The Glide
line uses 40: COPYFLG_NO_VERSION_DIALOG (0x20, "do not copy if target is
newer", `98DDK\inc\win98\SETUPAPI.H`) plus FORCE_FILE_IN_USE. Not
NO_OVERWRITE (0x10): that would never replace an older Velocity9x Glide.
The decision then rests on the version comparison. Ours carries the
Velocity9x version (0.14.0). The 3dfx DLLs on hand carry 1.00.01.0106,
2.56.00.0459, 2.61.00.0658 and 2.61.00.2704.

Measured through SetupX with a one-line INF on the guest:

- Over 3dfx 2.56, 2.61 and 1.00: kept, with nothing staged and no prompt.
- Over a Velocity9x 0.13.0 build: replaced, staged through `WININIT.INI`
  for the next boot, as the in-use flag stages the other driver files.
  The restart prompt is the one a display install shows anyway.

## Have Disk, the whole package

On the `Win98SE-Trio64` guest (boots 359-361), the S3 package was
installed twice through Display Properties, Adapter, Change, Have Disk
([evidence](../probe/86box-glide-have-disk-2026-10-09/)):

- Over the 3dfx 2.56 the guest already had: kept, with no prompt and
  nothing staged for it. The rest of the package installed and the
  driver started (`DriverInitResult=ok`).
- Over a 0.13.0 build of ours: staged through `WININIT.INI` and replaced
  at the restart by the package's DLL, confirmed by hash. The driver
  started.

## Not measured

- Windows 95's SetupX.
- `V9XCOPY.BAT`'s check. It cannot compare versions, so it replaces an
  existing `GLIDE2X.DLL` only if `FIND` sees `V9XGLIDE.LOG` in it, a
  string in ours and in none of the four 3dfx DLLs. If `FIND.EXE` is
  missing it keeps the file. It has not been run in DOS.

## Limits

- At Velocity9x 1.0 the comparison reaches 3dfx's own range. The INF
  comment says so.
- A family claiming a 3dfx chip (vendor 121A) is refused by
  `check-tree.ps1` until its Glide coexistence is decided.

## Also changed

The DLL was 468 KB, of which about 430 KB were zero-filled tables that
wlink writes into the image (`_BSS` in DGROUP), and it overflowed the
family floppies. The texture, record and surface tables are now
committed with VirtualAlloc at attach. The DLL is 79 KB. NFS II SE raced
on the netbook (boot 129) with it: 266-270 swaps per 15 s, nothing
refused, everything drawn, as before.

## Gates

- `run-checks.ps1` green. Each package carries `GLIDE2X.DLL`, and its
  INF self-check requires `glide2x.dll,,,40` and `glide2x.dll=1`.
- The floppies build: 1,280,988-1,292,976 bytes each.
