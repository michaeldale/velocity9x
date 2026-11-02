/*
 * V9XN32D.EXE: NVIDIA RIVA 128 (NV3) 2D engine write probe.
 *
 * Phase 3 of docs\plans\nvidia-riva128.md. Drives N32D.VXD's op list:
 * Linux rivafb's NV3 bring-up (riva_hw.c LoadStateExt with riva_tbl.h's
 * RivaTablePMC, RivaTablePTIMER, nv3TablePFIFO, nv3TablePRAMIN and
 * nv3TablePGRAPH, converted from word indexes to byte offsets), then two
 * solid fills through the GDI rectangle object and one screen-to-screen
 * blit, into a 64 KiB scratch window of video memory 2 MiB in, laid out as
 * an 800-pixel surface at the desktop's depth. The tool models every byte
 * the window should then hold and counts mismatches in each region. Writes
 * C:\V9XDIAG\V9XN32D.INI.
 *
 * Left out of rivafb's sequence: the DMA object and page table at RAMIN
 * 1400h (only the D3D object refers to them, and the page table overlaps
 * RAMRO and RAMFC), the D3D, surface and pixmap RAMHT entries' use, and
 * every mode-setting register (CRTC, PRAMDAC): the VBE has set the mode.
 *
 * /all also runs at 800x600x8 and 800x600x32, switching with
 * ChangeDisplaySettings and back afterwards; before each run's own
 * bring-up it reads RAMIN to see whether the mode set wiped what the
 * previous run wrote. Option A of the plan's 16 bpp decision is the 16 bpp
 * run: raw fills and copies on a 5:6:5 desktop with the engine set up for
 * 5:5:5, one fill colour with bit 15 clear and one with it set.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_N32D_PATH        V9X_DIAG_PATH("V9XN32D.INI")
#define V9X_N32D_VXD         "\\\\.\\N32D.VXD"
#define V9X_N32D_MAGIC       0x4432334eul
#define V9X_N32D_RUN         1u
#define V9X_N32D_OP_MAX      512u
#define V9X_N32D_BLOCK       8192u
#define V9X_N32D_HEADER      16u

#define V9X_ST_FOUND         0x00000001ul
#define V9X_ST_MAPPED        0x00000002ul
#define V9X_ST_IDENTITY      0x00000004ul
#define V9X_ST_REFUSED       0x00000008ul
#define V9X_ST_COMPLETE      0x00000010ul
#define V9X_ST_TIMEOUT       0x00000040ul

#define V9X_OP_MMIO_READ     1ul
#define V9X_OP_MMIO_WRITE    2ul
#define V9X_OP_VRAM_BLOCK    6ul
#define V9X_OP_WAIT_MMIO     7ul
#define V9X_OP_WAIT_IDLE     8ul
#define V9X_OP_SAVE          9ul
#define V9X_OP_RESTORE       10ul
#define V9X_OP_VRAM_PATTERN  11ul
#define V9X_OP_DELAY         12ul
#define V9X_OP_LATCH_FREE    13ul
#define V9X_OP_RAMIN_WRITE   14ul
#define V9X_OP_RAMIN_READ    15ul
#define V9X_OP_RAMIN_FILL    16ul
#define V9X_PATTERN_STEP     0x9e3779b9ul
#define V9X_TIMEOUT          0xfffffffful

/* The scratch window as a surface: video memory 2 MiB in (N32D.VXD's
 * N2_SCRATCH_OFFSET), 800 pixels wide, the first V9X_LINES lines used. */
#define V9X_SURFACE_OFFSET   0x00200000ul
#define V9X_WIDTH            800ul
#define V9X_LINES            10ul
#define V9X_SEED             0x5eed0001ul

/* Registers, riva_tbl.h word index x 4 plus the unit base. */
#define V9X_PMC_INTR_EN      0x000140ul
#define V9X_PMC_ENABLE       0x000200ul
#define V9X_PTIMER_INTR      0x009100ul
#define V9X_PTIMER_INTR_EN   0x009140ul
#define V9X_PTIMER_NUM       0x009200ul
#define V9X_PTIMER_DEN       0x009210ul
#define V9X_PFB_CONFIG_0     0x100200ul
#define V9X_PFIFO_INTR       0x002100ul
#define V9X_PFIFO_INTR_EN    0x002140ul
#define V9X_PFIFO_RAMHT      0x002210ul
#define V9X_PFIFO_RAMFC      0x002214ul
#define V9X_PFIFO_RAMRO      0x002218ul
#define V9X_PFIFO_RUNOUT     0x002400ul
#define V9X_PFIFO_RUNOUT_PUT 0x002410ul
#define V9X_PFIFO_RUNOUT_GET 0x002420ul
#define V9X_PFIFO_CACHES     0x002500ul
#define V9X_CACHE1_PUSH0     0x003200ul
#define V9X_CACHE1_PUSH1     0x003204ul
#define V9X_CACHE1_PUT       0x003210ul
#define V9X_CACHE1_PULL0     0x003240ul
#define V9X_CACHE1_PULL_ST   0x003250ul
#define V9X_CACHE1_GET       0x003270ul
#define V9X_CACHE1_CTX       0x003280ul
#define V9X_CACHE1_CTX_STEP  0x10ul
#define V9X_PGRAPH_INTR_0    0x400100ul
#define V9X_PGRAPH_INTR_1    0x400104ul
#define V9X_PGRAPH_SURF_OFF  0x400630ul
#define V9X_PGRAPH_SURF_PITCH 0x400650ul
#define V9X_PGRAPH_SURF_CFG  0x4006a8ul
#define V9X_PGRAPH_STATUS    0x4006b0ul
#define V9X_USER_FREE        0x800010ul

