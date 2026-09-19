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
/* V9X_GDITEXTDUMP copies a V9X_GDI_TEXT_DUMP: the arguments of the last
 * string-bitmap callback and the first bytes of its bitmap, so the shape of
 * what the DIB Engine hands the driver can be read on the host instead of
 * inferred from a screenshot. */
#define V9X_GDITEXTDUMP          0x56395444ul /* 'V9TD' */
/* V9X_GDITEXTPROBE arms ordinal 14 to accept dwParam1 screen text calls
 * through the engine path while GdiAccelText is off, so one known string can
 * be the first accelerated string a machine draws - and if it hangs, the one
 * with known arguments. Requires the engine to be live (another primitive
 * enabled) and the chip to be one the text path serves. */
#define V9X_GDITEXTPROBE         0x56395450ul /* 'V9TP' */

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
/* DDRAW.H:1745. Says the Blt callback serves DDBLT_DEPTHFILL; without it the
 * runtime clears a Z buffer by locking it and writing every word from the
 * CPU, once per frame. */
#define V9X_DDCAPS_BLTDEPTHFILL      0x10000000ul
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
#define V9X_DDHAL_SURFCB32_SETCOLORKEY   0x00000040ul
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
/* DDRAW.H:2799. Not 0x00002000, which is what its position in the flag list
 * suggests and what guessing it produces: the DDBLT_ flags are not densely
 * packed and this one sits well above DDBLT_ROP. Fill the destination
 * rectangle with bltFX.dwFillDepth. */
#define V9X_DDBLT_DEPTHFILL          0x02000000ul
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
    /* A union in DDRAW.H:223-229: dwFillColor, dwFillDepth, dwFillPixel and
     * lpDDSPattern share this DWORD. Named for the colour fill because that
     * is the older reader; the depth fill takes the same bytes and means
     * dwFillDepth by them. Adding a second member here would move
     * ddckDestColorkey and break the ABI. */
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
/* D3DDEVCAPS_TEXTURESYSTEMMEMORY, DDK D3DCAPS.H. The software engine
 * publishes it beside the video-memory cap when its own setting allows a
 * system-memory texture; no hardware path here can. */
#define V9X_D3DDEVCAPS_TEXTURESYSTEMMEMORY 0x00000100ul
#define V9X_D3DDEVCAPS_TEXTUREVIDEOMEMORY 0x00000200ul
#define V9X_D3DDD_LINECAPS               0x00000020ul
#define V9X_D3DPMISCCAPS_CULLNONE         0x00000010ul
#define V9X_D3DPMISCCAPS_CULLCW           0x00000020ul
#define V9X_D3DPMISCCAPS_CULLCCW          0x00000040ul
#define V9X_D3DPRASTERCAPS_DITHER         0x00000001ul
#define V9X_D3DPBLENDCAPS_ZERO            0x00000001ul
/* D3DCAPS.H: the driver honours a source colour key on textures. */
#define V9X_D3DPTEXTURECAPS_TRANSPARENCY  0x00000008ul
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
/* D3DCAPS.H: the source is scaled by the destination colour, per channel.
 * The software engine publishes this one; the S3D unit cannot express it. */
#define V9X_D3DPBLENDCAPS_DESTCOLOR       0x00000100ul
#define V9X_D3DRENDERSTATE_SRCBLEND                19ul
#define V9X_D3DRENDERSTATE_DESTBLEND               20ul
#define V9X_D3DRENDERSTATE_ALPHABLENDENABLE        27ul
#define V9X_D3DRENDERSTATE_COLORKEYENABLE          41ul
#define V9X_D3DRENDERSTATE_FOGENABLE              28ul
#define V9X_D3DRENDERSTATE_SPECULARENABLE         29ul
#define V9X_D3DRENDERSTATE_FOGCOLOR               34ul
#define V9X_D3DBLEND_ZERO                           1ul
#define V9X_D3DBLEND_ONE                            2ul
#define V9X_D3DBLEND_SRCALPHA                       5ul
#define V9X_D3DBLEND_INVSRCALPHA                    6ul
#define V9X_D3DBLEND_DESTCOLOR                      9ul
#define V9X_D3DRENDERSTATE_TEXTUREHANDLE             1ul
#define V9X_D3DRENDERSTATE_TEXTUREPERSPECTIVE        4ul
#define V9X_D3DRENDERSTATE_WRAPU                     5ul
#define V9X_D3DRENDERSTATE_WRAPV                     6ul
#define V9X_D3DRENDERSTATE_TEXTUREMAG               17ul
/* D3DRENDERSTATE_SHADEMODE (9) and its D3DSHADEMODE values, d3dtypes.h. */
#define V9X_D3DRENDERSTATE_SHADEMODE                9ul
#define V9X_D3DSHADE_FLAT                           1ul
#define V9X_D3DSHADE_GOURAUD                        2ul
#define V9X_D3DSHADE_PHONG                          3ul
#define V9X_D3DRENDERSTATE_TEXTUREMIN               18ul
#define V9X_D3DRENDERSTATE_TEXTUREMAPBLEND          21ul
#define V9X_D3DRENDERSTATE_TEXTUREADDRESS            3ul
#define V9X_D3DRENDERSTATE_TEXTUREADDRESSU          44ul
#define V9X_D3DRENDERSTATE_TEXTUREADDRESSV          45ul
#define V9X_D3DTADDRESS_WRAP                         1ul
#define V9X_D3DTADDRESS_MIRROR                       2ul
#define V9X_D3DTADDRESS_CLAMP                        3ul
#define V9X_D3DRENDERSTATE_BORDERCOLOR              43ul
/*
 * A private render state, and an instrument rather than a feature.
 *
 * The S3D command word carries the alpha control in bits 19:18, and the
 * engine sets one of two encodings from the blend states
 * (v9x_d3d_virge_alpha_bits). On the Trio3D/2X neither encoding produces a
 * blend: the source's colour has no effect on the result and both encodings
 * give the same wrong answer, byte for byte
 * (docs\decisions\2026-09-04-what-the-trio3d-blend-does-with-its-operands.md).
 * What the field means on that part is therefore an open question that only
 * the card can answer, and answering it means putting all four encodings
 * through the same draw.
 *
 * This state does that without a driver build per encoding: the argument
 * V9X_D3D_ALPHAFORCE_MAGIC | n forces the two bits to n's encoding, and
 * anything else - every value any application will ever write - leaves the
 * engine's own choice alone. It applies only where the engine would have
 * blended anyway, so it cannot turn on a blend that would otherwise be
 * refused.
 *
 * The state number is a real one. A number outside Direct3D's own range does
 * not work: IDirect3DDevice2::SetRenderState validates the type before the
 * HAL is ever asked, so 0x56394146 was rejected by the runtime and the
 * instrument's first four curves came back as one flat white
 * (measured on the emulated ViRGE/DX, 2026-09-04). D3DRENDERSTATE_STIPPLEPATTERN31
 * is a legal state the runtime passes through, this driver publishes no
 * stipple capability and ignores it, and the magic in the argument is what
 * keeps an application that does set a stipple pattern from tripping the
 * instrument by accident.
 *
 * The probe carries the same two numbers in its own definitions; the pairs are
 * asserted equal by scripts\check-tree.ps1.
 */
#define V9X_D3DRENDERSTATE_V9X_ALPHAFORCE   0x0000005ful
#define V9X_D3D_ALPHAFORCE_MAGIC            0x56390000ul
#define V9X_D3D_ALPHAFORCE_MASK             0xffff0000ul
#define V9X_D3D_ALPHAFORCE_ENGINE                    0ul
#define V9X_D3D_ALPHAFORCE_NONE                      1ul
#define V9X_D3D_ALPHAFORCE_SOURCE                    2ul
#define V9X_D3D_ALPHAFORCE_ENABLE                    3ul
#define V9X_D3D_ALPHAFORCE_BOTH                      4ul
/* Depth-buffer state. Values from the Windows 98 DDK's own ViRGE driver,
 * C:\98DDK\src\display\mini\s3v\D3DSTATE.C:73/95/130. */
#define V9X_D3DRENDERSTATE_ZENABLE                   7ul
#define V9X_D3DRENDERSTATE_ZWRITEENABLE             14ul
#define V9X_D3DRENDERSTATE_ZFUNC                    23ul
/* D3DCMP_*, the comparison functions D3DRENDERSTATE_ZFUNC selects between.
 * The chip's own encoding is a different order entirely; the engine maps
 * between them. */
#define V9X_D3DCMP_NEVER                             1ul
#define V9X_D3DCMP_LESS                              2ul
#define V9X_D3DCMP_EQUAL                             3ul
#define V9X_D3DCMP_LESSEQUAL                         4ul
#define V9X_D3DCMP_GREATER                           5ul
#define V9X_D3DCMP_NOTEQUAL                          6ul
#define V9X_D3DCMP_GREATEREQUAL                      7ul
#define V9X_D3DCMP_ALWAYS                            8ul
#define V9X_D3DFILTER_NEAREST                        1ul
#define V9X_D3DFILTER_LINEAR                         2ul
#define V9X_D3DFILTER_MIPNEAREST                     3ul
#define V9X_D3DFILTER_MIPLINEAR                      4ul
#define V9X_D3DFILTER_LINEARMIPNEAREST               5ul
#define V9X_D3DFILTER_LINEARMIPLINEAR                6ul
#define V9X_D3DTBLEND_DECAL                          1ul
#define V9X_D3DTBLEND_MODULATE                       2ul
#define V9X_D3DTBLEND_COPY                           7ul
#define V9X_D3DTBLEND_DECALALPHA                     3ul
#define V9X_D3DTBLEND_MODULATEALPHA                  4ul
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
/* D3DPTBLENDCAPS_MODULATEALPHA, d3dcaps.h: bit 3, between DECALALPHA (4)
 * and DECALMASK (0x10). */
