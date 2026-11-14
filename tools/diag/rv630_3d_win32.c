/*
 * V9XR63D.EXE: the ATI Radeon HD 2600 XT (RV630, R6xx, 1002:9588) 3D
 * engine's first draw: the GPU configured through the ring, xf86's default
 * state, and one solid rectangle through the shader core, checked pixel by
 * pixel.
 *
 * EGCP.VXD executes the op lists built here (V9XEGCP's VxD; its read list
 * carries r600_gpu_init's RV630 registers, its write list nothing new).
 * Writes C:\V9XDIAG\V9XR63D.INI. The microcode is read from PFP.BIN (576
 * dwords) and ME.BIN (5376 dwords) in the current directory, linux-firmware's
 * radeon/RV630_pfp.bin and RV630_me.bin, big-endian, as V9XEGCP's RV630
 * branch reads them (docs\decisions\2026-11-13-rv630-cp-ring-test.md).
 *
 * The streams are src\chipsets\ati\r600\r6_3d.c's and the shaders
 * r6_shader.c's, both host-tested against xf86-video-ati and Linux; see
 * include\velocity9x\r6_3d.h for the sources.
 *
 * Call 0 reads GRBM_STATUS for the chip's id: anything but 1002:9588 is
 * refused. Call 1 is read-only: the CP's registers, every register
 * r600_gpu_init's table writes, VGT_PRIMITIVE_TYPE, RAMCFG,
 * CC_GC_SHADER_PIPE_CONFIG and CC_RB_BACKEND_DISABLE. The run goes ahead
 * only if the ME is halted, as POST leaves it. /readonly stops there.
 *
 * Call 2:
 *   1. the scratch window (BAR0 + 241 MiB) saved and cleared; a 64x8
 *      ARGB8888 render target (pitch 64 pixels) filled with a background
 *      value inside 256 guard dwords each side; the ring, the shaders and
 *      the vertices written from the request's data area (VRAM_DATA);
 *   2. the CP stopped, microcode loaded, reset and resumed, exactly as
 *      V9XEGCP's RV630 branch does, then ME_INITIALIZE;
 *   3. r600_ring_test;
 *   4. the draw: r600_gpu_init's configuration as SET_CONFIG_REG,
 *      read-modify-write from call 1's values; xf86's default state; the
 *      solid rectangle; SCRATCH_REG0 written through the ring;
 *   5. the marker and GRBM_STATUS GUI idle awaited; then every config
 *      register call 1 read put back through the ring, and a second
 *      marker awaited (the VxD takes no MMIO write to them);
 *   6. the CP halted, a GRBM reset of the CP blocks, the CP's registers put
 *      back by MMIO as call 1 read them, HDP flushed, the target and its
 *      guards read back, the window restored.
 *
 * The solid draw ran exact on M-NZHWBR (docs\decisions\2026-11-13-rv630-
 * first-3d-draw.md). Three switches replace it with a draw through
 * r6_3d.c's batch state (v9x_r6_3d_build_state, _draw, _flush) and
 * r6_shader.c's interpolated shaders, as V9XEG3D's Phase 3 switches do on
 * Evergreen; none has run on a card yet:
 *   /gouraud  two triangles over the 64x8 target, red rising from 0 at the
 *             left edge to 1 at the right, green 0.5, blue 0.25, alpha 1;
 *             each pixel checked against the value at its centre, +-2;
 *   /depth    a 1D-tiled Z16 buffer at V9X_DEPTH_OFFSET filled with FFFFh,
 *             LESS with writes, in one draw: the whole target red at z
 *             0.5, the left half green at 0.75 (rejected), the right half
 *             blue at 0.25 (passes). Expected red left, blue right; the
 *             depth buffer's eight 8x8 tiles read back and reported;
 *   /modulate a 64x8 ARGB8888 texture (EGCP.VXD's pattern, linear aligned,
 *             pitch 64) sampled at texel centres with point filtering and
 *             multiplied by the interpolated colour (red rising 0 to 1,
 *             green 1, blue 0.5, alpha 1): two interpolants, each pixel
 *             checked, +-2.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/r6_shader.h"
#include "velocity9x/r6_3d.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_R63D_PATH        V9X_DIAG_PATH("V9XR63D.INI")
#define V9X_EGCP_VXD         "\\\\.\\EGCP.VXD"
#define V9X_EGCP_MAGIC       0x50434745ul
#define V9X_EGCP_RUN         1u
#define V9X_EGCP_OP_MAX      512u
#define V9X_EGCP_BLOCK       4096u
#define V9X_EGCP_DATA        8192u
#define V9X_EGCP_HEADER      16u
#define V9X_EGCP_TIMEOUT     0xfffffffful

#define V9X_ST_COMPLETE  0x00000010ul

#define V9X_OP_MMIO_READ    1ul
#define V9X_OP_MMIO_WRITE   2ul
#define V9X_OP_VRAM_FILL    5ul
#define V9X_OP_VRAM_BLOCK   6ul
#define V9X_OP_WAIT_MMIO    8ul
#define V9X_OP_SAVE         9ul
#define V9X_OP_RESTORE      10ul
#define V9X_OP_VRAM_PATTERN 11ul
#define V9X_OP_DELAY        12ul
#define V9X_OP_STREAM       13ul
#define V9X_OP_VRAM_DATA    14ul
/* EGCP.VXD's EC_OP_MMIO_INDEXED: registers 28000h-28FFFh read through
 * MM_INDEX / MM_DATA into the block area. */
#define V9X_OP_MMIO_INDEXED 15ul

/* The RV630's id, microcode sizes (radeon_ucode.h R600_PFP_UCODE_SIZE,
 * R600_PM4_UCODE_SIZE * 3) and VRAM start (r600d.h). */
#define V9X_R6_DEVICE_RV630         0x9588ul
#define V9X_R6_PFP_DWORDS           576ul
#define V9X_R6_ME_DWORDS            5376ul
#define V9X_R6_MC_VM_FB_LOCATION    0x2180ul

/* The CP's registers (r600d.h; the same offsets as Evergreen's). */
#define V9X_R6_HDP_MEM_FLUSH        0x5480ul
#define V9X_R6_SCRATCH_UMSK         0x8540ul
#define V9X_R6_SCRATCH_ADDR         0x8544ul
#define V9X_R6_CP_SEM_WAIT_TIMER    0x85bcul
#define V9X_R6_CP_RB_WPTR_DELAY     0x8704ul
#define V9X_R6_CP_RB_BASE           0xc100ul
#define V9X_R6_CP_RB_CNTL           0xc104ul
#define V9X_R6_CP_RB_RPTR_WR        0xc108ul
#define V9X_R6_CP_RB_RPTR_ADDR      0xc10cul
#define V9X_R6_CP_RB_RPTR_ADDR_HI   0xc110ul
#define V9X_R6_CP_RB_WPTR           0xc114ul
#define V9X_R6_CP_PFP_UCODE_ADDR    0xc150ul
#define V9X_R6_CP_PFP_UCODE_DATA    0xc154ul
#define V9X_R6_CP_ME_RAM_RADDR      0xc158ul
#define V9X_R6_CP_ME_RAM_WADDR      0xc15cul
#define V9X_R6_CP_ME_RAM_DATA       0xc160ul
#define V9X_R6_CP_DEBUG             0xc1fcul

/* Fields and values, as V9XEGCP's RV630 branch writes them: the halt
 * value with ME (28) and PFP (26) set, FFh to run, r600_cp_stop's test of
 * bit 28; CP_RB_CNTL's RB_NO_UPDATE (27) and RB_RPTR_WR_ENA (31); the
 * loading value RB_NO_UPDATE | RB_BLKSZ(15) | RB_BUFSZ(3); CP_DEBUG
 * (1 << 27) | (1 << 28); GRBM_SOFT_RESET's CP bit alone for the load and
 * resume, and CP | PA | SH | VGT | SPI | SX (the VxD's EC_GRBM_RESET_ALLOWED)
 * after the draw. */
#define V9X_R6_CP_HALT              0x14000000ul
#define V9X_R6_CP_ME_HALT           0x10000000ul
#define V9X_R6_CP_RUN               0x000000fful
#define V9X_R6_RB_NO_UPDATE         0x08000000ul
#define V9X_R6_RB_RPTR_WR_ENA       0x80000000ul
#define V9X_R6_RB_CNTL_LOADING      0x08000f03ul
#define V9X_R6_CP_DEBUG_VALUE       0x18000000ul
#define V9X_R6_GRBM_RESET_CP        0x00000001ul
#define V9X_R6_GRBM_RESET_CP_BLOCKS 0x00004721ul
#define V9X_R6_GUI_ACTIVE           0x80000000ul
#define V9X_FB_START_MASK           0x0000fffful
#define V9X_FB_LOW_MASK             0x000000fful
#define V9X_FB_HIGH_SHIFT           8u
#define V9X_FB_LOW_MAX              0x000000eful
#define V9X_ADDR_SHIFT_HIGH         24u

/* The window: BAR0 + 241 MiB, 64 KiB. The ring (16 KiB: RB_BUFSZ 11,
 * RB_BLKSZ 9), the write-back slots (unused: RB_NO_UPDATE, SCRATCH_UMSK 0),
 * the shaders and vertices, and the target inside its guards. */
#define V9X_SCRATCH_GPU_OFFSET      0x0f100000ul
#define V9X_WINDOW_DWORDS           16384ul
#define V9X_RB_CNTL                 ((9ul << 8) | 11ul)
#define V9X_SCRATCH_WB              0x4000ul
#define V9X_RPTR_WB                 0x4100ul
#define V9X_VS_OFFSET               0x5000ul
#define V9X_PS_OFFSET               0x5200ul
#define V9X_VB_OFFSET               0x5400ul
#define V9X_GUARD_OFFSET            0x6c00ul
#define V9X_GUARD_DWORDS            256ul
#define V9X_TARGET_OFFSET           0x7000ul
#define V9X_TARGET_PITCH            64ul
#define V9X_TARGET_HEIGHT           8ul
#define V9X_TARGET_DWORDS           (V9X_TARGET_PITCH * V9X_TARGET_HEIGHT)
#define V9X_READBACK_DWORDS         (V9X_GUARD_DWORDS * 2ul + V9X_TARGET_DWORDS)
#define V9X_GUARD_VALUE             0x5a5a5a5aul
#define V9X_BACKGROUND              0x11223344ul

/* The rectangle, [8, 56) x [2, 6), and its colour. */
#define V9X_RECT_X1                 8ul
#define V9X_RECT_Y1                 2ul
#define V9X_RECT_X2                 56ul
#define V9X_RECT_Y2                 6ul
#define V9X_COLOUR                  0xff3366ccul

/* The draw: the solid rectangle, or one of the three switches'. */
#define V9X_MODE_SOLID              0ul
#define V9X_MODE_GOURAUD            1ul
#define V9X_MODE_DEPTH              2ul
#define V9X_MODE_MODULATE           3ul
static DWORD v9x_mode = V9X_MODE_SOLID;

/*
 * Diagnostic modifiers, after /modulate and /depth's first runs on
 * M-NZHWBR (run a: /modulate's colour interpolant wrong in every channel,
 * /depth drew nothing and wrote no depth). Each separates one cause:
 *   /white    /modulate with every texel FFFFFFFFh: the pixel is the
 *             colour interpolant alone (red ramp, green FFh, blue 80h,
 *             alpha FFh);
 *   /twocol   /modulate's VS and both interpolants, but the Gouraud PS
 *             (R0 exported, no texture, no ALU multiply): the same
 *             expected pixels as /white, with the texture path removed;
 *   /nograd   SPI_PS_IN_CONTROL_0 rewritten as xf86's r600_set_spi writes
 *             it, NUM_INTERP alone (no PERSP_GRADIENT_ENA, which is
 *             Mesa's), after the builder's state; any mode;
 *   /zfunc:N  /depth's ZFUNC as N (hex, 0-7): 7, ALWAYS with writes,
 *             expects green left (drawn after red) and blue right, and
 *             depth BFFFh left (0.75) and 4000h right;
 *   /nodb     /depth's three rectangles with no depth buffer: green left,
 *             blue right, the depth buffer untouched (FFFFh).
 * After run b (/depth LESS drew nothing; ALWAYS drew right but left every
 * depth tile FFFFh), each of these separates the depth block's causes;
 * the expected pixels and tiles come from v9x_depth_model, which plays the
 * three rectangles through the asked function, fill and writes:
 *   /zfill:N  the depth buffer filled with N (hex dword, two Z16 values)
 *             instead of FFFFFFFFh: after ALWAYS, tiles still N mean the
 *             writes went elsewhere, FFFFh means every fragment's Z was
 *             1.0;
 *   /zclear:N DB_DEPTH_CLEAR as N (a float's bits, e.g. 3F800000);
 *   /zmode:N  DB_DEPTH_INFO's ARRAY_MODE as N (0 linear general, 1 linear
 *             aligned, 2 1D tiled), then SURFACE_BASE_UPDATE;
 *   /vte:N    PA_CL_VTE_CNTL as N (100h is xf86's default state's: Z
 *             divided by W), any mode;
 *   /dbsync   a SURFACE_SYNC of DB_ACTION | DB_DEST_BASE over the depth
 *             buffer after the flush (Mesa avoids it on R6xx: "hw bugs");
 *   /scan:N   window bytes N * 2000h to + 2000h read back, every non-zero
 *             dword outside the regions laid out reported (where stray
 *             depth or colour writes landed).
 */
static int v9x_white;
static int v9x_twocol;
static int v9x_nograd;
static int v9x_nodb;
static int v9x_dbsync;
static DWORD v9x_zfunc = V9X_R6_FUNC_LESS;
#define V9X_UNSET                   0xfffffffful
static DWORD v9x_zfill = 0xfffffffful;
static DWORD v9x_zclear = V9X_UNSET;
static DWORD v9x_zmode = V9X_UNSET;
static DWORD v9x_vte = V9X_UNSET;
static DWORD v9x_scan = V9X_UNSET;
/*
 * After runs c and d (/depth: every fragment's Z 1.0; /modulate: the
 * previous run's Gouraud colours from the same vertex address):
 *   /vboff:N  the vertices at window offset N (hex, 256-byte aligned,
 *             inside A000h-F000h, the free part of the window) instead of
 *             5400h: an address no earlier run used;
 *   /vcrange  xf86's ranged cache syncs (ranged_syncs) instead of the
 *             builder's whole-memory TC and VC invalidation;
 *   /zmin:N, /zmax:N  PA_SC_VPORT_ZMIN_0 / ZMAX_0 rewritten as N (a
 *             float's bits) after the builder's 0 and 1.0: /zmax:3F000000
 *             should clamp every Z to 0.5 if the clamp acts; /zmin:0
 *             /zmax:7F7FFFFF leaves no clamp in effect;
 *   /zscale:N, /zoffset:N  PA_CL_VPORT_ZSCALE_0 / ZOFFSET_0 written as N
 *             with VPORT_Z_SCALE_ENA and VPORT_Z_OFFSET_ENA set in
 *             PA_CL_VTE_CNTL (Z = z * scale + offset): /zscale:0
 *             /zoffset:3E800000 makes every Z 0.25 if the transform acts.
 */
