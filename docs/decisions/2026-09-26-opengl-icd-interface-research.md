# The Win9x OpenGL ICD contract, from four sources that agree, and what GLQuake and Quake 2 actually ask of a driver

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`

This is a **desk record**, not a measurement. Nothing here was reproduced on
a guest or a card. It collects the interface facts the OpenGL plan depends on
so that the guest measurements in that plan's Phase 0 know what they are
confirming or killing, and so the next person does not re-read the same
sources. Every item is marked **[C]** (stated by the source cited) or **[I]**
(inferred; nobody has shown it). Sources were read as interface
documentation; no code was copied.

Sources:

- ReactOS `dll/opengl/opengl32/{icdload.c,icd.h,opengl32.h,wgl.c}`,
  <https://github.com/reactos/reactos/tree/master/dll/opengl/opengl32>
- Mesa `src/gallium/frontends/wgl/gldrv.h` (Microsoft-copyright header),
  <https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/gallium/frontends/wgl/gldrv.h>
- vmdisp9x (`control.c`, `vmdisp9x.inf`, README) and mesa9x, JHRobotics,
  <https://github.com/JHRobotics/vmdisp9x>, <https://github.com/JHRobotics/mesa9x>
- The old Mesa Win32 ICD and GLDirect at tag `mesa_6_4`,
  `src/mesa/drivers/windows/{icd,gldirect,fx}`
- Wine `dlls/kernel32/kernel32.spec`, `dlls/krnl386.exe16/syslevel.c`,
  `dlls/gdi32/opengl.c`, `include/ddrawi.h`
- KernelEx `common/k32ord.def`, `k32ord.h`; SciTech SNAP `k32exp.h`,
  `ddstereo.c`
- Glide `glide3x/h3/minihwc/{minihwc.c,hwcext.h,dxdrvr.c}`,
  <https://github.com/sezero/glide>
- id Software `Quake` (WinQuake) and `Quake-2` on GitHub, master
- Microsoft KB125867 "Understanding Win16Mutex", KB Q147256, KB154877; the
  `IDirectDrawSurface7::Lock`, `Restore`, `DD_BLTDATA` and
  `D3DHAL_CONTEXTCREATEDATA` pages on learn.microsoft.com
- Vendor INFs: NVIDIA `NVAML.INF` (driverguide 570323), 3dfx
  `driver9x/Voodoo3.inf` (archive.org `voodoo-3-3000-driver`), VIA
  `k8viag.inf` (driverguide 817570)

## 1. How opengl32.dll finds the ICD on Windows 9x

- **[C]** The key is `HKLM\Software\Microsoft\Windows\CurrentVersion\OpenGLdrivers`,
  case-insensitive in practice (NVIDIA writes `OpenGLdrivers`, VIA
  `OpenGLDrivers`). On 9x it holds **one REG_SZ value per driver**: the value
  name is the driver name, the data the ICD DLL file name. There is no
  subkey. The `Dll`/`Version`/`DriverVersion`/`Flags` subkey layout is
  NT-only (`...\Windows NT\CurrentVersion\OpenGLDrivers\<name>`; ReactOS
  `icdload.c:36,186-255`, falling back to a plain value at `:189-200`).
- **[C]** Real 9x INF lines:
  - NVIDIA TNT: `HKLM,Software\Microsoft\Windows\CurrentVersion\OpenGLdrivers,RIVATNT,2,"nvopengl.dll"`
  - 3dfx Voodoo3 (`Voodoo3.inf:115`): `HKLM,"Software\Microsoft\Windows\CurrentVersion\OpenGLdrivers","3dfx",,"3dfxogl.dll"`
    (the ICD is `3dfxogl.dll`; `3dfxvgl.dll` is the standalone opengl32
    replacement for Voodoo1/2/Rush)
  - VIA: `...OpenGLDrivers",viagfx,,vticd.dll`
  - vmdisp9x (`vmdisp9x.inf:292-294`): `SOFTWARE,2,"mesa3d.dll"` and
    `VMWSVGA,2,"vmwsgl32.dll"`
- **[C]** opengl32 asks the **display driver** which name to look up. The
  16-bit driver's `Control()` receives `QUERYESCSUPPORT` (8) for
  `OPENGL_GETINFO` (0x1101 = 4353) and must return 1, then receives
  `OPENGL_GETINFO` itself (vmdisp9x `control.c:45-46,194-245`). The comment
  at `control.c:153-165` says this was reverse-engineered from shipping
  drivers and the system opengl32.dll. vmdisp9x also answers
  `QUERYESCSUPPORT` for `OPENGL_CMD` 0x1100 and `WNDOBJ_SETUP` 0x1102.
- **[C]** The 9x output layout (vmdisp9x `control.c:64-119`):
  `{ long Version; long DriverVersion; char DLL[262]; }`. The name is
  **ANSI**, not WCHAR: a dump from a real NVIDIA 9x driver has
  `R,I,V,A,T,N,T` at consecutive bytes 0x08-0x0E. opengl32's output buffer
  is 532 bytes (0x214). `Version` is 2; NVIDIA's dump has byte 0x06 = 0x01,
  so `DriverVersion` = 0x00010000; vmdisp9x returns Version=2,
  DriverVersion=1. The input pointer is NULL; do not dereference it.
  `Control` returns 1 on success.
