#ifndef VELOCITY9X_WIN9X_DDRAW_ABI_H
#define VELOCITY9X_WIN9X_DDRAW_ABI_H

/* Engine type and capability values are shared with the 16-bit hardware
 * layer, which cannot include this header. */
#include "velocity9x/engine_abi.h"

/*
 * Minimal Windows 9x DirectDraw HAL ABI used by Velocity9x, written from
 * the published Windows 98 DDK interface documentation (DDRAWI.H layouts).
 * The same header compiles in the 16-bit display driver (wcc) and the
 * 32-bit V9XHAL.DLL (wcc386). Every structure is packed to one byte so the
 * cross-bitness shared block has one layout.
 *
 * Pointer-width rule: fields DDRAW dereferences on the 16-bit side are
 * 16:16 far pointers, which are 4 bytes wide - the same width as the flat
 * 32-bit pointers the DDRAW32/HEL side uses. Both compilers therefore see
 * identical offsets; the 16-bit compilation uses FAR pointer types and the
 * 32-bit compilation uses flat types through V9X_DD_PTR/V9X_DD_CODE_PTR.
 */

#ifdef __386__
#define V9X_DD_PTR(type)        type *
#define V9X_DD_VOID_PTR         void *
typedef void *V9X_DD_CODE_PTR;
#else
#define V9X_DD_PTR(type)        type FAR *
#define V9X_DD_VOID_PTR         void FAR *
typedef void (FAR PASCAL *V9X_DD_CODE_PTR)();
#endif

/* Escape plumbing (values from the DDK DCI/DDRAWI contracts). */
#define V9X_QUERYESCSUPPORT               8u
#define V9X_DCICOMMAND                 3075u
#define V9X_DD_VERSION           0x00000200ul
#define V9X_DD_HAL_VERSION           0x00ffu
#define V9X_DD_RUNTIME_VERSION   0x0000050aul

#define V9X_DDCREATEDRIVEROBJECT         10ul
#define V9X_DDGET32BITDRIVERNAME         11ul
#define V9X_DDNEWCALLBACKFNS             12ul
#define V9X_DDVERSIONINFO                13ul

/* Project-private DCICOMMAND: copy the HAL trace snapshot to the output
 * buffer. The value is far outside the documented DCI/DDRAW command range
 * so a runtime that does not know it cannot collide with it. */
#define V9X_DDGETTRACE           0x56395452ul /* 'V9TR' */

/* Project-private DCICOMMAND: arm the engine fault injector. dwParam1 is the
 * number of subsequent bounded engine waits that must report a timeout
 * instead of completing, which drives the recovery path deterministically
 * without needing the hardware to actually hang. See fault_inject below. */
#define V9X_DDFAULTINJECT        0x56394649ul /* 'V9FI' */

/*
 * Project-private DCICOMMANDs for the 16-bit GDI acceleration path
 * (docs\plans\gdi-acceleration.md). They are answered by src\display16, not by
 * the HAL, and are served on every family - including one with no DirectDraw
 * HAL at all - because GDI acceleration is a display-driver service and its
 * counters are the only evidence that a primitive fired.
 *
 * V9X_GDIGETSTATS copies a V9X_GDI_STATS to the output buffer.
 * V9X_GDIFAULTINJECT arms the GDI bounded waits' fault injector with dwParam1,
 * mirroring V9X_DDFAULTINJECT: an armed count is consumed by production waits
 * falling into their existing timeout tail, so the injector drives the
 * shipping recovery path rather than a parallel test one.
 */
#define V9X_GDIGETSTATS          0x56394753ul /* 'V9GS' */
#define V9X_GDIFAULTINJECT       0x56394749ul /* 'V9GI' */

/* Driver-side return conventions. */
#define V9X_DDHAL_DRIVER_NOTHANDLED  0x00000000ul
#define V9X_DDHAL_DRIVER_HANDLED     0x00000001ul
#define V9X_DD_OK                    0x00000000ul
#define V9X_DDERR_WASSTILLDRAWING    0x8876021cul

/* Caps and flag bits used by this driver (DDK values). */
#define V9X_DDCAPS_BLT               0x00000040ul
#define V9X_DDCAPS_3D                0x00000001ul
#define V9X_DDCAPS_GDI               0x00000400ul
#define V9X_DDCAPS_VBI               0x00080000ul
#define V9X_DDCAPS_BLTCOLORFILL      0x04000000ul
#define V9X_DDSCAPS_3DDEVICE         0x00002000ul
#define V9X_DDSCAPS_BACKBUFFER       0x00000004ul
#define V9X_DDSCAPS_COMPLEX          0x00000008ul
#define V9X_DDSCAPS_OFFSCREENPLAIN   0x00000040ul
#define V9X_DDSCAPS_PRIMARYSURFACE   0x00000200ul
#define V9X_DDSCAPS_TEXTURE          0x00001000ul
#define V9X_DDSCAPS_MIPMAP           0x00400000ul
#define V9X_DDSCAPS_SYSTEMMEMORY     0x00000800ul
#define V9X_DDSCAPS_FLIP             0x00000010ul
#define V9X_DDSCAPS_VIDEOMEMORY      0x00004000ul
#define V9X_DDSCAPS_ZBUFFER          0x00020000ul
#define V9X_DDPF_ALPHAPIXELS         0x00000001ul
#define V9X_DDPF_RGB                 0x00000040ul
#define V9X_DDPF_PALETTEINDEXED8     0x00000020ul
#define V9X_DDPF_ZBUFFER             0x00000400ul
#define V9X_VIDMEM_ISLINEAR          0x00000001ul
#define V9X_DDMODEINFO_PALETTIZED        0x0001u
#define V9X_DDHALINFO_ISPRIMARYDISPLAY 0x00000001ul
#define V9X_DDHALINFO_GETDRIVERINFOSET 0x00000004ul

#define V9X_DDHAL_CB32_CREATESURFACE        0x00000002ul
#define V9X_DDHAL_CB32_WAITFORVERTICALBLANK 0x00000010ul
#define V9X_DDHAL_CB32_CANCREATESURFACE     0x00000020ul
#define V9X_DDHAL_CB32_SETEXCLUSIVEMODE     0x00000100ul
#define V9X_DDHAL_CB32_FLIPTOGDISURFACE     0x00000200ul
#define V9X_DDHAL_SURFCB32_DESTROYSURFACE  0x00000001ul
#define V9X_DDHAL_SURFCB32_FLIP          0x00000002ul
#define V9X_DDHAL_SURFCB32_LOCK          0x00000008ul
#define V9X_DDHAL_SURFCB32_UNLOCK        0x00000010ul
#define V9X_DDHAL_SURFCB32_BLT           0x00000020ul
#define V9X_DDHAL_SURFCB32_ADDATTACHEDSURFACE 0x00000080ul
#define V9X_DDHAL_SURFCB32_GETBLTSTATUS  0x00000100ul
#define V9X_DDHAL_SURFCB32_GETFLIPSTATUS 0x00000200ul

#define V9X_DDFLIP_NOVSYNC           0x00000008ul
#define V9X_DDFLIP_DONOTWAIT         0x00000020ul
#define V9X_DDWAITVB_I_TESTVB        0x80000006ul
#define V9X_DDWAITVB_BLOCKBEGIN      0x00000001ul
#define V9X_DDWAITVB_BLOCKEND        0x00000004ul
#define V9X_DDGFS_CANFLIP            0x00000001ul
#define V9X_DDGFS_ISFLIPDONE         0x00000002ul
#define V9X_DDGBS_CANBLT             0x00000001ul
#define V9X_DDGBS_ISBLTDONE          0x00000002ul

#define V9X_DDBLT_ASYNC              0x00000200ul
#define V9X_DDBLT_COLORFILL          0x00000400ul
#define V9X_DDBLT_ROP                0x00020000ul
#define V9X_DDBLT_WAIT               0x01000000ul
#define V9X_DDBLT_DONOTWAIT          0x08000000ul
#define V9X_DDROP_SRCCOPY            0x00cc0020ul
#define V9X_DDLOCK_WAIT              0x00000001ul
#define V9X_DDLOCK_DONOTWAIT         0x00004000ul