#define V9X_D3DPTBLENDCAPS_MODULATEALPHA  0x00000008ul
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

/*
 * DDRAWI_DDRAWSURFACE_LCL prefix: lpGbl at +4, ddsCaps at +32.
 *
 * The fields past ddsCaps were placed by measurement, not by reading a
 * header: on 2026-09-03 the HAL copied the sixteen DWORDs from dwFlags onward
 * of a texture the probe had given a source colour key of 0x7c1f/0x7c1f, and
 * the values landed at dwFlags+32 and +36 (docs/decisions/2026-09-03-
 * colour-key-and-blend-on-the-virge.md). The two zero DWORDs before them are
 * the destination blit key, which the probe had not set; the four before that
 * are the palette, clipper, mode and back-buffer count of the public DDRAWI
 * layout, whose order this agrees with. Only ddckCKSrcBlt is read.
 */
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
    DWORD lpDDPalette;
    DWORD lpDDClipper;
    DWORD dwModeCreatedIn;
    DWORD dwBackBufferCount;
    DWORD ddckCKDestBltLow;
    DWORD ddckCKDestBltHigh;
    DWORD ddckCKSrcBltLow;
    DWORD ddckCKSrcBltHigh;
} V9X_DD_SURFACE_LCL;

/* dwFlags bit set on that same measured LCL and on no other surface the
 * probe drew: the surface carries a source blit colour key. */
#define V9X_DDRAWISURF_HASCKEYSRCBLT 0x00080000ul

/*
 * One node of a surface's attachment list - DDRAWI's DBLNODE (DDRAWI.H),
 * mirrored here as an ABI shape the same way the surface structs above are.
 * lpAttachList on an LCL points at the first of these; `object` is the
 * attached surface and `next` the following node. Only the mip-chain walk
 * reads it, and only `next` and `object`.
 */
typedef struct v9x_dd_attach_node {
    struct v9x_dd_attach_node *next;
    struct v9x_dd_attach_node *prev;
    V9X_DD_SURFACE_LCL *object;
    DWORD object_int;
} V9X_DD_ATTACH_NODE;

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

/*
 * DDHAL_SETCOLORKEYDATA: {lpDD, lpDDSurface, dwFlags, ckNew, ddRVal, fn}, the
 * colour key being two DWORDs (low, high). dwFlags carries the DDCKEY_* the
 * application passed. The layout is checked by measurement rather than
 * trusted: the HAL copies the first six DWORDs of every call into
 * V9X_D3D_DIAGNOSTICS.color_key_raw, and the probe sets a key whose values it
 * knows.
 */
#define V9X_DDCKEY_COLORSPACE 0x00000001ul
#define V9X_DDCKEY_SRCBLT     0x00000008ul

typedef struct v9x_ddhal_setcolorkeydata {
    DWORD lpDD;
    V9X_DD_SURFACE_LCL *lpDDSurface;
    DWORD dwFlags;
    DWORD ckLow;
    DWORD ckHigh;
    DWORD ddRVal;
    DWORD SetColorKey;
} V9X_DDHAL_SETCOLORKEYDATA;

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
/* Bumped for the generalized engine descriptor, again when the mode table
 * became variable-length, again for the depth-surface diagnostics below, and
 * again on 2026-09-04 for the command-word census at the end of the block.
 * A mixed old/new DRV+DLL pair fails safe: DriverInit rejects on the
 * dwSize/abi mismatch and leaves a driverinit-pending trace rather than
 * running against the wrong layout. */
/*
 * 2026091915: V9X_D3D_DIAGNOSTICS gains the display watermark registers,
 * which is what an underrun is usually about. An append; the stamp moves
 * for the reason 2026091603 gives.
 *
 * 2026091914: V9X_D3D_DIAGNOSTICS gains lock_flip_pending, so Lock and Blt
 * are counted apart. An append; the stamp moves for the reason 2026091603
 * gives.
 *
 * 2026091913: the PIPESTAT reading gains a baseline and a clearing
 * boundary, without which a sticky bit set at boot or by a mode change
 * would be reported in every capture. An append; the stamp moves for the
 * reason 2026091603 gives.
 *
 * 2026091912: V9X_D3D_DIAGNOSTICS gains the PIPESTAT accumulators, whose
 * bit 31 is the display FIFO underrun. An append; the stamp moves for the
 * reason 2026091603 gives.
 *
 * 2026091911: V9X_D3D_DIAGNOSTICS gains the flip-issue delta, the
 * scanlines the write path costs between the window test and the flip
 * actually being issued. An append; the stamp moves for the reason
 * 2026091603 gives.
 *
 * 2026091910: V9X_D3D_DIAGNOSTICS gains virge_flip_idle_false, the strict
 * settle's count at the flip. An append; the stamp moves for the reason
 * 2026091603 gives.
 *
 * 2026091909: V9X_D3D_DIAGNOSTICS gains virge_idle_false_settle, the times
 * the ViRGE idle bit read set and then went clear inside a confirmation
 * window. An append; the stamp moves for the reason 2026091603 gives.
 *
 * 2026091908: V9X_D3D_DIAGNOSTICS gains blt_flip_pending, the Blt and Lock
 * exposure the D3D guard cannot see. An append; the stamp moves for the
 * reason 2026091603 gives.
 *
 * 2026091907: V9X_D3D_DIAGNOSTICS gains the flip-issue scanline, which
 * settles whether the plane base readback is the active or the pending
 * value. An append; the stamp moves for the reason 2026091603 gives.
 *
 * 2026091906: V9X_D3D_DIAGNOSTICS gains ecoskpd, the Gen3 register that
 * declares what the flip-pending bit means. An append; the stamp moves for
 * the reason 2026091603 gives.
 *
 * 2026091905: V9X_D3D_DIAGNOSTICS gains the flip-release and vblank-duty
 * counters, which test whether a flip ever waits and whether the retrace
 * source is honest. An append; the stamp moves for the reason 2026091603
 * gives.
 *
 * 2026091904: V9X_D3D_DIAGNOSTICS gains draws_into_presented, and the
 * present trace records only the first draw after each flip. A 32-record
 * ring filled 355,422 times in one Robots run and could never span a flip.
 * An append; the stamp moves for the reason 2026091603 gives.
 *
 * 2026091903: the 2026091902 target-offset counters are replaced (never
 * deployed) by the present trace and the ViRGE pending-draw counter. The
 * counters compared against one global value on every context lookup, so a
 * context switch or a lookup that never drew moved them; they could not say
 * which buffer a frame used. The stamp moves for the reason 2026091603
 * gives.
 *
 * 2026091902: V9X_D3D_DIAGNOSTICS gains target_offset_changes and
 * target_offset_prev, which say whether the flip chain ever rebinds the
 * engine to a different buffer. An append; the stamp moves for the reason
 * 2026091603 gives. Never deployed.
 *
 * 2026091901: V9X_D3D_DIAGNOSTICS gains flip_window_closed, which splits the
 * two conditions flip_still_drawing counted together. An append; the stamp
 * moves for the reason 2026091603 gives.
 *
 * 2026091717: the 2026091716 ACTHD fields are replaced by raw-transition
 * fields (never deployed), and the layout capture gains its sample's offset,
 * frame and count. The stamp moves for the reason 2026091603 gives.
 *
 * 2026091716: V9X_D3D_DIAGNOSTICS gains the ACTHD/INSTDONE readings and the
 * 24-register display layout capture. Never deployed.
 *
 * 2026091715: V9X_D3D_DIAGNOSTICS gains the completion-channel state (self
 * test, outstanding, abandoned, drain waits and stalls). An append; the
 * stamp moves for the reason 2026091603 gives.
 *
 * 2026091714: V9X_D3D_DIAGNOSTICS gains the CPU probe, the last breadcrumb
 * value and the late-arrival count. An append; the stamp moves for the
 * reason 2026091603 gives.
 *
 * 2026091713: V9X_D3D_DIAGNOSTICS gains the three HWS_PGA readings. An
 * append; the stamp moves for the reason 2026091603 gives.
 *
 * 2026091712: V9X_D3D_DIAGNOSTICS gains the breadcrumb counters. An append;
 * the stamp moves for the reason 2026091603 gives.
 *
 * 2026091711: V9X_D3D_DIAGNOSTICS gains the in-game layout fields (plane
 * stride, plane control, pipe source, target pitch and extent). An append;
 * the stamp moves for the reason 2026091603 gives.
 *
 * 2026091710: V9X_D3D_DIAGNOSTICS gains the draws-waited-for-flip counters.
 * An append; the stamp moves for the reason 2026091603 gives.
 *
 * 2026091709: V9X_D3D_DIAGNOSTICS gains the draws-to-front/back counters and
 * the last target and displayed offsets. An append; the stamp moves for the
 * reason 2026091603 gives.
 *
 * 2026091708: V9X_D3D_DIAGNOSTICS gains two ISR accumulators and the
 * frames-in-submit count. An append; the stamp moves for the reason
 * 2026091603 gives.
 *
 * 2026091707: V9X_D3D_DIAGNOSTICS gains five plane-base readback counters.
 * An append; the stamp moves for the reason 2026091603 gives.
 *
 * 2026091706: V9X_D3D_DIAGNOSTICS gains the two ring-flip counters. An
 * append; the stamp moves for the reason 2026091603 gives.
 *
 * 2026091705: V9X_D3D_DIAGNOSTICS gains the frame-tick line per pipe (four
 * DWORDs). An append; the stamp moves for the reason 2026091603 gives.
 *
 * 2026091704: V9X_D3D_DIAGNOSTICS gains five flip counters (handled, still
 * drawing, declined, forced idle, scanout unresolved). An append; the stamp
 * moves for the reason 2026091603 gives.
 *
 * 2026091703: V9X_D3D_DIAGNOSTICS gains the texture placement seen at
 * TextureCreate (two DWORDs) and the last unexpressible Z comparison. An
 * append; the stamp moves for the reason 2026091603 gives.
 *
 * 2026091702: V9X_D3D_DIAGNOSTICS gains nine scanout-watch counters (line
 * range, changes and frames elapsed per pipe, and the sample count). An
 * append at the end of that struct; the stamp moves for the reason
 * 2026091603 gives.
 *
 * 2026091701: V9X_DD_TRACE.counters[] grows by one WORD for
 * V9X_TRACE_D3D_RENDERLOOP. The marker is pushed, not counted, so the slot
 * is spare today; it is there so counters[] keeps covering every id, and
 * because the trace is the last field nothing else moves. A 32-bit side
 * with the longer array against a 16-bit side sized for the shorter one
 * would still write two bytes past the block.
 *
 * 2026091605: V9X_D3D_DIAGNOSTICS gains the surface-pointer CALL SITE and the
 * mask of sites that have rejected. Added because the fault address alone
 * cannot name the caller and a conclusion was drawn from it that it did not
 * support.
 *
 * 2026091604: V9X_D3D_DIAGNOSTICS gains the two surface-pointer counters.
 *
 * 2026091603: V9X_D3D_DIAGNOSTICS gains six Gen3 draw counters. An append
 * at the end of that struct, but the stamp moves anyway: a 32-bit HAL
 * writing fields a 16-bit side sized without them writes past the
 * allocation, and the stamp is what stops the two meeting.
 *
 * 2026091602: V9X_DD_ENGINE gains ring_linear_base and ring_bytes, and the
 * shared block grows to two DPMI pages to hold them.
 *
 * A layout change this time, not just a meaning change, and the block's own
 * allocated size moves with it - V9X_DD_SHARED_BYTES in runtime.asm. A
 * 16-bit side allocating one page and a 32-bit side reading two would read
 * whatever follows the allocation.
 *
 * 2026091601: V9X_DD_ENGINE's reserved1 becomes gtt_linear_base.
 *
 * No field moved and the struct did not grow, so this is a change of MEANING
 * rather than of layout - which is exactly why the stamp has to move. A
 * 16-bit side that still thinks the field is reserved writes zero to it, and a
 * 32-bit side that reads it as a second aperture would map address zero. An
 * address nobody set is a mapping to somewhere.
 */
