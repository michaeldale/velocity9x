# VSA-100 `EXT_HWC`: what stock h5 Glide asks of a Win9x display driver

Date: 2026-10-11
Status: desk only. Nothing here is measured. Every statement is read from
the open Glide source; how far it holds for the 3dfx-built `glide2x.dll` /
`glide3x.dll` that games ship with is itself a prediction (see
"Provenance").

Specification for Phase 4 of [the VSA-100 plan](../plans/3dfx-vsa100-family.md).

## Sources

| Tag | What | Cited as |
|---|---|---|
| T | Open Glide tree, `C:\everything\claude\personal\3dfx driver research\artifacts\3dfx_glide\`, git `0f26d59` (2023-03-18). 3dfx Glide General Public License: facts only (names, constants, layouts, call order), no code. | path:line, relative to the tree root |
| MH | `h5/minihwc/minihwc.c` | MH:line |
| HX | `h5/minihwc/hwcext.h` | HX:line |
| WM | `h5/minihwc/win_mode.c` | WM:line |
| QM | `h5/minihwc/qmodes.h` | QM:line |
| G3 | `h5/glide3/src/` | G3/file:line |
| G2 | `h5/glide/src/` (h5 Glide 2) | G2/file:line |

Not used: anything under `C:\everything\3dfx-driver` (standing rule).
No binary, no INF and no 3dfx display driver was examined.

## Provenance

T is a community fork (README.md:1-12) of KoolSmoky's tree, which is 3dfx's
last minihwc (newest 3dfx log entry 11/08/00, MH:49) plus 2001-2002 changes.
Fork changes that touch the driver interface:

- Mode set by `ChangeDisplaySettingsEx` instead of DirectDraw
  (WM:23-24, WM:438). 3dfx's binaries presumably set the mode through
  DirectDraw; the commented "Enumerating Display Modes" step (WM:434) is
  the remains of it.
- Hardware detection moved from `_GlideInitEnvironment` into `grGlideInit`
  (G3/diglide.c:353-361, G3/gpci.c:2058-2063).
- Multi-monitor enumeration via `EnumDisplayDevicesA` and
  `FX_GLIDE_SSTSELECT` (MH:31-33, MH:1465-1673). The `EnumDisplayMonitors`
  callback `monitorEnum` (MH:1171) is the older path, now used only where
  `EnumDisplayDevicesA` is missing (Win95, NT4; MH:1649-1656).
- NT5.1 dummy-context handling (NT only), extra resolutions and 144 Hz.

**h5 Glide 2 is present**: `h5/glide/src`, built by `glide2x.bat` with
`FX_GLIDE_HW=h5`, `FX_HW_PROJECTS=glide` (glide2x.bat:11, :18) against the
same `h5/minihwc` (glide2x.bat:32-40). It calls the same hwc entry points
(G2/gpci.c:831-870, G2/gsst.c:1543-1817, :2442-2485, DllMain
G2/gpci.c:525-537), so the escape protocol is identical. The sequence below
was traced in Glide 3 only; Glide 2 additionally writes the graphics PLL
directly when its `grxClk` environment setting is present
(G2/gsst.c:1797, MH:7080-7104).

## 1. Build configuration

| Define | Value in T | Source | Effect on the driver interface |
|---|---|---|---|
| `HWC_EXT_INIT` | on (because `HWC_INIT_VXD` is commented out) | config.mak:221; minihwc makefile:54-60 | Discovery and mapping through `ExtEscape`; no PCI scan, no VxD |
| `HWC_ACCESS_DDRAW` | on for Windows | minihwc makefile:41-42 | DirectDraw used for exclusive mode; `win_mode.c` compiled |
| `HWC_DXDRVR` | off | config.mak:199; makefile:47-52 | `dxdrvr.c` not used |
| `HWC_MINIVDD_HACK` | on | config.mak:209; makefile:94-96 | `\\.\H5VDD` used for multi-chip SLI/AA on 9x |
| `HWC_WINXP_MINIVDD_HACK` | 0 | config.mak:214 | none |
| `WIN40COMPAT` | on | config.mak:203; makefile:103-105 | headers only |
| `FX_GLIDE_NAPALM`, `H4` | on | makefile:89-92 | Napalm paths (multi-chip fields, AA, FIFO at low VRAM) |
| `IS_ALT_TAB` | on | config.mak:104-109 | window-procedure patch, NT branch only (MH:7548-7561) |
| `GLIDE_ALT_TAB` | on | config.mak:105; G3 makefile:37-38 | every API entry tests the lost-context DWORD (G3/fxglide.h:2987-2999) |
| `GLIDE_INIT_HWC` | on unless `HAL_CSIM` | config.mak:116; G3 makefile:195 | minihwc init, not HAL |
| `ENABLE_V3_W2K_GLIDE_CHANGES` | off | config.mak:262 | `PROTECT_CMD_FIFO` (0x1A) and `procID` fields compiled out (HX:318-320, 584-599) |
| `SLI_AA` | off | config.mak:267 | NT `GLIDESTATE` field only |
| `FX_PCI_TARGET=NT` → `NT_BUILD` | on | config.mak:149; G3 makefile:305-306 | not traced to an interface effect |

Which of these 3dfx had on for the shipped Win9x 1.04.00 build: not
determinable from T. `HWC_EXT_INIT` being the default since 1998
("EXT_HWC is default init method", MH:729-730) makes it likely.

## 2. Discovery

**Correction to the plan's summary.** Glide does not ask `QUERYESCMODE`
whether `EXT_HWC` exists. It sends `EXT_HWC` with sub-function
`GETDEVICECONFIG` directly; a non-zero `ExtEscape` return is the "yes".
`QUERYESCMODE` (0x8001) is a separate 3dfx escape used only for its
`QUERYDEVNODE` sub-function.

Win98 path (`EnumDisplayDevicesA` present, MH:1465), per display device:

1. Skip unless `DISPLAY_DEVICE_ATTACHED_TO_DESKTOP` (MH:1491).
2. `hdc = CreateDC(NULL, DeviceName, NULL, NULL)` (MH:1516).
3. `ExtEscape(hdc, 0x3DF3, 64, req, 44, res)`, `which = 2`, `devNo` = count
   of 3dfx boards found so far (0 for the first); `contextID` is left
   uninitialised. If it returns 0, the same call with escape `0xFD3`. If
   that returns 0, the device is dropped (MH:1524-1541). `0x13DF3`
   (`EXT_HWC_WXP`, HX:168) is commented out (MH:1534-1535).
4. Accepted only if reply `vendorID == 0x121A` (MH:1552).
5. Registry path = `DISPLAY_DEVICE.DeviceKey` (MH:1583).
6. `ExtEscape(hdc, 0x8001, 4, QIN{dwSubFunc=3}, 12, QDEVNODE)`; non-zero
   return → `devNode = dwDevNode`, else 0 (MH:1596-1609). `devNode` is used
   only for the H5VDD calls (§5).

Win95 path: the same probe on `GetDC(NULL)` (MH:1171-1300), registry path
from `QUERYDEVNODE` + `CM_Get_DevNode_Key(..., CM_REGISTRY_SOFTWARE)`
(MH:1139-1164).

`QUERYDEVNODE` is also used by `grGetRegistryOrEnvironmentString`
(G3/diget.c:932-943 → MH:7306-7362, 7271-7298), which the 3dfx OpenGL ICD
calls, and by `getModesRegPath` (WM:231-257; caller not found).

| Escape | Value | cbIn / cbOut | Used by Glide |
|---|---|---|---|
| `EXT_HWC` | 0x3DF3 | 64 / 44 | yes, first choice (HX:161) |
| `EXT_HWC_OLD` | 0xFD3 | 64 / 44 | fallback (HX:162) |
| `EXT_HWC_WXP` | 0x13DF3 | — | commented out |
| `EXT_HWC_SHARE_CPUTYPE` | 0x3DF4 | — | defined (HX:176), no caller in T |
| `QUERYESCMODE` | 0x8001 | 4 / 12 | sub-function `QUERYDEVNODE` = 3 only (QM:132, :143) |

`QIN` = one DWORD `dwSubFunc` (QM:188-190). `QDEVNODE` = `dwDevNode`,
`dwValidDefGamma` (DWORD), `bGamma` (BYTE) (QM:241-245); 12 bytes with MSVC
default packing (computed, no `#pragma pack` in QM). Only `dwDevNode` is
read. Other `QUERYESCMODE` sub-functions (QM:138-171) have no caller in T.