#define V9X_DDSD_CAPS                0x00000001ul
#define V9X_DDSD_PIXELFORMAT         0x00001000ul
#define V9X_DDRAWISURF_HASPIXELFORMAT 0x00002000ul

#pragma pack(push, 1)

/* DCI escape command block (DCIDDI.H layout). */
typedef struct v9x_dcicmd {
    DWORD dwCommand;
    DWORD dwParam1;
    DWORD dwParam2;
    DWORD dwVersion;
    DWORD dwReserved;
} V9X_DCICMD;

/* DDGET32BITDRIVERNAME output (DDRAWI.H DD32BITDRIVERDATA layout). */
typedef struct v9x_dd32bitdriverdata {
    char szName[260];
    char szEntryPoint[64];
    DWORD dwContext;
} V9X_DD32BITDRIVERDATA;

/* DDVERSIONINFO output (DDRAWI.H DDVERSIONDATA layout). */
typedef struct v9x_ddversiondata {
    DWORD dwHALVersion;
    DWORD dwReserved1;
    DWORD dwReserved2;
} V9X_DDVERSIONDATA;

/* DDRAW16 function table delivered by DDNEWCALLBACKFNS (DDHALDDRAWFNS). */
typedef struct v9x_ddhalddrawfns {
    DWORD dwSize;
    V9X_DD_CODE_PTR lpSetInfo;
    V9X_DD_CODE_PTR lpVidMemAlloc;
    V9X_DD_CODE_PTR lpVidMemFree;
} V9X_DDHALDDRAWFNS;

/* DDPIXELFORMAT (32 bytes). */
typedef struct v9x_ddpixelformat {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    DWORD dwRGBBitCount;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwRGBAlphaBitMask;
} V9X_DDPIXELFORMAT;

typedef struct v9x_ddcolorkey {
    DWORD dwColorSpaceLowValue;
    DWORD dwColorSpaceHighValue;
} V9X_DDCOLORKEY;

/* DDSCAPS/DDSURFACEDESC v1, used by D3DHAL_GLOBALDRIVERDATA's texture
 * format array. The union members in the public DDK layout are DWORD-sized. */
typedef struct v9x_ddcaps {
    DWORD dwCaps;
} V9X_DDSCAPS;

typedef struct v9x_ddsurfacedesc {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    LONG lPitch;
    DWORD dwBackBufferCount;
    DWORD dwMipMapCount;
    DWORD dwAlphaBitDepth;
    DWORD dwReserved;
    V9X_DD_VOID_PTR lpSurface;
    V9X_DDCOLORKEY ddckCKDestOverlay;
    V9X_DDCOLORKEY ddckCKDestBlt;
    V9X_DDCOLORKEY ddckCKSrcOverlay;
    V9X_DDCOLORKEY ddckCKSrcBlt;
    V9X_DDPIXELFORMAT ddpfPixelFormat;
    V9X_DDSCAPS ddsCaps;
} V9X_DDSURFACEDESC;

/* DDBLTFX (100 bytes). Pointer-valued union members are all DWORD-sized on
 * both sides of the Win9x DirectDraw boundary. */
typedef struct v9x_ddbltfx {
    DWORD dwSize;
    DWORD dwDDFX;
    DWORD dwROP;
    DWORD dwDDROP;
    DWORD dwRotationAngle;
    DWORD dwZBufferOpCode;
    DWORD dwZBufferLow;
    DWORD dwZBufferHigh;
    DWORD dwZBufferBaseDest;
    DWORD dwZDestConstBitDepth;
    DWORD dwZDestConst;
    DWORD dwZSrcConstBitDepth;
    DWORD dwZSrcConst;
    DWORD dwAlphaEdgeBlendBitDepth;
    DWORD dwAlphaEdgeBlend;
    DWORD dwReserved;
    DWORD dwAlphaDestConstBitDepth;
    DWORD dwAlphaDestConst;
    DWORD dwAlphaSrcConstBitDepth;
    DWORD dwAlphaSrcConst;
    DWORD dwFillColor;
    V9X_DDCOLORKEY ddckDestColorkey;
    V9X_DDCOLORKEY ddckSrcColorkey;
} V9X_DDBLTFX;

/* VIDMEM heap descriptor (24 bytes). ddsCaps fields are restriction
 * masks: what the heap can NOT be used for. */
typedef struct v9x_vidmem {
    DWORD dwFlags;
    DWORD fpStart;
    DWORD fpEnd;
    DWORD ddsCaps;
    DWORD ddsCapsAlt;
    DWORD lpHeap;
} V9X_VIDMEM;

/* VIDMEMINFO (80 bytes at pack(1)). */
typedef struct v9x_vidmeminfo {
    DWORD fpPrimary;
    DWORD dwFlags;
    DWORD dwDisplayWidth;
    DWORD dwDisplayHeight;
    LONG lDisplayPitch;
    V9X_DDPIXELFORMAT ddpfDisplay;
    DWORD dwOffscreenAlign;
    DWORD dwOverlayAlign;
    DWORD dwTextureAlign;
    DWORD dwZBufferAlign;
    DWORD dwAlphaAlign;
    DWORD dwNumHeaps;
    V9X_DD_PTR(V9X_VIDMEM) pvmList;
} V9X_VIDMEMINFO;

/* DDHALMODEINFO (36 bytes). */
typedef struct v9x_ddhalmodeinfo {
    DWORD dwWidth;
    DWORD dwHeight;
    LONG lPitch;
    DWORD dwBPP;
    WORD wFlags;
    WORD wRefreshRate;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwAlphaBitMask;
} V9X_DDHALMODEINFO;

#define V9X_DD_ROP_SPACE 8

/* DDCORECAPS (312 bytes = 78 DWORDs). */
typedef struct v9x_ddcorecaps {
    DWORD dwSize;
    DWORD dwCaps;
    DWORD dwCaps2;
    DWORD dwCKeyCaps;
    DWORD dwFXCaps;
    DWORD dwFXAlphaCaps;
    DWORD dwPalCaps;
    DWORD dwSVCaps;
    DWORD dwAlphaBltConstBitDepths;
    DWORD dwAlphaBltPixelBitDepths;
    DWORD dwAlphaBltSurfaceBitDepths;
    DWORD dwAlphaOverlayConstBitDepths;
    DWORD dwAlphaOverlayPixelBitDepths;
    DWORD dwAlphaOverlaySurfaceBitDepths;
    DWORD dwZBufferBitDepths;
    DWORD dwVidMemTotal;
    DWORD dwVidMemFree;
    DWORD dwMaxVisibleOverlays;
    DWORD dwCurrVisibleOverlays;
    DWORD dwNumFourCCCodes;
    DWORD dwAlignBoundarySrc;
    DWORD dwAlignSizeSrc;
    DWORD dwAlignBoundaryDest;
    DWORD dwAlignSizeDest;
    DWORD dwAlignStrideAlign;
    DWORD dwRops[V9X_DD_ROP_SPACE];
    DWORD ddsCaps;
    DWORD dwMinOverlayStretch;
    DWORD dwMaxOverlayStretch;
    DWORD dwMinLiveVideoStretch;
    DWORD dwMaxLiveVideoStretch;
    DWORD dwMinHwCodecStretch;
    DWORD dwMaxHwCodecStretch;
    DWORD dwReserved1;
    DWORD dwReserved2;
    DWORD dwReserved3;
    DWORD dwSVBCaps;
    DWORD dwSVBCKeyCaps;
    DWORD dwSVBFXCaps;
    DWORD dwSVBRops[V9X_DD_ROP_SPACE];
    DWORD dwVSBCaps;
    DWORD dwVSBCKeyCaps;
    DWORD dwVSBFXCaps;
    DWORD dwVSBRops[V9X_DD_ROP_SPACE];
    DWORD dwSSBCaps;
    DWORD dwSSBCKeyCaps;
    DWORD dwSSBFXCaps;
    DWORD dwSSBRops[V9X_DD_ROP_SPACE];
    DWORD dwMaxVideoPorts;
    DWORD dwCurrVideoPorts;
    DWORD dwSVBCaps2;
} V9X_DDCORECAPS;

