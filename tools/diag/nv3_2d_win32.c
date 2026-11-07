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
 * /d3d adds the D3D object and a short DMA object and draws one triangle
 * instead (Phase 7; see v9x_run_d3d). /d3dstate draws one case per piece
 * of 3D state and writes the raw pixels to C:\V9XDIAG\N3Dnn.BIN (see
 * v9x_d3d_cases). /d3dmip does the same for mipmap chains, files
 * N3D41.BIN on (v9x_d3d_mip_cases).
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
#define V9X_OP_VRAM_WRITE    3ul
#define V9X_OP_VRAM_FILL     5ul
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
/* The blit's corners: apart by default; /overlap moves them so source
 * and destination overlap, down-right and then up-left. */
static DWORD v9x_blit_sx = 4ul;
static DWORD v9x_blit_sy = 1ul;
static DWORD v9x_blit_dx = 120ul;
static DWORD v9x_blit_dy = 1ul;
/*
 * /surfaces: PGRAPH surface 0 this many lines below surfaces 1-3. rivafb's
 * surface objects give the destination (class 58h, NV3_SURF_DST in
 * envytools' graph intro) surface index 0 and the blit source (59h) index
 * 1, so fills and the blit destination should move down and the blit
 * source should not.
 */
static DWORD v9x_dst_shift = 0ul;
/* /d3dstate: lines each surface sits below the window's start, to tell
 * which one the D3D object draws into (the probes before boot 21 put all
 * four in one place). */
static DWORD v9x_surface_shift[4];
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
static BYTE v9x_blit_source[V9X_BLIT_H * V9X_BLIT_W * 4ul];

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

/*
 * /alpha, the third 16 bpp variant: every object's grobj ALPHA bit (3) set,
 * nv3_pgraph.xml's X1R5G5B5_A1R5G5B5 read as A1R5G5B5, the rest rivafb's.
 * The question (2026-11-07, boot 45): whether bit 15 then carries the
 * fill colour's and the blit source's own bit instead of 1, which the
 * HAL's 16 bpp fills need for DirectDraw's emulated colour keys.
 */
#define V9X_VARIANT_RIVAFB      0
#define V9X_VARIANT_Y16         1
#define V9X_VARIANT_ALPHA       2
#define V9X_GROBJ_ALPHA         0x00000008ul

/*
 * /d3d: rivafb's D3D pieces, which the 2D runs leave out. RAMHT entry
 * 80000013h with context 00D70347h (type 17h, the D3D textured triangle;
 * instance 347h), its instance words 10419208h (X1R5G5B5, zeta enabled,
 * destination surface mask 4: surface 2, zeta surface 3, per
 * nv3_pgraph.xml's grobj options) and 140h (the DMA object at RAMIN 1400h),
 * and the binding to subchannel 7 (RivaTableFIFO; riva_hw.c's Tri03 at
 * USER + E000h). rivafb's DMA object maps all 8 MiB with 800h page entries,
 * which run over RAMRO and RAMFC at 2000h; this one maps only the scratch
 * window's 16 pages, so texture offsets are from the window's start.
 */
#define V9X_SUB_D3D            7ul
#define V9X_D3D_HANDLE         0x80000013ul
#define V9X_D3D_RAMHT          0x0930ul
#define V9X_D3D_CONTEXT        0x00d70347ul
#define V9X_D3D_INSTANCE       0x3470ul
#define V9X_D3D_OPTIONS        0x10419208ul
#define V9X_D3D_DMA_INSTANCE   0x0140ul
#define V9X_D3D_DMA            0x1400ul
#define V9X_D3D_DMA_FLAGS      0x00010000ul
#define V9X_D3D_DMA_PAGES      16ul
#define V9X_D3D_PAGE           0x1000ul
#define V9X_D3D_PTE_PRESENT_RW 0x00000003ul
/* Within the scratch window: the zeta surface (16 lines of 1600 bytes, to
 * E400h) and the texture after it. Runs one to six put the texture at
 * C000h, inside the zeta surface, whose clear zeroed it: every video
 * memory texel read 0 and the bug looked like the DMA object's. */
#define V9X_D3D_ZETA           0x8000ul
#define V9X_D3D_TEXTURE        0xf000ul
#define V9X_D3D_LINES          16ul
static int v9x_d3d;
/* The DMA object's flags word and its first page entry's address, varied
 * by the fourth /d3d run (see v9x_d3d_fog below). */
static DWORD v9x_d3d_dma_flags = V9X_D3D_DMA_FLAGS;
static DWORD v9x_d3d_pte_base = V9X_SURFACE_OFFSET;
/* The D3D object's DMA instance word; /d3dstate's last case points it at
 * the whole-memory object it writes at RAMIN 4000h. */
static DWORD v9x_d3d_dma_word = V9X_D3D_DMA_INSTANCE;

/* The bring-up for one depth, appended to the op list. */
static void v9x_bring_up(DWORD bytes, DWORD pitch, int y16)
{
    const DWORD *instance;
    DWORD surf_config;
    DWORD depth_field;
    DWORD index;
    DWORD word;
    DWORD lines;

    lines = v9x_d3d ? V9X_D3D_LINES : V9X_LINES;
    if (bytes == 1ul) {
        instance = v9x_instance_8;
        surf_config = 0x00001111ul;
    } else if (bytes == 2ul) {
        instance = v9x_instance_16;
        surf_config = y16 == V9X_VARIANT_Y16 ? V9X_SURF_CONFIG_Y16 :
                      0x00002222ul;
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
    v9x_op(V9X_OP_VRAM_PATTERN, 0ul, (lines * pitch) / 4ul, V9X_SEED);

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
        if (y16 == V9X_VARIANT_Y16) {
            word = (word & ~V9X_COLOR_FORMAT_MASK) | V9X_COLOR_FORMAT_Y16;
        } else if (y16 == V9X_VARIANT_ALPHA) {
            word |= V9X_GROBJ_ALPHA;
        }
        v9x_op(V9X_OP_RAMIN_WRITE, v9x_instances[index], word, 0ul);
    }
    if (v9x_d3d) {
        v9x_op(V9X_OP_RAMIN_WRITE, V9X_D3D_RAMHT, V9X_D3D_HANDLE, 0ul);
        v9x_op(V9X_OP_RAMIN_WRITE, V9X_D3D_RAMHT + 4ul, V9X_D3D_CONTEXT,
               0ul);
        v9x_op(V9X_OP_RAMIN_WRITE, V9X_D3D_INSTANCE, V9X_D3D_OPTIONS, 0ul);
        v9x_op(V9X_OP_RAMIN_WRITE, V9X_D3D_INSTANCE + 4ul,
               v9x_d3d_dma_word, 0ul);
        /* nv3TablePRAMIN's DMA object: flags, limit, then page entries
         * (riva_hw.c writes (i << 12) | 3), here the scratch window's. */
        v9x_op(V9X_OP_RAMIN_FILL, V9X_D3D_DMA,
               (8ul + V9X_D3D_DMA_PAGES * 4ul) / 4ul, 0ul);
        v9x_op(V9X_OP_RAMIN_WRITE, V9X_D3D_DMA, v9x_d3d_dma_flags, 0ul);
        v9x_op(V9X_OP_RAMIN_WRITE, V9X_D3D_DMA + 4ul,
               V9X_D3D_DMA_PAGES * V9X_D3D_PAGE - 1ul, 0ul);
        for (index = 0ul; index < V9X_D3D_DMA_PAGES; ++index) {
            v9x_op(V9X_OP_RAMIN_WRITE, V9X_D3D_DMA + 8ul + index * 4ul,
                   (v9x_d3d_pte_base + index * V9X_D3D_PAGE) |
                   V9X_D3D_PTE_PRESENT_RW, 0ul);
        }
    }

    for (index = 0ul; index < sizeof(v9x_pgraph) / sizeof(v9x_pgraph[0]);
         ++index) {
        v9x_write(v9x_pgraph[index].offset, v9x_pgraph[index].value);
    }
    v9x_write(V9X_PGRAPH_SURF_CFG, surf_config);
    /* Surface 0, the destination, v9x_dst_shift lines below the others;
     * for /d3d, surface 3, the zeta surface, in its own part of the
     * window. */
    for (index = 0ul; index < 4ul; ++index) {
        v9x_write(V9X_PGRAPH_SURF_OFF + index * 4ul,
                  V9X_SURFACE_OFFSET +
                  (index == 0ul ? v9x_dst_shift * pitch : 0ul) +
                  v9x_surface_shift[index] * pitch +
                  (v9x_d3d && index == 3ul ? V9X_D3D_ZETA : 0ul));
        v9x_write(V9X_PGRAPH_SURF_PITCH + index * 4ul, pitch);
    }

    for (index = 0ul;
         index < sizeof(v9x_bindings) / sizeof(v9x_bindings[0]); ++index) {
        v9x_method(v9x_bindings[index].offset, 0ul,
                   v9x_bindings[index].value);
    }
    if (v9x_d3d) {
        v9x_method(V9X_SUB_D3D, 0ul, V9X_D3D_HANDLE);
    }
    v9x_op(V9X_OP_WAIT_MMIO, V9X_PGRAPH_STATUS, 1ul, 0ul);
    v9x_op(V9X_OP_DELAY, 1000ul, 0ul, 0ul);
    v9x_op(V9X_OP_LATCH_FREE, 0ul, 0ul, 0ul);
    v9x_at_idle_bind = v9x_op(V9X_OP_WAIT_IDLE, 0ul, 0ul, 0ul);

    /* rivafb riva_setup_accel and riva_set_rop_solid. */
    v9x_method(V9X_SUB_CLIP, V9X_M_CLIP_POINT, 0ul);
    v9x_method(V9X_SUB_CLIP, V9X_M_CLIP_SIZE, V9X_WIDTH | (lines << 16));
    v9x_method(V9X_SUB_PATTERN, V9X_M_PATTERN_COLOR0, 0xfffffffful);
    v9x_method(V9X_SUB_PATTERN, V9X_M_PATTERN_COLOR1, 0xfffffffful);
    v9x_method(V9X_SUB_PATTERN, V9X_M_PATTERN_MONO0, 0xfffffffful);
    v9x_method(V9X_SUB_PATTERN, V9X_M_PATTERN_MONO1, 0xfffffffful);
    v9x_method(V9X_SUB_ROP, V9X_M_ROP3, V9X_ROP_SRCCOPY);
}