**When nothing answers.** `hwcInit` returns NULL with an empty error string
(MH:1366, :2064-2067). `grGlideInit` then calls `GrErrorCallback(...,
fatal)` (G3/diglide.c:357-361). In T that pointer is an uninitialised
global (G3/gerror.c:176) until `_GlideInitEnvironment` installs the default
(G3/gpci.c:2056), which now runs later (`grSstSelect`, G3/disst.c:190):
unless the application installed its own callback, the call goes through
NULL. In the pre-fork order the default callback would show an empty
message box and `exit(1)` (G3/gerror.c:214-221). What 3dfx's binary does:
not established.

## 3. Single-chip Glide 3 session, in order

Defaults assumed: one Voodoo4, fullscreen, no AA (`SSTH3_SLI_AA_CONFIGURATION`
unset), `FX_GLIDE_BUMP` unset, no `grSstVidMode`.

| # | Glide call | Driver sees | Cite |
|---|---|---|---|
| 0 | `LoadLibrary` → `DllMain(ATTACH)` | nothing | G3/gpci.c:2097-2099 |
| 1 | `grGlideInit` → `_grSstDetectResources` → `hwcInit` (once per process, G3/gpci.c:869, :1255) | §2 probe: `EXT_HWC` `GETDEVICECONFIG`, `QUERYESCMODE`/`QUERYDEVNODE` | MH:1481-1629 |
| 2 | ″ | `EXT_HWC` `GETDEVICECONFIG` again; return 0 → `hwcInit` fails | MH:1722-1735 |
| 3 | ″ `checkResolutions` | `ChangeDisplaySettingsEx(CDS_TEST)` for 36 resolutions × 10 refresh rates at 16 bpp | MH:1848; WM:670-724 |
| 4 | ″ `hwcMapBoard` | `GETLINEARADDR`, `pHandle` = caller's process id | MH:2089-2103 |
| 5 | ″ `hwcInitRegisters` | **Glide reads `dramInit1` through `baseAddresses[0]`** (MMIO, in the app process) | MH:2281-2290, :2349 |
| 6 | `grSstSelect(0)` (inside `grGlideInit`) | registry/environment reads only | G3/diglide.c:378; G3/gpci.c:1307+ |
| 7 | `grSstWinOpen` | if a previous close unmapped: `GETLINEARADDR` again | G3/gsst.c:2215-2228 |
| 8 | ″ `hwcAllocBuffers` | nothing (arithmetic) | G3/gsst.c:2270 |
| 9 | ″ `hwcInitVideo` → `setVideoMode` | `DirectDrawCreate`, `SetCooperativeLevel(EXCLUSIVE\|FULLSCREEN)`, then `ChangeDisplaySettingsEx(x, y, bpp, refresh, CDS_FULLSCREEN)`, retried without refresh | WM:373-505 |
| 10 | ″ | `HWCSETEXCLUSIVE`; `resStatus != 1` → open fails | MH:4462-4477 |
| 11 | ″ | `VIDTIMING` only if the app called `grSstVidMode` | MH:4539-4554; G3/disst.c:232-245 |
| 12 | ″ | direct register writes, §6 | MH:4521-4882 |
| 13 | ″ (first open of this GC only) | `SHARE_CONTEXT_DWORD`; Glide then writes 0 through the returned pointer | G3/gsst.c:2329-2338; MH:7566-7596 |
| 14 | ″ gamma | 256 CLUT entries written directly, always (default gamma 1.3) | G3/gsst.c:2341-2350; MH:6857-6892 |
| 15 | ″ `hwcInitFifo` | idle wait on `status`, `cmdFifo0` programmed directly | G3/gsst.c:2409-2415; MH:2785-2858 |
| 16 | rendering, `grBufferSwap` | **no escapes**. Packets written to the FIFO in VRAM via `baseAddresses[1]`; status/read pointer via BAR0. Every API entry first tests `*lostContext` | G3/gsst.c:2748; G3/fxglide.h:2987-2999 |
| 17 | `grSstWinClose` | `grFlush`, `grFinish` | G3/gsst.c:3130-3189 |
| 18 | ″ `hwcRestoreVideo` (skipped if `*lostContext`) | idle wait; `cmdFifo0.baseSize = 0` | MH:5574-5577 |
| 19 | ″ | `PCI_OP` ×6, function 0, write 0 to offsets 0x8C, 0x90, 0x94, 0x80, 0x88, 0x88; return ignored | MH:5662-5734 |
| 20 | ″ | `HWCRLSEXCLUSIVE`; `resStatus != 1` → returns before step 21 | MH:5863-5892 |
| 21 | ″ `resetVideo` | `SetCooperativeLevel(NORMAL)`, DirectDraw released, `ChangeDisplaySettingsEx(registry mode, CDS_RESET)` | WM:512-557 |
| 22 | ″ | `UNMAP_MEMORY`, `procHandle` = id saved at step 13 | G3/gsst.c:3219-3224; MH:7653-7675 |
| 23 | `grGlideShutdown` | closes any open GC (steps 17-22); no escape of its own | G3/gglide.c:3971-4018 |
| 24 | `DllMain(DETACH)` | `grGlideShutdown`, then `UNMAP_MEMORY` once per board with the current process id | G3/gpci.c:2082-2096; MH:7611-7650 |