/* DIRECTDRAW object callbacks (48 bytes: 2 DWORDs + 10 pointers). */
typedef struct v9x_ddhal_ddcallbacks {
    DWORD dwSize;
    DWORD dwFlags;
    V9X_DD_CODE_PTR DestroyDriver;
    V9X_DD_CODE_PTR CreateSurface;
    V9X_DD_CODE_PTR SetColorKey;
    V9X_DD_CODE_PTR SetMode;
    V9X_DD_CODE_PTR WaitForVerticalBlank;
    V9X_DD_CODE_PTR CanCreateSurface;
    V9X_DD_CODE_PTR CreatePalette;
    V9X_DD_CODE_PTR GetScanLine;
    V9X_DD_CODE_PTR SetExclusiveMode;
    V9X_DD_CODE_PTR FlipToGDISurface;
} V9X_DDHAL_DDCALLBACKS;

/* DIRECTDRAWSURFACE object callbacks (68 bytes: 2 DWORDs + 15 pointers). */
typedef struct v9x_ddhal_ddsurfacecallbacks {
    DWORD dwSize;
    DWORD dwFlags;
    V9X_DD_CODE_PTR DestroySurface;
    V9X_DD_CODE_PTR Flip;
    V9X_DD_CODE_PTR SetClipList;
    V9X_DD_CODE_PTR Lock;
    V9X_DD_CODE_PTR Unlock;
    V9X_DD_CODE_PTR Blt;
    V9X_DD_CODE_PTR SetColorKey;
    V9X_DD_CODE_PTR AddAttachedSurface;
    V9X_DD_CODE_PTR GetBltStatus;
    V9X_DD_CODE_PTR GetFlipStatus;
    V9X_DD_CODE_PTR UpdateOverlay;
    V9X_DD_CODE_PTR SetOverlayPosition;
    V9X_DD_CODE_PTR reserved4;
    V9X_DD_CODE_PTR SetPalette;
} V9X_DDHAL_DDSURFACECALLBACKS;

/* DIRECTDRAWPALETTE object callbacks (16 bytes). */
typedef struct v9x_ddhal_ddpalettecallbacks {
    DWORD dwSize;
    DWORD dwFlags;
    V9X_DD_CODE_PTR DestroyPalette;
    V9X_DD_CODE_PTR SetEntries;
} V9X_DDHAL_DDPALETTECALLBACKS;

/* DIRECTDRAWEXEBUF pseudo-surface callbacks (28 bytes). */
#define V9X_DDHAL_EXEBUFCB32_CANCREATE 0x00000001ul
#define V9X_DDHAL_EXEBUFCB32_CREATE    0x00000002ul
#define V9X_DDHAL_EXEBUFCB32_DESTROY   0x00000004ul
#define V9X_DDHAL_EXEBUFCB32_LOCK      0x00000008ul
#define V9X_DDHAL_EXEBUFCB32_UNLOCK    0x00000010ul
typedef struct v9x_ddhal_ddexecutebuffercallbacks {
    DWORD dwSize;
    DWORD dwFlags;
    V9X_DD_CODE_PTR CanCreateExecuteBuffer;
    V9X_DD_CODE_PTR CreateExecuteBuffer;
    V9X_DD_CODE_PTR DestroyExecuteBuffer;
    V9X_DD_CODE_PTR LockExecuteBuffer;
    V9X_DD_CODE_PTR UnlockExecuteBuffer;
} V9X_DDHAL_DDEXEBUFCALLBACKS;

/* DDHALINFO (V2 layout, 456 bytes at pack(1)). */
typedef struct v9x_ddhalinfo {
    DWORD dwSize;
    V9X_DD_PTR(V9X_DDHAL_DDCALLBACKS) lpDDCallbacks;
    V9X_DD_PTR(V9X_DDHAL_DDSURFACECALLBACKS) lpDDSurfaceCallbacks;
    V9X_DD_PTR(V9X_DDHAL_DDPALETTECALLBACKS) lpDDPaletteCallbacks;
    V9X_VIDMEMINFO vmiData;
    V9X_DDCORECAPS ddCaps;
    DWORD dwMonitorFrequency;
    V9X_DD_CODE_PTR GetDriverInfo;
    DWORD dwModeIndex;
    V9X_DD_VOID_PTR lpdwFourCC;
    DWORD dwNumModes;
    V9X_DD_PTR(V9X_DDHALMODEINFO) lpModeInfo;
    DWORD dwFlags;
    V9X_DD_VOID_PTR lpPDevice;
    DWORD hInstance;
    DWORD lpD3DGlobalDriverData;
    DWORD lpD3DHALCallbacks;
    V9X_DD_VOID_PTR lpDDExeBufCallbacks;
} V9X_DDHALINFO;

#define V9X_DDHALINFO_SIZE 460ul

/* Minimal Direct3D HAL v1/DX5 ABI (D3DHAL.H/D3DCAPS.H). */
#define V9X_D3DDD_COLORMODEL             0x00000001ul
#define V9X_D3DDD_DEVCAPS                0x00000002ul
#define V9X_D3DDD_TRICAPS                0x00000040ul
#define V9X_D3DDD_DEVICERENDERBITDEPTH   0x00000080ul
#define V9X_D3DDD_DEVICEZBUFFERBITDEPTH  0x00000100ul
#define V9X_D3DCOLOR_RGB                         2ul
#define V9X_DDBD_16                      0x00000400ul
#define V9X_D3DDEVCAPS_FLOATTLVERTEX      0x00000001ul
#define V9X_D3DDEVCAPS_SORTEXACT          0x00000008ul
#define V9X_D3DDEVCAPS_EXECUTESYSTEMMEMORY 0x00000010ul
#define V9X_D3DDEVCAPS_TLVERTEXSYSTEMMEMORY 0x00000040ul
#define V9X_D3DDEVCAPS_DRAWPRIMTLVERTEX  0x00000400ul
#define V9X_D3DDEVCAPS_TEXTUREVIDEOMEMORY 0x00000200ul
#define V9X_D3DDD_LINECAPS               0x00000020ul
#define V9X_D3DPMISCCAPS_CULLNONE         0x00000010ul
#define V9X_D3DPMISCCAPS_CULLCW           0x00000020ul
#define V9X_D3DPMISCCAPS_CULLCCW          0x00000040ul
#define V9X_D3DPRASTERCAPS_DITHER         0x00000001ul
#define V9X_D3DPBLENDCAPS_ZERO            0x00000001ul
#define V9X_D3DPBLENDCAPS_ONE             0x00000002ul
#define V9X_D3DPSHADECAPS_SPECULARFLATRGB 0x00000080ul
#define V9X_D3DPRASTERCAPS_ZTEST          0x00000010ul
#define V9X_D3DPRASTERCAPS_SUBPIXEL       0x00000020ul
#define V9X_D3DPRASTERCAPS_FOGVERTEX      0x00000080ul
#define V9X_D3DPCMPCAPS_NEVER             0x00000001ul
#define V9X_D3DPCMPCAPS_LESS              0x00000002ul
#define V9X_D3DPCMPCAPS_EQUAL             0x00000004ul
#define V9X_D3DPCMPCAPS_LESSEQUAL         0x00000008ul
#define V9X_D3DPCMPCAPS_GREATER           0x00000010ul
#define V9X_D3DPCMPCAPS_NOTEQUAL          0x00000020ul
#define V9X_D3DPCMPCAPS_GREATEREQUAL      0x00000040ul
#define V9X_D3DPCMPCAPS_ALWAYS            0x00000080ul
#define V9X_D3DPSHADECAPS_COLORFLATRGB    0x00000002ul
#define V9X_D3DPSHADECAPS_COLORGOURAUDRGB 0x00000008ul
#define V9X_D3DPSHADECAPS_SPECULARGOURAUDRGB 0x00000200ul
#define V9X_D3DPSHADECAPS_ALPHAFLATBLEND  0x00001000ul
#define V9X_D3DPSHADECAPS_ALPHAGOURAUDBLEND 0x00004000ul
#define V9X_D3DPSHADECAPS_FOGFLAT         0x00040000ul
#define V9X_D3DPSHADECAPS_FOGGOURAUD      0x00080000ul
#define V9X_D3DPBLENDCAPS_SRCALPHA        0x00000010ul
#define V9X_D3DPBLENDCAPS_INVSRCALPHA     0x00000020ul
#define V9X_D3DRENDERSTATE_SRCBLEND                19ul
#define V9X_D3DRENDERSTATE_DESTBLEND               20ul
#define V9X_D3DRENDERSTATE_ALPHABLENDENABLE        27ul
#define V9X_D3DRENDERSTATE_FOGENABLE              28ul
#define V9X_D3DRENDERSTATE_SPECULARENABLE         29ul
#define V9X_D3DRENDERSTATE_FOGCOLOR               34ul
#define V9X_D3DBLEND_SRCALPHA                       5ul
#define V9X_D3DBLEND_INVSRCALPHA                    6ul
#define V9X_D3DRENDERSTATE_TEXTUREHANDLE             1ul
#define V9X_D3DRENDERSTATE_TEXTUREPERSPECTIVE        4ul
#define V9X_D3DRENDERSTATE_WRAPU                     5ul
#define V9X_D3DRENDERSTATE_WRAPV                     6ul
#define V9X_D3DRENDERSTATE_TEXTUREMAG               17ul
#define V9X_D3DRENDERSTATE_TEXTUREMIN               18ul
#define V9X_D3DRENDERSTATE_TEXTUREMAPBLEND          21ul
#define V9X_D3DRENDERSTATE_BORDERCOLOR              43ul
#define V9X_D3DFILTER_NEAREST                        1ul
#define V9X_D3DFILTER_LINEAR                         2ul
#define V9X_D3DFILTER_MIPNEAREST                     3ul
#define V9X_D3DFILTER_MIPLINEAR                      4ul
#define V9X_D3DFILTER_LINEARMIPNEAREST               5ul
#define V9X_D3DFILTER_LINEARMIPLINEAR                6ul
#define V9X_D3DTBLEND_DECAL                          1ul
#define V9X_D3DTBLEND_MODULATE                       2ul
#define V9X_D3DTBLEND_COPY                           7ul
#define V9X_D3DPTEXTURECAPS_PERSPECTIVE   0x00000001ul
#define V9X_D3DPTEXTURECAPS_POW2          0x00000002ul
#define V9X_D3DPTEXTURECAPS_ALPHA         0x00000004ul
#define V9X_D3DPTEXTURECAPS_SQUAREONLY    0x00000020ul
#define V9X_D3DPTFILTERCAPS_NEAREST       0x00000001ul
#define V9X_D3DPTFILTERCAPS_LINEAR        0x00000002ul
#define V9X_D3DPTFILTERCAPS_MIPNEAREST    0x00000004ul
#define V9X_D3DPTFILTERCAPS_MIPLINEAR     0x00000008ul
#define V9X_D3DPTFILTERCAPS_LINEARMIPNEAREST 0x00000010ul
#define V9X_D3DPTFILTERCAPS_LINEARMIPLINEAR 0x00000020ul
#define V9X_D3DPTBLENDCAPS_DECAL          0x00000001ul
#define V9X_D3DPTBLENDCAPS_MODULATE       0x00000002ul
#define V9X_D3DPTBLENDCAPS_COPY           0x00000040ul
#define V9X_D3DPTADDRESSCAPS_WRAP         0x00000001ul
#define V9X_D3DPTADDRESSCAPS_CLAMP        0x00000004ul
#define V9X_D3DPT_TRIANGLELIST                     4ul
#define V9X_D3DVT_TLVERTEX                         3ul