/* The USER area: channel 0, subchannel s at 800000h + s * 2000h
 * (riva_hw.c RivaGetConfig), and the methods each object takes (the
 * riva_hw.h structs: reserved words, then the methods). */
#define V9X_USER             0x800000ul
#define V9X_SUBCHANNEL_STEP  0x2000ul
#define V9X_SUB_ROP          0ul
#define V9X_SUB_CLIP         1ul
#define V9X_SUB_PATTERN      2ul
#define V9X_SUB_PIXMAP       3ul
#define V9X_SUB_BLIT         4ul
#define V9X_SUB_BITMAP       5ul
#define V9X_SUB_LINE         6ul
#define V9X_M_ROP3           0x300ul
#define V9X_M_CLIP_POINT     0x300ul
#define V9X_M_CLIP_SIZE      0x304ul
#define V9X_M_PATTERN_COLOR0 0x310ul
#define V9X_M_PATTERN_COLOR1 0x314ul
#define V9X_M_PATTERN_MONO0  0x318ul
#define V9X_M_PATTERN_MONO1  0x31cul
#define V9X_M_BLIT_SRC       0x300ul
#define V9X_M_BLIT_DST       0x304ul
#define V9X_M_BLIT_SIZE      0x308ul
#define V9X_M_RECT_COLOR     0x3fcul
#define V9X_M_RECT_POINT     0x400ul
#define V9X_M_RECT_SIZE      0x404ul
#define V9X_ROP_SRCCOPY      0xccul

/* The test: two fills, then a blit whose source covers both. */
#define V9X_FILL_A_X  8ul
#define V9X_FILL_A_Y  2ul
#define V9X_FILL_A_W  40ul
#define V9X_FILL_A_H  5ul
#define V9X_FILL_B_X  60ul
#define V9X_FILL_B_Y  2ul
#define V9X_FILL_B_W  20ul
#define V9X_FILL_B_H  5ul
#define V9X_BLIT_SX   4ul
#define V9X_BLIT_SY   1ul
#define V9X_BLIT_DX   120ul
#define V9X_BLIT_DY   1ul
#define V9X_BLIT_W    80ul
#define V9X_BLIT_H    7ul

struct v9x_n32d_op {
    DWORD op;
    DWORD a;
    DWORD b;
    DWORD c;
};

struct v9x_n32d_request {
    DWORD count;
    struct v9x_n32d_op ops[V9X_N32D_OP_MAX];
};

struct v9x_n32d_result {
    DWORD magic;
    DWORD status;
    DWORD id;
    DWORD bar0;
    DWORD bar1;
    DWORD boot0;
    DWORD empty;
    DWORD reserved0;
    DWORD executed;
    DWORD refused_index;
    DWORD block_used;
    DWORD config;
    DWORD reserved[4];
    DWORD op_results[V9X_N32D_OP_MAX];
    DWORD block[V9X_N32D_BLOCK];
};

/* The header must be the VxD's N2_HEADER_DWORDS long. */
typedef char v9x_n32d_header_matches[
    offsetof(struct v9x_n32d_result, op_results) ==
    sizeof(DWORD) * V9X_N32D_HEADER ? 1 : -1];

struct v9x_pair {
    DWORD offset;
    DWORD value;
};

/* nv3TablePFIFO, in order. */
static const struct v9x_pair v9x_pfifo[] = {
    { V9X_PFIFO_CACHES, 0x00000000ul },
    { V9X_CACHE1_PUSH0, 0x00000000ul },
    { V9X_CACHE1_PULL0, 0x00000000ul },
    { V9X_CACHE1_PULL_ST, 0x00000000ul },
    { V9X_CACHE1_PUSH1, 0x00000000ul },
    { V9X_PFIFO_RAMHT, 0x00000000ul },
    { V9X_PFIFO_RAMRO, 0x00002000ul },
    { V9X_PFIFO_RAMFC, 0x00002200ul },
    { V9X_CACHE1_PUT, 0x00000000ul },
    { V9X_CACHE1_GET, 0x00000000ul },
    { V9X_PFIFO_RUNOUT_PUT, 0x00000000ul },
    { V9X_PFIFO_RUNOUT_GET, 0x00000000ul },
    { V9X_PFIFO_RUNOUT, 0x00000000ul },
    { V9X_CACHE1_CTX + 0x00ul, 0x00000000ul },
    { V9X_CACHE1_CTX + 0x10ul, 0x00000000ul },
    { V9X_CACHE1_CTX + 0x20ul, 0x00000000ul },
    { V9X_CACHE1_CTX + 0x30ul, 0x00000000ul },
    { V9X_CACHE1_CTX + 0x40ul, 0x00000000ul },
    { V9X_CACHE1_CTX + 0x50ul, 0x00000000ul },
    { V9X_CACHE1_CTX + 0x60ul, 0x00000000ul },
    { V9X_CACHE1_CTX + 0x70ul, 0x00000000ul },
    { V9X_PFIFO_INTR_EN, 0x00000000ul },
    { V9X_PFIFO_INTR, 0xfffffffful },
    { V9X_CACHE1_PUSH0, 0x00000001ul },
    { V9X_CACHE1_PULL0, 0x00000001ul },
    { V9X_PFIFO_CACHES, 0x00000001ul }
};