/* The bring-up and the 2D test for one depth, as one op list. */
static void v9x_build(DWORD bytes, DWORD pitch, DWORD colour_a,
                      DWORD colour_b, int y16)
{
    DWORD index;

    v9x_request.count = 0ul;
    v9x_bring_up(bytes, pitch, y16);

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
               (v9x_blit_sy << 16) | v9x_blit_sx);
    v9x_method(V9X_SUB_BLIT, V9X_M_BLIT_DST,
               (v9x_blit_dy << 16) | v9x_blit_dx);
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
            v9x_put_pixel(bytes, pitch, V9X_FILL_A_X + x,
                          v9x_dst_shift + V9X_FILL_A_Y + y, colour_a);
        }
    }
    for (y = 0ul; y < V9X_FILL_B_H; ++y) {
        for (x = 0ul; x < V9X_FILL_B_W; ++x) {
            v9x_put_pixel(bytes, pitch, V9X_FILL_B_X + x,
                          v9x_dst_shift + V9X_FILL_B_Y + y, colour_b);
        }
    }
    /* Through a copy of the source, so an overlapping blit is modelled as
     * a move: the result DirectDraw and GDI expect. */
    for (y = 0ul; y < V9X_BLIT_H; ++y) {
        for (x = 0ul; x < V9X_BLIT_W * bytes; ++x) {
            v9x_blit_source[y * V9X_BLIT_W * 4ul + x] =
                v9x_expected[(v9x_blit_sy + y) * pitch +
                             v9x_blit_sx * bytes + x];
        }
    }
    for (y = 0ul; y < V9X_BLIT_H; ++y) {
        for (x = 0ul; x < V9X_BLIT_W * bytes; ++x) {
            byte = v9x_blit_source[y * V9X_BLIT_W * 4ul + x];
            v9x_expected[(v9x_dst_shift + v9x_blit_dy + y) * pitch +
                         v9x_blit_dx * bytes + x] = (BYTE)byte;
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
    if (v9x_inside(x, y, v9x_blit_dx, v9x_dst_shift + v9x_blit_dy,
                   V9X_BLIT_W, V9X_BLIT_H)) {
        return 2ul;
    }
    if (v9x_inside(x, y, V9X_FILL_A_X, v9x_dst_shift + V9X_FILL_A_Y,
                   V9X_FILL_A_W, V9X_FILL_A_H)) {
        return 0ul;
    }
    if (v9x_inside(x, y, V9X_FILL_B_X, v9x_dst_shift + V9X_FILL_B_Y,
                   V9X_FILL_B_W, V9X_FILL_B_H)) {
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
static int v9x_run_depth(HANDLE device, DWORD bits, int y16,
                         const char *suffix)
{
    char section[24];
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
    if (y16 == V9X_VARIANT_Y16) {
        lstrcatA(section, "Y16");
    } else if (y16 == V9X_VARIANT_ALPHA) {
        lstrcatA(section, "A1");
    }
    lstrcatA(section, suffix);

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

/*
 * /d3d, Phase 7's first question: does the D3D object draw a triangle, and
 * what does it write. One right triangle, (8,2) (72,2) (8,14), on a 16 bpp
 * surface (X1R5G5B5, as rivafb draws 3D) in the scratch window, read back
 * and compared with the pattern under it. The video mode is irrelevant:
 * nothing here is on screen.
 *
 * The triangle trigger is envytools' (nvhw/pgraph.c pgraph_prep_draw,
 * class 17h): each vertex is stored into the slot its specular word's low
 * nibble names when its V coordinate arrives, and then each of the
 * nibble triples 0-2 and 3-5 that holds three distinct slots draws that
 * triangle. So v0 and v1 carry one repeated slot number and draw nothing,
 * and v2 carries slots 2, 0, 1 in the first triple. The draw also needs
 * the texture offset, format, filter, config and fog colour all set (the
 * same function's valid bits 23-27). Coordinates are IEEE floats,
 * converted by the engine (nvhw/pgraph_d3d_nv3.c).
 *
 * Two runs: "Flat", a white texture under a red vertex colour, and
 * "Textured", a red and blue 4x4-block checker under white. Between them
 * they show whether the texture modulates or replaces the vertex colour.
 */
#define V9X_M_D3D_TEX_OFFSET   0x304ul
#define V9X_M_D3D_TEX_FORMAT   0x308ul
#define V9X_M_D3D_FILTER       0x30cul
#define V9X_M_D3D_FOG_COLOR    0x310ul
#define V9X_M_D3D_CONFIG       0x314ul
#define V9X_M_D3D_ALPHA        0x318ul
#define V9X_M_D3D_VERTEX       0x1000ul
/* nv3_3d.xml: A1R5G5B5, 8x8 (log2 3 for both sizes). */
#define V9X_D3D_TEX_FORMAT     0x33000000ul
/* nv3_3d.xml CONFIG: wrap U and V (1), source colour NORMAL (1), no
 * culling (1), Z ALWAYS (8), zeta writes NEVER, colour writes ALWAYS (4),
 * blend with source alpha. Inside hwtest's valid mask F77FBDF3h. */
#define V9X_D3D_CONFIG         0x04081150ul
/* ALPHA: function ALWAYS (8), reference 0. */
#define V9X_D3D_ALPHA          0x00000800ul
#define V9X_D3D_TEX_SIZE       8ul
#define V9X_FLOAT_ONE          0x3f800000ul
#define V9X_FLOAT_HALF         0x3f000000ul
/* Specular words: fog 255 (none, as Direct3D's specular alpha), slots. */
#define V9X_D3D_SPEC_V0        0x00000000ul
#define V9X_D3D_SPEC_V1        0x00111111ul
#define V9X_D3D_SPEC_V2        0x00222102ul
#define V9X_D3D_FOG_SHIFT      24u
/*
 * The second run (boot 18) drew black with ROP ADD over a cleared surface:
 * the source colour itself is 0. The third varies what could make it so:
 * the fog factor in the specular word's top byte and the fog colour (a
 * factor read the other way from Direct3D's fogs everything to the fog
 * colour). nv3_3d.xml's other source colours need CONFIG bit 9, which
 * hwtest's valid mask treats as reserved, so they are not tried.
 */
static DWORD v9x_d3d_fog = 0xfful;
static DWORD v9x_d3d_fog_color;
/*
 * The third run (boot 18): fog 255 with a white fog colour drew white
 * everywhere, so the NV3 reads that byte as the fog amount, the reverse of
 * Direct3D's specular alpha; fog 0 still drew black under a white texture,
 * so the texel fetch returns 0. rivafb's DMA object never drew a texture
 * (its Tri03 is a pointer nothing uses), so the fourth run varies it: the
 * flags word's bits 24-25 (nv3_pgraph.xml DMA_FLAGS UNK24, a target by
 * the look of NV4's objects) and page entries holding the framebuffer's bus
 * address in place of video memory offsets. A target that reads system
 * memory reads it, nothing more: the texture is only read. The two
 * variables are declared with v9x_d3d, which the bring-up reads.
 */
#define V9X_D3D_SAMPLES        5u
/*
 * The first run on M-7V636C (boot 18) rasterised the triangle and wrote
 * the destination back unchanged but for bit 15, cleared, in both runs: a
 * blend whose source factor was zero. The variants after it clear the
 * surface to 0 first and change CONFIG's blend: ROP ADD (bit 28) shows the
 * source whatever its alpha; DST_BLEND ZERO (bit 30) shows the source
 * scaled by its factor.
 */
#define V9X_D3D_ROP_ADD        0x10000000ul
#define V9X_D3D_DST_ZERO       0x40000000ul
static DWORD v9x_d3d_config = V9X_D3D_CONFIG;
static int v9x_d3d_clear;

static const DWORD v9x_d3d_sample_x[V9X_D3D_SAMPLES] = { 12ul, 30ul, 9ul, 60ul, 100ul };
static const DWORD v9x_d3d_sample_y[V9X_D3D_SAMPLES] = { 3ul, 4ul, 12ul, 2ul, 5ul };
static DWORD v9x_at_d3d_after;

/* A whole number below 2^23 as IEEE single bits; no floating point here,
 * so no C runtime. */
static DWORD v9x_float_of(DWORD value)
{
    DWORD exponent;

    if (value == 0ul) {
        return 0ul;
    }
    exponent = 0ul;
    while ((value >> (exponent + 1ul)) != 0ul) {
        ++exponent;
    }
    return ((127ul + exponent) << 23) |
           ((value << (23ul - exponent)) & 0x007ffffful);
}

static void v9x_d3d_vertex(DWORD specular, DWORD colour, DWORD x, DWORD y,
                           DWORD u, DWORD v)
{
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x00ul, specular);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x04ul, colour);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x08ul, v9x_float_of(x));
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x0cul, v9x_float_of(y));
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x10ul, V9X_FLOAT_HALF);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x14ul, V9X_FLOAT_ONE);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x18ul, u);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x1cul, v);
}

