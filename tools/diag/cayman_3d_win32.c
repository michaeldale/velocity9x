/*
 * V9XCM3D.EXE: the first draw on a Cayman-generation 3D engine (Trinity,
 * the HD 7660D, 1002:9901): the GPU configured, the CP started with clear
 * state, and Linux 3.9's textured copy of a 64x8 RGBA image, checked byte
 * for byte.
 *
 * Phase 5 of docs\plans\ati-trinity-hd7660d.md. V9XEG3D is the Evergreen
 * model; this is its blit alone, on Cayman's code path, with 40-bit GPU
 * addresses (Trinity's carve-out is at GPU F_00000000). EGCP.VXD executes
 * the op lists built here. Writes C:\V9XDIAG\V9XCM3D.INI. PFP.BIN and
 * ME.BIN in the current directory are ARUBA_pfp.bin and ARUBA_me.bin,
 * 2176 dwords each, as V9XEGCP reads them.
 *
 * Every Cayman-specific value comes from the host-tested Cayman module:
 *   cm_3d.c     cayman_gpu_init's writes for 9901, ME_INITIALIZE with
 *               eight contexts, cayman_cp_start's preamble around
 *               cayman_default_state, set_default_state's Cayman form,
 *               CP_COHER_CNTL2;
 *   cm_shader.c Linux 3.9's cayman_vs and cayman_ps, rebuilt.
 * The CP bring-up is V9XEGCP's Cayman one (three rings, measured three of
 * three on M-D7TFW4, docs\decisions\2026-11-03-trinity-cp-microcode.md).
 * The draw's packets are Linux 3.9's evergreen_blit_kms.c, which Cayman
 * shares apart from CP_COHER_CNTL2 before each ring SURFACE_SYNC; this
 * syncs the vertex buffer through the texture cache, Cayman having no
 * vertex cache, where Linux names VC_ACTION for every chip but Cedar,
 * Palm, Sumo and Caicos.
 *
 * Each 256-byte address field (SQ_PGM_START_VS and _PS, the texture base
 * and mip base, CB_COLOR0_BASE, CP_COHER_BASE, CP_RB0..2_BASE,
 * SCRATCH_ADDR) takes bits 39:32 in its top byte; the vertex resource
 * takes them in word 2 bits 7:0 beside the stride; CP_RB*_RPTR_ADDR_HI
 * takes them whole. The surfaces are linear aligned, 64 pixels wide, the
 * colour buffer with NON_DISP_TILING_ORDER as Mesa sets it on every
 * linear one (evergreen_state.c); /tiled draws both 1D tiled, as Linux
 * 3.9's blit did.
 *
 * Call 0 reads the id: anything but 9901 stops. Call 1 is read-only: the
 * CP's registers, every gpu_init register, CGTS_SM_CTRL_REG. The run goes
 * ahead only if the CP is halted. /readonly stops there. Call 2 saves and
 * lays out the window, writes gpu_init (with cayman_gpu_init's sixteen
 * CGTS_SM_CTRL_REG OVERRIDE writes and its value back), loads and starts
 * the CP, runs ME_INITIALIZE, the preamble, the ring test and the draw,
 * waits for the mark and GUI idle, halts and resets the CP blocks, puts
 * back every register call 1 read, and reads the destination back.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/cm_3d.h"
#include "velocity9x/cm_shader.h"
#include "velocity9x/eg_shader.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_CM3D_PATH        V9X_DIAG_PATH("V9XCM3D.INI")
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

/* EGCP.VXD's EC_PATTERN_STEP. */
#define V9X_PATTERN_STEP    0x9e3779b9ul

#define V9X_DEVICE_TRINITY  0x9901ul

/* Microcode, radeon_ucode.h CAYMAN_PFP/PM4_UCODE_SIZE. */
#define V9X_CM_PFP_DWORDS   2176ul
#define V9X_CM_ME_DWORDS    2176ul

/* Registers not in eg_3d.h (evergreend.h, nid.h). */
#define V9X_EG_MC_VM_FB_LOCATION    0x2024ul
#define V9X_EG_HDP_MEM_FLUSH        0x5480ul
#define V9X_EG_CC_RB_BACKEND_DIS    0x98f4ul
#define V9X_EG_CC_GC_SHADER_PIPE    0x8950ul
#define V9X_CM_CP_RB2_RPTR          0x86f8ul
#define V9X_CM_CP_RB1_RPTR          0x86fcul
#define V9X_CM_CP_RB1_BASE          0xc180ul
#define V9X_CM_CP_RB1_CNTL          0xc184ul
#define V9X_CM_CP_RB1_RPTR_ADDR     0xc188ul
#define V9X_CM_CP_RB1_RPTR_ADDR_HI  0xc18cul
#define V9X_CM_CP_RB1_WPTR          0xc190ul
#define V9X_CM_CP_RB2_BASE          0xc194ul
#define V9X_CM_CP_RB2_CNTL          0xc198ul
#define V9X_CM_CP_RB2_RPTR_ADDR     0xc19cul
#define V9X_CM_CP_RB2_RPTR_ADDR_HI  0xc1a0ul
#define V9X_CM_CP_RB2_WPTR          0xc1a4ul
/* cayman_cp_resume's CP_DEBUG and cayman_cp_enable's run value. */
#define V9X_CM_CP_DEBUG_VALUE       0x08000000ul
#define V9X_CM_CP_RUN               0x00000000ul