/*
 * After runs e and f (/depth: Z 1.0 whatever the viewport transform, the
 * Z clamp or DB_DEPTH_CLEAR):
 *   /zexport:N /depth's PS exports Z itself, PS constant 0's x = N (a
 *             float's bits), with SQ_PGM_EXPORTS_PS EXPORT_Z and
 *             DB_SHADER_CONTROL Z_EXPORT_ENABLE (late Z, no dual export):
 *             with /zfunc:7, tiles of N's Z16 mean the depth block takes
 *             a Z and the rasteriser's is what arrives as 1.0; FFFFh
 *             means the depth block writes 1.0 whatever Z it is given;
 *   /set:R=V  any context or config register R written V (both hex)
 *             after the builder's state, before the draw; up to eight,
 *             applied in order (DB_RENDER_CONTROL 28D0C, DB_RENDER_
 *             OVERRIDE 28D10, DB_SHADER_CONTROL 2880C, PA_CL_CLIP_CNTL
 *             28810, PA_SU_SC_MODE_CNTL 28814, PA_SU_POLY_OFFSET_DB_FMT_
 *             CNTL 28DF8, DB_DEPTH_INFO 28010 ...). A register outside
 *             every SET_* range refuses the run (RING-TOO-LONG).
 */
/*
 * After runs g, h and i (/depth: Z16 FFFFh and Z32F 4994E8C4h, about
 * 1.22e6, in every tile, whatever the Z source):
 *   /mesavte  Mesa's ordinary viewport, not the window-space one:
 *             PA_CL_VTE_CNTL 43Fh (every scale and offset enable, VTX_W0_
 *             FMT, VTX_XY_FMT and VTX_Z_FMT clear: r600_update_vs_state)
 *             with PA_CL_VPORT_XSCALE_0 .. ZOFFSET_0 an identity in x and
 *             y and /zscale:, /zoffset: (default 1.0, 0) in z. Every
 *             earlier Z-scale run kept VTX_Z_FMT set, which may take z
 *             past the viewport whatever the enables;
 *   /zleft    /depth draws only the left-half green rectangle: tiles 0-3
 *             changing and 4-7 keeping /zfill:'s value means the depth
 *             writes come from fragments; all eight changing means they
 *             do not.
 */
static int v9x_mesavte;
static int v9x_zleft;
/*
 * After runs j, k and l (each fragment's Z 4994E8C4h whatever the vertex
 * z, viewport, clamp, clear or PS export; the writes per fragment): one
 * input varied at a time, with /set:28010=10006 to read Z as floats over
 * the whole surface (F32Tile0-7):
 *   /zall:N   every /depth vertex at z N (float bits): run it twice with
 *             different N; the same F32 tiles mean vertex z never reaches
 *             the depth block;
 *   /wv:N     every /depth vertex's w as N (float bits, e.g. 3F000000);
 *   /zright   only the right-half blue rectangle (z 0.25): F32 tiles 4-7
 *             against /zleft's 0-3 show whether the constant depends on
 *             screen position or primitive.
 */
static int v9x_zright;
static DWORD v9x_zall = 0xfffffffful;
static DWORD v9x_wv = 0xfffffffful;
#define V9X_PA_CL_VPORT_XSCALE_0    0x2843cul
#define V9X_VPORT_DWORDS            6ul
#define V9X_VTE_MESA                0x0000043ful
#define V9X_FLOAT_ONE_BITS          0x3f800000ul
/*
 * /mesadb (after run n: no context register holds the constant Z): every
 * context register a depth-tested R6xx draw in Mesa's r600 driver writes
 * differently from this one, written as Mesa writes it after the
 * builder's state (r600_state.c: r600_create_rs_state, r600_emit_db_misc_
 * state, r600_init_atom_start_cs; r600_state_common.c: r600_emit_clip_
 * misc_state), for Z16, LESS with writes, no HTILE, no MSAA, a window-
 * space VS. The rest of the context, DB_DEBUG and DB_WATERMARKS already
 * match. /set: entries apply after these.
 */
struct v9x_reg_value {
    DWORD reg;
    DWORD value;
};
static const struct v9x_reg_value v9x_mesa_db[] = {
    /* FORCE_EOV_CNTDWN_ENABLE | WALK_ALIGN8_PRIM_FITS_ST (R6xx); ours
     * 4010h has xf86's WALK_ORDER_ENABLE instead. */
    { 0x28a4cul, 0x00004100ul },
    /* PIX_CENTER_HALF | QUANT_MODE(X_1_256TH 5), no ROUND_MODE; ours 2Dh
     * (xf86's ROUND_MODE(2)). */
    { 0x28c08ul, 0x00000029ul },
    /* SPI_INTERP_CONTROL_0: FLAT_SHADE_ENA | PNT_SPRITE_ENA |
     * PNT_SPRITE_OVRD_X(2) | _Y(3) | _Z(0) | _W(1); ours 0. */
    { 0x286d4ul, 0x0000086bul },
    /* SPI_THREAD_GROUPING 1 before R700; ours 0 (xf86). */
    { 0x286c8ul, 0x00000001ul },
    /* SQ_VTX_SEMANTIC_CLEAR ~0; never written here. */
    { 0x288e0ul, 0xfffffffful },
    /* VGT_REUSE_OFF 1, VGT_VTX_CNT_EN 0; ours 0, 0. */
    { 0x28ab4ul, 0x00000001ul },
    /* DB_RENDER_CONTROL ZPASS_INCREMENT_DISABLE; ours 60h (xf86's
     * compression disables). */
    { 0x28d0cul, 0x00000800ul },
    /* DB_RENDER_OVERRIDE FORCE_HIZ_ENABLE, FORCE_HIS_ENABLE0 and 1
     * FORCE_DISABLE, no FORCE_SHADER_Z_ORDER; ours 6Ah. */
    { 0x28d10ul, 0x0000002aul },
    /* PA_CL_CLIP_CNTL CLIP_DISABLE (window space) | DX_LINEAR_ATTR_CLIP_
     * ENA; ours 10000h. */
    { 0x28810ul, 0x01010000ul }
};
#define V9X_MESA_DB_COUNT (sizeof(v9x_mesa_db) / sizeof(v9x_mesa_db[0]))
static int v9x_mesadb;
/*
 * After run o (/mesadb changed nothing):
 *   /elem3    every VS export with ELEM_SIZE 3 (word 0 bits 31:30), as
 *             Mesa's r600_bytecode_cf_build sets it on every export;
 *   /zlit:N   /depth's VS overwrites the position's z with N (float bits,
 *             VS constant 0's x) after the fetch, before the export;
 *   /posw     /depth's VS exports z 0.5 (or /zlit:'s N) and w 1.0 from
 *             VS constant 0, x and y from the fetch;
 *   /cfgdump  after the draw, the PA, SC, SX, SPI and DB config registers
 *             read directly: 8A00h-8BFFh, 9000h-905Fh, 9100h-913Fh,
 *             9800h-98FFh (EGCP.VXD read windows, reads only), [Cfg] every
 *             non-zero one, [CfgJunk] any holding 4994E8C4h or a float in
 *             [2^20, 2^21);
 *   /mesaall  Mesa's whole ordinary path for /depth: /mesadb, PA_CL_VTE_
 *             CNTL 43Fh with a viewport scale 32, 4, 1 and offset 32, 4, 0
 *             (x, y, z), clipping on in D3D clip space (PA_CL_CLIP_CNTL
 *             DX_CLIP_SPACE_DEF | DX_LINEAR_ATTR_CLIP_ENA, 01080000h), and
 *             the vertices given in clip space (x / 32 - 1, y / 4 - 1, z,
 *             w 1), which that viewport maps back onto the same pixels.
 */
static int v9x_elem3;
static DWORD v9x_zlit = 0xfffffffful;
static int v9x_posw;
static int v9x_cfgdump;
static int v9x_mesaall;
#define V9X_EXPORT_ELEM_SIZE_3      0xc0000000ul
#define V9X_CF_INST_SHIFT           23u
#define V9X_CF_INST_MASK            0x7ful
#define V9X_CF_INST_EXPORT          0x27ul
#define V9X_CF_INST_EXPORT_DONE     0x28ul
#define V9X_CF_SLOTS                4ul
#define V9X_FLOAT_MINUS_ONE         0xbf800000ul
#define V9X_FLOAT_32_BITS           0x42000000ul
#define V9X_FLOAT_4_BITS            0x40800000ul
#define V9X_CLIP_D3D_LINEAR         0x01080000ul
#define V9X_R6_PA_CL_CLIP_CNTL_PROBE 0x28810ul
static const struct v9x_reg_value v9x_cfg_ranges[] = {
    { 0x8a00ul, 0x8c00ul },     /* PA_CL / PA_SU / PA_SC config */
    { 0x9000ul, 0x9060ul },     /* SX config, SX_DEBUG_1 */
    { 0x9100ul, 0x9140ul },     /* SPI_CONFIG_CNTL .. _1 */
    { 0x9800ul, 0x9900ul }      /* DB config, DB_DEBUG, GB_TILING_CONFIG */
};
#define V9X_CFG_RANGES (sizeof(v9x_cfg_ranges) / sizeof(v9x_cfg_ranges[0]))
#define V9X_CFG_MAX                 232ul
static DWORD v9x_cfg_offsets[V9X_CFG_MAX];
static DWORD v9x_cfg_count;
static DWORD v9x_at_cfg;
static DWORD v9x_zexport = V9X_UNSET;
#define V9X_SET_MAX                 8u
static DWORD v9x_set_regs[V9X_SET_MAX];
static DWORD v9x_set_values[V9X_SET_MAX];
static DWORD v9x_set_count;
#define V9X_SQ_ALU_CONSTANT_PS_0    0x30000ul
#define V9X_SQ_PGM_EXPORTS_PS       0x28854ul
#define V9X_DB_SHADER_CONTROL       0x2880cul
/* EXPORT_Z | EXPORT_COLORS(1) (Mesa S_028854_*); Z_EXPORT_ENABLE with
 * Z_ORDER LATE_Z and no dual export (Mesa sets dual export only without
 * a depth export). */
#define V9X_EXPORTS_Z_AND_COLOUR    0x00000003ul
#define V9X_DB_SHADER_Z_EXPORT      0x00000001ul
static DWORD v9x_vb_offset = V9X_VB_OFFSET;
static int v9x_vcrange;
static DWORD v9x_zmin = V9X_UNSET;
static DWORD v9x_zmax = V9X_UNSET;
static DWORD v9x_zscale = V9X_UNSET;
static DWORD v9x_zoffset = V9X_UNSET;
#define V9X_VB_FREE_FIRST           0xa000ul
#define V9X_VB_FREE_LAST            0xf000ul
#define V9X_PA_SC_VPORT_ZMIN_0      0x282d0ul
#define V9X_PA_SC_VPORT_ZMAX_0      0x282d4ul
#define V9X_PA_CL_VPORT_ZSCALE_0    0x2844cul
#define V9X_PA_CL_VPORT_ZOFFSET_0   0x28450ul
/* PA_CL_VTE_CNTL VPORT_Z_SCALE_ENA | VPORT_Z_OFFSET_ENA; the builder's
 * VTX_XY_FMT | VTX_Z_FMT under them unless /vte: says otherwise. */
#define V9X_VTE_Z_SCALE_OFFSET      0x00000030ul
#define V9X_VTE_BUILDER             0x00000300ul
static DWORD v9x_at_scan_block;
#define V9X_TEX_WHITE               0xfffffffful
#define V9X_ZFUNC_MASK              7ul
#define V9X_SPI_PS_IN_CONTROL_0     0x286ccul
/* Registers the overrides write (r600_reg_auto_r6xx.h, Mesa r600d.h). */
#define V9X_DB_DEPTH_CLEAR          0x2802cul
#define V9X_DB_DEPTH_INFO           0x28010ul
#define V9X_PA_CL_VTE_CNTL          0x28818ul
#define V9X_DB_FORMAT_DEPTH_16      1ul
#define V9X_DB_ARRAY_MODE_SHIFT     15u
#define V9X_DB_ARRAY_MODE_MASK      0xful
#define V9X_SURFACE_BASE_DEPTH      1ul
/* /scan:'s span: 8 KiB, eight of them over the window. */
#define V9X_SCAN_BYTES              0x2000ul
#define V9X_SCAN_DWORDS             (V9X_SCAN_BYTES / 4ul)
#define V9X_SCAN_CHUNKS             8ul
#define V9X_SCAN_REPORTED           16ul
#define V9X_Z16_MASK                0xfffful

/* /modulate's texture: 64x8 ARGB8888, linear aligned at pitch 64 (the
 * RV630's linear-aligned pitch alignment, V9X_R6_TEXTURE_PITCH_ALIGN),
 * filled by EGCP.VXD's VRAM_PATTERN: dword i is seed + i * 9E3779B9h. */
#define V9X_TEX_OFFSET              0x8000ul
#define V9X_TEX_DWORDS              V9X_TARGET_DWORDS
#define V9X_TEX_SEED                0x01020304ul
/* /texseed:N (hex): the pattern's seed, so a texture at the same address
 * differs from the last run's and a stale texture cache would show. */
static DWORD v9x_tex_seed = V9X_TEX_SEED;
#define V9X_PATTERN_STEP            0x9e3779b9ul
/* /depth's Z16 buffer, 64x8 1D tiled: eight 8x8 tiles of 128 bytes, one
 * row of them. Cleared to FFFFh, read back after the target. */
#define V9X_DEPTH_OFFSET            0x9000ul
#define V9X_DEPTH_DWORDS            256ul
#define V9X_DEPTH_TILE_DWORDS       32ul
#define V9X_DEPTH_TILES             8ul
/* The area filled and read back: 2 KiB, the whole 64x8 surface even as
 * DEPTH_32_FLOAT (/set:28010=10006), eight 8x8 tiles of 64 dwords. The
 * first 1 KiB is the Z16 surface the tile report checks. */
#define V9X_DEPTH_AREA_DWORDS       512ul
#define V9X_DEPTH_F32_TILE_DWORDS   64ul
#define V9X_DEPTH_CLEAR             0xfffffffful
#define V9X_DEPTH_TOLERANCE         2ul
#define V9X_TILE_EDGE               8ul

