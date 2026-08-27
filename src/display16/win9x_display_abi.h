#ifndef VELOCITY9X_WIN9X_DISPLAY_ABI_H
#define VELOCITY9X_WIN9X_DISPLAY_ABI_H

/*
 * Minimal public Windows 9x display/DDI structures used by Velocity9x.
 * These declarations intentionally cover only the fields needed by the
 * fixed 8-bpp DIB Engine bring-up paths.
 */

#include <windows.h>

#define V9X_GDIINFO_SIZE             110u
#define V9X_DIBENGINE_SIZE            48u

#define V9X_DRV_VERSION           0x0400u
#define V9X_DT_RASDISPLAY         0x0001u
#define V9X_DC_IGNORE_DFNP        0x0004u
#define V9X_RC_PALETTE            0x0100u
#define V9X_RC_DIBTODEV           0x0200u
#define V9X_C1_DIBENGINE          0x0010u
#define V9X_C1_REINIT_ABLE        0x0080u
#define V9X_C1_BYTE_PACKED        0x0400u
#define V9X_C1_COLORCURSOR        0x0800u
#define V9X_C1_SLOW_CARD          0x2000u

#define V9X_DE_MINIDRIVER         0x0001u
#define V9X_DE_PALETTIZED         0x0002u
#define V9X_DE_SELECTEDDIB        0x0004u
#define V9X_DE_OFFSCREEN          0x0008u
#define V9X_DE_BUSY               0x0010u
#define V9X_DE_FIVE6FIVE          0x0040u
/*
 * The destination needs a background palette translation the drawing engine
 * cannot perform. Every accelerated GDI path has to test it: an accelerated
 * fill under a translated palette produces a right-shaped, wrong-coloured
 * rectangle. Both Windows 98 DDK reference blitters gate on it
 * (98DDK\src\display\mini\xga\BITBLT.ASM:71 and
 * 98DDK\src\display\mini\s3v\S3BLT.ASM:130).
 */
/*
 * deType holds TYPE_DIBENG or **zero** (DIBENG.INC:55). Zero means the struct
 * is not a DIBENGINE at all but a plain Win16 BITMAP, and the two layouts share
 * only their first ten bytes - so a field read past that boundary from the
 * wrong one is garbage. Build 004's first enabled run read deBitsOffset (18)
 * out of a BITMAP and got 0x20000000; the discriminator is not optional.
 */
#define V9X_TYPE_DIBENG                  0x5250u

/*
 * The plain Win16 BITMAP, which is what GDI hands over as the source of a
 * monochrome BitBlt - the reference driver reads exactly these fields
 * (98DDK\src\display\mini\s3v\S3BLT.ASM:944-950, bmWidthBytes and bmBits).
 *
 * bmBits is a plain 16:16 far pointer - `bmBits dd 0`, "Far pointer to bits of
 * main memory bitmap" (GDIDEFS.INC:48) - and **not** an fword like deBits. The
 * offset half is therefore 16 bits by definition, which is why the reference's
 * `mov si,ds:[si.bmBits.off]` is a plain word move and not the truncation it
 * looks like next to a DIBENGINE.
 */
typedef struct {
    WORD bmType;           /* 0 for a BITMAP, V9X_TYPE_DIBENG for a DIBENGINE */
    WORD bmWidth;
    WORD bmHeight;
    WORD bmWidthBytes;
    BYTE bmPlanes;
    BYTE bmBitsPixel;
    WORD bmBitsOffset;
    WORD bmBitsSelector;
} V9X_BITMAP16;

typedef char v9x_assert_bitmap16[sizeof(V9X_BITMAP16) == 14 ? 1 : -1];

#define V9X_DE_PALETTE_XLAT       0x1000u
#define V9X_DE_VRAM               0x8000u
#define V9X_DE_VERSION            0x0400u
#define V9X_TYPE_DIBENG           0x5250u

/*
 * DIBEngine.deBeginAccess flags, from C:\98DDK\inc\win98\inc16\DIBENG.INC.
 * That file carries both an assembly equ (:126-127) and a C #define
 * (:131-132) of each, so these have a first-party source. There is no
 * dibeng.h in this DDK.
 */
