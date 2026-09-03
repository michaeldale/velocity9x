/*
 * Private header for the flat 32-bit DirectDraw HAL (V9XHAL.DLL).
 *
 * The HAL is one DLL built from several translation units - the DirectDraw
 * core, the CPU blit fallbacks, one module per drawing engine, and the ViRGE
 * Direct3D block. This header is what they share and nothing outside
 * src\display32 includes it: the S3 register vocabulary, the engine ops table,
 * and the handful of cross-module functions.
 *
 * Every declaration here was a file-scope static in the single-file ddhal.c.
 * A symbol only appears in this header if a second translation unit genuinely
 * needs it; anything used by one module stays static in that module.
 */
#ifndef VELOCITY9X_DDHAL_INTERNAL_H
#define VELOCITY9X_DDHAL_INTERNAL_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/win9x_ddraw_abi.h"

/* C3 negotiation experiment. Stage 1 publishes GetDriverInfo but declines
 * every GUID; stage 2 changes this to 1 to serve only D3DCallbacks2. */
#define V9X_C3_SERVE_D3D_CALLBACKS2 1

#define V9X_HAL_BASE            0xb0400000ul

/*
 * The 2D engine register map is shared with the 16-bit driver, which needs the
 * same registers for GDI acceleration and cannot depend on this DLL. Constants
 * only; the 3D windows below stay private to Direct3D.
 */
#include "velocity9x/s3_engine_regs.h"

/* Bounded vblank polling so a broken timing source cannot hang a caller. Stays
 * here rather than in the shared header: vblank waits are a DirectDraw service
 * and the 16-bit GDI path has no business polling for one. */
#define V9X_VBLANK_SPIN_LIMIT   0x00200000ul

/* ViRGE S3D setup and triangle register windows (new MMIO). */
#define V9X_VIRGE_3D_Z_BASE           0x0000b4d4ul
#define V9X_VIRGE_3D_DEST_BASE        0x0000b4d8ul
#define V9X_VIRGE_3D_CLIP_L_R         0x0000b4dcul
#define V9X_VIRGE_3D_CLIP_T_B         0x0000b4e0ul
#define V9X_VIRGE_3D_DEST_SRC_STRIDE  0x0000b4e4ul
#define V9X_VIRGE_3D_Z_STRIDE         0x0000b4e8ul
#define V9X_VIRGE_3D_TEX_BASE         0x0000b4ecul
#define V9X_VIRGE_3D_TEX_BORDER       0x0000b4f0ul
#define V9X_VIRGE_3D_FADE_COLOR       0x0000b4f4ul
#define V9X_VIRGE_3D_COMMAND          0x0000b500ul
#define V9X_VIRGE_3D_TBV              0x0000b504ul
#define V9X_VIRGE_3D_TBU              0x0000b508ul
/*
 * The mip-level gradients. D is interpolated across the triangle like U and V
 * are, from a start value in DS and these two per-axis steps - and until
 * 2026-09-03 the driver wrote DS and never these, so the level index drifted
 * across every textured triangle by whatever the registers last held: zero
 * after power-on, another driver's leftovers after a game, and different on
 * every boot. That is what "the Trio3D fetches the wrong mip level" and "the
 * emulated ViRGE returns a colour the texture does not contain" both were.
 * The engine picks one level per triangle, so both steps are written as zero.
 */
#define V9X_VIRGE_3D_DDDX             0x0000b518ul
#define V9X_VIRGE_3D_DVDX             0x0000b51cul
#define V9X_VIRGE_3D_DUDX             0x0000b520ul
#define V9X_VIRGE_3D_DVDY             0x0000b528ul
#define V9X_VIRGE_3D_DDDY             0x0000b524ul
#define V9X_VIRGE_3D_DUDY             0x0000b52cul
#define V9X_VIRGE_3D_DS               0x0000b530ul
#define V9X_VIRGE_3D_VS               0x0000b534ul
#define V9X_VIRGE_3D_US               0x0000b538ul
#define V9X_VIRGE_3D_DGDX_DBDX        0x0000b53cul
#define V9X_VIRGE_3D_DADX_DRDX        0x0000b540ul
#define V9X_VIRGE_3D_DGDY_DBDY        0x0000b544ul
#define V9X_VIRGE_3D_DADY_DRDY        0x0000b548ul
#define V9X_VIRGE_3D_GS_BS            0x0000b54cul
#define V9X_VIRGE_3D_AS_RS            0x0000b550ul
/* The depth gradients and start value, filling what used to be a gap in this
 * table. Offsets from VIRGE1.H:264-266 (TRI_3D_dZdX/dZdY/ZS02, relative to the
 * 0xb504 triangle-data base), cross-checked against 86Box's register decode in
 * build\reference-vid_s3_virge.c:1881-1888. */