/* Float bit patterns for the vertices. */
#define V9X_FLOAT_0                 0x00000000ul
#define V9X_FLOAT_0_25              0x3e800000ul
#define V9X_FLOAT_0_5               0x3f000000ul
#define V9X_FLOAT_0_75              0x3f400000ul
#define V9X_FLOAT_1                 0x3f800000ul

/* The interpolated draws' vertices: at most three rectangles of two
 * triangles at the Gouraud stride. */
#define V9X_VERTEX_DWORDS_MAX       (18ul * V9X_R6_GOURAUD_STRIDE / 4ul)
/* ARGB8888 pixels the depth draw should leave, and an interpolated
 * channel's tolerance. */
#define V9X_PIXEL_RED               0xffff0000ul
#define V9X_PIXEL_GREEN             0xff00ff00ul
#define V9X_PIXEL_BLUE              0xff0000fful
#define V9X_CHANNEL_TOLERANCE       2ul
#define V9X_UNORM8_MAX              255ul

#define V9X_SCRATCH_SEED            0xcafedeadul
#define V9X_SCRATCH_TEST            0xdeadbeeful
#define V9X_SCRATCH_DRAWN           0x3d3d3d3dul
#define V9X_SCRATCH_STAGE_GPU_INIT  0x3d000001ul
#define V9X_SCRATCH_STAGE_DEFAULT   0x3d000002ul
#define V9X_SCRATCH_PUT_BACK        0x9b9b9b9bul
/* r600_cp_resume's mdelay(15) and mdelay(1), as register reads. */
#define V9X_RESET_DELAY_READS       20000ul
#define V9X_SHORT_DELAY_READS       2000ul

/* The data area: the microcode, then the ring, the VS, the PS and the
 * vertices. The microcode takes 5952 of 8192 dwords; V9X_RING_MAX keeps
 * the rest inside the remaining 2240, and inside the 16 KiB ring. */
#define V9X_DATA_RING               (V9X_R6_PFP_DWORDS + V9X_R6_ME_DWORDS)
#define V9X_RING_MAX                2048ul
#define V9X_MISMATCHES_REPORTED     16ul

struct v9x_egcp_op {
    DWORD op;
    DWORD a;
    DWORD b;
    DWORD c;
};

struct v9x_egcp_request {
    DWORD count;
    struct v9x_egcp_op ops[V9X_EGCP_OP_MAX];
    DWORD data_count;
    DWORD data[V9X_EGCP_DATA];
};

struct v9x_egcp_result {
    DWORD magic;
    DWORD status;
    DWORD id;
    DWORD bar0;
    DWORD bar0_high;
    DWORD bar2;
    DWORD bar2_high;
    DWORD memsize;
    DWORD executed;
    DWORD refused_index;
    DWORD block_used;
    DWORD config;
    DWORD reserved[4];
    DWORD op_results[V9X_EGCP_OP_MAX];
    DWORD block[V9X_EGCP_BLOCK];
};

/* The header must be the VxD's EC_HEADER_DWORDS long. */
typedef char v9x_egcp_header_matches[
    offsetof(struct v9x_egcp_result, op_results) ==
    sizeof(DWORD) * V9X_EGCP_HEADER ? 1 : -1];

/* The CP's registers, put back by MMIO as V9XEGCP puts them back; R6xx
 * has no CP_SEM_INCOMPLETE_TIMER_CNTL. CP_ME_CNTL is first and goes back
 * last. */
static const DWORD v9x_cp_offsets[] = {
    V9X_R6_CP_ME_CNTL, V9X_R6_CP_RB_CNTL, V9X_R6_CP_RB_BASE,
    V9X_R6_CP_RB_RPTR_ADDR, V9X_R6_CP_RB_RPTR_ADDR_HI, V9X_R6_CP_RB_WPTR,
    V9X_R6_CP_DEBUG, V9X_R6_SCRATCH_REG0, V9X_R6_SCRATCH_UMSK,
    V9X_R6_SCRATCH_ADDR, V9X_R6_CP_SEM_WAIT_TIMER, V9X_R6_CP_RB_WPTR_DELAY
};
#define V9X_CP_SAVED (sizeof(v9x_cp_offsets) / sizeof(v9x_cp_offsets[0]))

/* Config registers the ring writes besides the gpu_init table, read in
 * call 1 and put back after it. SQ_DYN_GPR_CNTL_PS_FLUSH_REQ (8D8Ch),
 * which xf86's default state also writes, has no R6xx definition in
 * r600d.h and is not read; WAIT_UNTIL is a command, not state. */
static const DWORD v9x_extra_config_offsets[] = {
    V9X_R6_VGT_PRIMITIVE_TYPE
};
#define V9X_EXTRA_CONFIG \
    (sizeof(v9x_extra_config_offsets) / sizeof(v9x_extra_config_offsets[0]))
#define V9X_CONFIG_SAVED (V9X_R6_RV630_GPU_INIT_COUNT + V9X_EXTRA_CONFIG)

/* Read for the builder and the report, never written. */
static const DWORD v9x_info_offsets[] = {
    V9X_R6_RAMCFG, V9X_R6_CC_GC_SHADER_PIPE_CONFIG,
    V9X_R6_CC_RB_BACKEND_DISABLE, V9X_R6_GRBM_STATUS, V9X_R6_CP_STAT,
    V9X_R6_CP_RB_RPTR, V9X_R6_MC_VM_FB_LOCATION
};
#define V9X_INFO_COUNT (sizeof(v9x_info_offsets) / sizeof(v9x_info_offsets[0]))
#define V9X_INFO_RAMCFG       0u
#define V9X_INFO_PIPE_CONFIG  1u
#define V9X_INFO_FB_LOCATION  6u

static struct v9x_egcp_request v9x_request;
static struct v9x_egcp_result v9x_result;
static DWORD v9x_cp_saved[V9X_CP_SAVED];
static DWORD v9x_config_offsets[V9X_CONFIG_SAVED];
static DWORD v9x_config_saved[V9X_CONFIG_SAVED];
static DWORD v9x_info[V9X_INFO_COUNT];
static BYTE v9x_microcode_bytes[V9X_R6_ME_DWORDS * 4ul];
static v9x_u32 v9x_ring[V9X_RING_MAX];
static struct v9x_r6_pm4 v9x_stream;
static v9x_u32 v9x_vs[V9X_R6_SHADER_DWORDS_MAX];
static v9x_u32 v9x_ps[V9X_R6_SHADER_DWORDS_MAX];
static v9x_u32 v9x_vertices[V9X_VERTEX_DWORDS_MAX];
static DWORD v9x_vs_dwords;
static DWORD v9x_ps_dwords;
static DWORD v9x_vertex_dwords;
static DWORD v9x_vertex_count;

/* Indices of results the report needs, and the ring's submissions. */
static DWORD v9x_at_reset_read;
static DWORD v9x_at_wait_init;
static DWORD v9x_at_wait_test;
static DWORD v9x_at_test_value;
static DWORD v9x_at_wait_draw;
static DWORD v9x_at_wait_idle;
static DWORD v9x_at_drawn_value;
static DWORD v9x_at_rptr;
static DWORD v9x_at_cp_stat;
static DWORD v9x_at_grbm;
static DWORD v9x_at_wait_put_back;
static DWORD v9x_at_put_back_value;
static DWORD v9x_at_after_me_cntl;
static DWORD v9x_at_after_grbm;
static DWORD v9x_at_block;
static DWORD v9x_at_depth_block;
/*
 * /ctxdump (after run m: per-fragment Z 4994E8C4h whatever we vary): once
 * the draw is idle, before the put-back and the GRBM reset, every context
 * register 28000h-28FFCh read through MM_INDEX / MM_DATA, then:
 *   [Ctx]      every non-zero value, "R28xxx";
 *   [CtxDiff]  every register the ring's SET_CONTEXT_REG packets wrote
 *              whose value now differs, "R28xxx=READ WROTE";
 *   [CtxJunk]  every register holding 4994E8C4h, or a float within 2^20
 *              to 2^21 (4994E8C4h is 1219864.5), or 4994h in either half;
 *   [Viewport] the sixteen viewports, "VPn=XSCALE XOFFSET YSCALE YOFFSET
 *              ZSCALE ZOFFSET ZMIN ZMAX" (PA_CL_VPORT_*_n at 2843Ch + 24n,
 *              PA_SC_VPORT_ZMIN/ZMAX_n at 282D0h + 8n).
 * /scan: is dropped with it: the block area holds 4096 dwords.
 */
#define V9X_CTX_FIRST               0x28000ul
#define V9X_CTX_DWORDS              1024ul
#define V9X_CTX_WORDS_BITMAP        (V9X_CTX_DWORDS / 32ul)
#define V9X_VPORTS                  16ul
#define V9X_VPORT_CL_FIRST          0x2843cul
#define V9X_VPORT_CL_STRIDE         24ul
#define V9X_VPORT_SC_FIRST          0x282d0ul
#define V9X_VPORT_SC_STRIDE         8ul
#define V9X_JUNK_VALUE              0x4994e8c4ul
#define V9X_JUNK_HALF               0x4994ul
#define V9X_FLOAT_EXP_MASK          0x7f800000ul
#define V9X_FLOAT_EXP_2_20          0x49800000ul /* 147 << 23 */
static int v9x_ctxdump;
static DWORD v9x_at_ctx_block;
static DWORD v9x_ctx_wrote[V9X_CTX_DWORDS];
static DWORD v9x_ctx_written[V9X_CTX_WORDS_BITMAP];
static DWORD v9x_wptr_init;
static DWORD v9x_wptr_test;
static DWORD v9x_wptr_draw;
static DWORD v9x_wptr_put_back;
static DWORD v9x_tiling_config;

static void v9x_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15ul];
    }
    text[digits] = '\0';
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_R63D_PATH);
}

static void v9x_key(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_hex(text, value, 8);
    v9x_text(section, key, text);
}

/* A register's value under its offset as the key, "R8C00". */
static void v9x_reg_key(const char *section, DWORD offset, DWORD value)
{
    char key[8];

    key[0] = 'R';
    v9x_hex(key + 1, offset, 4);
    v9x_key(section, key, value);
}

static DWORD v9x_op(DWORD op, DWORD a, DWORD b, DWORD c)
{
    DWORD index;

    index = v9x_request.count++;
    v9x_request.ops[index].op = op;
    v9x_request.ops[index].a = a;
    v9x_request.ops[index].b = b;
    v9x_request.ops[index].c = c;
    return index;
}

static DWORD v9x_write(DWORD offset, DWORD value)
{
    return v9x_op(V9X_OP_MMIO_WRITE, offset, value, 0ul);
}

static DWORD v9x_read(DWORD offset)
{
    return v9x_op(V9X_OP_MMIO_READ, offset, 0ul, 0ul);
}

static int v9x_dioc(HANDLE device)
{
    DWORD returned;
    BOOL ok;

    returned = 0ul;
    ok = DeviceIoControl(device, V9X_EGCP_RUN, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_EGCP_MAGIC) {
        v9x_text("Report", "Result", "DIOC-FAILED");
        v9x_key("Report", "Returned", returned);
        return 0;
    }
    v9x_key("Report", "Status", v9x_result.status);
    if ((v9x_result.status & V9X_ST_COMPLETE) == 0ul) {
        v9x_key("Report", "RefusedIndex", v9x_result.refused_index);
        return 0;
    }
    return 1;
}

/* Reads count big-endian dwords from a file into the data area at
 * first. Non-zero when the file is exactly that long. */