/* nv3TablePGRAPH, in order. */
static const struct v9x_pair v9x_pgraph[] = {
    { 0x400080ul, 0x1230001ful },
    { 0x400084ul, 0x10113000ul },
    { 0x400088ul, 0x1131f101ul },
    { 0x40008cul, 0x0100f531ul },
    { 0x400180ul, 0x00000000ul },
    { 0x400194ul, 0x00000000ul },
    { 0x4001a0ul, 0x00000000ul },
    { 0x4001a4ul, 0x00000000ul },
    { 0x4001a8ul, 0x00000000ul },
    { 0x4001acul, 0x00000000ul },
    { 0x4001b0ul, 0x00000000ul },
    { 0x4001b4ul, 0x00000000ul },
    { 0x4001b8ul, 0x00000000ul },
    { 0x4001bcul, 0x00000000ul },
    { 0x4006a0ul, 0x00000000ul },
    { 0x401100ul, 0xfffffffful },
    { 0x401200ul, 0x00000001ul },
    { 0x400680ul, 0x00000000ul },
    { 0x400688ul, 0x00000000ul },
    { 0x400628ul, 0xfffffffful },
    { 0x400640ul, 0x00000000ul },
    { 0x400508ul, 0x00000000ul },
    { 0x400550ul, 0x00000000ul },
    { 0x400554ul, 0xfffffffful },
    { 0x400558ul, 0x00000000ul },
    { 0x40055cul, 0xfffffffful },
    { 0x400190ul, 0x10010002ul },
    { 0x400140ul, 0x00000000ul },
    { 0x400144ul, 0x00000000ul },
    { 0x400100ul, 0xfffffffful },
    { 0x400104ul, 0xfffffffful },
    { 0x401100ul, 0xfffffffful },
    { 0x4006a4ul, 0x00000001ul }
};

/* nv3TablePRAMIN's RAMHT entries for the objects bound below (handle,
 * context), and their instances' first words per depth (the _8BPP,
 * _15BPP and _32BPP tables; 15 serves 16, as rivafb does). */
static const struct v9x_pair v9x_ramht[] = {
    { 0x0800ul, 0x80000000ul }, { 0x0804ul, 0x00c20341ul },  /* ROP */
    { 0x0810ul, 0x80000001ul }, { 0x0814ul, 0x00c50342ul },  /* clip */
    { 0x0820ul, 0x80000002ul }, { 0x0824ul, 0x00c60343ul },  /* pattern */
    { 0x0900ul, 0x80000010ul }, { 0x0904ul, 0x00d10344ul },  /* pixmap */
    { 0x0910ul, 0x80000011ul }, { 0x0914ul, 0x00d00345ul },  /* blit */
    { 0x0920ul, 0x80000012ul }, { 0x0924ul, 0x00cc0346ul },  /* bitmap */
    { 0x0960ul, 0x80000016ul }, { 0x0964ul, 0x00ca034cul }   /* line */
};

static const DWORD v9x_instances[] = {
    0x3410ul, 0x3420ul, 0x3430ul, 0x3440ul, 0x3450ul, 0x3460ul, 0x34c0ul
};
static const DWORD v9x_instance_8[] = {
    0x10110203ul, 0x10110203ul, 0x1011020bul, 0x10118203ul, 0x10110203ul,
    0x10110203ul, 0x10118203ul
};
static const DWORD v9x_instance_16[] = {
    0x10110200ul, 0x10110200ul, 0x10110208ul, 0x10118200ul, 0x10110200ul,
    0x10110200ul, 0x10118200ul
};
static const DWORD v9x_instance_32[] = {
    0x10110201ul, 0x10110201ul, 0x10110209ul, 0x10118201ul, 0x10110201ul,
    0x10110201ul, 0x10118201ul
};