#define V9X_FB_ACCESS             0x0001u
#define V9X_CURSOREXCLUDE         0x0008u

#define V9X_VALMODE_YES                0u
#define V9X_VALMODE_NO_WRONG_DRIVER    1u
#define V9X_VALMODE_NO_NOMEM           2u

typedef struct v9x_point_type {
    short x;
    short y;
} V9X_POINT_TYPE;

typedef struct v9x_gdi_info {
    short dpVersion;
    short dpTechnology;
    short dpHorzSize;
    short dpVertSize;
    short dpHorzRes;
    short dpVertRes;
    short dpBitsPixel;
    short dpPlanes;
    short dpNumBrushes;
    short dpNumPens;
    short dpCapsFE;
    short dpNumFonts;
    short dpNumColors;
    short dpDEVICEsize;
    WORD dpCurves;
    WORD dpLines;
    WORD dpPolygonals;
    WORD dpText;
    WORD dpClip;
    WORD dpRaster;
    short dpAspectX;
    short dpAspectY;
    short dpAspectXY;
    short dpStyleLen;
    V9X_POINT_TYPE dpMLoWin;
    V9X_POINT_TYPE dpMLoVpt;
    V9X_POINT_TYPE dpMHiWin;
    V9X_POINT_TYPE dpMHiVpt;
    V9X_POINT_TYPE dpELoWin;
    V9X_POINT_TYPE dpELoVpt;
    V9X_POINT_TYPE dpEHiWin;
    V9X_POINT_TYPE dpEHiVpt;
    V9X_POINT_TYPE dpTwpWin;
    V9X_POINT_TYPE dpTwpVpt;
    short dpLogPixelsX;
    short dpLogPixelsY;
    short dpDCManage;
    WORD dpCaps1;
    short futureUse4;
    short futureUse5;
    short futureUse6;
    short futureUse7;
    WORD dpNumPalReg;
    WORD dpPalReserved;
    WORD dpColorRes;
} V9X_GDI_INFO;

typedef void (FAR PASCAL *V9X_ACCESS_PROC)(void);

/*
 * The same entry point deBeginAccess holds, with its real argument list:
 * BeginAccess(lpDevice, Left, Top, Right, Bottom, Flags), returning Flags.
 *
 * Declared in first-party code at 98DDK\src\display\mini\s3v\ACCESS.ASM:83-95,
 * and used through the PDEVICE the way the accelerated blitter does at
 * S3BLT.ASM:1778 - deCursorExclude is an alias for deBeginAccess
 * (DIBENG.INC:45), and calling it with CURSOREXCLUDE and the blit rectangle is
 * how a software cursor is lifted before the framebuffer is touched.
 *
 * V9X_ACCESS_PROC above stays as it is: that is the type of the field, and the
 * DIB Engine's own zero-argument view of it is what ddi.c assigns. This is the
 * caller's view of the same pointer.
 */
typedef WORD (FAR PASCAL *V9X_CURSOR_EXCLUDE_PROC)(void FAR *device,
                                                   WORD left, WORD top,
                                                   WORD right, WORD bottom,
                                                   WORD flags);

typedef struct v9x_dib_engine {
    WORD deType;
    WORD deWidth;
    WORD deHeight;
    WORD deWidthBytes;
    BYTE dePlanes;
    BYTE deBitsPixel;
    DWORD deReserved1;
    DWORD deDeltaScan;
    LPBYTE delpPDevice;
    DWORD deBitsOffset;
    WORD deBitsSelector;
    WORD deFlags;
    WORD deVersion;
    LPBITMAPINFO deBitmapInfo;
    V9X_ACCESS_PROC deBeginAccess;
    V9X_ACCESS_PROC deEndAccess;
    DWORD deDriverReserved;
} V9X_DIB_ENGINE;