static const DWORD v9x_d3d_after_offsets[] = {
    0x400100ul, 0x400104ul, 0x4006b0ul, 0x4006b4ul, 0x4006b8ul, 0x4006bcul,
    0x400644ul, 0x4006c8ul, 0x002100ul, 0x800010ul,
    0x401100ul, 0x401210ul, 0x401220ul, 0x401230ul, 0x401240ul,
    0x401250ul, 0x401270ul, 0x401280ul, 0x401800ul
};
static const char * const v9x_d3d_after_names[] = {
    "PGRAPH_INTR_0", "PGRAPH_INTR_1", "PGRAPH_STATUS", "TRAPPED_ADDR",
    "TRAPPED_DATA", "TRAPPED_GINST", "D3D_CONFIG", "D3D_ALPHA",
    "PFIFO_INTR_0", "USER_FREE",
    "DMA_INTR", "DMA_FLAGS", "DMA_LIMIT", "DMA_PTE", "DMA_PTE_TAG",
    "DMA_ADDR_VIRT_ADJ", "DMA_BYTES", "DMA_INST", "DMA_OFFSET_0"
};
#define V9X_D3D_AFTER_COUNT \
    (sizeof(v9x_d3d_after_offsets) / sizeof(v9x_d3d_after_offsets[0]))

static void v9x_build_d3d(DWORD pitch, int textured)
{
    DWORD texel;
    DWORD row;
    DWORD column;
    DWORD index;
    DWORD colour;

    v9x_request.count = 0ul;
    v9x_bring_up(2ul, pitch, 0);

    /* The texture, two texels a dword; the zeta surface cleared. */
    for (row = 0ul; row < V9X_D3D_TEX_SIZE; ++row) {
        for (column = 0ul; column < V9X_D3D_TEX_SIZE; column += 2ul) {
            if (!textured) {
                texel = 0xfffffffful;
            } else if (((row / 4ul) ^ (column / 4ul)) & 1ul) {
                texel = 0x801f801ful;  /* blue, alpha 1 */
            } else {
                texel = 0xfc00fc00ul;  /* red, alpha 1 */
            }
            v9x_op(V9X_OP_VRAM_WRITE, V9X_D3D_TEXTURE +
                   (row * V9X_D3D_TEX_SIZE + column) * 2ul, texel, 0ul);
        }
    }
    v9x_op(V9X_OP_VRAM_FILL, V9X_D3D_ZETA, (V9X_D3D_LINES * pitch) / 4ul,
           0ul);
    if (v9x_d3d_clear) {
        v9x_op(V9X_OP_VRAM_FILL, 0ul, (V9X_D3D_LINES * pitch) / 4ul, 0ul);
    }

    v9x_method(V9X_SUB_D3D, V9X_M_D3D_TEX_OFFSET, V9X_D3D_TEXTURE);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_TEX_FORMAT, V9X_D3D_TEX_FORMAT);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_FILTER, 0ul);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_FOG_COLOR, v9x_d3d_fog_color);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_CONFIG, v9x_d3d_config);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_ALPHA, V9X_D3D_ALPHA);
    v9x_op(V9X_OP_WAIT_IDLE, 0ul, 0ul, 0ul);

    colour = textured ? 0xfffffffful : 0xffff0000ul;
    v9x_d3d_vertex(V9X_D3D_SPEC_V0 | (v9x_d3d_fog << V9X_D3D_FOG_SHIFT),
                   colour, 8ul, 2ul, 0ul, 0ul);
    v9x_d3d_vertex(V9X_D3D_SPEC_V1 | (v9x_d3d_fog << V9X_D3D_FOG_SHIFT),
                   colour, 72ul, 2ul, V9X_FLOAT_ONE, 0ul);
    v9x_op(V9X_OP_WAIT_IDLE, 0ul, 0ul, 0ul);
    v9x_d3d_vertex(V9X_D3D_SPEC_V2 | (v9x_d3d_fog << V9X_D3D_FOG_SHIFT),
                   colour, 8ul, 14ul, 0ul, V9X_FLOAT_ONE);
    v9x_at_idle_fill = v9x_op(V9X_OP_WAIT_IDLE, 0ul, 0ul, 0ul);

    v9x_op(V9X_OP_VRAM_BLOCK, 0ul, (V9X_D3D_LINES * pitch) / 4ul, 0ul);
    v9x_at_d3d_after = v9x_request.count;
    for (index = 0ul; index < V9X_D3D_AFTER_COUNT; ++index) {
        v9x_read(v9x_d3d_after_offsets[index]);
    }
    v9x_op(V9X_OP_RESTORE, 0ul, 0ul, 0ul);
}

/* Pixel centres inside (8,2) (72,2) (8,14), doubled to stay integral:
 * (2x+1-16)*12 + (2y+1-4)*64 <= 1536. */
static int v9x_d3d_inside(DWORD x, DWORD y)
{
    long dx = 2l * (long)x + 1l - 16l;
    long dy = 2l * (long)y + 1l - 4l;

    return dx >= 0l && dy >= 0l && dx * 12l + dy * 64l <= 1536l;
}

static int v9x_run_d3d(HANDLE device, int textured, const char *section)
{
    const BYTE *got;
    DWORD pitch;
    DWORD returned;
    DWORD x;
    DWORD y;
    DWORD have;
    DWORD want;
    DWORD changed_in;
    DWORD changed_out;
    DWORD unchanged_in;
    DWORD min_x;
    DWORD min_y;
    DWORD max_x;
    DWORD max_y;
    DWORD values[8];
    DWORD counts[8];
    DWORD distinct;
    DWORD index;
    char key[24];
    BOOL ok;

    pitch = V9X_WIDTH * 2ul;
    v9x_build_d3d(pitch, textured);
    v9x_number(section, "Ops", v9x_request.count);
    v9x_key(section, "Config", v9x_d3d_config);

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
    v9x_number(section, "Executed", v9x_result.executed);
    if ((v9x_result.status & V9X_ST_COMPLETE) == 0ul) {
        v9x_number(section, "RefusedIndex", v9x_result.refused_index);
        v9x_text(section, "Result", "REFUSED-OR-UNMAPPED");
        return 0;
    }
    if ((v9x_result.status & V9X_ST_TIMEOUT) != 0ul) {
        v9x_text(section, "Timeout", "yes");
    }
    v9x_key(section, "FreeLatched", v9x_result.empty);
    v9x_key(section, "IdleDrawReads",
            v9x_result.op_results[v9x_at_idle_fill]);
    for (index = 0ul; index < V9X_D3D_AFTER_COUNT; ++index) {
        v9x_key(section, v9x_d3d_after_names[index],
                v9x_result.op_results[v9x_at_d3d_after + index]);
    }

    got = (const BYTE *)v9x_result.block;
    changed_in = 0ul;
    changed_out = 0ul;
    unchanged_in = 0ul;
    min_x = 0xfffffffful;
    min_y = 0xfffffffful;
    max_x = 0ul;
    max_y = 0ul;
    distinct = 0ul;
    for (y = 0ul; y < V9X_D3D_LINES; ++y) {
        for (x = 0ul; x < V9X_WIDTH; ++x) {
            want = v9x_d3d_clear ? 0ul :
                   v9x_pattern_byte(y * pitch + x * 2ul) |
                   (v9x_pattern_byte(y * pitch + x * 2ul + 1ul) << 8);
            have = v9x_pixel(got, 2ul, pitch, x, y);
            if (have == want) {
                if (v9x_d3d_inside(x, y)) {
                    ++unchanged_in;
                }
                continue;
            }
            if (v9x_d3d_inside(x, y)) {
                ++changed_in;
            } else {
                ++changed_out;
            }
            min_x = x < min_x ? x : min_x;
            min_y = y < min_y ? y : min_y;
            max_x = x > max_x ? x : max_x;
            max_y = y > max_y ? y : max_y;
            for (index = 0ul; index < distinct; ++index) {
                if (values[index] == have) {
                    break;
                }
            }
            if (index < distinct) {
                ++counts[index];
            } else if (distinct < 8ul) {
                values[distinct] = have;
                counts[distinct] = 1ul;
                ++distinct;
            }
        }
    }
    v9x_number(section, "ChangedInside", changed_in);
    v9x_number(section, "ChangedOutside", changed_out);
    v9x_number(section, "UnchangedInside", unchanged_in);
    if (changed_in + changed_out != 0ul) {
        v9x_key(section, "BoxMinYX", (min_y << 16) | min_x);
        v9x_key(section, "BoxMaxYX", (max_y << 16) | max_x);
    }
    for (index = 0ul; index < distinct; ++index) {
        lstrcpyA(key, "Value");
        v9x_decimal(key + 5, index);
        v9x_key(section, key, (values[index] << 16) | counts[index]);
    }
    for (index = 0ul; index < V9X_D3D_SAMPLES; ++index) {
        lstrcpyA(key, "Sample");
        v9x_decimal(key + 6, index);
        v9x_key(section, key,
                (v9x_d3d_sample_y[index] << 24) |
                (v9x_d3d_sample_x[index] << 16) |
                v9x_pixel(got, 2ul, pitch, v9x_d3d_sample_x[index],
                          v9x_d3d_sample_y[index]));
    }
    /* A draw that touched nothing outside the triangle and most of the
     * inside; the edges' rule is what this run measures, not assumes. */
    ok = changed_in != 0ul && changed_out <= 64ul &&
         unchanged_in <= changed_in / 4ul;
    v9x_text(section, "Result", ok ? "PASS" : "FAIL");
    return ok ? 1 : 0;
}