#define V9X_DD_SHARED_ABI   2026091915ul
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
    /*
     * A SECOND aperture, from ABI 2026091601.
     *
     * Every engine before this one derives what it needs from
     * control_linear_base: the ViRGE's control aperture is
     * framebuffer_linear_base + 0x01000000, a fixed offset from one base. Gen3
     * cannot be expressed that way. Its registers are in BAR0 and its page
     * table is in BAR3, two independent PCI regions whose addresses this
     * project has measured to differ between DOS and Windows on the same
     * machine - so neither can be derived from the other or cached across a
     * boot.
     *
     * It occupies what was reserved1, so NO FIELD MOVED and the struct did not
     * grow. That matters here rather than being a nicety: V9X_DD_SHARED is
     * DPMI-allocated at 4096 bytes and the assertion at the end of this header
     * bounds it. Appending a second DWORD for the mapped SIZE overflowed that
     * bound, which is why the size is not a field - see
     * V9X_DD_ENGINE_GTT_BYTES below.
     *
     * Zero means "this engine has no second aperture", which is every engine
     * but Gen3.
     */
    DWORD gtt_linear_base;
    /*
     * The command ring, for an engine whose HAL submits its own work.
     *
     * Published by the 16-bit side, which asks the mini-VDD for it, because
     * the 32-bit side cannot work it out and must not try. It derived this
     * address once from fb.vram_bytes - which has already had the reserve
     * taken off it - and landed a megabyte low, inside the DirectDraw heap,
     * where command dwords would have overwritten application surfaces.
     *
     * Zero means this engine has no ring the HAL may write, which is every
     * engine but Gen3.
     */
    DWORD ring_linear_base;
    DWORD ring_bytes;
} V9X_DD_ENGINE;

typedef struct v9x_dd_cb32 {
    DWORD Flip;             /* flat function pointers filled by the DLL */
    DWORD GetFlipStatus;
    DWORD Lock;
    DWORD Unlock;
    DWORD WaitForVerticalBlank;
    DWORD flags;            /* extra DDHALINFO.dwFlags bits             */
} V9X_DD_CB32;

/*
 * Why the last depth surface offered to v9x_d3d_set_target was refused.
 *
 * These exist because "the runtime never passed a depth surface" and "the
 * driver rejected the one it passed" are the same observation from outside:
 * both leave the context depth-less and both let ContextCreate return
 * DD_OK. Each guest round trip is expensive enough that the discriminating
 * detail has to be recorded the first time rather than narrowed by
 * re-running.
 *
 * NONE is the initial value and means no depth surface has been offered
 * since the driver loaded, which is itself the finding when the count of
 * offers is zero.
 */
#define V9X_D3D_ZREJECT_NONE          0ul
#define V9X_D3D_ZREJECT_ACCEPTED      1ul
/*
 * A non-null lpDDSZ whose wrapper carries no lpLcl. This one is not a
 * rejection: the driver treats it as no depth surface at all and creates the
 * context anyway, so it is the one arm where a title gets depth-less
 * rendering with every HRESULT still reporting success. It is recorded
 * rather than refused because refusing is a behaviour change, not
 * instrumentation.
 */
#define V9X_D3D_ZREJECT_NO_LCL        2ul
#define V9X_D3D_ZREJECT_NO_GBL        3ul  /* no global surface record     */
#define V9X_D3D_ZREJECT_NOT_ZBUFFER   4ul  /* DDSCAPS_ZBUFFER absent       */
#define V9X_D3D_ZREJECT_SYSTEM_MEMORY 5ul  /* allocated in system memory   */
#define V9X_D3D_ZREJECT_DIMENSIONS    6ul  /* smaller than the target      */
#define V9X_D3D_ZREJECT_UNALIGNED     7ul  /* offset not 8-byte aligned    */
#define V9X_D3D_ZREJECT_OVERLAPS_FB   8ul  /* would write the visible page */
#define V9X_D3D_ZREJECT_PITCH         9ul  /* disagrees with DDK stride    */
#define V9X_D3D_ZREJECT_BOUNDS       10ul  /* falls outside VRAM           */

/* Records in the present trace at the end of V9X_D3D_DIAGNOSTICS. Short
 * because the pattern repeats every frame: the last few frames answer the
 * question, and a longer ring costs shared-block bytes for nothing. */