/* FB_LOCATION's start field: low byte bits 31:24, high byte 39:32. The
 * window must not carry out of the low 32 bits. */
#define V9X_EG_FB_START_MASK        0x0000fffful
#define V9X_EG_FB_LOW_MASK          0x000000fful
#define V9X_EG_FB_HIGH_SHIFT        8u
#define V9X_EG_FB_LOW_MAX           0x000000eful
#define V9X_ADDR_HIGH_SHIFT         24u

/* PM4 (evergreend.h, radeon_blit_common.h). V9X_PACKET3's n is Linux's,
 * the values less one. */
#define V9X_PACKET3(op, n) \
    ((3ul << 30) | (((DWORD)(op) & 0xfful) << 8) | \
     (((DWORD)(n) & 0x3ffful) << 16))
#define V9X_PACKET2                 0x80000000ul
#define V9X_SYNC_CB_ACTION          0x02000000ul /* 1 << 25 */
#define V9X_SYNC_CB0_DEST_BASE      0x00000040ul /* 1 << 6 */
#define V9X_SYNC_TC_ACTION          0x00800000ul /* 1 << 23 */
#define V9X_SYNC_SH_ACTION          0x08000000ul /* 1 << 27 */
#define V9X_SYNC_POLL_INTERVAL      10ul
#define V9X_DI_PT_RECTLIST          0x11ul
#define V9X_DI_INDEX_SIZE_16_BIT    0x0ul
#define V9X_DI_SRC_SEL_AUTO_INDEX   0x2ul

/* Context registers. */
#define V9X_EG_PA_SC_SCREEN_SCISSOR  0x28030ul
#define V9X_EG_PA_SC_WINDOW_SCISSOR  0x28204ul
#define V9X_EG_PA_SC_GENERIC_SCISSOR 0x28240ul
#define V9X_EG_SQ_PGM_START_PS       0x28840ul
#define V9X_EG_SQ_PGM_START_VS       0x2885cul
#define V9X_EG_CB_COLOR0_BASE        0x28c60ul

/* The draw: 64x8 pixels of COLOR_8_8_8_8. */
#define V9X_DRAW_WIDTH              64ul
#define V9X_DRAW_HEIGHT             8ul
#define V9X_DRAW_BYTES              (V9X_DRAW_WIDTH * V9X_DRAW_HEIGHT * 4ul)
#define V9X_DRAW_DWORDS             (V9X_DRAW_BYTES / 4ul)
#define V9X_FORMAT_8_8_8_8          0x1aul
#define V9X_ARRAY_LINEAR_ALIGNED    1ul
#define V9X_ARRAY_1D_TILED_THIN1    2ul
/* CB_COLOR0_ATTRIB bit 4, S_028C74_NON_DISP_TILING_ORDER. */
#define V9X_CB_NON_DISP_TILING_ORDER 0x00000010ul
#define V9X_SQ_TEX_DIM_2D           1ul
#define V9X_SQ_TEX_VALID_TEXTURE    0x2ul
#define V9X_SQ_TEX_VALID_BUFFER     0x3ul
#define V9X_FLOAT_8                 0x41000000ul
#define V9X_FLOAT_64                0x42800000ul
#define V9X_BLIT_STRIDE             16ul
#define V9X_BLIT_VERTICES           3ul
#define V9X_BLIT_VERTEX_DWORDS      12ul

/* The window's layout, V9XEG3D's, with RB1 and RB2's idle rings where
 * V9XEGCP puts them. */
#define V9X_RB_CNTL                 ((9ul << 8) | 11ul)
#define V9X_RING_ALIGN_DWORDS       16ul
#define V9X_SCRATCH_WB              0x4000ul
#define V9X_RPTR_WB                 0x4100ul
#define V9X_VS_OFFSET               0x5000ul
#define V9X_PS_OFFSET               0x5100ul
#define V9X_VB_OFFSET               0x5400ul
#define V9X_SRC_OFFSET              0x6000ul
#define V9X_DST_OFFSET              0x7000ul
#define V9X_DST_GUARD_DWORDS        1024ul
#define V9X_RING1_OFFSET            0x8000ul
#define V9X_RING2_OFFSET            0xc000ul
#define V9X_GUARD_VALUE             0x5a5a5a5aul
#define V9X_SRC_SEED                0x01020304ul
#define V9X_SCRATCH_GPU_OFFSET      0x0f100000ul
#define V9X_WINDOW_DWORDS           16384ul

#define V9X_SCRATCH_SEED            0xcafedeadul
#define V9X_SCRATCH_TEST            0xdeadbeeful
#define V9X_SCRATCH_DRAWN           0x3d3d3d3dul
#define V9X_RESET_DELAY_READS       20000ul
#define V9X_SHORT_DELAY_READS       2000ul

/* The data area: the microcode, then the ring, the shaders, the vertices. */
#define V9X_DATA_RING               (V9X_CM_PFP_DWORDS + V9X_CM_ME_DWORDS)
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
typedef char v9x_data_fits[
    V9X_DATA_RING + V9X_RING_MAX + 2u * V9X_CM_SHADER_DWORDS_MAX +
    V9X_BLIT_VERTEX_DWORDS <= V9X_EGCP_DATA ? 1 : -1];

/* The CP's registers, put back last as V9XEGCP puts them back; CP_ME_CNTL
 * first and written last. */