Variants:

- **Exit without `grSstWinClose`**: step 24 runs 17-22 first, inside
  `DLL_PROCESS_DETACH`. Whether DirectDraw and `ChangeDisplaySettingsEx`
  behave inside process detach on Win98: not established.
- **Killed process**: nothing reaches the driver.
- **Lost context** (`*lostContext != 0` at close): `grSstWinClose` returns
  0 before step 18 (G3/gsst.c:3107-3117), so no `PCI_OP`, no
  `RLSEXCLUSIVE`, no mode set back. Steps 22 and 24 still run.
- **OpenGL** (`EnableOpenGL` called): `hwcRestoreVideo` runs once more at
  the top of close (G3/gsst.c:3098-3104).
- **Mode equals the desktop resolution**: Glide requests **8 bpp** so that a
  real mode change happens and the driver later restores the video
  registers (WM:352-366). The driver must offer 8 bpp at every Glide
  resolution it offers at 16.
- `setVideoMode` returns TRUE even if both mode sets fail (WM:507); Glide
  then scales its overlay to whatever the screen is (MH:4638-4672).
- `HWCSETEXCLUSIVE` failure returns from `hwcInitVideo` after the mode set
  and DirectDraw exclusive are already in place, with no undo
  (MH:4473-4477, G3/gsst.c:2300-2304).