#define V9X_D3D_PRESENT_TRACE 32u

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
    /*
     * Appended 2026-08-30, and append-only from here. This struct is copied
     * wholesale into V9X_DD_TRACE_SNAPSHOT and read back by a separately
     * built tool, so a field inserted above an existing one reassigns every
     * field after it with nothing to say so - the same failure that moved
     * the clipper's guard band to sixteen pixels on this branch.
     */
    DWORD depth_offered;    /* set_target calls carrying a depth surface   */
    DWORD depth_accepted;   /* ... of which the surface passed validation  */
    DWORD depth_reject;     /* V9X_D3D_ZREJECT_* of the most recent offer  */
    DWORD depth_caps;       /* ddsCaps of that surface                     */
    DWORD depth_offset;     /* its VRAM offset, or 0 if it had no lpGbl    */
    DWORD depth_pitch;      /* its lPitch as DirectDraw reported it        */
    /*
     * Appended 2026-09-03. The S3D engine has one TEX_BASE and reads every
     * mip level at a fixed offset from it - largest level first, each level
     * following the last - while DirectDraw allocates each level as its own
     * surface and promises nothing about where. Every mipmapped draw checks
     * the chain; a chain with a gap is drawn from level 0 alone rather than
     * fetched from whatever lies past the top level.
     */
    DWORD mip_chain_checks; /* mipmapped textures examined at draw time    */
    DWORD mip_chain_gaps;   /* ... of which were not laid out contiguously */
    /*
     * Appended the same day, because gaps=0 turned out to say nothing on its
     * own: a walk that finds no attached level returns "contiguous" having
     * verified nothing. These say how far the last walk actually got and what
     * it saw, so a zero in gaps can be told apart from a walk that never
     * started.
     */
    DWORD mip_chain_levels; /* levels below the top verified by the last walk */
    DWORD mip_chain_delta;  /* last walk: level-1 offset minus level-0 offset,
                               or 0xffffffff when no level 1 was found        */
    /*
     * Appended 2026-09-03, after 3DMark 99. Three questions its picture
     * raised that the block could not answer: were textures refused, and
     * why; which blend pairs did the application ask for that the engine
     * cannot express; and did colour keys reach the driver at all.
     */
    DWORD texture_refused_format; /* draws whose texture had a format the
                                     sampler lacks (not 1555/4444, or no
                                     format of its own)                      */
    DWORD texture_refused_shape;  /* ... or a shape it lacks: not square,
                                     not a power of two, out of range, pitch */
    DWORD texture_refused_last;   /* format: (bitcount<<24)|dwRBitMask, or
                                     0xffffffff for no format of its own;
                                     shape: (width<<16)|(pitch&0xffff)       */
    DWORD blend_skipped;          /* triangles not drawn because the blend
                                     pair has no S3D expression              */
    DWORD blend_last_pair;        /* (src<<16)|dest of the last such pair    */
    DWORD color_key_sets;         /* HAL SetColorKey calls                   */
    DWORD color_key_raw[6];       /* the last call's data block, verbatim    */
    DWORD color_key_draws;        /* textured draws with a source key applied*/
    DWORD color_key_rewrites;     /* texel-alpha rewrite passes over textures*/
    /*
     * Appended 2026-09-03, later the same day. DirectDraw did not call the
     * SetColorKey callback for a texture key (measured: color_key_sets stayed
     * 0 across a probe that set one), so the key has to be read from the
     * surface's own record, whose layout past ddsCaps this ABI does not
     * mirror. This is the instrument for finding it: the sixteen DWORDs of
     * the bound texture's LCL from dwFlags onward, copied at the first
     * textured draw with COLORKEYENABLE set, so a key the probe chose can be
     * located by its value.
     */
    DWORD lcl_tail_raw[16];
    DWORD lcl_tail_captures;
    /*
     * Appended 2026-09-03, after 3DMark 99 again. Its walls drew in the
     * probe's own texture-fill green and its ground in what looked like a
     * stretched copy of an old frame - texture memory nobody had written -
     * while texture_refused_* read zero. Two gaps: three of the sampler's
     * refusals were not counted (system memory, no TEXTURE cap, outside
     * VRAM), and nothing said what the sampler was actually reading. So:
     * the refusals, and two texels of every bound texture at draw time.
     */
    DWORD texture_refused_other;  /* sysmem, no TEXTURE cap, or out of VRAM */
    DWORD texture_last_offset;    /* the last sampled texture: VRAM offset  */
    DWORD texture_last_size;      /* ... edge in texels                     */
    DWORD texture_last_caps;      /* ... ddsCaps                            */
    DWORD texture_last_texels;    /* ... texel (0,0) low, (mid,mid) high    */
    DWORD texture_green_draws;    /* draws whose both sampled texels were
                                     0x83e0, the probe's fill               */
    DWORD texture_alpha_draws;    /* draws blended by texel alpha           */
    /*
     * Appended 2026-09-03, once texture_refused_other had read 14,005 for a
     * 3DMark 99 run and could not say which of its three reasons fired. It
     * still counts the sum; these split it, and the last refused surface's
     * caps say what kind of surface it was.
     */
    DWORD texture_refused_sysmem;  /* DDSCAPS_SYSTEMMEMORY set               */
    DWORD texture_refused_nocap;   /* DDSCAPS_TEXTURE absent                 */
    DWORD texture_refused_bounds;  /* offset unresolved or outside VRAM      */
    DWORD texture_refused_caps;    /* ddsCaps of the last surface so refused */
    DWORD texture_refused_vidmem;  /* its fpVidMem                           */
    /*
     * Appended 2026-09-03, for the 3D-done wait. done_seen counts idle waits
     * that found the 3D-done bit set after a launch; done_missing counts
     * idle waits that saw the engine idle for the whole spin and never the
     * done bit, and gave up - without a reset, because an engine that says
     * idle is not one to reset. If done_missing is ever non-zero the bit is
     * not doing what the wait assumes, on that chip or that emulator.
     */
    DWORD done_seen;
    DWORD done_missing;
    /*
     * Appended 2026-09-04, once the S3 Trio3D/2X had written done_missing for
     * every one of 117 probe cells: the wait now gives up asking a part that
     * has answered none of a long run of waits, and done_skipped counts the
     * waits taken after that decision, which spun for the bit not at all. A
     * run with done_skipped rising and done_seen at zero is a part without the
     * bit; done_skipped non-zero on a part whose done_seen is also non-zero
     * would be the rule firing when it should not.
     */
    DWORD done_skipped;
    /*
     * Appended 2026-09-05. The render target as the engine last programmed it:
     * the offset that goes into DEST_BASE and the pitch that goes into the
     * high half of DEST_SRC_STRIDE, refreshed per draw by
     * v9x_d3d_refresh_target so a flipping chain's page change is followed.
     *
     * A blend onto the primary chain's back buffer draws nothing on the
     * emulated ViRGE/DX while an opaque draw onto the same surface lands
     * (docs\decisions\2026-09-05-a-blend-onto-the-primary-chain-draws-nothing.md).
     * The depth surface has been published this way since the depth work and
     * the render target never was, so there was no way to ask whether the
     * engine was pointed where the application thought. Now there is: compare
     * these against the surface's own address and pitch.
     */
    DWORD target_offset;
    DWORD target_pitch;
    DWORD target_width;
    DWORD target_height;
    /*
     * Nothing is appended here for the render-target switch, and that is a
     * measured constraint rather than a choice: two more DWORDs took
     * V9X_DD_SHARED past the 4096 bytes the 16-bit side DPMI-allocates, and
     * the assert at the bottom of this header caught it. The question - was
     * V9xD3dSetRenderTarget entered at all - is answered by
     * trace.counters[V9X_TRACE_D3D_SETRENDERTARGET], which this block
     * already carries, so the probe's compact view reads it from there
     * (docs\issues\2026-09-05-setrendertarget-is-accepted-and-ignored.md).
     */
    /*
     * Appended 2026-09-16, for the Gen3 engine, because intel52 could not be
     * read: 404 RenderPrimitive calls produced about eighty submissions and
     * nothing anywhere said what happened to the rest. A draw that returns
     * zero is an error the application sees and a frame nobody can explain.
     *
     * i9xx_refuse_last carries the reason code of the last refusal - one of
     * the V9X_I9XX_REFUSE_* values in d3d_i9xx.c - so a capture names the
     * check rather than leaving the count to be guessed at.
     */
    DWORD i9xx_draws_submitted;
    DWORD i9xx_draws_refused;
    DWORD i9xx_refuse_last;
    DWORD i9xx_texture_draws;   /* draws that sampled a map                  */
    DWORD i9xx_depth_draws;     /* draws that tested depth                   */
    /*
     * Depth asked for and not delivered: the application enabled Z with a
     * comparison this engine's S6 does not carry, or a surface the footprint
     * check refused. The draw still goes, un-Z'd, which is the ViRGE's
     * behaviour for a blend it cannot express - and like that one it is a
     * wrong picture rather than a missing one, so it has to be counted or it
     * is invisible.
     */
    DWORD i9xx_depth_skipped;
    /*
     * Appended 2026-09-16, after the same instruction faulted in two boots.
     *
     * v9x_d3d_surface_lcl is handed a surface pointer by the runtime and
     * dereferences it. Final Reality's first RenderPrimitive passed one that
     * was non-null and not a surface, and the HAL died taking the application
     * with it - twice, at module offset 0x851 both times, which is the
     * `mov eax,[eax+4]` that reads lpLcl.
     *
     * surface_int_rejected counts the pointers the guard refused and
     * surface_int_last carries the last such value, because "a pointer was
     * bad" and "THIS pointer was bad" are different amounts of evidence and
     * only the second one leads anywhere.
     */
    DWORD surface_int_rejected;
    DWORD surface_int_last;
    /*
     * WHICH CALLER handed over the bad pointer, because the fault address
     * cannot say and I concluded from it anyway.
     *
     * v9x_d3d_surface_lcl has ten call sites. Two are RenderPrimitive's
     * execute and TL buffers; one is the texture-teardown scan that
     * DestroySurface runs over every stored texture pointer; the rest are
     * the render target, the depth surface, the colour-key pair and two more
     * primitive entry points. A fault inside that helper is consistent with
     * all of them, and the intel54 capture's last enter was DestroySurface -
     * which if anything favours the teardown scan over the reading I
     * published.
     *
     * surface_int_site is the last rejecting site and surface_int_sites is a
     * bitmask of every site that has ever rejected, because "which one this
     * time" and "which ones at all" are different questions and a capture
     * that answers only the first can still be read wrongly.
     */
    DWORD surface_int_site;
    DWORD surface_int_sites;
    /*
     * The scanout, watched once per boot from the draw path, per pipe.
     *
     * A Gen3 flip needs a vblank source, and the only one this driver has
     * is the VGA status port the ViRGE path reads - unmeasured on a 945GSE
     * driving an LVDS panel through pipe B. The pipe's display-line register
     * and frame counter are the native source. After the first submitted
     * draw the HAL reads both pipes' registers a few thousand times and
     * keeps the line range, the number of readings that changed, and the
     * frames elapsed. Many changes on the live pipe and none on the dead one
     * is the answer; a constant on both means the registers are not what
     * intel_gma.h says they are on this part.
     *
     * Raw counts, not a verdict: the summary is pure C in i9xx_scanline.c
     * with a host test, and the reading of it belongs in the record.
     */
    DWORD scan_samples;
    DWORD scan_a_line_min;
    DWORD scan_a_line_max;
    DWORD scan_a_line_changes;
    DWORD scan_a_frames;
    DWORD scan_b_line_min;
    DWORD scan_b_line_max;
    DWORD scan_b_line_changes;
    DWORD scan_b_frames;
    /*
     * Where the runtime PUT each texture, seen at TextureCreate.
     *
     * intel59 refused 1,118,317 draws for a system-memory texture while
     * every format was accepted, and the bind-time counters could not say
     * whether the surface was created there or moved there by Load. These
     * two can: a create whose surface already carries DDSCAPS_SYSTEMMEMORY
     * is counted, and the last create's caps are kept.
     */
    DWORD texture_create_sysmem;
    DWORD texture_create_last_caps;
    /*
     * The last Z comparison an application asked for that this engine could
     * not express. intel59 skipped the depth test on 663,556 draws; S6
     * carries LESS alone, and this says what was wanted instead.
     */
    DWORD i9xx_depth_last_func;
    /*
     * What Flip answered, and why. intel63 had 54,688 Flips and the ring
     * showed only WASSTILLDRAWING; the per-callback count could not say how
     * many were HANDLED, declined to DirectDraw's copy, or refused because
     * a pending flip's retrace never came. flip_forced_idle counts the
     * pending flips the bound in v9x_flip_done gave up on, and
     * scanout_unresolved the Intel vblank/base reads that found no single
     * live pipe and plane to act on.
     *
     * From 2026091901 flip_still_drawing is the previous flip not yet
     * taken alone; the window test has its own counter at the end of this
     * structure.
     */
    DWORD flip_handled;
    DWORD flip_still_drawing;
    DWORD flip_declined;
    DWORD flip_forced_idle;
    DWORD scanout_unresolved;
    /*
     * From the scanout watch: the display line at which the frame counter
     * was first seen to tick, per pipe, and whether a tick was seen. The
     * flip path treats DSL >= vactive as the blank; intel65 tore in the
     * lower half with the base written after that test, and intel66 tore
     * worse with it written inside it. Where the counter ticks says what
     * DSL's numbers mean on this part, which nothing has measured.
     */
    DWORD scan_a_tick_line;
    DWORD scan_a_tick_seen;
    DWORD scan_b_tick_line;
    DWORD scan_b_tick_seen;
    /* The ring flip: streams the parser was given, and flips refused before
     * one was - a stream the builder or decoder would not pass, or a ring
     * that would not take it. */
    DWORD flip_ring_issued;
    DWORD flip_ring_refused;
    /*
     * What the plane base register READ BACK, at two moments, per flip.
     *
     * intel71: the flip through MI_DISPLAY_FLIP tore like the register
     * write did, and the ISR pending bit was never seen set across 795
     * flips. Whether either mechanism applies the base at once or at the
     * retrace has been inferred from pictures three times and never read.
     * So: right after the flip is issued, does the base register already
     * hold the new offset (immediate) or the old one (deferred)? When the
     * state machine declares the flip done, does it hold the new offset
     * (taken) or not? And was the ISR pending bit set on the read made
     * directly after the ring submit, before any poll?
     */
    DWORD flip_base_immediate;
    DWORD flip_base_deferred;
    DWORD flip_taken_at_done;
    DWORD flip_not_taken_at_done;
    DWORD flip_ring_pending_seen;
    /*
     * Finding the pending bit empirically, and whether the streamer stalls.
     *
     * intel72: with i915's bits 11 and 10, ISR never showed a pending flip
     * on the read after the submit, the base read new at once, and the
     * picture still tore. So: the OR of ISR read directly after every flip
     * is issued, and the OR of ISR read directly before - a bit in the
     * first and not the second is the pending bit on this part, wherever
     * v4.4 puts it. And the number of flips across whose ring submit the
     * frame counter advanced: near all says the streamer stalled on
     * MI_DISPLAY_FLIP until the retrace, near none says it did not.
     */
    DWORD isr_after_flip_or;
    DWORD isr_before_flip_or;
    DWORD flip_frames_in_submit;
    /*
     * Where each draw landed relative to what the display was showing.
     *
     * intel74: with the flip issued in the blank and released at the tick,
     * the flicker got FASTER, not smaller. A flicker that speeds up as
     * flips become regular is the shape of frames being drawn into the
     * buffer on screen - every flip then shows a half-drawn frame. So each
     * batch compares its render-target offset with the plane base register
     * as it reads at that moment: draws_to_front is the batch landing in
     * the displayed buffer, draws_to_back the hidden one. Near-zero front
     * says the buffers are right and the cause is elsewhere; a large front
     * count is the fault, wherever it comes from.
     */
    DWORD draws_to_front;
    DWORD draws_to_back;
    DWORD draws_target_last;
    DWORD draws_displayed_last;
    /*
     * Batches that arrived while a flip was still pending, and how many of
     * those waits ran out.
     *
     * intel78: with the base written in active video the flip completes at
     * the tick that follows the latch, and the panel still shows the buffer
     * under construction. Flip returns as soon as the base is written;
     * DirectDraw gates Lock and Blt on GetFlipStatus but Direct3D draws go
     * straight to RenderPrimitive, so the game's first batches of a frame
     * land in the buffer the panel is still fetching. The engine now waits
     * for the pending flip before the first batch; this counts how often
     * that wait was needed, which is the measurement of the exposure.
     */
    DWORD draws_flip_waited;
    DWORD draws_flip_wait_timeouts;
    /*
     * The scanout layout during the game, not the desktop: the plane stride
     * register, plane control and pipe source size as read when a flip is
     * issued, and the render target's pitch and width<<16|height as the
     * draws see them. Equal base addresses are not the only way a batch
     * lands on screen - a plane stride wider than the target's pitch fetches
     * 480 rows into the next buffer - and DrawsToFront cannot see that.
     */
    DWORD flip_stride_last;
    DWORD flip_dspcntr_last;
    DWORD flip_pipesrc_last;
    DWORD draws_pitch_last;
    DWORD draws_extent_last;
    /*
     * The breadcrumb: how far behind the ring head the drawing actually is.
     *
     * intel80's /reuse probe presented the right buffer at every delay and
     * the fetch stride was right, while the game still shows a frame filling
     * in AFTER its flip. The one model left is that the flip presents a
     * frame the GPU has not finished: head == tail says the parser consumed
     * the batch, not that the pixels landed. Every batch now ends with an
     * MI_STORE_DWORD_IMM of a sequence number behind the MI_FLUSH, and the
     * submit waits for it after the head. lag_polls_max / lag_polls_total
     * are the polls spent between head == tail and the value arriving:
     * zero means the head was already the truth and this model is dead
     * too; large means the rendering was still running when every
     * previous build called the batch done.
     */
    DWORD breadcrumb_submits;
    DWORD breadcrumb_lag_polls_max;
    DWORD breadcrumb_lag_polls_total;
    DWORD breadcrumb_timeouts;
    /*
     * The status page, from intel82 on: HWS_PGA as read before this driver
     * wrote it (the BIOS value, 0x1FFFF000 on the netbook), the physical
     * page address written - the GTT's own entry for the reserve's status
     * page, read through BAR3, so the physical address is the hardware's
     * word and not arithmetic on BSM - and HWS_PGA read back afterwards.
     * A readback that differs from the write is the register refusing it.
     */
    DWORD hws_pga_before;
    DWORD hws_pga_written;
    DWORD hws_pga_after;
    /*
     * intel83: HWS_PGA took the page and MI_STORE_DWORD_INDEX into it still
     * never landed inside the wait - the same result as the IMM form. Two
     * stores by two mechanisms that both "never land" points at the wait or
     * the read, not the store. Three readings to separate them:
     *   hws_cpu_probe    1 if a CPU write to the page's second dword reads
     *                    back through the same mapping (the mapping is real
     *                    and writable), 2 if it did not read back.
     *   hws_value_last   the breadcrumb dword as read at the last timeout:
     *                    zero says nothing was ever stored, an older sequence
     *                    says the store works and lands LATE.
     *   breadcrumb_late  batches whose predecessor's breadcrumb had arrived
     *                    by the time the next batch was built: the store
     *                    works, and the drawing takes longer than the wait.
     */
    DWORD hws_cpu_probe;
    DWORD hws_value_last;
    /* From 2026091715 this counts a TIMED-OUT sequence later observed, once
     * per sequence (review R2); before, it counted any batch whose
     * predecessor's value was still in memory, which is every batch. */
    DWORD breadcrumb_late;
    /*
     * The completion channel as a state machine (review R1-R3).
     *   hws_selftest        0 not run; 1 the store-only round trip landed;
     *                       2 setup or the round trip failed - no batch
     *                       carries a breadcrumb after a 2.
     *   hws_selftest_polls  polls the round trip took to land.
     *   breadcrumb_outstanding  the sequence issued and not yet observed,
     *                       0 when none: a completion the driver still owes.
     *   breadcrumb_abandoned  channels given up: an outstanding sequence
     *                       unseen for the abandon bound, after which
     *                       breadcrumbs stop and the fact is recorded.
     *   render_drain_waits  Flip, Lock or Blt found rendering outstanding
     *                       and waited; render_drain_stalls the times that
     *                       wait ran out and WASSTILLDRAWING went back.
     */
    DWORD hws_selftest;
    DWORD hws_selftest_polls;
    DWORD breadcrumb_outstanding;
    DWORD breadcrumb_abandoned;
    DWORD render_drain_waits;
    DWORD render_drain_stalls;
    /*
     * ACTHD and INSTDONE, RAW, around the moment RING_HEAD reaches the
     * tail (intel84: the status-page round trip failed too, so no GPU
     * write reaches this side and the engine has to be read). Nothing
     * here interprets ACTHD as an address or as completion - its Gen3
     * address form and idle meaning are not established on this part
     * (review of 2347f59). What is recorded is whether the register kept
     * CHANGING after the parser was done, which needs no interpretation:
     *   acthd_at_head_last / acthd_after_last  the value at head == tail
     *                     and after a fixed number of polls, last submit.
     *   acthd_moved       submits in which ACTHD changed during those
     *                     polls; acthd_still the submits in which it did
     *                     not; acthd_changes_max the most distinct values
     *                     seen in one submit's polls.
     *   acthd_raw_min / acthd_raw_max  the range of every raw value read.
     *   instdone_at_head_last / instdone_after_last  the same two moments.
     *   tail_last         the ring tail of that submit, for the record.
     * A register that keeps moving after the head is at the tail is an
     * engine still working; whether that is rendering, and when it ends,
     * is for a later instrument that has validated the register.
     */
    DWORD acthd_at_head_last;
    DWORD acthd_after_last;
    DWORD acthd_moved;
    DWORD acthd_still;
    DWORD acthd_changes_max;
    DWORD acthd_raw_min;
    DWORD acthd_raw_max;
    DWORD instdone_at_head_last;
    DWORD instdone_after_last;
    DWORD tail_last;
    /*
     * The display layout as read at an APPLICATION flip during the game
     * (review H4): register offsets and their values, in pairs, so the
     * panel's actual fetch can be reconciled with the buffers. Taken only
     * when the flip target is not offset zero, which the desktop
     * restoration (FlipToGDISurface, exclusive-mode exit) always is - so
     * the sample survives leaving the game (review of 2347f59). The
     * target offset and frame counter of the sample are kept with it.
     */
    DWORD scan_reg_offset[24];
    DWORD scan_reg_value[24];
    DWORD scan_sample_offset;
    DWORD scan_sample_frame;
    DWORD scan_layout_samples;
    /*
     * Flips refused because the issue window was shut, split out of
     * flip_still_drawing (intel86: 52,608 of 53,193 Flips were refused and
     * the counter could not say by which of the two tests, so the thing
     * throttling presents to 585 could not be named). From here
     * flip_still_drawing is the previous flip not yet taken, and this is
     * the beam being outside v9x_scanout_flip_window_open.
     */
    DWORD flip_window_closed;
    /*
     * The present trace: which buffer each frame actually used, in order.
     *
     * An aggregate counter cannot answer this. The 2026091902 attempt
     * compared each render target against one global value on every
     * context lookup, so switching contexts or looking one up without
     * drawing moved it, and it could not say whether a given frame drew
     * into the buffer that had just been presented. What distinguishes a
     * stale binding from premature reuse is the ORDER of three things, so
     * the three are recorded together:
     *
     *   kind  1 flip accepted - offset is what was handed to
     *           set_display_start, seq the accepted-flip number.
     *         2 flip observed taken by the scanout, same seq.
     *         3 draw batch submitted - offset is the destination the
     *           engine was actually given, context the context index.
     *   context  the context index for a draw, 0xFFFFFFFF otherwise.
     *   offset   the buffer offset that record is about.
     *   seq      accepted flips so far, so a draw can be placed between
     *            the flip it follows and the completion it precedes.
     *
     * The ring is short on purpose: the pattern repeats every frame, and
     * the last few frames answer the question. present_trace_count is
     * records written in total, so wrap and loss are visible.
     */
    DWORD present_trace_kind[V9X_D3D_PRESENT_TRACE];
    DWORD present_trace_context[V9X_D3D_PRESENT_TRACE];
    DWORD present_trace_offset[V9X_D3D_PRESENT_TRACE];
    DWORD present_trace_seq[V9X_D3D_PRESENT_TRACE];
    DWORD present_trace_count;
    /*
     * Draw batches the ViRGE path began while a flip was still pending.
     *
     * The Intel path waits for the flip before its first batch (intel78,
     * draws_flip_waited); d3d_virge.c has no such guard - v9x_flip_wait_done
     * is called from d3d_i9xx.c alone. So the two backends are not in the
     * same state with respect to this hazard, and a flicker that looks the
     * same on both may reach the panel by two different routes. This
     * counts the exposure without changing it; the build that waits comes
     * after, so the two can be compared.
     */
    DWORD virge_draws_flip_pending;
    /*
     * Draw batches aimed at the buffer the most recently accepted flip
     * named - the one the panel was last told to show. Counted for every
     * batch rather than traced, so it is independent of the ring's length.
     * Zero across a run says the engine was never aimed at the presented
     * buffer; a large count is the fault the flicker would need, and the
     * present trace says whether it is premature reuse or a stale binding.
     */
    DWORD draws_into_presented;
    /*
     * Does a flip ever actually wait, and is the retrace source telling the
     * truth?
     *
     * On the S3 path a flip is armed WAIT_BLANK or WAIT_UNBLANK and is
     * declared taken the first time v9x_in_vblank agrees. If that source
     * answers yes when the beam is not in a retrace, every flip completes
     * on its first poll, the buffer is released before the CRTC start has
     * latched, and the application draws into memory the panel is still
     * fetching - which is what the Trio3D recording of 2026-09-19 shows,
     * and what draws_into_presented cannot see because it compares against
     * the driver's own record rather than the scanout.
     *
     *   flip_done_first_poll  flips declared taken on the first poll after
     *                         arming. Near the flip count means no flip
     *                         ever waited for anything.
     *   flip_armed_in_blank   flips armed while the source said blank.
     *   vblank_samples /      the source sampled once per draw batch. A
     *   vblank_in_blank       real retrace is a few per cent of a frame;
     *                         a ratio near 1 is a source stuck at yes, and
     *                         near 0 one stuck at no.
     */
    DWORD flip_done_first_poll;
    DWORD flip_armed_in_blank;
    DWORD vblank_samples;
    DWORD vblank_in_blank;
    /*
     * ECOSKPD as read once the engine is up, raw. Bit 0 is ECO_FLIP_DONE:
     * set, and this part's flip-pending bit means the flip is DONE; clear,
     * and it means queued with completion at the vblank. Gen3 is the only
     * generation that declares this, and three of this project's
     * investigations into a flip-pending bit that never sets were run
     * without ever asking. Raw, and interpreted nowhere but in the report.
     */
    DWORD ecoskpd;
    /*
     * The DSL line at which the plane base was read back after a flip was
     * issued, last, min and max, with the pipe's active height beside them.
     *
     * A base register holding the ACTIVE value cannot report a new offset
     * mid-frame, because the latch has not happened. Lines scattered
     * through active video with flip_base_immediate at 100% therefore say
     * the readback is the PENDING value, and i915's stall check - which
     * treats that readback as proof of completion - does not transfer to
     * this part. Lines clustered at or past vactive say the opposite.
     */
    DWORD flip_issue_line_last;
    DWORD flip_issue_line_min;
    DWORD flip_issue_line_max;
    DWORD flip_issue_vactive;
    /*
     * Blt and Lock calls that arrived with a flip still pending.
     *
     * The D3D draw guard measures zero on both backends because the
     * runtime holds the application behind GetFlipStatus, so a batch never
     * races a pending flip. The clear does not go that way: it is a Blt,
     * and the Blt and Lock paths wait on the ENGINE and never ask about
     * the flip. The panel shows a cleared buffer, so this is where to
     * look. Counted, not guarded - the guard comes after the count says
     * there is something to guard.
     */
    DWORD blt_flip_pending;
    /*
     * Settles where the ViRGE idle bit read SET and then went clear inside
     * the confirmation window - the engine was working after all.
     *
     * Non-zero is direct proof that idle alone is not a completion signal
     * on this part, which is the one thing the 3D-done bit would have said
     * and which this card has never once reported. Zero across a run says
     * idle is honest and the frame was complete when it was presented,
     * which would leave nothing in the driver's presentation path
     * unmeasured.
     */
    DWORD virge_idle_false_settle;
    /*
     * The same lie, caught at the flip with a window long enough to
     * matter: 8,192 reads against the ordinary settle's 32, affordable
     * because there are a few hundred flips rather than a quarter of a
     * million settles. Counted per broken confirmation, and the flip does
     * not proceed until the engine settles or the bound runs out.
     */
    DWORD virge_flip_idle_false;
    /*
     * Scanlines between entering the display-start write path and the flip
     * being issued - the cost the latch guard has to cover and did not.
     * intel89 inferred up to ninety-odd from the two ends; this measures
     * it, and the guard should be sized from its distribution.
     */
    DWORD flip_issue_delta_last;
    DWORD flip_issue_delta_max;
    /*
     * PIPESTAT for both pipes, ORed across every flip. Bit 31 is the
     * display FIFO underrun status and it is sticky, so a single set bit
     * anywhere in the run says the scanout starved at least once.
     *
     * An underrun shows a frame whose top is right and whose remainder is
     * not, depends on memory bandwidth rather than on presentation, and has
     * no interrupt on GMCH parts - which together would explain a fault
     * that concentrates in heavy parts of a scene and that no instrument in
     * the flip path can see. Untested; this is the first read.
     */
    DWORD pipestat_a_or;
    DWORD pipestat_b_or;
    /*
     * PIPESTAT as it read on the first flip of the session, BEFORE the
     * underrun status was cleared, and whether that clear happened.
     *
     * The accumulators above are sticky, so without a boundary they report
     * "an underrun happened at some point since power-on" - which a mode
     * change alone would satisfy, and this driver changes mode before the
     * game. With the baseline kept and the status cleared once, bit 31 in
     * pipestat_*_or is a FRESH underrun and bit 31 in pipestat_*_first is
     * the pre-existing one, which is reported separately rather than
     * conflated with it.
     *
     * pipestat_cleared 0 means no boundary was established and the
     * accumulators are inconclusive for new events.
     */
    DWORD pipestat_a_first;
    DWORD pipestat_b_first;
    DWORD pipestat_cleared;
    /*
     * Lock calls that arrived with a flip pending, apart from the Blts.
     * blt_flip_pending covered only Blt until 2026-09-19 because Lock does
     * not go through v9x_blt_drain; with Lock sampled it read 1,104 against
     * 1,124 Blts and 62,138 Locks, which one counter cannot attribute.
     */
    DWORD lock_flip_pending;
    /*
     * FW_BLC, FW_BLC2 and FW_BLC_SELF as they read at the session boundary
     * - the display watermarks, raw.
     *
     * intel90 measured the scanout underrunning on the live pipe, and an
     * underrun is usually a FIFO given too little margin rather than a
     * fault in the flip path. This driver has never programmed these: it
     * sets modes through the VBE BIOS, so they are whatever the BIOS left
     * for the mode the game ended up in. i915 computes them from the mode,
     * which is what to compare against. Bit 15 of FW_BLC_SELF is the
     * 945-only self-refresh enable.
     */
    DWORD fw_blc;
    DWORD fw_blc2;
    DWORD fw_blc_self;
} V9X_D3D_DIAGNOSTICS;