static const DWORD v9x_cp_offsets[] = {
    V9X_EG_CP_ME_CNTL, V9X_EG_CP_RB_CNTL, V9X_EG_CP_RB_BASE,
    V9X_EG_CP_RB_RPTR_ADDR, V9X_EG_CP_RB_RPTR_ADDR_HI, V9X_EG_CP_RB_WPTR,
    V9X_EG_CP_DEBUG, V9X_EG_SCRATCH_REG0, V9X_EG_SCRATCH_UMSK,
    V9X_EG_SCRATCH_ADDR, V9X_EG_CP_SEM_WAIT_TIMER, V9X_EG_CP_SEM_INCOMPLETE,
    V9X_EG_CP_RB_WPTR_DELAY,
    V9X_CM_CP_RB1_BASE, V9X_CM_CP_RB1_CNTL, V9X_CM_CP_RB1_RPTR_ADDR,
    V9X_CM_CP_RB1_RPTR_ADDR_HI, V9X_CM_CP_RB1_WPTR,
    V9X_CM_CP_RB2_BASE, V9X_CM_CP_RB2_CNTL, V9X_CM_CP_RB2_RPTR_ADDR,
    V9X_CM_CP_RB2_RPTR_ADDR_HI, V9X_CM_CP_RB2_WPTR
};
#define V9X_CP_SAVED (sizeof(v9x_cp_offsets) / sizeof(v9x_cp_offsets[0]))

/* Besides gpu_init's own: the shader-complex register its OVERRIDE
 * sequence writes, and the config registers the ring writes. */
static const DWORD v9x_extra_offsets[] = {
    V9X_CM_CGTS_SM_CTRL_REG, V9X_CM_CP_COHER_CNTL2, V9X_EG_VGT_PRIMITIVE_TYPE
};
#define V9X_EXTRA_SAVED \
    (sizeof(v9x_extra_offsets) / sizeof(v9x_extra_offsets[0]))
#define V9X_SAVED_COUNT \
    (V9X_CP_SAVED + V9X_CM_TRINITY_GPU_INIT_COUNT + V9X_EXTRA_SAVED)

/* Read and published, never written. */
static const DWORD v9x_info_offsets[] = {
    V9X_EG_CC_RB_BACKEND_DIS, V9X_EG_CC_GC_SHADER_PIPE,
    V9X_EG_GRBM_STATUS, V9X_EG_CP_STAT, V9X_EG_MC_VM_FB_LOCATION
};
#define V9X_INFO_COUNT (sizeof(v9x_info_offsets) / sizeof(v9x_info_offsets[0]))

static struct v9x_egcp_request v9x_request;
static struct v9x_egcp_result v9x_result;
static DWORD v9x_saved_offsets[V9X_SAVED_COUNT];
static DWORD v9x_saved[V9X_SAVED_COUNT];
static DWORD v9x_info[V9X_INFO_COUNT];
static DWORD v9x_ring[V9X_RING_MAX];
static DWORD v9x_ring_count;
static BYTE v9x_microcode_bytes[V9X_CM_ME_DWORDS * 4ul];
static v9x_u32 v9x_stage[V9X_RING_MAX];
static struct v9x_eg_pm4 v9x_stage_stream;
static v9x_u32 v9x_vs[V9X_CM_SHADER_DWORDS_MAX];
static v9x_u32 v9x_ps[V9X_CM_SHADER_DWORDS_MAX];
static DWORD v9x_vs_dwords;
static DWORD v9x_ps_dwords;
static DWORD v9x_vertices[V9X_BLIT_VERTEX_DWORDS];

/* Bits 39:32 of every GPU address in the window. */
static DWORD v9x_gpu_high;
static DWORD v9x_array_mode = V9X_ARRAY_LINEAR_ALIGNED;

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
    int count = 0;
    int at = 0;

    do {
        digits[count++] = (char)('0' + (int)(value % 10ul));
        value /= 10ul;
    } while (value != 0ul);
    while (count != 0) {
        text[at++] = digits[--count];
    }
    text[at] = '\0';
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_CM3D_PATH);
}

static void v9x_key(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_hex(text, value, 8);
    v9x_text(section, key, text);
}

/* A register's value under its offset, R<offset>. */
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

/* Call 1's value of a saved register. */
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
    DWORD returned = 0ul;
    BOOL ok;

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

/* Reads count big-endian dwords into the data area at first. Non-zero
 * when the file is exactly that long. */
