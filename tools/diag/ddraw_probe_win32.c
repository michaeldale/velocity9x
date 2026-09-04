/*
 * DirectDraw presentation probe for the Velocity9x bring-up guest.
 *
 * Reproduces the exact presentation path used by fullscreen DirectDraw
 * applications (SetDisplayMode, flip-chain primary, Flip with DDFLIP_WAIT)
 * and records every HRESULT and timing to C:\V9XDIAG\V9XDD.INI so a host can
 * distinguish a mode-switch refusal, a vertical-blank wait, and raw
 * framebuffer write cost. ddraw.dll is loaded dynamically; the module keeps
 * the diagnostic-suite rule of runtime-free static imports.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/probe_counts.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_RESULT_PATH V9X_DIAG_DD_INI
#define V9X_SECTION     "Velocity9xDDraw"

#define V9X_DDSD_CAPS               0x00000001ul
#define V9X_DDSD_HEIGHT             0x00000002ul
#define V9X_DDSD_WIDTH              0x00000004ul
#define V9X_DDSD_BACKBUFFERCOUNT    0x00000020ul
#define V9X_DDSD_MIPMAPCOUNT        0x00020000ul
#define V9X_DDSD_PIXELFORMAT        0x00001000ul
/* Depth-surface creation on the DirectDraw 1 interface this probe uses: the
 * bit depth travels in DDSURFACEDESC's union slot, not in a pixel format.
 * DDPF_ZBUFFER is the later spelling and is not what CreateSurface wants
 * here. */
#define V9X_DDSD_ZBUFFERBITDEPTH    0x00000040ul
#define V9X_DDSCAPS_BACKBUFFER      0x00000004ul
#define V9X_DDSCAPS_COMPLEX         0x00000008ul
#define V9X_DDSCAPS_FLIP            0x00000010ul
#define V9X_DDSCAPS_OFFSCREENPLAIN  0x00000040ul
#define V9X_DDSCAPS_PRIMARYSURFACE  0x00000200ul
#define V9X_DDSCAPS_SYSTEMMEMORY    0x00000800ul
#define V9X_DDSCAPS_TEXTURE         0x00001000ul
#define V9X_DDSCAPS_3DDEVICE        0x00002000ul
#define V9X_DDSCAPS_VIDEOMEMORY     0x00004000ul
#define V9X_DDSCAPS_MIPMAP          0x00400000ul
#define V9X_DDSCAPS_ZBUFFER         0x00020000ul
#define V9X_DDSCL_FULLSCREEN        0x00000001ul
#define V9X_DDSCL_NORMAL            0x00000008ul
#define V9X_DDSCL_EXCLUSIVE         0x00000010ul
/* Without this, DirectDraw hides every mode below 480 lines - including
 * 640x400 - from EnumDisplayModes and rejects SetDisplayMode for them. */
#define V9X_DDSCL_ALLOWMODEX        0x00000040ul
#define V9X_DDFLIP_WAIT             0x00000001ul
#define V9X_DDBLT_COLORFILL          0x00000400ul
#define V9X_DDBLT_KEYSRC             0x00008000ul
/* DDRAW.H:2799, and not where its neighbours suggest. */
#define V9X_DDBLT_DEPTHFILL          0x02000000ul
#define V9X_DDCKEY_SRCBLT            0x00000008ul
#define V9X_DDBLT_WAIT               0x01000000ul
#define V9X_DDGBS_CANBLT              0x00000001ul
#define V9X_DDGBS_ISBLTDONE          0x00000002ul
#define V9X_DDWAITVB_BLOCKBEGIN     0x00000001ul
#define V9X_DDLOCK_WAIT             0x00000001ul
#define V9X_DDPCAPS_8BIT            0x00000004ul
#define V9X_DDPCAPS_ALLOW256        0x00000040ul
#define V9X_DDERR_WASSTILLDRAWING   0x8876021cul
#define V9X_DDERR_UNSUPPORTED       0x88760231ul
#define V9X_D3DPT_TRIANGLELIST               4ul
#define V9X_D3DVT_TLVERTEX                   3ul

typedef struct v9x_ddscaps {
    DWORD dwCaps;
} V9X_DDSCAPS;

typedef struct v9x_ddcolorkey {
    DWORD dwColorSpaceLowValue;
    DWORD dwColorSpaceHighValue;
} V9X_DDCOLORKEY;

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
    LPVOID lpSurface;
    V9X_DDCOLORKEY ddckCKDestOverlay;
    V9X_DDCOLORKEY ddckCKDestBlt;
    V9X_DDCOLORKEY ddckCKSrcOverlay;
    V9X_DDCOLORKEY ddckCKSrcBlt;
    V9X_DDPIXELFORMAT ddpfPixelFormat;
    V9X_DDSCAPS ddsCaps;
} V9X_DDSURFACEDESC;

struct v9x_dd;
struct v9x_dds;
struct v9x_d3d2;
struct v9x_d3d_device2;
struct v9x_d3d_texture2;
struct v9x_d3d_viewport2;

typedef struct v9x_d3d_transform_caps {
    DWORD dwSize;
    DWORD dwCaps;
} V9X_D3D_TRANSFORM_CAPS;

typedef struct v9x_d3d_lighting_caps {
    DWORD dwSize;
    DWORD dwCaps;
    DWORD dwLightingModel;
    DWORD dwNumLights;
} V9X_D3D_LIGHTING_CAPS;

typedef struct v9x_d3d_prim_caps {
    DWORD values[14];
} V9X_D3D_PRIM_CAPS;

typedef struct v9x_d3d_device_desc {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dcmColorModel;
    DWORD dwDevCaps;
    V9X_D3D_TRANSFORM_CAPS dtcTransformCaps;
    DWORD bClipping;
    V9X_D3D_LIGHTING_CAPS dlcLightingCaps;
    V9X_D3D_PRIM_CAPS dpcLineCaps;
    V9X_D3D_PRIM_CAPS dpcTriCaps;
    DWORD dwDeviceRenderBitDepth;
    DWORD dwDeviceZBufferBitDepth;
    DWORD dwMaxBufferSize;
    DWORD dwMaxVertexCount;
    DWORD dx5Caps[8];
} V9X_D3D_DEVICE_DESC;

typedef HRESULT (__stdcall *V9X_D3D_ENUM_CALLBACK)(
    GUID *, char *, char *, V9X_D3D_DEVICE_DESC *,
    V9X_D3D_DEVICE_DESC *, void *);

typedef struct v9x_d3d2_vtbl {
    HRESULT (__stdcall *QueryInterface)(struct v9x_d3d2 *, const void *,
                                        void **);
    ULONG (__stdcall *AddRef)(struct v9x_d3d2 *);
    ULONG (__stdcall *Release)(struct v9x_d3d2 *);
    HRESULT (__stdcall *EnumDevices)(struct v9x_d3d2 *,
                                     V9X_D3D_ENUM_CALLBACK, void *);
    HRESULT (__stdcall *CreateLight)(struct v9x_d3d2 *, void **, void *);
    HRESULT (__stdcall *CreateMaterial)(struct v9x_d3d2 *, void **, void *);
    HRESULT (__stdcall *CreateViewport)(struct v9x_d3d2 *, void **, void *);
    HRESULT (__stdcall *FindDevice)(struct v9x_d3d2 *, void *, void *);
    HRESULT (__stdcall *CreateDevice)(struct v9x_d3d2 *, const GUID *,
                                      struct v9x_dds *,
                                      struct v9x_d3d_device2 **);
} V9X_D3D2_VTBL;

typedef struct v9x_d3d_device2_vtbl {
    HRESULT (__stdcall *QueryInterface)(struct v9x_d3d_device2 *,
                                        const void *, void **);
    ULONG (__stdcall *AddRef)(struct v9x_d3d_device2 *);
    ULONG (__stdcall *Release)(struct v9x_d3d_device2 *);
    /* Declared rather than left as void *: CreateDevice returning S_OK does
     * not establish that it returned the device that was asked for. This is
     * the only way to ask the object which half of itself is populated. */
    HRESULT (__stdcall *GetCaps)(struct v9x_d3d_device2 *,
                                 V9X_D3D_DEVICE_DESC *,
                                 V9X_D3D_DEVICE_DESC *);
    HRESULT (__stdcall *SwapTextureHandles)(struct v9x_d3d_device2 *,
                                             struct v9x_d3d_texture2 *,
                                             struct v9x_d3d_texture2 *);
    void *GetStats;
    HRESULT (__stdcall *AddViewport)(struct v9x_d3d_device2 *,
                                     struct v9x_d3d_viewport2 *);
    HRESULT (__stdcall *DeleteViewport)(struct v9x_d3d_device2 *,
                                        struct v9x_d3d_viewport2 *);
    void *NextViewport;
    HRESULT (__stdcall *EnumTextureFormats)(struct v9x_d3d_device2 *,
                                             HRESULT (__stdcall *)(
                                                 V9X_DDSURFACEDESC *,
                                                 void *), void *);
    HRESULT (__stdcall *BeginScene)(struct v9x_d3d_device2 *);
    HRESULT (__stdcall *EndScene)(struct v9x_d3d_device2 *);
    void *GetDirect3D;
    HRESULT (__stdcall *SetCurrentViewport)(struct v9x_d3d_device2 *,
                                            struct v9x_d3d_viewport2 *);
    void *GetCurrentViewport;
    /* Declared rather than left as void *: this is how a depth buffer is
     * bound to an existing device. Attaching the surface and re-driving the
     * render target makes the runtime hand the driver lpDDSZ, which is what
     * the Windows 98 DDK's own ViRGE driver relies on (D3DCB2.C:45-68). */
    HRESULT (__stdcall *SetRenderTarget)(struct v9x_d3d_device2 *,
                                         struct v9x_dds *, DWORD);
    void *GetRenderTarget;
    void *Begin;
    void *BeginIndexed;
    void *Vertex;
    void *Index;
    void *End;
    void *GetRenderState;
    HRESULT (__stdcall *SetRenderState)(struct v9x_d3d_device2 *, DWORD,
                                        DWORD);
    void *GetLightState;
    void *SetLightState;
    void *SetTransform;
    void *GetTransform;
    void *MultiplyTransform;
    HRESULT (__stdcall *DrawPrimitive)(struct v9x_d3d_device2 *, DWORD,
                                       DWORD, void *, DWORD, DWORD);
    void *DrawIndexedPrimitive;
    void *SetClipStatus;
    void *GetClipStatus;
} V9X_D3D_DEVICE2_VTBL;

typedef struct v9x_d3d_texture2_vtbl {
    HRESULT (__stdcall *QueryInterface)(struct v9x_d3d_texture2 *,
                                        const void *, void **);
    ULONG (__stdcall *AddRef)(struct v9x_d3d_texture2 *);
    ULONG (__stdcall *Release)(struct v9x_d3d_texture2 *);
    HRESULT (__stdcall *GetHandle)(struct v9x_d3d_texture2 *,
                                   struct v9x_d3d_device2 *, DWORD *);
    HRESULT (__stdcall *PaletteChanged)(struct v9x_d3d_texture2 *,
                                        DWORD, DWORD);
    HRESULT (__stdcall *Load)(struct v9x_d3d_texture2 *,
                              struct v9x_d3d_texture2 *);
} V9X_D3D_TEXTURE2_VTBL;

typedef struct v9x_d3d_viewport2_vtbl {
    void *QueryInterface;
    void *AddRef;
    ULONG (__stdcall *Release)(struct v9x_d3d_viewport2 *);
    void *Initialize;
    void *GetViewport;
    void *SetViewport;
    void *TransformVertices;
    void *LightElements;
    void *SetBackground;
    void *GetBackground;
    void *SetBackgroundDepth;
    void *GetBackgroundDepth;
    void *Clear;
    void *AddLight;
    void *DeleteLight;
    void *NextLight;
    void *GetViewport2;
    HRESULT (__stdcall *SetViewport2)(struct v9x_d3d_viewport2 *, void *);
} V9X_D3D_VIEWPORT2_VTBL;

struct v9x_d3d_viewport2 {
    const V9X_D3D_VIEWPORT2_VTBL *vtbl;
};

typedef struct v9x_d3d_viewport_desc2 {
    DWORD dwSize;
    DWORD dwX;
    DWORD dwY;
    DWORD dwWidth;
    DWORD dwHeight;
    float dvClipX;
    float dvClipY;
    float dvClipWidth;
    float dvClipHeight;
    float dvMinZ;
    float dvMaxZ;
} V9X_D3D_VIEWPORT_DESC2;

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

#define V9X_D3DRENDERSTATE_TEXTUREHANDLE      1ul
#define V9X_D3DRENDERSTATE_CULLMODE          22ul
#define V9X_D3DCULL_NONE                      1ul
#define V9X_D3DCULL_CCW                       3ul
#define V9X_D3DRENDERSTATE_TEXTUREMAG        17ul
#define V9X_D3DRENDERSTATE_TEXTUREMIN        18ul
#define V9X_D3DRENDERSTATE_SRCBLEND          19ul
#define V9X_D3DRENDERSTATE_DESTBLEND        20ul
#define V9X_D3DRENDERSTATE_TEXTUREMAPBLEND   21ul
#define V9X_D3DRENDERSTATE_ALPHABLENDENABLE 27ul
#define V9X_D3DRENDERSTATE_FOGENABLE        28ul
#define V9X_D3DRENDERSTATE_SPECULARENABLE   29ul
#define V9X_D3DRENDERSTATE_FOGCOLOR         34ul
#define V9X_D3DRENDERSTATE_ZENABLE            7ul
#define V9X_D3DRENDERSTATE_ZWRITEENABLE      14ul
#define V9X_D3DRENDERSTATE_ZFUNC             23ul
#define V9X_D3DCMP_LESS                       2ul
#define V9X_D3DCMP_ALWAYS                     8ul
#define V9X_D3DBLEND_SRCALPHA                5ul
#define V9X_D3DBLEND_INVSRCALPHA             6ul
#define V9X_D3DBLEND_ZERO_F                  1ul
#define V9X_D3DBLEND_ONE_F                   2ul
#define V9X_D3DBLEND_DESTCOLOR               9ul
#define V9X_D3DRENDERSTATE_COLORKEYENABLE    41ul
#define V9X_D3DFILTER_NEAREST                 1ul
#define V9X_D3DFILTER_LINEAR                  2ul
#define V9X_D3DFILTER_MIPNEAREST              3ul
#define V9X_D3DFILTER_MIPLINEAR               4ul
#define V9X_D3DRENDERSTATE_TEXTUREADDRESS    3ul
#define V9X_D3DRENDERSTATE_COLORKEYENABLE_R  41ul
#define V9X_D3DTADDRESS_WRAP_R                1ul
#define V9X_D3DFILTER_LINEARMIPLINEAR         6ul
#define V9X_D3DTBLEND_COPY                    7ul
#define V9X_D3DTBLEND_MODULATE                2ul

struct v9x_d3d2 {
    const V9X_D3D2_VTBL *vtbl;
};

struct v9x_d3d_device2 {
    const V9X_D3D_DEVICE2_VTBL *vtbl;
};

struct v9x_d3d_texture2 {
    const V9X_D3D_TEXTURE2_VTBL *vtbl;
};

static const GUID v9x_iid_d3d2 = {
    0x6aae1ec1ul, 0x662a, 0x11d0,
    { 0x88, 0x9d, 0x00, 0xaa, 0x00, 0xbb, 0xb7, 0x6a }
};

/* DirectX 2/3-era Direct3D. A title of that vintage - Hellbender is one -
 * carries only this interface and creates its device by asking the render
 * target for the enumerated device GUID, not through IDirect3D2::CreateDevice. */
static const GUID v9x_iid_d3d = {
    0x3bba0080ul, 0x2421, 0x11cf,
    { 0xa3, 0x1a, 0x00, 0xaa, 0x00, 0xb9, 0x33, 0x56 }
};

static const GUID v9x_iid_d3d_hal = {
    0x84e63de0ul, 0x46aa, 0x11cf,
    { 0x81, 0x6f, 0x00, 0x00, 0xc0, 0x20, 0x15, 0x6e }
};

static const GUID v9x_iid_d3d_texture2 = {
    0x93281502ul, 0x8cf8, 0x11d0,
    { 0x89, 0xab, 0x00, 0xa0, 0xc9, 0x05, 0x41, 0x29 }
};

typedef struct v9x_d3d_enum_result {
    DWORD hal_found;
    DWORD flags;
    DWORD render_depth;
    DWORD count;
    GUID guid[8];
    char description[8][64];
    char name[8][64];
    V9X_D3D_DEVICE_DESC hardware[8];
    V9X_D3D_DEVICE_DESC software[8];
} V9X_D3D_ENUM_RESULT;

#define V9X_TEXTURE_ENUM_MAX 8

typedef struct v9x_texture_enum_result {
    DWORD count;
    DWORD rgb565;
    DWORD argb1555;
    DWORD argb4444;
    /* Every enumerated format, so a driver offering more than the ones this
     * probe recognises can still be compared against field by field. */
    DWORD bits[V9X_TEXTURE_ENUM_MAX];
    DWORD flags[V9X_TEXTURE_ENUM_MAX];
    DWORD red[V9X_TEXTURE_ENUM_MAX];
    DWORD green[V9X_TEXTURE_ENUM_MAX];
    DWORD blue[V9X_TEXTURE_ENUM_MAX];
    DWORD alpha[V9X_TEXTURE_ENUM_MAX];
} V9X_TEXTURE_ENUM_RESULT;

/* IDirectDraw version 1 method table, in vtable order. */
typedef struct v9x_dd_vtbl {
    HRESULT (__stdcall *QueryInterface)(struct v9x_dd *, const void *,
                                        void **);
    ULONG (__stdcall *AddRef)(struct v9x_dd *);
    ULONG (__stdcall *Release)(struct v9x_dd *);
    HRESULT (__stdcall *Compact)(struct v9x_dd *);
    HRESULT (__stdcall *CreateClipper)(struct v9x_dd *, DWORD, void **,
                                       void *);
    HRESULT (__stdcall *CreatePalette)(struct v9x_dd *, DWORD, void *,
                                       void **, void *);
    HRESULT (__stdcall *CreateSurface)(struct v9x_dd *,
                                       V9X_DDSURFACEDESC *,
                                       struct v9x_dds **, void *);
    HRESULT (__stdcall *DuplicateSurface)(struct v9x_dd *, struct v9x_dds *,
                                          struct v9x_dds **);
    HRESULT (__stdcall *EnumDisplayModes)(struct v9x_dd *, DWORD,
                                          V9X_DDSURFACEDESC *, void *,
                                          void *);
    HRESULT (__stdcall *EnumSurfaces)(struct v9x_dd *, DWORD,
                                      V9X_DDSURFACEDESC *, void *, void *);
    HRESULT (__stdcall *FlipToGDISurface)(struct v9x_dd *);
    HRESULT (__stdcall *GetCaps)(struct v9x_dd *, void *, void *);
    HRESULT (__stdcall *GetDisplayMode)(struct v9x_dd *,
                                        V9X_DDSURFACEDESC *);
    HRESULT (__stdcall *GetFourCCCodes)(struct v9x_dd *, DWORD *, DWORD *);
    HRESULT (__stdcall *GetGDISurface)(struct v9x_dd *, struct v9x_dds **);
    HRESULT (__stdcall *GetMonitorFrequency)(struct v9x_dd *, DWORD *);
    HRESULT (__stdcall *GetScanLine)(struct v9x_dd *, DWORD *);
    HRESULT (__stdcall *GetVerticalBlankStatus)(struct v9x_dd *, BOOL *);
    HRESULT (__stdcall *Initialize)(struct v9x_dd *, void *);
    HRESULT (__stdcall *RestoreDisplayMode)(struct v9x_dd *);
    HRESULT (__stdcall *SetCooperativeLevel)(struct v9x_dd *, HWND, DWORD);
    HRESULT (__stdcall *SetDisplayMode)(struct v9x_dd *, DWORD, DWORD,
                                        DWORD);
    HRESULT (__stdcall *WaitForVerticalBlank)(struct v9x_dd *, DWORD,
                                              HANDLE);
} V9X_DD_VTBL;

/* IDirectDrawPalette method table, in vtable order. */
typedef struct v9x_ddpal_vtbl {
    HRESULT (__stdcall *QueryInterface)(struct v9x_ddpal *, const void *,
                                        void **);
    ULONG (__stdcall *AddRef)(struct v9x_ddpal *);
    ULONG (__stdcall *Release)(struct v9x_ddpal *);
    HRESULT (__stdcall *GetCaps)(struct v9x_ddpal *, DWORD *);
    HRESULT (__stdcall *GetEntries)(struct v9x_ddpal *, DWORD, DWORD, DWORD,
                                    PALETTEENTRY *);
    HRESULT (__stdcall *Initialize)(struct v9x_ddpal *, struct v9x_dd *,
                                    DWORD, PALETTEENTRY *);
    HRESULT (__stdcall *SetEntries)(struct v9x_ddpal *, DWORD, DWORD, DWORD,
                                    PALETTEENTRY *);
} V9X_DDPAL_VTBL;

typedef struct v9x_ddpal {
    V9X_DDPAL_VTBL *vtbl;
} V9X_DDPAL;

/* IDirectDrawSurface version 1 method table, in vtable order. */
typedef struct v9x_dds_vtbl {
    HRESULT (__stdcall *QueryInterface)(struct v9x_dds *, const void *,
                                        void **);
    ULONG (__stdcall *AddRef)(struct v9x_dds *);
    ULONG (__stdcall *Release)(struct v9x_dds *);
    HRESULT (__stdcall *AddAttachedSurface)(struct v9x_dds *,
                                            struct v9x_dds *);
    HRESULT (__stdcall *AddOverlayDirtyRect)(struct v9x_dds *, RECT *);
    HRESULT (__stdcall *Blt)(struct v9x_dds *, RECT *, struct v9x_dds *,
                             RECT *, DWORD, void *);
    HRESULT (__stdcall *BltBatch)(struct v9x_dds *, void *, DWORD, DWORD);
    HRESULT (__stdcall *BltFast)(struct v9x_dds *, DWORD, DWORD,
                                 struct v9x_dds *, RECT *, DWORD);
    HRESULT (__stdcall *DeleteAttachedSurface)(struct v9x_dds *, DWORD,
                                               struct v9x_dds *);
    HRESULT (__stdcall *EnumAttachedSurfaces)(struct v9x_dds *, void *,
                                              void *);
    HRESULT (__stdcall *EnumOverlayZOrders)(struct v9x_dds *, DWORD, void *,
                                            void *);
    HRESULT (__stdcall *Flip)(struct v9x_dds *, struct v9x_dds *, DWORD);
    HRESULT (__stdcall *GetAttachedSurface)(struct v9x_dds *, V9X_DDSCAPS *,
                                            struct v9x_dds **);
    HRESULT (__stdcall *GetBltStatus)(struct v9x_dds *, DWORD);
    HRESULT (__stdcall *GetCaps)(struct v9x_dds *, V9X_DDSCAPS *);
    HRESULT (__stdcall *GetClipper)(struct v9x_dds *, void **);
    HRESULT (__stdcall *GetColorKey)(struct v9x_dds *, DWORD,
                                     V9X_DDCOLORKEY *);
    HRESULT (__stdcall *GetDC)(struct v9x_dds *, HDC *);
    HRESULT (__stdcall *GetFlipStatus)(struct v9x_dds *, DWORD);
    HRESULT (__stdcall *GetOverlayPosition)(struct v9x_dds *, LONG *,
                                            LONG *);
    HRESULT (__stdcall *GetPalette)(struct v9x_dds *, void **);
    HRESULT (__stdcall *GetPixelFormat)(struct v9x_dds *,
                                        V9X_DDPIXELFORMAT *);
    HRESULT (__stdcall *GetSurfaceDesc)(struct v9x_dds *,
                                        V9X_DDSURFACEDESC *);
    HRESULT (__stdcall *Initialize)(struct v9x_dds *, struct v9x_dd *,
                                    V9X_DDSURFACEDESC *);
    HRESULT (__stdcall *IsLost)(struct v9x_dds *);
    HRESULT (__stdcall *Lock)(struct v9x_dds *, RECT *,
                              V9X_DDSURFACEDESC *, DWORD, HANDLE);
    HRESULT (__stdcall *ReleaseDC)(struct v9x_dds *, HDC);
    HRESULT (__stdcall *Restore)(struct v9x_dds *);
    HRESULT (__stdcall *SetClipper)(struct v9x_dds *, void *);
    HRESULT (__stdcall *SetColorKey)(struct v9x_dds *, DWORD,
                                     V9X_DDCOLORKEY *);
    HRESULT (__stdcall *SetOverlayPosition)(struct v9x_dds *, LONG, LONG);
    HRESULT (__stdcall *SetPalette)(struct v9x_dds *, void *);
    HRESULT (__stdcall *Unlock)(struct v9x_dds *, void *);
    HRESULT (__stdcall *UpdateOverlay)(struct v9x_dds *, RECT *,
                                       struct v9x_dds *, RECT *, DWORD,
                                       void *);
    HRESULT (__stdcall *UpdateOverlayDisplay)(struct v9x_dds *, DWORD);
    HRESULT (__stdcall *UpdateOverlayZOrder)(struct v9x_dds *, DWORD,
                                             struct v9x_dds *);
} V9X_DDS_VTBL;

struct v9x_dd {
    const V9X_DD_VTBL *vtbl;
};

struct v9x_dds {
    const V9X_DDS_VTBL *vtbl;
};

typedef HRESULT (__stdcall *V9X_DDCREATE)(void *, struct v9x_dd **, void *);
typedef DWORD (__stdcall *V9X_TIMEGETTIME)(void);

static V9X_TIMEGETTIME v9x_time;

static int v9x_guid_equal(const GUID *left, const GUID *right)
{
    const BYTE *a = (const BYTE *)left;
    const BYTE *b = (const BYTE *)right;
    unsigned index;

    for (index = 0u; index < sizeof(GUID); ++index) {
        if (a[index] != b[index]) {
            return 0;
        }
    }
    return 1;
}

static HRESULT __stdcall v9x_enum_d3d_device(
    GUID *guid, char *description, char *name,
    V9X_D3D_DEVICE_DESC *hardware, V9X_D3D_DEVICE_DESC *software,
    void *context)
{
    V9X_D3D_ENUM_RESULT *result = (V9X_D3D_ENUM_RESULT *)context;
    DWORD index = result->count;

    if (index < 8ul) {
        if (guid != 0) {
            result->guid[index] = *guid;
        }
        if (description != 0) {
            lstrcpynA(result->description[index], description, 64);
        }
        if (name != 0) {
            lstrcpynA(result->name[index], name, 64);
        }
        if (hardware != 0) {
            result->hardware[index] = *hardware;
        }
        if (software != 0) {
            result->software[index] = *software;
        }
    }
    ++result->count;
    if (guid != 0 && hardware != 0 &&
        v9x_guid_equal(guid, &v9x_iid_d3d_hal)) {
        result->hal_found = 1ul;
        result->flags = hardware->dwFlags;
        result->render_depth = hardware->dwDeviceRenderBitDepth;
    }
    return 1l;
}

static HRESULT __stdcall v9x_enum_texture_format(
    V9X_DDSURFACEDESC *desc, void *context)
{
    V9X_TEXTURE_ENUM_RESULT *result = (V9X_TEXTURE_ENUM_RESULT *)context;

    if (desc != 0) {
        if (result->count < V9X_TEXTURE_ENUM_MAX) {
            DWORD i = result->count;

            result->bits[i] = desc->ddpfPixelFormat.dwRGBBitCount;
            result->flags[i] = desc->ddpfPixelFormat.dwFlags;
            result->red[i] = desc->ddpfPixelFormat.dwRBitMask;
            result->green[i] = desc->ddpfPixelFormat.dwGBitMask;
            result->blue[i] = desc->ddpfPixelFormat.dwBBitMask;
            result->alpha[i] = desc->ddpfPixelFormat.dwRGBAlphaBitMask;
        }
        ++result->count;
        if ((desc->ddpfPixelFormat.dwFlags & 0x00000041ul) ==
                0x00000041ul &&
            desc->ddpfPixelFormat.dwRGBBitCount == 16ul &&
            desc->ddpfPixelFormat.dwRBitMask == 0x00000f00ul &&
            desc->ddpfPixelFormat.dwGBitMask == 0x000000f0ul &&
            desc->ddpfPixelFormat.dwBBitMask == 0x0000000ful &&
            desc->ddpfPixelFormat.dwRGBAlphaBitMask == 0x0000f000ul) {
            result->argb4444 = 1ul;
        }
        if ((desc->ddpfPixelFormat.dwFlags & 0x00000040ul) != 0ul &&
            desc->ddpfPixelFormat.dwRGBBitCount == 16ul &&
            desc->ddpfPixelFormat.dwRBitMask == 0x0000f800ul &&
            desc->ddpfPixelFormat.dwGBitMask == 0x000007e0ul &&
            desc->ddpfPixelFormat.dwBBitMask == 0x0000001ful) {
            result->rgb565 = 1ul;
        }
        if ((desc->ddpfPixelFormat.dwFlags & 0x00000041ul) ==
                0x00000041ul &&
            desc->ddpfPixelFormat.dwRGBBitCount == 16ul &&
            desc->ddpfPixelFormat.dwRBitMask == 0x00007c00ul &&
            desc->ddpfPixelFormat.dwGBitMask == 0x000003e0ul &&
            desc->ddpfPixelFormat.dwBBitMask == 0x0000001ful &&
            desc->ddpfPixelFormat.dwRGBAlphaBitMask == 0x00008000ul) {
            result->argb1555 = 1ul;
        }
    }
    return 1l;
}

static int v9x_has_switch(const char *option)
{
    const char *command_line = GetCommandLineA();
    unsigned offset;
    unsigned index;

    for (offset = 0u; command_line[offset] != '\0'; ++offset) {
        for (index = 0u; option[index] != '\0'; ++index) {
            char left = command_line[offset + index];
            char right = option[index];

            if (left >= 'A' && left <= 'Z') {
                left = (char)(left + ('a' - 'A'));
            }
            if (left != right) {
                break;
            }
        }
        if (option[index] == '\0') {
            return 1;
        }
    }
    return 0;
}

static void v9x_zero(void *block, unsigned length)
{
    unsigned char *bytes = (unsigned char *)block;

    while (length-- != 0u) {
        *bytes++ = 0u;
    }
}

static void v9x_uint_text(char *text, DWORD value)
{
    char reverse[12];
    int count = 0;
    int index;

    do {
        reverse[count++] = (char)('0' + (value % 10ul));
        value /= 10ul;
    } while (value != 0ul);
    for (index = 0; index < count; ++index) {
        text[index] = reverse[count - index - 1];
    }
    text[count] = '\0';
}

static void v9x_hex_text(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    int index;

    text[0] = '0';
    text[1] = 'x';
    for (index = 0; index < 8; ++index) {
        text[2 + index] = digits[(value >> ((7 - index) * 4)) & 0xful];
    }
    text[10] = '\0';
}

static void v9x_write_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_SECTION, key, value, V9X_RESULT_PATH);
}

static void v9x_write_uint(const char *key, DWORD value)
{
    char text[12];

    v9x_uint_text(text, value);
    v9x_write_text(key, text);
}

static void v9x_write_hresult(const char *key, HRESULT value)
{
    char text[11];

    v9x_hex_text(text, (DWORD)value);
    v9x_write_text(key, text);
}

/*
 * Windows caches .INI writes, and a display mode change immediately before
 * process exit discards the cached tail. Flush explicitly before exiting.
 */
static void v9x_flush_results(void)
{
    WritePrivateProfileStringA(0, 0, 0, V9X_RESULT_PATH);
}

static void v9x_write_d3d_devices(const V9X_D3D_ENUM_RESULT *result)
{
    char key[64];
    DWORD count = result->count < 8ul ? result->count : 8ul;
    DWORD index;

    v9x_write_uint("D3DDeviceCount", result->count);
    for (index = 0ul; index < count; ++index) {
        const V9X_D3D_DEVICE_DESC *hw = &result->hardware[index];
        const V9X_D3D_DEVICE_DESC *sw = &result->software[index];
        const DWORD *tri = hw->dpcTriCaps.values;

        wsprintfA(key, "D3DDevice%luDescription", index);
        v9x_write_text(key, result->description[index]);
        wsprintfA(key, "D3DDevice%luName", index);
        v9x_write_text(key, result->name[index]);
        wsprintfA(key, "D3DDevice%luGuidData1", index);
        v9x_write_uint(key, result->guid[index].Data1);
        wsprintfA(key, "D3DDevice%luHwFlags", index);
        v9x_write_uint(key, hw->dwFlags);
        wsprintfA(key, "D3DDevice%luHwColorModel", index);
        v9x_write_uint(key, hw->dcmColorModel);
        wsprintfA(key, "D3DDevice%luHwDevCaps", index);
        v9x_write_uint(key, hw->dwDevCaps);
        wsprintfA(key, "D3DDevice%luHwClipping", index);
        v9x_write_uint(key, hw->bClipping);
        wsprintfA(key, "D3DDevice%luHwTriMisc", index);
        v9x_write_uint(key, tri[1]);
        wsprintfA(key, "D3DDevice%luHwTriRaster", index);
        v9x_write_uint(key, tri[2]);
        wsprintfA(key, "D3DDevice%luHwTriZCmp", index);
        v9x_write_uint(key, tri[3]);
        wsprintfA(key, "D3DDevice%luHwTriSrcBlend", index);
        v9x_write_uint(key, tri[4]);
        wsprintfA(key, "D3DDevice%luHwTriDestBlend", index);
        v9x_write_uint(key, tri[5]);
        wsprintfA(key, "D3DDevice%luHwTriAlphaCmp", index);
        v9x_write_uint(key, tri[6]);
        wsprintfA(key, "D3DDevice%luHwTriShade", index);
        v9x_write_uint(key, tri[7]);
        wsprintfA(key, "D3DDevice%luHwTriTexture", index);
        v9x_write_uint(key, tri[8]);
        wsprintfA(key, "D3DDevice%luHwTriFilter", index);
        v9x_write_uint(key, tri[9]);
        wsprintfA(key, "D3DDevice%luHwTriBlend", index);
        v9x_write_uint(key, tri[10]);
        wsprintfA(key, "D3DDevice%luHwTriAddress", index);
        v9x_write_uint(key, tri[11]);
        wsprintfA(key, "D3DDevice%luHwRenderDepth", index);
        v9x_write_uint(key, hw->dwDeviceRenderBitDepth);
        wsprintfA(key, "D3DDevice%luHwZDepth", index);
        v9x_write_uint(key, hw->dwDeviceZBufferBitDepth);
        wsprintfA(key, "D3DDevice%luHwMaxBuffer", index);
        v9x_write_uint(key, hw->dwMaxBufferSize);
        wsprintfA(key, "D3DDevice%luHwMaxVertices", index);
        v9x_write_uint(key, hw->dwMaxVertexCount);
        wsprintfA(key, "D3DDevice%luSwFlags", index);
        v9x_write_uint(key, sw->dwFlags);
    }
}