/*
 * Bounded HAL callback trace (Hellbender plan H1). Both sides append
 * fixed-size records to a ring inside the shared block, so the last
 * callbacks before a fault survive the faulting process and can be read
 * back with the V9X_DDGETTRACE escape. All writers are allocation-free.
 */
#define V9X_DD_TRACE_RING_COUNT     56u
/*
 * One past the highest trace id, because v9x_trace_count indexes counters[]
 * with the id itself. At 50 the highest id of the day, V9X_TRACE_D3D_PRIMREJECT,
 * fell outside the array and was silently never counted - the ring still
 * showed it, but the ring is 56 deep and any real workload overwrites it.
 * A primitive rejected during a depth run would have read as zero rejects.
 */
#define V9X_DD_TRACE_ID_COUNT       52u
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
/*
 * Pushed once per RenderPrimitive, after the first triangle's vertices have
 * been read out of the execute and TL buffers and before they are clipped
 * and drawn. intel53-55 each show one RenderPrimitive enter with no exit
 * and no reject; this marker splits "died reading the buffers" from "died
 * in the clipper or the engine", which the enter event alone cannot.
 */
#define V9X_TRACE_D3D_RENDERLOOP      51u

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

/*
 * A census of the S3D command words a run used.
 *
 * The counters say how many triangles were drawn and the ring says which
 * callbacks ran, but neither says what state a wrong-looking draw was made
 * with - and 3DMark 99 on the Trio3D/2X has two defects left whose draws are
 * indistinguishable, by every counter, from the draws beside them that are
 * right (docs\issues\2026-09-03-3dmark99-on-the-trio3d-after-the-stride-fix.md).
 * A ring of the last N draws does not help either: 58,619 of them go by and
 * the interesting ones are in the middle.
 *
 * So this counts distinct command words instead. A run uses few - the word is
 * assembled from a handful of render states - so a small table holds the
 * whole set, in any order, without aiming at anything. The texture-size field
 * (bits 11:8) is masked out of the key and accumulated separately, because it
 * is the one field that varies per texture and would otherwise split every
 * word into a dozen.
 *
 * It is an instrument, not a counter: read it beside a picture, and the bits
 * of a word say what produced the picture.
 */