static int v9x_load_microcode(const char *path, DWORD first, DWORD count)
{
    HANDLE file;
    DWORD read = 0ul;
    DWORD size;
    DWORD index;
    const BYTE *bytes = v9x_microcode_bytes;

    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    size = GetFileSize(file, 0);
    if (size != count * 4ul || size > sizeof(v9x_microcode_bytes) ||
        !ReadFile(file, v9x_microcode_bytes, size, &read, 0) ||
        read != size) {
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

/* Pads with type-2 NOPs to Linux's 16-dword boundary; returns the write
 * pointer. */
static DWORD v9x_ring_close(void)
{
    while ((v9x_ring_count % V9X_RING_ALIGN_DWORDS) != 0ul) {
        v9x_ring_put(V9X_PACKET2);
    }
    return v9x_ring_count;
}

static struct v9x_eg_pm4 *v9x_stage_begin(void)
{
    v9x_eg_pm4_begin(&v9x_stage_stream, v9x_stage, V9X_RING_MAX);
    return &v9x_stage_stream;
}

/* Into the ring; an overflowed stage makes the ring too long to run. */
static void v9x_stage_end(void)
{
    DWORD index;

    for (index = 0ul; index < v9x_stage_stream.count; ++index) {
        v9x_ring_put(v9x_stage[index]);
    }
    if (v9x_stage_stream.overflow != 0ul) {
        v9x_ring_count = V9X_RING_MAX + 1ul;
    }
}

static void v9x_set_config(DWORD offset, DWORD value)
{
    v9x_ring_put(V9X_PACKET3(V9X_EG_P3_SET_CONFIG_REG, 1ul));
    v9x_ring_put((offset - V9X_EG_SET_CONFIG_START) >> 2);
    v9x_ring_put(value);
}

/* A 40-bit address as a 256-byte field: bits 39:8. */
static DWORD v9x_field(DWORD low)
{
    return (v9x_gpu_high << V9X_ADDR_HIGH_SHIFT) | (low >> 8);
}

/* cp_set_surface_sync on Cayman: CP_COHER_CNTL2 first (cm_3d.c), then the
 * Evergreen packet with the 40-bit base. */
static void v9x_surface_sync(DWORD sync_type, DWORD size, DWORD address)
{
    v9x_cm_3d_coher_cntl2(v9x_stage_begin());
    v9x_stage_end();
    v9x_ring_put(V9X_PACKET3(V9X_EG_P3_SURFACE_SYNC, 3ul));
    v9x_ring_put(sync_type);
    v9x_ring_put((size + 255ul) >> 8);
    v9x_ring_put(v9x_field(address));
    v9x_ring_put(V9X_SYNC_POLL_INTERVAL);
}

/* Linux 3.9's blit, Cayman's form: set_default_state, set_shaders,
 * set_tex_resource, set_render_target, set_scissors, set_vtx_resource,
 * draw_auto, the CB flushed, then the mark. */
static void v9x_ring_draw(DWORD g)
{
    DWORD vs = g + V9X_VS_OFFSET;
    DWORD ps = g + V9X_PS_OFFSET;
    DWORD vb = g + V9X_VB_OFFSET;
    DWORD src = g + V9X_SRC_OFFSET;
    DWORD dst = g + V9X_DST_OFFSET;
    DWORD vb_bytes = V9X_BLIT_VERTEX_DWORDS * 4ul;
    DWORD extent = V9X_DRAW_WIDTH | (V9X_DRAW_HEIGHT << 16);
    DWORD attrib;
    DWORD index;

    v9x_cm_3d_default_state(v9x_stage_begin());
    v9x_stage_end();

    /* set_shaders: the VS uses R0 and R1, the PS R0; the PS exports one
     * colour (SQ_PGM_EXPORTS_PS 2). */
    v9x_ring_put(V9X_PACKET3(V9X_EG_P3_SET_CONTEXT_REG, 3ul));
    v9x_ring_put((V9X_EG_SQ_PGM_START_VS - V9X_EG_SET_CONTEXT_START) >> 2);
    v9x_ring_put(v9x_field(vs));
    v9x_ring_put(2ul);
    v9x_ring_put(0ul);
    v9x_ring_put(V9X_PACKET3(V9X_EG_P3_SET_CONTEXT_REG, 4ul));
    v9x_ring_put((V9X_EG_SQ_PGM_START_PS - V9X_EG_SET_CONTEXT_START) >> 2);
    v9x_ring_put(v9x_field(ps));
    v9x_ring_put(1ul);
    v9x_ring_put(0ul);
    v9x_ring_put(2ul);
    v9x_surface_sync(V9X_SYNC_SH_ACTION, 512ul, vs);

    /* set_tex_resource: FMT_8_8_8_8, 64 wide, pitch 64, 8 high, swizzle
     * XYZW, a valid texture; the base and mip base 40-bit. */
    v9x_surface_sync(V9X_SYNC_TC_ACTION, V9X_DRAW_BYTES, src);
    v9x_ring_put(V9X_PACKET3(V9X_EG_P3_SET_RESOURCE, 8ul));
    v9x_ring_put(0ul);
    v9x_ring_put(V9X_SQ_TEX_DIM_2D | (((V9X_DRAW_WIDTH >> 3) - 1ul) << 6) |
                 ((V9X_DRAW_WIDTH - 1ul) << 18));
    v9x_ring_put((V9X_DRAW_HEIGHT - 1ul) | (v9x_array_mode << 28));
    v9x_ring_put(v9x_field(src));
    v9x_ring_put(v9x_field(src));
    v9x_ring_put((0ul << 16) | (1ul << 19) | (2ul << 22) | (3ul << 25));
    v9x_ring_put(0ul);
    v9x_ring_put(0ul);
    v9x_ring_put(V9X_FORMAT_8_8_8_8 | (V9X_SQ_TEX_VALID_TEXTURE << 30));

    /* set_render_target: COLOR_8_8_8_8, export norm, the array mode;
     * linear aligned takes NON_DISP_TILING_ORDER. */
    attrib = v9x_array_mode == V9X_ARRAY_LINEAR_ALIGNED ?
             V9X_CB_NON_DISP_TILING_ORDER : 0ul;
    v9x_ring_put(V9X_PACKET3(V9X_EG_P3_SET_CONTEXT_REG, 15ul));
    v9x_ring_put((V9X_EG_CB_COLOR0_BASE - V9X_EG_SET_CONTEXT_START) >> 2);
    v9x_ring_put(v9x_field(dst));
    v9x_ring_put((V9X_DRAW_WIDTH / 8ul) - 1ul);
    v9x_ring_put(((V9X_DRAW_WIDTH * V9X_DRAW_HEIGHT) / 64ul) - 1ul);
    v9x_ring_put(0ul);
    v9x_ring_put((V9X_FORMAT_8_8_8_8 << 2) | (1ul << 24) |
                 (v9x_array_mode << 8));
    v9x_ring_put(attrib);
    v9x_ring_put((V9X_DRAW_WIDTH - 1ul) | ((V9X_DRAW_HEIGHT - 1ul) << 16));
    for (index = 0ul; index < 8ul; ++index) {
        v9x_ring_put(0ul);
    }

    /* set_scissors(0, 0, 64, 8): Cayman's 2x1 floor does not apply. */
    v9x_ring_put(V9X_PACKET3(V9X_EG_P3_SET_CONTEXT_REG, 2ul));
    v9x_ring_put((V9X_EG_PA_SC_SCREEN_SCISSOR - V9X_EG_SET_CONTEXT_START) >> 2);
    v9x_ring_put(0ul);
    v9x_ring_put(extent);
    v9x_ring_put(V9X_PACKET3(V9X_EG_P3_SET_CONTEXT_REG, 2ul));
    v9x_ring_put((V9X_EG_PA_SC_GENERIC_SCISSOR - V9X_EG_SET_CONTEXT_START) >>
                 2);
    v9x_ring_put(0x80000000ul);
    v9x_ring_put(extent);
    v9x_ring_put(V9X_PACKET3(V9X_EG_P3_SET_CONTEXT_REG, 2ul));
    v9x_ring_put((V9X_EG_PA_SC_WINDOW_SCISSOR - V9X_EG_SET_CONTEXT_START) >>
                 2);
    v9x_ring_put(0x80000000ul);
    v9x_ring_put(extent);

    /* set_vtx_resource: the blit's 16-byte stride and size, bits 39:32 in
     * word 2 beside the stride, swizzle XYZW, a valid buffer; synced
     * through the texture cache, which Cayman's fetches use. */
    v9x_ring_put(V9X_PACKET3(V9X_EG_P3_SET_RESOURCE, 8ul));
    v9x_ring_put(0x580ul);
    v9x_ring_put(vb);
    v9x_ring_put(vb_bytes - 1ul);
    v9x_ring_put((V9X_BLIT_STRIDE << 8) | v9x_gpu_high);
    v9x_ring_put((0ul << 3) | (1ul << 6) | (2ul << 9) | (3ul << 12));
    v9x_ring_put(0ul);
    v9x_ring_put(0ul);
    v9x_ring_put(0ul);
    v9x_ring_put(V9X_SQ_TEX_VALID_BUFFER << 30);
    v9x_surface_sync(V9X_SYNC_TC_ACTION, vb_bytes, vb);

    /* draw_auto: a RECTLIST of three vertices. */
    v9x_set_config(V9X_EG_VGT_PRIMITIVE_TYPE, V9X_DI_PT_RECTLIST);
    v9x_ring_put(V9X_PACKET3(V9X_EG_P3_INDEX_TYPE, 0ul));
    v9x_ring_put(V9X_DI_INDEX_SIZE_16_BIT);
    v9x_ring_put(V9X_PACKET3(V9X_EG_P3_NUM_INSTANCES, 0ul));
    v9x_ring_put(1ul);
    v9x_ring_put(V9X_PACKET3(V9X_EG_P3_DRAW_INDEX_AUTO, 1ul));
    v9x_ring_put(V9X_BLIT_VERTICES);
    v9x_ring_put(V9X_DI_SRC_SEL_AUTO_INDEX);

    v9x_surface_sync(V9X_SYNC_CB_ACTION | V9X_SYNC_CB0_DEST_BASE,
                     V9X_DRAW_BYTES, dst);
    v9x_set_config(V9X_EG_SCRATCH_REG0, V9X_SCRATCH_DRAWN);
}

/* cayman_gpu_init's writes (cm_3d.c's table) from call 1's reads where
 * Linux reads first, with its CGTS_SM_CTRL_REG sequence after the four
 * texture-cache disables, where Linux has it. */
static void v9x_gpu_init(void)
{
    const struct v9x_eg_reg_update *update;
    DWORD index;
    DWORD repeat;
    DWORD value;

    for (index = 0ul; index < V9X_CM_TRINITY_GPU_INIT_COUNT; ++index) {
        update = &v9x_cm_trinity_gpu_init[index];
        value = update->set;
        if (update->keep != 0ul) {
            value |= v9x_saved_value(update->offset) & update->keep;
        }
        v9x_write(update->offset, value);
        if (update->offset == 0x914cul) {  /* CGTS_USER_TCC_DISABLE */
            for (repeat = 0ul; repeat < V9X_CM_CGTS_SM_OVERRIDE_WRITES;
                 ++repeat) {
                v9x_write(V9X_CM_CGTS_SM_CTRL_REG, V9X_CM_CGTS_SM_OVERRIDE);
            }
            v9x_write(V9X_CM_CGTS_SM_CTRL_REG,
                      v9x_saved_value(V9X_CM_CGTS_SM_CTRL_REG));
        }
    }
}

/* cayman_cp_load_microcode and cayman_cp_resume, as V9XEGCP runs them. */
static void v9x_cp_resume(DWORD g)
{
    static const DWORD cntl[3] = {
        V9X_EG_CP_RB_CNTL, V9X_CM_CP_RB1_CNTL, V9X_CM_CP_RB2_CNTL
    };
    static const DWORD rptr_addr[3] = {
        V9X_EG_CP_RB_RPTR_ADDR, V9X_CM_CP_RB1_RPTR_ADDR,
        V9X_CM_CP_RB2_RPTR_ADDR
    };
    static const DWORD rptr_addr_hi[3] = {
        V9X_EG_CP_RB_RPTR_ADDR_HI, V9X_CM_CP_RB1_RPTR_ADDR_HI,
        V9X_CM_CP_RB2_RPTR_ADDR_HI
    };
    static const DWORD base[3] = {
        V9X_EG_CP_RB_BASE, V9X_CM_CP_RB1_BASE, V9X_CM_CP_RB2_BASE
    };
    static const DWORD rptr[3] = {
        V9X_EG_CP_RB_RPTR, V9X_CM_CP_RB1_RPTR, V9X_CM_CP_RB2_RPTR
    };
    static const DWORD wptr[3] = {
        V9X_EG_CP_RB_WPTR, V9X_CM_CP_RB1_WPTR, V9X_CM_CP_RB2_WPTR
    };
    static const DWORD ring_offset[3] = {
        0ul, V9X_RING1_OFFSET, V9X_RING2_OFFSET
    };
    DWORD ring;

    v9x_write(V9X_EG_CP_ME_CNTL, V9X_EG_CP_HALT);
    v9x_write(V9X_EG_SCRATCH_UMSK, 0ul);
    v9x_write(V9X_EG_CP_PFP_UCODE_ADDR, 0ul);
    v9x_op(V9X_OP_STREAM, V9X_EG_CP_PFP_UCODE_DATA, 0ul, V9X_CM_PFP_DWORDS);
    v9x_write(V9X_EG_CP_PFP_UCODE_ADDR, 0ul);
    v9x_write(V9X_EG_CP_ME_RAM_WADDR, 0ul);
    v9x_op(V9X_OP_STREAM, V9X_EG_CP_ME_RAM_DATA, V9X_CM_PFP_DWORDS,
           V9X_CM_ME_DWORDS);
    v9x_write(V9X_EG_CP_PFP_UCODE_ADDR, 0ul);
    v9x_write(V9X_EG_CP_ME_RAM_WADDR, 0ul);
    v9x_write(V9X_EG_CP_ME_RAM_RADDR, 0ul);

    v9x_write(V9X_EG_GRBM_SOFT_RESET, V9X_EG_GRBM_RESET_CP_BLOCKS);
    v9x_read(V9X_EG_GRBM_SOFT_RESET);
    v9x_op(V9X_OP_DELAY, V9X_RESET_DELAY_READS, 0ul, 0ul);
    v9x_write(V9X_EG_GRBM_SOFT_RESET, 0ul);
    v9x_read(V9X_EG_GRBM_SOFT_RESET);
    v9x_write(V9X_EG_CP_SEM_WAIT_TIMER, 0ul);
    v9x_write(V9X_EG_CP_SEM_INCOMPLETE, 0ul);
    v9x_write(V9X_EG_CP_RB_WPTR_DELAY, 0ul);
    v9x_write(V9X_EG_CP_DEBUG, V9X_CM_CP_DEBUG_VALUE);
    v9x_write(V9X_EG_SCRATCH_ADDR, v9x_field(g + V9X_SCRATCH_WB));
    v9x_write(V9X_EG_SCRATCH_UMSK, 0ul);

    for (ring = 0ul; ring < 3ul; ++ring) {
        v9x_write(cntl[ring], V9X_RB_CNTL | V9X_EG_RB_NO_UPDATE);
        v9x_write(rptr_addr[ring], (g + V9X_RPTR_WB) & ~3ul);
        v9x_write(rptr_addr_hi[ring], v9x_gpu_high);
    }
    /* Writing one base resets every ring. */
    for (ring = 0ul; ring < 3ul; ++ring) {
        v9x_write(base[ring], v9x_field(g + ring_offset[ring]));
    }
    for (ring = 0ul; ring < 3ul; ++ring) {
        v9x_write(cntl[ring], V9X_RB_CNTL | V9X_EG_RB_NO_UPDATE |
                              V9X_EG_RB_RPTR_WR_ENA);
        v9x_write(rptr[ring], 0ul);
        v9x_write(wptr[ring], 0ul);
        v9x_op(V9X_OP_DELAY, V9X_SHORT_DELAY_READS, 0ul, 0ul);
        v9x_write(cntl[ring], V9X_RB_CNTL | V9X_EG_RB_NO_UPDATE);
    }
}

static void v9x_build_run(DWORD g)
{
    DWORD index;
    DWORD data;

    v9x_vs_dwords = v9x_cm_shader_blit_vs(v9x_vs);
    v9x_ps_dwords = v9x_cm_shader_blit_ps(v9x_ps);
    /* r600_kms_blit_copy's vertices: (dst x, dst y, src x, src y). */
    for (index = 0ul; index < V9X_BLIT_VERTEX_DWORDS; ++index) {
        v9x_vertices[index] = 0ul;
    }
    v9x_vertices[5] = V9X_FLOAT_8;
    v9x_vertices[7] = V9X_FLOAT_8;
    v9x_vertices[8] = V9X_FLOAT_64;
    v9x_vertices[9] = V9X_FLOAT_8;
    v9x_vertices[10] = V9X_FLOAT_64;
    v9x_vertices[11] = V9X_FLOAT_8;

    /* The ring: four submissions, each closed to 16 dwords. */
    v9x_ring_count = 0ul;
    v9x_cm_3d_me_initialize(v9x_stage_begin());
    v9x_stage_end();
    v9x_wptr_init = v9x_ring_close();
    v9x_cm_3d_preamble(v9x_stage_begin());
    v9x_stage_end();
    v9x_wptr_preamble = v9x_ring_close();
    v9x_set_config(V9X_EG_SCRATCH_REG0, V9X_SCRATCH_TEST);
    v9x_wptr_test = v9x_ring_close();
    v9x_ring_draw(g);
    v9x_wptr_draw = v9x_ring_close();

    data = V9X_DATA_RING;
    for (index = 0ul; index < v9x_ring_count && index < V9X_RING_MAX;
         ++index) {
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
    for (index = 0ul; index < V9X_BLIT_VERTEX_DWORDS; ++index) {
        v9x_request.data[data + index] = v9x_vertices[index];
    }
    v9x_request.data_count = data + V9X_BLIT_VERTEX_DWORDS;

    v9x_request.count = 0ul;
    v9x_op(V9X_OP_SAVE, 0ul, 0ul, 0ul);
    v9x_op(V9X_OP_VRAM_FILL, 0ul, V9X_WINDOW_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_PATTERN, V9X_SRC_OFFSET, V9X_DRAW_DWORDS, V9X_SRC_SEED);
    v9x_op(V9X_OP_VRAM_FILL, V9X_DST_OFFSET, V9X_DST_GUARD_DWORDS,
           V9X_GUARD_VALUE);
    v9x_op(V9X_OP_VRAM_DATA, 0ul, V9X_DATA_RING, v9x_ring_count);
    v9x_op(V9X_OP_VRAM_DATA, V9X_VS_OFFSET, V9X_DATA_RING + v9x_ring_count,
           v9x_vs_dwords);
    v9x_op(V9X_OP_VRAM_DATA, V9X_PS_OFFSET,
           V9X_DATA_RING + v9x_ring_count + v9x_vs_dwords, v9x_ps_dwords);
    v9x_op(V9X_OP_VRAM_DATA, V9X_VB_OFFSET,
           V9X_DATA_RING + v9x_ring_count + v9x_vs_dwords + v9x_ps_dwords,
           V9X_BLIT_VERTEX_DWORDS);

    v9x_gpu_init();
    v9x_cp_resume(g);

    /* cayman_cp_start: ME_INITIALIZE, then the CP released, then the
     * preamble. */
    v9x_write(V9X_EG_HDP_MEM_FLUSH, 1ul);
    v9x_write(V9X_EG_CP_RB_WPTR, v9x_wptr_init);
    v9x_write(V9X_EG_CP_ME_CNTL, V9X_CM_CP_RUN);
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

    /* Halt, reset the CP blocks, put back: gpu_init's and the ring's
     * registers, then the CP's, CP_ME_CNTL last. */
    v9x_write(V9X_EG_CP_ME_CNTL, V9X_EG_CP_HALT);
    v9x_op(V9X_OP_DELAY, V9X_SHORT_DELAY_READS, 0ul, 0ul);
    v9x_write(V9X_EG_GRBM_SOFT_RESET, V9X_EG_GRBM_RESET_CP_BLOCKS);
    v9x_read(V9X_EG_GRBM_SOFT_RESET);
    v9x_op(V9X_OP_DELAY, V9X_RESET_DELAY_READS, 0ul, 0ul);
    v9x_write(V9X_EG_GRBM_SOFT_RESET, 0ul);
    v9x_read(V9X_EG_GRBM_SOFT_RESET);
    for (index = V9X_CP_SAVED; index < V9X_SAVED_COUNT; ++index) {
        v9x_write(v9x_saved_offsets[index], v9x_saved[index]);
    }
    for (index = 1ul; index < V9X_CP_SAVED; ++index) {
        v9x_write(v9x_saved_offsets[index], v9x_saved[index]);
    }
    v9x_write(V9X_EG_CP_ME_CNTL, v9x_saved[0]);
    v9x_at_after_me_cntl = v9x_read(V9X_EG_CP_ME_CNTL);
    v9x_at_after_grbm = v9x_read(V9X_EG_GRBM_STATUS);
    v9x_write(V9X_EG_HDP_MEM_FLUSH, 1ul);
    v9x_at_block = v9x_op(V9X_OP_VRAM_BLOCK, V9X_DST_OFFSET,
                          V9X_DST_GUARD_DWORDS, 0ul);
    v9x_op(V9X_OP_RESTORE, 0ul, 0ul, 0ul);

    v9x_key("Run", "RingDwords", v9x_ring_count);
    v9x_key("Run", "WptrInit", v9x_wptr_init);
    v9x_key("Run", "WptrPreamble", v9x_wptr_preamble);
    v9x_key("Run", "WptrTest", v9x_wptr_test);
    v9x_key("Run", "WptrDraw", v9x_wptr_draw);
    v9x_key("Run", "ArrayMode", v9x_array_mode);
    v9x_key("Run", "VsDwords", v9x_vs_dwords);
    v9x_key("Run", "PsDwords", v9x_ps_dwords);
    v9x_key("Run", "DataDwords", v9x_request.data_count);
    v9x_key("Run", "Ops", v9x_request.count);
}

static DWORD v9x_pattern(DWORD index)
{
    return V9X_SRC_SEED + index * V9X_PATTERN_STEP;
}

/* The destination against the source pattern, then the guard. A copy of
 * the pattern shows which source dword each pixel took, so a misplaced
 * copy is told from a wrong one: the first eight rows' source indices are
 * reported. */
static void v9x_verify(void)
{
    const DWORD *got = v9x_result.block;
    DWORD bad = 0ul;
    DWORD guard_bad = 0ul;
    DWORD first_bad = 0xfffffffful;
    DWORD index;
    DWORD source;
    char text[12];
    char key[12];

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
    v9x_key("Result", "Dst0", got[0]);
    v9x_key("Result", "DstLast", got[V9X_DRAW_DWORDS - 1ul]);
    v9x_key("Result", "DstAfter", got[V9X_DRAW_DWORDS]);
    /* Each row's first pixel's source index (FFFFFFFF: not a source
     * dword). */
    for (index = 0ul; index < V9X_DRAW_HEIGHT; ++index) {
        for (source = 0ul; source < V9X_DRAW_DWORDS; ++source) {
            if (got[index * V9X_DRAW_WIDTH] == v9x_pattern(source)) {
                break;
            }
        }
        lstrcpyA(key, "Row");
        v9x_decimal(key + 3, index);
        lstrcatA(key, "Src");
        v9x_key("Map", key, source == V9X_DRAW_DWORDS ? 0xfffffffful :
                source);
    }
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
    WritePrivateProfileStringA(0, 0, 0, V9X_CM3D_PATH);
    ExitProcess(code);
}

void __stdcall V9xCayman3dEntry(void)
{
    HANDLE device;
    DWORD index;
    DWORD at;
    DWORD fb;
    DWORD g;
    const DWORD *r;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_CM3D_PATH);
    v9x_text("Report", "Tool", "V9XCM3D");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    if (v9x_switch_given("/tiled")) {
        v9x_array_mode = V9X_ARRAY_1D_TILED_THIN1;
    }

    /* The run's saved list: the CP's, gpu_init's, then the extras. */
    at = 0ul;
    for (index = 0ul; index < V9X_CP_SAVED; ++index) {
        v9x_saved_offsets[at++] = v9x_cp_offsets[index];
    }
    for (index = 0ul; index < V9X_CM_TRINITY_GPU_INIT_COUNT; ++index) {
        v9x_saved_offsets[at++] = v9x_cm_trinity_gpu_init[index].offset;
    }
    for (index = 0ul; index < V9X_EXTRA_SAVED; ++index) {
        v9x_saved_offsets[at++] = v9x_extra_offsets[index];
    }

    device = CreateFileA(V9X_EGCP_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_key("Report", "LoadError", GetLastError());
        v9x_finish("VXD-LOAD-FAILED", 1u);
    }

    /* Call 0: the id. */
    v9x_request.count = 0ul;
    v9x_request.data_count = 0ul;
    v9x_read(V9X_EG_GRBM_STATUS);
    if (!v9x_dioc(device)) {
        CloseHandle(device);
        v9x_finish("REFUSED", 2u);
    }
    v9x_key("Report", "Id", v9x_result.id);
    v9x_key("Report", "Bar0", v9x_result.bar0);
    v9x_key("Report", "Bar2", v9x_result.bar2);
    if ((v9x_result.id >> 16) != V9X_DEVICE_TRINITY) {
        CloseHandle(device);
        v9x_finish("NOT-TRINITY", 2u);
    }
    if (!v9x_load_microcode("PFP.BIN", 0ul, V9X_CM_PFP_DWORDS) ||
        !v9x_load_microcode("ME.BIN", V9X_CM_PFP_DWORDS, V9X_CM_ME_DWORDS)) {
        CloseHandle(device);
        v9x_finish("MICROCODE-MISSING", 1u);
    }
    v9x_request.data_count = V9X_CM_PFP_DWORDS + V9X_CM_ME_DWORDS;

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
    if ((fb & V9X_EG_FB_LOW_MASK) > V9X_EG_FB_LOW_MAX) {
        CloseHandle(device);
        v9x_finish("FB-START-UNSUPPORTED", 4u);
    }
    v9x_gpu_high = fb >> V9X_EG_FB_HIGH_SHIFT;
    g = ((fb & V9X_EG_FB_LOW_MASK) << 24) + V9X_SCRATCH_GPU_OFFSET;
    v9x_key("Run", "WindowGpuHigh", v9x_gpu_high);
    v9x_key("Run", "WindowGpu", g);
    if (v9x_switch_given("/readonly")) {
        CloseHandle(device);
        v9x_finish("READONLY", 0u);
    }

    /* Call 2: the run. */
    v9x_build_run(g);
    if (v9x_ring_count > V9X_RING_MAX) {
        CloseHandle(device);
        v9x_finish("RING-OVERFLOW", 5u);
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
    v9x_text("Result", "MeInitialize",
             r[v9x_at_wait_init] != V9X_EGCP_TIMEOUT ? "PASS" : "FAIL");
    v9x_text("Result", "Preamble",
             r[v9x_at_wait_preamble] != V9X_EGCP_TIMEOUT ? "PASS" : "FAIL");
    v9x_text("Result", "RingTest",
             r[v9x_at_test_value] == V9X_SCRATCH_TEST ? "PASS" : "FAIL");
    v9x_text("Result", "DrawMark",
             r[v9x_at_drawn_value] == V9X_SCRATCH_DRAWN ? "PASS" : "FAIL");
    v9x_verify();
    (void)v9x_at_block;
    v9x_finish("COMPLETE", 0u);
}