- **[C]** The NT layout differs (Mesa `gldrv.h:562-587`): input
  `OPENGLGETINFO { ULONG ulSubEsc; }` with `OPENGL_GETINFO_DRVNAME = 0`,
  output `GLDRVNAMERET { ULONG ulVersion; ULONG ulDriverVersion; WCHAR awch[MAX_PATH+1]; }`.
  Its comment says Version "must be 1"; ReactOS and vmdispxp say 2 works in
  practice.
- **[C]** Escape order in ReactOS (`icdload.c:141-157`): `QUERYESCSUPPORT`
  first; ≤0 means no ICD and the software path is used; then
  `OPENGL_GETINFO`.
- **[I]** If the escape is unsupported on 9x, Microsoft's generic renderer
  serves the application. Consistent with the 1990s pseudonymz note that
  "the ICD mechanism cannot support 3d only hardware".
- **[C]** Win95/98 disable ICD acceleration when two monitors are active
  (OpenGL FAQ, pseudonymz).
- **[C]** Original Windows 95 needs the OpenGL 1.1 runtime installed
  separately; OSR2 includes it (KB154877).

**To measure (plan Phase 0.1):** the layout above, on original 98, 98SE and
ME, by tracing every `Control` escape.

## 2. ICD exports

All `__stdcall`, undecorated names in the `.def`. Prototypes **[C]** from
Mesa `gldrv.h:473-506` and ReactOS `icd.h:373-388`:

```
BOOL   DrvCopyContext(DHGLRC src, DHGLRC dst, UINT mask)
DHGLRC DrvCreateContext(HDC)
DHGLRC DrvCreateLayerContext(HDC, int iLayerPlane)
BOOL   DrvDeleteContext(DHGLRC)
PGLCLTPROCTABLE DrvSetContext(HDC, DHGLRC, PFN_SETPROCTABLE)
                      /* PFN_SETPROCTABLE = VOID (APIENTRY*)(PGLCLTPROCTABLE) */
BOOL   DrvReleaseContext(DHGLRC)       /* ReactOS types it void; return TRUE */
BOOL   DrvShareLists(DHGLRC, DHGLRC)
LONG   DrvDescribePixelFormat(HDC, INT iPixelFormat, ULONG cjpfd, PIXELFORMATDESCRIPTOR*)
BOOL   DrvSetPixelFormat(HDC, LONG iPixelFormat)
BOOL   DrvSwapBuffers(HDC)
BOOL   DrvSwapLayerBuffers(HDC, UINT fuPlanes)
BOOL   DrvDescribeLayerPlane(HDC, INT, INT, UINT, LPLAYERPLANEDESCRIPTOR)
INT    DrvSetLayerPaletteEntries(HDC, INT, INT, INT, CONST COLORREF*)
INT    DrvGetLayerPaletteEntries(HDC, INT, INT, INT, COLORREF*)
BOOL   DrvRealizeLayerPalette(HDC, INT, BOOL)
PROC   DrvGetProcAddress(LPCSTR)
BOOL   DrvValidateVersion(ULONG)
VOID   DrvSetCallbackProcs(INT nProcs, PROC* pProcs)
```

- **[C]** `DHGLRC` is a 32-bit ULONG (`gldrv.h:384`).
- **[C]** ReactOS refuses the ICD unless all **16** of these are exported
  (`icdload.c:303-326`): Copy, Create, CreateLayer, Delete,
  DescribeLayerPlane, DescribePixelFormat, GetLayerPaletteEntries,
  GetProcAddress, ReleaseContext, RealizeLayerPalette, SetContext,
  SetLayerPaletteEntries, SetPixelFormat, ShareLists, SwapBuffers,
  SwapLayerBuffers. `DrvValidateVersion` and `DrvSetCallbackProcs` are
  optional (`:281-300`).
- **[C]** The old Mesa Win32 ICD (`icd/mesa.def`) exports exactly those 16
  plus `DrvValidateVersion` and no `DrvSetCallbackProcs`; its layer stubs
  return FALSE/0 (`icd/icd.c:130-347`). That set shipped against 9x-era
  opengl32.
- **[C]** `DrvValidateVersion` is called before the other lookups with the
  `DriverVersion` from the escape (ReactOS `icdload.c:284`); Mesa returns
  TRUE (`stw_device.c:293-305`).
- **[C]** `DrvSetCallbackProcs` receives `{SetCurrentValue, GetCurrentValue,
  GetDhglrc, ...}` (`gldrv.h:390-469`; ReactOS passes 3). **[I]** Probably
  NT5+ only; exporting it is harmless.
- **[C]** `DrvDescribePixelFormat` is called with a NULL descriptor and
  index 0 to get the count, and returns the total number of formats in
  every case (ReactOS `wgl.c:79,94`; old Mesa `icd.c:283-299`).

## 3. The dispatch table