static int v9x_load_microcode(const char *path, DWORD first, DWORD count)
{
    HANDLE file;
    BYTE *bytes = v9x_microcode_bytes;
    DWORD read;
    DWORD size;
    DWORD index;

    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    size = GetFileSize(file, 0);
    read = 0ul;
    if (size != count * 4ul || size > sizeof(v9x_microcode_bytes) ||
        !ReadFile(file, bytes, size, &read, 0) || read != size) {
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    for (index = 0ul; index < count; ++index) {
        v9x_request.data[first + index] =
            ((DWORD)bytes[index * 4ul] << 24) |
            ((DWORD)bytes[index * 4ul + 1ul] << 16) |
            ((DWORD)bytes[index * 4ul + 2ul] << 8) |
            (DWORD)bytes[index * 4ul + 3ul];
    }
    return 1;
}

/* Closes a submission at a 16-dword boundary and returns the write
 * pointer, in dwords. */
static DWORD v9x_ring_close(void)
{
    v9x_r6_pm4_pad(&v9x_stream);
    return v9x_stream.count;
}

/* The whole ring as built, one dword a line with its index, to
 * C:\V9XDIAG\V9XR63DR.TXT: a stalled CP's read pointer names the packet it
 * stopped on only against this. */
static void v9x_dump_ring(void)
{
    HANDLE file;
    char line[24];
    DWORD index;
    DWORD written;

    file = CreateFileA(V9X_DIAG_PATH("V9XR63DR.TXT"), GENERIC_WRITE, 0, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    for (index = 0ul; index < v9x_stream.count; ++index) {
        v9x_hex(line, index, 4);
        line[4] = ' ';
        v9x_hex(line + 5, v9x_ring[index], 8);
        line[13] = '\r';
        line[14] = '\n';
        (void)WriteFile(file, line, 15ul, &written, 0);
    }
    CloseHandle(file);
}

/* /mesaall's clip-space coordinate for a vertex at pixel 0, half or twice
 * half (the only ones /depth uses): -1, 0 or 1, which the viewport (scale
 * and offset half) maps back to the pixel. */
static DWORD v9x_clip_coordinate(DWORD pixel_bits, DWORD half_bits)
{
    if (pixel_bits == V9X_FLOAT_0) {
        return V9X_FLOAT_MINUS_ONE;
    }
    return pixel_bits == half_bits ? V9X_FLOAT_0 : V9X_FLOAT_1;
}

/* One vertex of x, y, z, w = 1, then the colour r, g, b, a = 1, as floats'
 * bits; for /modulate also the texel coordinate (x, y), unnormalised. */
static void v9x_vertex(DWORD x, DWORD y, DWORD z, DWORD r, DWORD gr,
                       DWORD b)
{
    v9x_u32 *at = v9x_vertices + v9x_vertex_dwords;
    DWORD dwords = (v9x_mode == V9X_MODE_MODULATE ?
                    V9X_R6_MODULATE_STRIDE : V9X_R6_GOURAUD_STRIDE) / 4ul;

    if (v9x_vertex_dwords + dwords > V9X_VERTEX_DWORDS_MAX) {
        return;
    }
    at[0] = x;
    at[1] = y;
    at[2] = z;
    at[3] = V9X_FLOAT_1;
    if (v9x_mode == V9X_MODE_DEPTH && v9x_zall != V9X_UNSET) {
        at[2] = v9x_zall;
    }
    if (v9x_mode == V9X_MODE_DEPTH && v9x_wv != V9X_UNSET) {
        at[3] = v9x_wv;
    }
    if (v9x_mode == V9X_MODE_DEPTH && v9x_mesaall) {
        at[0] = v9x_clip_coordinate(x, V9X_FLOAT_32_BITS);
        at[1] = v9x_clip_coordinate(y, V9X_FLOAT_4_BITS);
    }
    at[4] = r;
    at[5] = gr;
    at[6] = b;
    at[7] = V9X_FLOAT_1;
    if (v9x_mode == V9X_MODE_MODULATE) {
        at[8] = x;
        at[9] = y;
    }
    v9x_vertex_dwords += dwords;
    ++v9x_vertex_count;
}

/* A full-height rectangle from x0 to x1 (pixels) at depth z as two
 * triangles, red r0 at its left edge and r1 at its right. */
static void v9x_rect(DWORD x0, DWORD x1, DWORD z, DWORD r0, DWORD r1,
                     DWORD gr, DWORD b)
{
    DWORD fx0 = v9x_r6_3d_float_bits(x0);
    DWORD fx1 = v9x_r6_3d_float_bits(x1);
    DWORD fy0 = v9x_r6_3d_float_bits(0ul);
    DWORD fy1 = v9x_r6_3d_float_bits(V9X_TARGET_HEIGHT);

    v9x_vertex(fx0, fy0, z, r0, gr, b);
    v9x_vertex(fx1, fy0, z, r1, gr, b);
    v9x_vertex(fx0, fy1, z, r0, gr, b);
    v9x_vertex(fx1, fy0, z, r1, gr, b);
    v9x_vertex(fx1, fy1, z, r1, gr, b);
    v9x_vertex(fx0, fy1, z, r0, gr, b);
}

/* /elem3: ELEM_SIZE 3 on every export among the VS's four CF slots. */
static void v9x_elem3_patch(void)
{
    DWORD slot;
    DWORD inst;

    for (slot = 0ul; slot < V9X_CF_SLOTS; ++slot) {
        inst = (v9x_vs[slot * 2ul + 1ul] >> V9X_CF_INST_SHIFT) &
               V9X_CF_INST_MASK;
        if (inst == V9X_CF_INST_EXPORT || inst == V9X_CF_INST_EXPORT_DONE) {
            v9x_vs[slot * 2ul] |= V9X_EXPORT_ELEM_SIZE_3;
        }
    }
}

static void v9x_build_draw_mode(void);

/* The mode's shaders and vertices, then /elem3's change to the VS. */
static void v9x_build_draw(void)
{
    v9x_build_draw_mode();
    if (v9x_elem3) {
        v9x_elem3_patch();
    }
}

static void v9x_build_draw_mode(void)
{
    v9x_vertex_dwords = 0ul;
    v9x_vertex_count = 0ul;
    if (v9x_mode == V9X_MODE_SOLID) {
        v9x_vs_dwords = v9x_r6_shader_solid_vs(v9x_vs);
        v9x_ps_dwords = v9x_r6_shader_solid_ps(v9x_ps);
        v9x_r6_3d_rect_vertices(V9X_RECT_X1, V9X_RECT_Y1, V9X_RECT_X2,
                                V9X_RECT_Y2, v9x_vertices);
        v9x_vertex_dwords = V9X_R6_3D_VERTEX_DWORDS;
        return;
    }
    if (v9x_mode == V9X_MODE_MODULATE) {
        v9x_vs_dwords = v9x_r6_shader_modulate_vs(v9x_vs);
        v9x_ps_dwords = v9x_twocol ? v9x_r6_shader_gouraud_ps(v9x_ps)
                                   : v9x_r6_shader_modulate_ps(v9x_ps);
        v9x_rect(0ul, V9X_TARGET_PITCH, V9X_FLOAT_0_5, V9X_FLOAT_0,
                 V9X_FLOAT_1, V9X_FLOAT_1, V9X_FLOAT_0_5);
        return;
    }
    v9x_vs_dwords = v9x_r6_shader_gouraud_vs(v9x_vs);
    v9x_ps_dwords = v9x_r6_shader_gouraud_ps(v9x_ps);
    if (v9x_mode == V9X_MODE_DEPTH && v9x_zexport != V9X_UNSET) {
        v9x_ps_dwords = v9x_r6_shader_gouraud_zexport_ps(v9x_ps);
    }
    if (v9x_mode == V9X_MODE_DEPTH && (v9x_posw || v9x_zlit != V9X_UNSET)) {
        v9x_vs_dwords = v9x_r6_shader_gouraud_vs_zconst(v9x_posw ? 1ul : 0ul,
                                                       v9x_vs);
    }
    if (v9x_mode == V9X_MODE_GOURAUD) {
        v9x_rect(0ul, V9X_TARGET_PITCH, V9X_FLOAT_0_5, V9X_FLOAT_0,
                 V9X_FLOAT_1, V9X_FLOAT_0_5, V9X_FLOAT_0_25);
        return;
    }
    /* /depth: primitives of one draw reach the DB in order, so the three
     * rectangles go in one; /zleft draws the green one alone. */
    if (v9x_zleft) {
        v9x_rect(0ul, V9X_TARGET_PITCH / 2ul, V9X_FLOAT_0_75, V9X_FLOAT_0,
                 V9X_FLOAT_0, V9X_FLOAT_1, V9X_FLOAT_0);
        return;
    }
    if (v9x_zright) {
        v9x_rect(V9X_TARGET_PITCH / 2ul, V9X_TARGET_PITCH, V9X_FLOAT_0_25,
                 V9X_FLOAT_0, V9X_FLOAT_0, V9X_FLOAT_0, V9X_FLOAT_1);
        return;
    }
    v9x_rect(0ul, V9X_TARGET_PITCH, V9X_FLOAT_0_5, V9X_FLOAT_1, V9X_FLOAT_1,
             V9X_FLOAT_0, V9X_FLOAT_0);
    v9x_rect(0ul, V9X_TARGET_PITCH / 2ul, V9X_FLOAT_0_75, V9X_FLOAT_0,
             V9X_FLOAT_0, V9X_FLOAT_1, V9X_FLOAT_0);
    v9x_rect(V9X_TARGET_PITCH / 2ul, V9X_TARGET_PITCH, V9X_FLOAT_0_25,
             V9X_FLOAT_0, V9X_FLOAT_0, V9X_FLOAT_0, V9X_FLOAT_1);
}

/* The switches' draw through r6_3d.c's batch state: the target, the
 * shaders with their GPRs and interpolants, /depth's buffer and test,
 * /modulate's texture with point sampling at the last texel's clamp, the
 * vertices through the RV630's vertex cache; then the triangle list and
 * the flush. */
static const struct v9x_r6_3d_state v9x_zero_state;

static int v9x_ring_interp_draw(DWORD g)
{
    struct v9x_r6_3d_state state;
    v9x_u32 zconst[4];
    v9x_u32 vport[V9X_VPORT_DWORDS];
    DWORD index;

    state = v9x_zero_state;
    state.target_gpu = g + V9X_TARGET_OFFSET;
    state.target_pitch = V9X_TARGET_PITCH;
    state.target_height = V9X_TARGET_HEIGHT;
    state.target_format = V9X_R6_COLOR_8_8_8_8;
    state.write_alpha = 1ul;
    state.scissor_right = V9X_TARGET_PITCH;
    state.scissor_bottom = V9X_TARGET_HEIGHT;
    state.blend_control = v9x_r6_3d_blend(0ul, 0ul, 0ul);
    state.alpha_test_control = v9x_r6_3d_alpha_test(0ul,
                                                    V9X_R6_FUNC_ALWAYS);
    state.perspective = 1ul;
    state.pixel_centre_half = 1ul;
    state.vs_gpu = g + V9X_VS_OFFSET;
    state.ps_gpu = g + V9X_PS_OFFSET;
    state.vs_gprs = V9X_R6_GOURAUD_VS_GPRS;
    state.ps_gprs = V9X_R6_GOURAUD_PS_GPRS;
    state.interpolants = 1ul;
    if (v9x_mode == V9X_MODE_DEPTH && !v9x_nodb) {
        state.has_depth = 1ul;
        state.depth_gpu = g + V9X_DEPTH_OFFSET;
        state.depth_pitch = V9X_TARGET_PITCH;
        state.depth_height = V9X_TARGET_HEIGHT;
        state.depth_control = v9x_r6_3d_depth_control(1ul, 1ul, v9x_zfunc);
    }
    if (v9x_mode == V9X_MODE_MODULATE) {
        state.vs_gprs = V9X_R6_MODULATE_VS_GPRS;
        state.ps_gprs = V9X_R6_MODULATE_PS_GPRS;
        state.interpolants = 2ul;
    }
    if (v9x_mode == V9X_MODE_MODULATE && !v9x_twocol) {
        state.has_texture = 1ul;
        state.texture_gpu = g + V9X_TEX_OFFSET;
        state.texture_width = V9X_TARGET_PITCH;
        state.texture_height = V9X_TARGET_HEIGHT;
        state.texture_pitch = V9X_TARGET_PITCH;
        state.texture_format = V9X_R6_COLOR_8_8_8_8;
        state.sampler_word0 = v9x_r6_3d_sampler(
            0ul, 0ul, V9X_R6_TEX_CLAMP_LAST_TEXEL,
            V9X_R6_TEX_CLAMP_LAST_TEXEL);
    }
    state.vertex_gpu = g + v9x_vb_offset;
    state.vertex_stride = v9x_mode == V9X_MODE_MODULATE ?
                          V9X_R6_MODULATE_STRIDE : V9X_R6_GOURAUD_STRIDE;
    state.vertex_bytes = v9x_vertex_dwords * 4ul;
    state.vertex_cache = 1ul;
    state.ranged_syncs = v9x_vcrange ? 1ul : 0ul;
    if (v9x_mode == V9X_MODE_DEPTH && v9x_zexport != V9X_UNSET) {
        state.ps_gprs = V9X_R6_ZEXPORT_PS_GPRS;
    }
    if (v9x_r6_3d_build_state(&state, &v9x_stream) != V9X_STATUS_OK) {
        return 0;
    }
    /* The diagnostic overrides, after the builder's state (the later
     * write wins). */
    if (v9x_nograd) {
        v9x_r6_pm4_set1(&v9x_stream, V9X_SPI_PS_IN_CONTROL_0,
                        state.interpolants);
    }
    if (v9x_vte != V9X_UNSET) {
        v9x_r6_pm4_set1(&v9x_stream, V9X_PA_CL_VTE_CNTL, v9x_vte);
    }
    if (v9x_mesavte) {
        vport[0] = V9X_FLOAT_ONE_BITS;
        vport[1] = 0ul;
        vport[2] = V9X_FLOAT_ONE_BITS;
        vport[3] = 0ul;
        vport[4] = v9x_zscale != V9X_UNSET ? v9x_zscale : V9X_FLOAT_ONE_BITS;
        vport[5] = v9x_zoffset != V9X_UNSET ? v9x_zoffset : 0ul;
        v9x_r6_pm4_set(&v9x_stream, V9X_PA_CL_VPORT_XSCALE_0, vport,
                       V9X_VPORT_DWORDS);
        v9x_r6_pm4_set1(&v9x_stream, V9X_PA_CL_VTE_CNTL, V9X_VTE_MESA);
    } else if (v9x_zscale != V9X_UNSET || v9x_zoffset != V9X_UNSET) {
        v9x_r6_pm4_set1(&v9x_stream, V9X_PA_CL_VPORT_ZSCALE_0,
                        v9x_zscale != V9X_UNSET ? v9x_zscale : 0x3f800000ul);
        v9x_r6_pm4_set1(&v9x_stream, V9X_PA_CL_VPORT_ZOFFSET_0,
                        v9x_zoffset != V9X_UNSET ? v9x_zoffset : 0ul);
        v9x_r6_pm4_set1(&v9x_stream, V9X_PA_CL_VTE_CNTL,
                        (v9x_vte != V9X_UNSET ? v9x_vte : V9X_VTE_BUILDER) |
                        V9X_VTE_Z_SCALE_OFFSET);
    }
    if (v9x_zmin != V9X_UNSET) {
        v9x_r6_pm4_set1(&v9x_stream, V9X_PA_SC_VPORT_ZMIN_0, v9x_zmin);
    }
    if (v9x_zmax != V9X_UNSET) {
        v9x_r6_pm4_set1(&v9x_stream, V9X_PA_SC_VPORT_ZMAX_0, v9x_zmax);
    }
    if (v9x_mode == V9X_MODE_DEPTH && v9x_zexport != V9X_UNSET) {
        zconst[0] = v9x_zexport;
        zconst[1] = 0ul;
        zconst[2] = 0ul;
        zconst[3] = 0ul;
        v9x_r6_pm4_set(&v9x_stream, V9X_SQ_ALU_CONSTANT_PS_0, zconst, 4ul);
        v9x_r6_pm4_set1(&v9x_stream, V9X_SQ_PGM_EXPORTS_PS,
                        V9X_EXPORTS_Z_AND_COLOUR);
        v9x_r6_pm4_set1(&v9x_stream, V9X_DB_SHADER_CONTROL,
                        V9X_DB_SHADER_Z_EXPORT);
    }
    if (v9x_mesadb || v9x_mesaall) {
        for (index = 0ul; index < V9X_MESA_DB_COUNT; ++index) {
            v9x_r6_pm4_set1(&v9x_stream, v9x_mesa_db[index].reg,
                            v9x_mesa_db[index].value);
        }
    }
    /* /mesaall: the viewport that maps /depth's clip-space vertices onto
     * the target, Mesa's VTE, clipping on in D3D clip space. */
    if (v9x_mesaall && v9x_mode == V9X_MODE_DEPTH) {
        vport[0] = V9X_FLOAT_32_BITS;
        vport[1] = V9X_FLOAT_32_BITS;
        vport[2] = V9X_FLOAT_4_BITS;
        vport[3] = V9X_FLOAT_4_BITS;
        vport[4] = V9X_FLOAT_ONE_BITS;
        vport[5] = 0ul;
        v9x_r6_pm4_set(&v9x_stream, V9X_PA_CL_VPORT_XSCALE_0, vport,
                       V9X_VPORT_DWORDS);
        v9x_r6_pm4_set1(&v9x_stream, V9X_PA_CL_VTE_CNTL, V9X_VTE_MESA);
        v9x_r6_pm4_set1(&v9x_stream, V9X_R6_PA_CL_CLIP_CNTL_PROBE,
                        V9X_CLIP_D3D_LINEAR);
    }
    /* /zlit:, /posw: the Z-constant VS's constant 0, z then w. */
    if (v9x_mode == V9X_MODE_DEPTH && (v9x_posw || v9x_zlit != V9X_UNSET)) {
        zconst[0] = v9x_zlit != V9X_UNSET ? v9x_zlit : V9X_FLOAT_0_5;
        zconst[1] = V9X_FLOAT_ONE_BITS;
        zconst[2] = 0ul;
        zconst[3] = 0ul;
        v9x_r6_pm4_set(&v9x_stream, V9X_R6_VS_CONSTANT_0, zconst, 4ul);
    }
    for (index = 0ul; index < v9x_set_count; ++index) {
        v9x_r6_pm4_set1(&v9x_stream, v9x_set_regs[index],
                        v9x_set_values[index]);
    }
    if (state.has_depth != 0ul && v9x_zclear != V9X_UNSET) {
        v9x_r6_pm4_set1(&v9x_stream, V9X_DB_DEPTH_CLEAR, v9x_zclear);
    }
    if (state.has_depth != 0ul && v9x_zmode != V9X_UNSET) {
        v9x_r6_pm4_set1(&v9x_stream, V9X_DB_DEPTH_INFO,
                        V9X_DB_FORMAT_DEPTH_16 |
                        ((v9x_zmode & V9X_DB_ARRAY_MODE_MASK) <<
                         V9X_DB_ARRAY_MODE_SHIFT));
        v9x_r6_pm4_put(&v9x_stream,
                       v9x_r6_pm4_header(V9X_R6_P3_SURFACE_BASE_UPDATE, 1ul));
        v9x_r6_pm4_put(&v9x_stream, V9X_SURFACE_BASE_DEPTH);
    }
    v9x_r6_3d_draw(&v9x_stream, v9x_vertex_count);
    v9x_r6_3d_flush(&v9x_stream);
    if (state.has_depth != 0ul && v9x_dbsync) {
        v9x_r6_pm4_surface_sync(&v9x_stream,
                                V9X_R6_SYNC_DB_ACTION |
                                V9X_R6_SYNC_DB_DEST_BASE,
                                V9X_DEPTH_AREA_DWORDS * 4ul, state.depth_gpu);
    }
    return 1;
}

/* The four submissions. Returns 0 if the ring overflowed or the draw was
 * refused. */
static int v9x_build_ring(DWORD g)
{
    struct v9x_r6_3d_solid solid;
    v9x_u32 reads[V9X_R6_RV630_GPU_INIT_COUNT];
    DWORD index;

    v9x_r6_pm4_begin(&v9x_stream, v9x_ring, V9X_RING_MAX);

    /* r600_cp_start. */
    v9x_r6_3d_me_initialize(&v9x_stream);
    v9x_wptr_init = v9x_ring_close();

    /* r600_ring_test. */
    v9x_r6_3d_fence(&v9x_stream, V9X_SCRATCH_TEST);
    v9x_wptr_test = v9x_ring_close();

    /* The draw. The table's reads are the first of the config list. */
    for (index = 0ul; index < V9X_R6_RV630_GPU_INIT_COUNT; ++index) {
        reads[index] = v9x_config_saved[index];
    }
    v9x_r6_3d_gpu_init(&v9x_stream, reads, v9x_tiling_config);
    /* Stage markers: a stalled ME's last SCRATCH_REG0 says which part of
     * the draw it got past (the read pointer only says what it fetched). */
    v9x_r6_3d_fence(&v9x_stream, V9X_SCRATCH_STAGE_GPU_INIT);
    v9x_r6_3d_default_state(&v9x_stream, v9x_info[V9X_INFO_PIPE_CONFIG],
                            g + V9X_VS_OFFSET);
    v9x_r6_3d_fence(&v9x_stream, V9X_SCRATCH_STAGE_DEFAULT);
    if (v9x_mode != V9X_MODE_SOLID) {
        if (!v9x_ring_interp_draw(g)) {
            return 0;
        }
    } else {
        solid.target_gpu = g + V9X_TARGET_OFFSET;
        solid.target_pitch = V9X_TARGET_PITCH;
        solid.target_height = V9X_TARGET_HEIGHT;
        solid.vs_gpu = g + V9X_VS_OFFSET;
        solid.ps_gpu = g + V9X_PS_OFFSET;
        solid.vertex_gpu = g + v9x_vb_offset;
        solid.colour = V9X_COLOUR;
        if (v9x_r6_3d_solid(&solid, &v9x_stream) != V9X_STATUS_OK) {
            return 0;
        }
    }
    v9x_r6_3d_fence(&v9x_stream, V9X_SCRATCH_DRAWN);
    v9x_wptr_draw = v9x_ring_close();

    /* Every config register call 1 read, back as it read it. */
    for (index = 0ul; index < V9X_CONFIG_SAVED; ++index) {
        /* The CP's queue thresholds: never written by the draw, and
         * writing them through the ring stalls the ME (run 3). */
        if (v9x_config_offsets[index] == V9X_R6_CP_QUEUE_THRESHOLDS ||
            v9x_config_offsets[index] == V9X_R6_CP_MEQ_THRESHOLDS) {
            continue;
        }
        v9x_r6_pm4_set1(&v9x_stream, v9x_config_offsets[index],
                        v9x_config_saved[index]);
    }
    v9x_r6_3d_fence(&v9x_stream, V9X_SCRATCH_PUT_BACK);
    v9x_wptr_put_back = v9x_ring_close();
    return v9x_stream.overflow == 0ul;
}

static void v9x_build_run(DWORD g)
{
    DWORD index;
    DWORD data;

    /* The data area after the microcode: ring, VS, PS, vertices. */
    data = V9X_DATA_RING;
    for (index = 0ul; index < v9x_stream.count; ++index) {
        v9x_request.data[data + index] = v9x_ring[index];
    }
    data += v9x_stream.count;
    for (index = 0ul; index < v9x_vs_dwords; ++index) {
        v9x_request.data[data + index] = v9x_vs[index];
    }
    data += v9x_vs_dwords;
    for (index = 0ul; index < v9x_ps_dwords; ++index) {
        v9x_request.data[data + index] = v9x_ps[index];
    }
    data += v9x_ps_dwords;
    for (index = 0ul; index < v9x_vertex_dwords; ++index) {
        v9x_request.data[data + index] = v9x_vertices[index];
    }
    v9x_request.data_count = data + v9x_vertex_dwords;

    /* 1. The window, with /modulate's texture pattern and /depth's
     * cleared depth buffer. */
    v9x_request.count = 0ul;
    v9x_op(V9X_OP_SAVE, 0ul, 0ul, 0ul);
    v9x_op(V9X_OP_VRAM_FILL, 0ul, V9X_WINDOW_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_FILL, V9X_GUARD_OFFSET, V9X_READBACK_DWORDS,
           V9X_GUARD_VALUE);
    v9x_op(V9X_OP_VRAM_FILL, V9X_TARGET_OFFSET, V9X_TARGET_DWORDS,
           V9X_BACKGROUND);
    if (v9x_mode == V9X_MODE_MODULATE && v9x_white) {
        v9x_op(V9X_OP_VRAM_FILL, V9X_TEX_OFFSET, V9X_TEX_DWORDS,
               V9X_TEX_WHITE);
    } else if (v9x_mode == V9X_MODE_MODULATE) {
        v9x_op(V9X_OP_VRAM_PATTERN, V9X_TEX_OFFSET, V9X_TEX_DWORDS,
               v9x_tex_seed);
    }
    if (v9x_mode == V9X_MODE_DEPTH) {
        v9x_op(V9X_OP_VRAM_FILL, V9X_DEPTH_OFFSET, V9X_DEPTH_AREA_DWORDS,
               v9x_zfill);
    }
    v9x_op(V9X_OP_VRAM_DATA, 0ul, V9X_DATA_RING, v9x_stream.count);
    v9x_op(V9X_OP_VRAM_DATA, V9X_VS_OFFSET, V9X_DATA_RING + v9x_stream.count,
           v9x_vs_dwords);
    v9x_op(V9X_OP_VRAM_DATA, V9X_PS_OFFSET,
           V9X_DATA_RING + v9x_stream.count + v9x_vs_dwords, v9x_ps_dwords);
    v9x_op(V9X_OP_VRAM_DATA, v9x_vb_offset,
           V9X_DATA_RING + v9x_stream.count + v9x_vs_dwords + v9x_ps_dwords,
           v9x_vertex_dwords);

    /* 2. r600_cp_stop: the ME halted (the PFP with it), SCRATCH_UMSK 0. */
    v9x_write(V9X_R6_CP_ME_CNTL, V9X_R6_CP_HALT);
    v9x_write(V9X_R6_SCRATCH_UMSK, 0ul);

    /* r600_cp_load_microcode: ME first, then PFP. */
    v9x_write(V9X_R6_CP_RB_CNTL, V9X_R6_RB_CNTL_LOADING);
    v9x_write(V9X_R6_GRBM_SOFT_RESET, V9X_R6_GRBM_RESET_CP);
    v9x_read(V9X_R6_GRBM_SOFT_RESET);
    v9x_op(V9X_OP_DELAY, V9X_RESET_DELAY_READS, 0ul, 0ul);
    v9x_write(V9X_R6_GRBM_SOFT_RESET, 0ul);
    v9x_write(V9X_R6_CP_ME_RAM_WADDR, 0ul);
    v9x_op(V9X_OP_STREAM, V9X_R6_CP_ME_RAM_DATA, V9X_R6_PFP_DWORDS,
           V9X_R6_ME_DWORDS);
    v9x_write(V9X_R6_CP_PFP_UCODE_ADDR, 0ul);
    v9x_op(V9X_OP_STREAM, V9X_R6_CP_PFP_UCODE_DATA, 0ul, V9X_R6_PFP_DWORDS);
    v9x_write(V9X_R6_CP_PFP_UCODE_ADDR, 0ul);
    v9x_write(V9X_R6_CP_ME_RAM_WADDR, 0ul);
    v9x_write(V9X_R6_CP_ME_RAM_RADDR, 0ul);

    /* r600_cp_resume, writeback off. */
    v9x_write(V9X_R6_GRBM_SOFT_RESET, V9X_R6_GRBM_RESET_CP);
    v9x_at_reset_read = v9x_read(V9X_R6_GRBM_SOFT_RESET);
    v9x_op(V9X_OP_DELAY, V9X_RESET_DELAY_READS, 0ul, 0ul);
    v9x_write(V9X_R6_GRBM_SOFT_RESET, 0ul);
    v9x_read(V9X_R6_GRBM_SOFT_RESET);
    v9x_write(V9X_R6_CP_RB_CNTL, V9X_RB_CNTL);
    v9x_write(V9X_R6_CP_SEM_WAIT_TIMER, 0ul);
    v9x_write(V9X_R6_CP_RB_WPTR_DELAY, 0ul);
    v9x_write(V9X_R6_CP_RB_CNTL, V9X_RB_CNTL | V9X_R6_RB_RPTR_WR_ENA);
    v9x_write(V9X_R6_CP_RB_RPTR_WR, 0ul);
    v9x_write(V9X_R6_CP_RB_WPTR, 0ul);
    v9x_write(V9X_R6_CP_RB_RPTR_ADDR, (g + V9X_RPTR_WB) & ~3ul);
    v9x_write(V9X_R6_CP_RB_RPTR_ADDR_HI, 0ul);
    v9x_write(V9X_R6_SCRATCH_ADDR, (g + V9X_SCRATCH_WB) >> 8);
    v9x_write(V9X_R6_SCRATCH_UMSK, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_SHORT_DELAY_READS, 0ul, 0ul);
    v9x_write(V9X_R6_CP_RB_CNTL, V9X_RB_CNTL | V9X_R6_RB_NO_UPDATE);
    v9x_write(V9X_R6_CP_RB_BASE, g >> 8);
    v9x_write(V9X_R6_CP_DEBUG, V9X_R6_CP_DEBUG_VALUE);

    /* r600_cp_start: ME_INITIALIZE, then CP_ME_CNTL FFh. */
    v9x_write(V9X_R6_HDP_MEM_FLUSH, 1ul);
    v9x_write(V9X_R6_CP_RB_WPTR, v9x_wptr_init);
    v9x_write(V9X_R6_CP_ME_CNTL, V9X_R6_CP_RUN);
    v9x_at_wait_init = v9x_op(V9X_OP_WAIT_MMIO, V9X_R6_CP_RB_RPTR,
                              0xfffffffful, v9x_wptr_init);

    /* 3. r600_ring_test. */
    v9x_write(V9X_R6_SCRATCH_REG0, V9X_SCRATCH_SEED);
    v9x_write(V9X_R6_CP_RB_WPTR, v9x_wptr_test);
    v9x_at_wait_test = v9x_op(V9X_OP_WAIT_MMIO, V9X_R6_SCRATCH_REG0,
                              0xfffffffful, V9X_SCRATCH_TEST);
    v9x_at_test_value = v9x_read(V9X_R6_SCRATCH_REG0);

    /* 4, 5. The draw, its marker and GUI idle. */
    v9x_write(V9X_R6_CP_RB_WPTR, v9x_wptr_draw);
    v9x_at_wait_draw = v9x_op(V9X_OP_WAIT_MMIO, V9X_R6_SCRATCH_REG0,
                              0xfffffffful, V9X_SCRATCH_DRAWN);
    v9x_at_wait_idle = v9x_op(V9X_OP_WAIT_MMIO, V9X_R6_GRBM_STATUS,
                              V9X_R6_GUI_ACTIVE, 0ul);
    v9x_at_drawn_value = v9x_read(V9X_R6_SCRATCH_REG0);
    v9x_at_rptr = v9x_read(V9X_R6_CP_RB_RPTR);
    v9x_at_cp_stat = v9x_read(V9X_R6_CP_STAT);
    v9x_at_grbm = v9x_read(V9X_R6_GRBM_STATUS);
    if (v9x_ctxdump) {
        v9x_at_ctx_block = v9x_op(V9X_OP_MMIO_INDEXED, V9X_CTX_FIRST,
                                  V9X_CTX_DWORDS, 0ul);
    }
    if (v9x_cfgdump) {
        v9x_cfg_count = 0ul;
        for (index = 0ul; index < V9X_CFG_RANGES; ++index) {
            for (data = v9x_cfg_ranges[index].reg;
                 data < v9x_cfg_ranges[index].value &&
                 v9x_cfg_count < V9X_CFG_MAX; data += 4ul) {
                v9x_cfg_offsets[v9x_cfg_count++] = data;
            }
        }
        v9x_at_cfg = v9x_request.count;
        for (index = 0ul; index < v9x_cfg_count; ++index) {
            v9x_read(v9x_cfg_offsets[index]);
        }
    }

    /* The config registers back through the ring. A hung CP never reaches
     * this; the wait then times out and the reset below still runs. */
    v9x_write(V9X_R6_CP_RB_WPTR, v9x_wptr_put_back);
    v9x_at_wait_put_back = v9x_op(V9X_OP_WAIT_MMIO, V9X_R6_SCRATCH_REG0,
                                  0xfffffffful, V9X_SCRATCH_PUT_BACK);
    v9x_at_put_back_value = v9x_read(V9X_R6_SCRATCH_REG0);

    /* 6. Halt, reset the CP blocks (a hung pipe is left reset), put the
     * CP back. */
    v9x_write(V9X_R6_CP_ME_CNTL, V9X_R6_CP_HALT);
    v9x_op(V9X_OP_DELAY, V9X_SHORT_DELAY_READS, 0ul, 0ul);
    v9x_write(V9X_R6_GRBM_SOFT_RESET, V9X_R6_GRBM_RESET_CP_BLOCKS);
    v9x_read(V9X_R6_GRBM_SOFT_RESET);
    v9x_op(V9X_OP_DELAY, V9X_RESET_DELAY_READS, 0ul, 0ul);
    v9x_write(V9X_R6_GRBM_SOFT_RESET, 0ul);
    v9x_read(V9X_R6_GRBM_SOFT_RESET);
    for (index = 1ul; index < V9X_CP_SAVED; ++index) {
        v9x_write(v9x_cp_offsets[index], v9x_cp_saved[index]);
    }
    v9x_write(V9X_R6_CP_ME_CNTL, v9x_cp_saved[0]);
    v9x_at_after_me_cntl = v9x_read(V9X_R6_CP_ME_CNTL);
    v9x_at_after_grbm = v9x_read(V9X_R6_GRBM_STATUS);
    v9x_write(V9X_R6_HDP_MEM_FLUSH, 1ul);
    v9x_at_block = v9x_op(V9X_OP_VRAM_BLOCK, V9X_GUARD_OFFSET,
                          V9X_READBACK_DWORDS, 0ul);
    if (v9x_mode == V9X_MODE_DEPTH) {
        v9x_at_depth_block = v9x_op(V9X_OP_VRAM_BLOCK, V9X_DEPTH_OFFSET,
                                    V9X_DEPTH_AREA_DWORDS, 0ul);
    }
    if (v9x_scan != V9X_UNSET) {
        v9x_at_scan_block = v9x_op(V9X_OP_VRAM_BLOCK,
                                   v9x_scan * V9X_SCAN_BYTES,
                                   V9X_SCAN_DWORDS, 0ul);
    }
    v9x_op(V9X_OP_RESTORE, 0ul, 0ul, 0ul);

    v9x_key("Run", "Mode", v9x_mode);
    v9x_key("Run", "VsDwords", v9x_vs_dwords);
    v9x_key("Run", "PsDwords", v9x_ps_dwords);
    v9x_key("Run", "Vertices", v9x_vertex_count);
    v9x_key("Run", "RingDwords", v9x_stream.count);
    v9x_key("Run", "WptrInit", v9x_wptr_init);
    v9x_key("Run", "WptrTest", v9x_wptr_test);
    v9x_key("Run", "WptrDraw", v9x_wptr_draw);
    v9x_key("Run", "WptrPutBack", v9x_wptr_put_back);
    v9x_key("Run", "DataDwords", v9x_request.data_count);
    v9x_key("Run", "Ops", v9x_request.count);
}

/* /modulate's texel (x, y): the pattern's dword y * 64 + x. */
static DWORD v9x_pattern(DWORD index)
{
    return v9x_tex_seed + index * V9X_PATTERN_STEP;
}

/* Z16 of the rectangles' depths, 0.5, 0.75 and 0.25 of FFFFh. */
#define V9X_Z16_0_5                 0x8000ul
#define V9X_Z16_0_75                0xbffful
#define V9X_Z16_0_25                0x4000ul

/* DB_DEPTH_CONTROL's comparison of a fragment's Z16 with the stored one:
 * NEVER, LESS, EQUAL, LEQUAL, GREATER, NOTEQUAL, GEQUAL, ALWAYS. */
static int v9x_depth_passes(DWORD fragment, DWORD stored)
{
    switch (v9x_zfunc) {
    case 0ul: return 0;
    case 1ul: return fragment < stored;
    case 2ul: return fragment == stored;
    case 3ul: return fragment <= stored;
    case 4ul: return fragment > stored;
    case 5ul: return fragment != stored;
    case 6ul: return fragment >= stored;
    default:  return 1;
    }
}

/*
 * /depth's model at column x: the buffer starts at /zfill:'s Z16 (the
 * dword's low half; both halves for a fill of two equal values), then the
 * whole width red at 0.5, the left half green at 0.75 and the right half
 * blue at 0.25 go through the test in that order, writing Z where they
 * pass; with /nodb every rectangle passes and nothing is written. Returns
 * the pixel (the background where none passed) and the Z16 left.
 */
static DWORD v9x_depth_model(DWORD x, DWORD *z)
{
    static const DWORD rect_x0[3] = { 0ul, 0ul, V9X_TARGET_PITCH / 2ul };
    static const DWORD rect_x1[3] = {
        V9X_TARGET_PITCH, V9X_TARGET_PITCH / 2ul, V9X_TARGET_PITCH
    };
    static const DWORD rect_z[3] = { V9X_Z16_0_5, V9X_Z16_0_75,
                                     V9X_Z16_0_25 };
    static const DWORD rect_colour[3] = { V9X_PIXEL_RED, V9X_PIXEL_GREEN,
                                          V9X_PIXEL_BLUE };
    DWORD colour = V9X_BACKGROUND;
    DWORD stored = v9x_zfill & V9X_Z16_MASK;
    DWORD index;

    for (index = 0ul; index < 3ul; ++index) {
        if (x < rect_x0[index] || x >= rect_x1[index]) {
            continue;
        }
        /* /zleft draws only the green rectangle. */
        if ((v9x_zleft && index != 1ul) || (v9x_zright && index != 2ul)) {
            continue;
        }
        if (v9x_nodb) {
            colour = rect_colour[index];
            continue;
        }
        if (v9x_depth_passes(rect_z[index], stored)) {
            colour = rect_colour[index];
            stored = rect_z[index];
        }
    }
    *z = stored;
    return colour;
}

/*
 * The pixel a switch's draw should leave at (x, y), ARGB8888 (B, G, R, A
 * from the low byte, as COLOR_8_8_8_8 with COMP_SWAP 1 stores the
 * shader's R, G, B, A). Pixel centres are at x + 0.5 (PIX_CENTER), so
 * red's ramp there is (2x + 1) / 128, rounded. /gouraud: green 0.5 and
 * blue 0.25 of 255, 128 and 64 within the tolerance. /modulate: the texel
 * with red scaled by the ramp, green by 1, blue by 0.5, alpha by 1.
 * /depth: red left of x 32, blue right.
 */
static DWORD v9x_expected_pixel(DWORD x, DWORD y)
{
    DWORD texel;
    DWORD red;

    if (v9x_mode == V9X_MODE_DEPTH) {
        return v9x_depth_model(x, &red);
    }
    if (v9x_mode == V9X_MODE_MODULATE && (v9x_white || v9x_twocol)) {
        /* The colour interpolant itself: (ramp, 1, 0.5, 1). */
        red = ((2ul * x + 1ul) * V9X_UNORM8_MAX + V9X_TARGET_PITCH) /
              (2ul * V9X_TARGET_PITCH);
        return 0xff00ff80ul | (red << 16);
    }
    if (v9x_mode == V9X_MODE_MODULATE) {
        texel = v9x_pattern(y * V9X_TARGET_PITCH + x);
        red = (((texel >> 16) & V9X_UNORM8_MAX) * (2ul * x + 1ul) +
               V9X_TARGET_PITCH) / (2ul * V9X_TARGET_PITCH);
        return (texel & 0xff00ff00ul) | (red << 16) |
               ((texel & V9X_UNORM8_MAX) / 2ul);
    }
    red = ((2ul * x + 1ul) * V9X_UNORM8_MAX + V9X_TARGET_PITCH) /
          (2ul * V9X_TARGET_PITCH);
    return 0xff000000ul | (red << 16) | (128ul << 8) | 64ul;
}

/* What readback dword index should hold: the guards, the background, or
 * the colour inside the rectangle; or the switch's pixel. */
static DWORD v9x_expected(DWORD index)
{
    DWORD x;
    DWORD y;

    if (index < V9X_GUARD_DWORDS ||
        index >= V9X_GUARD_DWORDS + V9X_TARGET_DWORDS) {
        return V9X_GUARD_VALUE;
    }
    x = (index - V9X_GUARD_DWORDS) % V9X_TARGET_PITCH;
    y = (index - V9X_GUARD_DWORDS) / V9X_TARGET_PITCH;
    if (v9x_mode != V9X_MODE_SOLID) {
        return v9x_expected_pixel(x, y);
    }
    if (x >= V9X_RECT_X1 && x < V9X_RECT_X2 &&
        y >= V9X_RECT_Y1 && y < V9X_RECT_Y2) {
        return V9X_COLOUR;
    }
    return V9X_BACKGROUND;
}

/* Every byte within V9X_CHANNEL_TOLERANCE. */
static int v9x_pixel_close(DWORD got, DWORD want)
{
    DWORD shift;
    DWORD a;
    DWORD b;

    for (shift = 0ul; shift < 32ul; shift += 8ul) {
        a = (got >> shift) & V9X_UNORM8_MAX;
        b = (want >> shift) & V9X_UNORM8_MAX;
        if ((a > b ? a - b : b - a) > V9X_CHANNEL_TOLERANCE) {
            return 0;
        }
    }
    return 1;
}

/* The guards and the solid draw are exact; an interpolated pixel is
 * close. */
static int v9x_matches(DWORD got, DWORD want, DWORD index)
{
    if (v9x_mode == V9X_MODE_SOLID || index < V9X_GUARD_DWORDS ||
        index >= V9X_GUARD_DWORDS + V9X_TARGET_DWORDS) {
        return got == want;
    }
    return v9x_pixel_close(got, want);
}

/* One key per row of the target: /gouraud's red byte in hex for each
 * pixel; /depth's colour as a letter (R, G, B, "?" otherwise); /modulate's
 * "." for a pixel within tolerance, "X" for one outside. */
static void v9x_colour_map(const DWORD *target)
{
    char key[8];
    char line[V9X_TARGET_PITCH * 3ul + 1ul];
    DWORD row;
    DWORD column;
    DWORD value;
    char *at;

    for (row = 0ul; row < V9X_TARGET_HEIGHT; ++row) {
        at = line;
        for (column = 0ul; column < V9X_TARGET_PITCH; ++column) {
            value = target[row * V9X_TARGET_PITCH + column];
            if (v9x_mode == V9X_MODE_GOURAUD) {
                v9x_hex(at, (value >> 16) & V9X_UNORM8_MAX, 2);
                at[2] = ' ';
                at += 3;
            } else if (v9x_mode == V9X_MODE_MODULATE) {
                *at++ = v9x_pixel_close(value,
                                        v9x_expected_pixel(column, row)) ?
                        '.' : 'X';
            } else if (v9x_pixel_close(value, V9X_PIXEL_RED)) {
                *at++ = 'R';
            } else if (v9x_pixel_close(value, V9X_PIXEL_GREEN)) {
                *at++ = 'G';
            } else if (v9x_pixel_close(value, V9X_PIXEL_BLUE)) {
                *at++ = 'B';
            } else if (value == V9X_BACKGROUND) {
                *at++ = '_';
            } else {
                *at++ = '?';
            }
        }
        if (v9x_mode == V9X_MODE_GOURAUD) {
            --at;
        }
        *at = '\0';
        lstrcpyA(key, "Row");
        v9x_hex(key + 3, row, 1);
        v9x_text("Map", key, line);
    }
}

/* The depth buffer's eight 8x8 tiles: each lies wholly in one half, so
 * each should hold one value, v9x_depth_model's Z16 for its columns (LESS
 * over FFFFh: 8000h left, 4000h right; ALWAYS: BFFFh left; /nodb: the
 * fill), within a 16-bit unorm's rounding. Non-zero when all do. */
static int v9x_depth_report(const DWORD *depth)
{
    char key[12];
    DWORD tile;
    DWORD index;
    DWORD first;
    DWORD expected;
    DWORD z;
    DWORD bad = 0ul;

    for (tile = 0ul; tile < V9X_DEPTH_TILES; ++tile) {
        first = depth[tile * V9X_DEPTH_TILE_DWORDS];
        lstrcpyA(key, "DepthTile");
        v9x_hex(key + 9, tile, 1);
        v9x_key("Depth", key, first);
        (void)v9x_depth_model(tile * V9X_TILE_EDGE, &expected);
        for (index = 0ul; index < V9X_DEPTH_TILE_DWORDS; ++index) {
            if (depth[tile * V9X_DEPTH_TILE_DWORDS + index] != first) {
                ++bad;
                break;
            }
        }
        z = first & 0xfffful;
        if ((first >> 16) != z || z + V9X_DEPTH_TOLERANCE < expected ||
            z > expected + V9X_DEPTH_TOLERANCE) {
            ++bad;
        }
    }
    v9x_key("Depth", "Bad", bad);
    /* The same 2 KiB read as eight 32-bit tiles (DEPTH_32_FLOAT): each
     * tile's first dword, "F32TileN", and whether the tile is uniform. */
    for (tile = 0ul; tile < V9X_DEPTH_TILES; ++tile) {
        first = depth[tile * V9X_DEPTH_F32_TILE_DWORDS];
        lstrcpyA(key, "F32Tile");
        v9x_hex(key + 7, tile, 1);
        v9x_key("Depth", key, first);
        for (index = 0ul; index < V9X_DEPTH_F32_TILE_DWORDS; ++index) {
            if (depth[tile * V9X_DEPTH_F32_TILE_DWORDS + index] != first) {
                lstrcpyA(key, "F32Mixed");
                v9x_hex(key + 8, tile, 1);
                v9x_key("Depth", key,
                        depth[tile * V9X_DEPTH_F32_TILE_DWORDS + index]);
                break;
            }
        }
    }
    return bad == 0ul;
}

/* Whether window byte offset lies in a region this run lays out: the
 * ring, the shaders, the vertices, the target with its guards, and the
 * mode's texture or depth buffer. Everything else was zero-filled. */
static int v9x_laid_out(DWORD offset)
{
    if (offset < v9x_stream.count * 4ul) {
        return 1;
    }
    if ((offset >= V9X_VS_OFFSET &&
         offset < V9X_VS_OFFSET + v9x_vs_dwords * 4ul) ||
        (offset >= V9X_PS_OFFSET &&
         offset < V9X_PS_OFFSET + v9x_ps_dwords * 4ul) ||
        (offset >= v9x_vb_offset &&
         offset < v9x_vb_offset + v9x_vertex_dwords * 4ul) ||
        (offset >= V9X_GUARD_OFFSET &&
         offset < V9X_GUARD_OFFSET + V9X_READBACK_DWORDS * 4ul)) {
        return 1;
    }
    if (v9x_mode == V9X_MODE_MODULATE && offset >= V9X_TEX_OFFSET &&
        offset < V9X_TEX_OFFSET + V9X_TEX_DWORDS * 4ul) {
        return 1;
    }
    return v9x_mode == V9X_MODE_DEPTH && offset >= V9X_DEPTH_OFFSET &&
           offset < V9X_DEPTH_OFFSET + V9X_DEPTH_AREA_DWORDS * 4ul;
}

/* /scan:'s span: the count of non-zero dwords outside the laid-out
 * regions, and the first V9X_SCAN_REPORTED as "OOOO VVVVVVVV" (window
 * byte offset, value). */
static void v9x_scan_report(const DWORD *span)
{
    char key[8];
    char text[16];
    DWORD index;
    DWORD offset;
    DWORD found = 0ul;

    for (index = 0ul; index < V9X_SCAN_DWORDS; ++index) {
        offset = v9x_scan * V9X_SCAN_BYTES + index * 4ul;
        if (span[index] == 0ul || v9x_laid_out(offset)) {
            continue;
        }
        if (found < V9X_SCAN_REPORTED) {
            lstrcpyA(key, "Stray");
            v9x_hex(key + 5, found, 1);
            v9x_hex(text, offset, 4);
            text[4] = ' ';
            v9x_hex(text + 5, span[index], 8);
            v9x_text("Scan", key, text);
        }
        ++found;
    }
    v9x_key("Scan", "Base", v9x_scan * V9X_SCAN_BYTES);
    v9x_key("Scan", "Strays", found);
}

/* The value the ring's SET_CONTEXT_REG packets left in each context
 * register, up to the draw's end: v9x_ctx_wrote, and v9x_ctx_written's
 * bit for each register written at all. */
static void v9x_ctx_replay(void)
{
    DWORD at = 0ul;
    DWORD header;
    DWORD body;
    DWORD first;
    DWORD index;
    DWORD reg;

    for (index = 0ul; index < V9X_CTX_WORDS_BITMAP; ++index) {
        v9x_ctx_written[index] = 0ul;
    }
    while (at < v9x_wptr_draw) {
        header = v9x_ring[at];
        if (header == V9X_R6_PACKET2 || (header >> 30) != 3ul) {
            ++at;
            continue;
        }
        body = ((header >> 16) & V9X_R6_PACKET3_BODY_MAX) + 1ul;
        if (((header >> 8) & 0xfful) == V9X_R6_P3_SET_CONTEXT_REG &&
            at + body < v9x_stream.count) {
            first = v9x_ring[at + 1ul];
            for (index = 0ul; index + 1ul < body; ++index) {
                reg = first + index;
                if (reg < V9X_CTX_DWORDS) {
                    v9x_ctx_wrote[reg] = v9x_ring[at + 2ul + index];
                    v9x_ctx_written[reg / 32ul] |= 1ul << (reg % 32ul);
                }
            }
        }
        at += 1ul + body;
    }
}

/* Whether a value is, or looks like, the constant Z: 4994E8C4h, a float
 * in [2^20, 2^21), or 4994h in either half. */
static int v9x_junk_like(DWORD value)
{
    return value == V9X_JUNK_VALUE ||
           (value & V9X_FLOAT_EXP_MASK) == V9X_FLOAT_EXP_2_20 ||
           (value >> 16) == V9X_JUNK_HALF ||
           (value & 0xfffful) == V9X_JUNK_HALF;
}

/* /cfgdump's report: [Cfg] every non-zero register, "Rxxxx", and
 * [CfgJunk] those that look like the constant Z. */
static void v9x_cfg_report(const DWORD *values)
{
    char key[8];
    DWORD index;
    DWORD junk = 0ul;

    for (index = 0ul; index < v9x_cfg_count; ++index) {
        key[0] = 'R';
        v9x_hex(key + 1, v9x_cfg_offsets[index], 4);
        if (values[index] != 0ul) {
            v9x_key("Cfg", key, values[index]);
        }
        if (v9x_junk_like(values[index])) {
            v9x_key("CfgJunk", key, values[index]);
            ++junk;
        }
    }
    v9x_key("CfgJunk", "Count", junk);
}

/* "R28xxx" for context register index. */
static void v9x_ctx_key(char *key, DWORD index)
{
    key[0] = 'R';
    v9x_hex(key + 1, V9X_CTX_FIRST + index * 4ul, 5);
}

/* /ctxdump's report: [Ctx], [CtxDiff], [CtxJunk], [Viewport]. */
static void v9x_ctx_report(const DWORD *ctx)
{
    char key[12];
    char text[96];
    DWORD index;
    DWORD value;
    DWORD port;
    DWORD field;
    DWORD diffs = 0ul;
    DWORD junk = 0ul;

    v9x_ctx_replay();
    for (index = 0ul; index < V9X_CTX_DWORDS; ++index) {
        value = ctx[index];
        v9x_ctx_key(key, index);
        if (value != 0ul) {
            v9x_key("Ctx", key, value);
        }
        if ((v9x_ctx_written[index / 32ul] & (1ul << (index % 32ul))) !=
                0ul &&
            v9x_ctx_wrote[index] != value) {
            v9x_hex(text, value, 8);
            text[8] = ' ';
            v9x_hex(text + 9, v9x_ctx_wrote[index], 8);
            v9x_text("CtxDiff", key, text);
            ++diffs;
        }
        if (value == V9X_JUNK_VALUE ||
            (value & V9X_FLOAT_EXP_MASK) == V9X_FLOAT_EXP_2_20 ||
            (value >> 16) == V9X_JUNK_HALF ||
            (value & 0xfffful) == V9X_JUNK_HALF) {
            v9x_key("CtxJunk", key, value);
            ++junk;
        }
    }
    v9x_key("CtxDiff", "Count", diffs);
    v9x_key("CtxJunk", "Count", junk);
    for (port = 0ul; port < V9X_VPORTS; ++port) {
        for (field = 0ul; field < 6ul; ++field) {
            v9x_hex(text + field * 9ul,
                    ctx[(V9X_VPORT_CL_FIRST - V9X_CTX_FIRST +
                         port * V9X_VPORT_CL_STRIDE) / 4ul + field], 8);
            text[field * 9ul + 8ul] = ' ';
        }
        for (field = 0ul; field < 2ul; ++field) {
            v9x_hex(text + 54ul + field * 9ul,
                    ctx[(V9X_VPORT_SC_FIRST - V9X_CTX_FIRST +
                         port * V9X_VPORT_SC_STRIDE) / 4ul + field], 8);
            text[54ul + field * 9ul + 8ul] = ' ';
        }
        text[71] = '\0';
        lstrcpyA(key, "VP");
        v9x_hex(key + 2, port, 1);
        v9x_text("Viewport", key, text);
    }
}

/* "IIII XX,YY GOT WANT": the readback index, the pixel (FF,FF in a
 * guard), what was read and what was expected. */
static void v9x_mismatch(DWORD number, DWORD index, DWORD got, DWORD want)
{
    char key[12];
    char text[32];
    DWORD x = 0xfful;
    DWORD y = 0xfful;

    if (index >= V9X_GUARD_DWORDS &&
        index < V9X_GUARD_DWORDS + V9X_TARGET_DWORDS) {
        x = (index - V9X_GUARD_DWORDS) % V9X_TARGET_PITCH;
        y = (index - V9X_GUARD_DWORDS) / V9X_TARGET_PITCH;
    }
    lstrcpyA(key, "Mismatch");
    v9x_hex(key + 8, number, 1);
    v9x_hex(text, index, 4);
    text[4] = ' ';
    v9x_hex(text + 5, x, 2);
    text[7] = ',';
    v9x_hex(text + 8, y, 2);
    text[10] = ' ';
    v9x_hex(text + 11, got, 8);
    text[19] = ' ';
    v9x_hex(text + 20, want, 8);
    v9x_text("Pixels", key, text);
}

/* Returns non-zero when every pixel and guard dword is as expected. */
static int v9x_verify(const DWORD *got)
{
    DWORD index;
    DWORD want;
    DWORD mismatches = 0ul;
    DWORD guard_mismatches = 0ul;
    DWORD coloured = 0ul;
    DWORD background = 0ul;

    for (index = 0ul; index < V9X_READBACK_DWORDS; ++index) {
        want = v9x_expected(index);
        if (v9x_matches(got[index], want, index)) {
            if (want == V9X_GUARD_VALUE) {
                continue;
            }
            if (v9x_mode != V9X_MODE_SOLID || want == V9X_COLOUR) {
                ++coloured;
            } else if (want == V9X_BACKGROUND) {
                ++background;
            }
            continue;
        }
        if (want == V9X_GUARD_VALUE) {
            ++guard_mismatches;
        }
        if (mismatches < V9X_MISMATCHES_REPORTED) {
            v9x_mismatch(mismatches, index, got[index], want);
        }
        ++mismatches;
    }
    v9x_key("Pixels", "Mismatches", mismatches);
    v9x_key("Pixels", "GuardMismatches", guard_mismatches);
    if (v9x_mode != V9X_MODE_SOLID) {
        /* The switch's draw covers the target: sample pixels at its
         * corners and either side of the middle, and a map of each row. */
        v9x_key("Pixels", "DrawnCorrect", coloured);
        v9x_key("Pixels", "Pixel0", got[V9X_GUARD_DWORDS]);
        v9x_key("Pixels", "Pixel31", got[V9X_GUARD_DWORDS + 31ul]);
        v9x_key("Pixels", "Pixel32", got[V9X_GUARD_DWORDS + 32ul]);
        v9x_key("Pixels", "Pixel63", got[V9X_GUARD_DWORDS + 63ul]);
        v9x_key("Pixels", "PixelLast",
                got[V9X_GUARD_DWORDS + V9X_TARGET_DWORDS - 1ul]);
        v9x_key("Pixels", "Expected0", v9x_expected(V9X_GUARD_DWORDS));
        v9x_key("Pixels", "Expected63",
                v9x_expected(V9X_GUARD_DWORDS + 63ul));
        v9x_colour_map(got + V9X_GUARD_DWORDS);
        return mismatches == 0ul;
    }
    v9x_key("Pixels", "ColouredCorrect", coloured);
    v9x_key("Pixels", "BackgroundCorrect", background);
    /* Three sample pixels whatever happened: the rectangle's first, the
     * target's first, and the rectangle's last. */
    v9x_key("Pixels", "Inside",
            got[V9X_GUARD_DWORDS + V9X_RECT_Y1 * V9X_TARGET_PITCH +
                V9X_RECT_X1]);
    v9x_key("Pixels", "Corner", got[V9X_GUARD_DWORDS]);
    v9x_key("Pixels", "InsideLast",
            got[V9X_GUARD_DWORDS + (V9X_RECT_Y2 - 1ul) * V9X_TARGET_PITCH +
                V9X_RECT_X2 - 1ul]);
    return mismatches == 0ul;
}

static int v9x_switch_given(const char *switch_text)
{
    const char *line;
    int length;

    line = GetCommandLineA();
    length = lstrlenA(switch_text);
    while (*line != '\0') {
        if (CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE, line,
                           length, switch_text, length) == CSTR_EQUAL) {
            return 1;
        }
        ++line;
    }
    return 0;
}

/* The hex value after a switch such as "/zfunc:", or fallback when the
 * switch is absent or has no digits. */
static DWORD v9x_switch_hex(const char *switch_text, DWORD fallback)
{
    const char *line;
    int length;
    DWORD value = 0ul;
    int digits = 0;
    char c;

    line = GetCommandLineA();
    length = lstrlenA(switch_text);
    while (*line != '\0') {
        if (CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE, line,
                           length, switch_text, length) == CSTR_EQUAL) {
            break;
        }
        ++line;
    }
    if (*line == '\0') {
        return fallback;
    }
    line += length;
    for (;;) {
        c = *line++;
        if (c >= '0' && c <= '9') {
            value = (value << 4) | (DWORD)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value = (value << 4) | (DWORD)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value = (value << 4) | (DWORD)(c - 'A' + 10);
        } else {
            break;
        }
        ++digits;
    }
    return digits != 0 ? value : fallback;
}