#define V9X_D3DHAL2_CB32_SETRENDERTARGET   0x00000001ul
#define V9X_D3DHAL2_CB32_DRAWONEPRIMITIVE  0x00000004ul
#define V9X_D3DHAL2_CB32_DRAWONEINDEXEDPRIMITIVE 0x00000008ul
#define V9X_D3DHAL2_CB32_DRAWPRIMITIVES     0x00000010ul

typedef struct v9x_d3dtransformcaps {
    DWORD dwSize;
    DWORD dwCaps;
} V9X_D3DTRANSFORMCAPS;

typedef struct v9x_d3dlightingcaps {
    DWORD dwSize;
    DWORD dwCaps;
    DWORD dwLightingModel;
    DWORD dwNumLights;
} V9X_D3DLIGHTINGCAPS;

typedef struct v9x_d3dprimcaps {
    DWORD dwSize;
    DWORD dwMiscCaps;
    DWORD dwRasterCaps;
    DWORD dwZCmpCaps;
    DWORD dwSrcBlendCaps;
    DWORD dwDestBlendCaps;
    DWORD dwAlphaCmpCaps;
    DWORD dwShadeCaps;
    DWORD dwTextureCaps;
    DWORD dwTextureFilterCaps;
    DWORD dwTextureBlendCaps;
    DWORD dwTextureAddressCaps;
    DWORD dwStippleWidth;
    DWORD dwStippleHeight;
} V9X_D3DPRIMCAPS;

typedef struct v9x_d3ddevicedesc_v1 {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dcmColorModel;
    DWORD dwDevCaps;
    V9X_D3DTRANSFORMCAPS dtcTransformCaps;
    DWORD bClipping;
    V9X_D3DLIGHTINGCAPS dlcLightingCaps;
    V9X_D3DPRIMCAPS dpcLineCaps;
    V9X_D3DPRIMCAPS dpcTriCaps;
    DWORD dwDeviceRenderBitDepth;
    DWORD dwDeviceZBufferBitDepth;
    DWORD dwMaxBufferSize;
    DWORD dwMaxVertexCount;
} V9X_D3DDEVICEDESC_V1;

typedef struct v9x_d3dhal_globaldriverdata {
    DWORD dwSize;
    V9X_D3DDEVICEDESC_V1 hwCaps;
    DWORD dwNumVertices;
    DWORD dwNumClipVertices;
    DWORD dwNumTextureFormats;
    V9X_DD_VOID_PTR lpTextureFormats;
} V9X_D3DHAL_GLOBALDRIVERDATA;

typedef struct v9x_d3dhal_callbacks {
    DWORD dwSize;
    V9X_DD_CODE_PTR ContextCreate;
    V9X_DD_CODE_PTR ContextDestroy;
    V9X_DD_CODE_PTR ContextDestroyAll;
    V9X_DD_CODE_PTR SceneCapture;
    V9X_DD_CODE_PTR Execute;
    V9X_DD_CODE_PTR ExecuteClipped;
    V9X_DD_CODE_PTR RenderState;
    V9X_DD_CODE_PTR RenderPrimitive;
    DWORD dwReserved;
    V9X_DD_CODE_PTR TextureCreate;
    V9X_DD_CODE_PTR TextureDestroy;
    V9X_DD_CODE_PTR TextureSwap;
    V9X_DD_CODE_PTR TextureGetSurf;
    V9X_DD_CODE_PTR MatrixCreate;
    V9X_DD_CODE_PTR MatrixDestroy;
    V9X_DD_CODE_PTR MatrixSetData;
    V9X_DD_CODE_PTR MatrixGetData;
    V9X_DD_CODE_PTR SetViewportData;
    V9X_DD_CODE_PTR LightSet;
    V9X_DD_CODE_PTR MaterialCreate;
    V9X_DD_CODE_PTR MaterialDestroy;
    V9X_DD_CODE_PTR MaterialSetData;
    V9X_DD_CODE_PTR MaterialGetData;
    V9X_DD_CODE_PTR GetState;
    DWORD dwReserved0;
    DWORD dwReserved1;
    DWORD dwReserved2;
    DWORD dwReserved3;
    DWORD dwReserved4;
    DWORD dwReserved5;
    DWORD dwReserved6;
    DWORD dwReserved7;
    DWORD dwReserved8;
    DWORD dwReserved9;
} V9X_D3DHAL_CALLBACKS;

typedef struct v9x_d3dhal_callbacks2 {
    DWORD dwSize;
    DWORD dwFlags;
    V9X_DD_CODE_PTR SetRenderTarget;
    V9X_DD_CODE_PTR Clear;
    V9X_DD_CODE_PTR DrawOnePrimitive;
    V9X_DD_CODE_PTR DrawOneIndexedPrimitive;
    V9X_DD_CODE_PTR DrawPrimitives;
} V9X_D3DHAL_CALLBACKS2;