#define V9X_VIRGE_3D_DZDX             0x0000b554ul
#define V9X_VIRGE_3D_DZDY             0x0000b558ul
#define V9X_VIRGE_3D_ZS02             0x0000b55cul
#define V9X_VIRGE_3D_DXDY12           0x0000b560ul
#define V9X_VIRGE_3D_XEND12           0x0000b564ul
#define V9X_VIRGE_3D_DXDY01           0x0000b568ul
#define V9X_VIRGE_3D_XEND01           0x0000b56cul
#define V9X_VIRGE_3D_DXDY02           0x0000b570ul
#define V9X_VIRGE_3D_XSTART02         0x0000b574ul
#define V9X_VIRGE_3D_YSTART           0x0000b578ul
#define V9X_VIRGE_3D_Y01_Y12          0x0000b57cul

/*
 * The command word, with the Z-buffer mode field no longer baked in.
 *
 * The historic constant was 0x83000007, which decomposes (VIRGE1.H:126-178) as
 * cmd3D_CMD 0x80000000 | cmdZ_BUF_OFF 0x03000000 | cmdDEST_FMT_ZRGB1555 0x4 |
 * cmdHWCLIP_EN 0x2 | cmdAE_ENABLE 0x1 - that is, Z was explicitly switched off
 * by two bits sitting inside what looked like an opaque base value.
 */
#define V9X_VIRGE_3D_CMD_GOURAUD_16    0x80000007ul

/*
 * Z-buffer mode, bits 25:24. 00 is active; 11 is off. The two MUX modes are
 * never emitted - 86Box treats any non-zero value as off
 * (build\reference-vid_s3_virge.c:4220, use_z = !(cmd_set & CMD_SET_ZB_MODE)),
 * but they are documented hardware modes and silicon need not agree, so this
 * driver stays on the two it understands.
 */
#define V9X_VIRGE_3D_CMD_Z_BUF_OFF     0x03000000ul

/* Bit 23: write the passing depth back to the Z buffer (cmdZ_UP_EN). */
#define V9X_VIRGE_3D_CMD_Z_UPDATE      0x00800000ul

/*
 * Z compare function, bits 22:20. NOT in the D3DCMP order - LESS is 4 and
 * GREATER is 1, and six of the eight differ from (D3DCMP value - 1), so this
 * must be a table rather than arithmetic. Confirmed twice over: VIRGE1.H:161-169
 * and 86Box's Z_CLIP macro, which switches on (cmd_set >> 20) & 7.
 *
 * Note NEVER is zero. A command word that reaches the hardware with this field
 * unset therefore discards every pixel, which is why the mapping's default arm
 * is ALWAYS rather than a fallthrough to zero.
 */
#define V9X_VIRGE_3D_CMD_Z_CMP_NEVER        0x00000000ul
#define V9X_VIRGE_3D_CMD_Z_CMP_GREATER      0x00100000ul
#define V9X_VIRGE_3D_CMD_Z_CMP_EQUAL        0x00200000ul
#define V9X_VIRGE_3D_CMD_Z_CMP_GREATEREQUAL 0x00300000ul
#define V9X_VIRGE_3D_CMD_Z_CMP_LESS         0x00400000ul
#define V9X_VIRGE_3D_CMD_Z_CMP_NOTEQUAL     0x00500000ul
#define V9X_VIRGE_3D_CMD_Z_CMP_LESSEQUAL    0x00600000ul
#define V9X_VIRGE_3D_CMD_Z_CMP_ALWAYS       0x00700000ul

/* The Z-disabled command word, unchanged in value from the single constant
 * this replaced. */
#define V9X_VIRGE_3D_CMD_GOURAUD_16_AE \
    (V9X_VIRGE_3D_CMD_GOURAUD_16 | V9X_VIRGE_3D_CMD_Z_BUF_OFF)

/*
 * The regression-safety property, as a build failure rather than a review
 * note: a triangle drawn with Z disabled must emit exactly the command word
 * it emitted before the Z path existed.
 */