- **[C]** `GLCLTPROCTABLE` is `{ int cEntries; GLDISPATCHTABLE glDispatchTable; }`,
  4 + 336×4 = 1348 bytes on x86. `OPENGL_VERSION_100_ENTRIES = 306`,
  `OPENGL_VERSION_110_ENTRIES = 336` (Mesa `gldrv.h:30-31,377-380`;
  ReactOS `icd.h:10,354-358`). Set `cEntries` to 336; the old Mesa ICD uses
  `{ 336, { ... } }` (`icd.c:56-71`).
- **[C]** The slot order is given identically by Mesa `gldrv.h:33-373`,
  ReactOS `icd.h:12-350`, and old Mesa `icd/icdlist.h` (numbered 0-335), and
  matches Mesa's glapi static offsets (`gl_API.xml`: NewList=0, Begin=7,
  Viewport=305, ArrayElement=306, BindTexture=307, TexSubImage2D=333,
  PushClientAttrib=335). Slots 0-11: NewList, EndList, CallList, CallLists,
  DeleteLists, GenLists, ListBase, Begin, Bitmap, Color3b, Color3bv,
  Color3d. Slot 305 Viewport is the last 1.0 entry; 306-335: ArrayElement,
  BindTexture, ColorPointer, DisableClientState, DrawArrays, DrawElements,
  EdgeFlagPointer, EnableClientState, IndexPointer, Indexub, Indexubv,
  InterleavedArrays, NormalPointer, PolygonOffset, TexCoordPointer,
  VertexPointer, AreTexturesResident, CopyTexImage1D, CopyTexImage2D,
  CopyTexSubImage1D, CopyTexSubImage2D, DeleteTextures, GenTextures,
  GetPointerv, IsTexture, PrioritizeTextures, TexSubImage1D, TexSubImage2D,
  PopClientAttrib, PushClientAttrib. The full 336-entry list with
  prototypes is the manifest `src/opengl/gl_entrypoints.psd1` (plan Phase
  3), cross-checked between the two headers.
- **[I]** `GLEXTPROCTABLE` was internal to Microsoft's generic
  implementation; no public source defines it for ICDs. Extensions,
  including `EXT_vertex_array`, are reached only through
  `DrvGetProcAddress`, to which opengl32's `wglGetProcAddress` forwards
  (ReactOS `wgl.c:631-645`).
- **[I]** Every table pointer must be `__stdcall`, since opengl32's exported
  `gl*` stubs call through it.

## 4. GDI routing, pixel-format merging, threading

- **[C]** gdi32's `DescribePixelFormat`, `SetPixelFormat`, `GetPixelFormat`
  and `SwapBuffers` `LoadLibrary("opengl32.dll")` and call the matching
  `wgl*` export (Wine `dlls/gdi32/opengl.c:59-110`; pseudonymz says the
  same of the 9x-era link). opengl32 routes `wglSetPixelFormat` to
  `DrvSetPixelFormat` and `wglSwapBuffers` to `DrvSwapBuffers` (ReactOS
  `wgl.c:814-870,899-916`), and records the format per DC/window.
  `wglMakeCurrent` rejects a context/DC pair whose ICD or format differ
  (`wgl.c:669-677`).
- **[C]** ReactOS numbers **ICD formats 1..n first**, then the generic ones
  (`wgl.c:79-83,134,149-163,842-855`). **[I]** Microsoft's opengl32 is
  believed to do the same. **[I]** NT's registry `Flags` bit 0 controls
  whether opengl32 asks the ICD for formats; 9x has no Flags value and
  mesa9x works, so it presumably always asks.
- **[C]** An ICD format sets **neither** `PFD_GENERIC_FORMAT` (0x40) nor
  `PFD_GENERIC_ACCELERATED` (0x1000); an MCD (NT-only) sets both; software
  sets `GENERIC_FORMAT` only. Typical flags:
  `PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_SWAP_COPY`
  (old Mesa `icd.c:79-113`). Only formats matching the desktop depth show
  as accelerated.
- **[C]** NT and ReactOS opengl32 keep the current dispatch pointer,
  context, HDC and the ICD's private value in TEB fields (ReactOS
  `opengl32.h:77-135`; Wine `wgl.c:1152-1171`). **[I]** The 9x TIB has no
  such fields, so 9x opengl32 almost certainly uses its own TLS slot; either
  way the storage is opaque to the ICD.
- **[C]** MakeCurrent sequence (ReactOS `wgl.c:647-743`):
  `DrvReleaseContext(old)`, then `DrvSetContext(hdc, dhglrc, set_api_table)`;
  NULL means failure; otherwise opengl32 installs `&ret->glDispatchTable`
  for the thread; the callback may be called later by the ICD to replace
  the thread's table; `wglMakeCurrent(NULL, NULL)` calls only
  `DrvReleaseContext`. The ICD binds the context to the thread in its own
  TLS in `DrvSetContext` and returns a static table (Mesa
  `stw_context.c:995-1004`; old Mesa `icd.c:199-222`; TlsAlloc at
  `stw_tls.c:63`). Mesa also installs a per-thread `WH_CALLWNDPROC` hook to
  track window moves and resizes (`stw_tls.c:131`).

## 5. How 9x ICDs presented and found the clip list