/* riva_tbl.h RivaTableFIFO: subchannel and handle, without the D3D one. */
static const struct v9x_pair v9x_bindings[] = {
    { V9X_SUB_ROP, 0x80000000ul },
    { V9X_SUB_CLIP, 0x80000001ul },
    { V9X_SUB_PATTERN, 0x80000002ul },
    { V9X_SUB_PIXMAP, 0x80000010ul },
    { V9X_SUB_BLIT, 0x80000011ul },
    { V9X_SUB_BITMAP, 0x80000012ul },
    { V9X_SUB_LINE, 0x80000016ul }
};

static struct v9x_n32d_request v9x_request;
static struct v9x_n32d_result v9x_result;
static BYTE v9x_expected[V9X_N32D_BLOCK * 4u];

/* Op indexes the report reads back. */
static DWORD v9x_at_ramht_before;
static DWORD v9x_at_object_before;
static DWORD v9x_at_enable_before;
static DWORD v9x_at_pfb_before;
static DWORD v9x_at_idle_bind;
static DWORD v9x_at_idle_fill;
static DWORD v9x_at_idle_blit;
static DWORD v9x_at_after_first;

static const DWORD v9x_after_offsets[] = {
    0x000100ul, 0x000200ul, 0x002100ul, 0x003210ul, 0x003270ul,
    0x400100ul, 0x400104ul, 0x4006b0ul, 0x800010ul, 0x100200ul
};
static const char * const v9x_after_names[] = {
    "PMC_INTR_0", "PMC_ENABLE", "PFIFO_INTR_0", "CACHE1_PUT",
    "CACHE1_GET", "PGRAPH_INTR_0", "PGRAPH_INTR_1", "PGRAPH_STATUS",
    "USER_FREE", "PFB_CONFIG_0"
};
#define V9X_AFTER_COUNT (sizeof(v9x_after_offsets) / sizeof(v9x_after_offsets[0]))

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
    WritePrivateProfileStringA(section, key, value, V9X_N32D_PATH);
}

static void v9x_key(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_hex(text, value, 8);
    v9x_text(section, key, text);
}

static void v9x_number(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_decimal(text, value);
    v9x_text(section, key, text);
}

/* Appends an op; returns its index. */
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

static void v9x_write(DWORD offset, DWORD value)
{
    v9x_op(V9X_OP_MMIO_WRITE, offset, value, 0ul);
}

static void v9x_method(DWORD subchannel, DWORD method, DWORD value)
{
    v9x_write(V9X_USER + subchannel * V9X_SUBCHANNEL_STEP + method, value);
}

static DWORD v9x_read(DWORD offset)
{
    return v9x_op(V9X_OP_MMIO_READ, offset, 0ul, 0ul);
}

static DWORD v9x_pattern_byte(DWORD byte_index)
{
    DWORD dword;

    dword = V9X_SEED + (byte_index >> 2) * V9X_PATTERN_STEP;
    return (dword >> ((byte_index & 3ul) * 8u)) & 0xfful;
}

/*
 * The 16 bpp run's second variant: every surface Y16 (SURF_CONFIG format 0)
 * and every object Y16_A16Y16 (colour format 4), nv3_pgraph.xml's raw
 * 16-bit formats, in place of rivafb's X1R5G5B5. The first run on
 * M-7V636C (2026-11-02, boot 11) showed X1R5G5B5 forcing bit 15 on in both
 * fills and blits, which a 5:6:5 desktop cannot take.
 */
#define V9X_SURF_CONFIG_Y16     0x00000000ul
#define V9X_COLOR_FORMAT_MASK   0x00000007ul
#define V9X_COLOR_FORMAT_Y16    0x00000004ul