/* Every "/set:R=V" on the command line, R and V hex, up to V9X_SET_MAX,
 * into v9x_set_regs and v9x_set_values; a malformed one is skipped. */
static void v9x_parse_sets(void)
{
    static const char tag[] = "/set:";
    const char *line;
    DWORD reg;
    DWORD value;
    int digits;
    char c;

    v9x_set_count = 0ul;
    line = GetCommandLineA();
    while (*line != '\0' && v9x_set_count < V9X_SET_MAX) {
        if (CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE, line,
                           (int)(sizeof(tag) - 1u), tag,
                           (int)(sizeof(tag) - 1u)) != CSTR_EQUAL) {
            ++line;
            continue;
        }
        line += sizeof(tag) - 1u;
        reg = 0ul;
        value = 0ul;
        digits = 0;
        for (;;) {
            c = *line;
            if (c >= '0' && c <= '9') {
                reg = (reg << 4) | (DWORD)(c - '0');
            } else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') {
                reg = (reg << 4) | (DWORD)((c | 0x20) - 'a' + 10);
            } else {
                break;
            }
            ++digits;
            ++line;
        }
        if (digits == 0 || *line != '=') {
            continue;
        }
        ++line;
        digits = 0;
        for (;;) {
            c = *line;
            if (c >= '0' && c <= '9') {
                value = (value << 4) | (DWORD)(c - '0');
            } else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') {
                value = (value << 4) | (DWORD)((c | 0x20) - 'a' + 10);
            } else {
                break;
            }
            ++digits;
            ++line;
        }
        if (digits == 0) {
            continue;
        }
        v9x_set_regs[v9x_set_count] = reg;
        v9x_set_values[v9x_set_count] = value;
        ++v9x_set_count;
    }
}