typedef char v9x_assert_cmd_base_unchanged
    [(V9X_VIRGE_3D_CMD_GOURAUD_16_AE == 0x83000007ul) ? 1 : -1];
#define V9X_VIRGE_3D_CMD_ALPHA_SOURCE   0x00040000ul
#define V9X_VIRGE_3D_CMD_ALPHA_ENABLE   0x00080000ul
#define V9X_VIRGE_3D_CMD_TEXTURE_UNLIT  0x10000000ul
#define V9X_VIRGE_3D_CMD_TEXTURE_LIT    0x08000000ul
#define V9X_VIRGE_3D_CMD_TEX_ARGB1555   0x00000040ul
#define V9X_VIRGE_3D_CMD_TEX_ARGB4444   0x00000020ul
#define V9X_VIRGE_3D_CMD_FILTER_NEAREST 0x00004000ul
#define V9X_VIRGE_3D_CMD_FILTER_LINEAR  0x00006000ul
#define V9X_VIRGE_3D_CMD_MIP_NEAREST    0x00000000ul
#define V9X_VIRGE_3D_CMD_MIP_LINEAR     0x00001000ul
#define V9X_VIRGE_3D_CMD_LINEAR_MIP_NEAREST 0x00002000ul
#define V9X_VIRGE_3D_CMD_LINEAR_MIP_LINEAR  0x00003000ul
#define V9X_VIRGE_3D_CMD_TEX_MODULATE   0x00008000ul
#define V9X_VIRGE_3D_CMD_TEXTURE_WRAP   0x04000000ul

/*
 * Bounded 32-bit engine waits. Deliberately NOT in the shared header: these
 * are the flat HAL's own calibration, measured against a flat-model spin, and
 * the 16-bit GDI path keeps its own limits in src\display16\gdi_accel.c.
 * Sharing one number between two loops that do not cost the same would make
 * one of them wrong without saying so.
 */
#define V9X_VIRGE_FIFO_SPIN_LIMIT        0x00200000ul
#define V9X_VIRGE_IDLE_SPIN_LIMIT        0x00400000ul

/*
 * Port I/O and FPU primitives.
 *
 * These are #pragma aux inline definitions, not linked functions, so each
 * translation unit that uses one gets the instruction inline exactly as the
 * single-file build did. That is why they can be static in a shared header.
 */
static unsigned char v9x_inp(unsigned short port);
#pragma aux v9x_inp = "in al,dx" parm [dx] value [al] modify exact [al];

static void v9x_outp(unsigned short port, unsigned char value);
#pragma aux v9x_outp = "out dx,al" parm [dx] [al] modify exact [];

static unsigned short v9x_inpw(unsigned short port);
#pragma aux v9x_inpw = "in ax,dx" parm [dx] value [ax] modify exact [ax];

static void v9x_outpw(unsigned short port, unsigned short value);
#pragma aux v9x_outpw = "out dx,ax" parm [dx] [ax] modify exact [];

static void v9x_fpu_save(void *area);
#pragma aux v9x_fpu_save = "fnsave [eax]" parm [eax] modify exact [];

static void v9x_fpu_restore(void *area);
#pragma aux v9x_fpu_restore = "frstor [eax]" parm [eax] modify exact [];

static LONG v9x_float_to_long(float value);
#pragma aux v9x_float_to_long = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];

/* 32-bit protected-mode FNSAVE area is 108 bytes. */
typedef struct v9x_fpu_area {
    char bytes[112];
} V9X_FPU_AREA;

/* The shared block, published by DriverInit and read by every module. */
extern V9X_DD_SHARED *v9x_hal;

/* Bounded callback trace, in ddhal.c. */
void v9x_trace_push(WORD id, DWORD detail);
void v9x_trace_count(WORD id, DWORD detail);
void v9x_trace_enter(WORD id, DWORD detail);
void v9x_trace_exit(WORD id, DWORD result);
void v9x_trace_flush_fault(DWORD code, DWORD address);

/* ViRGE engine access, in ddhal.c. */
void v9x_mmio_write(DWORD offset, DWORD value);
int v9x_engine_status_validated(void);
int v9x_engine_validate_status(void);
int v9x_wait_fifo(DWORD entries, int wait);
int v9x_wait_idle(int wait);

/* Byte offset of a surface within the framebuffer, or 0xffffffff. */
DWORD v9x_surface_offset(const V9X_DD_SURFACE_LCL *surface);