/*
 * /d3dstate, step 2 of docs/plans/nvidia-riva128-d3d.md: the 3D state the
 * first triangle left unmeasured. Each case is one bring-up and one draw.
 * The readback is raw: the first 128 pixels of each of the 16 lines, colour
 * then zeta, 4096 bytes each, written to C:\V9XDIAG\N3Dnn.BIN and judged on
 * the host, where the models are easier to vary than here.
 *
 * The values are envytools': CONFIG's interpolator (bits 0-1, ZOH corner,
 * ZOH centre, FOH centre; hwtest allows 0-2), the write enables (0 never,
 * 1 alpha, 2 alpha and Z, 3 Z, 4 always), the comparisons (2 LESS, 5
 * GREATER, 8 ALWAYS), TEXTURE_FORMAT's colour key (the key in bits 0-15,
 * enable bit 16; hwtest's mask FF31FFFFh). nvhw/pgraph_d3d_nv3.c converts
 * x and y to 1/16 pixel, z to 24 bits inverted, and blends with a 4-bit
 * factor; the edge, Z and blend cases test those.
 *
 * The texture that tells texels apart stores, at storage index s (its
 * offset / 2), red 4 * (s & 7) and green 4 * (s >> 3): a pixel names the
 * storage texel it fetched, whatever the layout, with room for rounding.
 */
#define V9X_D3D_CFG_BASE       0x00001150ul /* wrap U, V; NORMAL; no cull */
#define V9X_D3D_INTERP_CENTER  0x00000001ul
#define V9X_D3D_INTERP_FOH     0x00000002ul
#define V9X_D3D_ZFUNC_SHIFT    16u
#define V9X_D3D_ZWRITE_SHIFT   20u
#define V9X_D3D_CWRITE_SHIFT   24u
#define V9X_CMP_LESS           2ul
#define V9X_CMP_GREATER        5ul
#define V9X_CMP_ALWAYS         8ul
#define V9X_WREN_ALPHA         1ul
#define V9X_WREN_Z             3ul
#define V9X_WREN_ALWAYS        4ul
#define V9X_D3D_KEY_ENABLE     0x00010000ul
#define V9X_D3D_TEX_X1         0x00100000ul /* TEXTURE_FORMAT colour 1 */
#define V9X_D3D_SOURCE_MASK    0x00000f00ul
#define V9X_D3D_CFG_OPAQUE     (V9X_D3D_CFG_BASE | \
                                (V9X_CMP_ALWAYS << V9X_D3D_ZFUNC_SHIFT) | \
                                (V9X_WREN_ALWAYS << V9X_D3D_CWRITE_SHIFT))
#define V9X_D3D_CFG_ZTEST(f)   (V9X_D3D_CFG_BASE | \
                                ((f) << V9X_D3D_ZFUNC_SHIFT) | \
                                (V9X_WREN_Z << V9X_D3D_ZWRITE_SHIFT) | \
                                (V9X_WREN_Z << V9X_D3D_CWRITE_SHIFT))
#define V9X_D3D_ALPHA_GREATER  ((V9X_CMP_GREATER << 8) | 0x7ful)
#define V9X_FLOAT_QUARTER      0x3e800000ul
#define V9X_FLOAT_3QUARTERS    0x3f400000ul
/* The quad's second triangle: v3 into slot 3, then slots 3, 1, 2. */
#define V9X_D3D_SPEC_V3        0x00333213ul
#define V9X_D3D_KEPT_X         128ul
#define V9X_D3D_KEPT_BYTES     (V9X_D3D_KEPT_X * 2ul * V9X_D3D_LINES)

#define V9X_TEX_WHITE          0ul
#define V9X_TEX_ID             1ul
#define V9X_TEX_ID_ALPHA       2ul /* alpha = s & 1 */
#define V9X_TEX_INDEX          3ul /* /d3dmip: s up to 191, see below */

#define V9X_SHAPE_QUAD         0ul /* (8,0)-(72,16), u and v 0 to 1 */
#define V9X_SHAPE_PERSPECTIVE  1ul /* the same, rhw 1/4 on the right */
#define V9X_SHAPE_HALF         2ul /* edges through pixel centres */
#define V9X_SHAPE_ZPAIR        3ul /* A z 1/2, then B z 1/4 and 3/4 */
#define V9X_SHAPE_QUARTER      4ul /* HALF's triangle at + 1/4 */
#define V9X_SHAPE_3QUARTERS    5ul /* and at + 3/4 */
#define V9X_SHAPE_MIPS         6ul /* four 32x16 quads, 1/2 to 8 texels/px */
#define V9X_SHAPE_MIPS_FAR     7ul /* the same at 16 to 128 */
#define V9X_SHAPE_MIPS_U       8ul /* u 1/2 to 8, v 1/2 */
#define V9X_SHAPE_MIPS_V       9ul /* v 1/2 to 8, u 1/2 */
#define V9X_SHAPE_MIPS_BIAS    10ul /* all at 1/2; SIZE_ADJUST per quad */

/* The whole-memory DMA object: 800h page entries at RAMIN 4000h, clear of
 * RAMHT (0), the small object (1400h), RAMRO (2000h), RAMFC (2200h) and
 * the instances (3400h); the texture is then addressed by its video memory
 * offset. */
#define V9X_D3D_BIG_DMA        0x4000ul
#define V9X_D3D_BIG_DMA_WORD   0x0400ul
#define V9X_D3D_BIG_PAGES      0x800ul
#define V9X_D3D_BIG_BATCH      480ul

struct v9x_d3d_case {
    const char *name;
    DWORD texture;
    DWORD shape;
    DWORD config;
    DWORD alpha;
    DWORD format_extra;
    DWORD colour;
    DWORD fill;
    DWORD zeta_fill;
    int big_dma;
    DWORD shift_surface;    /* surface index + 1 moved 8 lines; 0 none */
    DWORD texture_at;       /* window offset of the texture; 0 F000h */
    DWORD format;           /* TEXTURE_FORMAT in place of the 8x8 one; 0 */
    DWORD filter;           /* FILTER (spread, size adjust) */
};
#define V9X_D3D_SHIFT_LINES    8ul