/*
 * DIBENG realized brush, common header only.
 *
 * DIBENG.INC:183-253 declares six per-depth structs - DIB_Brush1/4/8/16/24/32 -
 * which share this 14-byte header and then differ only in the size of a
 * trailing Bits[] array. Rather than transcribe six near-copies, the header is
 * mirrored once and the two accelerated depths are size-asserted below, which
 * is what pins the layout.
 *
 * The solid-fill gate reads exactly two of these fields: BrushFlags &
 * V9X_BRUSH_COLORSOLID, and FgColor. The Mono/Mask/Bits arrays are never
 * touched, so they are deliberately absent - a driver that does not parse a
 * pattern should not carry a declaration inviting it to.
 *
 * BRUSHSIZE is 8 (DIBENG.INC:25), so the trailing arrays are
 * Mono[32] + Mask[32] + Bits[BRUSHSIZE * bpp].
 */
typedef struct v9x_dib_brush {
    BYTE BrushFlags;
    BYTE BrushBpp;
    WORD BrushStyle;
    DWORD FgColor;
    WORD Hatch;
    DWORD BgColor;
} V9X_DIB_BRUSH;

#define V9X_DIB_BRUSH_HEADER_SIZE   14u
#define V9X_DIB_BRUSH8_SIZE        142u   /* 14 + 32 + 32 + 8*8   */
#define V9X_DIB_BRUSH16_SIZE       206u   /* 14 + 32 + 32 + 8*16  */

/*
 * Where a solid brush's physical colour actually lives.
 *
 * NOT FgColor, which DIBENG.INC:189 labels "Physical fg color" and which this
 * driver read first. Measured on the ViRGE at 8 bpp: it holds the *logical*
 * COLORREF. Reading it and handing it to the engine's pattern-colour register
 * paints the low byte - the red channel - as a palette index, so every colour
 * with red 255 came out white
 * (docs\issues\2026-08-26-gdi-fill-brush-colour-not-physical.md).
 *
 * The reference driver takes it from the realized pattern instead:
 * 98DDK\src\display\mini\s3v\S3BLT.ASM PB_SolidPatBlt does
 * `mov ecx,dword ptr ds:[si.dp8BrushBits]` and comments it "ECX = solid
 * foregnd color". That is the right source for a reason worth keeping in mind:
 * DIBENG renders Bits[] at the *destination's* depth, so its first DWORD is
 * already the physical value replicated - four pixels at 8 bpp, two at 16 -
 * which is exactly the form a pattern-colour register wants. FgColor could
 * never have been, at any depth.
 *
 * Only the first DWORD is declared. For a COLORSOLID brush every pixel of the
 * pattern is the same value, and a driver that does not parse patterns should
 * not carry a declaration inviting it to.
 */
typedef struct v9x_dib_brush_solid {
    V9X_DIB_BRUSH header;
    BYTE Mono[32];
    BYTE Mask[32];
    DWORD Bits;
} V9X_DIB_BRUSH_SOLID;

/*
 * DIB_Brushxx.dpxxBrushStyle. The reference driver branches on this before it
 * looks at the flags (PatternBlt in S3BLT.ASM), and only BS_SOLID means a
 * solid colour: BS_HOLLOW draws nothing at all, and the other two carry a
 * pattern this driver does not read. These are the standard Windows BS_ values.
 */
#define V9X_BRUSH_STYLE_SOLID        0u
#define V9X_BRUSH_STYLE_HOLLOW       1u
#define V9X_BRUSH_STYLE_HATCHED      2u
#define V9X_BRUSH_STYLE_PATTERN      3u

/* DIB_Brushxx.dpxxBrushFlags, DIBENG.INC:258-265. */
#define V9X_BRUSH_COLORSOLID      0x01u
#define V9X_BRUSH_MONOSOLID       0x02u
#define V9X_BRUSH_PATTERNMONO     0x04u
#define V9X_BRUSH_MONOVALID       0x08u
#define V9X_BRUSH_MASKVALID       0x10u
#define V9X_BRUSH_PRIVATEDATA     0x20u
#define V9X_BRUSH_BRUSH40         0x40u
#define V9X_BRUSH_DIBENGBRUSH     0x80u

/*
 * Whole-struct size checks for the two depths this driver accelerates. The
 * DDK structs are byte-packed; these exist to catch a compiler that pads, in
 * which case FgColor would be read from the wrong offset and the fill would
 * take a garbage colour.
 */
