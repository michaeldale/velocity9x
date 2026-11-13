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
 * Nothing here has run on a card yet.
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
#define V9X_OP_DELAY        12ul
#define V9X_OP_STREAM       13ul
#define V9X_OP_VRAM_DATA    14ul

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
static v9x_u32 v9x_vs[V9X_R6_SOLID_VS_DWORDS];
static v9x_u32 v9x_ps[V9X_R6_SOLID_PS_DWORDS];
static v9x_u32 v9x_vertices[V9X_R6_3D_VERTEX_DWORDS];

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
    solid.target_gpu = g + V9X_TARGET_OFFSET;
    solid.target_pitch = V9X_TARGET_PITCH;
    solid.target_height = V9X_TARGET_HEIGHT;
    solid.vs_gpu = g + V9X_VS_OFFSET;
    solid.ps_gpu = g + V9X_PS_OFFSET;
    solid.vertex_gpu = g + V9X_VB_OFFSET;
    solid.colour = V9X_COLOUR;
    if (v9x_r6_3d_solid(&solid, &v9x_stream) != V9X_STATUS_OK) {
        return 0;
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
    for (index = 0ul; index < V9X_R6_SOLID_VS_DWORDS; ++index) {
        v9x_request.data[data + index] = v9x_vs[index];
    }
    data += V9X_R6_SOLID_VS_DWORDS;
    for (index = 0ul; index < V9X_R6_SOLID_PS_DWORDS; ++index) {
        v9x_request.data[data + index] = v9x_ps[index];
    }
    data += V9X_R6_SOLID_PS_DWORDS;
    for (index = 0ul; index < V9X_R6_3D_VERTEX_DWORDS; ++index) {
        v9x_request.data[data + index] = v9x_vertices[index];
    }
    v9x_request.data_count = data + V9X_R6_3D_VERTEX_DWORDS;

    /* 1. The window. */
    v9x_request.count = 0ul;
    v9x_op(V9X_OP_SAVE, 0ul, 0ul, 0ul);
    v9x_op(V9X_OP_VRAM_FILL, 0ul, V9X_WINDOW_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_FILL, V9X_GUARD_OFFSET, V9X_READBACK_DWORDS,
           V9X_GUARD_VALUE);
    v9x_op(V9X_OP_VRAM_FILL, V9X_TARGET_OFFSET, V9X_TARGET_DWORDS,
           V9X_BACKGROUND);
    v9x_op(V9X_OP_VRAM_DATA, 0ul, V9X_DATA_RING, v9x_stream.count);
    v9x_op(V9X_OP_VRAM_DATA, V9X_VS_OFFSET, V9X_DATA_RING + v9x_stream.count,
           V9X_R6_SOLID_VS_DWORDS);
    v9x_op(V9X_OP_VRAM_DATA, V9X_PS_OFFSET,
           V9X_DATA_RING + v9x_stream.count + V9X_R6_SOLID_VS_DWORDS,
           V9X_R6_SOLID_PS_DWORDS);
    v9x_op(V9X_OP_VRAM_DATA, V9X_VB_OFFSET,
           V9X_DATA_RING + v9x_stream.count + V9X_R6_SOLID_VS_DWORDS +
           V9X_R6_SOLID_PS_DWORDS, V9X_R6_3D_VERTEX_DWORDS);

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
    v9x_op(V9X_OP_RESTORE, 0ul, 0ul, 0ul);

    v9x_key("Run", "RingDwords", v9x_stream.count);
    v9x_key("Run", "WptrInit", v9x_wptr_init);
    v9x_key("Run", "WptrTest", v9x_wptr_test);
    v9x_key("Run", "WptrDraw", v9x_wptr_draw);
    v9x_key("Run", "WptrPutBack", v9x_wptr_put_back);
    v9x_key("Run", "DataDwords", v9x_request.data_count);
    v9x_key("Run", "Ops", v9x_request.count);
}

/* What readback dword index should hold: the guards, the background, or
 * the colour inside the rectangle. */
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
    if (x >= V9X_RECT_X1 && x < V9X_RECT_X2 &&
        y >= V9X_RECT_Y1 && y < V9X_RECT_Y2) {
        return V9X_COLOUR;
    }
    return V9X_BACKGROUND;
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
        if (got[index] == want) {
            if (want == V9X_COLOUR) {
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
    v9x_key("Run", "Colour", V9X_COLOUR);
    v9x_key("Run", "Background", V9X_BACKGROUND);
    if (v9x_switch_given("/readonly")) {
        CloseHandle(device);
        v9x_finish("READONLY", 0u);
    }

    /* Call 2: the run. The microcode is still in the data area. */
    v9x_r6_shader_solid_vs(v9x_vs);
    v9x_r6_shader_solid_ps(v9x_ps);
    v9x_r6_3d_rect_vertices(V9X_RECT_X1, V9X_RECT_Y1, V9X_RECT_X2,
                            V9X_RECT_Y2, v9x_vertices);
    if (!v9x_build_ring(g) ||
        V9X_DATA_RING + v9x_stream.count + V9X_R6_SOLID_VS_DWORDS +
        V9X_R6_SOLID_PS_DWORDS + V9X_R6_3D_VERTEX_DWORDS > V9X_EGCP_DATA) {
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
    v9x_finish("COMPLETE", 0u);
}