static void v9x_finish(const char *result, UINT code)
{
    v9x_text("Report", "Result", result);
    WritePrivateProfileStringA(0, 0, 0, V9X_R63D_PATH);
    ExitProcess(code);
}

void __stdcall V9xRv630DrawEntry(void)
{
    HANDLE device;
    DWORD index;
    DWORD at_cp;
    DWORD at_config;
    DWORD at_info;
    DWORD fb;
    DWORD g;
    const DWORD *r;
    int pixels_ok;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_R63D_PATH);
    v9x_text("Report", "Tool", "V9XR63D");
    v9x_text("Report", "Build", V9X_BUILD_ID);

    device = CreateFileA(V9X_EGCP_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_key("Report", "LoadError", GetLastError());
        v9x_finish("VXD-LOAD-FAILED", 1u);
    }

    /* Call 0: one status read, for the chip's id. */
    v9x_request.count = 0ul;
    v9x_request.data_count = 0ul;
    v9x_read(V9X_R6_GRBM_STATUS);
    if (!v9x_dioc(device)) {
        CloseHandle(device);
        v9x_finish("REFUSED", 2u);
    }
    v9x_key("Report", "Id", v9x_result.id);
    v9x_key("Report", "Bar0", v9x_result.bar0);
    v9x_key("Report", "Bar2", v9x_result.bar2);
    if ((v9x_result.id >> 16) != V9X_R6_DEVICE_RV630) {
        CloseHandle(device);
        v9x_finish("NOT-RV630", 6u);
    }

    if (!v9x_load_microcode("PFP.BIN", 0ul, V9X_R6_PFP_DWORDS) ||
        !v9x_load_microcode("ME.BIN", V9X_R6_PFP_DWORDS, V9X_R6_ME_DWORDS)) {
        CloseHandle(device);
        v9x_finish("MICROCODE-MISSING", 1u);
    }
    v9x_key("Report", "PfpFirst", v9x_request.data[0]);
    v9x_key("Report", "MeFirst", v9x_request.data[V9X_R6_PFP_DWORDS]);

    /* Call 1: read-only. The config list is the gpu_init table, then the
     * extra registers. */
    for (index = 0ul; index < V9X_R6_RV630_GPU_INIT_COUNT; ++index) {
        v9x_config_offsets[index] = v9x_r6_rv630_gpu_init[index].offset;
    }
    for (index = 0ul; index < V9X_EXTRA_CONFIG; ++index) {
        v9x_config_offsets[V9X_R6_RV630_GPU_INIT_COUNT + index] =
            v9x_extra_config_offsets[index];
    }
    v9x_request.count = 0ul;
    v9x_request.data_count = 0ul;
    at_cp = v9x_request.count;
    for (index = 0ul; index < V9X_CP_SAVED; ++index) {
        v9x_read(v9x_cp_offsets[index]);
    }
    at_config = v9x_request.count;
    for (index = 0ul; index < V9X_CONFIG_SAVED; ++index) {
        v9x_read(v9x_config_offsets[index]);
    }
    at_info = v9x_request.count;
    for (index = 0ul; index < V9X_INFO_COUNT; ++index) {
        v9x_read(v9x_info_offsets[index]);
    }
    if (!v9x_dioc(device)) {
        CloseHandle(device);
        v9x_finish("REFUSED", 2u);
    }
    r = v9x_result.op_results;
    for (index = 0ul; index < V9X_CP_SAVED; ++index) {
        v9x_cp_saved[index] = r[at_cp + index];
        v9x_reg_key("Before", v9x_cp_offsets[index], r[at_cp + index]);
    }
    for (index = 0ul; index < V9X_CONFIG_SAVED; ++index) {
        v9x_config_saved[index] = r[at_config + index];
        v9x_reg_key("Before", v9x_config_offsets[index],
                    r[at_config + index]);
    }
    for (index = 0ul; index < V9X_INFO_COUNT; ++index) {
        v9x_info[index] = r[at_info + index];
        v9x_reg_key("Before", v9x_info_offsets[index], r[at_info + index]);
    }

    /* The ME must be halted, as POST leaves it (r600_cp_stop halts the ME
     * alone; M-NZHWBR's BIOS leaves CP_ME_CNTL at 10000000h). */
    if ((v9x_cp_saved[0] & V9X_R6_CP_ME_HALT) != V9X_R6_CP_ME_HALT) {
        CloseHandle(device);
        v9x_finish("CP-NOT-HALTED", 3u);
    }
    /* The window's GPU address: VRAM's start from MC_VM_FB_LOCATION, whose
     * high byte must be 0 (R6xx addresses are 32 bits) and low byte leave
     * room for the window below 4 GiB. */
    fb = v9x_info[V9X_INFO_FB_LOCATION] & V9X_FB_START_MASK;
    if ((fb >> V9X_FB_HIGH_SHIFT) != 0ul ||
        (fb & V9X_FB_LOW_MASK) > V9X_FB_LOW_MAX) {
        CloseHandle(device);
        v9x_finish("FB-START-UNSUPPORTED", 4u);
    }
    g = ((fb & V9X_FB_LOW_MASK) << V9X_ADDR_SHIFT_HIGH) +
        V9X_SCRATCH_GPU_OFFSET;
    v9x_tiling_config = v9x_r6_rv630_tiling_config(v9x_info[V9X_INFO_RAMCFG]);
    v9x_key("Run", "WindowGpu", g);
    v9x_key("Run", "TilingConfig", v9x_tiling_config);
    if (v9x_switch_given("/gouraud")) {
        v9x_mode = V9X_MODE_GOURAUD;
    }
    if (v9x_switch_given("/depth")) {
        v9x_mode = V9X_MODE_DEPTH;
    }
    if (v9x_switch_given("/modulate")) {
        v9x_mode = V9X_MODE_MODULATE;
    }
    v9x_white = v9x_switch_given("/white");
    v9x_twocol = v9x_switch_given("/twocol");
    v9x_nograd = v9x_switch_given("/nograd");
    v9x_nodb = v9x_switch_given("/nodb");
    v9x_zfunc = v9x_switch_hex("/zfunc:", V9X_R6_FUNC_LESS) &
                V9X_ZFUNC_MASK;
    v9x_key("Run", "White", (DWORD)v9x_white);
    v9x_key("Run", "TwoCol", (DWORD)v9x_twocol);
    v9x_key("Run", "NoGrad", (DWORD)v9x_nograd);
    v9x_dbsync = v9x_switch_given("/dbsync");
    v9x_zfill = v9x_switch_hex("/zfill:", V9X_DEPTH_CLEAR);
    v9x_zclear = v9x_switch_hex("/zclear:", V9X_UNSET);
    v9x_zmode = v9x_switch_hex("/zmode:", V9X_UNSET);
    v9x_vte = v9x_switch_hex("/vte:", V9X_UNSET);
    v9x_scan = v9x_switch_hex("/scan:", V9X_UNSET);
    v9x_ctxdump = v9x_switch_given("/ctxdump");
    v9x_key("Run", "CtxDump", (DWORD)v9x_ctxdump);
    if (v9x_ctxdump ||
        (v9x_scan != V9X_UNSET && v9x_scan >= V9X_SCAN_CHUNKS)) {
        v9x_scan = V9X_UNSET;
    }
    v9x_key("Run", "NoDb", (DWORD)v9x_nodb);
    v9x_key("Run", "ZFunc", v9x_zfunc);
    v9x_key("Run", "DbSync", (DWORD)v9x_dbsync);
    v9x_key("Run", "ZFill", v9x_zfill);
    v9x_key("Run", "ZClear", v9x_zclear);
    v9x_key("Run", "ZMode", v9x_zmode);
    v9x_key("Run", "Vte", v9x_vte);
    v9x_key("Run", "Scan", v9x_scan);
    v9x_vcrange = v9x_switch_given("/vcrange");
    v9x_vb_offset = v9x_switch_hex("/vboff:", V9X_VB_OFFSET);
    if ((v9x_vb_offset & 0xfful) != 0ul ||
        ((v9x_vb_offset < V9X_VB_FREE_FIRST ||
          v9x_vb_offset > V9X_VB_FREE_LAST) &&
         v9x_vb_offset != V9X_VB_OFFSET)) {
        v9x_vb_offset = V9X_VB_OFFSET;
    }
    v9x_zmin = v9x_switch_hex("/zmin:", V9X_UNSET);
    v9x_zmax = v9x_switch_hex("/zmax:", V9X_UNSET);
    v9x_zscale = v9x_switch_hex("/zscale:", V9X_UNSET);
    v9x_zoffset = v9x_switch_hex("/zoffset:", V9X_UNSET);
    v9x_mesavte = v9x_switch_given("/mesavte");
    v9x_zleft = v9x_switch_given("/zleft");
    v9x_key("Run", "MesaVte", (DWORD)v9x_mesavte);
    v9x_key("Run", "ZLeft", (DWORD)v9x_zleft);
    v9x_zright = v9x_switch_given("/zright");
    v9x_zall = v9x_switch_hex("/zall:", V9X_UNSET);
    v9x_wv = v9x_switch_hex("/wv:", V9X_UNSET);
    v9x_key("Run", "ZRight", (DWORD)v9x_zright);
    v9x_key("Run", "ZAll", v9x_zall);
    v9x_key("Run", "W", v9x_wv);
    v9x_elem3 = v9x_switch_given("/elem3");
    v9x_zlit = v9x_switch_hex("/zlit:", V9X_UNSET);
    v9x_posw = v9x_switch_given("/posw");
    v9x_cfgdump = v9x_switch_given("/cfgdump");
    v9x_mesaall = v9x_switch_given("/mesaall");
    v9x_key("Run", "Elem3", (DWORD)v9x_elem3);
    v9x_key("Run", "ZLit", v9x_zlit);
    v9x_key("Run", "PosW", (DWORD)v9x_posw);
    v9x_key("Run", "CfgDump", (DWORD)v9x_cfgdump);
    v9x_key("Run", "MesaAll", (DWORD)v9x_mesaall);
    v9x_mesadb = v9x_switch_given("/mesadb");
    v9x_key("Run", "MesaDb", (DWORD)v9x_mesadb);
    v9x_zexport = v9x_switch_hex("/zexport:", V9X_UNSET);
    v9x_key("Run", "ZExport", v9x_zexport);
    v9x_parse_sets();
    for (index = 0ul; index < v9x_set_count; ++index) {
        char key[8];
        char text[20];

        /* "SetN=RRRRR VVVVVVVV" */
        lstrcpyA(key, "Set");
        v9x_hex(key + 3, index, 1);
        v9x_hex(text, v9x_set_regs[index], 5);
        text[5] = ' ';
        v9x_hex(text + 6, v9x_set_values[index], 8);
        v9x_text("Run", key, text);
    }
    v9x_tex_seed = v9x_switch_hex("/texseed:", V9X_TEX_SEED);
    v9x_key("Run", "TexSeed", v9x_tex_seed);
    v9x_key("Run", "VcRange", (DWORD)v9x_vcrange);
    v9x_key("Run", "VbOffset", v9x_vb_offset);
    v9x_key("Run", "ZMin", v9x_zmin);
    v9x_key("Run", "ZMax", v9x_zmax);
    v9x_key("Run", "ZScale", v9x_zscale);
    v9x_key("Run", "ZOffset", v9x_zoffset);
    v9x_key("Run", "Colour", V9X_COLOUR);
    v9x_key("Run", "Background", V9X_BACKGROUND);
    if (v9x_switch_given("/readonly")) {
        CloseHandle(device);
        v9x_finish("READONLY", 0u);
    }

    /* Call 2: the run. The microcode is still in the data area. */
    v9x_build_draw();
    if (!v9x_build_ring(g) ||
        V9X_DATA_RING + v9x_stream.count + v9x_vs_dwords + v9x_ps_dwords +
        v9x_vertex_dwords > V9X_EGCP_DATA) {
        CloseHandle(device);
        v9x_key("Run", "RingDwords", v9x_stream.count);
        v9x_finish("RING-TOO-LONG", 5u);
    }
    v9x_dump_ring();
    v9x_build_run(g);
    if (!v9x_dioc(device)) {
        CloseHandle(device);
        v9x_key("Report", "Executed", v9x_result.executed);
        v9x_finish("RUN-REFUSED", 5u);
    }
    CloseHandle(device);
    r = v9x_result.op_results;
    v9x_key("During", "GRBM_SOFT_RESET_Pulse", r[v9x_at_reset_read]);
    v9x_key("During", "WaitInit", r[v9x_at_wait_init]);
    v9x_key("During", "WaitTest", r[v9x_at_wait_test]);
    v9x_key("During", "TestValue", r[v9x_at_test_value]);
    v9x_key("During", "WaitDraw", r[v9x_at_wait_draw]);
    v9x_key("During", "WaitIdle", r[v9x_at_wait_idle]);
    v9x_key("During", "DrawnValue", r[v9x_at_drawn_value]);
    v9x_key("During", "CP_RB_RPTR", r[v9x_at_rptr]);
    v9x_key("During", "CP_STAT", r[v9x_at_cp_stat]);
    v9x_key("During", "GRBM_STATUS", r[v9x_at_grbm]);
    v9x_key("During", "WaitPutBack", r[v9x_at_wait_put_back]);
    v9x_key("During", "PutBackValue", r[v9x_at_put_back_value]);
    v9x_key("After", "CP_ME_CNTL", r[v9x_at_after_me_cntl]);
    v9x_key("After", "GRBM_STATUS", r[v9x_at_after_grbm]);

    pixels_ok = v9x_verify(v9x_result.block + r[v9x_at_block]);
    v9x_text("Result", "MeInitialize",
             r[v9x_at_wait_init] != V9X_EGCP_TIMEOUT ? "PASS" : "FAIL");
    v9x_text("Result", "RingTest",
             r[v9x_at_test_value] == V9X_SCRATCH_TEST ? "PASS" : "FAIL");
    v9x_text("Result", "DrawMarker",
             r[v9x_at_drawn_value] == V9X_SCRATCH_DRAWN ? "PASS" : "FAIL");
    v9x_text("Result", "GuiIdle",
             r[v9x_at_wait_idle] != V9X_EGCP_TIMEOUT ? "PASS" : "FAIL");
    v9x_text("Result", "PutBack",
             r[v9x_at_put_back_value] == V9X_SCRATCH_PUT_BACK ? "PASS" :
                                                                "FAIL");
    v9x_text("Result", "Pixels", pixels_ok ? "PASS" : "FAIL");
    if (v9x_mode == V9X_MODE_DEPTH) {
        v9x_text("Result", "DepthBuffer",
                 v9x_depth_report(v9x_result.block +
                                  r[v9x_at_depth_block]) ? "PASS" : "FAIL");
    }
    if (v9x_scan != V9X_UNSET) {
        v9x_scan_report(v9x_result.block + r[v9x_at_scan_block]);
    }
    if (v9x_ctxdump) {
        v9x_ctx_report(v9x_result.block + r[v9x_at_ctx_block]);
    }
    if (v9x_cfgdump) {
        v9x_cfg_report(r + v9x_at_cfg);
    }
    v9x_finish("COMPLETE", 0u);
}
