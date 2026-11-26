# Oland: V9XDDP's /mixed and /zprivate checks never run; the step 5 record misstates the zero keys

Date: 2026-11-17

Machine: W9QI0DAD (Oland XT `1002:6610`). Status: open (a record
correction, and two checks to run).

## The correction

[2026-11-17-oland-draw-shader-ddi8.md](../decisions/2026-11-17-oland-draw-shader-ddi8.md)
says of V9XDDP at boot 58: "80 keys. All are 1 except
`MipTriDegradedOk=0`". Decision records are not edited, so the
correction is here.

`ddp-boot58/V9XDD.INI` has 105 `...Ok=` keys. Besides `MipTriDegradedOk`,
eight more are 0. They were 0 at boot 58 and on 2026-11-17's run on the
read-pointer fix (`../probe/oland-cp-stall-w9qi0dad-2026-11-17/ddp-rptr-wb/`,
every Ok key identical):
- `D3DZCompareOk`, `D3DZWriteMaskOk`;
- `D3DZPCompareOk`, `D3DZPWriteMaskOk`;
- `MixedOrderingOk`, `MixedColorEncodingOk`, `MixedDepthEncodingOk`,
  `MixedOk`.

`D3DShapesOk` is 12, a count, not a flag.

## What the zeros are

Seeds, not failures. Carrizo's record found the same rows,
[2026-10-29-carrizo-ddp-zero-rows.md](../decisions/2026-10-29-carrizo-ddp-zero-rows.md).
V9XDDP writes these keys with `DDERR_UNSUPPORTED` (88760231h) before
running anything, and every HRESULT in the three blocks still holds it.
- **`D3DZ*` in `V9XDD.INI`.** The default depth check writes its real
  results to `V9XDD2.INI`. There, fetched from the Oland after the
  2026-11-17 run, `D3DZCompareOk=1` and `D3DZWriteMaskOk=1`, with every
  HRESULT 0 (`ddp-rptr-wb/V9XDD2.INI`). The guest's file date was not
  checked, so that the file is from this run is assumed, not shown.
- **`D3DZP*`** are written only under `/zprivate` (`D3DZPrivateRun=0`).
- **`Mixed*`** are written only under `/mixed` (`MixedRun=0`).

`MipTriDegradedOk=0` is expected on a part that blends between mip
levels (Carrizo's record).

## Next

Run `V9XDDP /mixed` and `V9XDDP /zprivate` on the Oland once each.
- `/mixed` is a HAL draw, then a CPU write through Lock, then a second
  HAL draw. It tests Lock of a tiled Z buffer: Carrizo needed Lock to
  detile its Z buffer before this passed.
- `/zprivate` tests depth through a private device.

## 1.0 re-read, 2026-11-26

Moved after 1.0: listed in the 1.0 plan's "Not in 1.0". A gap in probe
coverage with no defect seen; listed in the 1.0 plan.