/* The bring-up and the test for one depth, as one op list. */
static void v9x_build(DWORD bytes, DWORD pitch, DWORD colour_a,
                      DWORD colour_b, int y16)
{
    const DWORD *instance;
    DWORD surf_config;
    DWORD depth_field;
    DWORD index;
    DWORD word;

    v9x_request.count = 0ul;
    if (bytes == 1ul) {
        instance = v9x_instance_8;
        surf_config = 0x00001111ul;
    } else if (bytes == 2ul) {
        instance = v9x_instance_16;
        surf_config = y16 ? V9X_SURF_CONFIG_Y16 : 0x00002222ul;
    } else {
        instance = v9x_instance_32;
        surf_config = 0x00003333ul;
    }
    /* riva_hw.c CalcStateExt: width / 32 rounded up, bytes per pixel at
     * most 3, tiling disabled. */
    depth_field = bytes > 2ul ? 3ul : bytes;

    /* What the previous run, or the BIOS, left. */
    v9x_at_ramht_before = v9x_op(V9X_OP_RAMIN_READ, 0x0800ul, 0ul, 0ul);
    v9x_at_object_before = v9x_op(V9X_OP_RAMIN_READ, 0x3410ul, 0ul, 0ul);
    v9x_at_enable_before = v9x_read(V9X_PMC_ENABLE);
    v9x_at_pfb_before = v9x_read(V9X_PFB_CONFIG_0);

    v9x_op(V9X_OP_SAVE, 0ul, 0ul, 0ul);
    v9x_op(V9X_OP_VRAM_PATTERN, 0ul, (V9X_LINES * pitch) / 4ul, V9X_SEED);

    /* RivaTablePMC, RivaTablePTIMER. */
    v9x_write(V9X_PMC_INTR_EN, 0x00000000ul);
    v9x_write(V9X_PMC_ENABLE, 0xffff00fful);
    v9x_write(V9X_PMC_ENABLE, 0xfffffffful);
    v9x_write(V9X_PTIMER_NUM, 0x00000008ul);
    v9x_write(V9X_PTIMER_DEN, 0x00000003ul);
    v9x_write(V9X_PTIMER_INTR_EN, 0x00000000ul);
    v9x_write(V9X_PTIMER_INTR, 0xfffffffful);

    /* PFB before RAMIN, as LoadStateExt says it must be. */
    v9x_write(V9X_PFB_CONFIG_0, ((V9X_WIDTH + 31ul) / 32ul) |
                                (depth_field << 8) | 0x00001000ul);

    for (index = 0ul; index < sizeof(v9x_pfifo) / sizeof(v9x_pfifo[0]);
         ++index) {
        v9x_write(v9x_pfifo[index].offset, v9x_pfifo[index].value);
    }

    /* RAMIN: a clean RAMHT and object area, then the entries. */
    v9x_op(V9X_OP_RAMIN_FILL, 0x0000ul, 0x1000ul / 4ul, 0ul);
    v9x_op(V9X_OP_RAMIN_FILL, 0x3400ul, 0x0100ul / 4ul, 0ul);
    for (index = 0ul; index < sizeof(v9x_ramht) / sizeof(v9x_ramht[0]);
         ++index) {
        v9x_op(V9X_OP_RAMIN_WRITE, v9x_ramht[index].offset,
               v9x_ramht[index].value, 0ul);
    }
    for (index = 0ul;
         index < sizeof(v9x_instances) / sizeof(v9x_instances[0]);
         ++index) {
        word = instance[index];
        if (y16) {
            word = (word & ~V9X_COLOR_FORMAT_MASK) | V9X_COLOR_FORMAT_Y16;
        }
        v9x_op(V9X_OP_RAMIN_WRITE, v9x_instances[index], word, 0ul);
    }

    for (index = 0ul; index < sizeof(v9x_pgraph) / sizeof(v9x_pgraph[0]);
         ++index) {
        v9x_write(v9x_pgraph[index].offset, v9x_pgraph[index].value);
    }
    v9x_write(V9X_PGRAPH_SURF_CFG, surf_config);
    for (index = 0ul; index < 4ul; ++index) {
        v9x_write(V9X_PGRAPH_SURF_OFF + index * 4ul, V9X_SURFACE_OFFSET);
        v9x_write(V9X_PGRAPH_SURF_PITCH + index * 4ul, pitch);
    }

    for (index = 0ul;
         index < sizeof(v9x_bindings) / sizeof(v9x_bindings[0]); ++index) {
        v9x_method(v9x_bindings[index].offset, 0ul,
                   v9x_bindings[index].value);
    }
    v9x_op(V9X_OP_WAIT_MMIO, V9X_PGRAPH_STATUS, 1ul, 0ul);
    v9x_op(V9X_OP_DELAY, 1000ul, 0ul, 0ul);
    v9x_op(V9X_OP_LATCH_FREE, 0ul, 0ul, 0ul);
    v9x_at_idle_bind = v9x_op(V9X_OP_WAIT_IDLE, 0ul, 0ul, 0ul);

    /* rivafb riva_setup_accel and riva_set_rop_solid. */
    v9x_method(V9X_SUB_CLIP, V9X_M_CLIP_POINT, 0ul);
    v9x_method(V9X_SUB_CLIP, V9X_M_CLIP_SIZE, V9X_WIDTH | (V9X_LINES << 16));
    v9x_method(V9X_SUB_PATTERN, V9X_M_PATTERN_COLOR0, 0xfffffffful);
    v9x_method(V9X_SUB_PATTERN, V9X_M_PATTERN_COLOR1, 0xfffffffful);
    v9x_method(V9X_SUB_PATTERN, V9X_M_PATTERN_MONO0, 0xfffffffful);
    v9x_method(V9X_SUB_PATTERN, V9X_M_PATTERN_MONO1, 0xfffffffful);
    v9x_method(V9X_SUB_ROP, V9X_M_ROP3, V9X_ROP_SRCCOPY);

    /* rivafb_fillrect: x in the high half of the point. */
    v9x_method(V9X_SUB_BITMAP, V9X_M_RECT_COLOR, colour_a);
    v9x_method(V9X_SUB_BITMAP, V9X_M_RECT_POINT,
               (V9X_FILL_A_X << 16) | V9X_FILL_A_Y);
    v9x_method(V9X_SUB_BITMAP, V9X_M_RECT_SIZE,
               (V9X_FILL_A_W << 16) | V9X_FILL_A_H);
    v9x_method(V9X_SUB_BITMAP, V9X_M_RECT_COLOR, colour_b);
    v9x_method(V9X_SUB_BITMAP, V9X_M_RECT_POINT,
               (V9X_FILL_B_X << 16) | V9X_FILL_B_Y);
    v9x_method(V9X_SUB_BITMAP, V9X_M_RECT_SIZE,
               (V9X_FILL_B_W << 16) | V9X_FILL_B_H);
    v9x_at_idle_fill = v9x_op(V9X_OP_WAIT_IDLE, 0ul, 0ul, 0ul);

    /* rivafb_copyarea: y in the high half. */
    v9x_method(V9X_SUB_BLIT, V9X_M_BLIT_SRC,
               (V9X_BLIT_SY << 16) | V9X_BLIT_SX);
    v9x_method(V9X_SUB_BLIT, V9X_M_BLIT_DST,
               (V9X_BLIT_DY << 16) | V9X_BLIT_DX);
    v9x_method(V9X_SUB_BLIT, V9X_M_BLIT_SIZE,
               (V9X_BLIT_H << 16) | V9X_BLIT_W);
    v9x_at_idle_blit = v9x_op(V9X_OP_WAIT_IDLE, 0ul, 0ul, 0ul);

    v9x_op(V9X_OP_VRAM_BLOCK, 0ul, (V9X_LINES * pitch) / 4ul, 0ul);
    v9x_at_after_first = v9x_request.count;
    for (index = 0ul; index < V9X_AFTER_COUNT; ++index) {
        v9x_read(v9x_after_offsets[index]);
    }
    v9x_op(V9X_OP_RESTORE, 0ul, 0ul, 0ul);
}