#define V9X_D3D_CENSUS_SLOTS 32u

typedef struct v9x_d3d_census_entry {
    DWORD command;      /* the word, texture-size field cleared           */
    DWORD draws;        /* triangles launched with it                     */
    DWORD size_mask;    /* bit n: texture_size_log n was used with it     */
    DWORD tex_offset;   /* the last texture bound under it                */
    DWORD tex_caps;     /* that surface's ddsCaps                         */
} V9X_D3D_CENSUS_ENTRY;

typedef struct v9x_d3d_draw_census {
    DWORD slots_used;
    DWORD overflow;     /* draws whose word found no free slot            */
    V9X_D3D_CENSUS_ENTRY entries[V9X_D3D_CENSUS_SLOTS];
} V9X_D3D_DRAW_CENSUS;

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
    V9X_D3D_DRAW_CENSUS census;
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
/*
 * Monochrome CPU-source expansion (build 004). Colour upload is deliberately
 * not a primitive - see docs/decisions/2026-08-27-gdi-accel-004-design.md.
 *
 * 0x10 and NOT 0x08, which is the value the sequence would otherwise take.
 * Testing bit 3 of this mask in the shared 16-bit layer compiles to
 * `test al,8`, and that is the ViRGE's CR53[3] new-MMIO signature - a required
 * instruction in that chip's object and therefore a forbidden one in every
 * other family's image. Build 004 tripped exactly that: the ati image was
 * refused for "foreign-family instruction test al,8" the first time this
 * primitive was compiled in.
 *
 * The s3 family manifest already warns about this class of collision for
 * unanchored patterns. Do not tidy this back to 0x08.
 */
