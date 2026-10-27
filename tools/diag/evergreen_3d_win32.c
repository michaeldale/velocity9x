/*
 * V9XEG3D.EXE: the AMD Evergreen (Radeon HD 5450, Cedar) 3D engine's first
 * draw: the GPU configured, the CP started with clear state, and Linux
 * 3.9's textured copy of a 64x8 RGBA image, checked byte for byte.
 *
 * Phase 2 of docs\plans\ati-evergreen-cedar-3d.md. EGCP.VXD executes the
 * op lists built here (V9XEGCP's VxD, with evergreen_gpu_init's registers
 * added to its allow-lists). Writes C:\V9XDIAG\V9XEG3D.INI. The microcode
 * is read from PFP.BIN and ME.BIN in the current directory, as V9XEGCP
 * reads it.
 *
 * Sources, all under build\external\evergreen\:
 *   linux-radeon\evergreen.c        evergreen_gpu_init (Cedar's values),
 *                                    evergreen_cp_load_microcode,
 *                                    evergreen_cp_resume, evergreen_cp_start;
 *   linux-3.9\evergreen_blit_kms.c  set_default_state, set_shaders,
 *                                    set_tex_resource, set_render_target,
 *                                    set_scissors, set_vtx_resource,
 *                                    draw_auto, cp_set_surface_sync;
 *   linux-3.9\evergreen_blit_shaders.c  evergreen_default_state and the
 *                                    hand-assembled VS and PS (AMD, MIT);
 *   radeon_blit_common.h (v3.9)     DI_PT_RECTLIST 11h, DI_SRC_SEL_AUTO_INDEX
 *                                    2, FMT_ and COLOR_8_8_8_8 1Ah.
 * Linux 3.9 drew 1D-tiled surfaces; this draws linear-aligned ones (array
 * mode 1 in both the colour buffer and the texture), 64 pixels wide so the
 * pitch meets that mode's 64-element alignment.
 *
 * The shaders come from src\chipsets\ati\evergreen\eg_shader.c, whose host
 * test holds the blit pair to Linux's words. Phase 3 adds two draws in
 * place of the copy:
 *   /gouraud  two triangles over the 64x8 target, red rising from 0 at the
 *             left edge to 1 at the right, green 0.5, blue 0.25, alpha 1;
 *             each pixel checked against the interpolated value, +-2;
 *   /depth    a 1D-tiled Z16 buffer filled with FFFFh, LESS with writes:
 *             the whole target red at z 0.5, the left half green at 0.75
 *             (rejected), the right half blue at 0.25 (passes). Expected:
 *             red left, blue right; the depth buffer's eight tiles
 *             reported;
 *   /modulate the source pattern sampled at unnormalised texel
 *             coordinates and multiplied by a colour (red rising 0 to 1,
 *             green 1, blue 0.5, alpha 1): Direct3D's default stage,
 *             with two interpolants, checked per pixel, +-2.
 *
 * Call 1 is read-only: the CP's registers and every gpu_init register.
 * The run goes ahead only if the CP is halted, as POST leaves it. /readonly
 * stops there.
 *
 * Call 2:
 *   1. the scratch window (BAR0 + 241 MiB) saved, cleared, the source
 *      pattern and a guarded destination laid out, the ring, shaders and
 *      vertices written;
 *   2. evergreen_gpu_init's writes for Cedar, read-modify-write where
 *      Linux reads first. GB_BACKEND_MAP is 0: r6xx_remap_render_backend
 *      for one render backend gives 0 whatever the fuse straps say, so the
 *      RCU's straps are not read. The context registers it writes are
 *      left to CLEAR_STATE and packets, and DMIF_, HDP_ and
 *      DMA_TILING_CONFIG keep the BIOS's values: the display, the host
 *      path and the running DMA engine use them, and this draws nothing
 *      tiled;
 *   3. the CP stopped, microcode loaded, reset and resumed as V9XEGCP does;
 *   4. ME_INITIALIZE, then evergreen_cp_start's clear-state preamble;
 *   5. r600_ring_test;
 *   6. the draw: set_default_state (with the default state inline rather
 *      than through an indirect buffer), set_shaders, set_tex_resource,
 *      set_render_target, set_scissors, set_vtx_resource, draw_auto, the
 *      CB surface sync, then SCRATCH_REG0 written through the ring;
 *   7. GUI idle awaited, the CP halted, a GRBM reset of the CP blocks (so a
 *      hung pipe is left reset), every register put back as call 1 read it,
 *      HDP flushed, the destination read back, the window restored.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/eg_shader.h"
#include "velocity9x/eg_3d.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_EG3D_PATH        V9X_DIAG_PATH("V9XEG3D.INI")
#define V9X_EGCP_VXD         "\\\\.\\EGCP.VXD"
#define V9X_EGCP_MAGIC       0x50434745ul
#define V9X_EGCP_RUN         1u
#define V9X_EGCP_OP_MAX      512u
#define V9X_EGCP_BLOCK       4096u
#define V9X_EGCP_DATA        4096u
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

/* EGCP.VXD's EC_PATTERN_STEP. */
#define V9X_PATTERN_STEP    0x9e3779b9ul

/* Registers, evergreend.h; the CP's and those eg_3d.c's streams use are
 * in eg_3d.h. */
#define V9X_EG_MC_VM_FB_LOCATION    0x2024ul
#define V9X_EG_HDP_MISC_CNTL        0x2f4cul
#define V9X_EG_HDP_MEM_FLUSH        0x5480ul
#define V9X_EG_CGTS_SYS_TCC_DISABLE 0x3f90ul
#define V9X_EG_CGTS_USER_SYS_TCC    0x3f94ul
#define V9X_EG_CP_QUEUE_THRESHOLDS  0x8760ul
#define V9X_EG_CP_MEQ_THRESHOLDS    0x8764ul
#define V9X_EG_CP_PERFMON_CNTL      0x87fcul
#define V9X_EG_VGT_CACHE_INVALIDATE 0x88c4ul
#define V9X_EG_VGT_GS_VERTEX_REUSE  0x88d4ul
#define V9X_EG_VGT_NUM_INSTANCES    0x8974ul
#define V9X_EG_PA_CL_ENHANCE        0x8a14ul
#define V9X_EG_PA_SU_LINE_STIPPLE   0x8a60ul
#define V9X_EG_PA_SC_LINE_STIPPLE   0x8b10ul
#define V9X_EG_PA_SC_FORCE_EOV      0x8b24ul
#define V9X_EG_PA_SC_FIFO_SIZE      0x8bccul
#define V9X_EG_SQ_GPR_MGMT_1        0x8c04ul
#define V9X_EG_SQ_GPR_MGMT_2        0x8c08ul
#define V9X_EG_SQ_GPR_MGMT_3        0x8c0cul
#define V9X_EG_SQ_THREAD_MGMT       0x8c18ul
#define V9X_EG_SQ_THREAD_MGMT_2     0x8c1cul
#define V9X_EG_SQ_STACK_MGMT_1      0x8c20ul
#define V9X_EG_SQ_STACK_MGMT_2      0x8c24ul
#define V9X_EG_SQ_STACK_MGMT_3      0x8c28ul
#define V9X_EG_SQ_MS_FIFO_SIZES     0x8cf0ul
#define V9X_EG_SX_EXPORT_SIZES      0x900cul
#define V9X_EG_SX_DEBUG_1           0x9058ul
#define V9X_EG_SPI_CONFIG_CNTL      0x9100ul
#define V9X_EG_SPI_CONFIG_CNTL_1    0x913cul
#define V9X_EG_CGTS_TCC_DISABLE     0x9148ul
#define V9X_EG_CGTS_USER_TCC        0x914cul
#define V9X_EG_TA_CNTL_AUX          0x9508ul
#define V9X_EG_CC_RB_BACKEND_DIS    0x98f4ul
#define V9X_EG_GB_ADDR_CONFIG       0x98f8ul
#define V9X_EG_GB_BACKEND_MAP       0x98fcul
#define V9X_EG_CB_PERF_CTR0_SEL_0   0x9a20ul
#define V9X_EG_SMX_SAR_CTL0         0xa008ul
#define V9X_EG_SMX_DC_CTL0          0xa020ul

/* evergreen_gpu_init's writes for Cedar are eg_3d.c's table; /gbaddr:
 * overrides its GB_ADDR_CONFIG. */
#define V9X_EG_CEDAR_GB_ADDR_CONFIG 0x02010001ul /* CEDAR_GB_ADDR_CONFIG_GOLDEN */
#define V9X_EG_FB_START_MASK        0x0000fffful

/* PM4 packets (evergreend.h, radeon_blit_common.h). */
#define V9X_PACKET3(op, n) \
    ((3ul << 30) | (((DWORD)(op) & 0xfful) << 8) | \
     (((DWORD)(n) & 0x3ffful) << 16))
#define V9X_PACKET2                 0x80000000ul
#define V9X_P3_CLEAR_STATE          0x12ul
#define V9X_P3_MODE_CONTROL         0x18ul
#define V9X_P3_INDEX_TYPE           0x2aul
#define V9X_P3_DRAW_INDEX_AUTO      0x2dul
#define V9X_P3_NUM_INSTANCES        0x2ful
#define V9X_P3_SURFACE_SYNC         0x43ul
#define V9X_P3_ME_INITIALIZE        0x44ul
#define V9X_P3_PREAMBLE_CNTL        0x4aul
#define V9X_P3_SET_CONFIG_REG       0x68ul
#define V9X_P3_SET_CONTEXT_REG      0x69ul
#define V9X_P3_SET_RESOURCE         0x6dul
#define V9X_SET_CONFIG_START        0x00008000ul
#define V9X_SET_CONTEXT_START       0x00028000ul
#define V9X_PREAMBLE_BEGIN_CLEAR    0x20000000ul /* 2 << 28 */
#define V9X_PREAMBLE_END_CLEAR      0x30000000ul /* 3 << 28 */
#define V9X_SYNC_CB_ACTION          0x02000000ul /* 1 << 25 */
#define V9X_SYNC_CB0_DEST_BASE      0x00000040ul /* 1 << 6 */
#define V9X_SYNC_TC_ACTION          0x00800000ul /* 1 << 23 */
#define V9X_SYNC_SH_ACTION          0x08000000ul /* 1 << 27 */
#define V9X_SYNC_DB_ACTION          0x04000000ul /* 1 << 26 */
#define V9X_SYNC_DB_DEST_BASE       0x00004000ul /* 1 << 14 */
#define V9X_SYNC_POLL_INTERVAL      10ul
#define V9X_DI_PT_TRILIST           0x04ul
#define V9X_DI_PT_RECTLIST          0x11ul
#define V9X_DI_INDEX_SIZE_16_BIT    0x0ul
#define V9X_DI_SRC_SEL_AUTO_INDEX   0x2ul
#define V9X_EG_MAX_HW_CONTEXTS      4ul

/* Context registers. */
#define V9X_EG_PA_SC_SCREEN_SCISSOR  0x28030ul
#define V9X_EG_PA_SC_WINDOW_SCISSOR  0x28204ul
#define V9X_EG_PA_SC_GENERIC_SCISSOR 0x28240ul
#define V9X_EG_SQ_PGM_START_PS       0x28840ul
#define V9X_EG_SQ_PGM_START_VS       0x2885cul
#define V9X_EG_CB_COLOR0_BASE        0x28c60ul
/* The depth block (evergreend.h): DB_Z_INFO, DB_STENCIL_INFO, the Z and
 * stencil read and write bases, DB_DEPTH_SIZE and DB_DEPTH_SLICE are the
 * eight registers from 0x28040; DB_DEPTH_CONTROL is apart. */
#define V9X_EG_DB_Z_INFO             0x28040ul
#define V9X_EG_DB_DEPTH_CONTROL      0x28800ul
#define V9X_DB_FORMAT_Z_16           1ul
#define V9X_DB_Z_ENABLE              0x00000002ul
#define V9X_DB_Z_WRITE_ENABLE        0x00000004ul
#define V9X_DB_ZFUNC_LESS            0x00000010ul /* 1 << 4 */

/* The draw: 64x8 pixels of COLOR_8_8_8_8, linear aligned. */
#define V9X_DRAW_WIDTH              64ul
#define V9X_DRAW_HEIGHT             8ul
#define V9X_DRAW_BYTES              (V9X_DRAW_WIDTH * V9X_DRAW_HEIGHT * 4ul)
#define V9X_DRAW_DWORDS             (V9X_DRAW_BYTES / 4ul)
#define V9X_FORMAT_8_8_8_8          0x1aul
#define V9X_ARRAY_LINEAR_ALIGNED    1ul
#define V9X_ARRAY_1D_TILED_THIN1    2ul
/* CB_COLOR0_ATTRIB bit 4, evergreend.h S_028C74_NON_DISP_TILING_ORDER. */
#define V9X_CB_NON_DISP_TILING_ORDER 0x00000010ul

/* The array mode of both surfaces: linear aligned, or with /tiled the
 * 1D-tiled mode Linux 3.9's blit drew. A tiled copy of a tiled image
 * reproduces the source bytes whatever the tiling, so it tests the
 * pipeline apart from the linear layout. */
static DWORD v9x_array_mode = V9X_ARRAY_LINEAR_ALIGNED;

/* Experiment switches, each overriding one value (hex): /cbmode: and
 * /texmode: the colour buffer's and the texture's array mode apart,
 * /scissor: the scissor's right edge (draw only that many pixels of each
 * row), /gbaddr: GB_ADDR_CONFIG, /attrib: CB_COLOR0_ATTRIB. */