typedef struct v9x_dib_brush8 {
    V9X_DIB_BRUSH header;
    BYTE Mono[32];
    BYTE Mask[32];
    BYTE Bits[64];
} V9X_DIB_BRUSH8;

typedef struct v9x_dib_brush16 {
    V9X_DIB_BRUSH header;
    BYTE Mono[32];
    BYTE Mask[32];
    BYTE Bits[128];
} V9X_DIB_BRUSH16;

/*
 * GDI's DRAWMODE, from C:\98DDK\inc\win98\inc16\GDIDEFS.INC:1283. Only the head
 * of it is mirrored: this driver reads the two colours a monochrome expansion
 * needs and nothing else.
 *
 * Note which pair, because the struct carries both. These are the physical
 * colours; the logical ones are further down at LbkColor and LTextColor, and it
 * is the physical pair that belongs in an engine register. That distinction is
 * why this struct can be trusted where the realized brush could not
 * (docs\issues\2026-08-26-gdi-fill-brush-colour-not-physical.md): the brush
 * offered one field described as physical and held a logical COLORREF, while
 * this one names both forms and keeps them apart.
 */
typedef struct v9x_drawmode {
    short Rop2;
    short bkMode;
    DWORD bkColor;
    DWORD TextColor;
} V9X_DRAWMODE;

#define V9X_DRAWMODE_HEAD_SIZE      12u

typedef struct v9x_display_validate_mode {
    WORD size;
    WORD bits_per_pixel;
    short width;
    short height;
} V9X_DISPLAY_VALIDATE_MODE;

/* Legacy prefix returned by the master VDD's VDD_GET_DISPLAY_CONFIG API. */
typedef struct v9x_display_info {
    WORD header_size;
    WORD info_flags;
    DWORD device_node;
    char driver_name[16];
    WORD width;
    WORD height;
    WORD dpi;
    BYTE planes;
    BYTE bits_per_pixel;
    WORD maximum_refresh;
} V9X_DISPLAY_INFO;

typedef char v9x_assert_gdi_info_size[
    sizeof(V9X_GDI_INFO) == V9X_GDIINFO_SIZE ? 1 : -1];
typedef char v9x_assert_dib_engine_size[
    sizeof(V9X_DIB_ENGINE) == V9X_DIBENGINE_SIZE ? 1 : -1];
typedef char v9x_assert_validate_mode_size[
    sizeof(V9X_DISPLAY_VALIDATE_MODE) == 8u ? 1 : -1];
typedef char v9x_assert_display_info_size[
    sizeof(V9X_DISPLAY_INFO) == 34u ? 1 : -1];
/*
 * The brush layout, asserted three ways: the shared header's size, and the
 * whole realized brush for each accelerated depth. Offsets follow from the
 * header size holding, because every field in it is declared in DDK order and
 * a pad anywhere inside would show up as a 16-byte header.
 */
typedef char v9x_assert_dib_brush_header_size[
    sizeof(V9X_DIB_BRUSH) == V9X_DIB_BRUSH_HEADER_SIZE ? 1 : -1];
typedef char v9x_assert_dib_brush8_size[
    sizeof(V9X_DIB_BRUSH8) == V9X_DIB_BRUSH8_SIZE ? 1 : -1];
typedef char v9x_assert_dib_brush16_size[
    sizeof(V9X_DIB_BRUSH16) == V9X_DIB_BRUSH16_SIZE ? 1 : -1];
/*
 * The solid-brush accessor lands Bits at offset 78 - 14 + 32 + 32 - which is
 * where DIBENG.INC puts dpxxBrushBits in every one of its six per-depth
 * structs. Asserting the whole size is what pins that offset: a pad anywhere
 * above it would show up here rather than as a wrong fill colour on a screen.
 */
typedef char v9x_assert_drawmode_head_size[
    sizeof(V9X_DRAWMODE) == V9X_DRAWMODE_HEAD_SIZE ? 1 : -1];
typedef char v9x_assert_dib_brush_solid_size[
    sizeof(V9X_DIB_BRUSH_SOLID) == 82u ? 1 : -1];

#endif