### Request and reply layout

Computed from HX with MSVC default packing; all members are 4 bytes (`HDC`
and pointers included). Not measured.

`hwcExtRequest_t` (HX:651-677), 64 bytes:

| Off | Field | Notes |
|---|---|---|
| 0 | `contextID` | set only by `EXECUTEFIFO` (MH:3886); garbage otherwise |
| 4 | `which` | sub-function |
| 8 | `optData` | union, 56 bytes (largest member `hwcExtSliAAReq_t`, 14 DWORDs) |

`hwcExtResult_t` (HX:679-699), 44 bytes:

| Off | Field | Notes |
|---|---|---|
| 0 | `resStatus` | `FxI32` |
| 4 | `optData` | union, 40 bytes (`hwcExtDeviceConfigRes_t`, `hwcExtLinearAddrRes_t`) |

Glide's success tests differ by call: `ExtEscape` return non-zero
(discovery), `> 0`, `resStatus == 1`, or `-1` as the failure value. Each is
listed below. Return a positive `ExtEscape` value and `resStatus = 1` on
success and both tests pass.

### Sub-functions

Numbers confirmed against HX. "1-chip FS" = called in a single-chip
fullscreen session with the defaults above.

| No. | Name | 1-chip FS | Note |
|---|---|---|---|
| 0x0 | `GETDRIVERVERSION` | no | no caller in T (HX:189-194) |
| 0x1 | `ALLOCCONTEXT` | no | windowed surface API only (G3/gsfc.c:319) |
| 0x2 | `GETDEVICECONFIG` | **yes** | discovery + config |
| 0x3 | `GETLINEARADDR` | **yes** | per detect and per re-open |
| 0x4 | `ALLOCFIFO` | no | "OBSOLTE: Do not use" (HX:272-277); no caller |
| 0x5 | `EXECUTEFIFO` | no | windowed only (G3/fifo.c:747-775) |
| 0x6 | `QUERYCONTEXT` | no | `hwcQueryContext` (MH:7431) has no caller in G3 or G2 |
| 0x7 | `RELEASECONTEXT` | no | windowed only (G3/gsfc.c:432) |
| 0x8 | `HWCSETEXCLUSIVE` | **yes** | `resStatus == 1` required |
| 0x9 | `HWCRLSEXCLUSIVE` | **yes** | `resStatus == 1` required for the desktop to come back |
| 0xA-0xD | I2C | no | no caller |
| 0xE | `GETAGPINFO` | only with `FX_GLIDE_BUMP` | failure falls back to the VRAM FIFO |
| 0xF | `VIDTIMING` | only after `grSstVidMode` | failure ignored |
| 0x10 | `FIFOINFO` | no | windowed only (MH:3363) |
| 0x11 | `LINEAR_MAP_OFFSET` | no | windowed/surface only (MH:3178, :3754) |
| 0x12 | `DOWNLOAD_GAMMA` | no | no caller; Glide writes the CLUT itself |
| 0x13 | `RESTORE_DESKTOP` | no | no caller in T |
| 0x14 | `OVERLAY_DATA` | no | no caller |
| 0x15 | `SHARE_CONTEXT_DWORD` | **yes** | see below |
| 0x16 | `UNMAP_MEMORY` | **yes** | return ignored |
| 0x17 | `CONTEXT_DWORD_NT` | no | NT only (MH:7482-7565) |
| 0x18 | `PCI_OP` | **yes** (close) | return ignored |
| 0x19 | `GET_SLAVE_REGS` | no | only if `numChips > 1` |
| 0x1A | `PROTECT_CMD_FIFO` | no | compiled out |
| 0x1B | `SLI_AA_REQUEST` | no | NT only, multi-chip |
| 0x100-0x111 | `HWCEXT2_*` | no | defined (HX:701-884), no caller |

**0x2 `GETDEVICECONFIG`** (HX:227-250). Success: `ExtEscape` ≠ 0
(MH:1536, :1735), `> 0` in the surface path (MH:3024).

| Req off | Field | | Res off | Field | Glide's use |
|---|---|---|---|---|---|
| 8 | `dc` (HDC) | | 4 | `devNum` | stored if Napalm (MH:1764); no reader found |
| 12 | `devNo` | | 8 | `vendorID` | **must be 0x121A** (MH:1552) |
| | | | 12 | `deviceID` | PCI device id; 6..15 = Napalm (`h5/minihwc/minihwc.h`:517-521; `h5/incsrc/fxhal.h`:57-60), 3 = Banshee, else Voodoo3 (G3/gpci.c:1116-1123) |
| | | | 16 | `fbRam` | **bytes**; `h3Mem = fbRam >> 20` (MH:1743-1744) |
| | | | 20 | `chipRev` | stored, no reader found |
| | | | 24-32 | `pciStride`, `hwStride`, `tileMark` | surface path only (MH:3031-3033, :3736) |
| | | | 36 | `isMaster` | stored, no reader found |
| | | | 40 | `numChips` | **1**; > 4 becomes 0 (MH:1770-1773) |