static DWORD v9x_cb_mode;
static DWORD v9x_tex_mode;
static DWORD v9x_scissor_right = V9X_DRAW_WIDTH;
static DWORD v9x_gb_addr_config = V9X_EG_CEDAR_GB_ADDR_CONFIG;
static DWORD v9x_cb_attrib;
#define V9X_SQ_TEX_DIM_2D           1ul
#define V9X_SQ_TEX_VALID_TEXTURE    0x2ul
#define V9X_SQ_TEX_VALID_BUFFER     0x3ul
/* Float bit patterns: the rectangle's corners, the depths and colours. */
#define V9X_FLOAT_0                 0x00000000ul
#define V9X_FLOAT_0_25              0x3e800000ul
#define V9X_FLOAT_0_5               0x3f000000ul
#define V9X_FLOAT_0_75              0x3f400000ul
#define V9X_FLOAT_1                 0x3f800000ul
#define V9X_FLOAT_8                 0x41000000ul
#define V9X_FLOAT_32                0x42000000ul
#define V9X_FLOAT_64                0x42800000ul

/* The draw: the blit copy, or Phase 3's Gouraud or depth test. */
#define V9X_MODE_BLIT               0ul
#define V9X_MODE_GOURAUD            1ul
#define V9X_MODE_DEPTH              2ul
#define V9X_MODE_MODULATE           3ul
/* The HAL's draws, through eg_3d.c's state and eg_shader.c's Direct3D
 * shaders, the vertices in the render core's TL layout:
 *   /d3dtex    textured, white, rhw 1, u 0 to 1 across: the source copied;
 *   /d3dpersp  the same with rhw 1 at the left and 0.25 at the right;
 *   /d3dcolour untextured, one D3DCOLOR, which every pixel should hold;
 *   /d3dz      /depth's three rectangles, depth through eg_3d.c;
 *   /d3d565    untextured into a 565 target. */
#define V9X_MODE_D3D_FIRST          4ul
#define V9X_MODE_D3D_TEX            4ul
#define V9X_MODE_D3D_PERSP          5ul
#define V9X_MODE_D3D_COLOUR         6ul
#define V9X_MODE_D3D_Z              7ul
#define V9X_MODE_D3D_565            8ul
/* /d3d1555: /d3dtex with the source read as a 64x8 ARGB1555 texture
 * (its first 128 bytes a row), each pixel against the word it samples,
 * channel by channel. */
#define V9X_MODE_D3D_1555           9ul
/* /texw: and /texpitch: (hex, texels): the 1555 texture's width and pitch,
 * 64 and 64 by default; the draw still spans 64 pixels, so a narrower
 * texture is stretched across it. */
static DWORD v9x_tex_width = 64ul;
static DWORD v9x_tex_pitch = 64ul;
/* /tex8888: the same draw with 32-bit texels, the pattern's dwords. */
static int v9x_tex_8888;

/*
 * /d3dmip:K: a 32x32 8888 texture with a full chain (6 levels), laid out
 * linear aligned as Linux's evergreen_cs.c sizes it - every level's pitch
 * 64 texels (MAX(64, 256 / 4)), its rows unpadded, the levels one after
 * another from MIP_ADDRESS - over a pattern in the window's free space.
 * The sampler's LOD clamp holds the draw at level K with point mip
 * filtering, and each pixel is checked against the dword the model puts
 * there. Where a pixel holds another dword of the area, its index is
 * reported, to show the layout the sampler used instead.
 */
#define V9X_MODE_D3D_MIP            10ul
#define V9X_MIP_OFFSET              0x9000ul
#define V9X_MIP_DWORDS              0x1000ul
#define V9X_MIP_SEED                0x11223344ul
#define V9X_MIP_EDGE                32ul
#define V9X_MIP_LEVELS              6ul
#define V9X_MIP_PITCH_TEXELS        64ul
static DWORD v9x_mip_level;
/*
 * /d3dfog: the lit shaders (v9x_eg_shader_d3d_vs_lit and _ps_lit), three
 * bands of one D3DCOLOR, each with its own specular dword: x 0 to 16 a
 * colour and fog factor AAh, 16 to 32 factor 0 (all fog), 32 to 64
 * factor FFh and no colour (no fog). Fog colour V9X_FOG_COLOUR. Each
 * pixel is checked against fog + f * (min(1, colour + specular) - fog),
 * alpha the colour's. /d3dfogtex: the same over /d3dtex's texture,
 * modulated by white.
 */
#define V9X_MODE_D3D_FOG            11ul
#define V9X_MODE_D3D_FOG_TEX        12ul
#define V9X_FOG_COLOUR              0x00ff8000ul
#define V9X_FOG_SPECULAR_0          0xaa102030ul
#define V9X_FOG_SPECULAR_1          0x00000000ul
#define V9X_FOG_SPECULAR_2          0xff000000ul
#define V9X_FLOAT_16                0x41800000ul
static DWORD v9x_specular;
static DWORD v9x_mode = V9X_MODE_BLIT;
/* PA_SU_VTX_CNTL's PIX_CENTER_HALF for the Direct3D draws; /centre0
 * clears it. */
static DWORD v9x_centre_half = 1ul;
/* /vte: PA_CL_VTE_CNTL for the Direct3D draws, all ones for the
 * builder's. */
#define V9X_EG_PA_CL_VTE_CNTL       0x28818ul
static DWORD v9x_vte_override = 0xfffffffful;
/* /batches: how many whole Direct3D batches the draw sends (hex). */
static DWORD v9x_batches = 1ul;

#define V9X_D3D_VERTEX_DWORDS       8ul
/* D3DCOLORs, 0xAARRGGBB, stored as they are: B, G, R, A in memory. */
#define V9X_D3D_WHITE               0xfffffffful
#define V9X_D3D_RED                 0xffff0000ul
#define V9X_D3D_GREEN               0xff00ff00ul
#define V9X_D3D_BLUE                0xff0000fful
#define V9X_D3D_COLOUR_VALUE        0x80402010ul
/* R 84h, G 82h, B 42h are 16, 32 and 8 to within 0.1 in 5:6:5, so the
 * pixel is 8408h whichever way the CB rounds. */
#define V9X_D3D_565_COLOUR          0xff848242ul
#define V9X_D3D_565_PIXELS          0x84088408ul

/* /modulate's second interpolant (evergreend.h): the VS exports two
 * parameters, semantics 0 and 1, and the PS reads both. */
#define V9X_EG_SPI_VS_OUT_ID_0       0x2861cul
#define V9X_EG_SPI_PS_INPUT_CNTL_1   0x28648ul
#define V9X_EG_SPI_VS_OUT_CONFIG     0x286c4ul
#define V9X_EG_SPI_PS_IN_CONTROL_0   0x286ccul
#define V9X_SPI_SEMANTIC_1_IN_ID_0   0x00000100ul /* SEMANTIC_1(1) */
/* SEMANTIC(1) | DEFAULT_VAL(1), as the default state's CNTL_0 (100h). */
#define V9X_SPI_PS_INPUT_1_VALUE     0x00000101ul
#define V9X_SPI_VS_EXPORT_COUNT_2    0x00000002ul /* VS_EXPORT_COUNT(1) */
/* The default state's 20000001h with NUM_INTERP(2). */
#define V9X_SPI_PS_IN_CONTROL_2      0x20000002ul
#define V9X_MODULATE_VERTEX_DWORDS   (V9X_EG_MODULATE_STRIDE / 4ul)

/* The vertices: the blit's three (16 bytes each), or up to three
 * rectangles of two triangles at {x, y, z, w, r, g, b, a}. */
#define V9X_BLIT_STRIDE             16ul
#define V9X_COLOUR_STRIDE           32ul
#define V9X_COLOUR_VERTEX_DWORDS    8ul
#define V9X_VERTEX_DWORDS_MAX       144ul
static DWORD v9x_vertices[V9X_VERTEX_DWORDS_MAX];
static DWORD v9x_vertex_dwords;
static DWORD v9x_vertex_stride;
static DWORD v9x_vertex_count;

static v9x_u32 v9x_vs[V9X_EG_SHADER_DWORDS_MAX];
static v9x_u32 v9x_ps[V9X_EG_SHADER_DWORDS_MAX];
static DWORD v9x_vs_dwords;
static DWORD v9x_ps_dwords;

/* Expected 8-bit channels and the tolerance of an interpolated one. */
#define V9X_CHANNEL_TOLERANCE       2ul
#define V9X_PIXEL_RED               0xff0000fful
#define V9X_PIXEL_BLUE              0xffff0000ul
#define V9X_PIXEL_GREEN             0xff00ff00ul

/* The window's layout. */
#define V9X_RING_BYTES              0x4000ul
#define V9X_RB_CNTL                 ((9ul << 8) | 11ul)
#define V9X_RING_ALIGN_DWORDS       16ul
#define V9X_SCRATCH_WB              0x4000ul
#define V9X_RPTR_WB                 0x4100ul
#define V9X_VS_OFFSET               0x5000ul
#define V9X_PS_OFFSET               0x5100ul
/* After the largest PS, a lit one of 92 dwords. */
#define V9X_VB_OFFSET               0x5400ul
#define V9X_SRC_OFFSET              0x6000ul
#define V9X_DST_OFFSET              0x7000ul
#define V9X_DST_GUARD_DWORDS        1024ul
/* The Z16 depth buffer, 64x8 1D tiled: eight 8x8 tiles of 128 bytes,
 * read back with the destination. */
#define V9X_DEPTH_OFFSET            0x8000ul
#define V9X_DEPTH_DWORDS            256ul
#define V9X_DEPTH_TILE_DWORDS       32ul
#define V9X_DEPTH_CLEAR             0xfffffffful
#define V9X_READBACK_DWORDS         (V9X_DST_GUARD_DWORDS + V9X_DEPTH_DWORDS)
#define V9X_GUARD_VALUE             0x5a5a5a5aul
#define V9X_SRC_SEED                0x01020304ul
#define V9X_SCRATCH_GPU_OFFSET      0x0f100000ul
#define V9X_WINDOW_DWORDS           16384ul

#define V9X_SCRATCH_SEED            0xcafedeadul
#define V9X_SCRATCH_TEST            0xdeadbeeful
#define V9X_SCRATCH_DRAWN           0x3d3d3d3dul
#define V9X_RESET_DELAY_READS       20000ul
#define V9X_SHORT_DELAY_READS       2000ul

/* The data area: the microcode, then the ring, then the shaders and
 * vertices. */
#define V9X_DATA_RING               (V9X_EG_PFP_DWORDS + V9X_EG_ME_DWORDS)
#define V9X_RING_MAX                1200ul

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

typedef char v9x_egcp_header_matches[
    offsetof(struct v9x_egcp_result, op_results) ==
    sizeof(DWORD) * V9X_EGCP_HEADER ? 1 : -1];

/* Every register call 1 reads. The first V9X_CP_SAVED are the CP's, put
 * back as V9XEGCP puts them back; the rest are gpu_init's, put back after
 * the CP is halted and reset. */
static const DWORD v9x_saved_offsets[] = {
    V9X_EG_CP_ME_CNTL, V9X_EG_CP_RB_CNTL, V9X_EG_CP_RB_BASE,
    V9X_EG_CP_RB_RPTR_ADDR, V9X_EG_CP_RB_RPTR_ADDR_HI, V9X_EG_CP_RB_WPTR,
    V9X_EG_CP_DEBUG, V9X_EG_SCRATCH_REG0, V9X_EG_SCRATCH_UMSK,
    V9X_EG_SCRATCH_ADDR, V9X_EG_CP_SEM_WAIT_TIMER, V9X_EG_CP_SEM_INCOMPLETE,
    V9X_EG_CP_RB_WPTR_DELAY,
    V9X_EG_GRBM_CNTL, V9X_EG_GB_ADDR_CONFIG, V9X_EG_GB_BACKEND_MAP,
    V9X_EG_CGTS_SYS_TCC_DISABLE, V9X_EG_CGTS_USER_SYS_TCC,
    V9X_EG_CGTS_TCC_DISABLE, V9X_EG_CGTS_USER_TCC,
    V9X_EG_CP_QUEUE_THRESHOLDS, V9X_EG_CP_MEQ_THRESHOLDS, V9X_EG_TA_CNTL_AUX,
    V9X_EG_SX_DEBUG_1, V9X_EG_SMX_DC_CTL0, V9X_EG_SMX_SAR_CTL0,
    V9X_EG_SX_EXPORT_SIZES, V9X_EG_PA_SC_FIFO_SIZE, V9X_EG_VGT_NUM_INSTANCES,
    V9X_EG_SPI_CONFIG_CNTL, V9X_EG_SPI_CONFIG_CNTL_1, V9X_EG_CP_PERFMON_CNTL,
    V9X_EG_SQ_MS_FIFO_SIZES, V9X_EG_SQ_CONFIG, V9X_EG_SQ_GPR_MGMT_1,
    V9X_EG_SQ_GPR_MGMT_2, V9X_EG_SQ_GPR_MGMT_3, V9X_EG_SQ_THREAD_MGMT,
    V9X_EG_SQ_THREAD_MGMT_2, V9X_EG_SQ_STACK_MGMT_1, V9X_EG_SQ_STACK_MGMT_2,
    V9X_EG_SQ_STACK_MGMT_3, V9X_EG_SQ_DYN_GPR_FLUSH, V9X_EG_PA_SC_FORCE_EOV,
    V9X_EG_VGT_CACHE_INVALIDATE, V9X_EG_VGT_GS_VERTEX_REUSE,
    V9X_EG_PA_SU_LINE_STIPPLE, V9X_EG_PA_SC_LINE_STIPPLE,
    V9X_EG_CB_PERF_CTR0_SEL_0, V9X_EG_CB_PERF_CTR0_SEL_0 + 4ul,
    V9X_EG_CB_PERF_CTR0_SEL_0 + 8ul, V9X_EG_CB_PERF_CTR0_SEL_0 + 12ul,
    V9X_EG_CB_PERF_CTR0_SEL_0 + 16ul, V9X_EG_CB_PERF_CTR0_SEL_0 + 20ul,
    V9X_EG_CB_PERF_CTR0_SEL_0 + 24ul, V9X_EG_CB_PERF_CTR0_SEL_0 + 28ul,
    V9X_EG_HDP_MISC_CNTL, V9X_EG_PA_CL_ENHANCE,
    /* Written by the CP through the ring (set_default_state, draw_auto),
     * put back like the rest. */
    V9X_EG_SQ_LDS_RESOURCE_MGMT, V9X_EG_VGT_PRIMITIVE_TYPE
};
#define V9X_SAVED_COUNT (sizeof(v9x_saved_offsets) / sizeof(v9x_saved_offsets[0]))
#define V9X_CP_SAVED 13ul