static const struct v9x_d3d_case v9x_d3d_cases[] = {
    { "TexelZohCorner", V9X_TEX_ID, V9X_SHAPE_QUAD,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0 },
    { "TexelZohCenter", V9X_TEX_ID, V9X_SHAPE_QUAD,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD | V9X_D3D_INTERP_CENTER,
      V9X_D3D_ALPHA, 0ul, 0xfffffffful, 0ul, 0ul, 0 },
    { "TexelFoh", V9X_TEX_ID, V9X_SHAPE_QUAD,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD | V9X_D3D_INTERP_FOH,
      V9X_D3D_ALPHA, 0ul, 0xfffffffful, 0ul, 0ul, 0 },
    { "Perspective", V9X_TEX_ID, V9X_SHAPE_PERSPECTIVE,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0 },
    { "EdgeHalf", V9X_TEX_WHITE, V9X_SHAPE_HALF,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xffff0000ul, 0ul, 0ul, 0 },
    { "ZLess", V9X_TEX_WHITE, V9X_SHAPE_ZPAIR,
      V9X_D3D_CFG_ZTEST(V9X_CMP_LESS) | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA,
      0ul, 0ul, 0ul, 0ul, 0 },
    { "ZGreater", V9X_TEX_WHITE, V9X_SHAPE_ZPAIR,
      V9X_D3D_CFG_ZTEST(V9X_CMP_GREATER) | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA,
      0ul, 0ul, 0ul, 0ul, 0 },
    { "ZLessFillFfff", V9X_TEX_WHITE, V9X_SHAPE_ZPAIR,
      V9X_D3D_CFG_ZTEST(V9X_CMP_LESS) | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA,
      0ul, 0ul, 0ul, 0xfffffffful, 0 },
    { "AlphaTest", V9X_TEX_ID_ALPHA, V9X_SHAPE_QUAD,
      V9X_D3D_CFG_BASE | (V9X_CMP_ALWAYS << V9X_D3D_ZFUNC_SHIFT) |
      (V9X_WREN_ALPHA << V9X_D3D_CWRITE_SHIFT), V9X_D3D_ALPHA_GREATER, 0ul,
      0xfffffffful, 0x7fff7ffful, 0ul, 0 },
    { "BlendHalf", V9X_TEX_WHITE, V9X_SHAPE_QUAD, V9X_D3D_CFG_OPAQUE,
      V9X_D3D_ALPHA, 0ul, 0x80ff0000ul, 0x001f001ful, 0ul, 0 },
    { "ColourKey", V9X_TEX_ID, V9X_SHAPE_QUAD, V9X_D3D_CFG_OPAQUE,
      V9X_D3D_ALPHA, V9X_D3D_KEY_ENABLE | 0x9080ul, 0xfffffffful,
      0x7fff7ffful, 0ul, 0 },
    { "BigDma", V9X_TEX_ID, V9X_SHAPE_QUAD,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 1 },
    { "EdgeQuarter", V9X_TEX_WHITE, V9X_SHAPE_QUARTER,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xffff0000ul, 0ul, 0ul, 0 },
    { "Edge3Quarters", V9X_TEX_WHITE, V9X_SHAPE_3QUARTERS,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xffff0000ul, 0ul, 0ul, 0 },
    /*
     * Boot 20's HAL drew nothing it could be seen to: its opaque draws read
     * A1R5G5B5 texels as X1R5G5B5 under the source-alpha blend, on the
     * guess that X1 reads alpha 1. These measure that, then CONFIG's
     * SOURCE_COLOR values inside hwtest's mask (bits 8, 10 and 11; the
     * xml's ALPHA_ONE, 6, needs bit 9, which the mask refuses).
     */
    { "X1Opaque", V9X_TEX_ID, V9X_SHAPE_QUAD, V9X_D3D_CFG_OPAQUE,
      V9X_D3D_ALPHA, V9X_D3D_TEX_X1, 0xfffffffful, 0x7fff7ffful, 0ul, 0 },
    { "X1AlphaZero", V9X_TEX_ID_ALPHA, V9X_SHAPE_QUAD, V9X_D3D_CFG_OPAQUE,
      V9X_D3D_ALPHA, V9X_D3D_TEX_X1, 0xfffffffful, 0x7fff7ffful, 0ul, 0 },
    { "Source4AlphaZero", V9X_TEX_ID_ALPHA, V9X_SHAPE_QUAD,
      (V9X_D3D_CFG_OPAQUE & ~V9X_D3D_SOURCE_MASK) | 0x00000400ul,
      V9X_D3D_ALPHA, 0ul, 0xfffffffful, 0x7fff7ffful, 0ul, 0 },
    { "Source5AlphaZero", V9X_TEX_ID_ALPHA, V9X_SHAPE_QUAD,
      (V9X_D3D_CFG_OPAQUE & ~V9X_D3D_SOURCE_MASK) | 0x00000500ul,
      V9X_D3D_ALPHA, 0ul, 0xfffffffful, 0x7fff7ffful, 0ul, 0 },
    { "Source8AlphaZero", V9X_TEX_ID_ALPHA, V9X_SHAPE_QUAD,
      (V9X_D3D_CFG_OPAQUE & ~V9X_D3D_SOURCE_MASK) | 0x00000800ul,
      V9X_D3D_ALPHA, 0ul, 0xfffffffful, 0x7fff7ffful, 0ul, 0 },
    { "VertexAlphaZero", V9X_TEX_ID, V9X_SHAPE_QUAD, V9X_D3D_CFG_OPAQUE,
      V9X_D3D_ALPHA, 0ul, 0x00fffffful, 0x7fff7ffful, 0ul, 0 },
    /* Boot 21: the HAL's 425 batches all ran and left nothing in their
     * targets. Which surface does the object draw into, and which holds
     * zeta? Each case moves one surface 8 lines down. */
    { "Surface0Moved", V9X_TEX_ID, V9X_SHAPE_QUAD,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 1ul },
    { "Surface1Moved", V9X_TEX_ID, V9X_SHAPE_QUAD,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 2ul },
    { "Surface2Moved", V9X_TEX_ID, V9X_SHAPE_QUAD,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 3ul },
    /* Zeta: surface 3 cannot move without leaving the 64 KiB window, so
     * surface 1 moves under the Z pair instead. */
    { "ZLessSurface1Moved", V9X_TEX_WHITE, V9X_SHAPE_ZPAIR,
      V9X_D3D_CFG_ZTEST(V9X_CMP_LESS) | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA,
      0ul, 0ul, 0ul, 0ul, 0, 2ul },
    /* The texture offset's alignment: the HAL placed its copies on 256
     * bytes; every texture drawn before sat on 4 KiB. */
    { "TexAtF100", V9X_TEX_ID, V9X_SHAPE_QUAD,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0xf100ul },
    { "TexAtF800", V9X_TEX_ID, V9X_SHAPE_QUAD,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0xf800ul },
    { "TexAtF010", V9X_TEX_ID, V9X_SHAPE_QUAD,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0xf010ul }
};
#define V9X_D3D_CASE_COUNT \
    (sizeof(v9x_d3d_cases) / sizeof(v9x_d3d_cases[0]))

/*
 * /d3dmip: where the levels of a chain sit and how the chip picks one.
 * TEXTURE_FORMAT's SIZE_MIN (bits 24-27) and SIZE_MAX (28-31) are log2
 * sides, and FILTER has SPREAD_X (0-4), SPREAD_Y (8-12) and SIZE_ADJUST
 * (16-23) (envytools' nv3_3d.xml; nothing says what they do). The texture
 * is 192 texels from F000h, each naming its storage index s: red 2 x
 * (s & 15), green 2 x (s >> 4), blue 31 as a marker. An 8x8 level 0 is
 * s 0-63; if the smaller levels follow it, 4x4 is 64-79, 2x2 80-83 and
 * 1x1 84. The four quads sample 1/2, 2, 4 and 8 texels a pixel, point
 * sampled, so a level per quad if the chip chooses by the footprint.
 * Files N3D41.BIN on.
 */
#define V9X_D3D_MIP_TEXELS     192ul
#define V9X_D3D_FORMAT_ONE     0x33000000ul /* 8x8, one level */
#define V9X_D3D_FORMAT_CHAIN   0x30000000ul /* 8x8 down to 1x1 */
#define V9X_D3D_MIP_FIRST      41ul
#define V9X_D3D_FORMAT_MIN1    0x31000000ul /* 8x8 down to 2x2 */
#define V9X_D3D_FORMAT_4X4     0x20000000ul /* 4x4 down to 1x1 */

/* u_max and v_max of each quad, by shape: ratios 1/2, 2, 4, 8 both ways;
 * 16 to 128 both ways; u alone with v 1/2; v alone with u 1/2. */
static const DWORD v9x_d3d_mip_spans[4][4][2] = {
    { { 2ul, 1ul }, { 8ul, 4ul }, { 16ul, 8ul }, { 32ul, 16ul } },
    { { 64ul, 32ul }, { 128ul, 64ul }, { 256ul, 128ul }, { 512ul, 256ul } },
    { { 2ul, 1ul }, { 8ul, 1ul }, { 16ul, 1ul }, { 32ul, 1ul } },
    { { 2ul, 1ul }, { 2ul, 4ul }, { 2ul, 8ul }, { 2ul, 16ul } }
};