/*
 * DirectDraw runtime-internals dump.
 *
 * An IDirectDraw interface pointer is a DDRAWI_DIRECTDRAW_INT whose second
 * field is the DDRAWI_DIRECTDRAW_LCL, whose second field is the shared
 * DDRAWI_DIRECTDRAW_GBL. Both layouts are published in the Windows 98 DDK
 * (DDRAWI.H) with byte offsets, so the fields DDHAL_SetInfo is supposed to
 * populate can be read back directly. This distinguishes "the runtime never
 * recorded the HAL" from "the runtime recorded it and then disabled it".
 */
#define V9X_GBL_FLAGS            0x004u
#define V9X_GBL_CAPS_SIZE        0x00cu
#define V9X_GBL_CAPS             0x010u
#define V9X_GBL_CAPS_VIDMEMTOTAL 0x048u
#define V9X_GBL_CAPS_ROPS7       0x08cu
#define V9X_GBL_CAPS_DDSCAPS     0x090u
#define V9X_GBL_DDCBTMP          0x170u
#define V9X_GBL_MONITORFREQ      0x18cu
#define V9X_GBL_HELCAPS          0x194u
#define V9X_GBL_VMI_PRIMARY      0x3a4u
#define V9X_GBL_VMI_WIDTH        0x3acu
#define V9X_GBL_VMI_HEIGHT       0x3b0u
#define V9X_GBL_VMI_PITCH        0x3b4u
#define V9X_GBL_VMI_NUMHEAPS     0x3ecu
#define V9X_GBL_VMI_PVMLIST      0x3f0u
#define V9X_GBL_DRIVERHANDLE     0x3f4u
#define V9X_GBL_MODEINDEX        0x3fcu
#define V9X_GBL_NUMMODES         0x40cu
#define V9X_GBL_MODEINFO         0x410u
#define V9X_GBL_DRIVERNAME       0x438u
#define V9X_GBL_PDEVICE          0x460u
#define V9X_GBL_HINSTANCE        0x46cu
#define V9X_GBL_D3DGLOBAL        0x478u
#define V9X_GBL_BOTHCAPS         0x484u
#define V9X_GBL_SIZE             0x608u

/* DDHAL_CALLBACKS: the driver-supplied tables first, then the resolved HAL
 * tables, then the HEL tables. dwFlags is the second DWORD of each. */
#define V9X_CB_DRV_DD_FLAGS      0x004u
#define V9X_CB_DRV_SURF_FLAGS    0x034u
#define V9X_CB_HAL_DD_FLAGS      0x088u
#define V9X_CB_HAL_SURF_FLAGS    0x0b8u
#define V9X_CB_HEL_DD_FLAGS      0x10cu
#define V9X_CB_SIZE              0x140u

#define V9X_DDRAWI_NOHARDWARE    0x00000100ul
#define V9X_DDRAWI_DISPLAYDRV    0x00000020ul

static DWORD v9x_peek(const BYTE *base, unsigned offset)
{
    return *(const DWORD *)(base + offset);
}

static void v9x_write_field(const char *key, const BYTE *base,
                            unsigned offset)
{
    char text[11];

    v9x_hex_text(text, v9x_peek(base, offset));
    v9x_write_text(key, text);
}

/* One INI line per 8 DWORDs so an unexpected runtime layout can still be
 * decoded on the host without another guest round. */
static void v9x_write_raw(const char *prefix, const BYTE *base,
                          unsigned offset, unsigned dwords)
{
    char key[32];
    char line[128];
    char cell[11];
    unsigned index;
    unsigned column;
    unsigned length;
    unsigned copy;

    for (index = 0u; index < dwords; index += 8u) {
        wsprintfA(key, "%s%04X", prefix, offset + index * 4u);
        length = 0u;
        for (column = 0u; column < 8u && index + column < dwords; ++column) {
            v9x_hex_text(cell, v9x_peek(base, offset + (index + column) * 4u));
            if (length != 0u) {
                line[length++] = ' ';
            }
            for (copy = 2u; cell[copy] != '\0'; ++copy) {
                line[length++] = cell[copy];
            }
        }
        line[length] = '\0';
        v9x_write_text(key, line);
    }
}

/* Compose "<prefix><suffix>" into an INI key. Defined below; used by the
 * globals dump above it. */
static void v9x_compose_key(char *key, const char *prefix,
                            const char *suffix);

static void v9x_write_ddraw_globals(struct v9x_dd *ddraw)
{
    const BYTE *object = (const BYTE *)ddraw;
    const BYTE *lcl;
    const BYTE *gbl;
    const BYTE *callbacks;
    char name[16];
    unsigned index;

    if (ddraw == 0 || IsBadReadPtr(object, 8u)) {
        v9x_write_text("GblState", "no-object");
        return;
    }
    lcl = (const BYTE *)v9x_peek(object, 4u);
    if (lcl == 0 || IsBadReadPtr(lcl, 8u)) {
        v9x_write_text("GblState", "no-local");
        return;
    }
    gbl = (const BYTE *)v9x_peek(lcl, 4u);
    if (gbl == 0 || IsBadReadPtr(gbl, V9X_GBL_SIZE)) {
        v9x_write_text("GblState", "no-global");
        return;
    }
    v9x_write_text("GblState", "ok");

    v9x_write_field("GblFlags", gbl, V9X_GBL_FLAGS);
    v9x_write_uint("GblNoHardware",
                   (v9x_peek(gbl, V9X_GBL_FLAGS) & V9X_DDRAWI_NOHARDWARE)
                       != 0ul ? 1ul : 0ul);
    v9x_write_uint("GblDisplayDrv",
                   (v9x_peek(gbl, V9X_GBL_FLAGS) & V9X_DDRAWI_DISPLAYDRV)
                       != 0ul ? 1ul : 0ul);
    v9x_write_field("GblCapsSize", gbl, V9X_GBL_CAPS_SIZE);
    v9x_write_field("GblHalCaps", gbl, V9X_GBL_CAPS);
    v9x_write_field("GblHalDdsCaps", gbl, V9X_GBL_CAPS_DDSCAPS);
    v9x_write_field("GblHalRops7", gbl, V9X_GBL_CAPS_ROPS7);
    v9x_write_field("GblHalVidMemTotal", gbl, V9X_GBL_CAPS_VIDMEMTOTAL);
    v9x_write_field("GblHelCaps", gbl, V9X_GBL_HELCAPS);
    v9x_write_field("GblBothCaps", gbl, V9X_GBL_BOTHCAPS);
    v9x_write_field("GblMonitorFreq", gbl, V9X_GBL_MONITORFREQ);
    v9x_write_field("GblPrimary", gbl, V9X_GBL_VMI_PRIMARY);
    v9x_write_field("GblDisplayWidth", gbl, V9X_GBL_VMI_WIDTH);
    v9x_write_field("GblDisplayHeight", gbl, V9X_GBL_VMI_HEIGHT);
    v9x_write_field("GblDisplayPitch", gbl, V9X_GBL_VMI_PITCH);
    v9x_write_field("GblNumHeaps", gbl, V9X_GBL_VMI_NUMHEAPS);
    v9x_write_field("GblPvmList", gbl, V9X_GBL_VMI_PVMLIST);
    v9x_write_field("GblDriverHandle", gbl, V9X_GBL_DRIVERHANDLE);
    v9x_write_field("GblModeIndex", gbl, V9X_GBL_MODEINDEX);
    v9x_write_field("GblNumModes", gbl, V9X_GBL_NUMMODES);
    v9x_write_field("GblModeInfo", gbl, V9X_GBL_MODEINFO);
    /*
     * Dump the mode table DirectDraw actually holds. The driver publishes its
     * list through DDHALINFO, but EnumDisplayModes reports fewer entries than
     * the driver offers, so it matters whether a missing mode never reached
     * DirectDraw or reached it and was filtered afterwards.
     */
    {
        const BYTE *modes = (const BYTE *)v9x_peek(gbl, V9X_GBL_MODEINFO);
        DWORD count = v9x_peek(gbl, V9X_GBL_NUMMODES);
        DWORD index;

        if (count > 24ul) {
            count = 24ul;
        }
        for (index = 0ul; modes != 0 && index < count; ++index) {
            const BYTE *entry = modes + index * 36ul;
            char key[32];
            char index_text[12];
            char base[32];
            int end;

            if (IsBadReadPtr(entry, 36u)) {
                break;
            }
            v9x_uint_text(index_text, index);
            v9x_compose_key(base, "GblMode", index_text);
            for (end = 0; base[end] != '\0'; ++end) {
                key[end] = base[end];
            }
            key[end + 1] = '\0';
            key[end] = 'W';
            v9x_write_uint(key, *(const DWORD *)(entry + 0));
            key[end] = 'H';
            v9x_write_uint(key, *(const DWORD *)(entry + 4));
            key[end] = 'B';
            v9x_write_uint(key, *(const DWORD *)(entry + 12));
        }
    }
    v9x_write_field("GblPDevice", gbl, V9X_GBL_PDEVICE);
    v9x_write_field("GblHInstance", gbl, V9X_GBL_HINSTANCE);
    v9x_write_field("GblD3DGlobal", gbl, V9X_GBL_D3DGLOBAL);

    for (index = 0u; index < sizeof(name) - 1u; ++index) {
        name[index] = (char)gbl[V9X_GBL_DRIVERNAME + index];
        if (name[index] == '\0') {
            break;
        }
    }
    name[sizeof(name) - 1u] = '\0';
    v9x_write_text("GblDriverName", name);

    callbacks = (const BYTE *)v9x_peek(gbl, V9X_GBL_DDCBTMP);
    v9x_write_field("GblDDCBtmp", gbl, V9X_GBL_DDCBTMP);
    if (callbacks == 0 || IsBadReadPtr(callbacks, V9X_CB_SIZE)) {
        v9x_write_text("GblCallbackState", "unreadable");
        return;
    }
    v9x_write_text("GblCallbackState", "ok");
    v9x_write_field("CbDrvDdFlags", callbacks, V9X_CB_DRV_DD_FLAGS);
    v9x_write_field("CbDrvSurfFlags", callbacks, V9X_CB_DRV_SURF_FLAGS);
    v9x_write_field("CbHalDdFlags", callbacks, V9X_CB_HAL_DD_FLAGS);
    v9x_write_field("CbHalSurfFlags", callbacks, V9X_CB_HAL_SURF_FLAGS);
    v9x_write_field("CbHelDdFlags", callbacks, V9X_CB_HEL_DD_FLAGS);

    /* Raw windows so a layout surprise is still decodable on the host. */
    v9x_write_raw("GblRaw", gbl, 0x000u, 8u);
    v9x_write_raw("GblRaw", gbl, 0x140u, 24u);
    v9x_write_raw("GblRaw", gbl, 0x3a0u, 32u);
    v9x_write_raw("GblRaw", gbl, 0x440u, 24u);
    v9x_write_raw("CbRaw", callbacks, 0x000u, 8u);
    v9x_write_raw("CbRaw", callbacks, 0x080u, 16u);
}

/*
 * Palettized presentation at one resolution, the path Doom95 uses.
 *
 * Doom asks DirectDraw for a low-resolution palettized mode, attaches a
 * 256-entry palette and writes one index byte per pixel. If the driver hands
 * back a 16-bpp primary instead, those index bytes are read as half as many
 * RGB565 pixels: the picture ends up half as wide with garbage colours,
 * which is what the guest showed. Recording the depth and pitch actually
 * delivered, plus a known index read back through both the surface and the
 * GDI screen DC, separates a depth failure from a palette failure and shows
 * which resolutions survive the round trip.
 */
static void v9x_pal8_mode_test(struct v9x_dd *ddraw, const char *prefix,
                               DWORD width, DWORD height);

/* Dump every mode DirectDraw enumerates, so a resolution the driver never
 * published can be told apart from one it published and mis-programmed. */
static void v9x_enum_modes(struct v9x_dd *ddraw);


/* Compose "<prefix><suffix>" into an INI key. */
static void v9x_compose_key(char *key, const char *prefix, const char *suffix)
{
    int offset = 0;
    int index;

    for (index = 0; prefix[index] != '\0'; ++index) {
        key[offset++] = prefix[index];
    }
    for (index = 0; suffix[index] != '\0'; ++index) {
        key[offset++] = suffix[index];
    }
    key[offset] = '\0';
}

static void v9x_write_mode(const char *prefix,
                           const V9X_DDSURFACEDESC *desc)
{
    char key[32];
    int offset = 0;
    int index;

    for (index = 0; prefix[index] != '\0'; ++index) {
        key[offset++] = prefix[index];
    }
    key[offset] = 'W';
    key[offset + 1] = '\0';
    v9x_write_uint(key, desc->dwWidth);
    key[offset] = 'H';
    v9x_write_uint(key, desc->dwHeight);
    key[offset] = 'B';
    key[offset + 1] = 'p';
    key[offset + 2] = 'p';
    key[offset + 3] = '\0';
    v9x_write_uint(key, desc->ddpfPixelFormat.dwRGBBitCount);
}

static DWORD v9x_enum_mode_count;

static HRESULT __stdcall v9x_enum_mode_callback(V9X_DDSURFACEDESC *desc,
                                                void *context)
{
    char key[32];
    char index_text[12];

    (void)context;
    if (v9x_enum_mode_count < 32ul) {
        v9x_uint_text(index_text, v9x_enum_mode_count);
        v9x_compose_key(key, "EnumMode", index_text);
        v9x_write_mode(key, desc);
    }
    ++v9x_enum_mode_count;
    return 1l;
}

static void v9x_enum_modes(struct v9x_dd *ddraw)
{
    DEVMODEA device_mode;
    DWORD index;
    DWORD written = 0ul;

    v9x_enum_mode_count = 0ul;
    ddraw->vtbl->EnumDisplayModes(ddraw, 0ul, 0,
                                  0, (void *)v9x_enum_mode_callback);
    v9x_write_uint("EnumModeCount", v9x_enum_mode_count);

    /*
     * GDI's own list, for comparison. DirectDraw builds its mode list from
     * the driver's DDHALINFO, but only publishes the entries GDI will also
     * accept, so a mode present here and missing above is being filtered by
     * DirectDraw rather than never offered by the driver.
     */
    for (index = 0ul; index < 64ul; ++index) {
        char key[32];
        char index_text[12];

        v9x_zero(&device_mode, sizeof(device_mode));
        device_mode.dmSize = sizeof(device_mode);
        if (!EnumDisplaySettingsA(0, index, &device_mode)) {
            break;
        }
        if (written < 32ul) {
            char base[32];
            int end;

            v9x_uint_text(index_text, written);
            v9x_compose_key(base, "GdiMode", index_text);
            for (end = 0; base[end] != '\0'; ++end) {
                key[end] = base[end];
            }
            key[end + 1] = '\0';
            key[end] = 'W';
            v9x_write_uint(key, device_mode.dmPelsWidth);
            key[end] = 'H';
            v9x_write_uint(key, device_mode.dmPelsHeight);
            key[end] = 'B';
            v9x_write_uint(key, device_mode.dmBitsPerPel);
        }
        ++written;
    }
    v9x_write_uint("GdiModeCount", written);
}

static void v9x_pal8_mode_test(struct v9x_dd *ddraw, const char *prefix,
                               DWORD width, DWORD height)
{
    V9X_DDSURFACEDESC desc;
    struct v9x_dds *primary = 0;
    V9X_DDPAL *palette = 0;
    PALETTEENTRY entries[256];
    char key[40];
    HRESULT hr;
    unsigned index;
    unsigned probe_x = (unsigned)(width / 4ul);
    unsigned probe_y = (unsigned)(height / 4ul);

    v9x_compose_key(key, prefix, "SetModeHr");
    hr = ddraw->vtbl->SetDisplayMode(ddraw, width, height, 8ul);
    v9x_write_hresult(key, hr);
    if (hr != 0) {
        return;
    }

    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    if (ddraw->vtbl->GetDisplayMode(ddraw, &desc) == 0) {
        v9x_compose_key(key, prefix, "Mode");
        v9x_write_mode(key, &desc);
    }

    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = V9X_DDSD_CAPS;
    desc.ddsCaps.dwCaps = V9X_DDSCAPS_PRIMARYSURFACE;
    hr = ddraw->vtbl->CreateSurface(ddraw, &desc, &primary, 0);
    v9x_compose_key(key, prefix, "PrimaryHr");
    v9x_write_hresult(key, hr);
    if (hr != 0) {
        return;
    }

    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    if (primary->vtbl->GetSurfaceDesc(primary, &desc) == 0) {
        v9x_compose_key(key, prefix, "Primary");
        v9x_write_mode(key, &desc);
        v9x_compose_key(key, prefix, "PrimaryPitch");
        v9x_write_uint(key, (DWORD)desc.lPitch);
    }

    /* A ramp with a distinctive entry at index 40: pure blue. */
    for (index = 0u; index < 256u; ++index) {
        entries[index].peRed = (BYTE)index;
        entries[index].peGreen = (BYTE)(255u - index);
        entries[index].peBlue = (BYTE)((index * 3u) & 0xFFu);
        entries[index].peFlags = 0u;
    }
    entries[40].peRed = 0u;
    entries[40].peGreen = 0u;
    entries[40].peBlue = 255u;

    hr = ddraw->vtbl->CreatePalette(ddraw,
                                    V9X_DDPCAPS_8BIT | V9X_DDPCAPS_ALLOW256,
                                    entries, (void **)&palette, 0);
    v9x_compose_key(key, prefix, "CreatePaletteHr");
    v9x_write_hresult(key, hr);
    if (hr == 0) {
        hr = primary->vtbl->SetPalette(primary, palette);
        v9x_compose_key(key, prefix, "SetPaletteHr");
        v9x_write_hresult(key, hr);
    }

    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = primary->vtbl->Lock(primary, 0, &desc, V9X_DDLOCK_WAIT, 0);
    v9x_compose_key(key, prefix, "LockHr");
    v9x_write_hresult(key, hr);
    if (hr == 0 && desc.lpSurface != 0) {
        BYTE FAR *base = (BYTE FAR *)desc.lpSurface;
        unsigned y;

        for (y = probe_y; y < probe_y + 40u; ++y) {
            BYTE FAR *row = base + y * (DWORD)desc.lPitch;
            unsigned x;

            for (x = probe_x; x < probe_x + 40u; ++x) {
                row[x] = 40u;
            }
        }
        primary->vtbl->Unlock(primary, desc.lpSurface);

        v9x_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        if (primary->vtbl->Lock(primary, 0, &desc, V9X_DDLOCK_WAIT, 0) == 0) {
            BYTE FAR *row = (BYTE FAR *)desc.lpSurface +
                            (DWORD)(probe_y + 10u) * (DWORD)desc.lPitch;

            v9x_compose_key(key, prefix, "ReadIndex");
            v9x_write_uint(key, (DWORD)row[probe_x + 10u]);
            primary->vtbl->Unlock(primary, desc.lpSurface);
        }
    }

    {
        HDC screen = GetDC(0);

        v9x_compose_key(key, prefix, "ScreenBpp");
        v9x_write_uint(key, (DWORD)GetDeviceCaps(screen, BITSPIXEL));
        v9x_compose_key(key, prefix, "ScreenW");
        v9x_write_uint(key, (DWORD)GetDeviceCaps(screen, HORZRES));
        v9x_compose_key(key, prefix, "ScreenH");
        v9x_write_uint(key, (DWORD)GetDeviceCaps(screen, VERTRES));
        v9x_compose_key(key, prefix, "ScreenPixel");
        v9x_write_uint(key, (DWORD)GetPixel(screen, (int)(probe_x + 10u),
                                            (int)(probe_y + 10u)));
        ReleaseDC(0, screen);
    }

    if (palette != 0) {
        palette->vtbl->Release(palette);
    }
    primary->vtbl->Release(primary);
}

static void v9x_fill_surface(struct v9x_dds *surface, DWORD pattern)
{
    V9X_DDSURFACEDESC desc;
    HRESULT hr;
    DWORD FAR *pixels;
    DWORD count;

    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = surface->vtbl->Lock(surface, 0, &desc, V9X_DDLOCK_WAIT, 0);
    if (hr != 0) {
        return;
    }
    pixels = (DWORD *)desc.lpSurface;
    count = ((DWORD)desc.lPitch * desc.dwHeight) / 4ul;
    while (count-- != 0ul) {
        *pixels++ = pattern;
    }
    surface->vtbl->Unlock(surface, 0);
}

/*
 * Left half of every row one 16-bit texel, right half another. A solid fill
 * cannot tell a sampler that reads the right texture from one that reads it
 * with the wrong stride, size or level - every texel is the same - and that
 * is exactly what the large-texture rungs need to tell apart.
 */
static void v9x_fill_surface_halves(struct v9x_dds *surface, WORD left,
                                    WORD right)
{
    V9X_DDSURFACEDESC desc;
    HRESULT hr;
    BYTE FAR *row;
    DWORD y;
    DWORD x;

    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = surface->vtbl->Lock(surface, 0, &desc, V9X_DDLOCK_WAIT, 0);
    if (hr != 0) {
        return;
    }
    row = (BYTE FAR *)desc.lpSurface;
    for (y = 0ul; y < desc.dwHeight; ++y) {
        WORD FAR *texel = (WORD FAR *)row;

        for (x = 0ul; x < desc.dwWidth; ++x) {
            texel[x] = x < desc.dwWidth / 2ul ? left : right;
        }
        row += desc.lPitch;
    }
    surface->vtbl->Unlock(surface, 0);
}

/*
 * Every render state a rung may leave behind, put back to what Direct3D
 * starts a device with. Called at the start of each rung group and of each
 * matrix cell, so a key means one draw under one known state. Before this
 * existed, the first probe run after a reboot twice read rungs as undrawn
 * that the next run read correctly - a texture handle or a blend left on by
 * the previous rung, landing on whichever rung followed. Depth state is
 * left alone: the depth ladders own it and reset it themselves.
 */
static void v9x_probe_reset_state(struct v9x_d3d_device2 *device,
                                  V9X_D3DTLVERTEX *triangle)
{
    (void)device->vtbl->SetRenderState(device,
        V9X_D3DRENDERSTATE_TEXTUREHANDLE, 0ul);
    (void)device->vtbl->SetRenderState(device,
        V9X_D3DRENDERSTATE_TEXTUREMAPBLEND, V9X_D3DTBLEND_MODULATE);
    (void)device->vtbl->SetRenderState(device,
        V9X_D3DRENDERSTATE_ALPHABLENDENABLE, 0ul);
    (void)device->vtbl->SetRenderState(device,
        V9X_D3DRENDERSTATE_SRCBLEND, V9X_D3DBLEND_ONE_F);
    (void)device->vtbl->SetRenderState(device,
        V9X_D3DRENDERSTATE_DESTBLEND, V9X_D3DBLEND_ZERO_F);
    (void)device->vtbl->SetRenderState(device,
        V9X_D3DRENDERSTATE_TEXTUREMIN, V9X_D3DFILTER_NEAREST);
    (void)device->vtbl->SetRenderState(device,
        V9X_D3DRENDERSTATE_TEXTUREMAG, V9X_D3DFILTER_NEAREST);
    (void)device->vtbl->SetRenderState(device,
        V9X_D3DRENDERSTATE_TEXTUREADDRESS, V9X_D3DTADDRESS_WRAP_R);
    (void)device->vtbl->SetRenderState(device,
        V9X_D3DRENDERSTATE_COLORKEYENABLE_R, 0ul);
    (void)device->vtbl->SetRenderState(device,
        V9X_D3DRENDERSTATE_FOGENABLE, 0ul);
    (void)device->vtbl->SetRenderState(device,
        V9X_D3DRENDERSTATE_CULLMODE, V9X_D3DCULL_CCW);
    triangle[0].color = 0xffffffff; triangle[0].specular = 0ul;
    triangle[1].color = 0xffffffff; triangle[1].specular = 0ul;
    triangle[2].color = 0xffffffff; triangle[2].specular = 0ul;
    triangle[0].tu = 0.125f; triangle[0].tv = 0.125f;
    triangle[1].tu = 0.875f; triangle[1].tv = 0.125f;
    triangle[2].tu = 0.125f; triangle[2].tv = 0.875f;
}

/* Key names for the matrix are assembled, not listed: there are many. */
static void v9x_probe_cat(char *dest, const char *src)
{
    while (*dest != 0) ++dest;
    while (*src != 0) *dest++ = *src++;
    *dest = 0;
}

/*
 * The driver's counters, through the display driver's DCI escape - the same
 * route V9XTRACE uses, with a compact struct (probe_counts.h) instead of the
 * full snapshot. Returns 0, with the struct zeroed, on any driver that does
 * not answer: a vendor driver, or ours before the escape existed. The probe
 * then still runs; it just cannot say what the driver did during a cell.
 */
#define V9X_PROBE_QUERYESCSUPPORT 8u
#define V9X_PROBE_DCICOMMAND      3075u
#define V9X_PROBE_DD_VERSION      0x00000200ul

typedef struct v9x_probe_dcicmd {
    DWORD dwCommand;
    DWORD dwParam1;
    DWORD dwParam2;
    DWORD dwVersion;
    DWORD dwReserved;
} V9X_PROBE_DCICMD;

static int v9x_probe_counts(V9X_PROBE_COUNTS *counts)
{
    HDC screen;
    V9X_PROBE_DCICMD command;
    int result;

    v9x_zero(counts, sizeof(*counts));
    screen = GetDC(0);
    if (screen == 0) {
        return 0;
    }
    command.dwCommand = V9X_DDGETCOUNTS;
    command.dwParam1 = 0ul;
    command.dwParam2 = 0ul;
    command.dwVersion = V9X_PROBE_DD_VERSION;
    command.dwReserved = 0ul;
    result = ExtEscape(screen, V9X_PROBE_DCICOMMAND, sizeof(command),
                       (LPCSTR)&command, sizeof(*counts), (LPSTR)counts);
    ReleaseDC(0, screen);
    if (result <= 0 || counts->dwSize == 0ul) {
        v9x_zero(counts, sizeof(*counts));
        return 0;
    }
    return 1;
}

/*
 * What the driver did between two counter reads, written beside a cell only
 * when something happened: a key that is absent means "nothing", which keeps
 * the result file readable across hundreds of cells.
 */
static void v9x_probe_write_deltas(const char *prefix,
                                   const V9X_PROBE_COUNTS *before,
                                   const V9X_PROBE_COUNTS *after)
{
    char key[80];

    if (after->texture_refused != before->texture_refused) {
        key[0] = 0; v9x_probe_cat(key, prefix); v9x_probe_cat(key, "_Dref");
        v9x_write_uint(key, after->texture_refused - before->texture_refused);
    }
    if (after->blend_skipped != before->blend_skipped) {
        key[0] = 0; v9x_probe_cat(key, prefix); v9x_probe_cat(key, "_Dskip");
        v9x_write_uint(key, after->blend_skipped - before->blend_skipped);
    }
    if (after->engine_resets != before->engine_resets ||
        after->engine_idle_timeouts != before->engine_idle_timeouts ||
        after->engine_fifo_timeouts != before->engine_fifo_timeouts) {
        key[0] = 0; v9x_probe_cat(key, prefix); v9x_probe_cat(key, "_Dfault");
        v9x_write_uint(key,
            (after->engine_resets - before->engine_resets) +
            (after->engine_idle_timeouts - before->engine_idle_timeouts) +
            (after->engine_fifo_timeouts - before->engine_fifo_timeouts));
    }
    if (after->done_missing != before->done_missing) {
        key[0] = 0; v9x_probe_cat(key, prefix); v9x_probe_cat(key, "_Dmiss");
        v9x_write_uint(key, after->done_missing - before->done_missing);
    }
}


static DWORD v9x_time_surface_fill(struct v9x_dds *surface)
{
    DWORD started;

    v9x_fill_surface(surface, 0x18e318e3ul);
    started = v9x_time();
    v9x_fill_surface(surface, 0x07e007e0ul);
    return v9x_time() - started;
}

static HRESULT v9x_hardware_fill(struct v9x_dds *surface, DWORD color,
                                 DWORD *elapsed, HRESULT *done_result)
{
    V9X_DDBLTFX fx;
    HRESULT hr;
    DWORD started;

    v9x_zero(&fx, sizeof(fx));
    fx.dwSize = sizeof(fx);
    fx.dwFillColor = color;
    started = v9x_time();
    hr = surface->vtbl->Blt(surface, 0, 0, 0,
                            V9X_DDBLT_COLORFILL | V9X_DDBLT_WAIT, &fx);
    if (hr == 0) {
        do {
            *done_result = surface->vtbl->GetBltStatus(
                surface, V9X_DDGBS_ISBLTDONE);
        } while (*done_result == (HRESULT)V9X_DDERR_WASSTILLDRAWING &&
                 v9x_time() - started < 2000ul);
    } else {
        *done_result = hr;
    }
    *elapsed = v9x_time() - started;
    return hr;
}

/*
 * Read one 16-bit word out of a depth surface.
 *
 * Not v9x_surface_pixel16_equals, which gates on
 * ddpfPixelFormat.dwRGBBitCount == 16: a Z buffer's format is a depth format
 * and carries no RGB bit count, so that check refuses every depth surface and
 * the test would report a failure that is really a wrong reader.
 *
 * Returns non-zero when the word was read, and writes it to value_out.
 */
static int v9x_depth_word_at(struct v9x_dds *surface, DWORD x, DWORD y,
                             WORD *value_out)
{
    V9X_DDSURFACEDESC desc;
    HRESULT hr;
    BYTE FAR *row;

    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = surface->vtbl->Lock(surface, 0, &desc, V9X_DDLOCK_WAIT, 0);
    if (hr != 0) {
        return 0;
    }
    if (desc.lpSurface == 0 || desc.lPitch <= 0l ||
        x >= desc.dwWidth || y >= desc.dwHeight) {
        surface->vtbl->Unlock(surface, 0);
        return 0;
    }
    row = (BYTE FAR *)desc.lpSurface + y * (DWORD)desc.lPitch;
    *value_out = *(WORD FAR *)(row + x * 2ul);
    surface->vtbl->Unlock(surface, 0);
    return 1;
}

/*
 * DDBLT_DEPTHFILL, verified by reading the depth words back.
 *
 * Two different values, in sequence, and both are checked. One fill cannot
 * distinguish "the driver wrote the value" from "the surface already held it"
 * - freshly allocated video memory is whatever the last owner left, and a
 * single-value test passes by accident often enough to be worthless. The
 * second fill has to change what the first one wrote.
 *
 * Two positions, because a fill that writes only the first word or only the
 * first row is a real failure mode of a rectangle blit with the wrong pitch
 * or the wrong height, and reading (0,0) alone cannot see it.
 */
static void v9x_probe_depth_fill(struct v9x_dds *z_surface)
{
    static const WORD first = 0x1234u;
    static const WORD second = 0xabcdu;
    V9X_DDBLTFX fx;
    HRESULT hr;
    WORD origin = 0u;
    WORD corner = 0u;
    int read_ok;
    int ok = 0;

    v9x_zero(&fx, sizeof(fx));
    fx.dwSize = sizeof(fx);
    /* The union DDBLTFX shares between dwFillColor, dwFillDepth and
     * dwFillPixel; this member is the depth one here. */
    fx.dwFillColor = (DWORD)first;
    hr = z_surface->vtbl->Blt(z_surface, 0, 0, 0,
                              V9X_DDBLT_DEPTHFILL | V9X_DDBLT_WAIT, &fx);
    v9x_write_hresult("ZDepthFillHr", hr);
    if (hr == 0) {
        read_ok = v9x_depth_word_at(z_surface, 0ul, 0ul, &origin);
        v9x_write_uint("ZDepthFillRaw", read_ok ? (DWORD)origin : 0xfffful);

        if (read_ok && origin == first) {
            fx.dwFillColor = (DWORD)second;
            hr = z_surface->vtbl->Blt(z_surface, 0, 0, 0,
                                      V9X_DDBLT_DEPTHFILL | V9X_DDBLT_WAIT,
                                      &fx);
            v9x_write_hresult("ZDepthFill2Hr", hr);
            if (hr == 0 &&
                v9x_depth_word_at(z_surface, 0ul, 0ul, &origin) &&
                v9x_depth_word_at(z_surface, 63ul, 63ul, &corner)) {
                v9x_write_uint("ZDepthFill2Raw", (DWORD)origin);
                v9x_write_uint("ZDepthFillCornerRaw", (DWORD)corner);
                ok = origin == second && corner == second;
            }
        }
    }
    v9x_write_uint("ZDepthFillOk", ok ? 1ul : 0ul);
}

static int v9x_surface_pixel16_equals(struct v9x_dds *surface,
                                      DWORD x, DWORD y, WORD expected)
{
    V9X_DDSURFACEDESC desc;
    BYTE FAR *row;
    WORD value;
    HRESULT hr;

    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    hr = surface->vtbl->Lock(surface, 0, &desc, V9X_DDLOCK_WAIT, 0);
    if (hr != 0) {
        return 0;
    }
    if (desc.lpSurface == 0 ||
        desc.ddpfPixelFormat.dwRGBBitCount != 16ul ||
        x >= desc.dwWidth || y >= desc.dwHeight) {
        surface->vtbl->Unlock(surface, 0);
        return 0;
    }
    row = (BYTE FAR *)desc.lpSurface + y * (DWORD)desc.lPitch;
    value = *(WORD FAR *)(row + x * 2ul);
    surface->vtbl->Unlock(surface, 0);
    return value == expected;
}