/* Registers read and published but never written. */
static const DWORD v9x_info_offsets[] = {
    V9X_EG_CC_RB_BACKEND_DIS, 0x8950ul /* CC_GC_SHADER_PIPE_CONFIG */,
    0x2f48ul /* HDP_ADDR_CONFIG */,
    V9X_EG_GRBM_STATUS, V9X_EG_CP_STAT, V9X_EG_MC_VM_FB_LOCATION
};
#define V9X_INFO_COUNT (sizeof(v9x_info_offsets) / sizeof(v9x_info_offsets[0]))

static struct v9x_egcp_request v9x_request;
static struct v9x_egcp_result v9x_result;
static DWORD v9x_saved[V9X_SAVED_COUNT];
static DWORD v9x_info[V9X_INFO_COUNT];
static DWORD v9x_ring[V9X_RING_MAX];
static DWORD v9x_ring_count;

/* Indices of results the report needs. */
static DWORD v9x_at_wait_init;
static DWORD v9x_at_wait_preamble;
static DWORD v9x_at_wait_test;
static DWORD v9x_at_test_value;
static DWORD v9x_at_wait_draw;
static DWORD v9x_at_wait_idle;
static DWORD v9x_at_drawn_value;
static DWORD v9x_at_rptr;
static DWORD v9x_at_cp_stat;
static DWORD v9x_at_grbm;
static DWORD v9x_at_after_grbm;
static DWORD v9x_at_after_me_cntl;
static DWORD v9x_at_config_first;
static DWORD v9x_at_block;
static DWORD v9x_wptr_init;
static DWORD v9x_wptr_preamble;
static DWORD v9x_wptr_test;
static DWORD v9x_wptr_draw;

static void v9x_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15ul];
    }
    text[digits] = '\0';
}

static void v9x_decimal(char *text, DWORD value)
{
    char digits[11];
    int count;
    int at;

    count = 0;
    do {
        digits[count++] = (char)('0' + (int)(value % 10ul));
        value /= 10ul;
    } while (value != 0ul);
    at = 0;
    while (count != 0) {
        text[at++] = digits[--count];
    }
    text[at] = '\0';
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_EG3D_PATH);
}

static void v9x_key(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_hex(text, value, 8);
    v9x_text(section, key, text);
}