/*
 * 32-bit-side views of the runtime structures DDRAW passes to flat
 * callbacks. Only the fields the HAL reads are laid out; access is by
 * documented offset, so trailing fields are omitted.
 */
#ifdef __386__

typedef struct v9x_ddhal_cancreatesurfacedata {
    DWORD lpDD;
    DWORD lpDDSurfaceDesc;
    DWORD bIsDifferentPixelFormat;
    DWORD ddRVal;
    DWORD CanCreateSurface;
} V9X_DDHAL_CANCREATESURFACEDATA;

typedef struct v9x_ddhal_createsurfacedata {
    DWORD lpDD;
    DWORD lpDDSurfaceDesc;
    DWORD lplpSList;
    DWORD dwSCnt;
    DWORD ddRVal;
    DWORD CreateSurface;
} V9X_DDHAL_CREATESURFACEDATA;

typedef struct v9x_ddhal_destroysurfacedata {
    DWORD lpDD;
    DWORD lpDDSurface;
    DWORD ddRVal;
    DWORD DestroySurface;
} V9X_DDHAL_DESTROYSURFACEDATA;

typedef struct v9x_ddhal_addattachedsurfacedata {
    DWORD lpDD;
    DWORD lpDDSurface;
    DWORD lpSurfAttached;
    DWORD ddRVal;
    DWORD AddAttachedSurface;
} V9X_DDHAL_ADDATTACHEDSURFACEDATA;

/*
 * DDRAWI_DDRAWSURFACE_GBL prefix: fpVidMem at +20, lPitch at +24,
 * ddpfSurface at +40.
 *
 * The DDK notes that ddpfSurface is allocated only when the surface's format
 * differs from the primary's, so it may be read only when the owning LCL has
 * DDRAWISURF_HASPIXELFORMAT set. Without that flag the surface carries the
 * primary's format and these bytes are not part of the allocation.
 */
typedef struct v9x_dd_surface_gbl {
    DWORD dwRefCnt;
    DWORD dwGlobalFlags;
    DWORD dwBlockSizeY;
    DWORD dwBlockSizeX;
    DWORD lpDD;
    DWORD fpVidMem;
    LONG lPitch;
    WORD wHeight;
    WORD wWidth;
    DWORD dwUsageCount;
    DWORD dwReserved1;
    V9X_DDPIXELFORMAT ddpfSurface;
} V9X_DD_SURFACE_GBL;

/* DDRAWI_DDRAWSURFACE_LCL prefix: lpGbl at +4, ddsCaps at +32. */
typedef struct v9x_dd_surface_lcl {
    DWORD lpSurfMore;
    V9X_DD_SURFACE_GBL *lpGbl;
    DWORD hDDSurface;
    DWORD lpAttachList;
    DWORD lpAttachListFrom;
    DWORD dwLocalRefCnt;
    DWORD dwProcessId;
    DWORD dwFlags;
    DWORD ddsCaps;
} V9X_DD_SURFACE_LCL;

/* DDRAWI_DDRAWSURFACE_INT prefix. D3D HAL callbacks receive this wrapper. */
typedef struct v9x_dd_surface_int {
    DWORD lpVtbl;
    V9X_DD_SURFACE_LCL *lpLcl;
} V9X_DD_SURFACE_INT;

typedef struct v9x_ddhal_flipdata {
    DWORD lpDD;
    V9X_DD_SURFACE_LCL *lpSurfCurr;
    V9X_DD_SURFACE_LCL *lpSurfTarg;
    DWORD dwFlags;
    DWORD ddRVal;
    DWORD Flip;
} V9X_DDHAL_FLIPDATA;

typedef struct v9x_ddhal_getflipstatusdata {
    DWORD lpDD;
    V9X_DD_SURFACE_LCL *lpDDSurface;
    DWORD dwFlags;
    DWORD ddRVal;
    DWORD GetFlipStatus;
} V9X_DDHAL_GETFLIPSTATUSDATA;

typedef struct v9x_ddhal_lockdata {
    DWORD lpDD;
    V9X_DD_SURFACE_LCL *lpDDSurface;
    DWORD bHasRect;
    LONG rArea[4];
    DWORD lpSurfData;
    DWORD ddRVal;
    DWORD Lock;
    DWORD dwFlags;
} V9X_DDHAL_LOCKDATA;

typedef struct v9x_ddhal_unlockdata {
    DWORD lpDD;
    V9X_DD_SURFACE_LCL *lpDDSurface;
    DWORD ddRVal;
    DWORD Unlock;
} V9X_DDHAL_UNLOCKDATA;

typedef struct v9x_ddhal_bltdata {
    DWORD lpDD;
    V9X_DD_SURFACE_LCL *lpDDDestSurface;
    LONG rDest[4];
    V9X_DD_SURFACE_LCL *lpDDSrcSurface;
    LONG rSrc[4];
    DWORD dwFlags;
    DWORD dwROPFlags;
    V9X_DDBLTFX bltFX;
    DWORD ddRVal;
    DWORD Blt;
} V9X_DDHAL_BLTDATA;

typedef struct v9x_ddhal_getbltstatusdata {
    DWORD lpDD;
    V9X_DD_SURFACE_LCL *lpDDSurface;
    DWORD dwFlags;
    DWORD ddRVal;
    DWORD GetBltStatus;
} V9X_DDHAL_GETBLTSTATUSDATA;

typedef struct v9x_ddhal_waitforverticalblankdata {
    DWORD lpDD;
    DWORD dwFlags;
    DWORD bIsInVB;
    DWORD hEvent;
    DWORD ddRVal;
    DWORD WaitForVerticalBlank;
} V9X_DDHAL_WAITFORVERTICALBLANKDATA;

typedef struct v9x_d3dhal_contextcreatedata {
    void *lpDDGbl;
    void *lpDDS;
    void *lpDDSZ;
    DWORD dwPID;
    DWORD dwhContext;
    DWORD ddrval;
} V9X_D3DHAL_CONTEXTCREATEDATA;

typedef struct v9x_d3dhal_contextdestroydata {
    DWORD dwhContext;
    DWORD ddrval;
} V9X_D3DHAL_CONTEXTDESTROYDATA;

typedef struct v9x_d3dhal_contextdestroyalldata {
    DWORD dwPID;
    DWORD ddrval;
} V9X_D3DHAL_CONTEXTDESTROYALLDATA;

typedef struct v9x_d3dhal_texturecreatedata {
    DWORD dwhContext;
    void *lpDDS;
    DWORD dwHandle;
    DWORD ddrval;
} V9X_D3DHAL_TEXTURECREATEDATA;

typedef struct v9x_d3dhal_texturedestroydata {
    DWORD dwhContext;
    DWORD dwHandle;
    DWORD ddrval;
} V9X_D3DHAL_TEXTUREDESTROYDATA;

typedef struct v9x_d3dhal_textureswapdata {
    DWORD dwhContext;
    DWORD dwHandle1;
    DWORD dwHandle2;
    DWORD ddrval;
} V9X_D3DHAL_TEXTURESWAPDATA;

typedef struct v9x_d3dhal_texturegetsurfdata {
    DWORD dwhContext;
    DWORD lpDDS;
    DWORD dwHandle;
    DWORD ddrval;
} V9X_D3DHAL_TEXTUREGETSURFDATA;

typedef struct v9x_d3dhal_renderstatedata {
    DWORD dwhContext;
    DWORD dwOffset;
    DWORD dwCount;
    void *lpExeBuf;
    DWORD ddrval;
} V9X_D3DHAL_RENDERSTATEDATA;

typedef struct v9x_d3dstate {
    DWORD type;
    DWORD argument;
} V9X_D3DSTATE;

typedef struct v9x_d3dinstruction {
    BYTE bOpcode;
    BYTE bSize;
    WORD wCount;
} V9X_D3DINSTRUCTION;

typedef struct v9x_d3dstatus {
    DWORD dwFlags;
    DWORD dwStatus;
    LONG drExtent[4];
} V9X_D3DSTATUS;

