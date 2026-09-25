# 98SE's DDRAW.DLL imports the Win16 mutex by ordinals 93/97/98, and its OPENGL32 looks up all eighteen Drv* names and one registry value

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`, Phase 0.2 (first half) and 0.1 (the
static half)
Evidence: `2026-09-26-98se-ddraw-opengl32-imports.txt` (the parser output)

Instrument: `v9xctl get` of `C:\WINDOWS\SYSTEM\DDRAW.DLL` and `OPENGL32.DLL`
from two 86Box Windows 98 SE guests, `Win86SE` (port 9869, ViRGE/DX) and
`Win98SE-Fast-D3D` (port 9878), parsed host-side by a PE32 import/export
walker written for the purpose. The DX3 `ddraw.dll` (4.03.00.1096) in
`build\hellbender-cd` is the cross-check. This is a **static** measurement
of two files; nothing ran in the guest.

## Measured

Both guests hold byte-identical files (CRC32 `7CFF70EC` for DDRAW,
`02F464BB` for OPENGL32).

**DDRAW.DLL 4.06.03.0518** (DirectX 6.1a's DirectDraw), 299,008 bytes:

- Image base `0xBAAA0000`, the same as DX3. `.data` and `.rsrc` are marked
  `IMAGE_SCN_MEM_SHARED`; `.text`, `.edata`, `.reloc` are not.
- KERNEL32 imports: 90 by name and **3 by ordinal: 93, 97, 98**, which are
  `GetpWin16Lock`, `_EnterSysLevel`, `_LeaveSysLevel` per the sources in
  `2026-09-26-opengl-icd-interface-research.md` §6. DX3 imports the same
  three. No other DLL in either build is imported by ordinal.
- 33 named exports, including `AcquireDDThreadLock`, `ReleaseDDThreadLock`,
  `GetSurfaceFromDC`, `GetAliasedVidMem`, `GetNextMipMap`,
  `HeapVidMemAllocAligned`, `DDHAL32_VidMemAlloc`/`Free`,
  `DDInternalLock`/`Unlock`, `InternalLock`/`Unlock`, `D3DParseUnknownCommand`,
  the `VidMem*` heap family and the thunk data. None of them is a Win16-lock
  helper by name.

**OPENGL32.DLL 4.00** ("OpenGL Client DLL"), 753,808 bytes, image base
`0x78A80000`, no shared section:

- Imports `DCIMAN32.dll`: `DCIOpenProvider`, `DCICloseProvider`,
  `DCICreatePrimary`, `DCIDestroy`, `DCIBeginAccess`, `DCIEndAccess`,
  `WinWatchOpen`, `WinWatchClose`, `WinWatchGetClipList`,
  `WinWatchDidStatusChange`. This is how the generic renderer reaches the
  primary and its clip list on 9x.
- Imports `GDI32.dll` `ExtEscape`, `DescribePixelFormat`, `GetPixelFormat`,
  `SwapBuffers`, and `ADVAPI32.dll` **only** `RegOpenKeyExA`,
  `RegOpenKeyExW`, `RegQueryValueExA`, `RegQueryValueExW`, `RegCloseKey`.
  There is no `RegEnumKey*`, `RegEnumValue*` or `RegQueryInfoKey`.
- Contains the string `Software\Microsoft\Windows\CurrentVersion\OpenGLDrivers`
  in ASCII and `Software\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers`
  in UTF-16, and **no** strings `Dll`, `DriverVersion`, `Version` or `Flags`
  as value names (`Flags` occurs once, inside `glsCaptureFlags`).
- Contains all **eighteen** `Drv*` names: the sixteen ReactOS requires plus
  `DrvValidateVersion` and `DrvSetCallbackProcs`.
- Exports 336 `gl*` entry points plus `DllInitialize` and 24 `wgl*`
  functions, including `wglGetDefaultProcAddress`.
- Contains ten `MCD*` symbols (`MCDDrawPixels`, `MCDTextureKey`, ...) and the
  string `MCD32.DLL`.

## What this settles

- **The ICD's Win16-mutex route is the one DirectDraw itself uses on 98SE.**
  Resolving KERNEL32 ordinals 93/97/98 by walking its export table, as the
  plan's Phase 3 says, is not an exotic path: the DirectDraw runtime the HAL
  already lives under links to exactly those three ordinals.
- **9x opengl32 reads one registry value, not an NT-style subkey.** It can
  open a key and query a value by name, and cannot enumerate; and it carries
  none of the NT value names. That is the vmdisp9x model (`control.c`
  comments) and the model every vendor INF in the research note writes.
  What the value's *name* is (the string the display driver returns from
  `OPENGL_GETINFO`) is still the escape trace's to confirm.
- **9x opengl32 looks up `DrvValidateVersion` and `DrvSetCallbackProcs`**, so
  the ICD exports both; whether either is mandatory is not shown by a
  string table and is left to the Phase 0.9 probe.
- The plan's guest list for Phase 0.9 is right to start on 98SE: DX6.1a is
  what a stock 98SE guest has, and both guests are at the same version.

## What this does not establish

- That DDRAW **holds** the mutex around the HAL's Blt, Flip, CreateSurface
  and Direct3D callbacks. Importing the functions says it takes the lock
  somewhere; the `_ConfirmWin16Lock` trace inside the callbacks (Phase 0.2,
  second half) is still owed.
- Anything about original Windows 98 or ME, whose DDRAW builds may differ.
- Whether the `MCD*` code in this opengl32 is reachable on 9x. The
  2026-08-30 prior-art note's claim that MCD is NT-only stands as a claim
  about *driver models shipped*, not about this binary; nothing here tests
  it, and the plan does not depend on it.
- The exact `OPENGL_GETINFO` layout: string presence cannot show structure
  offsets. Phase 0.1's escape trace does that.

## Standing

Phase 0.2's static half is done for 98SE. The 16-bit `Control` trace build
for Phase 0.1 and the `_ConfirmWin16Lock` HAL trace for the rest of 0.2 are
the next measurements, both on the same two guests.
