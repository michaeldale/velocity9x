# Write-combining through the PAT on Gen3 to Gen5, for BIOSes that leave no MTRR

Date: 2026-11-20
Status: **built, gate green; the PAT route not yet run on a Gen3-5 part.**
Branch `intel-gen3-5-pat-wc`.

## Why

GitHub issue 28's Acer Aspire 4740 (Ironlake mobile, `8086:0046`) on 0.56.2:
its boot record reads `MtrrWrite=refused` with reason 12 (`NO_SLOT`). Its BIOS
uses all eight MTRR pairs (`Mtrr=... n=8`, eight ranges listed), and the
aperture at `A0000000` falls under the UC default. So every CPU store to the
aperture (surfaces, textures, the ring, batches and vertices) ran uncached.
On the desktop Ironlake the same stores write-combined took 3DMark 2000 from
1246 to 2909 (`2026-10-25-ironlake-write-combining.md`). The reporter's
3DMark 99 ran 1708 at 640x480x32 and 313 at 1366x768x32.

The HD 4600 met the same BIOS shape in October and got the PAT route
(`2026-10-28-haswell-pat-write-combining.md`); the `vbe` package has it
too. The Gen3-5 rows had `write_combine` 1 and no fallback, so a refusal was
the end of it.

## What changed

- `gma950_hw16.c`: `v9x_i9xx_write_combine`, the Haswell route for Gen3-5.
  When enable16.c's MTRR plan refuses for a BIOS reason (`ALREADY_COVERED`,
  `NO_SLOT`, `DEFAULT_NOT_UC`; `src\common\pat.c` decides), the aperture's
  DPMI mapping is typed WC through PAT entry 4 (`PatWrite=`). The ring
  reserve, which the mini-VDD maps separately (1 MiB from the reserve's
  start, `V9xMiniI9xxRingOpen`), is typed under the same plan when the
  descriptor opens the ring (`PatReserve=`), as Haswell types its engine
  reserve. The submit path already drains before `RING_TAIL` and each flip.
- Every Gen3-5 row takes it: GMA 900, 950, 3100, 3150, X3100, X4500, 4500,
  4500MHD, Ironlake desktop and mobile. Same design, same fix; on a machine
  whose MTRR plan succeeds (every one measured so far) it is never called.
- `family.psd1`: `src\common\mtrr.c` moves to LEAFCODE, as in the ati
  family. The fallback took intel-gma's `_TEXT` to 58,023 bytes, 679 over
  its 57,344-byte budget; `mtrr.c`'s entry points are already
  `V9X_LEAF_FAR`.

## Measured

- `run-checks.ps1`: passes on the branch.
- GMA 950 netbook, release probes on the branch build (`fda45491-dirty`,
  boot after install), for the `mtrr.c` move only: every probe passes, no
  verdict changed since the 0.58.0 checks, and `MtrrWrite=ok s=2
  b=d0000001 m=fc000800` (64 MiB at `D0000000`, WC) as before. Its MTRR
  plan succeeds, so the PAT route does not run there. Evidence:
  `docs\probe\i9xx-pat-wc-2026-11-20\gma950-checks\`.

## Not established

- The PAT route on any Gen3-5 part. Nothing here has a BIOS that refuses
  the MTRR plan; the first run will be the issue 28 laptop. What to read
  there: `PatWrite=ok pages=...` and `PatReserve=ok pages=...` in
  `V9XBOOT.INI`, and 3DMark 99 again at 1366x768x32.
- Aliasing: the reserve is reached through two linear mappings (the
  aperture's and the ring's), both typed WC under this route, as on Haswell.