- **[C]** 3dfx Voodoo3: `3dfxogl.dll` runs on Glide3, which talks to the 3dfx
  display driver through a private `ExtEscape(hdc, 0xFD3, ...)` with
  sub-functions `ALLOCCONTEXT`, `GETDEVICECONFIG`, `GETLINEARADDR`,
  `ALLOCFIFO`, `EXECUTEFIFO`, `LINEAR_MAP_OFFSET` (`hwcext.h:107-467`,
  `minihwc.c:650-1865`). Its DirectDraw path (`dxdrvr.c:358-406`) sets
  `DDSCL_NORMAL`, creates a primary with `PRIMARYSURFACE|3DDEVICE`, a
  clipper with `SetHWnd`, back/aux buffers as
  `OFFSCREENPLAIN|VIDEOMEMORY|3DDEVICE`, and uses `EXCLUSIVE|FULLSCREEN`
  with a flip chain for fullscreen (`:416-450`). Each context gets its own
  command FIFO created as a DirectDraw surface, locked **without**
  `NOSYSLOCK` and unlocked at once; a comment says `DDLOCK_NOSYSLOCK` does
  not work there. A "lostContext" dword shared through
  `HWCEXT_SHARE_CONTEXT_DWORD` tells Glide it lost the hardware.
- **[C]** GLDirect (SciTech) is **not** an ICD: it replaces opengl32.dll
  outright (`gldirect/opengl32.def:427-454`, `gldirect.rc:40`). Mesa core
  with D3D7/8/9 and software back ends. Windowed: `DDSCL_NORMAL`, primary,
  clipper via `DirectDrawCreateClipper` + `SetHWnd`, back buffer offscreen
  in VRAM, SwapBuffers = `Blt(front, clientRectOnScreen, back, src, DDBLT_WAIT)`
  (`dglcontext.c:1048-1107,2044-2194`); fullscreen uses `Flip`
  (`:2141-2155`); the clipper hwnd is re-pointed on context/window change
  (`:1801-1807`); the DX7 back end re-calls `SetHWnd` on every swap for MDI
  (`gld_wgl_dx7.c:692-713,1543-1553`). Current context in TLS
  (`dglcontext.c:278-279`).
- **[C]** mesa9x on vmdisp9x presents with GDI `StretchDIBits` onto the
  window DC (`win9x/vramcpy.c:62-67`, `svgadrv_present.c:115-267`), so GDI
  clips and no clip list is needed; it reaches its driver through its own
  FBHDA calls (`win9x/3d_accel.c`), with a global named mutex guarding
  object tables and VxD semaphores serializing command submission
  (`win9x/3d_svga.c`, vmdisp9x `vxd_svga.c`).
- **[C]** DCI offers `WinWatchOpen`/`WinWatchGetClipList` in DCIMAN32 inside
  `DCIBeginAccess`/`DCIEndAccess`, thunking to the 16-bit driver on 9x.
  **[C]** `WNDOBJ`/`OPENGL_CMD` are NT GDI mechanisms (`gldrv.h:526-560`).
- **[I]** NVIDIA's and ATI's 9x ICDs used private escapes or VxD calls; no
  public source confirms it.

The plan's choice (DirectDraw clipper + Blt, GDI-free) follows GLDirect and
Glide, the two 9x precedents that used DirectDraw.

## 6. The Win16 mutex and DirectDraw internals