Consequences of the values: `h3Mem == 8` forces `deviceID` to Banshee
(MH:1790-1792); minimum texture memory 4 MiB unless `h3Mem == 4`
(MH:1795-1797); `grSstWinOpen` refuses when
`bufSize × (colBufs + auxBufs) + 2 MiB + 64 KiB > h3Mem MiB`
(G3/gsst.c:2188-2198). `fbRam` is the ceiling of what Glide overwrites (§6).

**0x3 `GETLINEARADDR`** (HX:258-270). Success: `resStatus == 1`; the
`ExtEscape` return is ignored (MH:2097-2103). Failure at detection is
fatal (G3/gpci.c:1125-1127).

| Req off | Field | | Res off | Field | Glide's use |
|---|---|---|---|---|---|
| 8 | `devNum` (board, 0) | | 4 | `numBaseAddrs` | not read |
| 12 | `pHandle` (process id) | | 8 | `baseAddresses[0]` | **BAR0 linear**: I/O regs +0, CMD/AGP +0x80000, 2D +0x100000, 3D +0x200000, textures +0x600000, LFB +0x1000000 (MH:2281-2290; `h5/incsrc/h3defs.h`:2089-2099) |
| | | | 12 | `baseAddresses[1]` | **BAR1 linear** = raw LFB: FIFO and colour/aux buffers (MH:2338-2339; G3/gsst.c:2748, :2279). Also copied into `pciBaseAddr[2]` ("WTF?????", MH:2111-2113); its I/O-port use is DOS-only |
| | | | 16-40 | `[2..8]` | read only for slave chips |

Address space: Glide dereferences both addresses directly from ring 3 in
the calling process, from `grGlideInit` onwards (step 5). On Win9x that
means an address valid in every process (the shared arena) or one mapped
for this process; which the 3dfx driver used is not in T. Extent: the DOS
path maps 32 MiB per BAR on Napalm (MH:2203-2206); Glide's own pointers
reach BAR0 + 0x1000000 and BAR1 + `fbRam`. The full BAR0 extent Glide
touches is not established.

**0x8 `HWCSETEXCLUSIVE` / 0x9 `HWCRLSEXCLUSIVE`** (HX:353-354). Request
`+8 devNum` (written through the `linearAddrReq` member, MH:4465, :5873).
Reply: `resStatus == 1` only. Glide assigns no other meaning; what the
3dfx driver did on them is not in T.

**0x15 `SHARE_CONTEXT_DWORD`** (HX:521-528), 9x branch (MH:7566-7596).
Request `+8` = process id (written through `unmapMemoryReq.procHandle`).
Reply `+4 contextDWORD` = a pointer Glide stores as `gc->lostContext`.
Glide **writes** 0 through it at once (G3/gsst.c:2338) and reads it at every
API entry, so it must be a ring-3 readable and writable address in the app's
process for the life of the session. The driver sets it non-zero to tell
Glide it has lost the hardware (HX:514-519). Failure: `ExtEscape` returns
-1 or `resStatus == -1` → Glide uses a private dummy (always "not lost").
**A return of 0 is not treated as failure**: Glide then uses whatever is in
its uninitialised reply buffer as the pointer.

**0x16 `UNMAP_MEMORY`** (HX:536-539). Request `+8 procHandle`. Reply
ignored. Sent at every close and again at DLL detach (§3 steps 22, 24).
Glide sets `isMapped = FALSE` and calls `GETLINEARADDR` again at the next
open, so the driver may drop the mapping here.

**0x18 `PCI_OP`** (HX:559-572). Request `+8 DeviceId` (PCI function 0-3),
`+12 Operation` (0 read, 1 write), `+16 Offset`, `+20 Value`. Reply `+4
Value` for reads. Return value never checked. Single-chip calls:

- Every `hwcRestoreVideo` on Napalm: six writes of 0 to function 0 at
  `cfgSliLfbCtrl` 0x8C, `cfgAADepthBufferAperture` 0x90, `cfgAALfbCtrl`
  0x94, `cfgVideoCtrl0` 0x80, `cfgVideoCtrl2` 0x88 twice (MH:5662-5734;
  offsets `h5/incsrc/h3regs.h`:500-505). `cfgVideoCtrl1` is not cleared; the repeated
  0x88 looks like a slip for 0x84.
- With 2-sample AA on one chip: writes to 0x8C, 0x94, 0x90 and, for
  2-sample, 0x80, 0x84, 0x88 at open (MH:5027-5157), then reads of 0x94
  and 0x90 (MH:5162-5268).