#define V9X_GDI_PRIM_UPLOAD         0x00000010ul
/*
 * Text (build 005): ordinal 14 routes a screen ExtTextOut through
 * DIB_ExtTextOutExt with two driver callbacks, and the engine expands the
 * monochrome string bitmap the DIB Engine hands back. Trio64 only in this
 * build - see docs/decisions/2026-09-06-gdi-accel-005-text.md.
 */
#define V9X_GDI_PRIM_TEXT           0x00000020ul

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
    /* Memory-source blits declined for not being a monochrome expansion - a
     * colour upload, which build 004's design establishes is not worth
     * accelerating. Counted so the harness can prove they decline rather than
     * assume it. */
    DWORD decline_upload;
    DWORD uploads;
    /*
     * Why memory-source blits were refused. A **bitmask**, one bit per reason,
     * accumulated over the run - not a last-one-wins scalar, because a mixed
     * run declines for several reasons at once and the first attempt at this
     * reported only whichever operation happened to come last.
     *   bit 1 not enabled  2 no source   3 source is VRAM  4 source not 1bpp
     *   bit 5 no drawmode  6 zero stride 7 bit offset > destination x
     *   bit 8 source would leave its selector      9 source out of bounds
     * bit 0 is set when an upload was accepted.
     *
     * The detail carries the numbers behind the *geometry* reasons only (6-9),
     * since those are the ones a bit alone does not explain, and is left alone
     * by the earlier reasons so a later colour operation cannot overwrite it.
     */
    DWORD upload_reject_mask;
    DWORD upload_reject_detail;
    /*
     * The destination surface's base offset and pitch, as the last accepted
     * operation saw them. Reported because the Trio64 fill folds the base into
     * the y coordinate - `y = base / pitch + destination_y` - so a non-zero
     * base displaces every rectangle by that many scan lines, and every
     * emulated guest reports zero. Real silicon corrupted the screen and this
     * is the input that differs between the two.
     */
    DWORD last_base;
    DWORD last_pitch;
    /*
     * The Trio64 engine's raw status word, sampled at three points around the
     * last fill: on entry, immediately after the command is written, and after
     * a short settle. Recorded because the probe measured a fill that the
     * driver counted and the framebuffer never received, which leaves only one
     * question worth asking - whether the engine is executing at all.
     *
     * A status that never shows busy means the command is not being accepted,
     * which points at enhanced-mode state the 16-bit path never establishes
     * rather than at anything in the fill itself. 0xffff means the port is
     * reading back floating, i.e. nothing is decoding it.
     */
    DWORD last_status_entry;
    DWORD last_status_issued;
    /*
     * Was last_status_settled. It read the status a few instructions after the
     * command with an empty delay loop the compiler was free to delete, so
     * "still busy" meant nothing - a busy engine immediately after a command is
     * normal. Replaced rather than kept, because a diagnostic that cannot fail
     * informatively is worse than none.
     *
     * These are the S3 CRTC registers that decide where the engine's memory
     * origin is, sampled at fill time: CR6A is the current 64 KiB bank, CR35
     * the older bank register, CR51 carries display-start high bits, CR31 the
     * memory-configuration bits. GDI moves the bank for its own framebuffer
     * access; the 32-bit HAL uses the linear aperture and never does. If the
     * engine origin follows the bank, a non-zero bank displaces every fill -
     * potentially into the ~1.1 MiB of BARRY's 2 MiB that 800x600x16 does not
     * display, which is exactly what "the engine ran and nothing appeared"
     * looks like.
     */
    /*
     * CR50 is the one that matters, and it is the one the first pass at this
     * failed to read. The Trio32/Trio64 databook (DB014-B, "Extended System
     * Cont 1") gives it two fields the Graphics Engine uses and the CRTC does
     * not:
     *
     *   bits 7-6 plus bit 0  GE-SCR-W, "Graphics Engine Command Screen Pixel
     *                        Width" - bit 0 is the field's MSB:
     *                        000=1024 001=640 010=800 011=1280 100=1152 110=1600
     *   bits 5-4             PXL-LNGH, pixel length for Enhanced mode command
     *                        execution: 00=1 byte 01=2 bytes 11=4 bytes
     *
     * So the engine has its own screen width and its own pixel length, neither
     * of which is the display pitch this driver hands it. Nothing in this
     * driver programs CR50.
     */
    DWORD last_cr50;
    DWORD last_cr6a;
    DWORD last_cr51;
    DWORD last_cr31;
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
    /*
     * ADVFUNC_CNTL (4AE8H) as the last Trio64 operation's prepare read it,
     * and how many times the prepare had to set bit 0 (ENB EHFC) back. Bit 0
     * clearing under the driver was the 2026-08-27 hardware defect: the
     * engine executes with it clear and writes nothing, DOS-box activity
     * clears it on real silicon, and no other readable state changes. A
     * non-zero restore count on a healthy desktop means the environment is
     * actively flipping it and the per-operation guard is earning its keep.
     */
    DWORD last_advfunc;
    DWORD advfunc_restores;
    /*
     * Text, build 005. Ordinal 14 is a dispatcher of its own, with its own
     * call and decline tallies, because a text call that never reaches it
     * looks exactly like one it declined: the pixels are right either way.
     *
     * text_accepted counts calls routed through DIB_ExtTextOutExt with the
     * driver's callbacks attached; text_bitmaps and text_orects count what
     * those callbacks then put on the engine. text_fallbacks counts strings a
     * callback could not draw - the dispatcher notices and has DIB_ExtTextOut
     * redraw the whole string in software, so the pixels stay correct and the
     * count says how often the engine path gave up.
     *
     * text_reject_mask is a bitmask of reasons, accumulated over the run, in
     * the shape upload_reject_mask established:
     *   bit 1 not enabled   2 extent call (count < 0)   3 ETO_LEVEL_MODE
     *   bit 4 not the screen   5 busy or palette translate   6 depth
     *   bit 7 surface base not on a scan line   8 not a Trio64
     *   bit 9 callback device is not the screen   10 bitmap crosses 64 KiB
     *   bit 11 empty bitmap   12 coordinate out of the engine's range
     *   bit 13 a bounded wait expired
     * bit 0 is set when a string bitmap was expanded by the engine.
     * text_last_shape is WidthBytes << 16 | Height of the last bitmap seen.
     */
    DWORD text_calls;
    DWORD text_declines;
    DWORD text_accepted;
    DWORD text_bitmaps;
    DWORD text_orects;
    DWORD text_fallbacks;
    DWORD text_reject_mask;
    DWORD text_last_shape;
    /* GdiAccelSync as configured, and bounded idle waits that expired while
     * finishing an operation synchronously. */
    DWORD sync;
    DWORD sync_timeouts;
} V9X_GDI_STATS;