static void v9x_reg_key(const char *section, DWORD offset, DWORD value)
{
    char key[12];

    key[0] = 'R';
    v9x_hex(key + 1, offset, 5);
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

static DWORD v9x_saved_value(DWORD offset)
{
    DWORD index;

    for (index = 0ul; index < V9X_SAVED_COUNT; ++index) {
        if (v9x_saved_offsets[index] == offset) {
            return v9x_saved[index];
        }
    }
    return 0ul;
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

static int v9x_load_microcode(const char *path, DWORD first, DWORD count)
{
    HANDLE file;
    BYTE bytes[V9X_EG_ME_DWORDS * 4ul];
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
    if (size != count * 4ul || size > sizeof(bytes) ||
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

static void v9x_ring_put(DWORD value)
{
    if (v9x_ring_count < V9X_RING_MAX) {
        v9x_ring[v9x_ring_count] = value;
    }
    ++v9x_ring_count;
}

static void v9x_ring_list(const DWORD *values, DWORD count)
{
    DWORD index;

    for (index = 0ul; index < count; ++index) {
        v9x_ring_put(values[index]);
    }
}

static DWORD v9x_ring_close(void)
{
    while ((v9x_ring_count % V9X_RING_ALIGN_DWORDS) != 0ul) {
        v9x_ring_put(V9X_PACKET2);
    }
    return v9x_ring_count;
}

static void v9x_set_config(DWORD offset, DWORD value)
{
    v9x_ring_put(V9X_PACKET3(V9X_P3_SET_CONFIG_REG, 1ul));
    v9x_ring_put((offset - V9X_SET_CONFIG_START) >> 2);
    v9x_ring_put(value);
}

static void v9x_set_context(DWORD offset, DWORD value)
{
    v9x_ring_put(V9X_PACKET3(V9X_P3_SET_CONTEXT_REG, 1ul));
    v9x_ring_put((offset - V9X_SET_CONTEXT_START) >> 2);
    v9x_ring_put(value);
}

/* cp_set_surface_sync, Evergreen form (no CP_COHER_CNTL2 before Cayman). */
static void v9x_surface_sync(DWORD sync_type, DWORD size, DWORD address)
{
    v9x_ring_put(V9X_PACKET3(V9X_P3_SURFACE_SYNC, 3ul));
    v9x_ring_put(sync_type);
    v9x_ring_put((size + 255ul) >> 8);
    v9x_ring_put(address >> 8);
    v9x_ring_put(V9X_SYNC_POLL_INTERVAL);
}

/* eg_3d.c's streams, staged and then put into the ring. */
static v9x_u32 v9x_stage[V9X_RING_MAX];
static struct v9x_eg_pm4 v9x_stage_stream;

static struct v9x_eg_pm4 *v9x_stage_begin(void)
{
    v9x_eg_pm4_begin(&v9x_stage_stream, v9x_stage, V9X_RING_MAX);
    return &v9x_stage_stream;
}

/* Into the ring; an overflowed stage makes the ring too long to run. */
static void v9x_stage_end(void)
{
    v9x_ring_list(v9x_stage, v9x_stage_stream.count);
    if (v9x_stage_stream.overflow != 0ul) {
        v9x_ring_count = V9X_RING_MAX + 1ul;
    }
}

/* evergreen_cp_start after ME_INITIALIZE: the clear-state preamble. */
static void v9x_ring_preamble(void)
{
    v9x_eg_3d_preamble(v9x_stage_begin());
    v9x_stage_end();
}

/* Linux 3.9 set_default_state for Cedar, the default state inline. */
static void v9x_ring_default_state(void)
{
    v9x_eg_3d_default_state(v9x_stage_begin());
    v9x_stage_end();
}

static void v9x_ring_draw(DWORD g)
{
    DWORD vs = g + V9X_VS_OFFSET;
    DWORD ps = g + V9X_PS_OFFSET;
    DWORD vb = g + V9X_VB_OFFSET;
    DWORD src = g + V9X_SRC_OFFSET;
    DWORD dst = g + V9X_DST_OFFSET;
    DWORD depth = g + V9X_DEPTH_OFFSET;
    DWORD vb_bytes = v9x_vertex_dwords * 4ul;
    DWORD extent = v9x_scissor_right | (V9X_DRAW_HEIGHT << 16);
    DWORD index;

    v9x_ring_default_state();

    /* set_shaders: NUM_GPRS. The VS uses R0 and R1 (and R2 to modulate),
     * the PS R0 (and R1 to modulate). */
    v9x_ring_put(V9X_PACKET3(V9X_P3_SET_CONTEXT_REG, 3ul));
    v9x_ring_put((V9X_EG_SQ_PGM_START_VS - V9X_SET_CONTEXT_START) >> 2);
    v9x_ring_put(vs >> 8);
    v9x_ring_put(v9x_mode == V9X_MODE_MODULATE ? 3ul : 2ul);
    v9x_ring_put(0ul);
    v9x_ring_put(V9X_PACKET3(V9X_P3_SET_CONTEXT_REG, 4ul));
    v9x_ring_put((V9X_EG_SQ_PGM_START_PS - V9X_SET_CONTEXT_START) >> 2);
    v9x_ring_put(ps >> 8);
    v9x_ring_put(v9x_mode == V9X_MODE_MODULATE ? 2ul : 1ul);
    v9x_ring_put(0ul);
    v9x_ring_put(2ul);
    v9x_surface_sync(V9X_SYNC_SH_ACTION, 512ul, vs);
    if (v9x_mode == V9X_MODE_MODULATE) {
        v9x_set_context(V9X_EG_SPI_VS_OUT_ID_0, V9X_SPI_SEMANTIC_1_IN_ID_0);
        v9x_set_context(V9X_EG_SPI_PS_INPUT_CNTL_1, V9X_SPI_PS_INPUT_1_VALUE);
        v9x_set_context(V9X_EG_SPI_VS_OUT_CONFIG, V9X_SPI_VS_EXPORT_COUNT_2);
        v9x_set_context(V9X_EG_SPI_PS_IN_CONTROL_0, V9X_SPI_PS_IN_CONTROL_2);
    }

    /* set_tex_resource: FMT_8_8_8_8, 64 wide, pitch 64, 8 high, linear
     * aligned, swizzle XYZW, a valid texture. The blit and modulate
     * sample it. */
    if (v9x_mode == V9X_MODE_BLIT || v9x_mode == V9X_MODE_MODULATE) {
        v9x_surface_sync(V9X_SYNC_TC_ACTION, V9X_DRAW_BYTES, src);
        v9x_ring_put(V9X_PACKET3(V9X_P3_SET_RESOURCE, 8ul));
        v9x_ring_put(0ul);
        v9x_ring_put(V9X_SQ_TEX_DIM_2D |
                     (((V9X_DRAW_WIDTH >> 3) - 1ul) << 6) |
                     ((V9X_DRAW_WIDTH - 1ul) << 18));
        v9x_ring_put((V9X_DRAW_HEIGHT - 1ul) | (v9x_tex_mode << 28));
        v9x_ring_put(src >> 8);
        v9x_ring_put(src >> 8);
        v9x_ring_put((0ul << 16) | (1ul << 19) | (2ul << 22) | (3ul << 25));
        v9x_ring_put(0ul);
        v9x_ring_put(0ul);
        v9x_ring_put(V9X_FORMAT_8_8_8_8 | (V9X_SQ_TEX_VALID_TEXTURE << 30));
    }

    /* The depth buffer: Z16, 1D tiled (the DB has no linear mode), no
     * stencil, so the stencil bases repeat the Z base as Mesa's r600
     * leaves them. PITCH_TILE_MAX and HEIGHT_TILE_MAX count 8-pixel
     * tiles less one; SLICE_TILE_MAX counts 64-pixel tiles less one. The
     * default state leaves HiZ forced off (DB_RENDER_OVERRIDE 2Ah) and
     * compression disabled (DB_RENDER_CONTROL 60h), so no HTILE is
     * needed. */
    if (v9x_mode == V9X_MODE_DEPTH) {
        v9x_ring_put(V9X_PACKET3(V9X_P3_SET_CONTEXT_REG, 8ul));
        v9x_ring_put((V9X_EG_DB_Z_INFO - V9X_SET_CONTEXT_START) >> 2);
        v9x_ring_put(V9X_DB_FORMAT_Z_16 | (V9X_ARRAY_1D_TILED_THIN1 << 4));
        v9x_ring_put(0ul);
        v9x_ring_put(depth >> 8);
        v9x_ring_put(depth >> 8);
        v9x_ring_put(depth >> 8);
        v9x_ring_put(depth >> 8);
        v9x_ring_put(((V9X_DRAW_WIDTH / 8ul) - 1ul) |
                     (((V9X_DRAW_HEIGHT / 8ul) - 1ul) << 11));
        v9x_ring_put(((V9X_DRAW_WIDTH * V9X_DRAW_HEIGHT) / 64ul) - 1ul);
        v9x_ring_put(V9X_PACKET3(V9X_P3_SET_CONTEXT_REG, 1ul));
        v9x_ring_put((V9X_EG_DB_DEPTH_CONTROL - V9X_SET_CONTEXT_START) >> 2);
        v9x_ring_put(V9X_DB_Z_ENABLE | V9X_DB_Z_WRITE_ENABLE |
                     V9X_DB_ZFUNC_LESS);
    }

    /* set_render_target: COLOR_8_8_8_8, export norm, linear aligned. */
    v9x_ring_put(V9X_PACKET3(V9X_P3_SET_CONTEXT_REG, 15ul));
    v9x_ring_put((V9X_EG_CB_COLOR0_BASE - V9X_SET_CONTEXT_START) >> 2);
    v9x_ring_put(dst >> 8);
    v9x_ring_put((V9X_DRAW_WIDTH / 8ul) - 1ul);
    v9x_ring_put(((V9X_DRAW_WIDTH * V9X_DRAW_HEIGHT) / 64ul) - 1ul);
    v9x_ring_put(0ul);
    v9x_ring_put((V9X_FORMAT_8_8_8_8 << 2) | (1ul << 24) |
                 (v9x_cb_mode << 8));
    v9x_ring_put(v9x_cb_attrib);
    v9x_ring_put((V9X_DRAW_WIDTH - 1ul) | ((V9X_DRAW_HEIGHT - 1ul) << 16));
    for (index = 0ul; index < 8ul; ++index) {
        v9x_ring_put(0ul);
    }

    /* set_scissors(0, 0, 64, 8) */
    v9x_ring_put(V9X_PACKET3(V9X_P3_SET_CONTEXT_REG, 2ul));
    v9x_ring_put((V9X_EG_PA_SC_SCREEN_SCISSOR - V9X_SET_CONTEXT_START) >> 2);
    v9x_ring_put(0ul);
    v9x_ring_put(extent);
    v9x_ring_put(V9X_PACKET3(V9X_P3_SET_CONTEXT_REG, 2ul));
    v9x_ring_put((V9X_EG_PA_SC_GENERIC_SCISSOR - V9X_SET_CONTEXT_START) >> 2);
    v9x_ring_put(0x80000000ul);
    v9x_ring_put(extent);
    v9x_ring_put(V9X_PACKET3(V9X_P3_SET_CONTEXT_REG, 2ul));
    v9x_ring_put((V9X_EG_PA_SC_WINDOW_SCISSOR - V9X_SET_CONTEXT_START) >> 2);
    v9x_ring_put(0x80000000ul);
    v9x_ring_put(extent);

    /* set_vtx_resource: the mode's stride and size, swizzle XYZW, a valid
     * buffer; Cedar syncs the texture cache, having no vertex cache. */
    v9x_ring_put(V9X_PACKET3(V9X_P3_SET_RESOURCE, 8ul));
    v9x_ring_put(0x580ul);
    v9x_ring_put(vb);
    v9x_ring_put(vb_bytes - 1ul);
    v9x_ring_put(v9x_vertex_stride << 8);
    v9x_ring_put((0ul << 3) | (1ul << 6) | (2ul << 9) | (3ul << 12));
    v9x_ring_put(0ul);
    v9x_ring_put(0ul);
    v9x_ring_put(0ul);
    v9x_ring_put(V9X_SQ_TEX_VALID_BUFFER << 30);
    v9x_surface_sync(V9X_SYNC_TC_ACTION, vb_bytes, vb);

    /* draw_auto: the blit's RECTLIST of three vertices, or a TRILIST.
     * Primitives of one draw reach the DB in order, so the depth test's
     * three rectangles go in one. */
    v9x_set_config(V9X_EG_VGT_PRIMITIVE_TYPE,
                   v9x_mode == V9X_MODE_BLIT ? V9X_DI_PT_RECTLIST :
                   V9X_DI_PT_TRILIST);
    v9x_ring_put(V9X_PACKET3(V9X_P3_INDEX_TYPE, 0ul));
    v9x_ring_put(V9X_DI_INDEX_SIZE_16_BIT);
    v9x_ring_put(V9X_PACKET3(V9X_P3_NUM_INSTANCES, 0ul));
    v9x_ring_put(1ul);
    v9x_ring_put(V9X_PACKET3(V9X_P3_DRAW_INDEX_AUTO, 1ul));
    v9x_ring_put(v9x_vertex_count);
    v9x_ring_put(V9X_DI_SRC_SEL_AUTO_INDEX);

    /* The colour and depth buffers flushed to memory, then the mark. */
    v9x_surface_sync(V9X_SYNC_CB_ACTION | V9X_SYNC_CB0_DEST_BASE,
                     V9X_DRAW_BYTES, dst);
    if (v9x_mode == V9X_MODE_DEPTH) {
        v9x_surface_sync(V9X_SYNC_DB_ACTION | V9X_SYNC_DB_DEST_BASE,
                         V9X_DEPTH_DWORDS * 4ul, depth);
    }
    v9x_set_config(V9X_EG_SCRATCH_REG0, V9X_SCRATCH_DRAWN);
}

/* The Direct3D draws' stream, all of it eg_3d.c's: the default state,
 * the batch's state, the draw, the caches flushed, then the mark. */
static const struct v9x_eg_3d_state v9x_zero_state;

/* The model's byte offset of level `level` from the chain's base. */
static DWORD v9x_mip_level_offset(DWORD level)
{
    DWORD offset = 0ul;
    DWORD index;
    DWORD edge;

    for (index = 0ul; index < level; ++index) {
        edge = V9X_MIP_EDGE >> index;
        offset += V9X_MIP_PITCH_TEXELS * 4ul * (edge != 0ul ? edge : 1ul);
    }
    return offset;
}

/*
 * The chain's resource and sampler, after the builder's (later packets
 * win): linear aligned, levels 0 to 5, MIP_ADDRESS at level 1; point
 * filters, point mip filtering, the LOD clamped to v9x_mip_level (4.8
 * fixed point: with 4.6, LOD 1 read level 0 and LOD 2 to 5 level 1, boot
 * 23 - a quarter of the asked LOD, rounded).
 */
#define V9X_MIP_FILTER_POINT 0x00008000ul /* MIP_FILTER(1) << 15 */

static void v9x_mip_resource(struct v9x_eg_pm4 *stream, DWORD g)
{
    DWORD base = g + V9X_MIP_OFFSET;
    DWORD lod = v9x_mip_level * 256ul;

    v9x_eg_pm4_put(stream, 0xc0086d00ul);
    v9x_eg_pm4_put(stream, 0ul);
    v9x_eg_pm4_put(stream, V9X_SQ_TEX_DIM_2D |
                   (((V9X_MIP_PITCH_TEXELS >> 3) - 1ul) << 6) |
                   ((V9X_MIP_EDGE - 1ul) << 18));
    v9x_eg_pm4_put(stream, (V9X_MIP_EDGE - 1ul) |
                   (V9X_ARRAY_LINEAR_ALIGNED << 28));
    v9x_eg_pm4_put(stream, base >> 8);
    v9x_eg_pm4_put(stream, (base + v9x_mip_level_offset(1ul)) >> 8);
    v9x_eg_pm4_put(stream, (0ul << 16) | (1ul << 19) | (2ul << 22) |
                           (3ul << 25));
    v9x_eg_pm4_put(stream, V9X_MIP_LEVELS - 1ul);
    v9x_eg_pm4_put(stream, 0ul);
    v9x_eg_pm4_put(stream, V9X_FORMAT_8_8_8_8 |
                           (V9X_SQ_TEX_VALID_TEXTURE << 30));
    v9x_eg_pm4_put(stream, 0xc0036e00ul);
    v9x_eg_pm4_put(stream, 0ul);
    v9x_eg_pm4_put(stream, 0x12ul | V9X_MIP_FILTER_POINT);
    v9x_eg_pm4_put(stream, lod | (lod << 12));
    v9x_eg_pm4_put(stream, 0ul);
}

static DWORD v9x_d3d_textured(void)
{
    return v9x_mode == V9X_MODE_D3D_TEX || v9x_mode == V9X_MODE_D3D_PERSP ||
           v9x_mode == V9X_MODE_D3D_1555 || v9x_mode == V9X_MODE_D3D_MIP ||
           v9x_mode == V9X_MODE_D3D_FOG_TEX;
}

static void v9x_ring_draw_d3d(DWORD g)
{
    struct v9x_eg_3d_state state;
    struct v9x_eg_pm4 *stream = v9x_stage_begin();
    DWORD textured = v9x_d3d_textured();
    DWORD lit = v9x_mode == V9X_MODE_D3D_FOG ||
                v9x_mode == V9X_MODE_D3D_FOG_TEX;
    DWORD batch;

    state = v9x_zero_state;
    state.target_gpu = g + V9X_DST_OFFSET;
    state.target_pitch = V9X_DRAW_WIDTH;
    state.target_height = V9X_DRAW_HEIGHT;
    state.target_format = v9x_mode == V9X_MODE_D3D_565 ?
                          V9X_EG_COLOR_5_6_5 : V9X_EG_COLOR_8_8_8_8;
    state.scissor_right = V9X_DRAW_WIDTH;
    state.scissor_bottom = V9X_DRAW_HEIGHT;
    /* The checks read every byte, alpha included. */
    state.write_alpha = 1ul;
    if (v9x_mode == V9X_MODE_D3D_Z) {
        state.has_depth = 1ul;
        state.depth_gpu = g + V9X_DEPTH_OFFSET;
        state.depth_pitch = V9X_DRAW_WIDTH;
        state.depth_height = V9X_DRAW_HEIGHT;
        state.depth_control = v9x_eg_3d_depth_control(1ul, 1ul,
                                                      V9X_EG_FUNC_LESS);
    }
    state.blend_control = v9x_eg_3d_blend(0ul, 0ul, 0ul);
    state.alpha_test_control = v9x_eg_3d_alpha_test(0ul, V9X_EG_FUNC_ALWAYS);
    state.perspective = 1ul;
    state.pixel_centre_half = v9x_centre_half;
    state.vs_gpu = g + V9X_VS_OFFSET;
    state.vs_gprs = textured ? 3ul : 2ul;
    state.ps_gpu = g + V9X_PS_OFFSET;
    state.ps_gprs = textured ? 2ul : 1ul;
    state.interpolants = textured ? 2ul : 1ul;
    if (lit) {
        state.vs_gprs = 4ul;
        state.ps_gprs = 3ul;
        state.interpolants = V9X_EG_D3D_INTERPOLANTS(textured);
    }
    if (textured) {
        state.has_texture = 1ul;
        state.texture_levels = 1ul;
        state.texture_gpu = g + V9X_SRC_OFFSET;
        state.texture_width = V9X_DRAW_WIDTH;
        state.texture_height = V9X_DRAW_HEIGHT;
        state.texture_pitch = V9X_DRAW_WIDTH;
        state.texture_format = v9x_mode == V9X_MODE_D3D_1555 ?
                               V9X_EG_COLOR_1_5_5_5 : V9X_EG_COLOR_8_8_8_8;
        if (v9x_mode == V9X_MODE_D3D_MIP) {
            state.texture_gpu = g + V9X_MIP_OFFSET;
            state.texture_width = V9X_MIP_EDGE;
            state.texture_height = V9X_MIP_EDGE;
            state.texture_pitch = V9X_MIP_PITCH_TEXELS;
        }
        if (v9x_mode == V9X_MODE_D3D_1555) {
            state.texture_width = v9x_tex_width;
            state.texture_pitch = v9x_tex_pitch;
            if (v9x_tex_8888) {
                state.texture_format = V9X_EG_COLOR_8_8_8_8;
            }
        }
        state.sampler_word0 = v9x_eg_3d_sampler(
            0ul, 0ul, V9X_EG_TEX_CLAMP_LAST_TEXEL,
            V9X_EG_TEX_CLAMP_LAST_TEXEL);
    }
    state.vertex_gpu = g + V9X_VB_OFFSET;
    state.vertex_stride = V9X_EG_D3D_STRIDE;
    state.vertex_bytes = v9x_vertex_dwords * 4ul;
    /* /batches: the whole batch more than once, back to back, as the HAL
     * sends one batch after another without waiting. */
    for (batch = 0ul; batch < v9x_batches; ++batch) {
        v9x_eg_3d_default_state(stream);
        if (v9x_eg_3d_build_state(&state, stream) != V9X_STATUS_OK) {
            stream->overflow = 1ul;
        }
        if (v9x_mode == V9X_MODE_D3D_MIP) {
            v9x_mip_resource(stream, g);
        }
        /* /vte: overrides PA_CL_VTE_CNTL after the builder has set it. */
        if (v9x_vte_override != 0xfffffffful) {
            v9x_eg_pm4_set_context1(stream, V9X_EG_PA_CL_VTE_CNTL,
                                    v9x_vte_override);
        }
        v9x_eg_3d_draw(stream, v9x_vertex_count);
        v9x_eg_3d_flush(stream);
    }
    v9x_eg_3d_fence(stream, V9X_SCRATCH_DRAWN);
    v9x_stage_end();
}

/* evergreen_gpu_init's writes for Cedar (eg_3d.c's table), from call 1's
 * reads where Linux reads first; GB_ADDR_CONFIG as /gbaddr: gives it. */
static void v9x_gpu_init(void)
{
    const struct v9x_eg_reg_update *update;
    DWORD index;
    DWORD value;

    for (index = 0ul; index < V9X_EG_CEDAR_GPU_INIT_COUNT; ++index) {
        update = &v9x_eg_cedar_gpu_init[index];
        value = update->set;
        if (update->keep != 0ul) {
            value |= v9x_saved_value(update->offset) & update->keep;
        }
        if (update->offset == V9X_EG_GB_ADDR_CONFIG) {
            value = v9x_gb_addr_config;
        }
        v9x_write(update->offset, value);
    }
}

static void v9x_vertex(DWORD x, DWORD y, DWORD z, DWORD r, DWORD gr,
                       DWORD b)
{
    DWORD *at = v9x_vertices + v9x_vertex_dwords;

    if (v9x_vertex_dwords + V9X_COLOUR_VERTEX_DWORDS > V9X_VERTEX_DWORDS_MAX) {
        return;
    }
    at[0] = x;
    at[1] = y;
    at[2] = z;
    at[3] = V9X_FLOAT_1;
    at[4] = r;
    at[5] = gr;
    at[6] = b;
    at[7] = V9X_FLOAT_1;
    v9x_vertex_dwords += V9X_COLOUR_VERTEX_DWORDS;
    ++v9x_vertex_count;
}

/* A full-height rectangle from x0 to x1 at depth z as two triangles, red
 * r0 at its left edge and r1 at its right. */
static void v9x_rect(DWORD x0, DWORD x1, DWORD z, DWORD r0, DWORD r1,
                     DWORD gr, DWORD b)
{
    v9x_vertex(x0, V9X_FLOAT_0, z, r0, gr, b);
    v9x_vertex(x1, V9X_FLOAT_0, z, r1, gr, b);
    v9x_vertex(x0, V9X_FLOAT_8, z, r0, gr, b);
    v9x_vertex(x1, V9X_FLOAT_0, z, r1, gr, b);
    v9x_vertex(x1, V9X_FLOAT_8, z, r1, gr, b);
    v9x_vertex(x0, V9X_FLOAT_8, z, r0, gr, b);
}

/* A /modulate vertex at (x, y): red r, green 1, blue 0.5, alpha 1, and
 * the texel coordinate (x, y), unnormalised as the blit's. Each channel
 * differs so a swapped one shows. */
static void v9x_modulate_vertex(DWORD x, DWORD y, DWORD r)
{
    DWORD *at = v9x_vertices + v9x_vertex_dwords;

    if (v9x_vertex_dwords + V9X_MODULATE_VERTEX_DWORDS >
        V9X_VERTEX_DWORDS_MAX) {
        return;
    }
    at[0] = x;
    at[1] = y;
    at[2] = V9X_FLOAT_0_5;
    at[3] = V9X_FLOAT_1;
    at[4] = r;
    at[5] = V9X_FLOAT_1;
    at[6] = V9X_FLOAT_0_5;
    at[7] = V9X_FLOAT_1;
    at[8] = x;
    at[9] = y;
    v9x_vertex_dwords += V9X_MODULATE_VERTEX_DWORDS;
    ++v9x_vertex_count;
}

/* A Direct3D TL vertex as the render core hands it over (V9X_R3D_VERTEX):
 * sx, sy, sz, rhw, the D3DCOLOR, specular, tu, tv. */
static void v9x_d3d_vertex(DWORD x, DWORD y, DWORD z, DWORD rhw,
                           DWORD colour, DWORD u, DWORD v)
{
    DWORD *at = v9x_vertices + v9x_vertex_dwords;

    if (v9x_vertex_dwords + V9X_D3D_VERTEX_DWORDS > V9X_VERTEX_DWORDS_MAX) {
        return;
    }
    at[0] = x;
    at[1] = y;
    at[2] = z;
    at[3] = rhw;
    at[4] = colour;
    at[5] = v9x_specular;
    at[6] = u;
    at[7] = v;
    v9x_vertex_dwords += V9X_D3D_VERTEX_DWORDS;
    ++v9x_vertex_count;
}

/* A full-height rectangle from x0 to x1 as two triangles: depth z, one
 * colour, rhw and u at each edge, v 0 at the top and 1 at the bottom. */
static void v9x_d3d_rect(DWORD x0, DWORD x1, DWORD z, DWORD colour,
                         DWORD rhw0, DWORD rhw1, DWORD u0, DWORD u1)
{
    v9x_d3d_vertex(x0, V9X_FLOAT_0, z, rhw0, colour, u0, V9X_FLOAT_0);
    v9x_d3d_vertex(x1, V9X_FLOAT_0, z, rhw1, colour, u1, V9X_FLOAT_0);
    v9x_d3d_vertex(x0, V9X_FLOAT_8, z, rhw0, colour, u0, V9X_FLOAT_1);
    v9x_d3d_vertex(x1, V9X_FLOAT_0, z, rhw1, colour, u1, V9X_FLOAT_0);
    v9x_d3d_vertex(x1, V9X_FLOAT_8, z, rhw1, colour, u1, V9X_FLOAT_1);
    v9x_d3d_vertex(x0, V9X_FLOAT_8, z, rhw0, colour, u0, V9X_FLOAT_1);
}

/* The Direct3D draws: the HAL's shaders, vertices in the render core's
 * layout. */
static void v9x_build_d3d(void)
{
    DWORD textured = v9x_d3d_textured();
    DWORD colour;

    v9x_vs_dwords = v9x_eg_shader_d3d_vs(textured, v9x_vs);
    v9x_ps_dwords = v9x_eg_shader_d3d_ps(textured ? V9X_EG_D3D_OP_MODULATE :
                                         V9X_EG_D3D_OP_NONE, v9x_ps);
    v9x_vertex_stride = V9X_EG_D3D_STRIDE;
    v9x_specular = 0ul;
    if (v9x_mode == V9X_MODE_D3D_FOG || v9x_mode == V9X_MODE_D3D_FOG_TEX) {
        v9x_vs_dwords = v9x_eg_shader_d3d_vs_lit(textured, v9x_vs);
        v9x_ps_dwords = v9x_eg_shader_d3d_ps_lit(
            textured ? V9X_EG_D3D_OP_MODULATE : V9X_EG_D3D_OP_NONE, 1ul, 1ul,
            V9X_FOG_COLOUR, v9x_ps);
        colour = textured ? V9X_D3D_WHITE : V9X_D3D_COLOUR_VALUE;
        v9x_specular = V9X_FOG_SPECULAR_0;
        v9x_d3d_rect(V9X_FLOAT_0, V9X_FLOAT_16, V9X_FLOAT_0_5, colour,
                     V9X_FLOAT_1, V9X_FLOAT_1, V9X_FLOAT_0, V9X_FLOAT_0_25);
        v9x_specular = V9X_FOG_SPECULAR_1;
        v9x_d3d_rect(V9X_FLOAT_16, V9X_FLOAT_32, V9X_FLOAT_0_5, colour,
                     V9X_FLOAT_1, V9X_FLOAT_1, V9X_FLOAT_0_25, V9X_FLOAT_0_5);
        v9x_specular = V9X_FOG_SPECULAR_2;
        v9x_d3d_rect(V9X_FLOAT_32, V9X_FLOAT_64, V9X_FLOAT_0_5, colour,
                     V9X_FLOAT_1, V9X_FLOAT_1, V9X_FLOAT_0_5, V9X_FLOAT_1);
        v9x_specular = 0ul;
        return;
    }
    if (v9x_mode == V9X_MODE_D3D_TEX || v9x_mode == V9X_MODE_D3D_1555 ||
        v9x_mode == V9X_MODE_D3D_MIP) {
        v9x_d3d_rect(V9X_FLOAT_0, V9X_FLOAT_64, V9X_FLOAT_0_5,
                     V9X_D3D_WHITE, V9X_FLOAT_1, V9X_FLOAT_1, V9X_FLOAT_0,
                     V9X_FLOAT_1);
        return;
    }
    if (v9x_mode == V9X_MODE_D3D_PERSP) {
        v9x_d3d_rect(V9X_FLOAT_0, V9X_FLOAT_64, V9X_FLOAT_0_5,
                     V9X_D3D_WHITE, V9X_FLOAT_1, V9X_FLOAT_0_25, V9X_FLOAT_0,
                     V9X_FLOAT_1);
        return;
    }
    if (v9x_mode == V9X_MODE_D3D_COLOUR) {
        v9x_d3d_rect(V9X_FLOAT_0, V9X_FLOAT_64, V9X_FLOAT_0_5,
                     V9X_D3D_COLOUR_VALUE, V9X_FLOAT_1, V9X_FLOAT_1,
                     V9X_FLOAT_0, V9X_FLOAT_0);
        return;
    }
    if (v9x_mode == V9X_MODE_D3D_565) {
        v9x_d3d_rect(V9X_FLOAT_0, V9X_FLOAT_64, V9X_FLOAT_0_5,
                     V9X_D3D_565_COLOUR, V9X_FLOAT_1, V9X_FLOAT_1,
                     V9X_FLOAT_0, V9X_FLOAT_0);
        return;
    }
    v9x_d3d_rect(V9X_FLOAT_0, V9X_FLOAT_64, V9X_FLOAT_0_5, V9X_D3D_RED,
                 V9X_FLOAT_1, V9X_FLOAT_1, V9X_FLOAT_0, V9X_FLOAT_0);
    v9x_d3d_rect(V9X_FLOAT_0, V9X_FLOAT_32, V9X_FLOAT_0_75, V9X_D3D_GREEN,
                 V9X_FLOAT_1, V9X_FLOAT_1, V9X_FLOAT_0, V9X_FLOAT_0);
    v9x_d3d_rect(V9X_FLOAT_32, V9X_FLOAT_64, V9X_FLOAT_0_25, V9X_D3D_BLUE,
                 V9X_FLOAT_1, V9X_FLOAT_1, V9X_FLOAT_0, V9X_FLOAT_0);
}

/* The mode's shaders and vertices. */
static void v9x_build_draw(void)
{
    v9x_vertex_dwords = 0ul;
    v9x_vertex_count = 0ul;
    if (v9x_mode >= V9X_MODE_D3D_FIRST) {
        v9x_build_d3d();
        return;
    }
    if (v9x_mode == V9X_MODE_BLIT) {
        v9x_vs_dwords = v9x_eg_shader_blit_vs(v9x_vs);
        v9x_ps_dwords = v9x_eg_shader_blit_ps(v9x_ps);
        /* r600_kms_blit_copy's vertices: (dst x, dst y, src x, src y). */
        v9x_vertices[0] = 0ul;
        v9x_vertices[1] = 0ul;
        v9x_vertices[2] = 0ul;
        v9x_vertices[3] = 0ul;
        v9x_vertices[4] = 0ul;
        v9x_vertices[5] = V9X_FLOAT_8;
        v9x_vertices[6] = 0ul;
        v9x_vertices[7] = V9X_FLOAT_8;
        v9x_vertices[8] = V9X_FLOAT_64;
        v9x_vertices[9] = V9X_FLOAT_8;
        v9x_vertices[10] = V9X_FLOAT_64;
        v9x_vertices[11] = V9X_FLOAT_8;
        v9x_vertex_dwords = 12ul;
        v9x_vertex_count = 3ul;
        v9x_vertex_stride = V9X_BLIT_STRIDE;
        return;
    }

    if (v9x_mode == V9X_MODE_MODULATE) {
        v9x_vs_dwords = v9x_eg_shader_modulate_vs(v9x_vs);
        v9x_ps_dwords = v9x_eg_shader_modulate_ps(v9x_ps);
        v9x_vertex_stride = V9X_EG_MODULATE_STRIDE;
        v9x_modulate_vertex(V9X_FLOAT_0, V9X_FLOAT_0, V9X_FLOAT_0);
        v9x_modulate_vertex(V9X_FLOAT_64, V9X_FLOAT_0, V9X_FLOAT_1);
        v9x_modulate_vertex(V9X_FLOAT_0, V9X_FLOAT_8, V9X_FLOAT_0);
        v9x_modulate_vertex(V9X_FLOAT_64, V9X_FLOAT_0, V9X_FLOAT_1);
        v9x_modulate_vertex(V9X_FLOAT_64, V9X_FLOAT_8, V9X_FLOAT_1);
        v9x_modulate_vertex(V9X_FLOAT_0, V9X_FLOAT_8, V9X_FLOAT_0);
        return;
    }

    v9x_vs_dwords = v9x_eg_shader_gouraud_vs(v9x_vs);
    v9x_ps_dwords = v9x_eg_shader_gouraud_ps(v9x_ps);
    v9x_vertex_stride = V9X_COLOUR_STRIDE;
    if (v9x_mode == V9X_MODE_GOURAUD) {
        v9x_rect(V9X_FLOAT_0, V9X_FLOAT_64, V9X_FLOAT_0_5, V9X_FLOAT_0,
                 V9X_FLOAT_1, V9X_FLOAT_0_5, V9X_FLOAT_0_25);
        return;
    }
    v9x_rect(V9X_FLOAT_0, V9X_FLOAT_64, V9X_FLOAT_0_5, V9X_FLOAT_1,
             V9X_FLOAT_1, V9X_FLOAT_0, V9X_FLOAT_0);
    v9x_rect(V9X_FLOAT_0, V9X_FLOAT_32, V9X_FLOAT_0_75, V9X_FLOAT_0,
             V9X_FLOAT_0, V9X_FLOAT_1, V9X_FLOAT_0);
    v9x_rect(V9X_FLOAT_32, V9X_FLOAT_64, V9X_FLOAT_0_25, V9X_FLOAT_0,
             V9X_FLOAT_0, V9X_FLOAT_0, V9X_FLOAT_1);
}

static void v9x_build_run(DWORD g)
{
    DWORD index;
    DWORD data;

    v9x_build_draw();

    /* The ring: four submissions, each closed to 16 dwords. */
    v9x_ring_count = 0ul;
    v9x_eg_3d_me_initialize(v9x_stage_begin());
    v9x_stage_end();
    v9x_wptr_init = v9x_ring_close();
    v9x_ring_preamble();
    v9x_wptr_preamble = v9x_ring_close();
    v9x_set_config(V9X_EG_SCRATCH_REG0, V9X_SCRATCH_TEST);
    v9x_wptr_test = v9x_ring_close();
    if (v9x_mode >= V9X_MODE_D3D_FIRST) {
        v9x_ring_draw_d3d(g);
    } else {
        v9x_ring_draw(g);
    }
    v9x_wptr_draw = v9x_ring_close();

    /* The data area after the microcode: ring, VS, PS, vertices. */
    data = V9X_DATA_RING;
    for (index = 0ul; index < v9x_ring_count; ++index) {
        v9x_request.data[data + index] = v9x_ring[index];
    }
    data += v9x_ring_count;
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

    v9x_request.count = 0ul;
    v9x_op(V9X_OP_SAVE, 0ul, 0ul, 0ul);
    v9x_op(V9X_OP_VRAM_FILL, 0ul, V9X_WINDOW_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_PATTERN, V9X_SRC_OFFSET, V9X_DRAW_DWORDS,
           V9X_SRC_SEED);
    v9x_op(V9X_OP_VRAM_FILL, V9X_DST_OFFSET, V9X_DST_GUARD_DWORDS,
           V9X_GUARD_VALUE);
    if (v9x_mode == V9X_MODE_DEPTH || v9x_mode == V9X_MODE_D3D_Z) {
        v9x_op(V9X_OP_VRAM_FILL, V9X_DEPTH_OFFSET, V9X_DEPTH_DWORDS,
               V9X_DEPTH_CLEAR);
    }
    if (v9x_mode == V9X_MODE_D3D_MIP) {
        v9x_op(V9X_OP_VRAM_PATTERN, V9X_MIP_OFFSET, V9X_MIP_DWORDS,
               V9X_MIP_SEED);
    }
    v9x_op(V9X_OP_VRAM_DATA, 0ul, V9X_DATA_RING, v9x_ring_count);
    v9x_op(V9X_OP_VRAM_DATA, V9X_VS_OFFSET, V9X_DATA_RING + v9x_ring_count,
           v9x_vs_dwords);
    v9x_op(V9X_OP_VRAM_DATA, V9X_PS_OFFSET,
           V9X_DATA_RING + v9x_ring_count + v9x_vs_dwords, v9x_ps_dwords);
    v9x_op(V9X_OP_VRAM_DATA, V9X_VB_OFFSET,
           V9X_DATA_RING + v9x_ring_count + v9x_vs_dwords + v9x_ps_dwords,
           v9x_vertex_dwords);

    v9x_at_config_first = v9x_request.count;
    v9x_gpu_init();

    /* r700_cp_stop, evergreen_cp_load_microcode, evergreen_cp_resume. */
    v9x_write(V9X_EG_CP_ME_CNTL, V9X_EG_CP_HALT);
    v9x_write(V9X_EG_SCRATCH_UMSK, 0ul);
    v9x_write(V9X_EG_CP_RB_CNTL, V9X_EG_RB_CNTL_LOADING);
    v9x_write(V9X_EG_CP_PFP_UCODE_ADDR, 0ul);
    v9x_op(V9X_OP_STREAM, V9X_EG_CP_PFP_UCODE_DATA, 0ul, V9X_EG_PFP_DWORDS);
    v9x_write(V9X_EG_CP_PFP_UCODE_ADDR, 0ul);
    v9x_write(V9X_EG_CP_ME_RAM_WADDR, 0ul);
    v9x_op(V9X_OP_STREAM, V9X_EG_CP_ME_RAM_DATA, V9X_EG_PFP_DWORDS,
           V9X_EG_ME_DWORDS);
    v9x_write(V9X_EG_CP_PFP_UCODE_ADDR, 0ul);
    v9x_write(V9X_EG_CP_ME_RAM_WADDR, 0ul);
    v9x_write(V9X_EG_CP_ME_RAM_RADDR, 0ul);
    v9x_write(V9X_EG_GRBM_SOFT_RESET, V9X_EG_GRBM_RESET_CP_BLOCKS);
    v9x_read(V9X_EG_GRBM_SOFT_RESET);
    v9x_op(V9X_OP_DELAY, V9X_RESET_DELAY_READS, 0ul, 0ul);
    v9x_write(V9X_EG_GRBM_SOFT_RESET, 0ul);
    v9x_read(V9X_EG_GRBM_SOFT_RESET);
    v9x_write(V9X_EG_CP_RB_CNTL, V9X_RB_CNTL);
    v9x_write(V9X_EG_CP_SEM_WAIT_TIMER, 0ul);
    v9x_write(V9X_EG_CP_SEM_INCOMPLETE, 0ul);
    v9x_write(V9X_EG_CP_RB_WPTR_DELAY, 0ul);
    v9x_write(V9X_EG_CP_RB_CNTL, V9X_RB_CNTL | V9X_EG_RB_RPTR_WR_ENA);
    v9x_write(V9X_EG_CP_RB_RPTR_WR, 0ul);
    v9x_write(V9X_EG_CP_RB_WPTR, 0ul);
    v9x_write(V9X_EG_CP_RB_RPTR_ADDR, (g + V9X_RPTR_WB) & ~3ul);
    v9x_write(V9X_EG_CP_RB_RPTR_ADDR_HI, 0ul);
    v9x_write(V9X_EG_SCRATCH_ADDR, (g + V9X_SCRATCH_WB) >> 8);
    v9x_write(V9X_EG_SCRATCH_UMSK, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_SHORT_DELAY_READS, 0ul, 0ul);
    v9x_write(V9X_EG_CP_RB_CNTL, V9X_RB_CNTL | V9X_EG_RB_NO_UPDATE);
    v9x_write(V9X_EG_CP_RB_BASE, g >> 8);
    v9x_write(V9X_EG_CP_DEBUG, V9X_EG_CP_DEBUG_VALUE);

    /* evergreen_cp_start. */
    v9x_write(V9X_EG_HDP_MEM_FLUSH, 1ul);
    v9x_write(V9X_EG_CP_RB_WPTR, v9x_wptr_init);
    v9x_write(V9X_EG_CP_ME_CNTL, V9X_EG_CP_RUN);
    v9x_at_wait_init = v9x_op(V9X_OP_WAIT_MMIO, V9X_EG_CP_RB_RPTR,
                              0xfffffffful, v9x_wptr_init);
    v9x_write(V9X_EG_CP_RB_WPTR, v9x_wptr_preamble);
    v9x_at_wait_preamble = v9x_op(V9X_OP_WAIT_MMIO, V9X_EG_CP_RB_RPTR,
                                  0xfffffffful, v9x_wptr_preamble);

    /* r600_ring_test. */
    v9x_write(V9X_EG_SCRATCH_REG0, V9X_SCRATCH_SEED);
    v9x_write(V9X_EG_CP_RB_WPTR, v9x_wptr_test);
    v9x_at_wait_test = v9x_op(V9X_OP_WAIT_MMIO, V9X_EG_SCRATCH_REG0,
                              0xfffffffful, V9X_SCRATCH_TEST);
    v9x_at_test_value = v9x_read(V9X_EG_SCRATCH_REG0);

    /* The draw. */
    v9x_write(V9X_EG_CP_RB_WPTR, v9x_wptr_draw);
    v9x_at_wait_draw = v9x_op(V9X_OP_WAIT_MMIO, V9X_EG_SCRATCH_REG0,
                              0xfffffffful, V9X_SCRATCH_DRAWN);
    v9x_at_wait_idle = v9x_op(V9X_OP_WAIT_MMIO, V9X_EG_GRBM_STATUS,
                              V9X_EG_GUI_ACTIVE, 0ul);
    v9x_at_drawn_value = v9x_read(V9X_EG_SCRATCH_REG0);
    v9x_at_rptr = v9x_read(V9X_EG_CP_RB_RPTR);
    v9x_at_cp_stat = v9x_read(V9X_EG_CP_STAT);
    v9x_at_grbm = v9x_read(V9X_EG_GRBM_STATUS);

    /* Halt, reset the CP blocks, put back. */
    v9x_write(V9X_EG_CP_ME_CNTL, V9X_EG_CP_HALT);
    v9x_op(V9X_OP_DELAY, V9X_SHORT_DELAY_READS, 0ul, 0ul);
    v9x_write(V9X_EG_GRBM_SOFT_RESET, V9X_EG_GRBM_RESET_CP_BLOCKS);
    v9x_read(V9X_EG_GRBM_SOFT_RESET);
    v9x_op(V9X_OP_DELAY, V9X_RESET_DELAY_READS, 0ul, 0ul);
    v9x_write(V9X_EG_GRBM_SOFT_RESET, 0ul);
    v9x_read(V9X_EG_GRBM_SOFT_RESET);
    for (index = 1ul; index < V9X_SAVED_COUNT; ++index) {
        v9x_write(v9x_saved_offsets[index], v9x_saved[index]);
    }
    v9x_write(V9X_EG_CP_ME_CNTL, v9x_saved[0]);
    v9x_at_after_me_cntl = v9x_read(V9X_EG_CP_ME_CNTL);
    v9x_at_after_grbm = v9x_read(V9X_EG_GRBM_STATUS);
    v9x_write(V9X_EG_HDP_MEM_FLUSH, 1ul);
    v9x_at_block = v9x_op(V9X_OP_VRAM_BLOCK, V9X_DST_OFFSET,
                          V9X_READBACK_DWORDS, 0ul);
    v9x_op(V9X_OP_RESTORE, 0ul, 0ul, 0ul);

    v9x_key("Run", "RingDwords", v9x_ring_count);
    v9x_key("Run", "WptrInit", v9x_wptr_init);
    v9x_key("Run", "WptrPreamble", v9x_wptr_preamble);
    v9x_key("Run", "WptrTest", v9x_wptr_test);
    v9x_key("Run", "WptrDraw", v9x_wptr_draw);
    v9x_key("Run", "Mode", v9x_mode);
    v9x_key("Run", "VsDwords", v9x_vs_dwords);
    v9x_key("Run", "PsDwords", v9x_ps_dwords);
    v9x_key("Run", "Vertices", v9x_vertex_count);
    v9x_key("Run", "DataDwords", v9x_request.data_count);
    v9x_key("Run", "Ops", v9x_request.count);
}

static DWORD v9x_pattern(DWORD index)
{
    return V9X_SRC_SEED + index * V9X_PATTERN_STEP;
}

/* Where each destination dword came from: one key per row of 64, each
 * entry the source dword's index in three hex digits, "---" for the guard
 * value and "???" for anything that is neither. */
static void v9x_map(const DWORD *got)
{
    char key[8];
    char line[V9X_DRAW_WIDTH * 4ul + 1ul];
    DWORD row;
    DWORD column;
    DWORD source;
    DWORD value;
    char *at;

    for (row = 0ul; row < V9X_DRAW_HEIGHT; ++row) {
        at = line;
        for (column = 0ul; column < V9X_DRAW_WIDTH; ++column) {
            value = got[row * V9X_DRAW_WIDTH + column];
            if (value == V9X_GUARD_VALUE) {
                lstrcpyA(at, "---");
            } else {
                for (source = 0ul; source < V9X_DRAW_DWORDS; ++source) {
                    if (v9x_pattern(source) == value) {
                        break;
                    }
                }
                if (source == V9X_DRAW_DWORDS) {
                    lstrcpyA(at, "???");
                } else {
                    v9x_hex(at, source, 3);
                }
            }
            at[3] = ' ';
            at += 4;
        }
        at[-1] = '\0';
        lstrcpyA(key, "Row");
        v9x_decimal(key + 3, row);
        v9x_text("Map", key, line);
    }
}

/* The destination against the source pattern, then its guard. */
static void v9x_verify(void)
{
    const DWORD *got = v9x_result.block;
    DWORD bad = 0ul;
    DWORD guard_bad = 0ul;
    DWORD first_bad = 0xfffffffful;
    DWORD index;
    char text[12];

    for (index = 0ul; index < V9X_DRAW_DWORDS; ++index) {
        if (got[index] != v9x_pattern(index)) {
            if (first_bad == 0xfffffffful) {
                first_bad = index;
            }
            ++bad;
        }
    }
    for (index = V9X_DRAW_DWORDS; index < V9X_DST_GUARD_DWORDS; ++index) {
        if (got[index] != V9X_GUARD_VALUE) {
            ++guard_bad;
        }
    }
    v9x_decimal(text, bad);
    v9x_text("Result", "DrawMismatches", text);
    v9x_decimal(text, guard_bad);
    v9x_text("Result", "GuardMismatches", text);
    v9x_text("Result", "Draw", bad == 0ul && guard_bad == 0ul ? "PASS" :
             "FAIL");
    if (first_bad != 0xfffffffful) {
        v9x_key("Result", "FirstBadIndex", first_bad);
        v9x_key("Result", "FirstBadGot", got[first_bad]);
        v9x_key("Result", "FirstBadExpected", v9x_pattern(first_bad));
    }
    for (index = 0ul; index < 8ul; ++index) {
        char key[12];

        lstrcpyA(key, "Dst");
        v9x_decimal(key + 3, index);
        v9x_key("Result", key, got[index]);
    }
    v9x_key("Result", "DstLast", got[V9X_DRAW_DWORDS - 1ul]);
    v9x_key("Result", "DstAfter", got[V9X_DRAW_DWORDS]);
    v9x_map(got);
}

/* The pixel the Gouraud, depth or modulate draw should leave at index:
 * bytes R, G, B, A from low to high, as COLOR_8_8_8_8 with SWAP_STD
 * stores them. Red's ramp is sampled at pixel centres, (x + 0.5) / 64,
 * rounded; modulate scales the source texel's bytes by red's ramp, 1,
 * 0.5 and 1. */
/* /d3dfog's pixel: per channel B, G, R, fog + f * (min(255, colour +
 * specular) - fog), rounded; alpha the colour's. */
static DWORD v9x_fog_pixel(DWORD colour, DWORD specular)
{
    DWORD f = specular >> 24;
    DWORD result = colour & 0xff000000ul;
    DWORD shift;
    DWORD c;
    DWORD fog;
    long mixed;

    for (shift = 0ul; shift < 24ul; shift += 8ul) {
        c = ((colour >> shift) & 0xfful) + ((specular >> shift) & 0xfful);
        if (c > 255ul) {
            c = 255ul;
        }
        fog = (V9X_FOG_COLOUR >> shift) & 0xfful;
        mixed = (long)fog * 255l + (long)f * ((long)c - (long)fog);
        result |= (DWORD)((mixed + 127l) / 255l) << shift;
    }
    return result;
}

static DWORD v9x_expected_pixel(DWORD index)
{
    DWORD x = index % V9X_DRAW_WIDTH;
    DWORD texel;
    DWORD red;
    DWORD specular;

    if (v9x_mode == V9X_MODE_D3D_FOG || v9x_mode == V9X_MODE_D3D_FOG_TEX) {
        specular = x < 16ul ? V9X_FOG_SPECULAR_0 :
                   x < 32ul ? V9X_FOG_SPECULAR_1 : V9X_FOG_SPECULAR_2;
        return v9x_fog_pixel(v9x_mode == V9X_MODE_D3D_FOG ?
                             V9X_D3D_COLOUR_VALUE : v9x_pattern(index),
                             specular);
    }
    if (v9x_mode == V9X_MODE_D3D_COLOUR) {
        return V9X_D3D_COLOUR_VALUE;
    }
    if (v9x_mode == V9X_MODE_D3D_Z) {
        return x < V9X_DRAW_WIDTH / 2ul ? V9X_D3D_RED : V9X_D3D_BLUE;
    }
    if (v9x_mode == V9X_MODE_DEPTH) {
        return x < V9X_DRAW_WIDTH / 2ul ? V9X_PIXEL_RED : V9X_PIXEL_BLUE;
    }
    if (v9x_mode == V9X_MODE_MODULATE) {
        texel = v9x_pattern(index);
        red = ((texel & 0xfful) * (2ul * x + 1ul) + V9X_DRAW_WIDTH) /
              (2ul * V9X_DRAW_WIDTH);
        return red | (texel & 0xff00ul) |
               ((((texel >> 16) & 0xfful) / 2ul) << 16) |
               (texel & 0xff000000ul);
    }
    red = ((2ul * x + 1ul) * 255ul + V9X_DRAW_WIDTH) / (2ul * V9X_DRAW_WIDTH);
    return red | (128ul << 8) | (64ul << 16) | (255ul << 24);
}

static int v9x_pixel_close(DWORD got, DWORD expected)
{
    DWORD shift;
    DWORD a;
    DWORD b;

    for (shift = 0ul; shift < 32ul; shift += 8ul) {
        a = (got >> shift) & 0xfful;
        b = (expected >> shift) & 0xfful;
        if ((a > b ? a - b : b - a) > V9X_CHANNEL_TOLERANCE) {
            return 0;
        }
    }
    return 1;
}

/* One key per row: Gouraud's red byte in hex for each pixel; depth's
 * colour as a letter (R, G, B, "-" for the guard, "?" otherwise);
 * modulate's "." for a pixel within tolerance, "X" for one outside. */
static void v9x_colour_map(const DWORD *got)
{
    char key[8];
    char line[V9X_DRAW_WIDTH * 3ul + 1ul];
    DWORD row;
    DWORD column;
    DWORD value;
    char *at;

    for (row = 0ul; row < V9X_DRAW_HEIGHT; ++row) {
        at = line;
        for (column = 0ul; column < V9X_DRAW_WIDTH; ++column) {
            value = got[row * V9X_DRAW_WIDTH + column];
            if (v9x_mode == V9X_MODE_GOURAUD) {
                v9x_hex(at, value & 0xfful, 2);
                at[2] = ' ';
                at += 3;
                continue;
            }
            if (v9x_mode == V9X_MODE_MODULATE ||
                v9x_mode >= V9X_MODE_D3D_FIRST) {
                *at++ = v9x_pixel_close(value, v9x_expected_pixel(
                    row * V9X_DRAW_WIDTH + column)) ? '.' : 'X';
                continue;
            }
            if (v9x_pixel_close(value, V9X_PIXEL_RED)) {
                *at = 'R';
            } else if (v9x_pixel_close(value, V9X_PIXEL_GREEN)) {
                *at = 'G';
            } else if (v9x_pixel_close(value, V9X_PIXEL_BLUE)) {
                *at = 'B';
            } else if (value == V9X_GUARD_VALUE) {
                *at = '-';
            } else {
                *at = '?';
            }
            ++at;
        }
        if (v9x_mode == V9X_MODE_GOURAUD) {
            --at;
        }
        *at = '\0';
        lstrcpyA(key, "Row");
        v9x_decimal(key + 3, row);
        v9x_text("Map", key, line);
    }
}

/* The depth buffer's eight 8x8 tiles: each tile lies wholly in one half,
 * so each should hold one value, z 0.5 (8000h) on the left and 0.25
 * (4000h) on the right, within the rounding of a 16-bit unorm. */
static void v9x_depth_report(const DWORD *depth)
{
    char key[12];
    DWORD tile;
    DWORD index;
    DWORD first;
    DWORD expected;
    DWORD z;
    DWORD bad = 0ul;

    for (tile = 0ul; tile < V9X_DRAW_WIDTH / 8ul; ++tile) {
        first = depth[tile * V9X_DEPTH_TILE_DWORDS];
        lstrcpyA(key, "DepthTile");
        v9x_decimal(key + 9, tile);
        v9x_key("Result", key, first);
        expected = tile < V9X_DRAW_WIDTH / 16ul ? 0x8000ul : 0x4000ul;
        for (index = 0ul; index < V9X_DEPTH_TILE_DWORDS; ++index) {
            if (depth[tile * V9X_DEPTH_TILE_DWORDS + index] != first) {
                ++bad;
                break;
            }
        }
        z = first & 0xfffful;
        if ((first >> 16) != z || z + 2ul < expected || z > expected + 2ul) {
            ++bad;
        }
    }
    v9x_text("Result", "DepthBuffer", bad == 0ul ? "PASS" : "FAIL");
}

/* The Gouraud or depth draw against v9x_expected_pixel, then the guard. */
static void v9x_verify_colour(void)
{
    const DWORD *got = v9x_result.block;
    DWORD bad = 0ul;
    DWORD guard_bad = 0ul;
    DWORD first_bad = 0xfffffffful;
    DWORD index;
    char text[12];

    for (index = 0ul; index < V9X_DRAW_DWORDS; ++index) {
        if (!v9x_pixel_close(got[index], v9x_expected_pixel(index))) {
            if (first_bad == 0xfffffffful) {
                first_bad = index;
            }
            ++bad;
        }
    }
    for (index = V9X_DRAW_DWORDS; index < V9X_DST_GUARD_DWORDS; ++index) {
        if (got[index] != V9X_GUARD_VALUE) {
            ++guard_bad;
        }
    }
    v9x_decimal(text, bad);
    v9x_text("Result", "DrawMismatches", text);
    v9x_decimal(text, guard_bad);
    v9x_text("Result", "GuardMismatches", text);
    v9x_text("Result", "Draw", bad == 0ul && guard_bad == 0ul ? "PASS" :
             "FAIL");
    if (first_bad != 0xfffffffful) {
        v9x_key("Result", "FirstBadIndex", first_bad);
        v9x_key("Result", "FirstBadGot", got[first_bad]);
        v9x_key("Result", "FirstBadExpected", v9x_expected_pixel(first_bad));
    }
    v9x_key("Result", "Pixel0", got[0]);
    v9x_key("Result", "Pixel31", got[31]);
    v9x_key("Result", "Pixel32", got[32]);
    v9x_key("Result", "Pixel63", got[63]);
    v9x_colour_map(got);
    if (v9x_mode == V9X_MODE_DEPTH) {
        v9x_depth_report(got + V9X_DST_GUARD_DWORDS);
    }
}

/*
 * /d3dpersp's expected texel, perspective-correct, in integers. Triangle 1
 * is (0,0) (64,0) (0,8), triangle 2 (64,0) (64,8) (0,8); rhw is 1 at x = 0
 * and 0.25 at x = 64, scaled here to 4 and 1; u = x / 64 and v = y / 8 at
 * the corners. With xs = 2x + 1 and ys = 8(2y + 1), the screen
 * barycentrics times 128 are integers (x + 0.5 over 64 is xs / 128, y +
 * 0.5 over 8 is ys / 128), and u and v are their rhw-weighted ratios. The first model (u depending on x alone) was wrong:
 * v varies along a row too when rhw does.
 */
static DWORD v9x_persp_texel(DWORD x, DWORD y)
{
    long xs = (long)(2ul * x + 1ul);
    long ys = 8l * (long)(2ul * y + 1ul);
    long den;
    long u_num;
    long v_num;
    long column;
    long row;

    if (xs + ys < 128l) {
        /* l1 = 128 - xs - ys (rhw 4), l2 = xs (rhw 1, u 1), l3 = ys (rhw
         * 4, v 1). */
        den = 4l * (128l - xs - ys) + xs + 4l * ys;
        u_num = xs;
        v_num = 4l * ys;
    } else {
        /* la = 128 - ys (rhw 1, u 1), lb = ys - 128 + xs (rhw 1, u and v
         * 1), lc = 128 - xs (rhw 4, v 1). */
        den = (128l - ys) + (ys - 128l + xs) + 4l * (128l - xs);
        u_num = (128l - ys) + (ys - 128l + xs);
        v_num = (ys - 128l + xs) + 4l * (128l - xs);
    }
    column = (64l * u_num) / den;
    row = (8l * v_num) / den;
    if (column > 63l) {
        column = 63l;
    }
    if (row > 7l) {
        row = 7l;
    }
    return (DWORD)row * V9X_DRAW_WIDTH + (DWORD)column;
}

/* /d3dpersp: each pixel against v9x_persp_texel's texel; a pixel holding a
 * neighbouring texel instead (the sampler's rounding at an edge) counts
 * against ExactMismatches only. The map shows the texel each pixel took. */
static void v9x_verify_persp(void)
{
    const DWORD *got = v9x_result.block;
    DWORD exact_bad = 0ul;
    DWORD bad = 0ul;
    DWORD guard_bad = 0ul;
    DWORD index;
    DWORD expected;
    DWORD source;
    char text[12];

    for (index = 0ul; index < V9X_DRAW_DWORDS; ++index) {
        expected = v9x_persp_texel(index % V9X_DRAW_WIDTH,
                                   index / V9X_DRAW_WIDTH);
        if (got[index] == v9x_pattern(expected)) {
            continue;
        }
        ++exact_bad;
        for (source = 0ul; source < V9X_DRAW_DWORDS; ++source) {
            if (got[index] == v9x_pattern(source)) {
                break;
            }
        }
        if (source == V9X_DRAW_DWORDS ||
            (source % V9X_DRAW_WIDTH) + 1ul < expected % V9X_DRAW_WIDTH ||
            (source % V9X_DRAW_WIDTH) > expected % V9X_DRAW_WIDTH + 1ul ||
            source / V9X_DRAW_WIDTH + 1ul < expected / V9X_DRAW_WIDTH ||
            source / V9X_DRAW_WIDTH > expected / V9X_DRAW_WIDTH + 1ul) {
            ++bad;
        }
    }
    for (index = V9X_DRAW_DWORDS; index < V9X_DST_GUARD_DWORDS; ++index) {
        if (got[index] != V9X_GUARD_VALUE) {
            ++guard_bad;
        }
    }
    v9x_decimal(text, exact_bad);
    v9x_text("Result", "ExactMismatches", text);
    v9x_decimal(text, bad);
    v9x_text("Result", "DrawMismatches", text);
    v9x_decimal(text, guard_bad);
    v9x_text("Result", "GuardMismatches", text);
    v9x_text("Result", "Draw", bad == 0ul && guard_bad == 0ul ? "PASS" :
             "FAIL");
    v9x_map(got);
}

/* The mip area's dword `index`, as EGCP.VXD's VRAM_PATTERN lays it. */
static DWORD v9x_mip_pattern(DWORD index)
{
    return V9X_MIP_SEED + index * V9X_PATTERN_STEP;
}

/* The mip area's index holding `value`, or V9X_MIP_DWORDS. */
static DWORD v9x_mip_find(DWORD value)
{
    DWORD index;

    for (index = 0ul; index < V9X_MIP_DWORDS; ++index) {
        if (v9x_mip_pattern(index) == value) {
            break;
        }
    }
    return index;
}

/*
 * /d3dmip:K: pixel (x, y) at its centre samples level K's texel
 * ((2x + 1) * w / 128, (2y + 1) * h / 16), w and h level K's edges; the
 * model puts it at level K's offset + row * 256 + column * 4.
 */
static void v9x_verify_mip(void)
{
    const DWORD *got = v9x_result.block;
    DWORD edge = V9X_MIP_EDGE >> v9x_mip_level;
    DWORD bad = 0ul;
    DWORD index;
    DWORD x;
    DWORD y;
    DWORD expected;
    char text[12];

    if (edge == 0ul) {
        edge = 1ul;
    }
    for (index = 0ul; index < V9X_DRAW_DWORDS; ++index) {
        x = ((2ul * (index % V9X_DRAW_WIDTH) + 1ul) * edge) /
            (2ul * V9X_DRAW_WIDTH);
        y = ((2ul * (index / V9X_DRAW_WIDTH) + 1ul) * edge) /
            (2ul * V9X_DRAW_HEIGHT);
        expected = v9x_mip_pattern((v9x_mip_level_offset(v9x_mip_level) +
                                    y * V9X_MIP_PITCH_TEXELS * 4ul +
                                    x * 4ul) / 4ul);
        if (got[index] != expected) {
            ++bad;
        }
    }
    v9x_key("Result", "MipLevel", v9x_mip_level);
    v9x_key("Result", "ModelOffset", v9x_mip_level_offset(v9x_mip_level));
    v9x_decimal(text, bad);
    v9x_text("Result", "DrawMismatches", text);
    v9x_text("Result", "Draw", bad == 0ul ? "PASS" : "FAIL");
    v9x_key("Result", "Pixel0", got[0]);
    v9x_key("Result", "Pixel0Index", v9x_mip_find(got[0]));
    v9x_key("Result", "PixelLastIndex",
            v9x_mip_find(got[V9X_DRAW_DWORDS - 1ul]));
}

/* A 5-bit channel as the CB's 8-bit unorm holds it: v / 31, rounded. */
static DWORD v9x_expand5(DWORD v)
{
    return (v * 255ul + 15ul) / 31ul;
}

/*
 * /d3d1555: pixel (x, y) samples texel (x, y) of a 64-texel-pitch ARGB1555
 * texture over the source pattern, the 16-bit word at byte y * 128 + x * 2.
 * Its fields - blue 4:0, green 9:5, red 14:10, alpha 15 - should arrive in
 * the 8888 target's bytes 0 to 3, each within 1 of v / 31. Mismatches are
 * counted per channel, so a field read from the wrong bits shows which.
 */
static void v9x_verify_1555(void)
{
    static const char *keys[4] = {
        "BlueMismatches", "GreenMismatches", "RedMismatches",
        "AlphaMismatches" };
    const DWORD *got = v9x_result.block;
    DWORD channel_bad[4];
    DWORD bad = 0ul;
    DWORD first_bad = 0xfffffffful;
    DWORD first_word = 0ul;
    DWORD index;
    DWORD channel;
    DWORD byte;
    DWORD texel_bytes;
    DWORD word;
    DWORD expected[4];
    DWORD value;
    DWORD diff;
    int pixel_bad;
    char text[12];

    for (channel = 0ul; channel < 4ul; ++channel) {
        channel_bad[channel] = 0ul;
    }
    for (index = 0ul; index < V9X_DRAW_DWORDS; ++index) {
        /* Pixel centres at + 0.5 sample texel x * width / 64. */
        texel_bytes = v9x_tex_8888 ? 4ul : 2ul;
        byte = (index / V9X_DRAW_WIDTH) * v9x_tex_pitch * texel_bytes +
               ((index % V9X_DRAW_WIDTH) * v9x_tex_width / V9X_DRAW_WIDTH) *
               texel_bytes;
        if (v9x_tex_8888) {
            word = v9x_pattern(byte / 4ul);
            for (channel = 0ul; channel < 4ul; ++channel) {
                expected[channel] = (word >> (channel * 8ul)) & 0xfful;
            }
        } else {
            word = (v9x_pattern(byte / 4ul) >> ((byte & 2ul) * 8ul)) &
                   0xfffful;
            expected[0] = v9x_expand5(word & 31ul);
            expected[1] = v9x_expand5((word >> 5) & 31ul);
            expected[2] = v9x_expand5((word >> 10) & 31ul);
            expected[3] = (word & 0x8000ul) != 0ul ? 255ul : 0ul;
        }
        pixel_bad = 0;
        for (channel = 0ul; channel < 4ul; ++channel) {
            value = (got[index] >> (channel * 8ul)) & 0xfful;
            diff = value > expected[channel] ? value - expected[channel]
                                             : expected[channel] - value;
            if (diff > 1ul) {
                ++channel_bad[channel];
                pixel_bad = 1;
            }
        }
        if (pixel_bad) {
            if (first_bad == 0xfffffffful) {
                first_bad = index;
                first_word = word;
            }
            ++bad;
        }
    }
    v9x_decimal(text, bad);
    v9x_text("Result", "DrawMismatches", text);
    for (channel = 0ul; channel < 4ul; ++channel) {
        v9x_decimal(text, channel_bad[channel]);
        v9x_text("Result", keys[channel], text);
    }
    v9x_text("Result", "Draw", bad == 0ul ? "PASS" : "FAIL");
    if (first_bad != 0xfffffffful) {
        v9x_key("Result", "FirstBadIndex", first_bad);
        v9x_key("Result", "FirstBadWord", first_word);
        v9x_key("Result", "FirstBadGot", got[first_bad]);
    }
}

/* /d3d565: every 16-bit pixel 8408h, two to a dword, the guard after. */
static void v9x_verify_565(void)
{
    const DWORD *got = v9x_result.block;
    DWORD pixel_dwords = V9X_DRAW_DWORDS / 2ul;
    DWORD bad = 0ul;
    DWORD guard_bad = 0ul;
    DWORD index;
    char text[12];

    for (index = 0ul; index < pixel_dwords; ++index) {
        if (got[index] != V9X_D3D_565_PIXELS) {
            ++bad;
        }
    }
    for (index = pixel_dwords; index < V9X_DST_GUARD_DWORDS; ++index) {
        if (got[index] != V9X_GUARD_VALUE) {
            ++guard_bad;
        }
    }
    v9x_decimal(text, bad);
    v9x_text("Result", "DrawMismatches", text);
    v9x_decimal(text, guard_bad);
    v9x_text("Result", "GuardMismatches", text);
    v9x_text("Result", "Draw", bad == 0ul && guard_bad == 0ul ? "PASS" :
             "FAIL");
    v9x_key("Result", "Pixel0", got[0]);
    v9x_key("Result", "PixelLast", got[pixel_dwords - 1ul]);
    v9x_key("Result", "PixelAfter", got[pixel_dwords]);
}

/* The hex value after a switch such as "/cbmode:", or fallback when the
 * switch is absent. */
static DWORD v9x_switch_hex(const char *switch_text, DWORD fallback)
{
    const char *line;
    int length;
    DWORD value;
    char c;

    line = GetCommandLineA();
    length = lstrlenA(switch_text);
    while (*line != '\0') {
        if (CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE, line,
                           length, switch_text, length) == CSTR_EQUAL) {
            value = 0ul;
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
            }
            return value;
        }
        ++line;
    }
    return fallback;
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

static void v9x_finish(const char *result, UINT code)
{
    v9x_text("Report", "Result", result);
    WritePrivateProfileStringA(0, 0, 0, V9X_EG3D_PATH);
    ExitProcess(code);
}

void __stdcall V9xEvergreen3dEntry(void)
{
    HANDLE device;
    DWORD index;
    DWORD fb;
    DWORD g;
    const DWORD *r;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_EG3D_PATH);
    v9x_text("Report", "Tool", "V9XEG3D");
    v9x_text("Report", "Build", V9X_BUILD_ID);

    if (!v9x_load_microcode("PFP.BIN", 0ul, V9X_EG_PFP_DWORDS) ||
        !v9x_load_microcode("ME.BIN", V9X_EG_PFP_DWORDS, V9X_EG_ME_DWORDS)) {
        v9x_finish("MICROCODE-MISSING", 1u);
    }
    v9x_request.data_count = V9X_EG_PFP_DWORDS + V9X_EG_ME_DWORDS;

    device = CreateFileA(V9X_EGCP_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_key("Report", "LoadError", GetLastError());
        v9x_finish("VXD-LOAD-FAILED", 1u);
    }

    /* Call 1: read-only. */
    v9x_request.count = 0ul;
    for (index = 0ul; index < V9X_SAVED_COUNT; ++index) {
        v9x_read(v9x_saved_offsets[index]);
    }
    for (index = 0ul; index < V9X_INFO_COUNT; ++index) {
        v9x_read(v9x_info_offsets[index]);
    }
    if (!v9x_dioc(device)) {
        CloseHandle(device);
        v9x_finish("REFUSED", 2u);
    }
    v9x_key("Report", "Id", v9x_result.id);
    r = v9x_result.op_results;
    for (index = 0ul; index < V9X_SAVED_COUNT; ++index) {
        v9x_saved[index] = r[index];
        v9x_reg_key("Before", v9x_saved_offsets[index], r[index]);
    }
    for (index = 0ul; index < V9X_INFO_COUNT; ++index) {
        v9x_info[index] = r[V9X_SAVED_COUNT + index];
        v9x_reg_key("Before", v9x_info_offsets[index], v9x_info[index]);
    }
    if ((v9x_saved[0] & V9X_EG_CP_HALT) != V9X_EG_CP_HALT) {
        CloseHandle(device);
        v9x_finish("CP-NOT-HALTED", 3u);
    }
    fb = v9x_info[V9X_INFO_COUNT - 1ul] & V9X_EG_FB_START_MASK;
    if (fb > 0xeful) {
        CloseHandle(device);
        v9x_finish("FB-START-UNSUPPORTED", 4u);
    }
    g = (fb << 24) + V9X_SCRATCH_GPU_OFFSET;
    v9x_key("Run", "WindowGpu", g);
    if (v9x_switch_given("/readonly")) {
        CloseHandle(device);
        v9x_finish("READONLY", 0u);
    }
    if (v9x_switch_given("/tiled")) {
        v9x_array_mode = V9X_ARRAY_1D_TILED_THIN1;
    }
    if (v9x_switch_given("/gouraud")) {
        v9x_mode = V9X_MODE_GOURAUD;
    }
    if (v9x_switch_given("/depth")) {
        v9x_mode = V9X_MODE_DEPTH;
    }
    if (v9x_switch_given("/modulate")) {
        v9x_mode = V9X_MODE_MODULATE;
    }
    if (v9x_switch_given("/d3dtex")) {
        v9x_mode = V9X_MODE_D3D_TEX;
    }
    if (v9x_switch_given("/d3dpersp")) {
        v9x_mode = V9X_MODE_D3D_PERSP;
    }
    if (v9x_switch_given("/d3dcolour")) {
        v9x_mode = V9X_MODE_D3D_COLOUR;
    }
    if (v9x_switch_given("/d3dz")) {
        v9x_mode = V9X_MODE_D3D_Z;
    }
    if (v9x_switch_given("/d3d565")) {
        v9x_mode = V9X_MODE_D3D_565;
    }
    if (v9x_switch_given("/d3d1555")) {
        v9x_mode = V9X_MODE_D3D_1555;
    }
    if (v9x_switch_given("/d3dfog")) {
        v9x_mode = V9X_MODE_D3D_FOG;
    }
    if (v9x_switch_given("/d3dfogtex")) {
        v9x_mode = V9X_MODE_D3D_FOG_TEX;
    }
    v9x_tex_width = v9x_switch_hex("/texw:", 64ul);
    v9x_tex_pitch = v9x_switch_hex("/texpitch:", 64ul);
    v9x_tex_8888 = v9x_switch_given("/tex8888");
    if (v9x_switch_given("/d3dmip")) {
        v9x_mode = V9X_MODE_D3D_MIP;
        v9x_mip_level = v9x_switch_hex("/d3dmip:", 0ul);
    }
    v9x_key("Run", "TexWidth", v9x_tex_width);
    v9x_key("Run", "TexPitch", v9x_tex_pitch);
    if (v9x_switch_given("/centre0")) {
        v9x_centre_half = 0ul;
    }
    v9x_key("Run", "CentreHalf", v9x_centre_half);
    v9x_vte_override = v9x_switch_hex("/vte:", 0xfffffffful);
    v9x_key("Run", "VteOverride", v9x_vte_override);
    v9x_batches = v9x_switch_hex("/batches:", 1ul);
    if (v9x_batches == 0ul) {
        v9x_batches = 1ul;
    }
    v9x_key("Run", "Batches", v9x_batches);
    v9x_cb_mode = v9x_switch_hex("/cbmode:", v9x_array_mode);
    v9x_tex_mode = v9x_switch_hex("/texmode:", v9x_array_mode);
    v9x_scissor_right = v9x_switch_hex("/scissor:", V9X_DRAW_WIDTH);
    v9x_gb_addr_config = v9x_switch_hex("/gbaddr:",
                                        V9X_EG_CEDAR_GB_ADDR_CONFIG);
    /* A linear colour buffer needs CB_COLOR0_ATTRIB's NON_DISP_TILING_ORDER
     * (bit 4), as Mesa's r600 evergreen_set_color_surface_common forces it:
     * without it the CB put pixel x of each 8 at byte 4*x0 + 24*x1 and
     * dropped x2 (M-BXMFA8, 2026-10-27). Linux 3.9's tiled blit left it
     * clear. */
    v9x_cb_attrib = v9x_switch_hex(
        "/attrib:", v9x_cb_mode <= V9X_ARRAY_LINEAR_ALIGNED ?
        V9X_CB_NON_DISP_TILING_ORDER : 0ul);
    v9x_key("Run", "ArrayMode", v9x_array_mode);
    v9x_key("Run", "CbMode", v9x_cb_mode);
    v9x_key("Run", "TexMode", v9x_tex_mode);
    v9x_key("Run", "ScissorRight", v9x_scissor_right);
    v9x_key("Run", "GbAddrConfig", v9x_gb_addr_config);
    v9x_key("Run", "CbAttrib", v9x_cb_attrib);

    /* Call 2: the run. */
    v9x_build_run(g);
    if (v9x_ring_count > V9X_RING_MAX ||
        v9x_request.data_count > V9X_EGCP_DATA ||
        v9x_ring_count * 4ul > V9X_RING_BYTES) {
        CloseHandle(device);
        v9x_finish("RING-TOO-LONG", 6u);
    }
    if (!v9x_dioc(device)) {
        CloseHandle(device);
        v9x_key("Report", "Executed", v9x_result.executed);
        v9x_finish("RUN-REFUSED", 5u);
    }
    CloseHandle(device);
    r = v9x_result.op_results;
    v9x_key("During", "WaitInit", r[v9x_at_wait_init]);
    v9x_key("During", "WaitPreamble", r[v9x_at_wait_preamble]);
    v9x_key("During", "WaitTest", r[v9x_at_wait_test]);
    v9x_key("During", "TestValue", r[v9x_at_test_value]);
    v9x_key("During", "WaitDraw", r[v9x_at_wait_draw]);
    v9x_key("During", "WaitIdle", r[v9x_at_wait_idle]);
    v9x_key("During", "DrawnValue", r[v9x_at_drawn_value]);
    v9x_key("During", "CP_RB_RPTR", r[v9x_at_rptr]);
    v9x_key("During", "CP_STAT", r[v9x_at_cp_stat]);
    v9x_key("During", "GRBM_STATUS", r[v9x_at_grbm]);
    v9x_key("After", "CP_ME_CNTL", r[v9x_at_after_me_cntl]);
    v9x_key("After", "GRBM_STATUS", r[v9x_at_after_grbm]);
    (void)v9x_at_config_first;
    (void)v9x_at_block;
    v9x_text("Result", "MeInitialize",
             r[v9x_at_wait_init] != V9X_EGCP_TIMEOUT ? "PASS" : "FAIL");
    v9x_text("Result", "Preamble",
             r[v9x_at_wait_preamble] != V9X_EGCP_TIMEOUT ? "PASS" : "FAIL");
    v9x_text("Result", "RingTest",
             r[v9x_at_test_value] == V9X_SCRATCH_TEST ? "PASS" : "FAIL");
    v9x_text("Result", "DrawFence",
             r[v9x_at_wait_draw] != V9X_EGCP_TIMEOUT ? "PASS" : "FAIL");
    if (v9x_mode == V9X_MODE_BLIT || v9x_mode == V9X_MODE_D3D_TEX) {
        v9x_verify();
    } else if (v9x_mode == V9X_MODE_D3D_PERSP) {
        v9x_verify_persp();
    } else if (v9x_mode == V9X_MODE_D3D_565) {
        v9x_verify_565();
    } else if (v9x_mode == V9X_MODE_D3D_1555) {
        v9x_verify_1555();
    } else if (v9x_mode == V9X_MODE_D3D_MIP) {
        v9x_verify_mip();
    } else {
        v9x_verify_colour();
    }
    v9x_finish("COMPLETE", 0u);
}