typedef struct v9x_d3di_executedata {
    DWORD dwSize;
    DWORD dwHandle;
    DWORD dwVertexOffset;
    DWORD dwVertexCount;
    DWORD dwInstructionOffset;
    DWORD dwInstructionLength;
    DWORD dwHVertexOffset;
    V9X_D3DSTATUS dsStatus;
} V9X_D3DI_EXECUTEDATA;

typedef struct v9x_d3dhal_executedata {
    DWORD dwhContext;
    DWORD dwOffset;
    DWORD dwFlags;
    DWORD dwStatus;
    V9X_D3DI_EXECUTEDATA deExData;
    void *lpExeBuf;
    void *lpTLBuf;
    V9X_D3DINSTRUCTION diInstruction;
    DWORD ddrval;
} V9X_D3DHAL_EXECUTEDATA;

typedef struct v9x_d3dhal_executeclippeddata {
    DWORD dwhContext;
    DWORD dwOffset;
    DWORD dwFlags;
    DWORD dwStatus;
    V9X_D3DI_EXECUTEDATA deExData;
    void *lpExeBuf;
    void *lpTLBuf;
    void *lpHBuf;
    V9X_D3DINSTRUCTION diInstruction;
    DWORD ddrval;
} V9X_D3DHAL_EXECUTECLIPPEDDATA;

typedef struct v9x_d3dhal_renderprimitivedata {
    DWORD dwhContext;
    DWORD dwOffset;
    DWORD dwStatus;
    void *lpExeBuf;
    DWORD dwTLOffset;
    void *lpTLBuf;
    V9X_D3DINSTRUCTION diInstruction;
    DWORD ddrval;
} V9X_D3DHAL_RENDERPRIMITIVEDATA;

typedef struct v9x_d3dtriangle {
    WORD v1;
    WORD v2;
    WORD v3;
    WORD wFlags;
} V9X_D3DTRIANGLE;

typedef struct v9x_ddhal_getdriverinfodata {
    DWORD dwSize;
    DWORD dwFlags;
    BYTE guidInfo[16];
    DWORD dwExpectedSize;
    void *lpvData;
    DWORD dwActualSize;
    DWORD ddRVal;
    DWORD dwContext;
} V9X_DDHAL_GETDRIVERINFODATA;

typedef struct v9x_d3dhal_setrendertargetdata {
    DWORD dwhContext;
    void *lpDDS;
    void *lpDDSZ;
    DWORD ddrval;
} V9X_D3DHAL_SETRENDERTARGETDATA;

typedef struct v9x_d3dhal_drawoneprimitivedata {
    DWORD dwhContext;
    DWORD dwFlags;
    DWORD PrimitiveType;
    DWORD VertexType;
    void *lpvVertices;
    DWORD dwNumVertices;
    DWORD dwReserved;
    DWORD ddrval;
} V9X_D3DHAL_DRAWONEPRIMITIVEDATA;

typedef struct v9x_d3dhal_drawprimitivesdata {
    DWORD dwhContext;
    DWORD dwFlags;
    void *lpvData;
    DWORD dwReserved;
    DWORD ddrval;
} V9X_D3DHAL_DRAWPRIMITIVESDATA;

typedef struct v9x_d3dhal_drawprimcounts {
    WORD wNumStateChanges;
    WORD wPrimitiveType;
    WORD wVertexType;
    WORD wNumVertices;
} V9X_D3DHAL_DRAWPRIMCOUNTS;

typedef struct v9x_d3dtlvertex {
    float sx;
    float sy;
    float sz;
    float rhw;
    DWORD color;
    DWORD specular;
    float tu;
    float tv;
} V9X_D3DTLVERTEX;

#else /* 16-bit */

/* DDHAL_DESTROYDRIVERDATA, consumed by the 16-bit DestroyDriver callback. */
typedef struct v9x_ddhal_destroydriverdata {
    V9X_DD_VOID_PTR lpDD;
    DWORD ddRVal;
    V9X_DD_CODE_PTR DestroyDriver;
} V9X_DDHAL_DESTROYDRIVERDATA;

#endif /* __386__ */

/*
 * Cross-bitness shared block. The 16-bit driver allocates it with DPMI in
 * globally visible memory; its linear address is the dwContext handed to
 * V9XHAL.DLL's DriverInit. The 32-bit side owns all content except the
 * framebuffer descriptor, which the 16-bit side refreshes on every enable.
 */
/* Bumped for the generalized engine descriptor, and again when the mode table
 * became variable-length. A mixed old/new DRV+DLL pair fails safe: DriverInit
 * rejects on the dwSize/abi mismatch and leaves a driverinit-pending trace
 * rather than running against the wrong layout. */
#define V9X_DD_SHARED_ABI   2026081901ul
/*
 * Capacity of modes[], not the number of modes in use - that is mode_count,
 * which the 16-bit side sets from the family table. The two were the same
 * number while the table was a fixed seven rows duplicated on both sides.
 *
 * 32 is a cap on what DirectDraw is told about rather than on what the display
 * driver offers: once modes are discovered from the video BIOS a card can list
 * more than this, and dd16.c takes a subset. At 36 bytes per entry this costs
 * 1152 bytes of the 4096-byte DPMI block the whole structure has to fit in
 * (V9X_DD_SHARED_BYTES in src\display16\runtime.asm), which measured at 3096
 * bytes total - so raising it further is possible but not free.
 */
#define V9X_DD_MODE_COUNT           32u

/* fb.flags */
#define V9X_DD_FB_VALID          0x00000001ul

typedef struct v9x_dd_framebuffer {
    DWORD linear_base;      /* flat address of the mapped LFB           */
    DWORD physical_base;    /* PCI aperture physical address            */
    DWORD vram_bytes;       /* mapped aperture size (4 MiB)             */
    DWORD visible_bytes;    /* pitch * height of the active mode        */
    DWORD pitch;
    DWORD width;
    DWORD height;
    DWORD bits_per_pixel;
    DWORD flags;            /* V9X_DD_FB_*                              */
    /* Diagnostics for the DIBENG fault investigation. The 16-bit selector
     * that addresses the framebuffer is freed by Disable and reallocated by
     * the next Enable, so a cached copy held elsewhere would dangle; the
     * counts say whether a real Disable happened at all. See
     * docs/issues/2026-08-14-hellbender-dibeng-gpf.md. */
    DWORD screen_selector;
    DWORD enable_count;
    DWORD disable_count;
} V9X_DD_FRAMEBUFFER;

/* The active S3 mapping spans the full 64-MiB linear aperture. Only the
 * first vram_bytes are allocatable VRAM; the register window is addressed
 * through control_linear_base and must never be exposed as a heap. */
#define V9X_DD_ENGINE_VALID          0x00000001ul
/*
 * 0x00000002 and 0x00000004 were V9X_DD_ENGINE_S3_VIRGE_DX and
 * V9X_DD_ENGINE_S3_TRIO64, one identity bit per chip. Retired 2026-08-16:
 * chip identity is engine_type below, so adding a chip is a new enum value
 * rather than a new bit and a new branch at every reader. Left unassigned
 * rather than immediately reused, so a stale diagnostic reading this field
 * reports nothing rather than reporting a wrong chip.
 *
 * This field is now runtime state only.
 */
/* Distinct from the identity bits that used to live here. It once aliased
 * the Trio64 bit, so validating the ViRGE engine status made
 * v9x_trio_engine_ready() true on a ViRGE and would have routed its blits
 * through the Trio64 port-I/O command sequence. */
#define V9X_DD_ENGINE_STATUS_VALIDATED 0x00000008ul

/*
 * Engine identity and capability, as data rather than as flag bits.
 *
 * engine_type is the sole statement of which chip this is; a new chip is a new
 * value here and a caps mask, not another flag bit. engine_caps says what that
 * engine will do, so a chip can carry an engine with only part of its family's
 * capability set.
 */