/*
 * V9X_GDITEXTDUMP output. Everything the DIB Engine passed to the last
 * string-bitmap callback, verbatim, plus the leading bytes of the bitmap it
 * pointed at. A diagnostic, kept because the first guest run of build 005
 * drew the left part of every string as garbage and the right part correctly,
 * and no counter could say which of the callback's inputs was misread.
 */
#define V9X_GDI_TEXT_DUMP_BYTES  256u

typedef struct v9x_gdi_text_dump {
    DWORD dwSize;
    DWORD sequence;          /* callbacks seen; 0 means the dump is empty */
    DWORD buffer;            /* the far pointer, selector:offset          */
    DWORD flags;
    DWORD background;
    DWORD foreground;
    DWORD x;                 /* as passed, sign-extended                  */
    DWORD y;
    DWORD width_bytes;
    DWORD height;
    DWORD clip_left;         /* as passed, sign-extended; clip_present 0 = NULL */
    DWORD clip_top;
    DWORD clip_right;
    DWORD clip_bottom;
    DWORD clip_present;
    DWORD device_width;      /* deWidth / deWidthBytes / deBitsPixel     */
    DWORD device_width_bytes;
    DWORD device_bpp;
    DWORD copied;            /* bytes of bitmap actually copied below     */
    BYTE bits[V9X_GDI_TEXT_DUMP_BYTES];
} V9X_GDI_TEXT_DUMP;

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
    /*
     * Three, because the software rasterizer publishes RGB565 alongside
     * ARGB1555 and ARGB4444 while the ViRGE publishes only the latter two -
     * the S3D texture unit selects its texel format from command bits 7:5 and
     * has no RGB565 mode (build\reference-vid_s3_virge.c:4564-4577), so the
     * two engines legitimately publish different lists into the same array.
     *
     * Widening this moves every member after it, so the 16-bit driver and the
     * 32-bit HAL must be built and deployed together. dwSize is stamped from
     * sizeof(V9X_DD_SHARED) and the block is allocated at 4096, which the
     * assertion at the end of this header still holds.
     */
    V9X_DDSURFACEDESC texture_formats[3];
    V9X_D3DHAL_CALLBACKS d3d_callbacks;
    V9X_D3D_DIAGNOSTICS d3d_diagnostics;
    V9X_VIDMEM heaps[1];
    /* How many of modes[] the 16-bit side filled in. Written before DriverInit
     * runs, which validates it and publishes it as info.dwNumModes. */
    DWORD mode_count;
    V9X_DDHALMODEINFO modes[V9X_DD_MODE_COUNT];
    V9X_DD_TRACE trace;
    V9X_D3D_DRAW_CENSUS census;
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
    sizeof(V9X_GDI_STATS) == 228 ? 1 : -1];
typedef char v9x_dd_assert_gdi_text_dump[
    sizeof(V9X_GDI_TEXT_DUMP) == 76 + 256 ? 1 : -1];
/* 576: counters[] is one WORD per trace id, and grows by two bytes with each
 * id added (574 when PRIMREJECT landed inside it, 576 for RENDERLOOP). The
 * whole header is pack(1), so that is the entire difference - there is no
 * padding to absorb it. The trace is the last field of the shared block, so
 * nothing after it moves; the snapshot's dwSize rejects a stale reader. */
typedef char v9x_dd_assert_trace[
    sizeof(V9X_DD_TRACE) == 576 ? 1 : -1];
/* Must match V9X_DD_SHARED_BYTES in src/display16/runtime.asm, which is the
 * size the 16-bit side DPMI-allocates and the limit it sets on the selector. */
typedef char v9x_dd_assert_shared_fits_dpmi_block[
    sizeof(V9X_DD_SHARED) <= 8192 ? 1 : -1];

/*
 * How much of Gen3's second aperture the driver maps, in bytes.
 *
 * A constant rather than a field in V9X_DD_ENGINE, and not by preference: the
 * assertion above is a hard 4096-byte bound on the shared block, and appending
 * a DWORD for this overflowed it. The number is a property of the hardware
 * rather than of a boot - the Gen3 GTT is 256 KiB and the Phase 2 inventory
 * read all 65536 of its PTEs twice to establish that - so carrying it per-boot
 * would have been a second place for a constant to live.
 *
 * docs\decisions\2026-09-12-intel-phase2-gtt-inventory.md
 */
#define V9X_DD_ENGINE_GTT_BYTES 0x00040000ul

#endif /* VELOCITY9X_WIN9X_DDRAW_ABI_H */