- `hwcSLIReadEnable/Disable` only with `FX_GLIDE_A0_READ_ABORT` and SLI
  (MH:5985-6031).

A driver that implements `PCI_OP` must bound `Offset` and function; these
are config-space writes on behalf of any process that can get a DC.

**0xE `GETAGPINFO`** (HX:413-419). Reply `+4 lAddr`, `+8 pAddr`, `+12
size`. `resStatus != 1` or `size == 0` → VRAM FIFO (MH:6254-6270). Not
reached unless `FX_GLIDE_BUMP` is set (G3/gpci.c:2017; G3/gsst.c:2409-2433).

**0xF `VIDTIMING`** (HX:427-430). Request `+8` = a flat 32-bit pointer to
the app's `FxVideoTimingInfo`. Failure ignored (MH:4553). A 16-bit
`Control` handler cannot use the pointer without translating it.

Windowed-surface calls (0x1, 0x5, 0x7, 0x10, 0x11) are reached only through
`grSurfaceCreateContext(GR_SURFACECONTEXT_WINDOWED)` (G3/gsfc.c:296-370).
Layouts are in HX:202-219, 294-351, 437-466; not specified further here.

## 4. Version checks

None found. `hwcProtocol` is set to -1 unconditionally (MH:1524) and
`HWCEXT_PROTOCOL()` only tests for -1 (MH:850). `GETDRIVERVERSION`,
`QUERYVERSION` (QM:138, `QUERYMODE_MAJOR/MINOR` QM:203-204) and
`HWCEXT2_GETPROTOCOLREVISION` have no caller. `HWCEXT_PROTOCOLREV` = 1
(HX:155) is sent only in `ALLOCCONTEXT`. The only gates are `vendorID ==
0x121A`, the device-id class, and the `resStatus == 1` checks.

## 5. Multi-chip and `\\.\H5VDD`

Glide opens `\\.\H5VDD` (MH:859) only when all hold (MH:4910-4915,
4966-5016; restore MH:5588-5656): Napalm, `numChips > 1`, and
(`h3nwaySli > 1` or `h3pixelSample > 2` or 2-sample AA without a secondary
buffer), and the OS is not NT. Then `CreateFile(name, 0, 0, NULL, 0, 0,
NULL)` and `DeviceIoControl(code, &DIOC_DATA, 8, NULL, 0, NULL, NULL)` with
code 6 (`SLI_AA_ENABLE`) at open or 7 (`SLI_AA_DISABLE`) at close
(MH:866-867). `DIOC_DATA` = `dwDevNode` (from `QUERYDEVNODE`), `dwSpare` =
flat pointer to a 56-byte `SLI_AA_REQUEST` in the app (MH:869-897). Return
ignored; Glide rewrites `lfbMemoryConfig` afterwards (MH:5018-5020). In the
H5VDD branch the close skips the `PCI_OP` clean-up (MH:5659-5662).
Multi-chip also needs `GET_SLAVE_REGS` per slave (MH:2123-2151).

**Single chip: H5VDD is never opened**, with or without AA. Phase 4 on the
Voodoo4 does not need it. Report `numChips = 1`; 0 also avoids it but is
untested territory in Glide's chip loops.

## 6. Command FIFO, VRAM and register ownership

**Glide owns the FIFO.** It is not allocated by the driver (`ALLOCFIFO` is
obsolete). Glide places it in VRAM and programs `cmdFifo0` itself:

| Region (VRAM offset) | Owner | Cite |
|---|---|---|
| 0 .. 0x18000 | untouched ("Get out of the way of VGA"; `FX_GLIDE_FIFOEXTRA` overrides) | MH:2597-2600 |
| 0x18000 .. 0x117000 | command FIFO, 0xFF000 bytes | MH:2602-2622; MH:911 |
| 0x117000 .. `fbOffset` | textures | MH:2602-2623 |
| `fbOffset` .. `fbRam` | colour and aux buffers, allocated downwards from the top | MH:2501-2537 |

So everything from 0x18000 to `fbRam` is overwritten: desktop, off-screen
heap, DirectDraw surfaces, cursor image. A driver can keep a region only
by reporting a smaller `fbRam`.

Registers Glide writes directly from ring 3 during open (none are restored
by Glide): `cmdFifo0.*`, `cmdFifoThresh` (MH:2811-2849); `vidProcCfg`
(hardware cursor **disabled**, MH:2917; overlay, filter, desktop enable),
`vidDesktopOverlayStride`, `vidOverlayStartCoords`, `vidOverlayDudx/Dvdy`,
`vidOverlayEndScreenCoord`, `vidOverlayDudxOffsetSrcWidth`,
`vidPixelBufThold`, `vidScreenSize` (clears the desktop-address FIFO
enable), `miscInit0` (Y origin), `lfbMemoryConfig`, `dramInit1` (triple
buffer bit), `vidMaxRGBDelta` (MH:4521-4882); CLUT entries 0-255 of the
bank `vidProcCfg` selects (MH:6857-6892). Environment-only: `pllCtrl1/2`
(`H3_MEM_CLOCK`), `pciInit0` (`SST_PCI_LOWTHRESH`). If the idle wait times
out (10^9 status reads) Glide resets the 3D, 2D and command-stream engines
through `miscInit0/1` (MH:2752-2780).