/*
 * The render target's real channel layout, and the colours to expect from it.
 *
 * Every "did it draw the right colour" key in this probe used to compare
 * against a literal - 0x7C00 for red, 0x03E0 for green, 0x7FFF for white.
 * Those are ZRGB1555, and the render target is RGB565: the constants were
 * written to match what the ViRGE's triangle engine writes rather than what
 * the surface says it is, and the driver already records that mismatch as an
 * unresolved defect.
 *
 * That was harmless while one engine existed. It is not now. A second engine
 * writing the format the surface declares fails those comparisons while being
 * correct, and on 2026-09-01 it did so on three keys at once - the flat
 * triangle, the depth ladder and the write mask - each of which had to be read
 * back out of its raw value by hand. Every capability added after that would
 * have joined them.
 *
 * So the expectations are derived from the surface. The literals move to being
 * the fallback for a surface that will not describe itself, which keeps a
 * failed query looking like the old behaviour rather than like a black screen.
 */
typedef struct v9x_pixel_layout {
    DWORD red_mask;
    DWORD green_mask;
    DWORD blue_mask;
    DWORD valid;
} V9X_PIXEL_LAYOUT;

static DWORD v9x_mask_shift(DWORD mask)
{
    DWORD shift = 0ul;

    if (mask == 0ul) {
        return 0ul;
    }
    while ((mask & 1ul) == 0ul) {
        mask >>= 1;
        ++shift;
    }
    return shift;
}

/* A channel of a pixel, expanded to 0..255 so a comparison can be written
 * once and mean the same thing in 1555 and in 565, where the same colour is a
 * different number and green has an extra bit. */
static DWORD v9x_layout_channel(const V9X_PIXEL_LAYOUT *layout, DWORD pixel,
                                DWORD mask)
{
    DWORD shift;
    DWORD range;

    if (layout->valid == 0ul || mask == 0ul) {
        return 0ul;
    }
    shift = v9x_mask_shift(mask);
    range = mask >> shift;
    return (((pixel & mask) >> shift) * 255ul + range / 2ul) / range;
}

static DWORD v9x_layout_red(const V9X_PIXEL_LAYOUT *layout, DWORD pixel)
{
    return v9x_layout_channel(layout, pixel, layout->red_mask);
}

static DWORD v9x_layout_green(const V9X_PIXEL_LAYOUT *layout, DWORD pixel)
{
    return v9x_layout_channel(layout, pixel, layout->green_mask);
}

static DWORD v9x_layout_blue(const V9X_PIXEL_LAYOUT *layout, DWORD pixel)
{
    return v9x_layout_channel(layout, pixel, layout->blue_mask);
}

/*
 * The same three by index, for a rung that walks the channels rather than
 * naming one: 0 red, 1 green, 2 blue.
 */
#define V9X_PROBE_CHANNEL_RED   0ul
#define V9X_PROBE_CHANNEL_GREEN 1ul
#define V9X_PROBE_CHANNEL_BLUE  2ul

static DWORD v9x_layout_rgb(const V9X_PIXEL_LAYOUT *layout, DWORD pixel,
                            DWORD channel)
{
    if (channel == V9X_PROBE_CHANNEL_RED) {
        return v9x_layout_red(layout, pixel);
    }
    if (channel == V9X_PROBE_CHANNEL_GREEN) {
        return v9x_layout_green(layout, pixel);
    }
    return v9x_layout_blue(layout, pixel);
}

/* Which of the matrix's colours a target pixel is, on 0..255 channels. */
#define V9X_PROBE_HUE_OTHER   0ul
#define V9X_PROBE_HUE_GREEN   1ul
#define V9X_PROBE_HUE_BLUE    2ul
#define V9X_PROBE_HUE_MAGENTA 3ul
#define V9X_PROBE_HUE_CYAN    4ul
#define V9X_PROBE_HUE_BLACK   5ul

static DWORD v9x_probe_hue(const V9X_PIXEL_LAYOUT *layout, WORD raw)
{
    DWORD r;
    DWORD g;
    DWORD b;

    if (layout->valid == 0ul) {
        return V9X_PROBE_HUE_OTHER;
    }
    if (raw == 0u) {
        return V9X_PROBE_HUE_BLACK;
    }
    r = v9x_layout_red(layout, raw);
    g = v9x_layout_green(layout, raw);
    b = v9x_layout_blue(layout, raw);
    if (g >= 197ul && r <= 33ul && b <= 33ul) return V9X_PROBE_HUE_GREEN;
    if (b >= 197ul && r <= 33ul && g <= 33ul) return V9X_PROBE_HUE_BLUE;
    if (r >= 197ul && b >= 197ul && g <= 33ul) return V9X_PROBE_HUE_MAGENTA;
    if (g >= 197ul && b >= 197ul && r <= 33ul) return V9X_PROBE_HUE_CYAN;
    return V9X_PROBE_HUE_OTHER;
}


/* Three 0..255 channels as the surface would store them. Rounding is
 * to nearest so 255 lands on the full field rather than one short of it. */
static WORD v9x_layout_pack(const V9X_PIXEL_LAYOUT *layout,
                            DWORD red, DWORD green, DWORD blue)
{
    DWORD value = 0ul;
    DWORD shift;
    DWORD range;

    if (layout->valid == 0ul) {
        return 0u;
    }
    shift = v9x_mask_shift(layout->red_mask);
    range = layout->red_mask >> shift;
    value |= ((red * range + 127ul) / 255ul) << shift;
    shift = v9x_mask_shift(layout->green_mask);
    range = layout->green_mask >> shift;
    value |= ((green * range + 127ul) / 255ul) << shift;
    shift = v9x_mask_shift(layout->blue_mask);
    range = layout->blue_mask >> shift;
    value |= ((blue * range + 127ul) / 255ul) << shift;
    return (WORD)value;
}

static void v9x_surface_layout(struct v9x_dds *surface,
                               V9X_PIXEL_LAYOUT *layout)
{
    V9X_DDSURFACEDESC desc;

    layout->red_mask = 0ul;
    layout->green_mask = 0ul;
    layout->blue_mask = 0ul;
    layout->valid = 0ul;
    if (surface == 0) {
        return;
    }
    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    if (surface->vtbl->GetSurfaceDesc(surface, &desc) != 0) {
        return;
    }
    if (desc.ddpfPixelFormat.dwRGBBitCount != 16ul ||
        desc.ddpfPixelFormat.dwRBitMask == 0ul ||
        desc.ddpfPixelFormat.dwGBitMask == 0ul ||
        desc.ddpfPixelFormat.dwBBitMask == 0ul) {
        return;
    }
    layout->red_mask = desc.ddpfPixelFormat.dwRBitMask;
    layout->green_mask = desc.ddpfPixelFormat.dwGBitMask;
    layout->blue_mask = desc.ddpfPixelFormat.dwBBitMask;
    layout->valid = 1ul;
}

static WORD v9x_surface_pixel16(struct v9x_dds *surface, DWORD x, DWORD y)
{
    V9X_DDSURFACEDESC desc;
    BYTE FAR *row;
    WORD value = 0xffffu;

    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    if (surface->vtbl->Lock(surface, 0, &desc, V9X_DDLOCK_WAIT, 0) == 0) {
        if (desc.lpSurface != 0 && desc.ddpfPixelFormat.dwRGBBitCount == 16ul &&
            x < desc.dwWidth && y < desc.dwHeight) {
            row = (BYTE FAR *)desc.lpSurface + y * (DWORD)desc.lPitch;
            value = ((WORD FAR *)row)[x];
        }
        surface->vtbl->Unlock(surface, 0);
    }
    return value;
}

/*
 * One rung of a depth ladder: set the comparison and write mask, draw the
 * triangle at a given depth and colour, and read back the pixel.
 *
 * Returns non-zero only when every call succeeded. That matters more than it
 * looks: several rungs below expect the pixel to be *unchanged*, and a failed
 * SetRenderState or DrawPrimitive also leaves it unchanged. Without folding
 * the HRESULTs in, a driver that refused every depth call would pass the
 * rejection tests for entirely the wrong reason.
 *
 * The caller still compares the pixel itself, because what counts as correct
 * differs per rung.
 */
static int v9x_z_step(struct v9x_d3d_device2 *device,
                      V9X_D3DTLVERTEX *triangle, float depth, DWORD color,
                      DWORD compare, DWORD write_enable,
                      struct v9x_dds *target, DWORD *raw_out,
                      HRESULT *state_out, HRESULT *draw_out)
{
    HRESULT state_hr;
    HRESULT begin_hr;
    HRESULT draw_hr;
    HRESULT end_hr;

    triangle[0].sz = depth;
    triangle[1].sz = depth;
    triangle[2].sz = depth;
    triangle[0].color = color;
    triangle[1].color = color;
    triangle[2].color = color;

    state_hr = device->vtbl->SetRenderState(
        device, V9X_D3DRENDERSTATE_ZENABLE, 1ul);
    if (state_hr == 0) {
        state_hr = device->vtbl->SetRenderState(
            device, V9X_D3DRENDERSTATE_ZFUNC, compare);
    }
    if (state_hr == 0) {
        state_hr = device->vtbl->SetRenderState(
            device, V9X_D3DRENDERSTATE_ZWRITEENABLE, write_enable);
    }
    begin_hr = state_hr == 0 ? device->vtbl->BeginScene(device) : state_hr;
    if (begin_hr == 0) {
        draw_hr = device->vtbl->DrawPrimitive(
            device, V9X_D3DPT_TRIANGLELIST, V9X_D3DVT_TLVERTEX,
            triangle, 3ul, 0ul);
        end_hr = device->vtbl->EndScene(device);
    } else {
        draw_hr = begin_hr;
        end_hr = begin_hr;
    }
    *raw_out = v9x_surface_pixel16(target, 16ul, 16ul);
    /* Reported, not just folded into the result. A rung that renders nothing
     * because a state call was refused and one that renders nothing because
     * the depth test rejected it look identical in the pixel alone. */
    if (state_out != 0) {
        *state_out = state_hr;
    }
    if (draw_out != 0) {
        *draw_out = draw_hr;
    }
    return state_hr == 0 && draw_hr == 0 && end_hr == 0;
}

/*
 * Which device did CreateDevice actually return.
 *
 * S_OK from CreateDevice for IID_IDirect3DHALDevice was taken as proof that
 * the object was the HAL. It is not: a device that answered every call with
 * S_OK while the driver's own counters never moved is what stalled this work
 * for two probe designs, and nothing in the report could tell a HAL device
 * from a software one.
 *
 * D3DDEVICEDESC is returned in two halves and exactly one of them is filled
 * in. dcmColorModel is zero in the unused half, so a hardware device has a
 * non-zero dcmColorModel in the HW desc; an RGB or ramp emulation device has
 * it in the HEL desc instead. dwDeviceZBufferBitDepth comes from the same
 * half, which makes it the device's own answer about depth support rather
 * than the enumeration's.
 */
static void v9x_report_device(const char *prefix,
                              struct v9x_d3d_device2 *device)
{
    V9X_D3D_DEVICE_DESC hw;
    V9X_D3D_DEVICE_DESC hel;
    char key[48];
    HRESULT hr;

    v9x_zero(&hw, sizeof(hw));
    v9x_zero(&hel, sizeof(hel));
    hw.dwSize = sizeof(hw);
    hel.dwSize = sizeof(hel);
    hr = device->vtbl->GetCaps(device, &hw, &hel);
    wsprintfA(key, "%sCapsHr", prefix);
    v9x_write_hresult(key, hr);
    if (hr != 0) {
        return;
    }
    wsprintfA(key, "%sHwColorModel", prefix);
    v9x_write_uint(key, hw.dcmColorModel);
    wsprintfA(key, "%sHelColorModel", prefix);
    v9x_write_uint(key, hel.dcmColorModel);
    wsprintfA(key, "%sHwRenderDepth", prefix);
    v9x_write_uint(key, hw.dwDeviceRenderBitDepth);
    wsprintfA(key, "%sHwZDepth", prefix);
    v9x_write_uint(key, hw.dwDeviceZBufferBitDepth);
    wsprintfA(key, "%sHelZDepth", prefix);
    v9x_write_uint(key, hel.dwDeviceZBufferBitDepth);
    wsprintfA(key, "%sIsHardware", prefix);
    v9x_write_uint(key, hw.dcmColorModel != 0ul ? 1ul : 0ul);
}

/*
 * Paint a ramp so every row (or every column) of a surface carries a distinct
 * 16-bit value. An overlapping copy that runs in the wrong direction repeats
 * a band of the ramp instead of shifting it, which a flat fill could not
 * reveal.
 */
static int v9x_paint_ramp(struct v9x_dds *surface, DWORD extent,
                          int by_row)
{
    V9X_DDSURFACEDESC desc;
    BYTE FAR *base;
    DWORD x;
    DWORD y;

    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    if (surface->vtbl->Lock(surface, 0, &desc, V9X_DDLOCK_WAIT, 0) != 0) {
        return 0;
    }
    if (desc.lpSurface == 0 || desc.ddpfPixelFormat.dwRGBBitCount != 16ul ||
        desc.dwWidth < extent || desc.dwHeight < extent) {
        surface->vtbl->Unlock(surface, 0);
        return 0;
    }
    base = (BYTE FAR *)desc.lpSurface;
    for (y = 0ul; y < desc.dwHeight; ++y) {
        WORD FAR *row = (WORD FAR *)(base + y * (DWORD)desc.lPitch);

        for (x = 0ul; x < desc.dwWidth; ++x) {
            row[x] = (WORD)((by_row ? y : x) + 1ul);
        }
    }
    surface->vtbl->Unlock(surface, 0);
    return 1;
}

/*
 * Overlapping same-surface copy. This is the case the driver's copy-direction
 * handling exists for - a window scroll or a sprite moved a short distance -
 * and it cannot be reached by copying between two distinct surfaces.
 */
static void v9x_check_overlap(struct v9x_dds *surface, const char *prefix)
{
    RECT source_rect;
    RECT destination_rect;
    char key[40];
    HRESULT hr;

    /* Shift a 64x64 block down by 16 rows. Copying top-down would re-read
     * rows it had already overwritten and repeat the first 16 values. */
    source_rect.left = 0;
    source_rect.top = 0;
    source_rect.right = 64;
    source_rect.bottom = 64;
    destination_rect.left = 0;
    destination_rect.top = 16;
    destination_rect.right = 64;
    destination_rect.bottom = 80;
    if (v9x_paint_ramp(surface, 128ul, 1)) {
        hr = surface->vtbl->Blt(surface, &destination_rect, surface,
                                &source_rect, V9X_DDBLT_WAIT, 0);
        wsprintfA(key, "%sDownHr", prefix);
        v9x_write_hresult(key, hr);
        wsprintfA(key, "%sDownPixelOk", prefix);
        v9x_write_uint(key,
                       hr == 0 &&
                       v9x_surface_pixel16_equals(surface, 10ul, 16ul, 1u) &&
                       v9x_surface_pixel16_equals(surface, 10ul, 47ul, 32u) &&
                       v9x_surface_pixel16_equals(surface, 10ul, 79ul, 64u)
                           ? 1ul : 0ul);
        wsprintfA(key, "%sDownSeen", prefix);
        v9x_write_uint(key,
                       v9x_surface_pixel16(surface, 10ul, 79ul));
    }

    /* Shift a 64x64 block right by 16 columns, which exercises the
     * within-row reverse copy rather than the row order. */
    destination_rect.left = 16;
    destination_rect.top = 0;
    destination_rect.right = 80;
    destination_rect.bottom = 64;
    if (v9x_paint_ramp(surface, 128ul, 0)) {
        hr = surface->vtbl->Blt(surface, &destination_rect, surface,
                                &source_rect, V9X_DDBLT_WAIT, 0);
        wsprintfA(key, "%sRightHr", prefix);
        v9x_write_hresult(key, hr);
        wsprintfA(key, "%sRightPixelOk", prefix);
        v9x_write_uint(key,
                       hr == 0 &&
                       v9x_surface_pixel16_equals(surface, 16ul, 10ul, 1u) &&
                       v9x_surface_pixel16_equals(surface, 47ul, 10ul, 32u) &&
                       v9x_surface_pixel16_equals(surface, 79ul, 10ul, 64u)
                           ? 1ul : 0ul);
        wsprintfA(key, "%sRightSeen", prefix);
        v9x_write_uint(key,
                       v9x_surface_pixel16(surface, 79ul, 10ul));
    }

}

/*
 * Run the overlap checks twice. An offscreen surface has its own pitch, which
 * only an engine with per-surface base and stride registers can address; a
 * display-pitch surface is additionally reachable by an engine that walks
 * display memory as one surface, so both engine paths get pixel-verified
 * coverage of the copy-direction handling.
 */
static void v9x_test_overlap(struct v9x_dd *ddraw, struct v9x_dds *display)
{
    V9X_DDSURFACEDESC desc;
    struct v9x_dds *surface = 0;
    HRESULT hr;

    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH | V9X_DDSD_HEIGHT;
    desc.dwWidth = 128ul;
    desc.dwHeight = 128ul;
    desc.ddsCaps.dwCaps = V9X_DDSCAPS_OFFSCREENPLAIN |
                          V9X_DDSCAPS_VIDEOMEMORY;
    hr = ddraw->vtbl->CreateSurface(ddraw, &desc, &surface, 0);
    v9x_write_hresult("OverlapSurfaceHr", hr);
    if (hr == 0 && surface != 0) {
        v9x_check_overlap(surface, "Overlap");
        surface->vtbl->Release(surface);
    }
    if (display != 0) {
        v9x_check_overlap(display, "OverlapPitch");
    }
}

static LRESULT CALLBACK v9x_window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcA(window, message, wparam, lparam);
}