static const struct v9x_d3d_case v9x_d3d_mip_cases[] = {
    { "MipOneLevel", V9X_TEX_INDEX, V9X_SHAPE_MIPS,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_ONE, 0ul },
    { "MipChain", V9X_TEX_INDEX, V9X_SHAPE_MIPS,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_CHAIN, 0ul },
    { "MipChainAdjust10", V9X_TEX_INDEX, V9X_SHAPE_MIPS,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_CHAIN,
      0x00100000ul },
    { "MipChainAdjustF0", V9X_TEX_INDEX, V9X_SHAPE_MIPS,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_CHAIN,
      0x00f00000ul },
    { "MipChainSpread04", V9X_TEX_INDEX, V9X_SHAPE_MIPS,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_CHAIN,
      0x00000404ul },
    { "MipChainSpread1F", V9X_TEX_INDEX, V9X_SHAPE_MIPS,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_CHAIN,
      0x00001f1ful },
    { "MipChainFoh", V9X_TEX_INDEX, V9X_SHAPE_MIPS,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD | V9X_D3D_INTERP_FOH,
      V9X_D3D_ALPHA, 0ul, 0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul,
      V9X_D3D_FORMAT_CHAIN, 0ul },
    /* Run 1 never fetched below level 1. Further ratios, a shorter chain,
     * one from 4x4, and each axis alone. */
    { "MipChainFar", V9X_TEX_INDEX, V9X_SHAPE_MIPS_FAR,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_CHAIN, 0ul },
    { "MipChainMin1", V9X_TEX_INDEX, V9X_SHAPE_MIPS_FAR,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_MIN1, 0ul },
    { "MipChain4x4", V9X_TEX_INDEX, V9X_SHAPE_MIPS,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_4X4, 0ul },
    { "MipChainU", V9X_TEX_INDEX, V9X_SHAPE_MIPS_U,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_CHAIN, 0ul },
    { "MipChainV", V9X_TEX_INDEX, V9X_SHAPE_MIPS_V,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_CHAIN, 0ul },
    { "MipChainFarAdjustF0", V9X_TEX_INDEX, V9X_SHAPE_MIPS_FAR,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_CHAIN,
      0x00f00000ul },
    /* Run 2: no footprint fetched below level 1, and F0h at 4 texels a
     * pixel read a 2x2 at s 64. Magnified, SIZE_ADJUST alone picks the
     * level: one value per quad, low byte first. */
    { "MipBiasF0toC0", V9X_TEX_INDEX, V9X_SHAPE_MIPS_BIAS,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_CHAIN,
      0xc0d0e0f0ul },
    { "MipBiasB0to80", V9X_TEX_INDEX, V9X_SHAPE_MIPS_BIAS,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_CHAIN,
      0x8090a0b0ul },
    { "MipBiasFine", V9X_TEX_INDEX, V9X_SHAPE_MIPS_BIAS,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_CHAIN,
      0xf8fcfefful },
    { "MipBias4x4", V9X_TEX_INDEX, V9X_SHAPE_MIPS_BIAS,
      V9X_D3D_CFG_OPAQUE | V9X_D3D_ROP_ADD, V9X_D3D_ALPHA, 0ul,
      0xfffffffful, 0ul, 0ul, 0, 0ul, 0ul, V9X_D3D_FORMAT_4X4,
      0xc0d0e0f0ul }
};
#define V9X_D3D_MIP_COUNT \
    (sizeof(v9x_d3d_mip_cases) / sizeof(v9x_d3d_mip_cases[0]))

static DWORD v9x_at_d3d_kept;

/* value + quarters / 4 as IEEE single bits: 4 * value + quarters, divided
 * by four through its exponent. */
static DWORD v9x_float_quarters_of(DWORD value, DWORD quarters)
{
    return v9x_float_of(4ul * value + quarters) - (2ul << 23);
}

static DWORD v9x_texel_id(DWORD s, int alpha_from_s)
{
    DWORD alpha;

    alpha = alpha_from_s ? (s & 1ul) : 1ul;
    return (alpha << 15) | (((s & 7ul) * 4ul) << 10) |
           (((s >> 3) * 4ul) << 5);
}

/* /d3dmip's texel: its storage index, in steps of two so modulation's
 * rounding cannot move it to a neighbour. */
static DWORD v9x_texel_index(DWORD s)
{
    return 0x8000ul | (((s & 15ul) * 2ul) << 10) | (((s >> 4) * 2ul) << 5) |
           0x1ful;
}

/* One vertex, every coordinate already IEEE bits, then a wait: these
 * writes do not check the FIFO's free count, and a vertex is 32 bytes. */
static void v9x_d3d_vertex_at(DWORD specular, DWORD colour, DWORD x,
                              DWORD y, DWORD z, DWORD rhw, DWORD u, DWORD v)
{
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x00ul, specular);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x04ul, colour);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x08ul, x);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x0cul, y);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x10ul, z);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x14ul, rhw);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x18ul, u);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_VERTEX + 0x1cul, v);
    v9x_op(V9X_OP_WAIT_IDLE, 0ul, 0ul, 0ul);
}

/* Two triangles, (x0,y0) (x1,y0) (x0,y1) and (x1,y0) (x1,y1) (x0,y1),
 * texture coordinates 0 to 1 across; fog byte 0 (none). */
static void v9x_d3d_quad(DWORD colour, DWORD x0, DWORD y0, DWORD x1,
                         DWORD y1, DWORD z, DWORD rhw_right)
{
    v9x_d3d_vertex_at(V9X_D3D_SPEC_V0, colour, v9x_float_of(x0),
                      v9x_float_of(y0), z, V9X_FLOAT_ONE, 0ul, 0ul);
    v9x_d3d_vertex_at(V9X_D3D_SPEC_V1, colour, v9x_float_of(x1),
                      v9x_float_of(y0), z, rhw_right, V9X_FLOAT_ONE, 0ul);
    v9x_d3d_vertex_at(V9X_D3D_SPEC_V2, colour, v9x_float_of(x0),
                      v9x_float_of(y1), z, V9X_FLOAT_ONE, 0ul,
                      V9X_FLOAT_ONE);
    v9x_d3d_vertex_at(V9X_D3D_SPEC_V3, colour, v9x_float_of(x1),
                      v9x_float_of(y1), z, rhw_right, V9X_FLOAT_ONE,
                      V9X_FLOAT_ONE);
}

/* /d3dmip's quad: u 0 to u_max, v 0 to v_max (whole numbers), rhw 1. */
static void v9x_d3d_quad_uv(DWORD colour, DWORD x0, DWORD y0, DWORD x1,
                            DWORD y1, DWORD u_max, DWORD v_max)
{
    DWORD u;
    DWORD v;

    u = v9x_float_of(u_max);
    v = v9x_float_of(v_max);
    v9x_d3d_vertex_at(V9X_D3D_SPEC_V0, colour, v9x_float_of(x0),
                      v9x_float_of(y0), V9X_FLOAT_HALF, V9X_FLOAT_ONE, 0ul,
                      0ul);
    v9x_d3d_vertex_at(V9X_D3D_SPEC_V1, colour, v9x_float_of(x1),
                      v9x_float_of(y0), V9X_FLOAT_HALF, V9X_FLOAT_ONE, u, 0ul);
    v9x_d3d_vertex_at(V9X_D3D_SPEC_V2, colour, v9x_float_of(x0),
                      v9x_float_of(y1), V9X_FLOAT_HALF, V9X_FLOAT_ONE, 0ul, v);
    v9x_d3d_vertex_at(V9X_D3D_SPEC_V3, colour, v9x_float_of(x1),
                      v9x_float_of(y1), V9X_FLOAT_HALF, V9X_FLOAT_ONE, u, v);
}

/* The whole-memory DMA object, written ahead of its case in requests of
 * RAMIN writes alone; the bring-up leaves RAMIN 4000h alone. */
static int v9x_write_big_dma(HANDLE device)
{
    DWORD page;
    DWORD returned;
    BOOL ok;

    page = 0ul;
    while (page < V9X_D3D_BIG_PAGES) {
        v9x_request.count = 0ul;
        if (page == 0ul) {
            v9x_op(V9X_OP_RAMIN_WRITE, V9X_D3D_BIG_DMA, V9X_D3D_DMA_FLAGS,
                   0ul);
            v9x_op(V9X_OP_RAMIN_WRITE, V9X_D3D_BIG_DMA + 4ul,
                   V9X_D3D_BIG_PAGES * V9X_D3D_PAGE - 1ul, 0ul);
        }
        while (page < V9X_D3D_BIG_PAGES &&
               v9x_request.count < V9X_D3D_BIG_BATCH) {
            v9x_op(V9X_OP_RAMIN_WRITE, V9X_D3D_BIG_DMA + 8ul + page * 4ul,
                   (page * V9X_D3D_PAGE) | V9X_D3D_PTE_PRESENT_RW, 0ul);
            ++page;
        }
        returned = 0ul;
        ok = DeviceIoControl(device, V9X_N32D_RUN, &v9x_request,
                             sizeof(v9x_request), &v9x_result,
                             sizeof(v9x_result), &returned, 0);
        if (!ok || returned != sizeof(v9x_result) ||
            (v9x_result.status & V9X_ST_COMPLETE) == 0ul) {
            return 0;
        }
    }
    return 1;
}

