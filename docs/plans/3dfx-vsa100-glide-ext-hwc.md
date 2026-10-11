# VSA-100 Phase 4: 3dfx's own Glide on Velocity9x through `EXT_HWC`

Date: 2026-10-11

Status: step 1 done
([2026-10-11-shipped-glide-asks-querydevnode-before-ext-hwc.md](../decisions/2026-10-11-shipped-glide-asks-querydevnode-before-ext-hwc.md)):
the shipped Glide needs `QUERYDEVNODE` answered before it sends `EXT_HWC`;
that answer is in. Step 2 next. Parent
plan: [3dfx-vsa100-family.md](3dfx-vsa100-family.md), Phase 4. Protocol:
[vsa100-ext-hwc.md](../specifications/vsa100-ext-hwc.md). Boundaries:
[win9x-driver-boundaries.md](../specifications/win9x-driver-boundaries.md).

## What is already true

- **The target DLLs are 3dfx's Glide 2.61.00.0658 and 3.10.00.0658**, in
  A8U4I5's `C:\WINDOWS\SYSTEM` from the 1.04.00 install (240,640 and 331,776
  bytes). Build 0658 is the build the open Glide tree was taken from.
- **Both DLLs contain `push 3DF3h` and `push FD3h`, the strings `ExtEscape`
  and `H5VDD`, and no `EnumDisplayDevices`** (byte search of the shipped
  files, 2026-10-11). So the escape numbers hold, and display enumeration is
  the pre-fork path (`GetDC(NULL)` per the spec's Win95 branch), not the
  open tree's.
- **The game folders on A8U4I5 carry Velocity9x's own wrappers** (Diablo II
  and Rollcage `glide3x.dll` 83,968 bytes, NFS II demo `glide2x.dll` 79,360),
  which Windows loads before SYSTEM's. Tests of 3dfx's Glide therefore use a
  probe that loads `C:\WINDOWS\SYSTEM\GLIDE3X.DLL` by path, not those games,
  until the plan reaches its game gate.
- **A ring-3 process can already use mini-VDD mappings.** The HAL, running in
  each application's process, dereferences `control_linear_base` and the
  framebuffer's linear base, both from `_MapPhysToLinear` in the mini-VDD.
  That is what `GETLINEARADDR` needs.

## Where each part lives

| Piece | Layer | Why |
|---|---|---|
| Escape entry for `3DF3h`, `FD3h`, `8001h` | `dd16.c` `Control`, 3dfx build only | The existing escape boundary (OpenGL's `GETINFO` is there). |
| Request decode and reply build | New pure module, `src\chipsets\tdfx\tdfx_hwc.c`, host-tested | Layout and values are policy: offsets, the device-config reply, refusals. |
| BAR0 and BAR1 mappings for Glide | Mini-VDD, a new map function | Mapping is the mini-VDD's job; it already maps BAR0's 2D block and the framebuffer. |
| Exclusive take and release | 16-bit, the shared block's engine flags | GDI and the HAL must stop touching the chip while Glide owns it; both already honour `V9X_DD_ENGINE_VALID`. |
| Desktop restore | Glide's own `ChangeDisplaySettings(CDS_RESET)` after release, reaching our Enable | No new code expected; to be measured. |
| `PCI_OP` | 16-bit, bounded to function 0 and the six offsets Glide writes | Config writes on behalf of any process with a DC must be narrow. |

No new external symbol beyond the escape handler's call into the pure module.

## Steps

1. **Logging stub and session probe.** `Control` records every `3DF3h`,
   `FD3h` and `8001h` call in `C:\V9XDIAG\V9XHWC.INI` (sub-function and the
   64-byte request) and declines it. `V9XGL3P.EXE` loads a named
   `GLIDE3X.DLL`, installs an error callback that records and exits, and runs
   `grGlideInit`, `grSstSelect`, `grSstWinOpen`, one clear and swap,
   `grSstWinClose`, `grGlideShutdown`, recording each step. Expected: one
   `GETDEVICECONFIG` request, declined, then Glide's fatal error. This
   confirms the request shape on the shipped DLL.
2. **Discovery and mapping.** Answer `GETDEVICECONFIG` (vendor `121A`,
   device `0009`, `fbRam` = usable bytes, one chip) and `GETLINEARADDR`
   (BAR0 32 MiB and BAR1 `fbRam`, mapped by the mini-VDD). `SHARE_CONTEXT_DWORD`
   answers -1, Glide's documented "no", until a lost-context word exists in
   shared memory. Expected: `grGlideInit` succeeds; `grSstWinOpen` reaches
   `HWCSETEXCLUSIVE`.
3. **Exclusive and release.** Take: clear the engine's valid flag for GDI and
   the HAL, record it. Release: set it back after Glide's mode set restores
   the desktop. `PCI_OP` writes for the six offsets, function 0 only;
   `UNMAP_MEMORY` accepted.
4. **Gate.** The probe draws a frame and returns a usable desktop; then the
   Phase 1 games with 3dfx's DLL in their folders (each wrapper saved first).

## Open before step 2

- Whether the 16-bit `Control` receives the 64-byte request and the 44-byte
  reply as far pointers it can write, and what `ExtEscape` returns for a
  given `Control` return. Step 1 measures the first.
- Whether GDI or the DIB Engine draws to the screen while Glide holds it,
  and so whether clearing the engine flag is enough.