typedef struct v9x_dd_engine {
    /* Field offsets are unchanged since ABI 2026081601. Phase 7 changed only
     * which of them the 32-bit HAL reads, not where any of them sit. */
    DWORD control_linear_base;
    DWORD mapped_aperture_bytes;
    DWORD flags;
    DWORD fifo_timeouts;
    DWORD idle_timeouts;
    DWORD reset_count;
    /* Appended in ABI 2026081601. */
    DWORD engine_type;
    DWORD engine_caps;
    /* Port-I/O base and CRTC index port for engines addressed that way; the
     * Trio64's 8514/A command set needs both, the ViRGE's MMIO does not. */
    DWORD io_base;
    DWORD crtc_index_port;
    /* Engine fault injector, armed by V9X_DDFAULTINJECT. Non-zero means the
     * next N bounded waits report a timeout instead of completing: each one
     * decrements this, counts itself in fifo_timeouts/idle_timeouts, flushes
     * the fault trace and runs the engine's recovery, exactly as a real
     * timeout would. It exists because the timeout and reset paths are
     * otherwise unreachable on healthy hardware, so any gate that asserts
     * "recovery still works" would pass vacuously. Zero (the default, and
     * what every normal boot leaves it at) disables it entirely; it occupies
     * what was reserved0 through ABI 2026081601, so the layout is unchanged.
     */
    DWORD fault_inject;
    DWORD reserved1;
} V9X_DD_ENGINE;

typedef struct v9x_dd_cb32 {
    DWORD Flip;             /* flat function pointers filled by the DLL */
    DWORD GetFlipStatus;
    DWORD Lock;
    DWORD Unlock;
    DWORD WaitForVerticalBlank;
    DWORD flags;            /* extra DDHALINFO.dwFlags bits             */
} V9X_DD_CB32;

typedef struct v9x_d3d_diagnostics {
    DWORD context_creates;
    DWORD context_destroys;
    DWORD context_destroy_alls;
    DWORD context_rejects;
    DWORD render_state_calls;
    DWORD render_primitive_calls;
    DWORD execute_calls;
    DWORD texture_creates;
    DWORD texture_destroys;
    DWORD texture_swaps;
    DWORD texture_get_surfs;
} V9X_D3D_DIAGNOSTICS;

/*
 * Bounded HAL callback trace (Hellbender plan H1). Both sides append
 * fixed-size records to a ring inside the shared block, so the last
 * callbacks before a fault survive the faulting process and can be read
 * back with the V9X_DDGETTRACE escape. All writers are allocation-free.
 */
#define V9X_DD_TRACE_RING_COUNT     56u
#define V9X_DD_TRACE_ID_COUNT       50u
#define V9X_DD_TRACE_EXIT_FLAG   0x8000u

/* Trace event ids. Gaps group the sources: 16-bit escapes, DirectDraw
 * HAL callbacks, Direct3D HAL callbacks. */
#define V9X_TRACE_DRIVERINIT           1u
#define V9X_TRACE_DD16_CREATEOBJECT    2u
#define V9X_TRACE_DD16_DESTROYDRIVER   3u
#define V9X_TRACE_DD16_NEWCALLBACKFNS  4u
#define V9X_TRACE_DD16_GET32BITNAME    5u
#define V9X_TRACE_FLIP                10u
#define V9X_TRACE_GETFLIPSTATUS       11u
#define V9X_TRACE_LOCK                12u
#define V9X_TRACE_UNLOCK              13u
#define V9X_TRACE_BLT                 14u
#define V9X_TRACE_GETBLTSTATUS        15u
#define V9X_TRACE_WAITFORVBLANK       16u
#define V9X_TRACE_SETEXCLUSIVE        17u
#define V9X_TRACE_FLIPTOGDI           18u
#define V9X_TRACE_GETDRIVERINFO       19u
#define V9X_TRACE_CANCREATESURFACE    20u
#define V9X_TRACE_CREATESURFACE       21u
#define V9X_TRACE_DESTROYSURFACE      22u
#define V9X_TRACE_ADDATTACHEDSURFACE  23u
/* Counted only when the driver itself executed the blit (the Blt callback
 * returned DDHAL_DRIVER_HANDLED). Separating this from V9X_TRACE_BLT is what
 * distinguishes engine execution from a HEL fallback, which produces the same
 * pixels and the same ddRVal. */
#define V9X_TRACE_BLT_ENGINE          24u
#define V9X_TRACE_D3D_CTXCREATE       30u
#define V9X_TRACE_D3D_CTXDESTROY      31u
#define V9X_TRACE_D3D_CTXDESTROYALL   32u
#define V9X_TRACE_D3D_RENDERSTATE     33u
#define V9X_TRACE_D3D_RENDERPRIM      34u
#define V9X_TRACE_D3D_SETRENDERTARGET 35u
#define V9X_TRACE_D3D_DRAWONEPRIM     36u
#define V9X_TRACE_D3D_DRAWPRIMS       37u
#define V9X_TRACE_D3D_DRAWONEINDEXED  38u
#define V9X_TRACE_D3D_TARGET_LAYOUT   39u
#define V9X_TRACE_D3D_EXECUTE         40u
#define V9X_TRACE_EXEBUF_CANCREATE    41u
#define V9X_TRACE_EXEBUF_CREATE       42u
#define V9X_TRACE_EXEBUF_DESTROY      43u
#define V9X_TRACE_EXEBUF_LOCK         44u
#define V9X_TRACE_EXEBUF_UNLOCK       45u
#define V9X_TRACE_D3D_TEXTURECREATE   46u
#define V9X_TRACE_D3D_TEXTUREDESTROY  47u
#define V9X_TRACE_D3D_TEXTURESWAP     48u
#define V9X_TRACE_D3D_TEXTUREGETSURF  49u
#define V9X_TRACE_D3D_PRIMREJECT      50u

typedef struct v9x_dd_trace_entry {
    WORD id;            /* trace id, V9X_DD_TRACE_EXIT_FLAG on exit    */
    WORD seq;           /* low word of the event sequence              */
    DWORD detail;       /* enter: callback argument; exit: result code */
} V9X_DD_TRACE_ENTRY;

typedef struct v9x_dd_trace {
    DWORD seq;          /* total events recorded                       */
    DWORD head;         /* next ring slot                              */
    DWORD last_enter_id;
    DWORD last_enter_detail;
    DWORD last_exit_id;
    DWORD last_exit_result;
    WORD counters[V9X_DD_TRACE_ID_COUNT]; /* per-id enter counts       */
    V9X_DD_TRACE_ENTRY ring[V9X_DD_TRACE_RING_COUNT];
} V9X_DD_TRACE;

/* V9X_DDGETTRACE output. Field-for-field copy of the live shared state;
 * dwSize/abi let the reader reject a mismatched driver build. */
typedef struct v9x_dd_trace_snapshot {
    DWORD dwSize;
    DWORD abi;
    DWORD driver_init_done;
    V9X_DD_FRAMEBUFFER fb;
    V9X_DD_ENGINE engine;
    V9X_D3D_DIAGNOSTICS d3d;
    V9X_DD_TRACE trace;
} V9X_DD_TRACE_SNAPSHOT;

/*
 * V9X_GDIGETSTATS output: everything the /accel harness needs to decide
 * whether an accelerated primitive actually ran.
 *
 * The `advertised` and `enabled` pair is the whole point. A build that
 * compiles a primitive advertises it; a build (or a SYSTEM.INI key) that turns
 * it on enables it. The harness fails when a primitive is advertised and
 * enabled and its counter is nonetheless zero - which is the anti-vacuous-pass
 * check, and the check the ati package would have needed
 * (docs\issues\2026-08-26-ati-package-cannot-enable.md).
 *
 * decline_* is not decoration: when the zero-counter check fires, the decline
 * tallies are what say which gate ate every operation.
 */
#define V9X_GDI_PRIM_FILL           0x00000001ul
#define V9X_GDI_PRIM_COPY           0x00000002ul
#define V9X_GDI_PRIM_OVERLAP        0x00000004ul