- **[C]** KERNEL32 exports `GetpWin16Lock` (#93), `_CheckNotSysLevel` (#94),
  `_ConfirmSysLevel` (#95), `_ConfirmWin16Lock` (#96), `_EnterSysLevel`
  (#97), `_LeaveSysLevel` (#98) **by ordinal only** (Wine
  `kernel32.spec`, all `-noname`). KernelEx imports #93/#97/#98 statically
  through a NONAME `.def` on 98/ME (`common/k32ord.def`, `k32ord.h`):
  `void __stdcall _GetpWin16Lock(CRITICAL_SECTION**)`,
  `_EnterSysLevel(CRITICAL_SECTION*)`, `_LeaveSysLevel(CRITICAL_SECTION*)`.
  SciTech's `k32exp.h` defines the same ordinals, credits Schulman's
  *Unauthorized Windows 95*, and finds them by walking KERNEL32's export
  table by hand rather than `GetProcAddress`. **[I]** The hand walk exists
  because Win95's `GetProcAddress` refuses ordinals into KERNEL32.
- **[C]** In Wine's model the Win16Mutex is a SYSLEVEL at level 1, re-entrant
  on the same thread (`syslevel.c`).
- **[C, local]** The DX3 `ddraw.dll` (4.03.00.1096) in
  `build/hellbender-cd/HELLCD/Setup/directx/` imports KERNEL32 **#93, #97,
  #98** plus HeapCreate, ThunkConnect32 and FT_Thunk; its image base is
  0xBAAA0000 with `.data`, `.idata`, `.rdata`, `.rsrc` marked SHARED. The
  98SE (DX6.1) and ME (DX7) builds were **not** checked; plan Phase 0.2
  does that on the guests.
- **[C]** `Lock` and `GetDC` on VRAM surfaces take the Win16Mutex unless
  `DDLOCK_NOSYSLOCK`, and that flag is ignored for the primary
  (`IDirectDrawSurface7::Lock` page; KB Q147256). The public `ddrawi.h`
  carries `dwWin16LockCnt`, `DDRAWI_NEEDSWIN16FORVRAMLOCK`,
  `DDRAWISURFGBL_LOCKNOTHOLDINGWIN16LOCK`, `ACCESSRECT_NOTHOLDINGWIN16LOCK`.
- **[I]** That DirectDraw holds the lock around Blt, Flip, CreateSurface
  and the D3D callbacks is assumed by this tree
  (`docs/plans/gdi-acceleration.md:29`, `d3d_i9xx.c:2010-2012`) and has
  **no public Microsoft statement**. Phase 0.2 measures it with
  `_ConfirmWin16Lock` inside the callbacks.
- **[C]** DDRAW exports no helper that takes the Win16 lock. DX3 exports
  `DDHAL32_VidMemAlloc/Free`, `DDInternalLock/Unlock`, `VidMem*`,
  `DSoundHelp` and the thunk data; later versions add
  `AcquireDDThreadLock`/`ReleaseDDThreadLock`, `GetDDSurfaceLocal`,
  `DDGetAttachedSurfaceLcl` (ddwrapper `exports.def`). **[I]**
  `AcquireDDThreadLock` is DDraw's own critical section, not the Win16Mutex.
- **[C]** KB125867: the Win16Mutex admits one thread at a time into 16-bit
  code; most USER32/GDI32 calls thunk down and take it, KERNEL32 calls do
  not; Microsoft says applications and DLLs must not touch it. **[I]** The
  hazards that follow: waiting on an event, mutex or `Sleep` while holding
  it deadlocks as soon as the awaited thread calls USER/GDI; calling DDraw
  while holding it can invert lock order against DDraw's critical section;
  a Win16 app that stops yielding stalls submission (DDraw has the same
  exposure). Recursion on the same thread nests. One lock also serializes
  against the 16-bit GDI acceleration path.
- **[C]** SciTech SNAP locks a DirectDraw surface specifically to obtain the
  Win16Mutex as its exclusive-hardware lock on 9x (`ddstereo.c`).
- **[C]** `DDRAWI_DDRAWSURFACE_INT { lpVtbl, lpLcl, lpLink, dwIntRefCnt }`;
  LCL has `lpSurfMore, lpGbl, ..., dwProcessId`; GBL has `fpVidMem` and a
  union of `lPitch`/`dwLinearSize` (SDK `ddrawi.h`, Wine `include/ddrawi.h`).
  `D3DHAL_CONTEXTCREATEDATA` unions `LPDIRECTDRAWSURFACE lpDDS` with
  `LPDDRAWI_DDRAWSURFACE_LCL lpDDSLcl`: pre-DX7 runtimes pass the COM
  pointer, which drivers cast to `_INT`; DX7+ passes the LCL. **[I]** The
  same cast from an application works on 9x for any interface version,
  because each interface is its own INT pointing at one LCL. **[I, strong]**
  DDraw allocates INT/LCL/GBL from a shared heap (the shared-arena HAL
  already receives these pointers from every process). **[I]** `fpVidMem`
  of a **system-memory** surface is a private address below 2 GB; only VRAM
  surfaces give a global linear address; read `fpVidMem`/`lPitch` under the
  lock because lose/restore rewrites them. No ICD was found that pokes these
  internals; Glide uses public DirectDraw calls.
- **[C]** Windowed present under `DDSCL_NORMAL`: primary + `SetHWnd` clipper
  + `Blt` to the `ClientToScreen` rect works; a clipper tied to an HWND
  refuses `SetClipList` (`DDERR_CLIPPERISUSINGHWND`). A mode change or
  another app taking exclusive mode gives `DDERR_SURFACELOST`; `Restore`
  fixes it, or returns `DDERR_WRONGMODE` after a mode change, in which case
  the surfaces are recreated. On 9x the HAL Blt callback can receive
  `IsClipped=TRUE` with `rOrigDest`, `dwRectCnt`, `prDestRects`
  (`DD_BLTDATA`). **This tree never reads `IsClipped`** (grep of `src/`);
  plan Phase 0.3 measures what 98SE actually hands the HAL.

## 7. What GLQuake and Quake 2 ask of the driver

Line numbers are against `id-Software/Quake` (`WinQuake/`) and
`id-Software/Quake-2` (`ref_gl/`, `win32/`) master, read 2026-09-26.

**Loading and setup [C]**

- GLQuake links `opengl32.lib` statically. It uses `LoadLibrary("opengl32.dll")`
  + `GetProcAddress("glBindTexture")` only when `GL_EXT_texture_object` is
  absent or `-gl11` is given (`gl_vidnt.c:518-529`); otherwise
  `wglGetProcAddress("glBindTextureEXT")`, and a NULL there is fatal
  (`:533-536`).
- Quake 2 `QGL_Init` (`qgl_win.c:3030`) `LoadLibrary`s `gl_driver` (default
  `opengl32`; the menu also offers `3dfxgl`, `pvrgl`, `veritegl`) and
  `GetProcAddress`es **all 336 GL 1.1 entry points** plus 21 `wgl*`
  functions (`:3054-3413`), none NULL-checked. `minidriver =
  !strstr(gl_driver, "opengl32")` (`glw_imp.c:435-438`); in ICD mode it uses
  GDI `ChoosePixelFormat`/`SetPixelFormat`/`DescribePixelFormat`
  (`:468-478`), and swaps through `wglSwapBuffers` only when
  `gl_drawbuffer == "GL_BACK"` (`:594-597`).
- Pixel format, both: `PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER`,
  RGBA, **24 colour bits, 32 depth bits, 0 stencil** (`gl_vidnt.c:816-848`;
  `glw_imp.c:392-411`). GLQuake never checks what it got. Quake 2 sets
  `mcd_accelerated = gl_allow_software` for a format without
  `PFD_GENERIC_ACCELERATED` (`:480-492`), which only matters to
  `VerifyDriver`, which rejects only a lower-cased renderer equal to
  `"gdi generic"` (`:47-56,523`).
- Display mode: GLQuake defaults to fullscreen via `ChangeDisplaySettings`
  with bpp order 15, 16, 32, 24 (`gl_vidnt.c:1740-1752`) and takes the
  **first** listed mode matching width and bpp (`:1724-1731`), so a driver
  listing 640x400 before 640x480 at 16 bpp gets 640x400. Quake 2 defaults
  to **windowed** (`vid_fullscreen 0`, `vid_dll.c:714`), `gl_mode 3` =
  640x480, keeps the desktop bpp unless `gl_bitdepth` (Win95 OSR2+ only).
- Every Quake 2 `vid_restart` or mode change destroys the window and GL
  context and rebuilds them (`glw_imp.c:176-179`); a ref reload also
  `FreeLibrary`s opengl32 (`qgl_win.c:2645`).
- Quake 2 calls `wglSwapIntervalEXT` only if `"WGL_EXT_swap_control"`
  appears **in `GL_EXTENSIONS`** (`gl_rmain.c:1259-1261`); it never calls
  `wglGetExtensionsStringEXT`.

**Entry points called at runtime [C]**

- GLQuake: glAlphaFunc, glBlendFunc, glClear, glClearColor, glCullFace,
  glDepthFunc, glDepthMask, glDepthRange, glDisable, glEnable,
  glDrawBuffer, glReadBuffer, glFinish, glGetString, glHint,
  glPolygonMode, glShadeModel, glViewport; glBegin/glEnd, glColor3f,
  glColor3ubv, glColor4f, glColor4fv, glTexCoord2f, glVertex2f,
  glVertex3f, glVertex3fv; glFrustum, glOrtho, glGetFloatv,
  glLoadIdentity, glLoadMatrixf, glMatrixMode, glPushMatrix, glPopMatrix,
  glRotatef, glScalef, glTranslatef; glBindTexture (via pointer),
  glTexEnvf, glTexImage2D, glTexParameterf, glTexSubImage2D,
  glReadPixels; the SGIS multitexture pair; glColorTableEXT. `glFog*` is
  inside a comment (`gl_rmain.c:1133-1138`). **Never:** glGenTextures,
  glDeleteTextures, glGetIntegerv, glGetError, glPixelStore, glScissor,
  glColorMask, glPolygonOffset, stencil, lighting, texgen, display lists,
  glDrawArrays/Elements.
- Quake 2 adds: glColor3fv, glColor4ubv, glDeleteTextures, glGetError
  (every frame, `glw_imp.c:592`), glPointSize, glScissor,
  glEnableClientState, glVertexPointer, glColorPointer, glArrayElement
  (**inside** glBegin/glEnd, mixed with immediate glTexCoord2f,
  `gl_mesh.c:219-228`, only with `gl_vertex_arrays 1`, default 0),
  glLockArraysEXT/glUnlockArraysEXT, glPointParameterf(v)EXT. Never
  glDisableClientState, glGenTextures, glGetIntegerv, glHint, fog, stencil,
  glColorMask, glPolygonOffset, lighting, texgen, display lists.
- Primitives: `GL_POLYGON` (world, water, sky, lightmaps), `GL_TRIANGLE_FAN`
  and `GL_TRIANGLE_STRIP` (alias models, shadows, multitexture world),
  `GL_QUADS` (2D, sprites, polyblend), `GL_TRIANGLES` (particles). Quake 2
  adds `GL_POINTS` (only with `EXT_point_parameters`), `GL_LINE_STRIP`
  (`gl_showtris`), `GL_LINES` (Intergraph stereo only).

**State [C]**

- Blend pairs: `SRC_ALPHA/ONE_MINUS_SRC_ALPHA` (default), `ONE/ONE`
  (flashblend, `gl_saturatelighting`), `ZERO/ONE_MINUS_SRC_COLOR` (GLQuake
  luminance lightmaps, `gl_rsurf.c:382,682`), `ZERO/SRC_COLOR` (Quake 2
  lightmaps, `gl_rsurf.c:348,351,361`). No others.
- Texture env: `GL_REPLACE` default; `GL_MODULATE` for models, particles,
  alpha surfaces, the Quake 2 lightmap unit; `GL_BLEND` only on GLQuake's
  SGIS lightmap unit with an inverted-luminance lightmap and the default
  black env colour (`gl_rsurf.c:452,554`). `GL_DECAL` never. Quake 2 caches
  env mode per texture unit (`gl_image.c:114-122`).
- Alpha test `GL_GREATER, 0.666`, enabled by default, off for 3D in
  `R_SetupGL`, on for 2D.
- Depth: `GL_LEQUAL` and `GL_GEQUAL` only. **ztrick** (GLQuake default
  `gl_ztrick 1`, `gl_vidnt.c:106`; Quake 2 default 0): odd frames range
  `(0, 0.49999)` with LEQUAL, even frames range `(1, 0.5)` (**near >
  far**) with GEQUAL, and the depth buffer is **never cleared**; with
  `gl_clear 0` (default) GLQuake issues no `glClear` during play. Weapon
  depth hack `glDepthRange(min, min + 0.3(max-min))`. `glClearDepth` never
  called.
- Cull: `glCullFace(GL_FRONT)` when `gl_cull`; `GL_BACK` for GLQuake
  mirrors and the Quake 2 left-handed weapon, both with `glScalef(-1, ...)`
  on the **projection** matrix.
- Polygon mode `FILL` only; `glShadeModel` FLAT by default, SMOOTH for
  models and dlights. Fog, stencil, polygon offset, glColorMask: unused.
  `glScissor`: Quake 2 only, around a no-world view. `glReadPixels(GL_RGB,
  GL_UNSIGNED_BYTE)` for screenshots; GLQuake `envmap` reads the **front**
  buffer as RGBA. GLQuake draws the loading disc straight to **`GL_FRONT`**
  (`gl_draw.c:811-813`); Quake 2 `glDrawBuffer(GL_BACK)` every frame.
  `glFinish` when `gl_finish` or `r_speeds`.

**Textures [C]**

- Neither calls `glGenTextures`. GLQuake numbers from 1 upward; Quake 2
  uses fixed names 1024/1152/1153+ and deletes with `glDeleteTextures`.
  **The driver must accept `glBindTexture` on never-generated names.**
- Internal formats are mostly the component counts 3 and 4 with
  `GL_RGBA`/`GL_UNSIGNED_BYTE` data; GLQuake lightmaps use internal format 1
  from `GL_LUMINANCE` data (`-lm_a/_i/_2/_4` select ALPHA, INTENSITY,
  RGBA4, RGBA; `-lm_i` passes `GL_INTENSITY` as the *format*, an invalid
  enum, `gl_rsurf.c:1685-1687`). Quake 2 lightmaps are RGBA data with
  internal format 3, `GL_LUMINANCE8`, `GL_INTENSITY8` or the alpha format by
  `gl_monolightmap`; RGBA→L and RGBA→I take R. Paletted uploads use
  `GL_COLOR_INDEX8_EXT`.
- Sizes: width and height rounded up to a power of two **independently**
  (`gl_draw.c:1008-1011`; Quake 2 `gl_image.c:968-975`), so 64x32 and
  512x256 are normal. GLQuake caps at `gl_max_size` 1024 (256 for a
  renderer starting "3dfx" or containing "Glide"); Quake 2 hard-caps at
  256. **Neither queries `GL_MAX_TEXTURE_SIZE`.** Lightmaps 128x128,
  particle 8x8, cinematics 256x256 uploaded every frame.
- Mip chains are uploaded level by level down to 1x1 with each dimension
  clamped at 1 (64x32 ... 2x1, 1x1).
- Filters: default min `GL_LINEAR_MIPMAP_NEAREST`, mag `GL_LINEAR`; all six
  offered; non-mipmapped textures use the mag filter for both. Wrap is
  `GL_REPEAT` only. Quake 2 particle coordinates reach 1.0625. All
  parameters via `glTexParameterf`.
- `glTexSubImage2D`: GLQuake full-width rows; Quake 2 arbitrary
  sub-rectangles.

**Extensions and renderer-string checks [C]**

- GLQuake: `GL_EXT_texture_object`; `GL_SGIS_multitexture` matched as
  `"GL_SGIS_multitexture "` **with a trailing space** (`gl_vidnt.c:580`),
  so it is missed when last in the string; `glColorTableEXT` is enabled if
  `wglGetProcAddress` returns non-NULL **and the extension is not
  advertised** (`:1519-1521`), a bug that makes a NULL return mandatory.
  Quake 2: `GL_SGIS_multitexture`, `GL_EXT_paletted_texture` +
  `GL_EXT_shared_texture_palette` (then loads `pics/16to8.dat`, fatal if
  missing), `GL_EXT_compiled_vertex_array`, `WGL_EXT_swap_control`,
  `GL_EXT_point_parameters`. SGIS tokens `TEXTURE0_SGIS=0x835E`,
  `TEXTURE1_SGIS=0x835F`. **Neither checks `GL_ARB_multitexture`.**
- Both print the extension string through `vsprintf` into a 4096-byte
  buffer; keep `GL_EXTENSIONS` well under 4 KB.
- GLQuake: `strnicmp(renderer, "PowerVR", 7)` → `fullsbardraw`;
  `strnicmp(renderer, "Permedia", 8)` → RGBA lightmaps with alpha blending;
  renderer starting "3dfx" or containing "Glide" → `gl_max_size 256`;
  renderer containing "Voodoo" or vendor containing "3Dfx" (case-sensitive)
  → gamma 1.0.
- Quake 2 (`gl_rmain.c:1162-1190`) lower-cases renderer and vendor, then in
  order: renderer contains `voodoo` (+`rush`), **vendor** contains `sgi`,
  renderer `permedia`, `glint`, `glzicd`, `gdi`, `pcx2`, `verite`, else
  OTHER. Effects: PERMEDIA2 forces `gl_monolightmap A`; MCD (`gdi`) forces
  `gl_finish 1` and 2D alpha-test hacks; 3DLabs sets `allow_cds`; Voodoo
  gets identity gamma. Mask bug: `GL_RENDERER_SGI` (0x00F00000) overlaps
  `GL_RENDERER_RENDITION` (0x001C0000), so a vendor containing "sgi" also
  triggers the Rendition hacks. `"gdi generic"` exactly is rejected.
- **Safe strings**, case-insensitive: the renderer must not contain voodoo,
  rush, permedia, glint, glzicd, gdi, pcx2, verite or glide, nor start
  with 3dfx or powervr; the vendor must not contain sgi or 3dfx.
  "Velocity9x" satisfies both.

**Matrices and features [C]**: MODELVIEW and PROJECTION only;
`glFrustum` via a local `MYgluPerspective` (near 4, far 4096); `glOrtho(0,
w, h, 0, -99999, 99999)`; `glGetFloatv(GL_MODELVIEW_MATRIX)` then
`glLoadMatrixf` of it; push/pop on both stacks (Quake 2). No GL lighting,
display lists, texgen or texture matrices; lighting is CPU-side per-vertex
`glColor` with SMOOTH + MODULATE.

**Cvars that simplify testing [C]**: `gl_ztrick 0`, `gl_clear 1`,
`gl_finish 1`, `gl_texsort`, `gl_flashblend` (GLQuake default 1 skips
dynamic lightmap updates; Quake 2 default 0), `r_fullbright 1`,
`r_dynamic 0`/`gl_dynamic 0`, `gl_nobind`, `gl_cull 0`, `gl_polyblend 0`,
`r_drawentities 0`, `gl_picmip`, `gl_texturemode`, `gl_playermip`
(GLQuake). Command line: `-window`, `-width`, `-height`, `-bpp`, `-current`,
`-nomtex`, `-no8bit`, `-gl11`, `-lm_1|_a|_i|_2|_4`. `r_drawflat` does not
exist in either GL renderer. **Quake 2 `gl_log 1`** writes every GL call
with arguments to `gl.log` (`qgl_win.c:751ff`). `gl_monolightmap`: only the
first character counts, and `R_Init` resets it to `0` unless the second
character is `F` (`gl_rmain.c:1192-1207`), so use `AF`, `LF`, ...: `0` RGB
lightmaps with `ZERO/SRC_COLOR`; `L`/`I` luminance/intensity, still
multiplicative; **`A`** alpha-format lightmap (RGB=0, α=255-max) with
`SRC_ALPHA/ONE_MINUS_SRC_ALPHA`, the Permedia2 path that needs **no
multiplicative blend**; `C` fake coloured with the same blend. The
multitexture path ignores it and modulates on unit 1, so test `A` with
`gl_ext_multitexture 0`. GLQuake's equivalent is `-lm_4`.

## 8. What this supersedes

`2026-08-30-virge-opengl-prior-art.md` concluded that the achievable
deliverable was a MiniGL or a wrapper onto the D3D path, "not an OpenGL
1.1 implementation", because none of the four shipped ViRGE GL efforts was
an ICD. The two hardware walls it names still stand: the ViRGE has no
triangle setup and no multiplicative blend, and this record adds that
Quake 2's lightmaps need exactly that blend (`ZERO/SRC_COLOR`). What
changed is the decision, taken 2026-09-26: a full ICD on a render core
shared with Direct3D, with the ViRGE's missing blend served by a per-draw
software fallback and `gl_monolightmap A` recorded as the no-fallback path.
The MiniGL conclusion is withdrawn; the four dead ends it lists are still
not to be walked.

## What this does not establish

- Anything about original Windows 98, ME or 95: every source above is
  version-agnostic or NT, and no guest for those OSes exists here.
- That DDRAW.DLL on ME still imports #93/#97/#98 (98SE does: see
  `2026-09-26-98se-ddraw-imports-the-win16-mutex-ordinals.md`), or holds the
  Win16 lock around HAL callbacks.
- What 98SE hands the HAL for a clipped Blt.
- Whether the INT→LCL cast and `dwProcessId` behave as inferred from an
  application process.
- Whether 9x opengl32 orders ICD pixel formats first.

Each is a numbered measurement in the plan's Phase 0.