static void v9x_build_d3d_case(const struct v9x_d3d_case *one, DWORD pitch)
{
    DWORD index;
    DWORD texel;
    DWORD y;
    DWORD a_config;
    DWORD quarters;
    DWORD texture_at;
    DWORD texels;
    DWORD ratio;
    DWORD left;

    texture_at = one->texture_at != 0ul ? one->texture_at : V9X_D3D_TEXTURE;
    v9x_request.count = 0ul;
    v9x_bring_up(2ul, pitch, 0);

    texels = one->texture == V9X_TEX_INDEX ? V9X_D3D_MIP_TEXELS :
             V9X_D3D_TEX_SIZE * V9X_D3D_TEX_SIZE;
    for (index = 0ul; index < texels; index += 2ul) {
        if (one->texture == V9X_TEX_INDEX) {
            texel = v9x_texel_index(index) |
                    (v9x_texel_index(index + 1ul) << 16);
        } else if (one->texture == V9X_TEX_WHITE) {
            texel = 0xfffffffful;
        } else {
            texel = v9x_texel_id(index, one->texture == V9X_TEX_ID_ALPHA) |
                    (v9x_texel_id(index + 1ul,
                                  one->texture == V9X_TEX_ID_ALPHA) << 16);
        }
        v9x_op(V9X_OP_VRAM_WRITE, texture_at + index * 2ul, texel, 0ul);
    }
    v9x_op(V9X_OP_VRAM_FILL, V9X_D3D_ZETA, (V9X_D3D_LINES * pitch) / 4ul,
           one->zeta_fill);
    v9x_op(V9X_OP_VRAM_FILL, 0ul, (V9X_D3D_LINES * pitch) / 4ul, one->fill);

    v9x_method(V9X_SUB_D3D, V9X_M_D3D_TEX_OFFSET,
               one->big_dma ? V9X_SURFACE_OFFSET + texture_at : texture_at);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_TEX_FORMAT,
               (one->format != 0ul ? one->format : V9X_D3D_TEX_FORMAT) |
               one->format_extra);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_FILTER, one->filter);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_FOG_COLOR, 0ul);
    /* The Z pair draws A unconditionally, then B under the case's test. */
    a_config = one->shape == V9X_SHAPE_ZPAIR ?
               (V9X_D3D_CFG_BASE | (V9X_CMP_ALWAYS << V9X_D3D_ZFUNC_SHIFT) |
                (V9X_WREN_ALWAYS << V9X_D3D_ZWRITE_SHIFT) |
                (V9X_WREN_ALWAYS << V9X_D3D_CWRITE_SHIFT) |
                V9X_D3D_ROP_ADD) :
               one->config;
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_CONFIG, a_config);
    v9x_method(V9X_SUB_D3D, V9X_M_D3D_ALPHA, one->alpha);
    v9x_op(V9X_OP_WAIT_IDLE, 0ul, 0ul, 0ul);

    if (one->shape == V9X_SHAPE_QUAD) {
        v9x_d3d_quad(one->colour, 8ul, 0ul, 72ul, 16ul, V9X_FLOAT_HALF,
                     V9X_FLOAT_ONE);
    } else if (one->shape == V9X_SHAPE_MIPS_BIAS) {
        /* Magnified, so every texel of the level shows and none aliases;
         * byte q of the case's filter is quad q's SIZE_ADJUST. */
        left = 0ul;
        for (ratio = 0ul; ratio < 4ul; ++ratio) {
            v9x_method(V9X_SUB_D3D, V9X_M_D3D_FILTER,
                       ((one->filter >> (8u * (unsigned int)ratio)) & 0xfful)
                       << 16);
            v9x_op(V9X_OP_WAIT_IDLE, 0ul, 0ul, 0ul);
            v9x_d3d_quad_uv(one->colour, left, 0ul, left + 32ul, 16ul, 2ul,
                            1ul);
            left += 32ul;
        }
    } else if (one->shape >= V9X_SHAPE_MIPS &&
               one->shape <= V9X_SHAPE_MIPS_V) {
        /* Each 32x16 quad spans u 0 to 4 ru and v 0 to 2 rv of the 8x8
         * level: ru texels a pixel across, rv down (v9x_d3d_mip_spans). */
        left = 0ul;
        for (ratio = 0ul; ratio < 4ul; ++ratio) {
            v9x_d3d_quad_uv(one->colour, left, 0ul, left + 32ul, 16ul,
                v9x_d3d_mip_spans[one->shape - V9X_SHAPE_MIPS][ratio][0],
                v9x_d3d_mip_spans[one->shape - V9X_SHAPE_MIPS][ratio][1]);
            left += 32ul;
        }
    } else if (one->shape == V9X_SHAPE_PERSPECTIVE) {
        v9x_d3d_quad(one->colour, 8ul, 0ul, 72ul, 16ul, V9X_FLOAT_HALF,
                     V9X_FLOAT_QUARTER);
    } else if (one->shape == V9X_SHAPE_HALF ||
               one->shape == V9X_SHAPE_QUARTER ||
               one->shape == V9X_SHAPE_3QUARTERS) {
        /* The first triangle moved by a fraction of a pixel: at a half its
         * top and left edges run through pixel centres, so the edge rule
         * decides them; a quarter and three quarters tell whether the
         * fraction is kept, rounded or dropped. */
        quarters = one->shape == V9X_SHAPE_HALF ? 2ul :
                   one->shape == V9X_SHAPE_QUARTER ? 1ul : 3ul;
        v9x_d3d_vertex_at(V9X_D3D_SPEC_V0, one->colour,
                          v9x_float_quarters_of(8ul, quarters),
                          v9x_float_quarters_of(2ul, quarters),
                          V9X_FLOAT_HALF, V9X_FLOAT_ONE, 0ul, 0ul);
        v9x_d3d_vertex_at(V9X_D3D_SPEC_V1, one->colour,
                          v9x_float_quarters_of(72ul, quarters),
                          v9x_float_quarters_of(2ul, quarters),
                          V9X_FLOAT_HALF, V9X_FLOAT_ONE, V9X_FLOAT_ONE, 0ul);
        v9x_d3d_vertex_at(V9X_D3D_SPEC_V2, one->colour,
                          v9x_float_quarters_of(8ul, quarters),
                          v9x_float_quarters_of(14ul, quarters),
                          V9X_FLOAT_HALF, V9X_FLOAT_ONE, 0ul, V9X_FLOAT_ONE);
    } else {
        /* A red over x 8-72 at z 1/2; B blue over x 40-104, z 1/4 in the
         * top eight lines and 3/4 in the bottom eight. Where B overlaps
         * A, the case's comparison picks; right of A, zeta's fill does. */
        v9x_d3d_quad(0xffff0000ul, 8ul, 0ul, 72ul, 16ul, V9X_FLOAT_HALF,
                     V9X_FLOAT_ONE);
        v9x_method(V9X_SUB_D3D, V9X_M_D3D_CONFIG, one->config);
        v9x_op(V9X_OP_WAIT_IDLE, 0ul, 0ul, 0ul);
        v9x_d3d_quad(0xff0000fful, 40ul, 0ul, 104ul, 8ul,
                     V9X_FLOAT_QUARTER, V9X_FLOAT_ONE);
        v9x_d3d_quad(0xff0000fful, 40ul, 8ul, 104ul, 16ul,
                     V9X_FLOAT_3QUARTERS, V9X_FLOAT_ONE);
    }
    v9x_at_idle_fill = v9x_op(V9X_OP_WAIT_IDLE, 0ul, 0ul, 0ul);

    /* Colour, then zeta, each line's first V9X_D3D_KEPT_X pixels. */
    v9x_at_d3d_kept = v9x_request.count;
    for (y = 0ul; y < V9X_D3D_LINES; ++y) {
        v9x_op(V9X_OP_VRAM_BLOCK, y * pitch, V9X_D3D_KEPT_X / 2ul, 0ul);
    }
    for (y = 0ul; y < V9X_D3D_LINES; ++y) {
        v9x_op(V9X_OP_VRAM_BLOCK, V9X_D3D_ZETA + y * pitch,
               V9X_D3D_KEPT_X / 2ul, 0ul);
    }
    v9x_at_d3d_after = v9x_request.count;
    for (index = 0ul; index < V9X_D3D_AFTER_COUNT; ++index) {
        v9x_read(v9x_d3d_after_offsets[index]);
    }
    v9x_op(V9X_OP_RESTORE, 0ul, 0ul, 0ul);
}