**How the two sides avoid each other in T:** DirectDraw exclusive
cooperative level (WM:402-405), `HWCSETEXCLUSIVE`/`HWCRLSEXCLUSIVE`, and
the lost-context DWORD. `PROTECT_CMD_FIFO` is compiled out. Nothing else.
The restore model is a full mode set by the display driver: `resetVideo`
calls `ChangeDisplaySettingsEx(registry mode, CDS_RESET)` after
`RLSEXCLUSIVE`, and the 8 bpp trick (WM:352-366) exists to guarantee the
mode set happens.

What the driver must therefore do (derived, not stated in T):

- On `HWCSETEXCLUSIVE`: stop all hardware access that touches VRAM above
  0x18000, the 2D engine or `cmdFifo0` until release; return 1.
- On `HWCRLSEXCLUSIVE`: return 1 (otherwise no mode set back follows,
  MH:5887-5890); treat all VRAM contents and the registers above as lost.
- On the mode set that follows: reprogram every register in the list,
  re-enable the cursor, reload the CLUT/gamma, reinitialise its own FIFO
  and 2D state, invalidate off-screen caches and DirectDraw surfaces.
- When it takes the hardware back without a close (task switch, DOS box,
  another mode set while exclusive): write non-zero to the shared DWORD,
  then restore the desktop itself, because Glide will not (§3 variants).

`HWCEXT_RESTORE_DESKTOP`'s description (HX:487-499) says 3dfx's driver ran
its return-from-DOS restore for it: video, command FIFO, cursor, overlay
flags. Glide never sends it.

## 7. Registry and environment

Lookup (`hwcGetenvEx`, MH:7373-7427): process environment first, then
`HKCU\<RegPath>\glide`, then `HKLM\<RegPath>\glide`; `REG_SZ` values only.
`<RegPath>` on Win98 is `DISPLAY_DEVICE.DeviceKey` (MH:1583); on Win95 the
devnode software key (MH:1139-1164). The Win98 `DeviceKey` string format
for a Velocity9x device is not established.

Nothing is required: every key has a default. Keys that change what the
driver sees:

| Key | Default | Effect | Cite |
|---|---|---|---|
| `SSTH3_SLI_AA_CONFIGURATION` | 2 (no AA) | 1/3/6 → 2-sample AA (`PCI_OP` at open), 4/7 → 4, 8 → 8, 0 → force one chip | G3/gpci.c:1755-1780 |
| `FX_GLIDE_AA_SAMPLE` | unset | overrides AA | G3/gpci.c:1791-1792 |
| `FX_GLIDE_BPP` | 0 | 32 → 32 bpp mode set | G3/gpci.c:1786; G3/gsst.c:1670 |
| `FX_GLIDE_NUM_CHIPS` | unset | may only lower `numChips` | MH:1833-1842 |
| `FX_GLIDE_FBRAM` | unset | overrides `h3Mem` (MiB) | MH:1844-1846 |
| `FX_GLIDE_DEVICEID` | unset | overrides `deviceID` | MH:1753-1756 |
| `FX_GLIDE_TMU_MEMSIZE` | unset | 1 → 2 MiB minimum texture | MH:1826-1831 |
| `FX_GLIDE_SSTSELECT` | unset | 1 → force this board to SST 0 (fork) | MH:1665-1673 |
| `FX_GLIDE_FIFOEXTRA` | 96 KiB | FIFO start pad | MH:2599-2600 |
| `FX_GLIDE_V3FIFO` (`getenv` only) | unset | Voodoo3 FIFO placement below the buffers | MH:2593-2595, :2626-2647 |
| `FX_GLIDE_BUMP` | unset | set → `GETAGPINFO` path | G3/gpci.c:2017 |
| `FX_GLIDE_REFRESH` | from app | overrides refresh Hz | MH:4424-4425 |
| `SSTH3_RGAMMA/GGAMMA/BGAMMA` | 1.3 | CLUT ramp | G3/gpci.c:1891-1893 |
| `FX_GLIDE_BRIGHTNESS`, `FX_GLIDE_CONTRAST` | 0, 1 | CLUT ramp | MH:6820-6831 |
| `SSTH3_OVERLAYMODE` | optimal | overlay filter | MH:4679-4716 |
| `SSTVB_PIXTHOLD` | 32 | `vidPixelBufThold` | MH:4527-4536 |
| `SSTH3_SDRAM` | unset | forces SDRAM flag | MH:2352-2353 |
| `H3_MEM_CLOCK` | unset | writes `pllCtrl1/2` | MH:4832-4867 |
| `SST_PCI_LOWTHRESH` | unset | writes `pciInit0` | MH:5554-5562 |
| `FX_GLIDE_NO_HW` | unset | FIFO left disabled | MH:2842-2844 |