/* Outcome of an attempt to express a blit on a particular engine. */
#define V9X_BLT_DONE      0   /* the engine has been programmed          */
#define V9X_BLT_BUSY      1   /* engine busy and the caller asked not to wait */
#define V9X_BLT_DECLINED  2   /* this engine cannot express the request  */

/*
 * Runtime engine dispatch.
 *
 * Each engine entry point used to be reached through a
 * `v9x_trio_engine_ready() ? trio : virge` test inlined at the call site.
 * That is a branch per chip per site, and it hid the fact that the tests are
 * not all asking the same question:
 *
 *   ready            - is this engine present and addressable at all
 *   validate_status  - may a command be issued right now? Latching on the
 *                      ViRGE, where the first use of a mode has to confirm
 *                      the status register reads sensibly before any MMIO
 *                      command is written to it
 *   status_validated - has that already happened? A passive test, used by
 *                      the drain before the CPU touches engine-owned memory
 *   can_blt          - the DDGBS_CANBLT answer
 *
 * On the Trio64 the first three collapse onto one flag test and CANBLT is a
 * non-blocking idle poll, because its 8514/A engine has no status latch. On
 * the ViRGE they are four different things, and conflating them is exactly
 * what V9X_DD_ENGINE_STATUS_VALIDATED was separated out to prevent.
 *
 * There is deliberately no `recover` member. Recovery is never dispatched
 * across engines: it is called only from within the bounded wait that expired,
 * and the Trio64 has no recovery at all - forced timeouts there raise
 * idle_timeouts and leave reset_count flat, which is the measured per-target
 * baseline in docs\decisions\2026-08-16-engine-fault-injection.md. A member no
 * caller reads would only invite the assumption that every engine has one.
 */
typedef struct v9x_engine32_ops {
    int (*ready)(void);
    int (*validate_status)(void);
    int (*status_validated)(void);
    int (*can_blt)(void);
    int (*wait_idle)(int wait);
    int (*fill)(V9X_DDHAL_BLTDATA *data, DWORD offset,
                DWORD bytes_per_pixel, int wait);
    int (*copy)(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                DWORD destination_offset, DWORD bytes_per_pixel, int wait);
} V9X_ENGINE32_OPS;

/* One table per engine, each in its own module under engines\. */
extern const V9X_ENGINE32_OPS v9x_engine32_virge;
extern const V9X_ENGINE32_OPS v9x_engine32_trio;

/* Selects one of them from engine.engine_type, or null. In ddhal_core.c. */
const V9X_ENGINE32_OPS *v9x_engine32(void);

/* Consume one armed fault injection, if any. In ddhal_core.c. */
int v9x_fault_injected(void);

/* Shared S3 scanout controls, in engines/vga_scanout.c. */
unsigned char v9x_read_crtc(unsigned char index);
void v9x_write_crtc(unsigned char index, unsigned char value);
int v9x_in_vblank(void);
/* Non-zero when the scanout was programmed; 0 when the offset is not a whole
 * number of doublewords and the registers cannot express it. */
int v9x_set_display_start(DWORD byte_offset);

/* CPU blit fallbacks, in blt_cpu.c. */
void v9x_cpu_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                  DWORD bytes_per_pixel);
void v9x_cpu_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                  DWORD destination_offset, DWORD bytes_per_pixel);

/*
 * The Direct3D block, in d3d\d3d_core.c.
 *
 * v9x_d3d_publish fills the shared block's D3D global data, texture formats
 * and callback tables. DriverInit calls it at the point the single-file build
 * wrote those fields inline; the 16-bit side still clamps them back out for a
 * chipset whose engine_caps lack D3D.
 *
 * v9x_d3d_depth_bytes_per_pixel is the 2D side's one question for the D3D
 * side, and exists so that DDBLT_DEPTHFILL can fill a depth surface without
 * this file knowing how wide a depth pixel is on any particular chip. Zero
 * means the fitted chip has no D3D engine and therefore no depth buffers.
 * Declared here as well as in d3d\d3d_internal.h because ddhal_core.c must
 * not include the D3D internals to ask one number.
 */
void v9x_d3d_publish(V9X_DD_SHARED *shared);
DWORD v9x_d3d_depth_bytes_per_pixel(void);
DWORD __stdcall V9xHalGetDriverInfo(V9X_DDHAL_GETDRIVERINFODATA *data);

#endif /* VELOCITY9X_DDHAL_INTERNAL_H */