static int v9x_run_d3d_case(HANDLE device, DWORD number,
                            const struct v9x_d3d_case *one)
{
    char path[40];
    HANDLE file;
    DWORD pitch;
    DWORD returned;
    DWORD written;
    DWORD index;
    int length;
    BOOL ok;

    pitch = V9X_WIDTH * 2ul;
    v9x_d3d_dma_word = V9X_D3D_DMA_INSTANCE;
    if (one->big_dma) {
        if (!v9x_write_big_dma(device)) {
            v9x_text(one->name, "Result", "BIG-DMA-WRITE-FAILED");
            return 0;
        }
        v9x_d3d_dma_word = V9X_D3D_BIG_DMA_WORD;
    }
    for (index = 0ul; index < 4ul; ++index) {
        v9x_surface_shift[index] = one->shift_surface == index + 1ul ?
                                   V9X_D3D_SHIFT_LINES : 0ul;
    }
    v9x_build_d3d_case(one, pitch);
    for (index = 0ul; index < 4ul; ++index) {
        v9x_surface_shift[index] = 0ul;
    }
    v9x_number(one->name, "Ops", v9x_request.count);
    v9x_key(one->name, "Config", one->config);

    returned = 0ul;
    ok = DeviceIoControl(device, V9X_N32D_RUN, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_N32D_MAGIC) {
        v9x_text(one->name, "Result", "DIOC-FAILED");
        return 0;
    }
    v9x_key(one->name, "Status", v9x_result.status);
    if ((v9x_result.status & V9X_ST_COMPLETE) == 0ul) {
        v9x_number(one->name, "RefusedIndex", v9x_result.refused_index);
        v9x_text(one->name, "Result", "REFUSED-OR-UNMAPPED");
        return 0;
    }
    if ((v9x_result.status & V9X_ST_TIMEOUT) != 0ul) {
        v9x_text(one->name, "Timeout", "yes");
    }
    v9x_key(one->name, "IdleDrawReads",
            v9x_result.op_results[v9x_at_idle_fill]);
    for (index = 0ul; index < V9X_D3D_AFTER_COUNT; ++index) {
        v9x_key(one->name, v9x_d3d_after_names[index],
                v9x_result.op_results[v9x_at_d3d_after + index]);
    }

    /* The block holds only the kept lines, colour then zeta, in order. */
    lstrcpyA(path, V9X_DIAG_PATH("N3D00.BIN"));
    length = lstrlenA(path);
    path[length - 6] = (char)('0' + (int)(number / 10ul));
    path[length - 5] = (char)('0' + (int)(number % 10ul));
    file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        v9x_text(one->name, "Result", "BIN-WRITE-FAILED");
        return 0;
    }
    written = 0ul;
    ok = WriteFile(file, v9x_result.block, 2ul * V9X_D3D_KEPT_BYTES,
                   &written, 0);
    CloseHandle(file);
    v9x_text(one->name, "File", path);
    v9x_text(one->name, "Result",
             ok && written == 2ul * V9X_D3D_KEPT_BYTES &&
             (v9x_result.status & V9X_ST_TIMEOUT) == 0ul ? "DONE" : "FAIL");
    return ok && (v9x_result.status & V9X_ST_TIMEOUT) == 0ul;
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

    /* Ahead of /d3d, which its name contains. */
    if (v9x_switch_given("/d3dmip")) {
        if (bits != 16ul) {
            v9x_text("Report", "Result", "D3D-NEEDS-16BPP");
            CloseHandle(device);
            WritePrivateProfileStringA(0, 0, 0, V9X_N32D_PATH);
            ExitProcess(1u);
        }
        v9x_d3d = 1;
        all_pass = 1;
        for (index = 0ul; index < V9X_D3D_MIP_COUNT; ++index) {
            if (!v9x_run_d3d_case(device, V9X_D3D_MIP_FIRST + index,
                                  &v9x_d3d_mip_cases[index])) {
                all_pass = 0;
            }
        }
        CloseHandle(device);
        v9x_text("Report", "Result", all_pass ? "DONE" : "FAIL");
        WritePrivateProfileStringA(0, 0, 0, V9X_N32D_PATH);
        ExitProcess(all_pass ? 0u : 3u);
    }

    if (v9x_switch_given("/d3dstate")) {
        if (bits != 16ul) {
            v9x_text("Report", "Result", "D3D-NEEDS-16BPP");
            CloseHandle(device);
            WritePrivateProfileStringA(0, 0, 0, V9X_N32D_PATH);
            ExitProcess(1u);
        }
        v9x_d3d = 1;
        all_pass = 1;
        for (index = 0ul; index < V9X_D3D_CASE_COUNT; ++index) {
            if (!v9x_run_d3d_case(device, index + 1ul,
                                  &v9x_d3d_cases[index])) {
                all_pass = 0;
            }
        }
        CloseHandle(device);
        v9x_text("Report", "Result", all_pass ? "DONE" : "FAIL");
        WritePrivateProfileStringA(0, 0, 0, V9X_N32D_PATH);
        ExitProcess(all_pass ? 0u : 3u);
    }

    if (v9x_switch_given("/d3d")) {
        /* The engine's depth field is the desktop's (PFB_CONFIG_0); the
         * 3D object draws 16 bpp, so the desktop has to be 16. */
        if (bits != 16ul) {
            v9x_text("Report", "Result", "D3D-NEEDS-16BPP");
            CloseHandle(device);
            WritePrivateProfileStringA(0, 0, 0, V9X_N32D_PATH);
            ExitProcess(1u);
        }
        v9x_d3d = 1;
        v9x_d3d_clear = 1;
        v9x_d3d_config = V9X_D3D_CONFIG | V9X_D3D_ROP_ADD;
        /* The control: fog 255 to a white fog colour draws white. */
        v9x_d3d_fog_color = 0x00fffffful;
        all_pass = v9x_run_d3d(device, 0, "D3DFogWhite");
        v9x_key("Report", "Bar1", v9x_result.bar1);
        v9x_d3d_fog_color = 0ul;
        v9x_d3d_fog = 0ul;
        /*
         * The fourth run (boot 18): target 0 (rivafb's) drew black with
         * video memory offsets and with the bus address; flags 0 timed
         * out; targets 1, 2 and 3 drew identical varying reds, texels from
         * system memory at the entries' addresses. So the fifth points
         * targets 1 and 2 at the framebuffer's bus address, where the
         * texture is.
         */
        /* The fifth (boot 18): targets 1 and 2 at the bus address drew
         * black too. The sixth reads PGRAPH's DMA state after each draw:
         * the video memory target as rivafb has it, and PCI for
         * comparison. */
        if (!v9x_run_d3d(device, 0, "D3DTargetVram")) {
            all_pass = 0;
        }
        if (!v9x_run_d3d(device, 1, "D3DTargetVramTextured")) {
            all_pass = 0;
        }
        /* Fog 255 to white over the checker: the fog amount scales. */
        v9x_d3d_fog = 0x80ul;
        v9x_d3d_fog_color = 0x00fffffful;
        if (!v9x_run_d3d(device, 0, "D3DFogHalfWhite")) {
            all_pass = 0;
        }
        CloseHandle(device);
        v9x_text("Report", "Result", all_pass ? "PASS" : "FAIL");
        WritePrivateProfileStringA(0, 0, 0, V9X_N32D_PATH);
        ExitProcess(all_pass ? 0u : 3u);
    }

    all_pass = v9x_run_depth(device, bits, 0, "");
    if (bits == 16ul && !v9x_run_depth(device, bits, V9X_VARIANT_Y16, "")) {
        all_pass = 0;
    }
    if (bits == 16ul && v9x_switch_given("/alpha")) {
        if (!v9x_run_depth(device, bits, V9X_VARIANT_ALPHA, "")) {
            all_pass = 0;
        }
        /* An overlapping move, whose source bits the blit reads back
         * after writing some of them. */
        v9x_blit_sx = 4ul;
        v9x_blit_sy = 2ul;
        v9x_blit_dx = 7ul;
        v9x_blit_dy = 2ul;
        if (!v9x_run_depth(device, bits, V9X_VARIANT_ALPHA, "OverlapRight")) {
            all_pass = 0;
        }
        v9x_blit_sx = 4ul;
        v9x_blit_sy = 1ul;
        v9x_blit_dx = 120ul;
        v9x_blit_dy = 1ul;
    }
    if (v9x_switch_given("/overlap")) {
        /* Down-right: the destination 4 right and 2 down of the source. */
        v9x_blit_sx = 4ul;
        v9x_blit_sy = 1ul;
        v9x_blit_dx = 8ul;
        v9x_blit_dy = 3ul;
        if (!v9x_run_depth(device, bits, 0, "OverlapDownRight")) {
            all_pass = 0;
        }
        /* Up-left: the reverse. */
        v9x_blit_sx = 8ul;
        v9x_blit_sy = 3ul;
        v9x_blit_dx = 4ul;
        v9x_blit_dy = 1ul;
        if (!v9x_run_depth(device, bits, 0, "OverlapUpLeft")) {
            all_pass = 0;
        }
        /* Same row, right by 3: the scroll that most tests a left-to-
         * right copier. */
        v9x_blit_sx = 4ul;
        v9x_blit_sy = 2ul;
        v9x_blit_dx = 7ul;
        v9x_blit_dy = 2ul;
        if (!v9x_run_depth(device, bits, 0, "OverlapRight")) {
            all_pass = 0;
        }
        v9x_blit_sx = 4ul;
        v9x_blit_sy = 1ul;
        v9x_blit_dx = 120ul;
        v9x_blit_dy = 1ul;
    }
    if (v9x_switch_given("/surfaces")) {
        /* Fills to y 4-8, the blit's destination to y 3-9: inside the
         * ten lines read back. */
        v9x_dst_shift = 2ul;
        if (!v9x_run_depth(device, bits, 0, "DstSurfaceShifted")) {
            all_pass = 0;
        }
        v9x_dst_shift = 0ul;
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
            if (!v9x_run_depth(device, others[index], 0, "")) {
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