static void v9x_put_pixel(DWORD bytes, DWORD pitch, DWORD x, DWORD y,
                          DWORD colour)
{
    DWORD at;
    DWORD byte;

    at = y * pitch + x * bytes;
    for (byte = 0ul; byte < bytes; ++byte) {
        v9x_expected[at + byte] = (BYTE)(colour >> (byte * 8u));
    }
}

static void v9x_model(DWORD bytes, DWORD pitch, DWORD colour_a,
                      DWORD colour_b)
{
    DWORD total;
    DWORD at;
    DWORD x;
    DWORD y;
    DWORD byte;

    total = V9X_LINES * pitch;
    for (at = 0ul; at < total; ++at) {
        v9x_expected[at] = (BYTE)v9x_pattern_byte(at);
    }
    for (y = 0ul; y < V9X_FILL_A_H; ++y) {
        for (x = 0ul; x < V9X_FILL_A_W; ++x) {
            v9x_put_pixel(bytes, pitch, V9X_FILL_A_X + x, V9X_FILL_A_Y + y,
                          colour_a);
        }
    }
    for (y = 0ul; y < V9X_FILL_B_H; ++y) {
        for (x = 0ul; x < V9X_FILL_B_W; ++x) {
            v9x_put_pixel(bytes, pitch, V9X_FILL_B_X + x, V9X_FILL_B_Y + y,
                          colour_b);
        }
    }
    /* Source and destination do not overlap, so a straight copy. */
    for (y = 0ul; y < V9X_BLIT_H; ++y) {
        for (x = 0ul; x < V9X_BLIT_W * bytes; ++x) {
            byte = v9x_expected[(V9X_BLIT_SY + y) * pitch +
                                V9X_BLIT_SX * bytes + x];
            v9x_expected[(V9X_BLIT_DY + y) * pitch + V9X_BLIT_DX * bytes +
                         x] = (BYTE)byte;
        }
    }
}