typedef struct v9x_gdi_stats {
    DWORD dwSize;
    DWORD abi;
    /* Primitives this binary contains code for. */
    DWORD advertised;
    /* Of those, the ones the compile-time defaults and SYSTEM.INI left on. */
    DWORD enabled;
    DWORD engine_type;          /* V9X_DD_ENGINE_TYPE_*, 0 = no engine  */
    DWORD threshold;            /* minimum accelerated pixel count      */
    DWORD calls;                /* BitBlt entries                       */
    DWORD declines;             /* forwarded to DIB_BitBlt              */
    DWORD fills;                /* solid fills issued to the engine     */
    DWORD copies;               /* screen-to-screen copies issued       */
    DWORD idle_timeouts;
    DWORD fifo_timeouts;
    DWORD resets;
    DWORD poisoned;             /* 1 once the session-long latch is set */
    DWORD fault_inject;         /* armed injections still unconsumed    */
    DWORD drains;               /* BeginAccess slow-path engine drains   */
    /* Decline tallies, in gate order. */
    DWORD decline_disabled;
    DWORD decline_poisoned;
    DWORD decline_not_screen;
    DWORD decline_busy;
    DWORD decline_palette_xlat;
    DWORD decline_depth;
    DWORD decline_rop;
    DWORD decline_geometry;
    /* Overlapping same-surface copies, declined until build 003 turns overlap
     * on. Separate from decline_geometry because it is the one decline a build
     * can be asked to prove it is still making: at 002 the harness issues
     * overlapping copies deliberately and checks that this advanced. */
    DWORD decline_overlap;
    DWORD decline_threshold;
    DWORD decline_engine;
    /*
     * The last operation the dispatcher accepted, for diagnosing a wrong-pixel
     * failure without a second guest round trip.
     *
     * Added because the first GdiAccelFill=1 run needed exactly this and did
     * not have it: the harness could say "the engine painted white where the
     * DIB Engine painted yellow" but not what colour the driver had decided on,
     * so it could not separate a misread brush from a misclassified ROP from a
     * register the engine wants in a different format.
     */
    DWORD last_rop256;
    DWORD last_color;
    DWORD last_brush_flags;
    DWORD last_brush_bpp;
    DWORD last_brush_style;
    DWORD last_bpp;
} V9X_GDI_STATS;

typedef struct v9x_dd_shared {
    DWORD dwSize;           /* sizeof(V9X_DD_SHARED)                    */
    DWORD abi;              /* V9X_DD_SHARED_ABI                        */
    DWORD driver_init_done; /* set by DriverInit after content build    */
    V9X_DD_FRAMEBUFFER fb;
    V9X_DD_ENGINE engine;
    V9X_DD_CB32 cb32;
    DWORD hInstance;        /* 32-bit DLL module handle                 */
    V9X_DDHALINFO info;
    V9X_DDHAL_DDCALLBACKS dd_callbacks;
    V9X_DDHAL_DDSURFACECALLBACKS surface_callbacks;
    V9X_DDHAL_DDPALETTECALLBACKS palette_callbacks;
    V9X_DDHAL_DDEXEBUFCALLBACKS execute_buffer_callbacks;
    V9X_D3DHAL_GLOBALDRIVERDATA d3d_global;
    V9X_DDSURFACEDESC texture_formats[2];
    V9X_D3DHAL_CALLBACKS d3d_callbacks;
    V9X_D3D_DIAGNOSTICS d3d_diagnostics;
    V9X_VIDMEM heaps[1];
    /* How many of modes[] the 16-bit side filled in. Written before DriverInit
     * runs, which validates it and publishes it as info.dwNumModes. */
    DWORD mode_count;
    V9X_DDHALMODEINFO modes[V9X_DD_MODE_COUNT];
    V9X_DD_TRACE trace;
} V9X_DD_SHARED;

#pragma pack(pop)

/* One-byte-per-check size guards; both compilers must agree. */
typedef char v9x_dd_assert_pixelformat[
    sizeof(V9X_DDPIXELFORMAT) == 32 ? 1 : -1];
typedef char v9x_dd_assert_surfacedesc[
    sizeof(V9X_DDSURFACEDESC) == 108 ? 1 : -1];
typedef char v9x_dd_assert_vidmem[sizeof(V9X_VIDMEM) == 24 ? 1 : -1];
typedef char v9x_dd_assert_bltfx[sizeof(V9X_DDBLTFX) == 100 ? 1 : -1];
typedef char v9x_dd_assert_vidmeminfo[sizeof(V9X_VIDMEMINFO) == 80 ? 1 : -1];
typedef char v9x_dd_assert_modeinfo[
    sizeof(V9X_DDHALMODEINFO) == 36 ? 1 : -1];
typedef char v9x_dd_assert_corecaps[
    sizeof(V9X_DDCORECAPS) == 316 ? 1 : -1];
typedef char v9x_dd_assert_halinfo[
    sizeof(V9X_DDHALINFO) == V9X_DDHALINFO_SIZE ? 1 : -1];
typedef char v9x_dd_assert_d3dprimcaps[
    sizeof(V9X_D3DPRIMCAPS) == 56 ? 1 : -1];
typedef char v9x_dd_assert_d3ddevdesc[
    sizeof(V9X_D3DDEVICEDESC_V1) == 172 ? 1 : -1];
typedef char v9x_dd_assert_d3dglobal[
    sizeof(V9X_D3DHAL_GLOBALDRIVERDATA) == 192 ? 1 : -1];
typedef char v9x_dd_assert_d3dcallbacks[
    sizeof(V9X_D3DHAL_CALLBACKS) == 140 ? 1 : -1];
typedef char v9x_dd_assert_exebufcallbacks[
    sizeof(V9X_DDHAL_DDEXEBUFCALLBACKS) == 28 ? 1 : -1];
#ifdef __386__
typedef char v9x_dd_assert_d3dstatus[
    sizeof(V9X_D3DSTATUS) == 24 ? 1 : -1];
typedef char v9x_dd_assert_d3dexecutedata[
    sizeof(V9X_D3DI_EXECUTEDATA) == 52 ? 1 : -1];
typedef char v9x_dd_assert_d3dhalexecute[
    sizeof(V9X_D3DHAL_EXECUTEDATA) == 84 ? 1 : -1];
typedef char v9x_dd_assert_d3dhalexecuteclipped[
    sizeof(V9X_D3DHAL_EXECUTECLIPPEDDATA) == 88 ? 1 : -1];
typedef char v9x_dd_assert_d3dhaltexturecreate[
    sizeof(V9X_D3DHAL_TEXTURECREATEDATA) == 16 ? 1 : -1];
typedef char v9x_dd_assert_d3dhaltexturedestroy[
    sizeof(V9X_D3DHAL_TEXTUREDESTROYDATA) == 12 ? 1 : -1];
typedef char v9x_dd_assert_d3dhaltextureswap[
    sizeof(V9X_D3DHAL_TEXTURESWAPDATA) == 16 ? 1 : -1];
typedef char v9x_dd_assert_d3dhaltexturegetsurf[
    sizeof(V9X_D3DHAL_TEXTUREGETSURFDATA) == 16 ? 1 : -1];
#endif
typedef char v9x_dd_assert_dcicmd[sizeof(V9X_DCICMD) == 20 ? 1 : -1];
typedef char v9x_dd_assert_dd32data[
    sizeof(V9X_DD32BITDRIVERDATA) == 328 ? 1 : -1];
#ifdef __386__
typedef char v9x_dd_assert_surface_gbl[
    sizeof(V9X_DD_SURFACE_GBL) == 72 ? 1 : -1];
typedef char v9x_dd_assert_bltdata[
    sizeof(V9X_DDHAL_BLTDATA) == 160 ? 1 : -1];
#endif
typedef char v9x_dd_assert_trace_entry[
    sizeof(V9X_DD_TRACE_ENTRY) == 8 ? 1 : -1];
/* The GDI stats block crosses the 16-bit/32-bit boundary through ExtEscape,
 * so both compilers have to lay it out the same way. */
typedef char v9x_dd_assert_gdi_stats[
    sizeof(V9X_GDI_STATS) == 132 ? 1 : -1];
typedef char v9x_dd_assert_trace[
    sizeof(V9X_DD_TRACE) == 572 ? 1 : -1];
/* Must match V9X_DD_SHARED_BYTES in src/display16/runtime.asm, which is the
 * size the 16-bit side DPMI-allocates and the limit it sets on the selector. */
typedef char v9x_dd_assert_shared_fits_dpmi_block[
    sizeof(V9X_DD_SHARED) <= 4096 ? 1 : -1];

#endif /* VELOCITY9X_WIN9X_DDRAW_ABI_H */
