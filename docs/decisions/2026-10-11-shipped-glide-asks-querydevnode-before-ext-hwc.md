# 3dfx's shipped Glide asks QUERYDEVNODE before it sends EXT_HWC

Date: 2026-10-11. Machine: A8U4I5, boots 416-418, Velocity9x `3dfx` with the
2D engine. DLL: 3dfx `GLIDE3X.DLL` 3.10.00.0658 from `C:\WINDOWS\SYSTEM`
(331,776 bytes), driven by `V9XGL3P.EXE` (`scripts\build-glide3-session.ps1`).
Evidence: [a8u4i5-voodoo4-glide-2026-10-11](../probe/a8u4i5-voodoo4-glide-2026-10-11/).
Plan: [3dfx-vsa100-glide-ext-hwc.md](../plans/3dfx-vsa100-glide-ext-hwc.md),
step 1. Protocol: [vsa100-ext-hwc.md](../specifications/vsa100-ext-hwc.md).

## Measured

`dd16.c`'s `Control` logged every `3DF3h`, `FD3h` and `8001h` call to
`C:\V9XDIAG\V9XHWC.INI`.

| Boot | Driver answer | Glide's escapes during `grGlideInit` | Outcome |
|---|---|---|---|
| 416 | all declined | `8001h` sub-function 3 (`QUERYDEVNODE`), 32+ times; no `3DF3h` | still in `grGlideInit` at 90 s |
| 417 | all declined | `QUERYDEVNODE` 64-127 times, `3DF3h` 0 times | an empty "Error" message box (screenshot) |
| 418 | `QUERYDEVNODE` answered with the VDD's devnode | `QUERYDEVNODE`, then `3DF3h` with `which` = 2, then `FD3h` the same, both declined; then more `QUERYDEVNODE` (190 by the 25 s read) | the same empty "Error" box |

The 64-byte `EXT_HWC` request arrived whole (16 DWORDs readable): offset 4
held 2, `GETDEVICECONFIG`. Offsets 8 and 12, which the open tree's
`hwcExtRequest_t` names `dc` and `devNo`, held `BFF7A567` and `81EF5000`,
neither a 16-bit HDC; they look like stack contents.

Both shipped DLLs (Glide 2.61.00.0658 and 3.10.00.0658) contain
`push 3DF3h`, `push FD3h`, `ExtEscape` and `H5VDD`, and do not import
`EnumDisplayDevices` (byte search).

## What this shows

1. **`QUERYDEVNODE` is a gate in the shipped DLL.** Declined, Glide never
   sends `EXT_HWC`; it ends in the empty error box the spec predicted for a
   driver that answers nothing. The open tree's Win98 path does not have
   this order, which is why the spec did not predict it.
2. **The sub-function sits where the spec says** (offset 4), and the
   `EXT_HWC_OLD` fallback is real.
3. **Glide's error callback was not the probe's.** `grErrorSetCallback`
   was called before `grGlideInit`, and the default box still appeared:
   either the shipped DLL resets the callback during init, or this failure
   path does not use it. A remote run must expect a dialog to dismiss.
4. **A 16-bit `Control` can read Glide's 64-byte request** through the
   pointer GDI passes; the selector limit allowed all 16 DWORDs.

## Not established

- Whether offsets 8 and 12 are ever meaningful in the shipped DLL's request.
- What 3dfx's driver returned for `QUERYDEVNODE`'s gamma fields.
- Glide 2's sequence (only Glide 3 was run).