void __stdcall V9xDdrawProbeEntry(void)
{
    WNDCLASSA window_class;
    HWND window;
    HMODULE winmm;
    HMODULE ddraw_module;
    V9X_DDCREATE create;
    struct v9x_dd *ddraw = 0;
    struct v9x_dds *primary = 0;
    struct v9x_dds *backbuffer = 0;
    struct v9x_dds *stage = 0;
    struct v9x_dds *d3d_target = 0;
    struct v9x_dds *texture_surface = 0;
    struct v9x_dds *texture_surface2 = 0;
    struct v9x_dds *texture_mip_level = 0;
    struct v9x_d3d2 *d3d = 0;
    struct v9x_d3d_device2 *d3d_device = 0;
    struct v9x_d3d_texture2 *texture = 0;
    struct v9x_d3d_texture2 *texture2 = 0;
    struct v9x_d3d_viewport2 *d3d_viewport = 0;
    V9X_D3D_ENUM_RESULT d3d_result;
    DWORD caps_buffer[79]; /* DDCAPS_DX3, 0x13c bytes. */
    V9X_PIXEL_LAYOUT target_layout;
    /* The ZRGB1555 literals, as the fallback for a surface that will not
     * describe itself. Overwritten from the target below. */
    WORD expect_red = 0x7c00u;
    WORD expect_green = 0x03e0u;
    WORD expect_blue = 0x001fu;
    WORD expect_white = 0x7fffu;
    BOOL in_vblank = FALSE;
    V9X_TEXTURE_ENUM_RESULT texture_result;
    V9X_DDSURFACEDESC desc;
    V9X_DDSCAPS caps;
    HRESULT hr;
    DWORD frequency = 0ul;
    DWORD started;
    DWORD elapsed;
    DWORD flip_total = 0ul;
    DWORD flip_max = 0ul;
    V9X_D3DTLVERTEX triangle[3];
    int index;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA(V9X_SECTION, 0, 0, V9X_RESULT_PATH);
    v9x_write_text("Build", V9X_BUILD_ID);
    v9x_write_text("Result", "INCOMPLETE");
    v9x_write_uint("TexFormatCount", 0ul);
    v9x_write_uint("TexFormat565", 0ul);
    v9x_write_uint("TexFormat1555", 0ul);
    /* Seeds for a driver whose D3D branch never runs. DDERR_UNSUPPORTED, not
     * E_FAIL: a tier with no texture support is answering honestly, and E_FAIL
     * reads as "something broke" in every log that quotes it. A driver that
     * does run the branch overwrites these with the real HRESULTs. */
    v9x_write_hresult("TexSurfaceHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_hresult("TexHandleHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_hresult("TexSwapHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    /*
     * Depth-buffer seeds, same reasoning. Seeded rather than left absent so a
     * driver whose Z branch never runs still produces the keys: a baseline
     * diff then shows values that changed rather than keys that appeared, and
     * "not attempted" is distinguishable from "attempted and returned zero".
     * The raw pixels seed to 65535, which is not a colour any test expects.
     */
    v9x_write_hresult("D3DZSurfaceHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_hresult("D3DZStateHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_hresult("D3DZDrawHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_hresult("D3DZAttachHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_hresult("D3DZDeviceHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_uint("D3DZSurfacePitch", 0ul);
    v9x_write_uint("D3DZSurfaceCaps", 0ul);
    v9x_write_uint("D3DZInitRaw", 65535ul);
    v9x_write_uint("D3DZRejectRaw", 65535ul);
    v9x_write_uint("D3DZAcceptRaw", 65535ul);
    v9x_write_uint("D3DZUpdateRaw", 65535ul);
    v9x_write_uint("D3DZNoWriteRaw", 65535ul);
    v9x_write_uint("D3DZMaskRaw", 65535ul);
    v9x_write_uint("D3DZCompareOk", 0ul);
    v9x_write_uint("D3DZWriteMaskOk", 0ul);
    /*
     * The private-device design's own keys, seeded for the same reason. The
     * two designs write disjoint key sets so a result file can never leave it
     * ambiguous which one produced a given pixel; D3DZPrivateRun says which
     * ran, and stays 0 on a default run.
     */
    v9x_write_uint("D3DZPrivateRun", 0ul);
    v9x_write_hresult("D3DZPTargetHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_hresult("D3DZPSurfaceHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_hresult("D3DZPAttachHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_hresult("D3DZPDeviceHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_hresult("D3DZPViewportHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_hresult("D3DZPStateHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_hresult("D3DZPDrawHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_uint("D3DZPSurfacePitch", 0ul);
    v9x_write_uint("D3DZPSurfaceCaps", 0ul);
    v9x_write_uint("D3DZPInitRaw", 65535ul);
    v9x_write_uint("D3DZPRejectRaw", 65535ul);
    v9x_write_uint("D3DZPAcceptRaw", 65535ul);
    v9x_write_uint("D3DZPUpdateRaw", 65535ul);
    v9x_write_uint("D3DZPNoWriteRaw", 65535ul);
    v9x_write_uint("D3DZPMaskRaw", 65535ul);
    v9x_write_uint("D3DZPCompareOk", 0ul);
    v9x_write_uint("D3DZPWriteMaskOk", 0ul);
    /*
     * Device identity, for both devices. Zero means GetCaps was never asked,
     * which is distinct from a device that answered and reported no hardware
     * colour model - the second is a software device, the first is a device
     * that was never created.
     */
    v9x_write_hresult("D3DMainCapsHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_uint("D3DMainIsHardware", 0ul);
    v9x_write_hresult("D3DZPCapsHr", (HRESULT)V9X_DDERR_UNSUPPORTED);
    v9x_write_uint("D3DZPIsHardware", 0ul);
    /* Depth gradients are deliberately NOT exercised: every vertex in the
     * ladders below carries the same sz. See the block itself for why. */
    v9x_write_uint("D3DZGradientTested", 0ul);

    winmm = LoadLibraryA("WINMM.DLL");
    v9x_time = winmm != 0
        ? (V9X_TIMEGETTIME)GetProcAddress(winmm, "timeGetTime") : 0;
    ddraw_module = LoadLibraryA("DDRAW.DLL");
    create = ddraw_module != 0
        ? (V9X_DDCREATE)GetProcAddress(ddraw_module, "DirectDrawCreate")
        : 0;
    if (v9x_time == 0 || create == 0) {
        v9x_write_text("Result", "FAIL-LOAD");
        ExitProcess(1u);
    }

    v9x_zero(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = v9x_window_proc;
    window_class.hInstance = GetModuleHandleA(0);
    window_class.lpszClassName = "Velocity9xDdrawProbeWindow";
    RegisterClassA(&window_class);
    window = CreateWindowExA(0ul, window_class.lpszClassName,
                             "Velocity9x DirectDraw probe", WS_POPUP,
                             0, 0, 64, 64, 0, 0, window_class.hInstance, 0);
    if (window == 0) {
        v9x_write_text("Result", "FAIL-WINDOW");
        ExitProcess(1u);
    }
    ShowWindow(window, SW_SHOWNORMAL);
    SetForegroundWindow(window);

    hr = create(0, &ddraw, 0);
    v9x_write_hresult("CreateHr", hr);
    if (hr != 0) {
        v9x_write_text("Result", "FAIL-CREATE");
        ExitProcess(1u);
    }

    v9x_zero(caps_buffer, sizeof(caps_buffer));
    caps_buffer[0] = sizeof(caps_buffer);
    hr = ddraw->vtbl->GetCaps(ddraw, caps_buffer, 0);
    v9x_write_hresult("GetCapsHr", hr);
    v9x_write_uint("ReportedCaps", caps_buffer[1]);
    v9x_write_ddraw_globals(ddraw);
    hr = ddraw->vtbl->GetVerticalBlankStatus(ddraw, &in_vblank);
    v9x_write_hresult("VBlankStatusHr", hr);
    v9x_write_uint("VBlankStatus", in_vblank ? 1ul : 0ul);

    v9x_zero(&d3d_result, sizeof(d3d_result));
    hr = ddraw->vtbl->QueryInterface(ddraw, &v9x_iid_d3d2,
                                     (void **)&d3d);
    v9x_write_hresult("D3DQueryHr", hr);
    if (hr == 0 && d3d != 0) {
        hr = d3d->vtbl->EnumDevices(d3d, v9x_enum_d3d_device,
                                    &d3d_result);
        v9x_write_hresult("D3DEnumHr", hr);
        v9x_write_uint("D3DHalFound", d3d_result.hal_found);
        v9x_write_uint("D3DHalFlags", d3d_result.flags);
        v9x_write_uint("D3DHalRenderDepth", d3d_result.render_depth);
        v9x_write_d3d_devices(&d3d_result);
    }

    /* Desktop mode and monitor frequency before any mode request. */
    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    if (ddraw->vtbl->GetDisplayMode(ddraw, &desc) == 0) {
        v9x_write_mode("Desktop", &desc);
    }
    hr = ddraw->vtbl->GetMonitorFrequency(ddraw, &frequency);
    v9x_write_hresult("MonitorFreqHr", hr);
    v9x_write_uint("MonitorFreq", hr == 0 ? frequency : 0ul);

    /* Vertical-blank period from the desktop, no mode change involved. */
    hr = ddraw->vtbl->SetCooperativeLevel(ddraw, window, V9X_DDSCL_NORMAL);
    v9x_write_hresult("CoopNormalHr", hr);
    hr = ddraw->vtbl->WaitForVerticalBlank(ddraw, V9X_DDWAITVB_BLOCKBEGIN,
                                           0);
    v9x_write_hresult("VBlankHr", hr);
    if (hr == 0) {
        started = v9x_time();
        for (index = 0; index < 10; ++index) {
            ddraw->vtbl->WaitForVerticalBlank(ddraw,
                                              V9X_DDWAITVB_BLOCKBEGIN, 0);
        }
        v9x_write_uint("VBlank10Ms", v9x_time() - started);
    }

    /* The exact sequence a fullscreen game performs. */
    hr = ddraw->vtbl->SetCooperativeLevel(ddraw, window,
                                          V9X_DDSCL_EXCLUSIVE |
                                          V9X_DDSCL_FULLSCREEN |
                                          V9X_DDSCL_ALLOWMODEX);
    v9x_write_hresult("CoopExclusiveHr", hr);
    hr = ddraw->vtbl->WaitForVerticalBlank(ddraw, V9X_DDWAITVB_BLOCKBEGIN,
                                           0);
    v9x_write_hresult("ExclusiveVBlankHr", hr);
    /*
     * Palettized 8-bpp presentation, the path Doom95 uses.
     *
     * Doom asks DirectDraw for a 640x480x8 mode, attaches a 256-entry
     * palette and writes one index byte per pixel. If the driver hands back
     * a 16-bpp primary instead, those index bytes are read as half as many
     * RGB565 pixels: the picture ends up 320 columns wide with garbage
     * colours, which is exactly what the guest showed. This block records
     * the depth actually delivered and reads one known index back through
     * both the surface and the GDI screen DC, so a depth failure and a
     * palette failure can be told apart.
     */
    if (v9x_has_switch("/pal8")) {
        v9x_enum_modes(ddraw);
        v9x_pal8_mode_test(ddraw, "Pal8_640_480_", 640ul, 480ul);
        v9x_pal8_mode_test(ddraw, "Pal8_640_400_", 640ul, 400ul);
        v9x_pal8_mode_test(ddraw, "Pal8_320_240_", 320ul, 240ul);
        v9x_pal8_mode_test(ddraw, "Pal8_320_200_", 320ul, 200ul);
        v9x_write_text("Result", "PAL8");
        v9x_flush_results();
        ddraw->vtbl->RestoreDisplayMode(ddraw);
        ddraw->vtbl->SetCooperativeLevel(ddraw, window, V9X_DDSCL_NORMAL);
        ddraw->vtbl->Release(ddraw);
        DestroyWindow(window);
        ExitProcess(0u);
    }

    hr = ddraw->vtbl->SetDisplayMode(ddraw, 640ul, 480ul, 16ul);
    v9x_write_hresult("SetModeHr", hr);
    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    if (ddraw->vtbl->GetDisplayMode(ddraw, &desc) == 0) {
        v9x_write_mode("AfterMode", &desc);
    }

    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_BACKBUFFERCOUNT;
    desc.ddsCaps.dwCaps = V9X_DDSCAPS_PRIMARYSURFACE | V9X_DDSCAPS_FLIP |
                          V9X_DDSCAPS_COMPLEX;
    desc.dwBackBufferCount = 1ul;
    hr = ddraw->vtbl->CreateSurface(ddraw, &desc, &primary, 0);
    v9x_write_hresult("PrimaryHr", hr);
    if (hr == 0) {
        v9x_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        if (primary->vtbl->GetSurfaceDesc(primary, &desc) == 0) {
            v9x_write_mode("Primary", &desc);
            v9x_write_uint("PrimaryPitch", (DWORD)desc.lPitch);
        }
        caps.dwCaps = V9X_DDSCAPS_BACKBUFFER;
        hr = primary->vtbl->GetAttachedSurface(primary, &caps, &backbuffer);
        v9x_write_hresult("BackbufferHr", hr);
    }

    /*
     * Legacy device creation, on its own render target.
     *
     * The rest of this probe creates its Direct3D device through
     * IDirect3D2::CreateDevice. A DirectX 2/3-era application cannot: it
     * holds only IID_IDirect3D and creates the device by calling
     * QueryInterface for the enumerated device GUID on the render-target
     * surface. Both routes reach the same HAL, but only one of them was
     * covered, so a failure specific to the legacy route would have been
     * invisible. It passes today; keep it covered. A separate surface keeps
     * the attempt from disturbing the IDirect3D2 device below.
     */
    if (d3d_result.hal_found != 0ul) {
        struct v9x_dds *legacy_target = 0;
        void *legacy_d3d = 0;
        void *legacy_device = 0;

        hr = ddraw->vtbl->QueryInterface(ddraw, &v9x_iid_d3d, &legacy_d3d);
        v9x_write_hresult("D3DV1InterfaceHr", hr);

        v9x_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH | V9X_DDSD_HEIGHT;
        desc.dwWidth = 64ul;
        desc.dwHeight = 64ul;
        desc.ddsCaps.dwCaps = V9X_DDSCAPS_3DDEVICE |
                              V9X_DDSCAPS_OFFSCREENPLAIN |
                              V9X_DDSCAPS_VIDEOMEMORY;
        hr = ddraw->vtbl->CreateSurface(ddraw, &desc, &legacy_target, 0);
        v9x_write_hresult("D3DV1TargetHr", hr);
        if (hr == 0 && legacy_target != 0) {
            hr = legacy_target->vtbl->QueryInterface(
                legacy_target, &v9x_iid_d3d_hal, &legacy_device);
            v9x_write_hresult("D3DV1DeviceHr", hr);
            v9x_write_uint("D3DV1DeviceOk",
                           hr == 0 && legacy_device != 0 ? 1ul : 0ul);
            if (legacy_device != 0) {
                struct v9x_dds *unknown = (struct v9x_dds *)legacy_device;

                unknown->vtbl->Release(unknown);
            }
            legacy_target->vtbl->Release(legacy_target);
        }
        if (legacy_d3d != 0) {
            struct v9x_dds *unknown = (struct v9x_dds *)legacy_d3d;

            unknown->vtbl->Release(unknown);
        }
    }

    if (d3d != 0 && d3d_result.hal_found != 0ul) {
        v9x_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH | V9X_DDSD_HEIGHT;
        desc.dwWidth = 64ul;
        desc.dwHeight = 64ul;
        desc.ddsCaps.dwCaps = V9X_DDSCAPS_3DDEVICE |
                              V9X_DDSCAPS_OFFSCREENPLAIN |
                              V9X_DDSCAPS_VIDEOMEMORY;
        hr = ddraw->vtbl->CreateSurface(ddraw, &desc, &d3d_target, 0);
        v9x_write_hresult("D3DTargetHr", hr);
        v9x_surface_layout(d3d_target, &target_layout);
        if (target_layout.valid != 0ul) {
            expect_red = v9x_layout_pack(&target_layout, 255ul, 0ul, 0ul);
            expect_green = v9x_layout_pack(&target_layout, 0ul, 255ul, 0ul);
            expect_blue = v9x_layout_pack(&target_layout, 0ul, 0ul, 255ul);
            expect_white = v9x_layout_pack(&target_layout, 255ul, 255ul,
                                           255ul);
        }
        /* Recorded so a result file says which constants its Ok keys used.
         * Without these, a key that flipped between two runs looks like a
         * driver change when it may be a surface-format change. */
        v9x_write_uint("D3DTargetFormatValid", target_layout.valid);
        v9x_write_uint("D3DTargetRMask", target_layout.red_mask);
        v9x_write_uint("D3DTargetGMask", target_layout.green_mask);
        v9x_write_uint("D3DTargetBMask", target_layout.blue_mask);
        v9x_write_uint("D3DExpectRed", expect_red);
        v9x_write_uint("D3DExpectGreen", expect_green);
        v9x_write_uint("D3DExpectBlue", expect_blue);
        v9x_write_uint("D3DExpectWhite", expect_white);
        if (hr == 0 && d3d_target != 0) {
            hr = d3d->vtbl->CreateDevice(d3d, &v9x_iid_d3d_hal,
                                         d3d_target, &d3d_device);
            v9x_write_hresult("D3DCreateDeviceHr", hr);
            if (hr == 0 && d3d_device != 0) {
                HRESULT begin_hr;
                HRESULT draw_hr;
                HRESULT end_hr;
                HRESULT viewport_hr;
                HRESULT texture_hr;
                HRESULT texture2_hr;
                HRESULT swap_hr;
                HRESULT state_hr;
                DWORD texture_handle = 0ul;
                DWORD texture_handle2 = 0ul;
                DWORD trilinear_raw;
                V9X_D3D_VIEWPORT_DESC2 viewport_desc;

                /* Before anything is asked of it. Every pixel result below is
                 * only about this driver if this device is the HAL. */
                v9x_report_device("D3DMain", d3d_device);

                v9x_zero(&texture_result, sizeof(texture_result));
                texture_hr = d3d_device->vtbl->EnumTextureFormats(
                    d3d_device, v9x_enum_texture_format, &texture_result);
                v9x_write_hresult("TexEnumHr", texture_hr);
                v9x_write_uint("TexFormatCount", texture_result.count);
                v9x_write_uint("TexFormat565", texture_result.rgb565);
                v9x_write_uint("TexFormat1555", texture_result.argb1555);
                v9x_write_uint("TexFormat4444", texture_result.argb4444);
                {
                    DWORD i;
                    char key[32];

                    for (i = 0ul; i < texture_result.count &&
                                  i < V9X_TEXTURE_ENUM_MAX; ++i) {
                        wsprintfA(key, "TexFmt%luBits", i);
                        v9x_write_uint(key, texture_result.bits[i]);
                        wsprintfA(key, "TexFmt%luFlags", i);
                        v9x_write_hresult(key, (HRESULT)texture_result.flags[i]);
                        wsprintfA(key, "TexFmt%luR", i);
                        v9x_write_hresult(key, (HRESULT)texture_result.red[i]);
                        wsprintfA(key, "TexFmt%luG", i);
                        v9x_write_hresult(key, (HRESULT)texture_result.green[i]);
                        wsprintfA(key, "TexFmt%luB", i);
                        v9x_write_hresult(key, (HRESULT)texture_result.blue[i]);
                        wsprintfA(key, "TexFmt%luA", i);
                        v9x_write_hresult(key, (HRESULT)texture_result.alpha[i]);
                    }
                }

                v9x_zero(&desc, sizeof(desc));
                desc.dwSize = sizeof(desc);
                desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH |
                               V9X_DDSD_HEIGHT | V9X_DDSD_PIXELFORMAT;
                desc.dwWidth = 64ul;
                desc.dwHeight = 64ul;
                desc.ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;
                desc.ddpfPixelFormat.dwSize = sizeof(V9X_DDPIXELFORMAT);
                desc.ddpfPixelFormat.dwFlags = 0x00000041ul;
                desc.ddpfPixelFormat.dwRGBBitCount = 16ul;
                desc.ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
                desc.ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
                desc.ddpfPixelFormat.dwBBitMask = 0x0000001ful;
                desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0x00008000ul;
                texture_hr = ddraw->vtbl->CreateSurface(
                    ddraw, &desc, &texture_surface, 0);
                v9x_write_hresult("TexSurfaceHr", texture_hr);
                v9x_zero(&desc, sizeof(desc));
                desc.dwSize = sizeof(desc);
                desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH |
                               V9X_DDSD_HEIGHT | V9X_DDSD_PIXELFORMAT |
                               V9X_DDSD_MIPMAPCOUNT;
                desc.dwWidth = 64ul;
                desc.dwHeight = 64ul;
                desc.dwMipMapCount = 2ul;
                desc.ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE |
                                      V9X_DDSCAPS_COMPLEX |
                                      V9X_DDSCAPS_MIPMAP |
                                      V9X_DDSCAPS_VIDEOMEMORY;
                desc.ddpfPixelFormat.dwSize = sizeof(V9X_DDPIXELFORMAT);
                desc.ddpfPixelFormat.dwFlags = 0x00000041ul;
                desc.ddpfPixelFormat.dwRGBBitCount = 16ul;
                desc.ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
                desc.ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
                desc.ddpfPixelFormat.dwBBitMask = 0x0000001ful;
                desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0x00008000ul;
                texture2_hr = ddraw->vtbl->CreateSurface(
                    ddraw, &desc, &texture_surface2, 0);
                v9x_write_hresult("TexSurface2Hr", texture2_hr);
                if (texture_hr == 0 && texture_surface != 0) {
                    /* Both 16-bit halves of the DWORD pattern: a pattern of 0x83e0 alone
                     * filled every other texel black, and every texture rung
                     * that sampled an odd column read the fill, not the
                     * engine. Caught 2026-09-03. */
                    v9x_fill_surface(texture_surface, 0x83e083e0ul);
                }
                if (texture2_hr == 0 && texture_surface2 != 0) {
                    V9X_DDSURFACEDESC mip_desc;

                    v9x_zero(&mip_desc, sizeof(mip_desc));
                    mip_desc.dwSize = sizeof(mip_desc);
                    if (texture_surface2->vtbl->GetSurfaceDesc(
                            texture_surface2, &mip_desc) == 0) {
                        v9x_write_uint("TexMipTopW", mip_desc.dwWidth);
                        v9x_write_uint("TexMipTopH", mip_desc.dwHeight);
                        v9x_write_uint("TexMipTopPitch", (DWORD)mip_desc.lPitch);
                        v9x_write_uint("TexMipTopAddress", (DWORD)mip_desc.lpSurface);
                        v9x_write_uint("TexMipTopCaps",
                                       mip_desc.ddsCaps.dwCaps);
                    }
                    v9x_fill_surface(texture_surface2, 0x83e083e0ul);
                    caps.dwCaps = V9X_DDSCAPS_MIPMAP;
                    texture2_hr = texture_surface2->vtbl->GetAttachedSurface(
                        texture_surface2, &caps, &texture_mip_level);
                    v9x_write_hresult("TexMipLevelHr", texture2_hr);
                    if (texture2_hr == 0 && texture_mip_level != 0) {
                        v9x_zero(&mip_desc, sizeof(mip_desc));
                        mip_desc.dwSize = sizeof(mip_desc);
                        if (texture_mip_level->vtbl->GetSurfaceDesc(
                                texture_mip_level, &mip_desc) == 0) {
                            v9x_write_uint("TexMipLevelW", mip_desc.dwWidth);
                            v9x_write_uint("TexMipLevelH", mip_desc.dwHeight);
                            v9x_write_uint("TexMipLevelPitch",
                                           (DWORD)mip_desc.lPitch);
                            v9x_write_uint("TexMipLevelAddress",
                                           (DWORD)mip_desc.lpSurface);
                            v9x_write_uint("TexMipLevelCaps",
                                           mip_desc.ddsCaps.dwCaps);
                        }
                        v9x_fill_surface(texture_mip_level, 0x801f801ful);
                    }
                }

                if (texture_hr == 0 && texture_surface != 0) {
                    texture_hr = texture_surface->vtbl->QueryInterface(
                        texture_surface, &v9x_iid_d3d_texture2,
                        (void **)&texture);
                }
                if (texture_hr == 0 && texture != 0) {
                    texture_hr = texture->vtbl->GetHandle(
                        texture, d3d_device, &texture_handle);
                }
                if (texture2_hr == 0 && texture_surface2 != 0) {
                    texture2_hr = texture_surface2->vtbl->QueryInterface(
                        texture_surface2, &v9x_iid_d3d_texture2,
                        (void **)&texture2);
                }
                if (texture2_hr == 0 && texture2 != 0) {
                    texture2_hr = texture2->vtbl->GetHandle(
                        texture2, d3d_device, &texture_handle2);
                }
                v9x_write_hresult("TexHandleHr",
                    texture_hr != 0 ? texture_hr : texture2_hr);
                v9x_write_uint("TexHandle", texture_handle);
                v9x_write_uint("TexHandle2", texture_handle2);

                swap_hr = texture_hr != 0 ? texture_hr : texture2_hr;
                if (swap_hr == 0) {
                    swap_hr = texture2->vtbl->Load(texture2, texture);
                }
                v9x_write_hresult("TexLoadHr", swap_hr);
                if (swap_hr == 0) {
                    swap_hr = d3d_device->vtbl->SwapTextureHandles(
                        d3d_device, texture, texture2);
                }
                v9x_write_hresult("TexSwapHr", swap_hr);
                if (texture_surface2 != 0) {
                    v9x_fill_surface(texture_surface2, 0x83e083e0ul);
                }
                if (texture_mip_level != 0) {
                    v9x_fill_surface(texture_mip_level, 0x801f801ful);
                }

                viewport_hr = d3d->vtbl->CreateViewport(
                    d3d, (void **)&d3d_viewport, 0);
                v9x_write_hresult("D3DCreateViewportHr", viewport_hr);
                if (viewport_hr == 0 && d3d_viewport != 0) {
                    viewport_hr = d3d_device->vtbl->AddViewport(
                        d3d_device, d3d_viewport);
                }
                v9x_write_hresult("D3DAddViewportHr", viewport_hr);
                if (viewport_hr == 0) {
                    v9x_zero(&viewport_desc, sizeof(viewport_desc));
                    viewport_desc.dwSize = sizeof(viewport_desc);
                    viewport_desc.dwWidth = 64ul;
                    viewport_desc.dwHeight = 64ul;
                    viewport_desc.dvClipX = -1.0f;
                    viewport_desc.dvClipY = 1.0f;
                    viewport_desc.dvClipWidth = 2.0f;
                    viewport_desc.dvClipHeight = 2.0f;
                    viewport_desc.dvMinZ = 0.0f;
                    viewport_desc.dvMaxZ = 1.0f;
                    viewport_hr = d3d_viewport->vtbl->SetViewport2(
                        d3d_viewport, &viewport_desc);
                }
                v9x_write_hresult("D3DSetViewportHr", viewport_hr);
                if (viewport_hr == 0) {
                    viewport_hr = d3d_device->vtbl->SetCurrentViewport(
                        d3d_device, d3d_viewport);
                }
                v9x_write_hresult("D3DCurrentViewportHr", viewport_hr);

                v9x_fill_surface(d3d_target, 0ul);
                /* Exercise the fractional S11.20 setup path advertised by
                 * D3DPRASTERCAPS_SUBPIXEL, not only integer coordinates. */
                triangle[0].sx = 8.25f;
                triangle[0].sy = 8.25f;
                triangle[0].sz = 0.0f;
                triangle[0].rhw = 1.0f;
                triangle[0].color = 0xffff0000ul;
                triangle[0].specular = 0ul;
                triangle[0].tu = 0.0f;
                triangle[0].tv = 0.0f;
                triangle[1] = triangle[0];
                triangle[1].sx = 55.75f;
                triangle[2] = triangle[0];
                triangle[2].sy = 55.75f;

                begin_hr = viewport_hr == 0
                    ? d3d_device->vtbl->BeginScene(d3d_device) : viewport_hr;
                v9x_write_hresult("D3DBeginSceneHr", begin_hr);
                if (begin_hr == 0) {
                    draw_hr = d3d_device->vtbl->DrawPrimitive(
                        d3d_device, V9X_D3DPT_TRIANGLELIST,
                        V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                    v9x_write_hresult("D3DDrawPrimitiveHr", draw_hr);
                    end_hr = d3d_device->vtbl->EndScene(d3d_device);
                } else {
                    draw_hr = begin_hr;
                    end_hr = begin_hr;
                }
                v9x_write_hresult("D3DEndSceneHr", end_hr);
                v9x_write_uint("D3DTrianglePixelRaw",
                               v9x_surface_pixel16(d3d_target, 16ul, 16ul));
                v9x_write_uint("D3DTrianglePixelOk",
                    draw_hr == 0 && end_hr == 0 &&
                    v9x_surface_pixel16_equals(d3d_target, 16ul, 16ul,
                                               expect_red) ? 1ul : 0ul);

                /*
                 * The same triangle with its vertex order reversed, then the
                 * other half of the same square in both orders.
                 *
                 * Every triangle this probe drew before 2026-09-03 was wound
                 * the same way, and the software rasterizer's independence
                 * from vertex order is host-tested while the ViRGE engine's
                 * never was. A quad's two triangles are commonly wound in
                 * opposite directions, and Final Reality's 3D scene on the
                 * emulated ViRGE lost one triangle of every quad as the clear
                 * colour while the same scene on the software engine lost
                 * none. An engine whose span setup walks the wrong way for one
                 * winding puts those spans outside the clip rectangle and
                 * draws nothing - which is exactly what that looks like.
                 */
                {
                    V9X_D3DTLVERTEX wound[3];
                    HRESULT cull_hr;

                    /*
                     * Culling off first. Direct3D's default is CULLMODE=CCW
                     * and the runtime applies it before the HAL sees the
                     * triangle, so without this the reversed rungs below
                     * measure the runtime, not the driver: they read 0 on the
                     * software engine too, whose rasterizer is order-
                     * independent by host test. Restored to CCW afterwards so
                     * the rungs that follow see the state they were written
                     * against.
                     */
                    cull_hr = d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_CULLMODE,
                        V9X_D3DCULL_NONE);
                    v9x_write_hresult("D3DCullNoneHr", cull_hr);

                    v9x_fill_surface(d3d_target, 0ul);
                    wound[0] = triangle[0];
                    wound[1] = triangle[2];
                    wound[2] = triangle[1];
                    begin_hr = d3d_device->vtbl->BeginScene(d3d_device);
                    if (begin_hr == 0) {
                        draw_hr = d3d_device->vtbl->DrawPrimitive(
                            d3d_device, V9X_D3DPT_TRIANGLELIST,
                            V9X_D3DVT_TLVERTEX, wound, 3ul, 0ul);
                        end_hr = d3d_device->vtbl->EndScene(d3d_device);
                    } else {
                        draw_hr = begin_hr;
                        end_hr = begin_hr;
                    }
                    v9x_write_uint("D3DTriangleReverseRaw",
                                   v9x_surface_pixel16(d3d_target, 16ul,
                                                       16ul));
                    v9x_write_uint("D3DTriangleReverseOk",
                        draw_hr == 0 && end_hr == 0 &&
                        v9x_surface_pixel16_equals(d3d_target, 16ul, 16ul,
                                                   expect_red) ? 1ul : 0ul);

                    /* The other half of the square: (55.75,8.25),
                     * (55.75,55.75), (8.25,55.75), sampled at (48,48). First
                     * in that order, then reversed. */
                    v9x_fill_surface(d3d_target, 0ul);
                    wound[0] = triangle[0];
                    wound[0].sx = 55.75f;
                    wound[0].sy = 8.25f;
                    wound[1] = triangle[0];
                    wound[1].sx = 55.75f;
                    wound[1].sy = 55.75f;
                    wound[2] = triangle[0];
                    wound[2].sx = 8.25f;
                    wound[2].sy = 55.75f;
                    begin_hr = d3d_device->vtbl->BeginScene(d3d_device);
                    if (begin_hr == 0) {
                        draw_hr = d3d_device->vtbl->DrawPrimitive(
                            d3d_device, V9X_D3DPT_TRIANGLELIST,
                            V9X_D3DVT_TLVERTEX, wound, 3ul, 0ul);
                        end_hr = d3d_device->vtbl->EndScene(d3d_device);
                    } else {
                        draw_hr = begin_hr;
                        end_hr = begin_hr;
                    }
                    v9x_write_uint("D3DTriangleOtherHalfRaw",
                                   v9x_surface_pixel16(d3d_target, 48ul,
                                                       48ul));
                    v9x_write_uint("D3DTriangleOtherHalfOk",
                        draw_hr == 0 && end_hr == 0 &&
                        v9x_surface_pixel16_equals(d3d_target, 48ul, 48ul,
                                                   expect_red) ? 1ul : 0ul);

                    v9x_fill_surface(d3d_target, 0ul);
                    {
                        V9X_D3DTLVERTEX swap = wound[1];
                        wound[1] = wound[2];
                        wound[2] = swap;
                    }
                    begin_hr = d3d_device->vtbl->BeginScene(d3d_device);
                    if (begin_hr == 0) {
                        draw_hr = d3d_device->vtbl->DrawPrimitive(
                            d3d_device, V9X_D3DPT_TRIANGLELIST,
                            V9X_D3DVT_TLVERTEX, wound, 3ul, 0ul);
                        end_hr = d3d_device->vtbl->EndScene(d3d_device);
                    } else {
                        draw_hr = begin_hr;
                        end_hr = begin_hr;
                    }
                    v9x_write_uint("D3DTriangleOtherHalfReverseRaw",
                                   v9x_surface_pixel16(d3d_target, 48ul,
                                                       48ul));
                    v9x_write_uint("D3DTriangleOtherHalfReverseOk",
                        draw_hr == 0 && end_hr == 0 &&
                        v9x_surface_pixel16_equals(d3d_target, 48ul, 48ul,
                                                   expect_red) ? 1ul : 0ul);

                    /*
                     * A ladder of triangle shapes, still with culling off.
                     *
                     * Every triangle this probe drew before this ladder was
                     * the same comfortable right-angled shape. The core's
                     * clipper hands the engine something else entirely for a
                     * polygon that leaves the screen: a fan of slivers, flat-
                     * topped and flat-bottomed pieces, obtuse triangles whose
                     * long edge lies on either side, and edges running along
                     * the target border. Final Reality's scene on the emulated
                     * ViRGE lost whole triangles of that kind while the
                     * software engine, fed the same clipped fans, lost none.
                     * Each entry is a shape class, sampled at a point well
                     * inside it, in a 64x64 target.
                     */
                    {
                        static const struct v9x_shape {
                            float x0, y0, x1, y1, x2, y2;
                            DWORD px, py;
                            const char *key;
                        } shapes[] = {
                            /* flat top, apex below, long edge left */
                            {  4.f,  4.f, 60.f,  4.f,  4.f, 60.f, 12ul, 12ul, "D3DShapeFlatTopLRaw" },
                            /* flat top, apex below, long edge right */
                            {  4.f,  4.f, 60.f,  4.f, 60.f, 60.f, 52ul, 12ul, "D3DShapeFlatTopRRaw" },
                            /* flat bottom, apex above, long edge left */
                            {  4.f, 60.f, 60.f, 60.f,  4.f,  4.f, 12ul, 52ul, "D3DShapeFlatBotLRaw" },
                            /* flat bottom, apex above, long edge right */
                            {  4.f, 60.f, 60.f, 60.f, 60.f,  4.f, 52ul, 52ul, "D3DShapeFlatBotRRaw" },
                            /* obtuse, middle vertex far right of long edge */
                            {  4.f,  4.f, 60.f, 20.f,  8.f, 60.f, 20ul, 20ul, "D3DShapeObtuseRRaw" },
                            /* obtuse, middle vertex far left of long edge */
                            { 60.f,  4.f,  4.f, 20.f, 56.f, 60.f, 44ul, 20ul, "D3DShapeObtuseLRaw" },
                            /* acute, middle vertex right */
                            { 30.f,  4.f, 40.f, 32.f, 30.f, 60.f, 33ul, 32ul, "D3DShapeAcuteRRaw" },
                            /* acute, middle vertex left */
                            { 34.f,  4.f, 24.f, 32.f, 34.f, 60.f, 31ul, 32ul, "D3DShapeAcuteLRaw" },
                            /* wide sliver, three scanlines tall */
                            {  2.f, 30.f, 62.f, 31.f,  2.f, 33.f, 20ul, 31ul, "D3DShapeSliverRaw" },
                            /* edge on the left border */
                            {  0.f,  4.f,  0.f, 60.f, 40.f, 32.f,  8ul, 32ul, "D3DShapeBorderLRaw" },
                            /* edge on the right border (63 = width-1) */
                            { 63.f,  4.f, 63.f, 60.f, 24.f, 32.f, 55ul, 32ul, "D3DShapeBorderRRaw" },
                            /* edge on the bottom border (63 = height-1) */
                            {  4.f, 63.f, 60.f, 63.f, 32.f, 24.f, 32ul, 55ul, "D3DShapeBorderBRaw" }
                        };
                        DWORD shape;
                        DWORD shapes_ok = 0ul;

                        for (shape = 0ul;
                             shape < sizeof(shapes) / sizeof(shapes[0]);
                             ++shape) {
                            WORD raw;

                            v9x_fill_surface(d3d_target, 0ul);
                            wound[0] = triangle[0];
                            wound[0].sx = shapes[shape].x0;
                            wound[0].sy = shapes[shape].y0;
                            wound[1] = triangle[0];
                            wound[1].sx = shapes[shape].x1;
                            wound[1].sy = shapes[shape].y1;
                            wound[2] = triangle[0];
                            wound[2].sx = shapes[shape].x2;
                            wound[2].sy = shapes[shape].y2;
                            begin_hr = d3d_device->vtbl->BeginScene(d3d_device);
                            if (begin_hr == 0) {
                                draw_hr = d3d_device->vtbl->DrawPrimitive(
                                    d3d_device, V9X_D3DPT_TRIANGLELIST,
                                    V9X_D3DVT_TLVERTEX, wound, 3ul, 0ul);
                                end_hr = d3d_device->vtbl->EndScene(d3d_device);
                            } else {
                                draw_hr = begin_hr;
                                end_hr = begin_hr;
                            }
                            raw = v9x_surface_pixel16(d3d_target,
                                                      shapes[shape].px,
                                                      shapes[shape].py);
                            v9x_write_uint(shapes[shape].key, raw);
                            if (draw_hr == 0 && end_hr == 0 &&
                                raw == (WORD)expect_red) {
                                ++shapes_ok;
                            }
                        }
                        v9x_write_uint("D3DShapesOk", shapes_ok);
                        v9x_write_uint("D3DShapesCount",
                                       (DWORD)(sizeof(shapes) /
                                               sizeof(shapes[0])));
                    }

                    (void)d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_CULLMODE,
                        V9X_D3DCULL_CCW);

                    /* Put the original back for the rungs that follow. */
                    v9x_fill_surface(d3d_target, 0ul);
                    begin_hr = d3d_device->vtbl->BeginScene(d3d_device);
                    if (begin_hr == 0) {
                        draw_hr = d3d_device->vtbl->DrawPrimitive(
                            d3d_device, V9X_D3DPT_TRIANGLELIST,
                            V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                        end_hr = d3d_device->vtbl->EndScene(d3d_device);
                    }
                }
                v9x_write_uint("D3DSubpixelTriangleOk",
                    draw_hr == 0 && end_hr == 0 &&
                    v9x_surface_pixel16_equals(d3d_target, 16ul, 16ul,
                                               expect_red) ? 1ul : 0ul);

                /*
                 * Is it a triangle, or is it the triangle's bounding box?
                 *
                 * Nothing above can tell those apart: (16,16) is inside both.
                 * The vertices are (8.25,8.25), (55.75,8.25) and (8.25,55.75),
                 * so the hypotenuse is x + y = 64 and (48,48) is far outside
                 * the triangle while sitting squarely inside its box. The
                 * surface was cleared to zero before the draw, so an unpainted
                 * pixel is zero and a filled box is not.
                 *
                 * This exists because mode 2 shipped a deliberate bounding-box
                 * stub as its first stage - it was the instrument that proved
                 * the path on a card with no 3D engine - and the pixel it
                 * produced at (16,16) is indistinguishable from a rasterizer's.
                 * The colour is not asserted here: what the two engines call
                 * red is a separate open question recorded against
                 * D3DTrianglePixelRaw, and folding it in would make one key
                 * answer two questions.
                 */
                v9x_write_uint("D3DTriangleOutsideRaw",
                               v9x_surface_pixel16(d3d_target, 48ul, 48ul));
                v9x_write_uint("D3DTriangleShapeOk",
                    draw_hr == 0 && end_hr == 0 &&
                    v9x_surface_pixel16(d3d_target, 16ul, 16ul) != 0u &&
                    v9x_surface_pixel16(d3d_target, 48ul, 48ul) == 0u
                        ? 1ul : 0ul);

                /*
                 * Does a triangle that overhangs the render target reach its
                 * last row and column?
                 *
                 * The driver's clipper cuts geometry to [0, extent - 1], so
                 * the furthest coordinate any vertex can carry is the bottom
                 * row's top edge rather than its bottom. Under a coverage rule
                 * that samples pixel centres, that row's centre then sits half
                 * a pixel past the furthest thing the clipper will express and
                 * is never covered - a one-pixel dark line down the right and
                 * along the bottom of anything full-screen.
                 *
                 * Whether that happens is a property of the engine's coverage
                 * rule, and the two engines here have different ones, so it is
                 * measured rather than reasoned about. The centre key is the
                 * control: it says the triangle drew at all, which is what
                 * separates "the edge is missing" from "nothing is there".
                 *
                 * The vertices deliberately overhang by a wide margin so the
                 * clipper, not the geometry, is what decides the edge.
                 */
                v9x_fill_surface(d3d_target, 0ul);
                triangle[0].sx = -32.0f;
                triangle[0].sy = -32.0f;
                triangle[1] = triangle[0];
                triangle[1].sx = 224.0f;
                triangle[2] = triangle[0];
                triangle[2].sy = 224.0f;
                if (begin_hr == 0) {
                    HRESULT edge_begin =
                        d3d_device->vtbl->BeginScene(d3d_device);

                    if (edge_begin == 0) {
                        d3d_device->vtbl->DrawPrimitive(
                            d3d_device, V9X_D3DPT_TRIANGLELIST,
                            V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                        d3d_device->vtbl->EndScene(d3d_device);
                    }
                    v9x_write_hresult("D3DEdgeBeginSceneHr", edge_begin);
                }
                v9x_write_uint("D3DEdgeCentreRaw",
                               v9x_surface_pixel16(d3d_target, 32ul, 32ul));
                v9x_write_uint("D3DEdgeRightRaw",
                               v9x_surface_pixel16(d3d_target, 63ul, 32ul));
                v9x_write_uint("D3DEdgeBottomRaw",
                               v9x_surface_pixel16(d3d_target, 32ul, 63ul));
                v9x_write_uint("D3DEdgeTopLeftRaw",
                               v9x_surface_pixel16(d3d_target, 0ul, 0ul));

                /* Put the geometry back. Every test below this point reuses
                 * `triangle` for its own subject - specular, fog, alpha,
                 * texture - and reads the pixel at (16,16), which the
                 * overhanging triangle above does not put where they expect
                 * it. Leaving the coordinates changed would move six later
                 * results without touching the code that produces them. */
                triangle[0].sx = 8.25f;
                triangle[0].sy = 8.25f;
                triangle[1].sx = 55.75f;
                triangle[1].sy = 8.25f;
                triangle[2].sx = 8.25f;
                triangle[2].sy = 55.75f;

                /*
                 * The same triangle into a full-screen-sized render target.
                 *
                 * Every Direct3D test above draws into a 64x64 surface, which
                 * the heap places low in video memory and whose pitch is 128
                 * bytes. A game renders into something the size of the screen:
                 * a 640x480 target has a 1280-byte pitch and lands at a high
                 * offset, behind the primary and its back buffer. Those are
                 * the two values the engine's DEST_BASE and DEST_SRC_STRIDE
                 * registers carry, and nothing here has ever exercised them
                 * at a game's magnitudes.
                 *
                 * This exists because of a report from a physical S3 Trio3D on
                 * 2026-09-02: Final Reality's intro displayed, its 3D section
                 * rendered at 8 flips a second with no engine fault, and the
                 * screen stayed black throughout - while every 64x64 pixel
                 * test in this probe passed on the same machine and the same
                 * boot. An engine that draws correctly into a small target and
                 * wrongly into a large one produces exactly that, and no key
                 * here could tell.
                 *
                 * Read back through Lock rather than GDI, so this says
                 * something about where the engine wrote rather than about
                 * which page GDI owns - see the FlipPixelOk issue for what
                 * happens when that distinction is skipped.
                 *
                 * Behind /bigtarget rather than default, only because it is
                 * new: the probe's default set runs on every family and this
                 * has been through two targets. Promote it after an ATI and a
                 * VBE run.
                 *
                 * Its first version omitted the viewport that the 64x64 test
                 * above creates, and a device with no current viewport faults
                 * inside DrawPrimitive. It died on a physical Trio3D, an
                 * emulated ViRGE and an emulated Trio64 in software mode,
                 * which read like a defect in shared driver code and was a
                 * defect in this test. See
                 * docs\issues\2026-09-02-large-render-target-kills-the-caller.md
                 * for the retraction.
                 */
                if (v9x_has_switch("/bigtarget")) {
                    struct v9x_dds *big_target = 0;
                    struct v9x_d3d_device2 *big_device = 0;
                    struct v9x_d3d_viewport2 *big_viewport = 0;
                    HRESULT big_hr;
                    HRESULT big_draw = (HRESULT)V9X_DDERR_UNSUPPORTED;
                    WORD big_raw = 0xffffu;
                    WORD big_outside = 0xffffu;

                    v9x_zero(&desc, sizeof(desc));
                    desc.dwSize = sizeof(desc);
                    desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH |
                                   V9X_DDSD_HEIGHT;
                    desc.dwWidth = 640ul;
                    desc.dwHeight = 480ul;
                    desc.ddsCaps.dwCaps = V9X_DDSCAPS_3DDEVICE |
                                          V9X_DDSCAPS_OFFSCREENPLAIN |
                                          V9X_DDSCAPS_VIDEOMEMORY;
                    big_hr = ddraw->vtbl->CreateSurface(ddraw, &desc,
                                                        &big_target, 0);
                    v9x_write_hresult("D3DBigTargetHr", big_hr);
                    if (big_hr == 0 && big_target != 0) {
                        v9x_zero(&desc, sizeof(desc));
                        desc.dwSize = sizeof(desc);
                        if (big_target->vtbl->GetSurfaceDesc(big_target,
                                                             &desc) == 0) {
                            v9x_write_uint("D3DBigPitch",
                                           (DWORD)desc.lPitch);
                        }
                        big_hr = d3d->vtbl->CreateDevice(d3d,
                                                         &v9x_iid_d3d_hal,
                                                         big_target,
                                                         &big_device);
                        v9x_write_hresult("D3DBigDeviceHr", big_hr);
                    }
                    if (big_hr == 0 && big_device != 0) {
                        /*
                         * Its own viewport, and this is not optional: a
                         * device with no current viewport faults inside
                         * DrawPrimitive. The first version of this test
                         * omitted it and died on every target - a real
                         * ViRGE-path chip, an emulated ViRGE and an emulated
                         * Trio64 in software mode - which reads exactly like
                         * a driver defect and was not one.
                         */
                        big_hr = d3d->vtbl->CreateViewport(
                            d3d, (void **)&big_viewport, 0);
                        if (big_hr == 0 && big_viewport != 0) {
                            big_hr = big_device->vtbl->AddViewport(
                                big_device, big_viewport);
                        }
                        if (big_hr == 0) {
                            v9x_zero(&viewport_desc, sizeof(viewport_desc));
                            viewport_desc.dwSize = sizeof(viewport_desc);
                            viewport_desc.dwWidth = 640ul;
                            viewport_desc.dwHeight = 480ul;
                            viewport_desc.dvClipX = -1.0f;
                            viewport_desc.dvClipY = 1.0f;
                            viewport_desc.dvClipWidth = 2.0f;
                            viewport_desc.dvClipHeight = 2.0f;
                            viewport_desc.dvMinZ = 0.0f;
                            viewport_desc.dvMaxZ = 1.0f;
                            big_hr = big_viewport->vtbl->SetViewport2(
                                big_viewport, &viewport_desc);
                        }
                        if (big_hr == 0) {
                            big_hr = big_device->vtbl->SetCurrentViewport(
                                big_device, big_viewport);
                        }
                        v9x_write_hresult("D3DBigViewportHr", big_hr);
                    }
                    if (big_hr == 0 && big_device != 0) {
                        v9x_write_uint("D3DBigStage", 1ul);
                        v9x_fill_surface(big_target, 0ul);
                        v9x_write_uint("D3DBigStage", 2ul);
                        /*
                         * Wholly inside the target, so that this test changes
                         * exactly one thing against the 64x64 case: the size
                         * of the render target, and with it the DEST_BASE
                         * offset and the 1280-byte stride.
                         *
                         * An earlier version ran to y = 503.75 in a 480-high
                         * target, which also made it the first large-target
                         * triangle the clipper had to cut - two variables at
                         * once, and no way to tell which mattered.
                         *
                         * (100,100) is inside; (400,300) is outside the
                         * hypotenuse and inside the bounding box.
                         */
                        triangle[0].sx = 8.25f;
                        triangle[0].sy = 8.25f;
                        triangle[1].sx = 503.75f;
                        triangle[1].sy = 8.25f;
                        triangle[2].sx = 8.25f;
                        triangle[2].sy = 400.75f;
                        if (big_device->vtbl->BeginScene(big_device) == 0) {
                            v9x_write_uint("D3DBigStage", 3ul);
                            big_draw = big_device->vtbl->DrawPrimitive(
                                big_device, V9X_D3DPT_TRIANGLELIST,
                                V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                            v9x_write_uint("D3DBigStage", 4ul);
                            big_device->vtbl->EndScene(big_device);
                            v9x_write_uint("D3DBigStage", 5ul);
                        }
                        v9x_write_hresult("D3DBigDrawHr", big_draw);
                        big_raw = v9x_surface_pixel16(big_target, 100ul,
                                                      100ul);
                        v9x_write_uint("D3DBigStage", 6ul);
                        big_outside = v9x_surface_pixel16(big_target, 400ul,
                                                          300ul);
                        v9x_write_uint("D3DBigStage", 7ul);
                    }
                    v9x_write_uint("D3DBigRaw", big_raw);
                    v9x_write_uint("D3DBigOutsideRaw", big_outside);
                    v9x_write_uint("D3DBigShapeOk",
                        big_draw == 0 && big_raw != 0u && big_raw != 0xffffu &&
                        big_outside == 0u ? 1ul : 0ul);
                    if (big_viewport != 0) {
                        struct v9x_dds *unknown =
                            (struct v9x_dds *)big_viewport;

                        unknown->vtbl->Release(unknown);
                    }
                    if (big_device != 0) {
                        struct v9x_dds *unknown =
                            (struct v9x_dds *)big_device;

                        unknown->vtbl->Release(unknown);
                    }
                    if (big_target != 0) {
                        big_target->vtbl->Release(big_target);
                    }

                    /* Restore the 64x64 geometry for the tests below. */
                    triangle[0].sx = 8.25f;
                    triangle[0].sy = 8.25f;
                    triangle[1].sx = 55.75f;
                    triangle[1].sy = 8.25f;
                    triangle[2].sx = 8.25f;
                    triangle[2].sy = 55.75f;
                }

                v9x_fill_surface(d3d_target, 0ul);
                state_hr = d3d_device->vtbl->SetRenderState(
                    d3d_device, V9X_D3DRENDERSTATE_FOGENABLE, 0ul);
                if (state_hr == 0) {
                    state_hr = d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_SPECULARENABLE, 1ul);
                }
                v9x_write_hresult("D3DSpecularStateHr", state_hr);
                triangle[0].color = 0xff000000ul;
                triangle[0].specular = 0xff00ff00ul;
                triangle[1].color = triangle[0].color;
                triangle[1].specular = triangle[0].specular;
                triangle[2].color = triangle[0].color;
                triangle[2].specular = triangle[0].specular;
                begin_hr = state_hr == 0
                    ? d3d_device->vtbl->BeginScene(d3d_device) : state_hr;
                if (begin_hr == 0) {
                    draw_hr = d3d_device->vtbl->DrawPrimitive(
                        d3d_device, V9X_D3DPT_TRIANGLELIST,
                        V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                    end_hr = d3d_device->vtbl->EndScene(d3d_device);
                } else {
                    draw_hr = begin_hr;
                    end_hr = begin_hr;
                }
                v9x_write_uint("D3DSpecularGouraudOk",
                    draw_hr == 0 && end_hr == 0 &&
                    v9x_surface_pixel16_equals(d3d_target, 16ul, 16ul,
                                               expect_green) ? 1ul : 0ul);

                v9x_fill_surface(d3d_target, 0ul);
                state_hr = d3d_device->vtbl->SetRenderState(
                    d3d_device, V9X_D3DRENDERSTATE_SPECULARENABLE, 0ul);
                if (state_hr == 0) {
                    state_hr = d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_FOGCOLOR,
                        0x000000fful);
                }
                if (state_hr == 0) {
                    state_hr = d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_FOGENABLE, 1ul);
                }
                v9x_write_hresult("D3DFogStateHr", state_hr);
                triangle[0].color = 0xffff0000ul;
                triangle[0].specular = 0x00000000ul;
                triangle[1].color = triangle[0].color;
                triangle[1].specular = triangle[0].specular;
                triangle[2].color = triangle[0].color;
                triangle[2].specular = triangle[0].specular;
                begin_hr = state_hr == 0
                    ? d3d_device->vtbl->BeginScene(d3d_device) : state_hr;
                if (begin_hr == 0) {
                    draw_hr = d3d_device->vtbl->DrawPrimitive(
                        d3d_device, V9X_D3DPT_TRIANGLELIST,
                        V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                    end_hr = d3d_device->vtbl->EndScene(d3d_device);
                } else {
                    draw_hr = begin_hr;
                    end_hr = begin_hr;
                }
                v9x_write_uint("D3DDepthFogOk",
                    draw_hr == 0 && end_hr == 0 &&
                    v9x_surface_pixel16_equals(d3d_target, 16ul, 16ul,
                                               expect_blue) ? 1ul : 0ul);
                (void)d3d_device->vtbl->SetRenderState(
                    d3d_device, V9X_D3DRENDERSTATE_FOGENABLE, 0ul);

                v9x_probe_reset_state(d3d_device, triangle);
                v9x_fill_surface(d3d_target,
                                 ((DWORD)expect_blue << 16) |
                                 (DWORD)expect_blue);
                state_hr = d3d_device->vtbl->SetRenderState(
                    d3d_device, V9X_D3DRENDERSTATE_SRCBLEND,
                    V9X_D3DBLEND_SRCALPHA);
                if (state_hr == 0) {
                    state_hr = d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_DESTBLEND,
                        V9X_D3DBLEND_INVSRCALPHA);
                }
                if (state_hr == 0) {
                    state_hr = d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_ALPHABLENDENABLE, 1ul);
                }
                v9x_write_hresult("D3DVertexAlphaStateHr", state_hr);
                triangle[0].color = 0x80ff0000ul;
                triangle[0].specular = 0ul;
                triangle[1].color = triangle[0].color;
                triangle[1].specular = triangle[0].specular;
                triangle[2].color = triangle[0].color;
                triangle[2].specular = triangle[0].specular;
                begin_hr = state_hr == 0
                    ? d3d_device->vtbl->BeginScene(d3d_device) : state_hr;
                if (begin_hr == 0) {
                    draw_hr = d3d_device->vtbl->DrawPrimitive(
                        d3d_device, V9X_D3DPT_TRIANGLELIST,
                        V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                    end_hr = d3d_device->vtbl->EndScene(d3d_device);
                } else {
                    draw_hr = begin_hr;
                    end_hr = begin_hr;
                }
                /*
                 * Half-alpha red over a blue destination, so both channels
                 * land near the middle. A range on 0..255 channels rather
                 * than the old exact 0x400F: that literal is a 1555 bit
                 * pattern, and the same colour in 565 is a different number
                 * with a different green field width. The window is +/- 20
                 * levels, which is wider than either format's quantisation
                 * and far narrower than the difference between blending and
                 * not.
                 */
                {
                    WORD blend_raw =
                        v9x_surface_pixel16(d3d_target, 16ul, 16ul);

                    v9x_write_uint("D3DVertexAlphaBlendRaw", blend_raw);
                    v9x_write_uint("D3DVertexAlphaBlendOk",
                        draw_hr == 0 && end_hr == 0 &&
                        target_layout.valid != 0ul &&
                        v9x_layout_red(&target_layout, blend_raw) >= 112ul &&
                        v9x_layout_red(&target_layout, blend_raw) <= 152ul &&
                        v9x_layout_green(&target_layout, blend_raw) <= 20ul &&
                        v9x_layout_blue(&target_layout, blend_raw) >= 103ul &&
                        v9x_layout_blue(&target_layout, blend_raw) <= 143ul
                            ? 1ul : 0ul);
                }
                (void)d3d_device->vtbl->SetRenderState(
                    d3d_device, V9X_D3DRENDERSTATE_ALPHABLENDENABLE, 0ul);

                v9x_fill_surface(d3d_target, 0ul);
                state_hr = d3d_device->vtbl->SetRenderState(
                    d3d_device, V9X_D3DRENDERSTATE_TEXTUREHANDLE,
                    texture_handle2);
                if (state_hr == 0) {
                    state_hr = d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_TEXTUREMAPBLEND,
                        V9X_D3DTBLEND_COPY);
                }
                if (state_hr == 0) {
                    state_hr = d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_TEXTUREMIN,
                        V9X_D3DFILTER_NEAREST);
                }
                if (state_hr == 0) {
                    state_hr = d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_TEXTUREMAG,
                        V9X_D3DFILTER_NEAREST);
                }
                v9x_write_hresult("D3DTextureStateHr", state_hr);
                triangle[0].color = 0xfffffffful;
                triangle[0].specular = 0ul;
                triangle[0].tu = 0.125f;
                triangle[0].tv = 0.125f;
                triangle[1].color = triangle[0].color;
                triangle[1].specular = triangle[0].specular;
                triangle[1].tu = 0.875f;
                triangle[1].tv = 0.125f;
                triangle[2].color = triangle[0].color;
                triangle[2].specular = triangle[0].specular;
                triangle[2].tu = 0.125f;
                triangle[2].tv = 0.875f;
                begin_hr = state_hr == 0
                    ? d3d_device->vtbl->BeginScene(d3d_device) : state_hr;
                if (begin_hr == 0) {
                    draw_hr = d3d_device->vtbl->DrawPrimitive(
                        d3d_device, V9X_D3DPT_TRIANGLELIST,
                        V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                    end_hr = d3d_device->vtbl->EndScene(d3d_device);
                } else {
                    draw_hr = begin_hr;
                    end_hr = begin_hr;
                }
                v9x_write_uint("D3DBaseTextureOk",
                    draw_hr == 0 && end_hr == 0 &&
                    v9x_surface_pixel16_equals(d3d_target, 16ul, 16ul,
                                               expect_green) ? 1ul : 0ul);
                v9x_write_uint("D3DBaseTextureRaw",
                    v9x_surface_pixel16(d3d_target, 16ul, 16ul));

                /*
                 * The same texture, tiled: coordinates from 0 to 2 across
                 * the triangle, then from -0.5 to 0.5. Every texel is green,
                 * so a green pixel anywhere says the sampler wrapped and a
                 * black one says it returned the border colour instead. The
                 * default address mode is WRAP and nothing here sets it,
                 * which is how every application that tiles a texture uses
                 * it. Final Reality's black wedges on the emulated ViRGE were
                 * this rung failing, drawn a thousand times.
                 */
                v9x_fill_surface(d3d_target, 0ul);
                triangle[0].tu = 0.0f;
                triangle[0].tv = 0.0f;
                triangle[1].tu = 2.0f;
                triangle[1].tv = 0.0f;
                triangle[2].tu = 0.0f;
                triangle[2].tv = 2.0f;
                begin_hr = d3d_device->vtbl->BeginScene(d3d_device);
                if (begin_hr == 0) {
                    draw_hr = d3d_device->vtbl->DrawPrimitive(
                        d3d_device, V9X_D3DPT_TRIANGLELIST,
                        V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                    end_hr = d3d_device->vtbl->EndScene(d3d_device);
                } else {
                    draw_hr = begin_hr;
                    end_hr = begin_hr;
                }
                v9x_write_hresult("D3DTiledTextureDrawHr", draw_hr);
                v9x_write_uint("D3DTiledTextureRaw",
                    v9x_surface_pixel16(d3d_target, 40ul, 12ul));
                v9x_write_uint("D3DTiledTextureRaw2",
                    v9x_surface_pixel16(d3d_target, 12ul, 12ul));
                v9x_write_uint("D3DTiledTextureOk",
                    draw_hr == 0 && end_hr == 0 &&
                    v9x_surface_pixel16_equals(d3d_target, 40ul, 12ul,
                                               expect_green) &&
                    v9x_surface_pixel16_equals(d3d_target, 12ul, 12ul,
                                               expect_green) ? 1ul : 0ul);

                v9x_fill_surface(d3d_target, 0ul);
                triangle[0].tu = -0.5f;
                triangle[0].tv = -0.5f;
                triangle[1].tu = 0.5f;
                triangle[1].tv = -0.5f;
                triangle[2].tu = -0.5f;
                triangle[2].tv = 0.5f;
                begin_hr = d3d_device->vtbl->BeginScene(d3d_device);
                if (begin_hr == 0) {
                    draw_hr = d3d_device->vtbl->DrawPrimitive(
                        d3d_device, V9X_D3DPT_TRIANGLELIST,
                        V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                    end_hr = d3d_device->vtbl->EndScene(d3d_device);
                } else {
                    draw_hr = begin_hr;
                    end_hr = begin_hr;
                }
                v9x_write_hresult("D3DTiledNegativeDrawHr", draw_hr);
                v9x_write_hresult("D3DTiledNegativeEndHr", end_hr);
                v9x_write_uint("D3DTiledNegativeRaw",
                    v9x_surface_pixel16(d3d_target, 12ul, 12ul));
                v9x_write_uint("D3DTiledNegativeRaw2",
                    v9x_surface_pixel16(d3d_target, 30ul, 20ul));
                v9x_write_uint("D3DTiledNegativeOk",
                    draw_hr == 0 && end_hr == 0 &&
                    v9x_surface_pixel16_equals(d3d_target, 12ul, 12ul,
                                               expect_green) ? 1ul : 0ul);
                /* The rungs below set their own coordinates. */

                v9x_fill_surface(d3d_target, 0ul);
                /*
                 * The TWO-LEVEL texture, texture_handle2, and not the plain
                 * one. Until 2026-09-03 this bound texture_handle - the 64x64
                 * texture with no mip chain - and then asked for MIPNEAREST
                 * and expected the colour of a level that texture does not
                 * have. The emulated ViRGE happened to return that colour,
                 * which read as a pass, and a physical Trio3D/2X returned
                 * black, which read as a chip that could not select a level.
                 * Neither reading was about mip selection. The trilinear rung
                 * below inherits this binding and was wrong the same way.
                 * See docs\issues\2026-09-03-trio3d-alpha-and-mip-differ-from-virge-dx.md.
                 */
                state_hr = d3d_device->vtbl->SetRenderState(
                    d3d_device, V9X_D3DRENDERSTATE_TEXTUREHANDLE,
                    texture_handle2);
                if (state_hr == 0) {
                    state_hr = d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_TEXTUREMIN,
                        V9X_D3DFILTER_MIPNEAREST);
                }
                v9x_write_hresult("D3DMipmapStateHr", state_hr);
                triangle[0].tu = 0.0f;
                triangle[0].tv = 0.0f;
                triangle[1].tu = 2.0f;
                triangle[1].tv = 0.0f;
                triangle[2].tu = 0.0f;
                triangle[2].tv = 2.0f;
                begin_hr = state_hr == 0
                    ? d3d_device->vtbl->BeginScene(d3d_device) : state_hr;
                if (begin_hr == 0) {
                    draw_hr = d3d_device->vtbl->DrawPrimitive(
                        d3d_device, V9X_D3DPT_TRIANGLELIST,
                        V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                    end_hr = d3d_device->vtbl->EndScene(d3d_device);
                } else {
                    draw_hr = begin_hr;
                    end_hr = begin_hr;
                }
                v9x_write_uint("D3DMipmapLevelSelectOk",
                    draw_hr == 0 && end_hr == 0 &&
                    v9x_surface_pixel16_equals(d3d_target, 16ul, 16ul,
                                               expect_blue) ? 1ul : 0ul);
                v9x_write_uint("D3DMipmapLevelRaw",
                    v9x_surface_pixel16(d3d_target, 16ul, 16ul));

                v9x_fill_surface(d3d_target, 0ul);
                state_hr = d3d_device->vtbl->SetRenderState(
                    d3d_device, V9X_D3DRENDERSTATE_TEXTUREMIN,
                    V9X_D3DFILTER_LINEARMIPLINEAR);
                if (state_hr == 0) {
                    state_hr = d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_TEXTUREMAG,
                        V9X_D3DFILTER_LINEAR);
                }
                v9x_write_hresult("D3DTrilinearStateHr", state_hr);
                /* 64 texels * 1.11328125 UV / 47.5 pixels = 1.5,
                 * selecting LOD 0.5 exactly between the green and blue
                 * levels. */
                triangle[0].tu = 0.0f;
                triangle[0].tv = 0.0f;
                triangle[1].tu = 1.11328125f;
                triangle[1].tv = 0.0f;
                triangle[2].tu = 0.0f;
                triangle[2].tv = 1.11328125f;
                begin_hr = state_hr == 0
                    ? d3d_device->vtbl->BeginScene(d3d_device) : state_hr;
                if (begin_hr == 0) {
                    draw_hr = d3d_device->vtbl->DrawPrimitive(
                        d3d_device, V9X_D3DPT_TRIANGLELIST,
                        V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                    end_hr = d3d_device->vtbl->EndScene(d3d_device);
                } else {
                    draw_hr = begin_hr;
                    end_hr = begin_hr;
                }
                trilinear_raw = v9x_surface_pixel16(
                    d3d_target, 16ul, 16ul);
                v9x_write_uint("D3DTrilinearRaw", trilinear_raw);
                /* Half green, half blue, as 0..255 channels. The old
                 * form masked the 1555 fields directly: green 12..18 of 31
                 * and blue 12..20 of 31, which is 39%..58% and 39%..65%.
                 * These are those same fractions, and they now mean the same
                 * thing whichever format the target is in. */
                v9x_write_uint("D3DTrilinearBlendOk",
                    draw_hr == 0 && end_hr == 0 &&
                    target_layout.valid != 0ul &&
                    v9x_layout_green(&target_layout, trilinear_raw) >= 95ul &&
                    v9x_layout_green(&target_layout, trilinear_raw) <= 152ul &&
                    v9x_layout_blue(&target_layout, trilinear_raw) >= 95ul &&
                    v9x_layout_blue(&target_layout, trilinear_raw) <= 168ul
                        ? 1ul : 0ul);
                /*
                 * ARGB4444 sampling.
                 *
                 * The texel is 0xf0f0: opaque pure green in 4444. Read as
                 * ARGB1555 - the driver's only other texture format, and what
                 * it assumed unconditionally before it classified surfaces -
                 * the same bits decode to strong red and blue with little
                 * green, so the channel balance distinguishes correct
                 * sampling from a misread format without having to predict
                 * how the hardware expands four bits.
                 *
                 * The expected value is in ZRGB1555, not the RGB565 of the
                 * display: the ViRGE triangle engine writes its own 1555
                 * layout into the target, which is why the flat-colour test
                 * above reads 0x7c00 for red rather than 0xf800.
                 */
                {
                    struct v9x_dds *surface4444 = 0;
                    struct v9x_d3d_texture2 *texture4444 = 0;
                    DWORD handle4444 = 0ul;
                    HRESULT hr4444;
                    WORD raw4444 = 0u;

                    v9x_zero(&desc, sizeof(desc));
                    desc.dwSize = sizeof(desc);
                    desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH |
                                   V9X_DDSD_HEIGHT | V9X_DDSD_PIXELFORMAT;
                    desc.dwWidth = 64ul;
                    desc.dwHeight = 64ul;
                    desc.ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;
                    desc.ddpfPixelFormat.dwSize = sizeof(V9X_DDPIXELFORMAT);
                    desc.ddpfPixelFormat.dwFlags = 0x00000041ul;
                    desc.ddpfPixelFormat.dwRGBBitCount = 16ul;
                    desc.ddpfPixelFormat.dwRBitMask = 0x00000f00ul;
                    desc.ddpfPixelFormat.dwGBitMask = 0x000000f0ul;
                    desc.ddpfPixelFormat.dwBBitMask = 0x0000000ful;
                    desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0x0000f000ul;
                    hr4444 = ddraw->vtbl->CreateSurface(ddraw, &desc,
                                                        &surface4444, 0);
                    v9x_write_hresult("Tex4444SurfaceHr", hr4444);
                    if (hr4444 == 0 && surface4444 != 0) {
                        v9x_fill_surface(surface4444, 0xf0f0f0f0ul);
                        hr4444 = surface4444->vtbl->QueryInterface(
                            surface4444, &v9x_iid_d3d_texture2,
                            (void **)&texture4444);
                        v9x_write_hresult("Tex4444InterfaceHr", hr4444);
                    }
                    if (hr4444 == 0 && texture4444 != 0) {
                        hr4444 = texture4444->vtbl->GetHandle(
                            texture4444, d3d_device, &handle4444);
                        v9x_write_hresult("Tex4444HandleHr", hr4444);
                    }
                    if (hr4444 == 0 && handle4444 != 0ul) {
                        v9x_fill_surface(d3d_target, 0ul);
                        hr4444 = d3d_device->vtbl->SetRenderState(
                            d3d_device, V9X_D3DRENDERSTATE_TEXTUREHANDLE,
                            handle4444);
                        if (hr4444 == 0) {
                            hr4444 = d3d_device->vtbl->SetRenderState(
                                d3d_device,
                                V9X_D3DRENDERSTATE_TEXTUREMAPBLEND,
                                V9X_D3DTBLEND_COPY);
                        }
                        triangle[0].tu = 0.125f;
                        triangle[0].tv = 0.125f;
                        triangle[1].tu = 0.875f;
                        triangle[1].tv = 0.125f;
                        triangle[2].tu = 0.125f;
                        triangle[2].tv = 0.875f;
                        begin_hr = hr4444 == 0
                            ? d3d_device->vtbl->BeginScene(d3d_device)
                            : hr4444;
                        if (begin_hr == 0) {
                            draw_hr = d3d_device->vtbl->DrawPrimitive(
                                d3d_device, V9X_D3DPT_TRIANGLELIST,
                                V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                            end_hr = d3d_device->vtbl->EndScene(d3d_device);
                        }
                        raw4444 = v9x_surface_pixel16(d3d_target,
                                                      16ul, 16ul);
                        v9x_write_uint("Tex4444Raw", raw4444);
                        v9x_write_uint("Tex4444PixelOk",
                            begin_hr == 0 && draw_hr == 0 && end_hr == 0 &&
                            target_layout.valid != 0ul &&
                            v9x_layout_green(&target_layout,
                                             raw4444) >= 197ul &&
                            v9x_layout_red(&target_layout,
                                           raw4444) <= 33ul &&
                            v9x_layout_blue(&target_layout,
                                            raw4444) <= 33ul ? 1ul : 0ul);
                        (void)d3d_device->vtbl->SetRenderState(
                            d3d_device,
                            V9X_D3DRENDERSTATE_TEXTUREHANDLE, 0ul);
                    }
                    if (texture4444 != 0) {
                        texture4444->vtbl->Release(texture4444);
                    }
                    if (surface4444 != 0) {
                        surface4444->vtbl->Release(surface4444);
                    }
                }

                (void)d3d_device->vtbl->SetRenderState(
                    d3d_device, V9X_D3DRENDERSTATE_TEXTUREHANDLE, 0ul);

                v9x_probe_reset_state(d3d_device, triangle);
                /*
                 * A source colour key on a texture.
                 *
                 * The texture is one solid colour and that colour is the
                 * key, so a driver that honours the key draws nothing and
                 * the target keeps its fill; one that ignores it paints the
                 * triangle in the key colour. The key is set through
                 * IDirectDrawSurface::SetColorKey, which reaches the HAL's
                 * SetColorKey callback - whose data-block layout the trace
                 * block records raw so that these known values (low 0x7c1f,
                 * high 0x7c1f, DDCKEY_SRCBLT) can be found in it.
                 */
                {
                    struct v9x_dds *keyed = 0;
                    struct v9x_d3d_texture2 *keyed_texture = 0;
                    DWORD keyed_handle = 0ul;
                    V9X_DDCOLORKEY key;
                    HRESULT key_hr;
                    HRESULT set_key_hr = 0x80004005ul;
                    WORD keyed_raw = 0u;

                    v9x_zero(&desc, sizeof(desc));
                    desc.dwSize = sizeof(desc);
                    desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH |
                                   V9X_DDSD_HEIGHT | V9X_DDSD_PIXELFORMAT;
                    desc.dwWidth = 64ul;
                    desc.dwHeight = 64ul;
                    desc.ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;
                    desc.ddpfPixelFormat.dwSize = sizeof(V9X_DDPIXELFORMAT);
                    desc.ddpfPixelFormat.dwFlags = 0x00000041ul;
                    desc.ddpfPixelFormat.dwRGBBitCount = 16ul;
                    desc.ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
                    desc.ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
                    desc.ddpfPixelFormat.dwBBitMask = 0x0000001ful;
                    desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0x00008000ul;
                    key_hr = ddraw->vtbl->CreateSurface(ddraw, &desc,
                                                        &keyed, 0);
                    v9x_write_hresult("ColorKeySurfaceHr", key_hr);
                    if (key_hr == 0 && keyed != 0) {
                        /* Magenta with the alpha bit set: opaque to a
                         * driver that does not rewrite it. */
                        v9x_fill_surface(keyed, 0xfc1ffc1ful);
                        key.dwColorSpaceLowValue = 0x7c1ful;
                        key.dwColorSpaceHighValue = 0x7c1ful;
                        set_key_hr = keyed->vtbl->SetColorKey(
                            keyed, V9X_DDCKEY_SRCBLT, &key);
                        v9x_write_hresult("ColorKeySetHr", set_key_hr);
                        key_hr = keyed->vtbl->QueryInterface(
                            keyed, &v9x_iid_d3d_texture2,
                            (void **)&keyed_texture);
                    }
                    if (key_hr == 0 && keyed_texture != 0) {
                        key_hr = keyed_texture->vtbl->GetHandle(
                            keyed_texture, d3d_device, &keyed_handle);
                    }
                    if (key_hr == 0 && keyed_handle != 0ul) {
                        v9x_fill_surface(d3d_target, 0ul);
                        key_hr = d3d_device->vtbl->SetRenderState(
                            d3d_device, V9X_D3DRENDERSTATE_TEXTUREHANDLE,
                            keyed_handle);
                        if (key_hr == 0) {
                            key_hr = d3d_device->vtbl->SetRenderState(
                                d3d_device,
                                V9X_D3DRENDERSTATE_TEXTUREMAPBLEND,
                                V9X_D3DTBLEND_COPY);
                        }
                        if (key_hr == 0) {
                            key_hr = d3d_device->vtbl->SetRenderState(
                                d3d_device,
                                V9X_D3DRENDERSTATE_COLORKEYENABLE, 1ul);
                        }
                        triangle[0].tu = 0.125f;
                        triangle[0].tv = 0.125f;
                        triangle[1].tu = 0.875f;
                        triangle[1].tv = 0.125f;
                        triangle[2].tu = 0.125f;
                        triangle[2].tv = 0.875f;
                        begin_hr = key_hr == 0
                            ? d3d_device->vtbl->BeginScene(d3d_device)
                            : key_hr;
                        if (begin_hr == 0) {
                            draw_hr = d3d_device->vtbl->DrawPrimitive(
                                d3d_device, V9X_D3DPT_TRIANGLELIST,
                                V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                            end_hr = d3d_device->vtbl->EndScene(d3d_device);
                        }
                        keyed_raw = v9x_surface_pixel16(d3d_target,
                                                        16ul, 16ul);
                        v9x_write_hresult("ColorKeyDrawHr", draw_hr);
                        v9x_write_uint("ColorKeyRaw", keyed_raw);
                        v9x_write_uint("ColorKeyOk",
                            begin_hr == 0 && draw_hr == 0 && end_hr == 0 &&
                            set_key_hr == 0 && keyed_raw == 0u ? 1ul : 0ul);
                        (void)d3d_device->vtbl->SetRenderState(
                            d3d_device,
                            V9X_D3DRENDERSTATE_COLORKEYENABLE, 0ul);
                        (void)d3d_device->vtbl->SetRenderState(
                            d3d_device,
                            V9X_D3DRENDERSTATE_TEXTUREHANDLE, 0ul);
                    }
                    if (keyed_texture != 0) {
                        keyed_texture->vtbl->Release(keyed_texture);
                    }
                    if (keyed != 0) {
                        keyed->vtbl->Release(keyed);
                    }
                }

                v9x_probe_reset_state(d3d_device, triangle);
                /*
                 * Textures larger than the 64 texels every rung above uses.
                 *
                 * Final Reality's textures are 64 texels across and it draws
                 * correctly on the Trio3D/2X; 3DMark 99's are 128 and 256
                 * and on the same card they draw as scrambled noise while the
                 * emulated ViRGE/DX draws them correctly. Each size here is
                 * filled green on the left and blue on the right and drawn
                 * twice, once from each half. A sampler that addresses the
                 * texture correctly reads green then blue; one that has the
                 * stride, size or level wrong reads something else, and the
                 * raw values say what.
                 */
                {
                    static const DWORD big_sizes[3] = { 64ul, 128ul, 256ul };
                    static const char *big_left[3] = {
                        "Tex64LeftRaw", "Tex128LeftRaw", "Tex256LeftRaw" };
                    static const char *big_right[3] = {
                        "Tex64RightRaw", "Tex128RightRaw", "Tex256RightRaw" };
                    static const char *big_ok[3] = {
                        "Tex64HalvesOk", "Tex128HalvesOk", "Tex256HalvesOk" };
                    static const char *big_hr[3] = {
                        "Tex64SurfaceHr", "Tex128SurfaceHr", "Tex256SurfaceHr" };
                    DWORD big_index;

                    for (big_index = 0ul; big_index < 3ul; ++big_index) {
                        struct v9x_dds *big = 0;
                        struct v9x_d3d_texture2 *big_texture = 0;
                        DWORD big_handle = 0ul;
                        HRESULT big_hr_value;
                        WORD left_raw = 0u;
                        WORD right_raw = 0u;
                        HRESULT left_hr = 0x80004005ul;
                        HRESULT right_hr = 0x80004005ul;

                        v9x_zero(&desc, sizeof(desc));
                        desc.dwSize = sizeof(desc);
                        desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH |
                                       V9X_DDSD_HEIGHT | V9X_DDSD_PIXELFORMAT;
                        desc.dwWidth = big_sizes[big_index];
                        desc.dwHeight = big_sizes[big_index];
                        desc.ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;
                        desc.ddpfPixelFormat.dwSize = sizeof(V9X_DDPIXELFORMAT);
                        desc.ddpfPixelFormat.dwFlags = 0x00000041ul;
                        desc.ddpfPixelFormat.dwRGBBitCount = 16ul;
                        desc.ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
                        desc.ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
                        desc.ddpfPixelFormat.dwBBitMask = 0x0000001ful;
                        desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0x00008000ul;
                        big_hr_value = ddraw->vtbl->CreateSurface(
                            ddraw, &desc, &big, 0);
                        v9x_write_hresult(big_hr[big_index], big_hr_value);
                        if (big_hr_value == 0 && big != 0) {
                            v9x_fill_surface_halves(big, 0x83e0u, 0x801fu);
                            big_hr_value = big->vtbl->QueryInterface(
                                big, &v9x_iid_d3d_texture2,
                                (void **)&big_texture);
                        }
                        if (big_hr_value == 0 && big_texture != 0) {
                            big_hr_value = big_texture->vtbl->GetHandle(
                                big_texture, d3d_device, &big_handle);
                        }
                        if (big_hr_value == 0 && big_handle != 0ul) {
                            big_hr_value = d3d_device->vtbl->SetRenderState(
                                d3d_device, V9X_D3DRENDERSTATE_TEXTUREHANDLE,
                                big_handle);
                            if (big_hr_value == 0) {
                                big_hr_value = d3d_device->vtbl->SetRenderState(
                                    d3d_device,
                                    V9X_D3DRENDERSTATE_TEXTUREMAPBLEND,
                                    V9X_D3DTBLEND_COPY);
                            }
                            /* Left half: every coordinate inside u < 0.5. */
                            v9x_fill_surface(d3d_target, 0ul);
                            triangle[0].tu = 0.10f; triangle[0].tv = 0.10f;
                            triangle[1].tu = 0.40f; triangle[1].tv = 0.10f;
                            triangle[2].tu = 0.10f; triangle[2].tv = 0.40f;
                            begin_hr = big_hr_value == 0
                                ? d3d_device->vtbl->BeginScene(d3d_device)
                                : big_hr_value;
                            if (begin_hr == 0) {
                                left_hr = d3d_device->vtbl->DrawPrimitive(
                                    d3d_device, V9X_D3DPT_TRIANGLELIST,
                                    V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                                end_hr = d3d_device->vtbl->EndScene(d3d_device);
                                if (end_hr != 0) left_hr = end_hr;
                            }
                            left_raw = v9x_surface_pixel16(d3d_target,
                                                           16ul, 16ul);
                            /* Right half: every coordinate inside u > 0.5. */
                            v9x_fill_surface(d3d_target, 0ul);
                            triangle[0].tu = 0.60f; triangle[0].tv = 0.10f;
                            triangle[1].tu = 0.90f; triangle[1].tv = 0.10f;
                            triangle[2].tu = 0.60f; triangle[2].tv = 0.40f;
                            begin_hr = big_hr_value == 0
                                ? d3d_device->vtbl->BeginScene(d3d_device)
                                : big_hr_value;
                            if (begin_hr == 0) {
                                right_hr = d3d_device->vtbl->DrawPrimitive(
                                    d3d_device, V9X_D3DPT_TRIANGLELIST,
                                    V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                                end_hr = d3d_device->vtbl->EndScene(d3d_device);
                                if (end_hr != 0) right_hr = end_hr;
                            }
                            right_raw = v9x_surface_pixel16(d3d_target,
                                                            16ul, 16ul);
                            v9x_write_uint(big_left[big_index], left_raw);
                            v9x_write_uint(big_right[big_index], right_raw);
                            v9x_write_uint(big_ok[big_index],
                                left_hr == 0 && right_hr == 0 &&
                                target_layout.valid != 0ul &&
                                v9x_layout_green(&target_layout,
                                                 left_raw) >= 197ul &&
                                v9x_layout_blue(&target_layout,
                                                left_raw) <= 33ul &&
                                v9x_layout_blue(&target_layout,
                                                right_raw) >= 197ul &&
                                v9x_layout_green(&target_layout,
                                                 right_raw) <= 33ul
                                ? 1ul : 0ul);
                            (void)d3d_device->vtbl->SetRenderState(
                                d3d_device,
                                V9X_D3DRENDERSTATE_TEXTUREHANDLE, 0ul);
                        }
                        if (big_texture != 0) {
                            big_texture->vtbl->Release(big_texture);
                        }
                        if (big != 0) {
                            big->vtbl->Release(big);
                        }
                    }
                    triangle[0].tu = 0.125f; triangle[0].tv = 0.125f;
                    triangle[1].tu = 0.875f; triangle[1].tv = 0.125f;
                    triangle[2].tu = 0.125f; triangle[2].tv = 0.875f;
                }

                v9x_probe_reset_state(d3d_device, triangle);
                /*
                 * Texel alpha, per format, under a MODULATE blend.
                 *
                 * 3DMark 99's sprites on the Trio3D/2X draw with opaque black
                 * boxes where the emulated ViRGE/DX draws them transparent;
                 * they are ARGB4444 textures blended with MODULATE. Each
                 * rung here fills a texture opaque green on the left and
                 * alpha-zero blue on the right, enables SRCALPHA over
                 * INVSRCALPHA with MODULATE and white vertices, and draws each
                 * half over a black target. A sampler that honours texel
                 * alpha reads green then black; one that does not reads green
                 * then blue.
                 */
                {
                    static const char *alpha_names[2][4] = {
                        { "Alpha4444SurfaceHr", "Alpha4444LeftRaw",
                          "Alpha4444RightRaw", "Alpha4444Ok" },
                        { "Alpha1555SurfaceHr", "Alpha1555LeftRaw",
                          "Alpha1555RightRaw", "Alpha1555Ok" } };
                    DWORD alpha_index;

                    for (alpha_index = 0ul; alpha_index < 2ul; ++alpha_index) {
                        struct v9x_dds *asurf = 0;
                        struct v9x_d3d_texture2 *atex = 0;
                        DWORD ahandle = 0ul;
                        HRESULT ahr;
                        WORD left_raw = 0u;
                        WORD right_raw = 0u;
                        HRESULT left_hr = 0x80004005ul;
                        HRESULT right_hr = 0x80004005ul;

                        v9x_zero(&desc, sizeof(desc));
                        desc.dwSize = sizeof(desc);
                        desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH |
                                       V9X_DDSD_HEIGHT | V9X_DDSD_PIXELFORMAT;
                        desc.dwWidth = 64ul;
                        desc.dwHeight = 64ul;
                        desc.ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;
                        desc.ddpfPixelFormat.dwSize = sizeof(V9X_DDPIXELFORMAT);
                        desc.ddpfPixelFormat.dwFlags = 0x00000041ul;
                        desc.ddpfPixelFormat.dwRGBBitCount = 16ul;
                        if (alpha_index == 0ul) {
                            desc.ddpfPixelFormat.dwRBitMask = 0x00000f00ul;
                            desc.ddpfPixelFormat.dwGBitMask = 0x000000f0ul;
                            desc.ddpfPixelFormat.dwBBitMask = 0x0000000ful;
                            desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0x0000f000ul;
                        } else {
                            desc.ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
                            desc.ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
                            desc.ddpfPixelFormat.dwBBitMask = 0x0000001ful;
                            desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0x00008000ul;
                        }
                        ahr = ddraw->vtbl->CreateSurface(ddraw, &desc, &asurf, 0);
                        v9x_write_hresult(alpha_names[alpha_index][0], ahr);
                        if (ahr == 0 && asurf != 0) {
                            if (alpha_index == 0ul) {
                                v9x_fill_surface_halves(asurf, 0xf0f0u, 0x000fu);
                            } else {
                                v9x_fill_surface_halves(asurf, 0x83e0u, 0x001fu);
                            }
                            ahr = asurf->vtbl->QueryInterface(
                                asurf, &v9x_iid_d3d_texture2, (void **)&atex);
                        }
                        if (ahr == 0 && atex != 0) {
                            ahr = atex->vtbl->GetHandle(atex, d3d_device, &ahandle);
                        }
                        if (ahr == 0 && ahandle != 0ul) {
                            ahr = d3d_device->vtbl->SetRenderState(
                                d3d_device, V9X_D3DRENDERSTATE_TEXTUREHANDLE,
                                ahandle);
                            if (ahr == 0) ahr = d3d_device->vtbl->SetRenderState(
                                d3d_device, V9X_D3DRENDERSTATE_TEXTUREMAPBLEND,
                                V9X_D3DTBLEND_MODULATE);
                            if (ahr == 0) ahr = d3d_device->vtbl->SetRenderState(
                                d3d_device, V9X_D3DRENDERSTATE_SRCBLEND,
                                V9X_D3DBLEND_SRCALPHA);
                            if (ahr == 0) ahr = d3d_device->vtbl->SetRenderState(
                                d3d_device, V9X_D3DRENDERSTATE_DESTBLEND,
                                V9X_D3DBLEND_INVSRCALPHA);
                            if (ahr == 0) ahr = d3d_device->vtbl->SetRenderState(
                                d3d_device, V9X_D3DRENDERSTATE_ALPHABLENDENABLE,
                                1ul);
                            triangle[0].color = 0xffffffff;
                            triangle[1].color = 0xffffffff;
                            triangle[2].color = 0xffffffff;
                            v9x_fill_surface(d3d_target, 0ul);
                            triangle[0].tu = 0.10f; triangle[0].tv = 0.10f;
                            triangle[1].tu = 0.40f; triangle[1].tv = 0.10f;
                            triangle[2].tu = 0.10f; triangle[2].tv = 0.40f;
                            begin_hr = ahr == 0
                                ? d3d_device->vtbl->BeginScene(d3d_device) : ahr;
                            if (begin_hr == 0) {
                                left_hr = d3d_device->vtbl->DrawPrimitive(
                                    d3d_device, V9X_D3DPT_TRIANGLELIST,
                                    V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                                end_hr = d3d_device->vtbl->EndScene(d3d_device);
                                if (end_hr != 0) left_hr = end_hr;
                            }
                            left_raw = v9x_surface_pixel16(d3d_target, 16ul, 16ul);
                            v9x_fill_surface(d3d_target, 0ul);
                            triangle[0].tu = 0.60f; triangle[0].tv = 0.10f;
                            triangle[1].tu = 0.90f; triangle[1].tv = 0.10f;
                            triangle[2].tu = 0.60f; triangle[2].tv = 0.40f;
                            begin_hr = ahr == 0
                                ? d3d_device->vtbl->BeginScene(d3d_device) : ahr;
                            if (begin_hr == 0) {
                                right_hr = d3d_device->vtbl->DrawPrimitive(
                                    d3d_device, V9X_D3DPT_TRIANGLELIST,
                                    V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                                end_hr = d3d_device->vtbl->EndScene(d3d_device);
                                if (end_hr != 0) right_hr = end_hr;
                            }
                            right_raw = v9x_surface_pixel16(d3d_target, 16ul, 16ul);
                            v9x_write_uint(alpha_names[alpha_index][1], left_raw);
                            v9x_write_uint(alpha_names[alpha_index][2], right_raw);
                            v9x_write_uint(alpha_names[alpha_index][3],
                                left_hr == 0 && right_hr == 0 &&
                                target_layout.valid != 0ul &&
                                v9x_layout_green(&target_layout, left_raw) >= 197ul &&
                                right_raw == 0u ? 1ul : 0ul);
                            (void)d3d_device->vtbl->SetRenderState(
                                d3d_device, V9X_D3DRENDERSTATE_ALPHABLENDENABLE, 0ul);
                            (void)d3d_device->vtbl->SetRenderState(
                                d3d_device, V9X_D3DRENDERSTATE_TEXTUREHANDLE, 0ul);
                        }
                        if (atex != 0) atex->vtbl->Release(atex);
                        if (asurf != 0) asurf->vtbl->Release(asurf);
                    }
                }

                v9x_probe_reset_state(d3d_device, triangle);
                /*
                 * THE ALPHA TRANSFER CURVE.
                 *
                 * The matrix says every blended cell fails on the Trio3D/2X
                 * and every unblended one passes, but not what value of A the
                 * part used. Its three readings tell three different stories:
                 * an opaque texel drew nothing, an alpha-0 texel correctly
                 * drew nothing, and a half-alpha texel drew a neutral grey
                 * out of a texel that has colour in one channel
                 * (docs/decisions/2026-09-04-the-trio3d-runs-the-matrix.md).
                 *
                 * This walks A instead of sampling it at one point. The
                 * destination is filled red - not black, so the destination's
                 * own contribution is visible and separable from the
                 * source's - and one uniform blue ARGB4444 texture is drawn
                 * over it nine times with the texel's alpha stepped 0, 2, 4
                 * ... 14, 15. Under destination = source * A + destination *
                 * (1 - A) the blue rises and the red falls, monotonically,
                 * between the two ends. Both ends are measured rather than
                 * assumed: AlphaCurveDstRaw is the fill read back with
                 * nothing drawn over it, AlphaCurveSrcRaw the same texel
                 * drawn with the blend off.
                 *
                 * The walk is then repeated with vertex alpha on an
                 * untextured triangle, which is the engine's other alpha path
                 * - ALPHA_SOURCE|ALPHA_ENABLE where the textured one sets
                 * ALPHA_ENABLE alone - so a defect common to both is told
                 * apart from one that is not.
                 *
                 * The walk is taken three times over rotated operand pairs -
                 * blue over red, red over green, green over blue - so a
                 * blend that mishandles A is told from one the operands never
                 * reach.
                 *
                 * Keys: AlphaCurve_<a>_Raw, AlphaCurveB_<a>_Raw and
                 * AlphaCurveC_<a>_Raw for the three pairs,
                 * VtxAlphaCurve_<a>_Raw for the vertex walk, each with
                 * *DstRaw, *SrcRaw, *Hr and *Ok. Ok means both ends landed
                 * where the blend equation puts them and the source's channel
                 * never fell as A rose.
                 */
                {
                    /*
                     * The steps. Texel alpha is four bits, so 0..15 by twos
                     * with the endpoint; vertex alpha is eight, stepped to
                     * the same nine points so the two curves line up.
                     */
                    static const DWORD curve_texel_alpha[9] = {
                        0ul, 2ul, 4ul, 6ul, 8ul, 10ul, 12ul, 14ul, 15ul };
                    static const DWORD curve_vertex_alpha[9] = {
                        0ul, 34ul, 68ul, 102ul, 136ul, 170ul, 204ul,
                        238ul, 255ul };
                    /*
                     * Three operand pairs, not one. A curve taken with a
                     * single pair of colours cannot tell a blend that
                     * mishandles alpha from one that never sees the operands
                     * at all: rotate them, and if the output's channels do
                     * not rotate with them, the operands are not what is
                     * reaching the blender. Each pair's source is an opaque
                     * primary in the texture and its destination a different
                     * primary in the target.
                     */
                    static const char *curve_tex_keys[3][9] = {
                        { "AlphaCurve_0_Raw", "AlphaCurve_2_Raw",
                          "AlphaCurve_4_Raw", "AlphaCurve_6_Raw",
                          "AlphaCurve_8_Raw", "AlphaCurve_10_Raw",
                          "AlphaCurve_12_Raw", "AlphaCurve_14_Raw",
                          "AlphaCurve_15_Raw" },
                        { "AlphaCurveB_0_Raw", "AlphaCurveB_2_Raw",
                          "AlphaCurveB_4_Raw", "AlphaCurveB_6_Raw",
                          "AlphaCurveB_8_Raw", "AlphaCurveB_10_Raw",
                          "AlphaCurveB_12_Raw", "AlphaCurveB_14_Raw",
                          "AlphaCurveB_15_Raw" },
                        { "AlphaCurveC_0_Raw", "AlphaCurveC_2_Raw",
                          "AlphaCurveC_4_Raw", "AlphaCurveC_6_Raw",
                          "AlphaCurveC_8_Raw", "AlphaCurveC_10_Raw",
                          "AlphaCurveC_12_Raw", "AlphaCurveC_14_Raw",
                          "AlphaCurveC_15_Raw" } };
                    static const char *curve_pair_keys[3][4] = {
                        { "AlphaCurveDstRaw", "AlphaCurveSrcRaw",
                          "AlphaCurveHr", "AlphaCurveOk" },
                        { "AlphaCurveBDstRaw", "AlphaCurveBSrcRaw",
                          "AlphaCurveBHr", "AlphaCurveBOk" },
                        { "AlphaCurveCDstRaw", "AlphaCurveCSrcRaw",
                          "AlphaCurveCHr", "AlphaCurveCOk" } };
                    /* Destination r,g,b; source texel; dst then src channel. */
                    static const DWORD curve_pairs[3][6] = {
                        { 255ul, 0ul, 0ul, 0x0000000ful,
                          V9X_PROBE_CHANNEL_RED, V9X_PROBE_CHANNEL_BLUE },
                        { 0ul, 255ul, 0ul, 0x00000f00ul,
                          V9X_PROBE_CHANNEL_GREEN, V9X_PROBE_CHANNEL_RED },
                        { 0ul, 0ul, 255ul, 0x000000f0ul,
                          V9X_PROBE_CHANNEL_BLUE, V9X_PROBE_CHANNEL_GREEN } };
                    static const char *curve_vtx_keys[9] = {
                        "VtxAlphaCurve_0_Raw", "VtxAlphaCurve_34_Raw",
                        "VtxAlphaCurve_68_Raw", "VtxAlphaCurve_102_Raw",
                        "VtxAlphaCurve_136_Raw", "VtxAlphaCurve_170_Raw",
                        "VtxAlphaCurve_204_Raw", "VtxAlphaCurve_238_Raw",
                        "VtxAlphaCurve_255_Raw" };
                    /*
                     * A channel is 0..255 after the layout expands it. 197
                     * and 33 are the thresholds the hue classifier already
                     * uses for "present" and "absent"; a curve that rises
                     * must not fall by more than one 5-bit step, which is 8.
                     */
                    static const DWORD curve_present = 197ul;
                    static const DWORD curve_absent = 33ul;
                    static const DWORD curve_step_slack = 8ul;
                    struct v9x_dds *curve_surface = 0;
                    struct v9x_d3d_texture2 *curve_texture = 0;
                    DWORD curve_handle = 0ul;
                    WORD curve_dst_raw = 0u;
                    WORD curve_src_raw = 0u;
                    WORD curve_tex_raw[9];
                    WORD curve_vtx_raw[9];
                    DWORD curve_fill;
                    DWORD ci;
                    DWORD curve_pair;
                    DWORD curve_ok;
                    HRESULT curve_hr;
                    HRESULT curve_draw_hr = 0;
                    V9X_PROBE_COUNTS curve_before;
                    V9X_PROBE_COUNTS curve_after;
                    int curve_counts_ok = v9x_probe_counts(&curve_before);

                    for (ci = 0ul; ci < 9ul; ++ci) {
                        curve_tex_raw[ci] = 0u;
                        curve_vtx_raw[ci] = 0u;
                    }

                    curve_fill = 0ul;
                    v9x_zero(&desc, sizeof(desc));
                    desc.dwSize = sizeof(desc);
                    desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH |
                                   V9X_DDSD_HEIGHT | V9X_DDSD_PIXELFORMAT;
                    desc.dwWidth = 64ul;
                    desc.dwHeight = 64ul;
                    desc.ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;
                    desc.ddpfPixelFormat.dwSize = sizeof(V9X_DDPIXELFORMAT);
                    desc.ddpfPixelFormat.dwFlags = 0x00000041ul;
                    desc.ddpfPixelFormat.dwRGBBitCount = 16ul;
                    desc.ddpfPixelFormat.dwRBitMask = 0x00000f00ul;
                    desc.ddpfPixelFormat.dwGBitMask = 0x000000f0ul;
                    desc.ddpfPixelFormat.dwBBitMask = 0x0000000ful;
                    desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0x0000f000ul;
                    curve_hr = ddraw->vtbl->CreateSurface(ddraw, &desc,
                                                          &curve_surface, 0);
                    v9x_write_hresult("AlphaCurveSurfaceHr", curve_hr);
                    if (curve_hr == 0 && curve_surface != 0) {
                        curve_hr = curve_surface->vtbl->QueryInterface(
                            curve_surface, &v9x_iid_d3d_texture2,
                            (void **)&curve_texture);
                    }
                    if (curve_hr == 0 && curve_texture != 0) {
                        curve_hr = curve_texture->vtbl->GetHandle(
                            curve_texture, d3d_device, &curve_handle);
                    }

                    for (curve_pair = 0ul; curve_pair < 3ul; ++curve_pair) {
                        DWORD dst_channel = curve_pairs[curve_pair][4];
                        DWORD src_channel = curve_pairs[curve_pair][5];
                        DWORD opaque_texel = curve_pairs[curve_pair][3] |
                                             0x0000f000ul;

                        curve_fill = target_layout.valid != 0ul
                            ? (DWORD)v9x_layout_pack(&target_layout,
                                                     curve_pairs[curve_pair][0],
                                                     curve_pairs[curve_pair][1],
                                                     curve_pairs[curve_pair][2])
                            : 0x7c00ul;
                        curve_fill |= curve_fill << 16;
                        curve_draw_hr = 0;

                        /* The destination on its own: nothing drawn over it. */
                        v9x_fill_surface(d3d_target, curve_fill);
                        curve_dst_raw = v9x_surface_pixel16(d3d_target,
                                                            16ul, 16ul);
                        v9x_write_uint(curve_pair_keys[curve_pair][0],
                                       curve_dst_raw);

                        if (curve_hr == 0 && curve_handle != 0ul) {
                            /*
                             * The source on its own: the opaque texel with the
                             * blend off, which is the value the curve must
                             * reach at A = 15 and nowhere else.
                             */
                            v9x_fill_surface(curve_surface,
                                opaque_texel | (opaque_texel << 16));
                            (void)d3d_device->vtbl->SetRenderState(d3d_device,
                                V9X_D3DRENDERSTATE_TEXTUREHANDLE, curve_handle);
                            (void)d3d_device->vtbl->SetRenderState(d3d_device,
                                V9X_D3DRENDERSTATE_TEXTUREMAPBLEND,
                                V9X_D3DTBLEND_COPY);
                            v9x_fill_surface(d3d_target, curve_fill);
                            begin_hr = d3d_device->vtbl->BeginScene(d3d_device);
                            if (begin_hr == 0) {
                                curve_draw_hr = d3d_device->vtbl->DrawPrimitive(
                                    d3d_device, V9X_D3DPT_TRIANGLELIST,
                                    V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                                end_hr = d3d_device->vtbl->EndScene(d3d_device);
                                if (end_hr != 0) curve_draw_hr = end_hr;
                            }
                            curve_src_raw = v9x_surface_pixel16(d3d_target,
                                                                16ul, 16ul);

                            (void)d3d_device->vtbl->SetRenderState(d3d_device,
                                V9X_D3DRENDERSTATE_TEXTUREMAPBLEND,
                                V9X_D3DTBLEND_MODULATE);
                            (void)d3d_device->vtbl->SetRenderState(d3d_device,
                                V9X_D3DRENDERSTATE_SRCBLEND,
                                V9X_D3DBLEND_SRCALPHA);
                            (void)d3d_device->vtbl->SetRenderState(d3d_device,
                                V9X_D3DRENDERSTATE_DESTBLEND,
                                V9X_D3DBLEND_INVSRCALPHA);
                            (void)d3d_device->vtbl->SetRenderState(d3d_device,
                                V9X_D3DRENDERSTATE_ALPHABLENDENABLE, 1ul);
                            for (ci = 0ul; ci < 9ul; ++ci) {
                                DWORD texel = (curve_texel_alpha[ci] << 12) |
                                              curve_pairs[curve_pair][3];

                                v9x_fill_surface(curve_surface,
                                                 texel | (texel << 16));
                                v9x_fill_surface(d3d_target, curve_fill);
                                begin_hr =
                                    d3d_device->vtbl->BeginScene(d3d_device);
                                if (begin_hr == 0) {
                                    HRESULT step_hr =
                                        d3d_device->vtbl->DrawPrimitive(
                                            d3d_device, V9X_D3DPT_TRIANGLELIST,
                                            V9X_D3DVT_TLVERTEX, triangle,
                                            3ul, 0ul);
                                    end_hr =
                                        d3d_device->vtbl->EndScene(d3d_device);
                                    if (end_hr != 0) step_hr = end_hr;
                                    if (step_hr != 0) curve_draw_hr = step_hr;
                                }
                                curve_tex_raw[ci] =
                                    v9x_surface_pixel16(d3d_target, 16ul, 16ul);
                            }
                            (void)d3d_device->vtbl->SetRenderState(d3d_device,
                                V9X_D3DRENDERSTATE_ALPHABLENDENABLE, 0ul);
                            (void)d3d_device->vtbl->SetRenderState(d3d_device,
                                V9X_D3DRENDERSTATE_TEXTUREHANDLE, 0ul);
                        }

                        v9x_write_uint(curve_pair_keys[curve_pair][1],
                                       curve_src_raw);
                        for (ci = 0ul; ci < 9ul; ++ci) {
                            v9x_write_uint(curve_tex_keys[curve_pair][ci],
                                           curve_tex_raw[ci]);
                        }
                        v9x_write_hresult(curve_pair_keys[curve_pair][2],
                                          curve_draw_hr);

                        curve_ok = curve_draw_hr == 0 && curve_handle != 0ul &&
                            target_layout.valid != 0ul ? 1ul : 0ul;
                        if (curve_ok != 0ul) {
                            /* A = 0 must leave the destination standing. */
                            if (v9x_layout_rgb(&target_layout, curve_tex_raw[0],
                                    dst_channel) < curve_present ||
                                v9x_layout_rgb(&target_layout, curve_tex_raw[0],
                                    src_channel) > curve_absent) {
                                curve_ok = 0ul;
                            }
                            /* A = 15 must be the source and nothing else. */
                            if (v9x_layout_rgb(&target_layout, curve_tex_raw[8],
                                    src_channel) < curve_present ||
                                v9x_layout_rgb(&target_layout, curve_tex_raw[8],
                                    dst_channel) > curve_absent) {
                                curve_ok = 0ul;
                            }
                            for (ci = 1ul; ci < 9ul; ++ci) {
                                DWORD prev = v9x_layout_rgb(&target_layout,
                                    curve_tex_raw[ci - 1ul], src_channel);
                                DWORD here = v9x_layout_rgb(&target_layout,
                                    curve_tex_raw[ci], src_channel);

                                if (here + curve_step_slack < prev) {
                                    curve_ok = 0ul;  /* fell as A rose */
                                }
                            }
                        }
                        v9x_write_uint(curve_pair_keys[curve_pair][3], curve_ok);
                    }
                    if (curve_counts_ok) {
                        (void)v9x_probe_counts(&curve_after);
                        v9x_probe_write_deltas("AlphaCurve", &curve_before,
                                               &curve_after);
                    }

                    if (curve_texture != 0) {
                        curve_texture->vtbl->Release(curve_texture);
                    }
                    if (curve_surface != 0) {
                        curve_surface->vtbl->Release(curve_surface);
                    }

                    /*
                     * The same walk with vertex alpha and no texture. The
                     * engine's other alpha path: the untextured blend takes A
                     * from the vertex, and D3DVertexAlphaBlendOk has read 0
                     * on this card since the part was first driven.
                     */
                    v9x_probe_reset_state(d3d_device, triangle);
                    if (curve_counts_ok) {
                        (void)v9x_probe_counts(&curve_before);
                    }
                    /* Back to the first pair's operands: red under blue. */
                    curve_fill = target_layout.valid != 0ul
                        ? (DWORD)v9x_layout_pack(&target_layout, 255ul, 0ul, 0ul)
                        : 0x7c00ul;
                    curve_fill |= curve_fill << 16;
                    v9x_fill_surface(d3d_target, curve_fill);
                    curve_dst_raw = v9x_surface_pixel16(d3d_target, 16ul, 16ul);
                    curve_draw_hr = 0;
                    (void)d3d_device->vtbl->SetRenderState(d3d_device,
                        V9X_D3DRENDERSTATE_TEXTUREHANDLE, 0ul);
                    triangle[0].color = 0xff0000fful;
                    triangle[1].color = 0xff0000fful;
                    triangle[2].color = 0xff0000fful;
                    v9x_fill_surface(d3d_target, curve_fill);
                    begin_hr = d3d_device->vtbl->BeginScene(d3d_device);
                    if (begin_hr == 0) {
                        curve_draw_hr = d3d_device->vtbl->DrawPrimitive(
                            d3d_device, V9X_D3DPT_TRIANGLELIST,
                            V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                        end_hr = d3d_device->vtbl->EndScene(d3d_device);
                        if (end_hr != 0) curve_draw_hr = end_hr;
                    }
                    v9x_write_uint("VtxAlphaCurveSrcRaw",
                                   v9x_surface_pixel16(d3d_target, 16ul, 16ul));
                    v9x_write_uint("VtxAlphaCurveDstRaw", curve_dst_raw);

                    (void)d3d_device->vtbl->SetRenderState(d3d_device,
                        V9X_D3DRENDERSTATE_SRCBLEND, V9X_D3DBLEND_SRCALPHA);
                    (void)d3d_device->vtbl->SetRenderState(d3d_device,
                        V9X_D3DRENDERSTATE_DESTBLEND,
                        V9X_D3DBLEND_INVSRCALPHA);
                    (void)d3d_device->vtbl->SetRenderState(d3d_device,
                        V9X_D3DRENDERSTATE_ALPHABLENDENABLE, 1ul);
                    for (ci = 0ul; ci < 9ul; ++ci) {
                        DWORD vertex_colour =
                            (curve_vertex_alpha[ci] << 24) | 0x000000fful;

                        triangle[0].color = vertex_colour;
                        triangle[1].color = vertex_colour;
                        triangle[2].color = vertex_colour;
                        v9x_fill_surface(d3d_target, curve_fill);
                        begin_hr = d3d_device->vtbl->BeginScene(d3d_device);
                        if (begin_hr == 0) {
                            HRESULT step_hr = d3d_device->vtbl->DrawPrimitive(
                                d3d_device, V9X_D3DPT_TRIANGLELIST,
                                V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                            end_hr = d3d_device->vtbl->EndScene(d3d_device);
                            if (end_hr != 0) step_hr = end_hr;
                            if (step_hr != 0) curve_draw_hr = step_hr;
                        }
                        curve_vtx_raw[ci] =
                            v9x_surface_pixel16(d3d_target, 16ul, 16ul);
                        v9x_write_uint(curve_vtx_keys[ci], curve_vtx_raw[ci]);
                    }
                    (void)d3d_device->vtbl->SetRenderState(d3d_device,
                        V9X_D3DRENDERSTATE_ALPHABLENDENABLE, 0ul);
                    v9x_write_hresult("VtxAlphaCurveHr", curve_draw_hr);

                    curve_ok = curve_draw_hr == 0 &&
                        target_layout.valid != 0ul ? 1ul : 0ul;
                    if (curve_ok != 0ul) {
                        if (v9x_layout_red(&target_layout,
                                           curve_vtx_raw[0]) < curve_present ||
                            v9x_layout_blue(&target_layout,
                                            curve_vtx_raw[0]) > curve_absent) {
                            curve_ok = 0ul;
                        }
                        if (v9x_layout_blue(&target_layout,
                                            curve_vtx_raw[8]) < curve_present ||
                            v9x_layout_red(&target_layout,
                                           curve_vtx_raw[8]) > curve_absent) {
                            curve_ok = 0ul;
                        }
                        for (ci = 1ul; ci < 9ul; ++ci) {
                            DWORD prev = v9x_layout_blue(&target_layout,
                                                         curve_vtx_raw[ci - 1ul]);
                            DWORD here = v9x_layout_blue(&target_layout,
                                                         curve_vtx_raw[ci]);

                            if (here + curve_step_slack < prev) {
                                curve_ok = 0ul;
                            }
                        }
                    }
                    v9x_write_uint("VtxAlphaCurveOk", curve_ok);
                    if (curve_counts_ok) {
                        (void)v9x_probe_counts(&curve_after);
                        v9x_probe_write_deltas("VtxAlphaCurve", &curve_before,
                                               &curve_after);
                    }
                }

                v9x_probe_reset_state(d3d_device, triangle);
                /*
                 * A texture with no mip chain, and known memory after it.
                 *
                 * The Trio3D record of mip-level differences was taken with
                 * the texture stride wrong; this re-asks the question with an
                 * instrument. A 64-texel green texture is created, then a
                 * 32x32 blue texture straight after it - DirectDraw's linear
                 * heap will usually place it where a level 1 would sit, and
                 * MipGapDelta records the actual byte distance so the reading
                 * can be trusted only when it is 8192. The green texture is
                 * drawn with plain LINEAR filtering, and again with
                 * MIPNEAREST, at a size that would want level 1 if there were
                 * one. Green both times is the top level being read; blue is
                 * the memory after it.
                 */
                {
                    struct v9x_dds *top = 0;
                    struct v9x_dds *after = 0;
                    struct v9x_d3d_texture2 *top_tex = 0;
                    DWORD top_handle = 0ul;
                    HRESULT mhr;
                    DWORD top_addr = 0ul;
                    DWORD after_addr = 0ul;
                    WORD linear_raw = 0u;
                    WORD mipnear_raw = 0u;
                    V9X_DDSURFACEDESC adesc;

                    v9x_zero(&desc, sizeof(desc));
                    desc.dwSize = sizeof(desc);
                    desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH |
                                   V9X_DDSD_HEIGHT | V9X_DDSD_PIXELFORMAT;
                    desc.dwWidth = 64ul;
                    desc.dwHeight = 64ul;
                    desc.ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE;
                    desc.ddpfPixelFormat.dwSize = sizeof(V9X_DDPIXELFORMAT);
                    desc.ddpfPixelFormat.dwFlags = 0x00000041ul;
                    desc.ddpfPixelFormat.dwRGBBitCount = 16ul;
                    desc.ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
                    desc.ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
                    desc.ddpfPixelFormat.dwBBitMask = 0x0000001ful;
                    desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0x00008000ul;
                    mhr = ddraw->vtbl->CreateSurface(ddraw, &desc, &top, 0);
                    if (mhr == 0) {
                        desc.dwWidth = 32ul;
                        desc.dwHeight = 32ul;
                        mhr = ddraw->vtbl->CreateSurface(ddraw, &desc, &after, 0);
                    }
                    v9x_write_hresult("MipGapSurfaceHr", mhr);
                    if (mhr == 0 && top != 0 && after != 0) {
                        v9x_fill_surface(top, 0x83e083e0ul);
                        v9x_fill_surface(after, 0x801f801ful);
                        v9x_zero(&adesc, sizeof(adesc));
                        adesc.dwSize = sizeof(adesc);
                        if (top->vtbl->Lock(top, 0, &adesc, V9X_DDLOCK_WAIT, 0) == 0) {
                            top_addr = (DWORD)adesc.lpSurface;
                            top->vtbl->Unlock(top, 0);
                        }
                        v9x_zero(&adesc, sizeof(adesc));
                        adesc.dwSize = sizeof(adesc);
                        if (after->vtbl->Lock(after, 0, &adesc, V9X_DDLOCK_WAIT, 0) == 0) {
                            after_addr = (DWORD)adesc.lpSurface;
                            after->vtbl->Unlock(after, 0);
                        }
                        v9x_write_uint("MipGapDelta", after_addr - top_addr);
                        mhr = top->vtbl->QueryInterface(
                            top, &v9x_iid_d3d_texture2, (void **)&top_tex);
                    }
                    if (mhr == 0 && top_tex != 0) {
                        mhr = top_tex->vtbl->GetHandle(top_tex, d3d_device, &top_handle);
                    }
                    if (mhr == 0 && top_handle != 0ul) {
                        mhr = d3d_device->vtbl->SetRenderState(
                            d3d_device, V9X_D3DRENDERSTATE_TEXTUREHANDLE, top_handle);
                        if (mhr == 0) mhr = d3d_device->vtbl->SetRenderState(
                            d3d_device, V9X_D3DRENDERSTATE_TEXTUREMAPBLEND,
                            V9X_D3DTBLEND_COPY);
                        if (mhr == 0) mhr = d3d_device->vtbl->SetRenderState(
                            d3d_device, V9X_D3DRENDERSTATE_TEXTUREMIN,
                            V9X_D3DFILTER_LINEAR);
                        /* Coordinates spanning the whole texture over a small
                         * triangle: what would want a lower level. */
                        triangle[0].tu = 0.0f; triangle[0].tv = 0.0f;
                        triangle[1].tu = 4.0f; triangle[1].tv = 0.0f;
                        triangle[2].tu = 0.0f; triangle[2].tv = 4.0f;
                        v9x_fill_surface(d3d_target, 0ul);
                        begin_hr = mhr == 0 ? d3d_device->vtbl->BeginScene(d3d_device) : mhr;
                        if (begin_hr == 0) {
                            draw_hr = d3d_device->vtbl->DrawPrimitive(
                                d3d_device, V9X_D3DPT_TRIANGLELIST,
                                V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                            end_hr = d3d_device->vtbl->EndScene(d3d_device);
                        }
                        linear_raw = v9x_surface_pixel16(d3d_target, 16ul, 16ul);
                        v9x_write_uint("MipGapLinearRaw", linear_raw);
                        (void)d3d_device->vtbl->SetRenderState(
                            d3d_device, V9X_D3DRENDERSTATE_TEXTUREMIN,
                            V9X_D3DFILTER_MIPNEAREST);
                        v9x_fill_surface(d3d_target, 0ul);
                        begin_hr = d3d_device->vtbl->BeginScene(d3d_device);
                        if (begin_hr == 0) {
                            draw_hr = d3d_device->vtbl->DrawPrimitive(
                                d3d_device, V9X_D3DPT_TRIANGLELIST,
                                V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                            end_hr = d3d_device->vtbl->EndScene(d3d_device);
                        }
                        mipnear_raw = v9x_surface_pixel16(d3d_target, 16ul, 16ul);
                        v9x_write_uint("MipGapMipNearestRaw", mipnear_raw);
                        v9x_write_uint("MipGapOk",
                            target_layout.valid != 0ul &&
                            v9x_layout_green(&target_layout, linear_raw) >= 197ul &&
                            v9x_layout_green(&target_layout, mipnear_raw) >= 197ul
                            ? 1ul : 0ul);
                        (void)d3d_device->vtbl->SetRenderState(
                            d3d_device, V9X_D3DRENDERSTATE_TEXTUREMIN,
                            V9X_D3DFILTER_NEAREST);
                        (void)d3d_device->vtbl->SetRenderState(
                            d3d_device, V9X_D3DRENDERSTATE_TEXTUREHANDLE, 0ul);
                        triangle[0].tu = 0.125f; triangle[0].tv = 0.125f;
                        triangle[1].tu = 0.875f; triangle[1].tv = 0.125f;
                        triangle[2].tu = 0.125f; triangle[2].tv = 0.875f;
                    }
                    if (top_tex != 0) top_tex->vtbl->Release(top_tex);
                    if (after != 0) after->vtbl->Release(after);
                    if (top != 0) top->vtbl->Release(top);
                }

                v9x_probe_reset_state(d3d_device, triangle);
                /*
                 * THE TEXTURE MATRIX.
                 *
                 * Every texture rung above tests one point; the faults found
                 * on the Trio3D/2X on 2026-09-03 lived between the points -
                 * a stride that only mattered above 64 texels, alpha that
                 * only mattered in one format. This walks the space:
                 *
                 *   size    64, 128, 256
                 *   format  ARGB1555, ARGB4444
                 *   layout  plain; a two-level chain DirectDraw built
                 *           (contiguous); a two-level chain built by hand
                 *           with a filler surface between the levels (gapped)
                 *   filter  NEAREST, LINEAR, MIPNEAREST, LINEARMIPLINEAR;
                 *           and NEAREST once more with texel alpha blended
                 *
                 * Level 0 is green on the left and blue on the right; level 1
                 * is magenta and cyan. Each cell draws the left half and the
                 * right half over black and classifies both pixels by hue. A
                 * cell passes when the left is green or magenta and the right
                 * is blue or cyan - either level, correctly addressed - or,
                 * for the alpha cell, when the right is black (its texels
                 * have alpha 0). Anything else is a wrong address, a wrong
                 * format, a level that was never allocated, or alpha ignored,
                 * and the raw values say which.
                 *
                 * Keys: TexM_<size>_<fmt>_<layout>_<filter>_L, _R, _Ok, and
                 * TexMatrixOk / TexMatrixCount as the summary. Gapped chains
                 * also record TexM_..._Delta, the byte distance from level 0
                 * to level 1, so a reading is trusted only when the gap is
                 * real; a contiguous chain's delta is the level-0 size.
                 */
                {
                    static const DWORD m_sizes[3] = { 64ul, 128ul, 256ul };
                    static const char *m_size_names[3] = { "64", "128", "256" };
                    static const char *m_fmt_names[2] = { "1555", "4444" };
                    static const char *m_layout_names[3] = { "plain", "chain", "gapped" };
                    static const DWORD m_filters[7] = {
                        V9X_D3DFILTER_NEAREST, V9X_D3DFILTER_LINEAR,
                        V9X_D3DFILTER_MIPNEAREST, V9X_D3DFILTER_LINEARMIPLINEAR,
                        V9X_D3DFILTER_NEAREST, V9X_D3DFILTER_NEAREST,
                        V9X_D3DFILTER_NEAREST };
                    /*
                     * Two cells added 2026-09-03 after the first matrix run:
                     * "wrap" draws with coordinates outside the first repeat
                     * (the wrap-bit rung existed once, at 64 texels, and the
                     * Trio3D taught that once is not a measurement), and
                     * "halfa" blends an ARGB4444 texel of alpha 8/15 so a
                     * chip that thresholds alpha rather than blending it is
                     * told apart from one that blends - ARGB1555 has one
                     * alpha bit and skips the cell.
                     */
                    static const char *m_filter_names[7] = {
                        "near", "lin", "mipnear", "trilin", "alpha", "wrap", "halfa" };
                    DWORD m_ok = 0ul;
                    DWORD m_count = 0ul;
                    DWORD m_started = GetTickCount();
                    V9X_PROBE_COUNTS m_before;
                    V9X_PROBE_COUNTS m_after;
                    int m_counts_ok = v9x_probe_counts(&m_before);
                    DWORD si, fi, li, ti;

                    v9x_write_uint("TexMatrixCountsOk", m_counts_ok ? 1ul : 0ul);

                    for (si = 0ul; si < 3ul; ++si)
                    for (fi = 0ul; fi < 2ul; ++fi)
                    for (li = 0ul; li < 3ul; ++li) {
                        struct v9x_dds *m_top = 0;
                        struct v9x_dds *m_level = 0;
                        struct v9x_dds *m_filler = 0;
                        struct v9x_d3d_texture2 *m_tex = 0;
                        DWORD m_handle = 0ul;
                        HRESULT m_hr;
                        DWORD top_addr = 0ul;
                        DWORD level_addr = 0ul;
                        WORD left0, right0, left1, right1, right0_alpha;
                        char m_prefix[48];
                        char m_key[64];
                        V9X_DDSURFACEDESC m_desc;

                        m_prefix[0] = 0;
                        v9x_probe_cat(m_prefix, "TexM_");
                        v9x_probe_cat(m_prefix, m_size_names[si]);
                        v9x_probe_cat(m_prefix, "_");
                        v9x_probe_cat(m_prefix, m_fmt_names[fi]);
                        v9x_probe_cat(m_prefix, "_");
                        v9x_probe_cat(m_prefix, m_layout_names[li]);

                        if (fi == 0ul) {
                            left0 = 0x83e0u; right0 = 0x801fu; right0_alpha = 0x001fu;
                            left1 = 0xfc1fu; right1 = 0x83ffu;
                        } else {
                            left0 = 0xf0f0u; right0 = 0xf00fu; right0_alpha = 0x000fu;
                            left1 = 0xff0fu; right1 = 0xf0ffu;
                        }

                        /* Level 0. */
                        v9x_zero(&desc, sizeof(desc));
                        desc.dwSize = sizeof(desc);
                        desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH |
                                       V9X_DDSD_HEIGHT | V9X_DDSD_PIXELFORMAT;
                        desc.dwWidth = m_sizes[si];
                        desc.dwHeight = m_sizes[si];
                        desc.ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE |
                                              V9X_DDSCAPS_VIDEOMEMORY;
                        if (li == 1ul) {
                            desc.dwFlags |= V9X_DDSD_MIPMAPCOUNT;
                            desc.dwMipMapCount = 2ul;
                            desc.ddsCaps.dwCaps |= V9X_DDSCAPS_COMPLEX |
                                                   V9X_DDSCAPS_MIPMAP;
                        } else if (li == 2ul) {
                            desc.ddsCaps.dwCaps |= V9X_DDSCAPS_MIPMAP;
                        }
                        desc.ddpfPixelFormat.dwSize = sizeof(V9X_DDPIXELFORMAT);
                        desc.ddpfPixelFormat.dwFlags = 0x00000041ul;
                        desc.ddpfPixelFormat.dwRGBBitCount = 16ul;
                        if (fi == 0ul) {
                            desc.ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
                            desc.ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
                            desc.ddpfPixelFormat.dwBBitMask = 0x0000001ful;
                            desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0x00008000ul;
                        } else {
                            desc.ddpfPixelFormat.dwRBitMask = 0x00000f00ul;
                            desc.ddpfPixelFormat.dwGBitMask = 0x000000f0ul;
                            desc.ddpfPixelFormat.dwBBitMask = 0x0000000ful;
                            desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0x0000f000ul;
                        }
                        m_hr = ddraw->vtbl->CreateSurface(ddraw, &desc, &m_top, 0);

                        /* Level 1: from the chain, or built by hand across a
                         * filler so it cannot be where the engine expects. */
                        if (m_hr == 0 && li == 1ul) {
                            caps.dwCaps = V9X_DDSCAPS_MIPMAP;
                            m_hr = m_top->vtbl->GetAttachedSurface(m_top, &caps, &m_level);
                        } else if (m_hr == 0 && li == 2ul) {
                            V9X_DDSURFACEDESC l_desc = desc;

                            l_desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH |
                                             V9X_DDSD_HEIGHT | V9X_DDSD_PIXELFORMAT;
                            l_desc.ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE |
                                                    V9X_DDSCAPS_VIDEOMEMORY;
                            l_desc.dwWidth = 64ul;
                            l_desc.dwHeight = 64ul;
                            m_hr = ddraw->vtbl->CreateSurface(ddraw, &l_desc, &m_filler, 0);
                            if (m_hr == 0) {
                                l_desc.dwWidth = m_sizes[si] / 2ul;
                                l_desc.dwHeight = m_sizes[si] / 2ul;
                                l_desc.ddsCaps.dwCaps |= V9X_DDSCAPS_MIPMAP;
                                m_hr = ddraw->vtbl->CreateSurface(ddraw, &l_desc, &m_level, 0);
                            }
                            if (m_hr == 0) {
                                m_hr = m_top->vtbl->AddAttachedSurface(m_top, m_level);
                                m_key[0] = 0;
                                v9x_probe_cat(m_key, m_prefix);
                                v9x_probe_cat(m_key, "_AttachHr");
                                v9x_write_hresult(m_key, m_hr);
                            }
                        }
                        if (m_hr == 0 && m_top != 0) {
                            v9x_fill_surface_halves(m_top, left0, right0);
                            v9x_zero(&m_desc, sizeof(m_desc));
                            m_desc.dwSize = sizeof(m_desc);
                            if (m_top->vtbl->Lock(m_top, 0, &m_desc, V9X_DDLOCK_WAIT, 0) == 0) {
                                top_addr = (DWORD)m_desc.lpSurface;
                                m_top->vtbl->Unlock(m_top, 0);
                            }
                        }
                        if (m_hr == 0 && m_level != 0) {
                            v9x_fill_surface_halves(m_level, left1, right1);
                            v9x_zero(&m_desc, sizeof(m_desc));
                            m_desc.dwSize = sizeof(m_desc);
                            if (m_level->vtbl->Lock(m_level, 0, &m_desc, V9X_DDLOCK_WAIT, 0) == 0) {
                                level_addr = (DWORD)m_desc.lpSurface;
                                m_level->vtbl->Unlock(m_level, 0);
                            }
                            m_key[0] = 0;
                            v9x_probe_cat(m_key, m_prefix);
                            v9x_probe_cat(m_key, "_Delta");
                            v9x_write_uint(m_key, level_addr - top_addr);
                        }
                        if (m_hr == 0 && m_top != 0) {
                            m_hr = m_top->vtbl->QueryInterface(
                                m_top, &v9x_iid_d3d_texture2, (void **)&m_tex);
                        }
                        if (m_hr == 0 && m_tex != 0) {
                            m_hr = m_tex->vtbl->GetHandle(m_tex, d3d_device, &m_handle);
                        }
                        m_key[0] = 0;
                        v9x_probe_cat(m_key, m_prefix);
                        v9x_probe_cat(m_key, "_Hr");
                        v9x_write_hresult(m_key, m_hr);

                        for (ti = 0ul; ti < 7ul && m_hr == 0 && m_handle != 0ul; ++ti) {
                            WORD l_raw = 0u;
                            WORD r_raw = 0u;
                            HRESULT l_hr = 0x80004005ul;
                            HRESULT r_hr = 0x80004005ul;
                            DWORD l_hue, r_hue;
                            DWORD cell_ok;
                            HRESULT c_hr;
                            int blended = ti == 4ul || ti == 6ul;

                            if (ti == 6ul && fi == 0ul) {
                                continue;   /* one alpha bit: no half */
                            }
                            v9x_probe_reset_state(d3d_device, triangle);
                            if (m_counts_ok) {
                                v9x_probe_counts(&m_before);
                            }
                            if (ti == 4ul) {
                                /* Right half alpha 0, then blended. */
                                v9x_fill_surface_halves(m_top, left0, right0_alpha);
                            } else if (ti == 6ul) {
                                /* Right half alpha 8 of 15, then blended. */
                                v9x_fill_surface_halves(m_top, left0, 0x800fu);
                            } else if (ti == 0ul || ti == 5ul) {
                                v9x_fill_surface_halves(m_top, left0, right0);
                            }
                            c_hr = d3d_device->vtbl->SetRenderState(
                                d3d_device, V9X_D3DRENDERSTATE_TEXTUREHANDLE, m_handle);
                            if (c_hr == 0) c_hr = d3d_device->vtbl->SetRenderState(
                                d3d_device, V9X_D3DRENDERSTATE_TEXTUREMAPBLEND,
                                blended ? V9X_D3DTBLEND_MODULATE : V9X_D3DTBLEND_COPY);
                            if (c_hr == 0) c_hr = d3d_device->vtbl->SetRenderState(
                                d3d_device, V9X_D3DRENDERSTATE_TEXTUREMIN, m_filters[ti]);
                            if (c_hr == 0 && blended) {
                                c_hr = d3d_device->vtbl->SetRenderState(
                                    d3d_device, V9X_D3DRENDERSTATE_SRCBLEND,
                                    V9X_D3DBLEND_SRCALPHA);
                                if (c_hr == 0) c_hr = d3d_device->vtbl->SetRenderState(
                                    d3d_device, V9X_D3DRENDERSTATE_DESTBLEND,
                                    V9X_D3DBLEND_INVSRCALPHA);
                                if (c_hr == 0) c_hr = d3d_device->vtbl->SetRenderState(
                                    d3d_device, V9X_D3DRENDERSTATE_ALPHABLENDENABLE, 1ul);
                            }
                            v9x_fill_surface(d3d_target, 0ul);
                            triangle[0].tu = 0.10f; triangle[0].tv = 0.10f;
                            triangle[1].tu = 0.40f; triangle[1].tv = 0.10f;
                            triangle[2].tu = 0.10f; triangle[2].tv = 0.40f;
                            if (ti == 5ul) {
                                triangle[0].tu = -0.40f; triangle[1].tu = -0.10f;
                                triangle[2].tu = -0.40f;
                            }
                            begin_hr = c_hr == 0 ? d3d_device->vtbl->BeginScene(d3d_device) : c_hr;
                            if (begin_hr == 0) {
                                l_hr = d3d_device->vtbl->DrawPrimitive(
                                    d3d_device, V9X_D3DPT_TRIANGLELIST,
                                    V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                                end_hr = d3d_device->vtbl->EndScene(d3d_device);
                                if (end_hr != 0) l_hr = end_hr;
                            }
                            l_raw = v9x_surface_pixel16(d3d_target, 16ul, 16ul);
                            v9x_fill_surface(d3d_target, 0ul);
                            triangle[0].tu = 0.60f; triangle[0].tv = 0.10f;
                            triangle[1].tu = 0.90f; triangle[1].tv = 0.10f;
                            triangle[2].tu = 0.60f; triangle[2].tv = 0.40f;
                            if (ti == 5ul) {
                                triangle[0].tu = 1.10f; triangle[1].tu = 1.40f;
                                triangle[2].tu = 1.10f;
                            }
                            begin_hr = c_hr == 0 ? d3d_device->vtbl->BeginScene(d3d_device) : c_hr;
                            if (begin_hr == 0) {
                                r_hr = d3d_device->vtbl->DrawPrimitive(
                                    d3d_device, V9X_D3DPT_TRIANGLELIST,
                                    V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                                end_hr = d3d_device->vtbl->EndScene(d3d_device);
                                if (end_hr != 0) r_hr = end_hr;
                            }
                            r_raw = v9x_surface_pixel16(d3d_target, 16ul, 16ul);
                            l_hue = v9x_probe_hue(&target_layout, l_raw);
                            r_hue = v9x_probe_hue(&target_layout, r_raw);
                            /* LINEARMIPLINEAR may blend the two levels'
                             * colours, which is neither hue; a chain cell
                             * under it passes on "something was drawn on
                             * both halves" and the raw values carry the
                             * rest. Every other cell must be one of the
                             * exact hues. */
                            if (ti == 3ul && li != 0ul) {
                                cell_ok = l_hr == 0 && r_hr == 0 &&
                                    l_hue != V9X_PROBE_HUE_BLACK &&
                                    r_hue != V9X_PROBE_HUE_BLACK ? 1ul : 0ul;
                            } else if (ti == 5ul) {
                                /* Wrapped: the halves swap. */
                                cell_ok = l_hr == 0 && r_hr == 0 &&
                                    (l_hue == V9X_PROBE_HUE_BLUE ||
                                     l_hue == V9X_PROBE_HUE_CYAN) &&
                                    (r_hue == V9X_PROBE_HUE_GREEN ||
                                     r_hue == V9X_PROBE_HUE_MAGENTA) ? 1ul : 0ul;
                            } else if (ti == 6ul) {
                                /* Half alpha: blue at roughly half over
                                 * black, and nothing in the other channels.
                                 * 70..190 of 255 admits the chip's rounding
                                 * and excludes both "opaque" and "gone". */
                                DWORD hb = v9x_layout_blue(&target_layout, r_raw);
                                DWORD hr_ = v9x_layout_red(&target_layout, r_raw);
                                DWORD hg = v9x_layout_green(&target_layout, r_raw);

                                cell_ok = l_hr == 0 && r_hr == 0 &&
                                    (l_hue == V9X_PROBE_HUE_GREEN ||
                                     l_hue == V9X_PROBE_HUE_MAGENTA) &&
                                    target_layout.valid != 0ul &&
                                    hb >= 70ul && hb <= 190ul &&
                                    hr_ <= 33ul && hg <= 33ul ? 1ul : 0ul;
                            } else {
                                cell_ok = l_hr == 0 && r_hr == 0 &&
                                    (l_hue == V9X_PROBE_HUE_GREEN ||
                                     l_hue == V9X_PROBE_HUE_MAGENTA) &&
                                    (ti == 4ul ? r_hue == V9X_PROBE_HUE_BLACK
                                               : (r_hue == V9X_PROBE_HUE_BLUE ||
                                                  r_hue == V9X_PROBE_HUE_CYAN))
                                    ? 1ul : 0ul;
                            }
                            m_key[0] = 0;
                            v9x_probe_cat(m_key, m_prefix);
                            v9x_probe_cat(m_key, "_");
                            v9x_probe_cat(m_key, m_filter_names[ti]);
                            v9x_probe_cat(m_key, "_L");
                            v9x_write_uint(m_key, l_raw);
                            m_key[0] = 0;
                            v9x_probe_cat(m_key, m_prefix);
                            v9x_probe_cat(m_key, "_");
                            v9x_probe_cat(m_key, m_filter_names[ti]);
                            v9x_probe_cat(m_key, "_R");
                            v9x_write_uint(m_key, r_raw);
                            m_key[0] = 0;
                            v9x_probe_cat(m_key, m_prefix);
                            v9x_probe_cat(m_key, "_");
                            v9x_probe_cat(m_key, m_filter_names[ti]);
                            v9x_probe_cat(m_key, "_Ok");
                            v9x_write_uint(m_key, cell_ok);
                            if (l_hr != 0 || r_hr != 0) {
                                m_key[0] = 0;
                                v9x_probe_cat(m_key, m_prefix);
                                v9x_probe_cat(m_key, "_");
                                v9x_probe_cat(m_key, m_filter_names[ti]);
                                v9x_probe_cat(m_key, "_Hr");
                                v9x_write_hresult(m_key, l_hr != 0 ? l_hr : r_hr);
                            }
                            if (m_counts_ok) {
                                v9x_probe_counts(&m_after);
                                m_key[0] = 0;
                                v9x_probe_cat(m_key, m_prefix);
                                v9x_probe_cat(m_key, "_");
                                v9x_probe_cat(m_key, m_filter_names[ti]);
                                v9x_probe_write_deltas(m_key, &m_before, &m_after);
                            }
                            ++m_count;
                            m_ok += cell_ok;
                        }
                        v9x_probe_reset_state(d3d_device, triangle);
                        if (m_tex != 0) m_tex->vtbl->Release(m_tex);
                        if (li == 2ul && m_level != 0) m_level->vtbl->Release(m_level);
                        if (m_filler != 0) m_filler->vtbl->Release(m_filler);
                        if (m_top != 0) m_top->vtbl->Release(m_top);
                    }
                    v9x_write_uint("TexMatrixOk", m_ok);
                    v9x_write_uint("TexMatrixCount", m_count);
                    v9x_write_uint("TexMatrixMs", GetTickCount() - m_started);
                }

                /*
                 * RENDER TARGETS OF REAL SIZES, WITH DEPTH AND A TEXTURE.
                 *
                 * Every target above is one 64x64 offscreen surface. Games
                 * render to 640x480 and up, and a pitch fault would hide at 64
 * exactly as the texture stride fault hid at 64 texels. Each size
                 * here gets its own target, its own 16-bit depth surface, its
                 * own device and viewport, and a 64-texel texture; then three
                 * draws that between them exercise what no rung above does at
                 * once - a texture with depth on:
                 *
                 *   1. ZFUNC ALWAYS, left half of the texture, z 0.5  -> green
                 *   2. ZFUNC LESS,   right half,               z 0.75 -> still green
                 *   3. ZFUNC LESS,   right half,               z 0.25 -> blue
                 *
                 * Keys: Tgt_<w>_Hr, _Pitch, _TexRaw/_TexOk, _ZRejectRaw/Ok,
                 * _ZAcceptRaw/Ok, and TargetsOk / TargetsCount / TargetsMs.
                 * The largest size may not fit beside everything else the
                 * probe has allocated; its _Hr says so and it is not counted.
                 */
                {
                    static const DWORD t_w[4] = { 320ul, 640ul, 800ul, 1024ul };
                    static const DWORD t_h[4] = { 240ul, 480ul, 600ul, 768ul };
                    static const char *t_names[4] = { "320", "640", "800", "1024" };
                    DWORD t_ok = 0ul;
                    DWORD t_count = 0ul;
                    DWORD t_started = GetTickCount();
                    DWORD ti2;

                    for (ti2 = 0ul; ti2 < 4ul; ++ti2) {
                        struct v9x_dds *t_target = 0;
                        struct v9x_dds *t_z = 0;
                        struct v9x_dds *t_texs = 0;
                        struct v9x_d3d_texture2 *t_tex = 0;
                        struct v9x_d3d_device2 *t_device = 0;
                        struct v9x_d3d_viewport2 *t_viewport = 0;
                        V9X_D3D_VIEWPORT_DESC2 t_view;
                        V9X_D3DTLVERTEX t_tri[3];
                        DWORD t_handle = 0ul;
                        HRESULT t_hr;
                        char t_prefix[24];
                        char t_key[48];
                        WORD raw_tex = 0u, raw_rej = 0u, raw_acc = 0u;
                        DWORD ok_tex = 0ul, ok_rej = 0ul, ok_acc = 0ul;
                        V9X_PROBE_COUNTS t_before, t_after;
                        int t_counts_ok;

                        t_prefix[0] = 0;
                        v9x_probe_cat(t_prefix, "Tgt_");
                        v9x_probe_cat(t_prefix, t_names[ti2]);

                        v9x_zero(&desc, sizeof(desc));
                        desc.dwSize = sizeof(desc);
                        desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH | V9X_DDSD_HEIGHT;
                        desc.dwWidth = t_w[ti2];
                        desc.dwHeight = t_h[ti2];
                        desc.ddsCaps.dwCaps = V9X_DDSCAPS_3DDEVICE |
                                              V9X_DDSCAPS_OFFSCREENPLAIN |
                                              V9X_DDSCAPS_VIDEOMEMORY;
                        t_hr = ddraw->vtbl->CreateSurface(ddraw, &desc, &t_target, 0);
                        t_key[0] = 0; v9x_probe_cat(t_key, t_prefix); v9x_probe_cat(t_key, "_TargetHr");
                        v9x_write_hresult(t_key, t_hr);
                        if (t_hr == 0) {
                            v9x_zero(&desc, sizeof(desc));
                            desc.dwSize = sizeof(desc);
                            if (t_target->vtbl->GetSurfaceDesc(t_target, &desc) == 0) {
                                t_key[0] = 0; v9x_probe_cat(t_key, t_prefix);
                                v9x_probe_cat(t_key, "_Pitch");
                                v9x_write_uint(t_key, (DWORD)desc.lPitch);
                            }
                            v9x_zero(&desc, sizeof(desc));
                            desc.dwSize = sizeof(desc);
                            desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH |
                                           V9X_DDSD_HEIGHT | V9X_DDSD_ZBUFFERBITDEPTH;
                            desc.dwWidth = t_w[ti2];
                            desc.dwHeight = t_h[ti2];
                            desc.dwMipMapCount = 16ul;   /* dwZBufferBitDepth */
                            desc.ddsCaps.dwCaps = V9X_DDSCAPS_ZBUFFER |
                                                  V9X_DDSCAPS_VIDEOMEMORY;
                            t_hr = ddraw->vtbl->CreateSurface(ddraw, &desc, &t_z, 0);
                            t_key[0] = 0; v9x_probe_cat(t_key, t_prefix); v9x_probe_cat(t_key, "_ZHr");
                            v9x_write_hresult(t_key, t_hr);
                        }
                        if (t_hr == 0) {
                            t_hr = t_target->vtbl->AddAttachedSurface(t_target, t_z);
                            t_key[0] = 0; v9x_probe_cat(t_key, t_prefix); v9x_probe_cat(t_key, "_AttachHr");
                            v9x_write_hresult(t_key, t_hr);
                        }
                        if (t_hr == 0) {
                            t_hr = d3d->vtbl->CreateDevice(d3d, &v9x_iid_d3d_hal,
                                                           t_target, &t_device);
                            t_key[0] = 0; v9x_probe_cat(t_key, t_prefix); v9x_probe_cat(t_key, "_DeviceHr");
                            v9x_write_hresult(t_key, t_hr);
                        }
                        if (t_hr == 0) {
                            t_hr = d3d->vtbl->CreateViewport(d3d, (void **)&t_viewport, 0);
                        }
                        if (t_hr == 0) {
                            t_hr = t_device->vtbl->AddViewport(t_device, t_viewport);
                        }
                        if (t_hr == 0) {
                            v9x_zero(&t_view, sizeof(t_view));
                            t_view.dwSize = sizeof(t_view);
                            t_view.dwWidth = t_w[ti2];
                            t_view.dwHeight = t_h[ti2];
                            t_view.dvClipX = -1.0f;
                            t_view.dvClipY = 1.0f;
                            t_view.dvClipWidth = 2.0f;
                            t_view.dvClipHeight = 2.0f;
                            t_view.dvMinZ = 0.0f;
                            t_view.dvMaxZ = 1.0f;
                            t_hr = t_viewport->vtbl->SetViewport2(t_viewport, &t_view);
                        }
                        if (t_hr == 0) {
                            t_hr = t_device->vtbl->SetCurrentViewport(t_device, t_viewport);
                        }
                        if (t_hr == 0) {
                            v9x_zero(&desc, sizeof(desc));
                            desc.dwSize = sizeof(desc);
                            desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH |
                                           V9X_DDSD_HEIGHT | V9X_DDSD_PIXELFORMAT;
                            desc.dwWidth = 64ul;
                            desc.dwHeight = 64ul;
                            desc.ddsCaps.dwCaps = V9X_DDSCAPS_TEXTURE | V9X_DDSCAPS_VIDEOMEMORY;
                            desc.ddpfPixelFormat.dwSize = sizeof(V9X_DDPIXELFORMAT);
                            desc.ddpfPixelFormat.dwFlags = 0x00000041ul;
                            desc.ddpfPixelFormat.dwRGBBitCount = 16ul;
                            desc.ddpfPixelFormat.dwRBitMask = 0x00007c00ul;
                            desc.ddpfPixelFormat.dwGBitMask = 0x000003e0ul;
                            desc.ddpfPixelFormat.dwBBitMask = 0x0000001ful;
                            desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0x00008000ul;
                            t_hr = ddraw->vtbl->CreateSurface(ddraw, &desc, &t_texs, 0);
                        }
                        if (t_hr == 0) {
                            v9x_fill_surface_halves(t_texs, 0x83e0u, 0x801fu);
                            t_hr = t_texs->vtbl->QueryInterface(
                                t_texs, &v9x_iid_d3d_texture2, (void **)&t_tex);
                        }
                        if (t_hr == 0) {
                            t_hr = t_tex->vtbl->GetHandle(t_tex, t_device, &t_handle);
                        }
                        t_key[0] = 0; v9x_probe_cat(t_key, t_prefix); v9x_probe_cat(t_key, "_Hr");
                        v9x_write_hresult(t_key, t_hr);

                        if (t_hr == 0 && t_handle != 0ul) {
                            DWORD step;
                            static const float step_z[3] = { 0.5f, 0.75f, 0.25f };
                            static const DWORD step_func[3] = {
                                V9X_D3DCMP_ALWAYS, V9X_D3DCMP_LESS, V9X_D3DCMP_LESS };
                            static const float step_u0[3] = { 0.10f, 0.60f, 0.60f };

                            t_counts_ok = v9x_probe_counts(&t_before);
                            v9x_fill_surface(t_target, 0ul);
                            v9x_probe_reset_state(t_device, t_tri);
                            (void)t_device->vtbl->SetRenderState(t_device,
                                V9X_D3DRENDERSTATE_TEXTUREHANDLE, t_handle);
                            (void)t_device->vtbl->SetRenderState(t_device,
                                V9X_D3DRENDERSTATE_TEXTUREMAPBLEND, V9X_D3DTBLEND_COPY);
                            (void)t_device->vtbl->SetRenderState(t_device,
                                V9X_D3DRENDERSTATE_ZENABLE, 1ul);
                            (void)t_device->vtbl->SetRenderState(t_device,
                                V9X_D3DRENDERSTATE_ZWRITEENABLE, 1ul);
                            for (step = 0ul; step < 3ul; ++step) {
                                WORD raw;
                                HRESULT s_hr;

                                (void)t_device->vtbl->SetRenderState(t_device,
                                    V9X_D3DRENDERSTATE_ZFUNC, step_func[step]);
                                t_tri[0].sx = 8.25f;  t_tri[0].sy = 8.25f;
                                t_tri[1].sx = 55.75f; t_tri[1].sy = 8.25f;
                                t_tri[2].sx = 8.25f;  t_tri[2].sy = 55.75f;
                                t_tri[0].sz = t_tri[1].sz = t_tri[2].sz = step_z[step];
                                t_tri[0].rhw = t_tri[1].rhw = t_tri[2].rhw = 1.0f;
                                t_tri[0].color = t_tri[1].color = t_tri[2].color = 0xffffffff;
                                t_tri[0].specular = t_tri[1].specular = t_tri[2].specular = 0ul;
                                t_tri[0].tu = step_u0[step];         t_tri[0].tv = 0.10f;
                                t_tri[1].tu = step_u0[step] + 0.30f; t_tri[1].tv = 0.10f;
                                t_tri[2].tu = step_u0[step];         t_tri[2].tv = 0.40f;
                                s_hr = t_device->vtbl->BeginScene(t_device);
                                if (s_hr == 0) {
                                    s_hr = t_device->vtbl->DrawPrimitive(
                                        t_device, V9X_D3DPT_TRIANGLELIST,
                                        V9X_D3DVT_TLVERTEX, t_tri, 3ul, 0ul);
                                    end_hr = t_device->vtbl->EndScene(t_device);
                                    if (end_hr != 0) s_hr = end_hr;
                                }
                                raw = v9x_surface_pixel16(t_target, 16ul, 16ul);
                                if (step == 0ul) {
                                    raw_tex = raw;
                                    ok_tex = s_hr == 0 &&
                                        v9x_probe_hue(&target_layout, raw) == V9X_PROBE_HUE_GREEN;
                                } else if (step == 1ul) {
                                    raw_rej = raw;
                                    ok_rej = s_hr == 0 &&
                                        v9x_probe_hue(&target_layout, raw) == V9X_PROBE_HUE_GREEN;
                                } else {
                                    raw_acc = raw;
                                    ok_acc = s_hr == 0 &&
                                        v9x_probe_hue(&target_layout, raw) == V9X_PROBE_HUE_BLUE;
                                }
                                if (s_hr != 0) {
                                    t_key[0] = 0; v9x_probe_cat(t_key, t_prefix);
                                    v9x_probe_cat(t_key, step == 0ul ? "_TexHr" :
                                                         step == 1ul ? "_ZRejectHr" : "_ZAcceptHr");
                                    v9x_write_hresult(t_key, s_hr);
                                }
                            }
                            t_key[0] = 0; v9x_probe_cat(t_key, t_prefix); v9x_probe_cat(t_key, "_TexRaw");
                            v9x_write_uint(t_key, raw_tex);
                            t_key[0] = 0; v9x_probe_cat(t_key, t_prefix); v9x_probe_cat(t_key, "_TexOk");
                            v9x_write_uint(t_key, ok_tex);
                            t_key[0] = 0; v9x_probe_cat(t_key, t_prefix); v9x_probe_cat(t_key, "_ZRejectRaw");
                            v9x_write_uint(t_key, raw_rej);
                            t_key[0] = 0; v9x_probe_cat(t_key, t_prefix); v9x_probe_cat(t_key, "_ZRejectOk");
                            v9x_write_uint(t_key, ok_rej);
                            t_key[0] = 0; v9x_probe_cat(t_key, t_prefix); v9x_probe_cat(t_key, "_ZAcceptRaw");
                            v9x_write_uint(t_key, raw_acc);
                            t_key[0] = 0; v9x_probe_cat(t_key, t_prefix); v9x_probe_cat(t_key, "_ZAcceptOk");
                            v9x_write_uint(t_key, ok_acc);
                            if (t_counts_ok) {
                                v9x_probe_counts(&t_after);
                                v9x_probe_write_deltas(t_prefix, &t_before, &t_after);
                            }
                            t_count += 3ul;
                            t_ok += ok_tex + ok_rej + ok_acc;
                        }
                        if (t_tex != 0) t_tex->vtbl->Release(t_tex);
                        if (t_texs != 0) t_texs->vtbl->Release(t_texs);
                        if (t_viewport != 0) {
                            struct v9x_dds *u = (struct v9x_dds *)t_viewport; u->vtbl->Release(u);
                        }
                        if (t_device != 0) {
                            struct v9x_dds *u = (struct v9x_dds *)t_device; u->vtbl->Release(u);
                        }
                        if (t_z != 0) t_z->vtbl->Release(t_z);
                        if (t_target != 0) t_target->vtbl->Release(t_target);
                    }
                    v9x_write_uint("TargetsOk", t_ok);
                    v9x_write_uint("TargetsCount", t_count);
                    v9x_write_uint("TargetsMs", GetTickCount() - t_started);
                }

                v9x_probe_reset_state(d3d_device, triangle);
                /*
                 * A blend the S3D unit cannot express: DESTCOLOR over ZERO,
                 * the multiplicative pass lightmaps use. The target is
                 * filled green and the triangle is white, so a correct
                 * multiply leaves green, a driver that declines the pass
                 * leaves green, and a driver that draws it opaque - which is
                 * what produced 3DMark 99's saw-toothed panels - leaves
                 * white. Green is the only acceptable answer; how it was
                 * reached is what D3dBlendSkipped in the trace block says.
                 */
                {
                    HRESULT blend_hr;
                    WORD blend_raw;

                    v9x_fill_surface(d3d_target, 0x03e003e0ul);
                    blend_hr = d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_ALPHABLENDENABLE, 1ul);
                    if (blend_hr == 0) {
                        blend_hr = d3d_device->vtbl->SetRenderState(
                            d3d_device, V9X_D3DRENDERSTATE_SRCBLEND,
                            V9X_D3DBLEND_DESTCOLOR);
                    }
                    if (blend_hr == 0) {
                        blend_hr = d3d_device->vtbl->SetRenderState(
                            d3d_device, V9X_D3DRENDERSTATE_DESTBLEND,
                            V9X_D3DBLEND_ZERO_F);
                    }
                    triangle[0].color = 0xffffffff;
                    triangle[1].color = 0xffffffff;
                    triangle[2].color = 0xffffffff;
                    begin_hr = blend_hr == 0
                        ? d3d_device->vtbl->BeginScene(d3d_device)
                        : blend_hr;
                    if (begin_hr == 0) {
                        draw_hr = d3d_device->vtbl->DrawPrimitive(
                            d3d_device, V9X_D3DPT_TRIANGLELIST,
                            V9X_D3DVT_TLVERTEX, triangle, 3ul, 0ul);
                        end_hr = d3d_device->vtbl->EndScene(d3d_device);
                    }
                    blend_raw = v9x_surface_pixel16(d3d_target, 16ul, 16ul);
                    v9x_write_hresult("BlendModulateHr", blend_hr);
                    v9x_write_uint("BlendModulateRaw", blend_raw);
                    v9x_write_uint("BlendModulateOk",
                        begin_hr == 0 && draw_hr == 0 && end_hr == 0 &&
                        blend_raw == 0x03e0u ? 1ul : 0ul);
                    (void)d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_ALPHABLENDENABLE, 0ul);
                    (void)d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_SRCBLEND,
                        V9X_D3DBLEND_ONE_F);
                    (void)d3d_device->vtbl->SetRenderState(
                        d3d_device, V9X_D3DRENDERSTATE_DESTBLEND,
                        V9X_D3DBLEND_ZERO_F);
                }

                /*
                 * Depth buffering, on the device that has already drawn
                 * everything above.
                 *
                 * Skipped under /zprivate, which runs the private-device
                 * design further down instead. The two are mutually
                 * exclusive on purpose: the driver's depth counters are
                 * cumulative, so a run in which both designs offer a depth
                 * surface cannot say which offer produced which count - and
                 * that ambiguity is the whole reason those counters exist.
                 *
                 * Running last on the working device costs isolation and buys
                 * the thing that matters: the render-state and primitive calls
                 * demonstrably reach the driver, because the driver's counters
                 * move when they run. Every pixel key above is already written
                 * by this point, so nothing here can retract one.
                 *
                 * What this design cannot do is bind the depth surface. It
                 * attaches the surface and re-drives SetRenderTarget, which is
                 * the DDK's own route (D3DCB2.C:45-68) - but on this runtime
                 * IDirect3DDevice2::SetRenderTarget never reaches
                 * V9xD3dSetRenderTarget at all, and returns S_OK regardless.
                 * D3DZDeviceHr = 0 from this block therefore says nothing
                 * about the driver having seen or validated the surface; the
                 * D3dDepthOffered counter is what says that. The declaration
                 * of SetRenderTarget in the vtable above is kept because the
                 * negative result is worth re-checking on any other runtime.
                 *
                 * Every vertex carries the same sz. Depth gradients are not
                 * tested: 86Box doubles a triangle's start depth but not its
                 * per-pixel X gradient (build\reference-vid_s3_virge.c:4261
                 * against :4413), so a sloped test would measure that
                 * inconsistency rather than this driver. D3DZGradientTested
                 * stays 0 to say so.
                 *
                 * The ladders are self-initialising: nothing assumes the depth
                 * buffer starts at any value, because the driver implements no
                 * depth clear and DirectDraw's software one is not what this
                 * is testing.
                 */
                if (!v9x_has_switch("/zprivate")) {
                    struct v9x_dds *z_surface = 0;
                    HRESULT z_hr;

                    v9x_zero(&desc, sizeof(desc));
                    desc.dwSize = sizeof(desc);
                    desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH |
                                   V9X_DDSD_HEIGHT |
                                   V9X_DDSD_ZBUFFERBITDEPTH;
                    desc.dwWidth = 64ul;
                    desc.dwHeight = 64ul;
                    /* The union slot DDSURFACEDESC shares between
                     * dwMipMapCount, dwZBufferBitDepth and dwRefreshRate. */
                    desc.dwMipMapCount = 16ul;
                    desc.ddsCaps.dwCaps = V9X_DDSCAPS_ZBUFFER |
                                          V9X_DDSCAPS_VIDEOMEMORY;
                    z_hr = ddraw->vtbl->CreateSurface(ddraw, &desc,
                                                      &z_surface, 0);
                    v9x_write_hresult("D3DZSurfaceHr", z_hr);

                    if (z_hr == 0 && z_surface != 0) {
                        /* Where it actually landed. "Created but in system
                         * memory" and "not created" must not look the same:
                         * the driver refuses a system-memory depth surface. */
                        v9x_zero(&desc, sizeof(desc));
                        desc.dwSize = sizeof(desc);
                        if (z_surface->vtbl->GetSurfaceDesc(z_surface,
                                                            &desc) == 0) {
                            v9x_write_uint("D3DZSurfacePitch",
                                           (DWORD)desc.lPitch);
                            v9x_write_uint("D3DZSurfaceCaps",
                                           desc.ddsCaps.dwCaps);
                        }
                        /* Before the attach, so a depth fill is tested on a
                         * plain Z surface rather than on one the render
                         * target owns - the driver's Blt path does not care,
                         * and neither should this. */
                        v9x_probe_depth_fill(z_surface);

                        z_hr = d3d_target->vtbl->AddAttachedSurface(
                            d3d_target, z_surface);
                        v9x_write_hresult("D3DZAttachHr", z_hr);
                    }
                    if (z_hr == 0) {
                        z_hr = d3d_device->vtbl->SetRenderTarget(
                            d3d_device, d3d_target, 0ul);
                        v9x_write_hresult("D3DZDeviceHr", z_hr);
                    }

                    if (z_hr == 0) {
                        DWORD init_raw = 65535ul;
                        DWORD reject_raw = 65535ul;
                        DWORD accept_raw = 65535ul;
                        DWORD update_raw = 65535ul;
                        DWORD nowrite_raw = 65535ul;
                        DWORD mask_raw = 65535ul;
                        HRESULT z_state_hr = 0;
                        HRESULT z_draw_hr = 0;
                        int ladder_ok;
                        int mask_ok;

                        triangle[0].sx = 8.25f;
                        triangle[0].sy = 8.25f;
                        triangle[0].rhw = 1.0f;
                        triangle[0].specular = 0ul;
                        triangle[0].tu = 0.0f;
                        triangle[0].tv = 0.0f;
                        triangle[1] = triangle[0];
                        triangle[1].sx = 55.75f;
                        triangle[2] = triangle[0];
                        triangle[2].sy = 55.75f;

                        v9x_fill_surface(d3d_target, 0ul);

                        /*
                         * Ladder one: does the comparison work, and does a
                         * passing fragment update the buffer. A distinct
                         * colour per rung, so "unchanged" is proved by the
                         * pixel still being the previous colour rather than
                         * merely not being the new one. Rung D closes the last
                         * hole: without it, a buffer that keeps comparing
                         * against A's 0.5 for ever - never taking C's write -
                         * still passes A to C.
                         */
                        ladder_ok = v9x_z_step(d3d_device, triangle, 0.5f,
                                               0xffff0000ul,
                                               V9X_D3DCMP_ALWAYS, 1ul,
                                               d3d_target, &init_raw,
                                               &z_state_hr, &z_draw_hr) &&
                                    init_raw == (DWORD)expect_red;
                        ladder_ok = v9x_z_step(d3d_device, triangle, 0.75f,
                                               0xff00ff00ul,
                                               V9X_D3DCMP_LESS, 1ul,
                                               d3d_target, &reject_raw,
                                               0, 0) &&
                                    reject_raw == (DWORD)expect_red &&
                                    ladder_ok;
                        ladder_ok = v9x_z_step(d3d_device, triangle, 0.25f,
                                               0xff0000fful,
                                               V9X_D3DCMP_LESS, 1ul,
                                               d3d_target, &accept_raw,
                                               0, 0) &&
                                    accept_raw == (DWORD)expect_blue &&
                                    ladder_ok;
                        ladder_ok = v9x_z_step(d3d_device, triangle, 0.5f,
                                               0xfffffffful,
                                               V9X_D3DCMP_LESS, 1ul,
                                               d3d_target, &update_raw,
                                               0, 0) &&
                                    update_raw == (DWORD)expect_blue &&
                                    ladder_ok;
                        v9x_write_hresult("D3DZStateHr", z_state_hr);
                        v9x_write_hresult("D3DZDrawHr", z_draw_hr);
                        v9x_write_uint("D3DZInitRaw", init_raw);
                        v9x_write_uint("D3DZRejectRaw", reject_raw);
                        v9x_write_uint("D3DZAcceptRaw", accept_raw);
                        v9x_write_uint("D3DZUpdateRaw", update_raw);
                        v9x_write_uint("D3DZCompareOk",
                                       ladder_ok ? 1ul : 0ul);

                        /*
                         * Ladder two: does a cleared write-enable really
                         * suppress the depth write. Seed 0.25, draw 0.125 with
                         * the write masked off, then draw 0.1875 - which is
                         * behind 0.125 but in front of 0.25, so it is accepted
                         * only if the masked draw left the buffer at 0.25.
                         */
                        v9x_fill_surface(d3d_target, 0ul);
                        mask_ok = v9x_z_step(d3d_device, triangle, 0.25f,
                                             0xfffffffful,
                                             V9X_D3DCMP_ALWAYS, 1ul,
                                             d3d_target, &mask_raw, 0, 0) &&
                                  mask_raw == (DWORD)expect_white;
                        mask_ok = v9x_z_step(d3d_device, triangle, 0.125f,
                                             0xff00ff00ul,
                                             V9X_D3DCMP_LESS, 0ul,
                                             d3d_target, &nowrite_raw,
                                             0, 0) &&
                                  nowrite_raw == (DWORD)expect_green &&
                                  mask_ok;
                        mask_ok = v9x_z_step(d3d_device, triangle, 0.1875f,
                                             0xffff0000ul,
                                             V9X_D3DCMP_LESS, 1ul,
                                             d3d_target, &mask_raw, 0, 0) &&
                                  mask_raw == (DWORD)expect_red && mask_ok;
                        v9x_write_uint("D3DZNoWriteRaw", nowrite_raw);
                        v9x_write_uint("D3DZMaskRaw", mask_raw);
                        v9x_write_uint("D3DZWriteMaskOk",
                                       mask_ok ? 1ul : 0ul);

                        (void)d3d_device->vtbl->SetRenderState(
                            d3d_device, V9X_D3DRENDERSTATE_ZENABLE, 0ul);
                    }
                    if (z_surface != 0) {
                        z_surface->vtbl->Release(z_surface);
                    }
                }

                if (d3d_viewport != 0) {
                    d3d_device->vtbl->DeleteViewport(d3d_device,
                                                     d3d_viewport);
                    d3d_viewport->vtbl->Release(d3d_viewport);
                    d3d_viewport = 0;
                }
                if (texture2 != 0) {
                    texture2->vtbl->Release(texture2);
                    texture2 = 0;
                }
                if (texture != 0) {
                    texture->vtbl->Release(texture);
                    texture = 0;
                }
                if (texture_surface2 != 0) {
                    if (texture_mip_level != 0) {
                        texture_mip_level->vtbl->Release(texture_mip_level);
                        texture_mip_level = 0;
                    }
                    texture_surface2->vtbl->Release(texture_surface2);
                    texture_surface2 = 0;
                }
                if (texture_surface != 0) {
                    texture_surface->vtbl->Release(texture_surface);
                    texture_surface = 0;
                }
                d3d_device->vtbl->Release(d3d_device);
                d3d_device = 0;
                v9x_write_uint("D3DContextCycleOk", 1ul);
            } else {
                v9x_write_uint("D3DContextCycleOk", 0ul);
            }
        }
    }

    /*
     * Depth buffering, second design: a private render target, a depth
     * surface attached to it before CreateDevice, and a private device.
     *
     * This is the only route by which this runtime can hand the driver a
     * depth surface. IDirect3DDevice2::SetRenderTarget never reaches
     * V9xD3dSetRenderTarget - the driver's counter for it stays absent across
     * every run, including runs whose pixels are correct - so lpDDSZ in
     * D3DHAL_CONTEXTCREATEDATA, filled at context creation, is what is left.
     * That is why the surface is attached before CreateDevice and not after.
     *
     * Run only under /zprivate, so the driver's depth counters belong
     * unambiguously to this block. It gets its own target rather than sharing
     * d3d_target because a depth surface that failed validation would fail
     * context creation with it, taking every pixel result above down with it
     * for a reason that has nothing to do with what they measure.
     *
     * An earlier attempt at this design reported S_OK from every call and
     * moved none of the driver's counters. It was read as a driver fault. The
     * device-identity report below exists because that reading was never
     * checked: CreateDevice returning S_OK for IID_IDirect3DHALDevice does
     * not establish that the object returned is the HAL.
     *
     * Every vertex carries the same sz. Depth gradients are deliberately
     * untested: 86Box doubles a triangle's start depth but not its per-pixel
     * X gradient (build\reference-vid_s3_virge.c:4261 against :4413), so a
     * sloped test would measure that inconsistency rather than this driver.
     *
     * The ladders are self-initialising - nothing assumes the depth buffer
     * starts at any value, because the driver implements no depth clear.
     */
    if (d3d != 0 && d3d_result.hal_found != 0ul &&
        v9x_has_switch("/zprivate")) {
        struct v9x_dds *z_target = 0;
        struct v9x_dds *z_surface = 0;
        struct v9x_d3d_device2 *z_device = 0;
        struct v9x_d3d_viewport2 *z_viewport = 0;
        HRESULT z_hr;

        v9x_write_uint("D3DZPrivateRun", 1ul);

        v9x_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH | V9X_DDSD_HEIGHT;
        desc.dwWidth = 64ul;
        desc.dwHeight = 64ul;
        desc.ddsCaps.dwCaps = V9X_DDSCAPS_3DDEVICE |
                              V9X_DDSCAPS_OFFSCREENPLAIN |
                              V9X_DDSCAPS_VIDEOMEMORY;
        z_hr = ddraw->vtbl->CreateSurface(ddraw, &desc, &z_target, 0);
        v9x_write_hresult("D3DZPTargetHr", z_hr);

        if (z_hr == 0 && z_target != 0) {
            v9x_zero(&desc, sizeof(desc));
            desc.dwSize = sizeof(desc);
            desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH |
                           V9X_DDSD_HEIGHT | V9X_DDSD_ZBUFFERBITDEPTH;
            desc.dwWidth = 64ul;
            desc.dwHeight = 64ul;
            /* The union slot DDSURFACEDESC shares between dwMipMapCount,
             * dwZBufferBitDepth and dwRefreshRate. */
            desc.dwMipMapCount = 16ul;
            desc.ddsCaps.dwCaps = V9X_DDSCAPS_ZBUFFER |
                                  V9X_DDSCAPS_VIDEOMEMORY;
            z_hr = ddraw->vtbl->CreateSurface(ddraw, &desc, &z_surface, 0);
            v9x_write_hresult("D3DZPSurfaceHr", z_hr);
        }

        if (z_hr == 0 && z_surface != 0) {
            /* Where it actually landed. "Created but in system memory" and
             * "not created" must not look the same: the driver refuses a
             * system-memory depth surface. */
            v9x_zero(&desc, sizeof(desc));
            desc.dwSize = sizeof(desc);
            if (z_surface->vtbl->GetSurfaceDesc(z_surface, &desc) == 0) {
                v9x_write_uint("D3DZPSurfacePitch", (DWORD)desc.lPitch);
                v9x_write_uint("D3DZPSurfaceCaps", desc.ddsCaps.dwCaps);
            }
            z_hr = z_target->vtbl->AddAttachedSurface(z_target, z_surface);
            v9x_write_hresult("D3DZPAttachHr", z_hr);
        }

        if (z_hr == 0 && z_target != 0) {
            z_hr = d3d->vtbl->CreateDevice(d3d, &v9x_iid_d3d_hal,
                                           z_target, &z_device);
            v9x_write_hresult("D3DZPDeviceHr", z_hr);
        }

        if (z_hr == 0 && z_device != 0) {
            v9x_report_device("D3DZP", z_device);
        }

        if (z_hr == 0 && z_device != 0) {
            V9X_D3DTLVERTEX z_tri[3];
            V9X_D3D_VIEWPORT_DESC2 z_view;
            DWORD init_raw = 65535ul;
            DWORD reject_raw = 65535ul;
            DWORD accept_raw = 65535ul;
            DWORD update_raw = 65535ul;
            DWORD nowrite_raw = 65535ul;
            DWORD mask_raw = 65535ul;
            HRESULT z_state_hr = 0;
            HRESULT z_draw_hr = 0;
            int ladder_ok;
            int mask_ok;

            z_hr = d3d->vtbl->CreateViewport(d3d, (void **)&z_viewport, 0);
            if (z_hr == 0 && z_viewport != 0) {
                z_hr = z_device->vtbl->AddViewport(z_device, z_viewport);
            }
            if (z_hr == 0) {
                v9x_zero(&z_view, sizeof(z_view));
                z_view.dwSize = sizeof(z_view);
                z_view.dwWidth = 64ul;
                z_view.dwHeight = 64ul;
                z_view.dvClipX = -1.0f;
                z_view.dvClipY = 1.0f;
                z_view.dvClipWidth = 2.0f;
                z_view.dvClipHeight = 2.0f;
                z_view.dvMinZ = 0.0f;
                z_view.dvMaxZ = 1.0f;
                z_hr = z_viewport->vtbl->SetViewport2(z_viewport, &z_view);
            }
            if (z_hr == 0) {
                z_hr = z_device->vtbl->SetCurrentViewport(z_device,
                                                          z_viewport);
            }
            v9x_write_hresult("D3DZPViewportHr", z_hr);

            if (z_hr == 0) {
                z_tri[0].sx = 8.25f;
                z_tri[0].sy = 8.25f;
                z_tri[0].sz = 0.5f;
                z_tri[0].rhw = 1.0f;
                z_tri[0].color = 0xffff0000ul;
                z_tri[0].specular = 0ul;
                z_tri[0].tu = 0.0f;
                z_tri[0].tv = 0.0f;
                z_tri[1] = z_tri[0];
                z_tri[1].sx = 55.75f;
                z_tri[2] = z_tri[0];
                z_tri[2].sy = 55.75f;

                v9x_fill_surface(z_target, 0ul);

                /*
                 * Ladder one: does the comparison work, and does a passing
                 * fragment update the buffer. A distinct colour per rung, so
                 * "unchanged" is proved by the pixel still being the previous
                 * colour rather than merely not being the new one. Rung D
                 * closes the last hole: without it, a buffer that keeps
                 * comparing against A's 0.5 for ever - never taking C's write
                 * - still passes A to C.
                 */
                ladder_ok = v9x_z_step(z_device, z_tri, 0.5f, 0xffff0000ul,
                                       V9X_D3DCMP_ALWAYS, 1ul,
                                       z_target, &init_raw,
                                       &z_state_hr, &z_draw_hr) &&
                            init_raw == (DWORD)expect_red;
                ladder_ok = v9x_z_step(z_device, z_tri, 0.75f, 0xff00ff00ul,
                                       V9X_D3DCMP_LESS, 1ul,
                                       z_target, &reject_raw, 0, 0) &&
                            reject_raw == (DWORD)expect_red && ladder_ok;
                ladder_ok = v9x_z_step(z_device, z_tri, 0.25f, 0xff0000fful,
                                       V9X_D3DCMP_LESS, 1ul,
                                       z_target, &accept_raw, 0, 0) &&
                            accept_raw == (DWORD)expect_blue && ladder_ok;
                ladder_ok = v9x_z_step(z_device, z_tri, 0.5f, 0xfffffffful,
                                       V9X_D3DCMP_LESS, 1ul,
                                       z_target, &update_raw, 0, 0) &&
                            update_raw == (DWORD)expect_blue && ladder_ok;
                v9x_write_hresult("D3DZPStateHr", z_state_hr);
                v9x_write_hresult("D3DZPDrawHr", z_draw_hr);
                v9x_write_uint("D3DZPInitRaw", init_raw);
                v9x_write_uint("D3DZPRejectRaw", reject_raw);
                v9x_write_uint("D3DZPAcceptRaw", accept_raw);
                v9x_write_uint("D3DZPUpdateRaw", update_raw);
                v9x_write_uint("D3DZPCompareOk", ladder_ok ? 1ul : 0ul);

                /*
                 * Ladder two: does a cleared write-enable really suppress the
                 * depth write. Seed 0.25, draw 0.125 with the write masked
                 * off, then draw 0.1875 - which is behind 0.125 but in front
                 * of 0.25, so it is accepted only if the masked draw left the
                 * buffer at 0.25.
                 */
                v9x_fill_surface(z_target, 0ul);
                mask_ok = v9x_z_step(z_device, z_tri, 0.25f, 0xfffffffful,
                                     V9X_D3DCMP_ALWAYS, 1ul,
                                     z_target, &mask_raw, 0, 0) &&
                          mask_raw == (DWORD)expect_white;
                mask_ok = v9x_z_step(z_device, z_tri, 0.125f, 0xff00ff00ul,
                                     V9X_D3DCMP_LESS, 0ul,
                                     z_target, &nowrite_raw, 0, 0) &&
                          nowrite_raw == (DWORD)expect_green && mask_ok;
                mask_ok = v9x_z_step(z_device, z_tri, 0.1875f, 0xffff0000ul,
                                     V9X_D3DCMP_LESS, 1ul,
                                     z_target, &mask_raw, 0, 0) &&
                          mask_raw == (DWORD)expect_red && mask_ok;
                v9x_write_uint("D3DZPNoWriteRaw", nowrite_raw);
                v9x_write_uint("D3DZPMaskRaw", mask_raw);
                v9x_write_uint("D3DZPWriteMaskOk", mask_ok ? 1ul : 0ul);
            }
        }

        /*
         * Teardown only. Nothing above returns early, so Result still reaches
         * COMPLETE whether or not any of this worked.
         */
        if (z_viewport != 0) {
            if (z_device != 0) {
                z_device->vtbl->DeleteViewport(z_device, z_viewport);
            }
            z_viewport->vtbl->Release(z_viewport);
        }
        if (z_device != 0) {
            z_device->vtbl->Release(z_device);
        }
        if (z_surface != 0) {
            z_surface->vtbl->Release(z_surface);
        }
        if (z_target != 0) {
            z_target->vtbl->Release(z_target);
        }
    }

    if (backbuffer != 0) {
        HRESULT fill_done;
        HRESULT fill_can;

        started = 0ul;
        fill_can = backbuffer->vtbl->GetBltStatus(backbuffer,
                                                  V9X_DDGBS_CANBLT);
        v9x_write_hresult("BltCanHr", fill_can);
        if (v9x_has_switch("/status-only")) {
            if (d3d_target != 0) {
                d3d_target->vtbl->Release(d3d_target);
            }
            if (d3d != 0) {
                d3d->vtbl->Release(d3d);
            }
            backbuffer->vtbl->Release(backbuffer);
            primary->vtbl->Release(primary);
            ddraw->vtbl->RestoreDisplayMode(ddraw);
            ddraw->vtbl->SetCooperativeLevel(ddraw, window,
                                              V9X_DDSCL_NORMAL);
            ddraw->vtbl->Release(ddraw);
            DestroyWindow(window);
            v9x_write_text("Result", "STATUS-ONLY");
            ExitProcess(fill_can == 0 ? 0u : 2u);
        }
        if (fill_can == 0) {
            hr = v9x_hardware_fill(backbuffer, 0x000007e0ul, &started,
                                   &fill_done);
        } else {
            hr = fill_can;
            fill_done = fill_can;
        }
        v9x_write_hresult("BltFillHr", hr);
        v9x_write_hresult("BltFillDoneHr", fill_done);
        v9x_write_uint("BltFillMs", started);
        v9x_write_uint("BltFillPixelOk",
                       hr == 0 && fill_done == 0 &&
                       v9x_surface_pixel16_equals(backbuffer, 100ul, 100ul,
                                                  0x07e0u) ? 1ul : 0ul);

        /* Raw surface write cost, then the flip itself. */
        v9x_write_uint("BackFillMs", v9x_time_surface_fill(backbuffer));
        v9x_write_uint("PrimaryFillMs", v9x_time_surface_fill(primary));

        do {
            hr = primary->vtbl->Flip(primary, 0, V9X_DDFLIP_WAIT);
        } while (hr == (HRESULT)V9X_DDERR_WASSTILLDRAWING);
        v9x_write_hresult("FlipHr", hr);
        if (hr == 0) {
            started = v9x_time();
            for (index = 0; index < 20; ++index) {
                DWORD flip_started = v9x_time();

                do {
                    hr = primary->vtbl->Flip(primary, 0, V9X_DDFLIP_WAIT);
                } while (hr == (HRESULT)V9X_DDERR_WASSTILLDRAWING);
                elapsed = v9x_time() - flip_started;
                if (elapsed > flip_max) {
                    flip_max = elapsed;
                }
            }
            flip_total = v9x_time() - started;
            v9x_write_uint("Flip20Ms", flip_total);
            v9x_write_uint("FlipMaxMs", flip_max);

            /* Flip correctness: fill the backbuffer with a known color,
             * flip, and read the visible screen back through GDI. Two
             * rounds with different colors catch a flip that never moves
             * the display as well as one stuck on a single page. */
            {
                HDC screen;
                COLORREF seen_red;
                COLORREF seen_blue;
                int pixel_ok;
                /* /hold pauses on each verification color so the emulated
                 * scanout can be captured from the host: GDI readback only
                 * sees the fixed GDI page once real flips are in play. */
                int hold = v9x_has_switch("/hold");

                v9x_fill_surface(backbuffer, 0xf800f800ul);
                do {
                    hr = primary->vtbl->Flip(primary, 0, V9X_DDFLIP_WAIT);
                } while (hr == (HRESULT)V9X_DDERR_WASSTILLDRAWING);
                if (hold) {
                    Sleep(5000);
                }
                screen = GetDC(0);
                seen_red = GetPixel(screen, 100, 100);
                ReleaseDC(0, screen);

                v9x_fill_surface(backbuffer, 0x001f001ful);
                do {
                    hr = primary->vtbl->Flip(primary, 0, V9X_DDFLIP_WAIT);
                } while (hr == (HRESULT)V9X_DDERR_WASSTILLDRAWING);
                if (hold) {
                    Sleep(5000);
                }
                screen = GetDC(0);
                seen_blue = GetPixel(screen, 100, 100);
                ReleaseDC(0, screen);

                pixel_ok = seen_red != CLR_INVALID &&
                           seen_blue != CLR_INVALID &&
                           GetRValue(seen_red) > 0xc0u &&
                           GetGValue(seen_red) < 0x40u &&
                           GetBValue(seen_red) < 0x40u &&
                           GetBValue(seen_blue) > 0xc0u &&
                           GetRValue(seen_blue) < 0x40u &&
                           GetGValue(seen_blue) < 0x40u;
                v9x_write_uint("FlipPixelOk", pixel_ok ? 1u : 0u);
            }
        }
    }

    /* Stage-surface availability, mirroring the game's fallback ladder. */
    v9x_zero(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH | V9X_DDSD_HEIGHT;
    desc.dwWidth = 640ul;
    desc.dwHeight = 480ul;
    desc.ddsCaps.dwCaps = V9X_DDSCAPS_OFFSCREENPLAIN |
                          V9X_DDSCAPS_VIDEOMEMORY;
    hr = ddraw->vtbl->CreateSurface(ddraw, &desc, &stage, 0);
    v9x_write_hresult("VideoStageHr", hr);
    if (hr == 0 && stage != 0) {
        stage->vtbl->Release(stage);
        stage = 0;
    }
    desc.ddsCaps.dwCaps = V9X_DDSCAPS_OFFSCREENPLAIN |
                          V9X_DDSCAPS_SYSTEMMEMORY;
    hr = ddraw->vtbl->CreateSurface(ddraw, &desc, &stage, 0);
    v9x_write_hresult("SystemStageHr", hr);
    if (hr == 0 && stage != 0) {
        stage->vtbl->Release(stage);
        stage = 0;
    }

    /*
     * Source-copy blit. The driver advertises ROP3 SRCCOPY because the Win9x
     * runtime refuses a DDCAPS_BLT HAL without it, but implements only solid
     * colour fills, so this blit must be declined by the HAL and completed by
     * the HEL. The pixel check proves the fallback is correct, and pairing it
     * with the HAL's BltEngine counter proves the driver did not execute it.
     */
    if (backbuffer != 0) {
        v9x_zero(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = V9X_DDSD_CAPS | V9X_DDSD_WIDTH | V9X_DDSD_HEIGHT;
        desc.dwWidth = 64ul;
        desc.dwHeight = 64ul;
        desc.ddsCaps.dwCaps = V9X_DDSCAPS_OFFSCREENPLAIN |
                              V9X_DDSCAPS_VIDEOMEMORY;
        hr = ddraw->vtbl->CreateSurface(ddraw, &desc, &stage, 0);
        v9x_write_hresult("SrcCopySurfaceHr", hr);
        if (hr == 0 && stage != 0) {
            RECT source_rect;
            RECT destination_rect;
            V9X_DDCOLORKEY source_key;

            v9x_fill_surface(stage, 0x001f001ful);
            v9x_fill_surface(backbuffer, 0xf800f800ul);
            source_rect.left = 0;
            source_rect.top = 0;
            source_rect.right = 64;
            source_rect.bottom = 64;
            destination_rect.left = 32;
            destination_rect.top = 32;
            destination_rect.right = 96;
            destination_rect.bottom = 96;
            hr = backbuffer->vtbl->Blt(backbuffer, &destination_rect, stage,
                                       &source_rect, V9X_DDBLT_WAIT, 0);
            v9x_write_hresult("SrcCopyBltHr", hr);
            v9x_write_uint("SrcCopyPixelOk",
                           hr == 0 &&
                           v9x_surface_pixel16_equals(backbuffer, 64ul, 64ul,
                                                      0x001fu) &&
                           v9x_surface_pixel16_equals(backbuffer, 16ul, 16ul,
                                                      0xf800u) ? 1ul : 0ul);

            /* Operations the driver deliberately does not advertise. The
             * runtime owns the refusal - DDCAPS_BLTSTRETCH and
             * DDCAPS_COLORKEY are absent - so these must fail without ever
             * reaching the HAL, which could not complete them. */
            destination_rect.right = 160;
            destination_rect.bottom = 160;
            hr = backbuffer->vtbl->Blt(backbuffer, &destination_rect, stage,
                                       &source_rect, V9X_DDBLT_WAIT, 0);
            v9x_write_hresult("StretchBltHr", hr);
            destination_rect.right = 96;
            destination_rect.bottom = 96;
            source_key.dwColorSpaceLowValue = 0x001ful;
            source_key.dwColorSpaceHighValue = 0x001ful;
            hr = stage->vtbl->SetColorKey(stage, V9X_DDCKEY_SRCBLT,
                                          &source_key);
            v9x_write_hresult("KeySrcSetHr", hr);
            hr = backbuffer->vtbl->Blt(backbuffer, &destination_rect, stage,
                                       &source_rect,
                                       V9X_DDBLT_WAIT | V9X_DDBLT_KEYSRC, 0);
            v9x_write_hresult("KeySrcBltHr", hr);

            stage->vtbl->Release(stage);
            stage = 0;
        }
    }

    v9x_test_overlap(ddraw, backbuffer);

    /* Diagnostic hold keeps the DirectDraw object and HAL instance alive so
     * V9XTRACE can snapshot callback dispatch from a second process. */
    if (v9x_has_switch("/hold")) {
        v9x_write_text("Hold", "active");
        Sleep(15000ul);
    }

    if (backbuffer != 0) {
        backbuffer->vtbl->Release(backbuffer);
    }
    if (primary != 0) {
        primary->vtbl->Release(primary);
    }
    if (d3d_target != 0) {
        d3d_target->vtbl->Release(d3d_target);
    }
    if (d3d != 0) {
        d3d->vtbl->Release(d3d);
    }
    hr = ddraw->vtbl->RestoreDisplayMode(ddraw);
    v9x_write_hresult("RestoreHr", hr);
    ddraw->vtbl->SetCooperativeLevel(ddraw, window, V9X_DDSCL_NORMAL);
    ddraw->vtbl->Release(ddraw);
    DestroyWindow(window);
    v9x_write_text("Result", "COMPLETE");
    WritePrivateProfileStringA(0, 0, 0, V9X_RESULT_PATH);
    ExitProcess(0u);
}