static int v9x_inside(DWORD x, DWORD y, DWORD rx, DWORD ry, DWORD rw,
                      DWORD rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

/* Region of a pixel: 0 fill A, 1 fill B, 2 blit destination, 3 the rest
 * (the blit source outside the fills included). */
static DWORD v9x_region(DWORD x, DWORD y)
{
    if (v9x_inside(x, y, V9X_BLIT_DX, V9X_BLIT_DY, V9X_BLIT_W, V9X_BLIT_H)) {
        return 2ul;
    }
    if (v9x_inside(x, y, V9X_FILL_A_X, V9X_FILL_A_Y, V9X_FILL_A_W,
                   V9X_FILL_A_H)) {
        return 0ul;
    }
    if (v9x_inside(x, y, V9X_FILL_B_X, V9X_FILL_B_Y, V9X_FILL_B_W,
                   V9X_FILL_B_H)) {
        return 1ul;
    }
    return 3ul;
}

static DWORD v9x_pixel(const BYTE *base, DWORD bytes, DWORD pitch, DWORD x,
                       DWORD y)
{
    DWORD value;
    DWORD byte;
    DWORD at;

    value = 0ul;
    at = y * pitch + x * bytes;
    for (byte = 0ul; byte < bytes; ++byte) {
        value |= (DWORD)base[at + byte] << (byte * 8u);
    }
    return value;
}

/*
 * Mismatches with the format's unused bits ignored: bit 15 at 16 bpp
 * (X1R5G5B5) and bits 31:24 at 32 bpp (X8R8G8B8). The first all-depth run
 * on M-7V636C (boot 11) saw the engine change exactly those bits: bit 15
 * set at 16 bpp, the top byte's bits 7:6 forced to 10b at 32 bpp. A
 * display in 5:5:5 or X8R8G8B8 does not show them.
 */
static DWORD v9x_compare_masked(DWORD bytes, DWORD pitch)
{
    const BYTE *got;
    DWORD mask;
    DWORD bad;
    DWORD x;
    DWORD y;

    if (bytes == 2ul) {
        mask = 0x00007ffful;
    } else if (bytes == 4ul) {
        mask = 0x00fffffful;
    } else {
        mask = 0x000000fful;
    }
    got = (const BYTE *)v9x_result.block;
    bad = 0ul;
    for (y = 0ul; y < V9X_LINES; ++y) {
        for (x = 0ul; x < V9X_WIDTH; ++x) {
            if ((v9x_pixel(v9x_expected, bytes, pitch, x, y) & mask) !=
                (v9x_pixel(got, bytes, pitch, x, y) & mask)) {
                ++bad;
            }
        }
    }
    return bad;
}

static int v9x_compare(const char *section, DWORD bytes, DWORD pitch)
{
    static const char * const names[4] = {
        "FillA", "FillB", "BlitDst", "Outside"
    };
    const BYTE *got;
    DWORD bad[4];
    DWORD first_got[4];
    DWORD first_want[4];
    DWORD first_at[4];
    DWORD x;
    DWORD y;
    DWORD region;
    DWORD want;
    DWORD have;
    char key[32];

    got = (const BYTE *)v9x_result.block;
    for (region = 0ul; region < 4ul; ++region) {
        bad[region] = 0ul;
        first_got[region] = 0ul;
        first_want[region] = 0ul;
        first_at[region] = 0ul;
    }
    for (y = 0ul; y < V9X_LINES; ++y) {
        for (x = 0ul; x < V9X_WIDTH; ++x) {
            want = v9x_pixel(v9x_expected, bytes, pitch, x, y);
            have = v9x_pixel(got, bytes, pitch, x, y);
            if (want == have) {
                continue;
            }
            region = v9x_region(x, y);
            if (bad[region] == 0ul) {
                first_got[region] = have;
                first_want[region] = want;
                first_at[region] = (y << 16) | x;
            }
            ++bad[region];
        }
    }
    for (region = 0ul; region < 4ul; ++region) {
        lstrcpyA(key, names[region]);
        lstrcatA(key, "Bad");
        v9x_number(section, key, bad[region]);
        if (bad[region] != 0ul) {
            lstrcpyA(key, names[region]);
            lstrcatA(key, "FirstAtYX");
            v9x_key(section, key, first_at[region]);
            lstrcpyA(key, names[region]);
            lstrcatA(key, "FirstWant");
            v9x_key(section, key, first_want[region]);
            lstrcpyA(key, names[region]);
            lstrcatA(key, "FirstGot");
            v9x_key(section, key, first_got[region]);
        }
    }
    return bad[0] == 0ul && bad[1] == 0ul && bad[2] == 0ul &&
           bad[3] == 0ul;
}

/* One depth: build, run, compare, report. Returns nonzero on a pass. */
static int v9x_run_depth(HANDLE device, DWORD bits, int y16)
{
    char section[12];
    DWORD bytes;
    DWORD pitch;
    DWORD colour_a;
    DWORD colour_b;
    DWORD returned;
    DWORD index;
    BOOL ok;
    int pass;

    bytes = bits / 8ul;
    pitch = V9X_WIDTH * bytes;
    if (bits == 8ul) {
        colour_a = 0x0000005aul;
        colour_b = 0x000000c3ul;
    } else if (bits == 16ul) {
        /* 5:5:5 white-ish magenta with bit 15 clear, and a 5:6:5
         * magenta with bit 15 set. */
        colour_a = 0x00007c1ful;
        colour_b = 0x0000f81ful;
    } else {
        colour_a = 0x00ff00fful;
        colour_b = 0xff123456ul;
    }
    lstrcpyA(section, "D");
    v9x_decimal(section + 1, bits);
    if (y16) {
        lstrcatA(section, "Y16");
    }

    v9x_build(bytes, pitch, colour_a, colour_b, y16);
    v9x_number(section, "Ops", v9x_request.count);
    v9x_key(section, "ColourA", colour_a);
    v9x_key(section, "ColourB", colour_b);
    v9x_number(section, "Pitch", pitch);

    returned = 0ul;
    ok = DeviceIoControl(device, V9X_N32D_RUN, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_N32D_MAGIC) {
        v9x_text(section, "Result", "DIOC-FAILED");
        return 0;
    }
    v9x_key(section, "Status", v9x_result.status);
    v9x_key(section, "Boot0", v9x_result.boot0);
    v9x_number(section, "Executed", v9x_result.executed);
    if ((v9x_result.status & V9X_ST_COMPLETE) == 0ul) {
        v9x_number(section, "RefusedIndex", v9x_result.refused_index);
        v9x_text(section, "Result", "REFUSED-OR-UNMAPPED");
        return 0;
    }
    v9x_key(section, "RamhtBefore",
            v9x_result.op_results[v9x_at_ramht_before]);
    v9x_key(section, "ObjectBefore",
            v9x_result.op_results[v9x_at_object_before]);
    v9x_key(section, "EnableBefore",
            v9x_result.op_results[v9x_at_enable_before]);
    v9x_key(section, "PfbConfigBefore",
            v9x_result.op_results[v9x_at_pfb_before]);
    v9x_key(section, "FreeLatched", v9x_result.empty);
    v9x_key(section, "IdleBindReads",
            v9x_result.op_results[v9x_at_idle_bind]);
    v9x_key(section, "IdleFillReads",
            v9x_result.op_results[v9x_at_idle_fill]);
    v9x_key(section, "IdleBlitReads",
            v9x_result.op_results[v9x_at_idle_blit]);
    for (index = 0ul; index < V9X_AFTER_COUNT; ++index) {
        v9x_key(section, v9x_after_names[index],
                v9x_result.op_results[v9x_at_after_first + index]);
    }
    if ((v9x_result.status & V9X_ST_TIMEOUT) != 0ul) {
        v9x_text(section, "Timeout", "yes");
    }

    v9x_model(bytes, pitch, colour_a, colour_b);
    pass = v9x_compare(section, bytes, pitch);
    v9x_number(section, "MaskedBad", v9x_compare_masked(bytes, pitch));
    v9x_text(section, "Result", pass ? "PASS" : "FAIL");
    return pass;
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

/* 800x600 at bits, for this process; 0 on failure. */
static int v9x_set_mode(DWORD bits)
{
    DEVMODEA mode;
    BYTE *clear;
    DWORD at;

    /* No C runtime here, so no memset behind ZeroMemory. */
    clear = (BYTE *)&mode;
    for (at = 0ul; at < sizeof(mode); ++at) {
        clear[at] = 0u;
    }
    mode.dmSize = sizeof(mode);
    mode.dmBitsPerPel = bits;
    mode.dmPelsWidth = V9X_WIDTH;
    mode.dmPelsHeight = 600ul;
    mode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
    if (ChangeDisplaySettingsA(&mode, 0) != DISP_CHANGE_SUCCESSFUL) {
        return 0;
    }
    Sleep(2000ul);
    return 1;
}

static DWORD v9x_current_bits(void)
{
    HDC screen;
    DWORD bits;

    screen = GetDC(0);
    bits = (DWORD)GetDeviceCaps(screen, BITSPIXEL) *
           (DWORD)GetDeviceCaps(screen, PLANES);
    ReleaseDC(0, screen);
    return bits;
}

void __stdcall V9xNv32dEntry(void)
{
    static const DWORD others[2] = { 8ul, 32ul };
    HANDLE device;
    DWORD bits;
    DWORD index;
    int all_pass;
    int switched;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_N32D_PATH);
    v9x_text("Report", "Tool", "V9XN32D");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Access", "nv3-2d-write");

    if ((DWORD)GetSystemMetrics(SM_CXSCREEN) != V9X_WIDTH) {
        v9x_text("Report", "Result", "NOT-800-WIDE");
        WritePrivateProfileStringA(0, 0, 0, V9X_N32D_PATH);
        ExitProcess(1u);
    }
    bits = v9x_current_bits();
    v9x_number("Report", "DesktopBits", bits);
    if (bits != 8ul && bits != 16ul && bits != 32ul) {
        v9x_text("Report", "Result", "UNSUPPORTED-DEPTH");
        WritePrivateProfileStringA(0, 0, 0, V9X_N32D_PATH);
        ExitProcess(1u);
    }

    device = CreateFileA(V9X_N32D_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        WritePrivateProfileStringA(0, 0, 0, V9X_N32D_PATH);
        ExitProcess(2u);
    }

    all_pass = v9x_run_depth(device, bits, 0);
    if (bits == 16ul && !v9x_run_depth(device, bits, 1)) {
        all_pass = 0;
    }
    switched = 0;
    if (v9x_switch_given("/all")) {
        for (index = 0ul; index < 2ul; ++index) {
            if (others[index] == bits) {
                continue;
            }
            if (!v9x_set_mode(others[index])) {
                v9x_number("Report", "ModeSetFailed", others[index]);
                all_pass = 0;
                continue;
            }
            switched = 1;
            if (!v9x_run_depth(device, others[index], 0)) {
                all_pass = 0;
            }
        }
        if (switched) {
            ChangeDisplaySettingsA(0, 0);
            Sleep(2000ul);
        }
    }
    CloseHandle(device);

    v9x_text("Report", "Result", all_pass ? "PASS" : "FAIL");
    WritePrivateProfileStringA(0, 0, 0, V9X_N32D_PATH);
    ExitProcess(all_pass ? 0u : 3u);
}