`SSTH3_SGRAM_*`, `SSTH3_GRXCLOCK`, `SSTH3_MEMCLOCK` are read but used only
on the DOS path (MH:2373-2437). Which of these the 3dfx INF set is not in T.

## 8. Testable predictions

For a logging `EXT_HWC` stub in the 16-bit `Control` on A8U4I5 (Glide 3,
then Glide 2), plus the stock-driver baseline where noted.

1. The first 3dfx escape a Glide 3 title sends is 0x3DF3 with cbIn 64,
   cbOut 44, `which` 2, `devNo` 0. No `QUERYESCMODE` precedes it.
2. If 0x3DF3 returns 0, exactly one 0xFD3 with the same shape follows; if
   that returns 0, no further 3dfx escape arrives.
3. After a yes: 0x8001 with cbIn 4, `dwSubFunc` 3, cbOut 12; then a second
   `GETDEVICECONFIG`; then the CDS_TEST burst (if those reach the driver);
   then `GETLINEARADDR` with `pHandle` equal to the title's process id.
4. A bad `baseAddresses[0]` faults inside `grGlideInit`, before any
   mode set.
5. In `grSstWinOpen` the mode set arrives before `HWCSETEXCLUSIVE`, at 16
   bpp, or 8 bpp when the title's resolution equals the desktop's.
6. `SHARE_CONTEXT_DWORD` (0x15) arrives after `HWCSETEXCLUSIVE`, once per
   process, `+8` = process id.
7. No 3dfx escape arrives between the end of `grSstWinOpen` and
   `grSstWinClose`.
8. Close order: six `PCI_OP` writes of 0 (function 0; 0x8C, 0x90, 0x94,
   0x80, 0x88, 0x88), `HWCRLSEXCLUSIVE`, a mode set back to the registry
   mode, `UNMAP_MEMORY`. Unloading the DLL adds one more `UNMAP_MEMORY`.
9. Sub-functions 0x0, 0x1, 0x4, 0x5, 0x6, 0x7, 0xA-0xD, 0x10-0x14, 0x17,
   0x19, 0x1A, 0x1B never arrive from a single-chip fullscreen title.
10. No process opens `\\.\H5VDD` on a Voodoo4.
11. With `resStatus` ≠ 1 for `HWCRLSEXCLUSIVE`, the desktop stays in the
    Glide mode after the title exits.
12. With `SHARE_CONTEXT_DWORD` unhandled (return 0) the title faults in
    `grSstWinOpen`; with return -1 it runs.
13. VRAM below 0x18000 has the same checksum before and after a session;
    above it does not. The hardware cursor is off and the CLUT changed after
    a session until the driver restores them.
14. The stock 3dfx 1.04.00 `glide3x.dll` on the stock driver sends the same
    escape sequence (capture via a filter or the probe VxD), except that its
    mode set comes through DirectDraw rather than `ChangeDisplaySettingsEx`.
    This is the test of whether T speaks for the shipped binary.

## 9. Not established

- Whether 3dfx's shipped `glide2x.dll`/`glide3x.dll` (1.04.00 and the
  versions games bundle) follow T: init order, mode-set API, error path,
  any escape T lacks. Prediction 14 tests it.
- Which build defines 3dfx used for the shipped Win9x Glide.
- How Win98 GDI hands a 64-byte `ExtEscape` input and 44-byte output to a
  16-bit `Control`, and what `ExtEscape` returns to the caller for a given
  `Control` return value. Win9x DDK, not T.
- Whether Win98 `ChangeDisplaySettingsEx(CDS_TEST)` reaches the display
  driver (and so whether the 360-call burst is visible to it), and whether
  `CDS_RESET` forces a driver mode set when the mode is unchanged.
- Whether USER sets `DISPLAY_DEVICE_ATTACHED_TO_DESKTOP` for a Velocity9x
  primary on Win98, and the format of its `DeviceKey`.
- Which linear address space 3dfx's driver returned in `GETLINEARADDR`, and
  how large a BAR0 mapping Glide needs.
- What memory 3dfx's driver returned for `SHARE_CONTEXT_DWORD`, and when it
  set it.
- What 3dfx's driver did on `HWCSETEXCLUSIVE`/`HWCRLSEXCLUSIVE` beyond
  returning 1, and how GDI drawing was kept off the hardware while
  exclusive.
- Whether DirectDraw and `ChangeDisplaySettingsEx` work inside
  `DLL_PROCESS_DETACH` on Win98 (exit without close).
- Whether a Velocity9x 2D engine may use `cmdFifo1` while Glide owns
  `cmdFifo0`.
- What a killed Glide title leaves behind on a Voodoo4 (the plan's
  Voodoo3 note says the chip wedges until power-off).
- Whether any title calls `grSstVidMode` (and so `VIDTIMING`), uses the
  windowed surface API, or sets `FX_GLIDE_BUMP`.
