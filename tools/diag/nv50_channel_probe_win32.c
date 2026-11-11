/*
 * V9XN5CH.EXE: GT218 PFIFO channel and PGRAPH 2D bring-up probe.
 *
 * Step 3 of docs\plans\nvidia-gt218-pgraph-2d.md. Builds one channel with
 * nv50_chan.c (step 2) and the context program with nv50_grctx.c (step 1),
 * writes them through BAR1 at video memory 192 MiB (N5CH.VXD's window), and
 * runs, stopping at the first stage that fails:
 *
 *   pre     the state the VBE left: PMC, PFIFO, PGRAPH, VM fault status;
 *   region  the channel's structures written and spot-read back;
 *   fifo    (3a) nouveau's nv50_fifo_init, channel 1 bound and enabled,
 *           its runlist committed; one IB entry whose pushbuffer is
 *           PFIFO's REF_CNT method; pass when IB GET reaches PUT;
 *   gr      (3b) PGRAPH reset and nouveau's nv50_gr_init with the context
 *           program uploaded; a second IB entry binding the 502D object;
 *           pass when GET reaches PUT with PGRAPH's interrupt status clear;
 *   fill    (3c) a third IB entry filling 64x64 pixels of a 128x72
 *           X8R8G8B8 surface at 194 MiB that the CPU first filled with a
 *           pattern; pass when exactly the rectangle changed to the colour.
 *
 * Writes C:\V9XDIAG\V9XN5CH.INI, flushed after every stage, so a hang
 * leaves the stages before it on disk. /stop:fifo and /stop:gr end the run
 * after that stage.
 *
 * Nothing here touches the display: the region is above the 128 MiB the
 * driver maps, PMC_ENABLE may change only its PFIFO and PGRAPH bits (the
 * VxD enforces it), and PMC_INTR_EN stays as found.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/nv50_chan.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_N5CH_PATH        V9X_DIAG_PATH("V9XN5CH.INI")
#define V9X_N5CH_VXD         "\\\\.\\N5CH.VXD"
#define V9X_N5CH_MAGIC       0x4843354eul
#define V9X_N5CH_RUN         1u
#define V9X_N5CH_OP_MAX      512u
#define V9X_N5CH_BLOCK       4096u
#define V9X_N5CH_DATA        0x30000u
#define V9X_N5CH_HEADER      16u

#define V9X_OP_MMIO_READ     1ul
#define V9X_OP_MMIO_WRITE    2ul
#define V9X_OP_VRAM_DATA     3ul
#define V9X_OP_VRAM_READ     4ul
#define V9X_OP_VRAM_FILL     5ul
#define V9X_OP_VRAM_BLOCK    6ul
#define V9X_OP_WAIT_MMIO     7ul
#define V9X_OP_WAIT_VRAM     8ul
#define V9X_OP_DELAY         9ul
#define V9X_OP_TIMEOUT       0xfffffffful

#define V9X_ST_FOUND         0x00000001ul
#define V9X_ST_MAPPED        0x00000002ul
#define V9X_ST_IDENTITY      0x00000004ul
#define V9X_ST_REFUSED       0x00000008ul
#define V9X_ST_COMPLETE      0x00000010ul
#define V9X_ST_TIMEOUT       0x00000040ul

/* The VxD's window: BAR1 + 192 MiB, which is video memory 192 MiB. */
#define V9X_WINDOW_VRAM      0x0c000000ul
#define V9X_CHID             1ul

/* The fill target, 2 MiB into the window: 128x72 X8R8G8B8, pitch 512,
 * the rectangle at (8, 4), 64x64. */
#define V9X_FILL_OFFSET      0x00200000ul
#define V9X_FILL_PITCH       512ul
#define V9X_FILL_WIDTH       128ul
#define V9X_FILL_HEIGHT      72ul
#define V9X_FILL_X           8ul
#define V9X_FILL_Y           4ul
#define V9X_FILL_SIZE        64ul
#define V9X_FILL_COLOUR      0x00a5c3e1ul
#define V9X_FILL_PATTERN     0x5a5a5a5aul
#define V9X_COLOUR_MASK      0x00fffffful

/* REF_CNT's value for stage fifo. */
#define V9X_REF_VALUE        0x5a5a0001ul

/* Where each stage's pushbuffer starts, in words from the pushbuffer. */
#define V9X_PUSH_FIFO        0ul
#define V9X_PUSH_GR          64ul
#define V9X_PUSH_FILL        128ul
#define V9X_PUSH_STAGE_MAX   64ul

/* nouveau's nv50_fifo_init and nv50_gr_init values. */
#define V9X_PFIFO_250C       0x00250cul
#define V9X_PFIFO_250C_VALUE 0x6f3cfc34ul
#define V9X_PFIFO_2044       0x002044ul
#define V9X_PFIFO_2044_VALUE 0x01003ffful
#define V9X_PFIFO_INTR_EN_VALUE 0xbffffffful
#define V9X_PFIFO_CACHE1_PUSH 0x003200ul
#define V9X_PFIFO_CACHE1_PULL 0x003250ul
#define V9X_PGRAPH_CTXCTL    0x40008cul
#define V9X_PGRAPH_INTR      0x400100ul
#define V9X_PGRAPH_TRAP      0x400108ul
#define V9X_PGRAPH_STATUS    0x400700ul
#define V9X_PGRAPH_FIFO      0x400500ul
#define V9X_PGRAPH_CTXPROG_INDEX 0x400324ul
#define V9X_PGRAPH_CTXPROG_DATA  0x400328ul
#define V9X_TRAP_RESET       0xc0000000ul
/* PBUS units: the context program generator's input. */
#define V9X_PBUS_UNITS       0x001540ul

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_n5ch_request {
    DWORD count;
    DWORD ops[V9X_N5CH_OP_MAX][4];
    DWORD data_count;
    DWORD data[V9X_N5CH_DATA];
};

struct v9x_n5ch_result {
    DWORD magic;
    DWORD status;
    DWORD id;
    DWORD bar0;
    DWORD bar1;
    DWORD bar1_high;
    DWORD boot0;
    DWORD done;
    DWORD refused_at;
    DWORD config;
    DWORD block_used;
    DWORD reserved[5];
    DWORD results[V9X_N5CH_OP_MAX];
    DWORD block[V9X_N5CH_BLOCK];
};

typedef char v9x_n5ch_header_matches[
    offsetof(struct v9x_n5ch_result, results) ==
    sizeof(DWORD) * V9X_N5CH_HEADER ? 1 : -1];
typedef char v9x_n5ch_region_fits[
    V9X_NV50_REGION_WORDS <= V9X_N5CH_DATA ? 1 : -1];

static struct v9x_n5ch_request v9x_request;
static struct v9x_n5ch_result v9x_result;
static struct v9x_nv50_chan_layout v9x_layout;
static DWORD v9x_stop_after_fifo;
static DWORD v9x_stop_after_gr;

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
    WritePrivateProfileStringA(section, key, value, V9X_N5CH_PATH);
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

static void v9x_flush(void)
{
    WritePrivateProfileStringA(0, 0, 0, V9X_N5CH_PATH);
}

static void v9x_finish(const char *result, UINT code)
{
    v9x_text("Report", "Result", result);
    v9x_flush();
    ExitProcess(code);
}

static int v9x_switch_given(const char *switch_text)
{
    const char *line = GetCommandLineA();
    int length = lstrlenA(switch_text);

    while (*line != '\0') {
        if (CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE, line,
                           length, switch_text, length) == CSTR_EQUAL) {
            return 1;
        }
        ++line;
    }
    return 0;
}

static DWORD v9x_config_read(DWORD device, DWORD offset)
{
    DWORD saved;
    DWORD value;

    saved = v9x_inpd(0x0cf8u);
    v9x_outpd(0x0cf8u, device | (offset & 0xfcul));
    value = v9x_inpd(0x0cfcu);
    v9x_outpd(0x0cf8u, saved);
    return value;
}

/* ---- The op list. ---- */

static void v9x_ops_begin(void)
{
    v9x_request.count = 0ul;
    v9x_request.data_count = 0ul;
}

/* Returns the op's index, for reading its result after the run. */
static DWORD v9x_op(DWORD op, DWORD a, DWORD b, DWORD c)
{
    DWORD at = v9x_request.count;

    if (at >= V9X_N5CH_OP_MAX) {
        v9x_finish("TOO-MANY-OPS", 9u);
    }
    v9x_request.ops[at][0] = op;
    v9x_request.ops[at][1] = a;
    v9x_request.ops[at][2] = b;
    v9x_request.ops[at][3] = c;
    ++v9x_request.count;
    return at;
}

static DWORD v9x_read(DWORD offset)
{
    return v9x_op(V9X_OP_MMIO_READ, offset, 0ul, 0ul);
}

static void v9x_write(DWORD offset, DWORD value)
{
    v9x_op(V9X_OP_MMIO_WRITE, offset, value, 0ul);
}

static DWORD v9x_wait(DWORD offset, DWORD mask, DWORD value)
{
    return v9x_op(V9X_OP_WAIT_MMIO, offset, mask, value);
}

/* words dwords of buffer into the window at offset, through the data
 * area. */
static void v9x_vram_data(DWORD offset, const DWORD *buffer, DWORD words)
{
    DWORD first = v9x_request.data_count;
    DWORD index;

    if (first + words > V9X_N5CH_DATA) {
        v9x_finish("TOO-MUCH-DATA", 9u);
    }
    for (index = 0ul; index < words; ++index) {
        v9x_request.data[first + index] = buffer[index];
    }
    v9x_request.data_count = first + words;
    v9x_op(V9X_OP_VRAM_DATA, offset, first, words);
}

static void v9x_progress(const char *step);

/* Run the list. Returns nonzero when every op ran. */
static int v9x_run(const char *stage)
{
    HANDLE device;
    DWORD returned = 0ul;
    BOOL ok;
    char key[32];

    v9x_progress(stage);
    device = CreateFileA(V9X_N5CH_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_key("Report", "LoadError", GetLastError());
        v9x_finish("VXD-LOAD-FAILED", 2u);
    }
    ok = DeviceIoControl(device, V9X_N5CH_RUN, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_N5CH_MAGIC) {
        v9x_key("Report", "Returned", returned);
        v9x_finish("DIOC-FAILED", 2u);
    }
    lstrcpyA(key, stage);
    lstrcatA(key, ".Status");
    v9x_key("Report", key, v9x_result.status);
    if ((v9x_result.status & V9X_ST_COMPLETE) == 0ul) {
        lstrcpyA(key, stage);
        lstrcatA(key, ".RefusedAt");
        v9x_number("Report", key, v9x_result.refused_at);
        return 0;
    }
    return 1;
}

/* The step about to run, flushed first: after a hang, the last Progress
 * in the INI names the DIOC run that never returned. */
static void v9x_progress(const char *step)
{
    v9x_text("Report", "Progress", step);
    v9x_flush();
}

static DWORD v9x_value(DWORD op)
{
    return v9x_result.results[op];
}

/* A result into the stage's section as <name>=value. */
static void v9x_put(const char *section, const char *name, DWORD op)
{
    v9x_key(section, name, v9x_value(op));
}

static int v9x_timed_out(DWORD op)
{
    return v9x_value(op) == V9X_OP_TIMEOUT;
}

/* ---- Status registers read after every stage. ---- */

struct v9x_status_ops {
    DWORD pmc_enable;
    DWORD pmc_intr;
    DWORD pfifo_intr;
    DWORD caches;
    DWORD chan;
    DWORD runlist;
    DWORD push;
    DWORD pull;
    DWORD ib_get;
    DWORD ib_put;
    DWORD ref;
    DWORD gr_intr;
    DWORD gr_trap;
    DWORD gr_status;
    DWORD gr_ctx;
    DWORD gr_trap_status;
    DWORD gr_trap_addr;
    DWORD gr_trap_data;
    DWORD gr_trap_class;
    DWORD vm_fault[4];
};

static void v9x_status_request(struct v9x_status_ops *s)
{
    DWORD index;

    s->pmc_enable = v9x_read(V9X_NV50_PMC_ENABLE);
    s->pmc_intr = v9x_read(0x000100ul);
    s->pfifo_intr = v9x_read(V9X_NV50_PFIFO_INTR);
    s->caches = v9x_read(V9X_NV50_PFIFO_CACHES);
    s->chan = v9x_read(V9X_NV50_PFIFO_CHAN(V9X_CHID));
    s->runlist = v9x_read(V9X_NV50_PFIFO_RUNLIST_COUNT);
    s->push = v9x_read(V9X_PFIFO_CACHE1_PUSH);
    s->pull = v9x_read(V9X_PFIFO_CACHE1_PULL);
    s->ib_get = v9x_read(V9X_NV50_USERD(V9X_CHID) + V9X_NV50_USERD_IB_GET);
    s->ib_put = v9x_read(V9X_NV50_USERD(V9X_CHID) + V9X_NV50_USERD_IB_PUT);
    s->ref = v9x_read(V9X_NV50_USERD(V9X_CHID) + V9X_NV50_USERD_REF);
    s->gr_intr = v9x_read(V9X_PGRAPH_INTR);
    s->gr_trap = v9x_read(V9X_PGRAPH_TRAP);
    s->gr_status = v9x_read(V9X_PGRAPH_STATUS);
    s->gr_ctx = v9x_read(0x40032cul);
    s->gr_trap_status = v9x_read(0x400804ul);
    s->gr_trap_addr = v9x_read(0x400808ul);
    s->gr_trap_data = v9x_read(0x40080cul);
    s->gr_trap_class = v9x_read(0x400814ul);
    for (index = 0ul; index < 4ul; ++index) {
        s->vm_fault[index] = v9x_read(0x100c90ul + index * 4ul);
    }
}

static void v9x_status_publish(const char *section,
                               const struct v9x_status_ops *s)
{
    char key[16];
    DWORD index;

    v9x_put(section, "PmcEnable", s->pmc_enable);
    v9x_put(section, "PmcIntr", s->pmc_intr);
    v9x_put(section, "PfifoIntr", s->pfifo_intr);
    v9x_put(section, "PfifoCaches", s->caches);
    v9x_put(section, "Channel1", s->chan);
    v9x_put(section, "RunlistCount", s->runlist);
    v9x_put(section, "Cache1Push", s->push);
    v9x_put(section, "Cache1Pull", s->pull);
    v9x_put(section, "IbGet", s->ib_get);
    v9x_put(section, "IbPut", s->ib_put);
    v9x_put(section, "Ref", s->ref);
    v9x_put(section, "PgraphIntr", s->gr_intr);
    v9x_put(section, "PgraphTrap", s->gr_trap);
    v9x_put(section, "PgraphStatus", s->gr_status);
    v9x_put(section, "PgraphCtx", s->gr_ctx);
    v9x_put(section, "PgraphTrapStatus", s->gr_trap_status);
    v9x_put(section, "PgraphTrapAddr", s->gr_trap_addr);
    v9x_put(section, "PgraphTrapData", s->gr_trap_data);
    v9x_put(section, "PgraphTrapClass", s->gr_trap_class);
    for (index = 0ul; index < 4ul; ++index) {
        lstrcpyA(key, "VmFault");
        v9x_hex(key + 7, 0x100c90ul + index * 4ul, 6);
        v9x_put(section, key, s->vm_fault[index]);
    }
}

/* ---- The stages. ---- */

static DWORD v9x_pmc_enable_found;

static void v9x_stage_pre(void)
{
    struct v9x_status_ops s;
    DWORD enable;
    DWORD intr_en;
    DWORD ctxctl;
    DWORD runlist_addr;

    v9x_ops_begin();
    enable = v9x_read(V9X_NV50_PMC_ENABLE);
    intr_en = v9x_read(0x000140ul);
    ctxctl = v9x_read(V9X_PGRAPH_CTXCTL);
    runlist_addr = v9x_read(V9X_NV50_PFIFO_RUNLIST_ADDR);
    v9x_status_request(&s);
    if (!v9x_run("pre")) {
        v9x_finish("PRE-REFUSED", 3u);
    }
    v9x_key("Report", "Bar0", v9x_result.bar0);
    v9x_key("Report", "Bar1", v9x_result.bar1);
    v9x_key("Report", "Boot0", v9x_result.boot0);
    v9x_put("Pre", "PmcIntrEn", intr_en);
    v9x_put("Pre", "PgraphCtxCtl", ctxctl);
    v9x_put("Pre", "RunlistAddr", runlist_addr);
    v9x_status_publish("Pre", &s);
    v9x_pmc_enable_found = v9x_value(enable);
    v9x_flush();
}

static DWORD v9x_region[V9X_NV50_REGION_WORDS];

static void v9x_push_stage(DWORD first_word, struct v9x_nv50_push *push)
{
    v9x_nv50_push_init(push,
                       v9x_region + (V9X_NV50_PUSH / 4ul) + first_word,
                       V9X_PUSH_STAGE_MAX);
}

/* Write an IB entry for a stage's pushbuffer into the window. */
static void v9x_ib_write(DWORD entry_index, DWORD first_word, DWORD words)
{
    DWORD entry[2];

    v9x_nv50_ib_entry(v9x_layout.push_addr + first_word * 4ul, words,
                      entry);
    v9x_vram_data(V9X_NV50_IB + entry_index * 8ul, entry, 2ul);
}

static void v9x_stage_region(void)
{
    struct v9x_nv50_grctx_input grctx;
    struct v9x_nv50_push push;
    DWORD data[1];
    DWORD row;
    DWORD read_pd;
    DWORD read_ramfc;
    DWORD read_pt;
    DWORD read_dma;
    v9x_status status;

    grctx.chipset = (v9x_result.boot0 >> 20) & 0x1fful;
    v9x_ops_begin();
    /* 1540h decides the context program; read it before building. */
    row = v9x_read(V9X_PBUS_UNITS);
    if (!v9x_run("units")) {
        v9x_finish("UNITS-REFUSED", 3u);
    }
    grctx.units = v9x_value(row);
    /* PFB 100714h type 1 on boot 82: DDR2 or DDR3, not GDDR5 (4). */
    grctx.gddr5 = 0ul;
    v9x_key("Region", "Units", grctx.units);

    status = v9x_nv50_chan_build(V9X_WINDOW_VRAM, V9X_CHID, &grctx,
                                 v9x_region, V9X_NV50_REGION_WORDS,
                                 &v9x_layout);
    v9x_number("Region", "BuildStatus", (DWORD)status);
    if (status != V9X_STATUS_OK) {
        v9x_finish("BUILD-FAILED", 4u);
    }
    v9x_number("Region", "ProgramWords", v9x_layout.program_words);
    v9x_number("Region", "ContextBytes", v9x_layout.grctx_bytes);
    v9x_key("Region", "ChannelEntry", v9x_layout.chan_entry);
    v9x_key("Region", "RunlistReg", v9x_layout.runlist_reg);

    /* Stage fifo's pushbuffer: PFIFO's REF_CNT, nothing for an engine. */
    v9x_push_stage(V9X_PUSH_FIFO, &push);
    data[0] = V9X_REF_VALUE;
    v9x_nv50_push_method(&push, 0ul, V9X_NV50_MTHD_REF_CNT, 1ul, data);
    v9x_number("Region", "FifoPushWords", push.count);

    v9x_ops_begin();
    v9x_vram_data(0ul, v9x_region, V9X_NV50_REGION_WORDS);
    v9x_ib_write(0ul, V9X_PUSH_FIFO, push.count);
    /* The fill target: the pattern over the whole surface. */
    v9x_op(V9X_OP_VRAM_FILL, V9X_FILL_OFFSET,
           V9X_FILL_PITCH / 4ul * V9X_FILL_HEIGHT, V9X_FILL_PATTERN);
    read_pd = v9x_op(V9X_OP_VRAM_READ, V9X_NV50_INST_PD, 0ul, 0ul);
    read_ramfc = v9x_op(V9X_OP_VRAM_READ,
                        V9X_NV50_INST_RAMFC + 0x80ul, 0ul, 0ul);
    read_pt = v9x_op(V9X_OP_VRAM_READ,
                     V9X_NV50_PT + (V9X_WINDOW_VRAM >> 16) * 8ul, 0ul, 0ul);
    read_dma = v9x_op(V9X_OP_VRAM_READ, V9X_NV50_INST_DMA_VM, 0ul, 0ul);
    if (!v9x_run("region")) {
        v9x_finish("REGION-REFUSED", 4u);
    }
    v9x_put("Region", "ReadPde0", read_pd);
    v9x_put("Region", "ReadRamfc80", read_ramfc);
    v9x_put("Region", "ReadPteSelf", read_pt);
    v9x_put("Region", "ReadDma", read_dma);
    v9x_text("Region", "Readback",
             v9x_value(read_pd) == v9x_region[V9X_NV50_INST_PD / 4ul] &&
             v9x_value(read_ramfc) ==
                 v9x_region[(V9X_NV50_INST_RAMFC + 0x80ul) / 4ul] &&
             v9x_value(read_dma) == 0x7fc0003dul ? "ok" : "differs");
    v9x_flush();
}

static int v9x_stage_fifo(void)
{
    struct v9x_status_ops s;
    DWORD index;
    DWORD runlist_wait;
    DWORD get_wait;

    v9x_ops_begin();
    /* nv50_fifo_init: reset PFIFO, its constants, interrupt status and
     * enables (status only: PMC_INTR_EN is not touched), the channel
     * table cleared. */
    v9x_write(V9X_NV50_PMC_ENABLE,
              v9x_pmc_enable_found & ~V9X_NV50_PMC_ENABLE_PFIFO);
    v9x_write(V9X_NV50_PMC_ENABLE,
              v9x_pmc_enable_found | V9X_NV50_PMC_ENABLE_PFIFO);
    v9x_write(V9X_PFIFO_250C, V9X_PFIFO_250C_VALUE);
    v9x_write(V9X_PFIFO_2044, V9X_PFIFO_2044_VALUE);
    v9x_write(V9X_NV50_PFIFO_INTR, 0xfffffffful);
    v9x_write(V9X_NV50_PFIFO_INTR_EN, V9X_PFIFO_INTR_EN_VALUE);
    for (index = 0ul; index < V9X_NV50_PFIFO_CHAN_COUNT; ++index) {
        v9x_write(V9X_NV50_PFIFO_CHAN(index), 0ul);
    }
    v9x_write(V9X_PFIFO_CACHE1_PUSH, 1ul);
    v9x_write(V9X_PFIFO_CACHE1_PULL, 1ul);
    v9x_write(V9X_NV50_PFIFO_CACHES, 1ul);
    if (!v9x_run("fifo-init")) {
        v9x_finish("FIFO-REFUSED", 5u);
    }

    /*
     * No VM TLB flush here. nouveau flushes no unit for PFIFO
     * (vmmnv50.c nv50_vmm_flush), and for PGRAPH on the GT215 family it
     * first stops PGRAPH's FIFO access and waits for its units to idle
     * (gr\g84.c g84_gr_tlb_flush, "hw bug workaround"). The first run of
     * this probe (boot 83) raised a raw PGRAPH flush at this point and the
     * machine stopped answering during the stage.
     */

    /* Channel 1: bound and started (g84.c bind, nv50.c start), then the
     * runlist naming it, and the wait nouveau makes for the update. */
    v9x_ops_begin();
    v9x_write(V9X_NV50_PFIFO_CHAN(V9X_CHID), v9x_layout.chan_entry);
    v9x_write(V9X_NV50_PFIFO_RUNLIST_ADDR, v9x_layout.runlist_reg);
    v9x_write(V9X_NV50_PFIFO_RUNLIST_COUNT, v9x_layout.runlist_count);
    runlist_wait = v9x_wait(V9X_NV50_PFIFO_RUNLIST_COUNT,
                            V9X_NV50_PFIFO_RUNLIST_BUSY, 0ul);
    if (!v9x_run("fifo-bind")) {
        v9x_finish("FIFO-REFUSED", 5u);
    }
    v9x_put("Fifo", "RunlistWait", runlist_wait);
    v9x_flush();

    /* One IB entry, then the kick: IB PUT = 1. */
    v9x_ops_begin();
    v9x_write(V9X_NV50_USERD(V9X_CHID) + V9X_NV50_USERD_IB_PUT, 1ul);
    get_wait = v9x_wait(V9X_NV50_USERD(V9X_CHID) + V9X_NV50_USERD_IB_GET,
                        0xfffffffful, 1ul);
    v9x_status_request(&s);
    if (!v9x_run("fifo-kick")) {
        v9x_finish("FIFO-REFUSED", 5u);
    }
    v9x_put("Fifo", "GetWait", get_wait);
    v9x_status_publish("Fifo", &s);
    v9x_text("Fifo", "RefMatches",
             v9x_value(s.ref) == V9X_REF_VALUE ? "yes" : "no");
    v9x_flush();
    if (v9x_timed_out(runlist_wait) || v9x_timed_out(get_wait)) {
        v9x_text("Fifo", "Result", "TIMEOUT");
        return 0;
    }
    v9x_text("Fifo", "Result", "PASS");
    return 1;
}

static int v9x_stage_gr(void)
{
    struct v9x_status_ops s;
    struct v9x_nv50_push push;
    DWORD index;
    DWORD enable_now;
    DWORD read_enable;
    DWORD get_wait;

    /* PMC_ENABLE as it is now, PFIFO back on. */
    v9x_ops_begin();
    read_enable = v9x_read(V9X_NV50_PMC_ENABLE);
    if (!v9x_run("gr-enable")) {
        v9x_finish("GR-REFUSED", 6u);
    }
    enable_now = v9x_value(read_enable);

    v9x_ops_begin();
    /* Reset PGRAPH (mc gt215: 00201000h). */
    v9x_write(V9X_NV50_PMC_ENABLE,
              enable_now & ~V9X_NV50_PMC_ENABLE_PGRAPH);
    v9x_write(V9X_NV50_PMC_ENABLE,
              enable_now | V9X_NV50_PMC_ENABLE_PGRAPH);

    /* nv50_gr_init, in its order (gr\nv50.c). */
    v9x_write(V9X_PGRAPH_CTXCTL, 0x00000004ul);
    v9x_write(0x400804ul, V9X_TRAP_RESET);
    v9x_write(0x406800ul, V9X_TRAP_RESET);
    v9x_write(0x400c04ul, V9X_TRAP_RESET);
    v9x_write(0x401800ul, V9X_TRAP_RESET);
    v9x_write(0x405018ul, V9X_TRAP_RESET);
    v9x_write(0x402000ul, V9X_TRAP_RESET);
    /* Per TP in 1540h bits 15:0: TP 0 only on this card. */
    v9x_write(0x408600ul, V9X_TRAP_RESET);
    v9x_write(0x408708ul, V9X_TRAP_RESET);
    v9x_write(0x40831cul, V9X_TRAP_RESET);
    v9x_write(V9X_PGRAPH_TRAP, 0xfffffffful);
    v9x_write(0x400138ul, 0xfffffffful);
    v9x_write(V9X_PGRAPH_INTR, 0xfffffffful);
    v9x_write(0x40013cul, 0xfffffffful);
    v9x_write(V9X_PGRAPH_FIFO, 0x00010001ul);
    /* nv50_grctx_init: the context program. */
    v9x_write(V9X_PGRAPH_CTXPROG_INDEX, 0ul);
    for (index = 0ul; index < v9x_layout.program_words; ++index) {
        v9x_write(V9X_PGRAPH_CTXPROG_DATA, v9x_layout.program[index]);
    }
    v9x_write(0x400824ul, 0ul);
    v9x_write(0x400828ul, 0ul);
    v9x_write(0x40082cul, 0ul);
    v9x_write(0x400830ul, 0ul);
    v9x_write(0x40032cul, 0ul);
    v9x_write(0x400330ul, 0ul);
    /* Zcull for A8 (not A0/AA/AC). */
    v9x_write(0x402cc0ul, 0ul);
    v9x_write(0x402ca8ul, 0x00000002ul);
    for (index = 0ul; index < 8ul; ++index) {
        v9x_write(0x402c20ul + index * 0x10ul, 0ul);
        v9x_write(0x402c24ul + index * 0x10ul, 0ul);
        v9x_write(0x402c28ul + index * 0x10ul, 0ul);
        v9x_write(0x402c2cul + index * 0x10ul, 0ul);
    }
    if (!v9x_run("gr-init")) {
        v9x_finish("GR-REFUSED", 6u);
    }

    /* The 502D bind as IB entry 1. */
    v9x_ops_begin();
    v9x_push_stage(V9X_PUSH_GR, &push);
    v9x_nv50_push_bind_2d(&push);
    v9x_vram_data(V9X_NV50_PUSH + V9X_PUSH_GR * 4ul, push.words,
                  push.count);
    v9x_ib_write(1ul, V9X_PUSH_GR, push.count);
    v9x_write(V9X_NV50_USERD(V9X_CHID) + V9X_NV50_USERD_IB_PUT, 2ul);
    get_wait = v9x_wait(V9X_NV50_USERD(V9X_CHID) + V9X_NV50_USERD_IB_GET,
                        0xfffffffful, 2ul);
    v9x_op(V9X_OP_DELAY, 100000ul, 0ul, 0ul);
    v9x_status_request(&s);
    if (!v9x_run("gr")) {
        v9x_finish("GR-REFUSED", 6u);
    }
    v9x_put("Gr", "GetWait", get_wait);
    v9x_status_publish("Gr", &s);
    v9x_flush();
    if (v9x_timed_out(get_wait)) {
        v9x_text("Gr", "Result", "TIMEOUT");
        return 0;
    }
    if (v9x_value(s.gr_intr) != 0ul) {
        v9x_text("Gr", "Result", "PGRAPH-INTR");
        return 0;
    }
    v9x_text("Gr", "Result", "PASS");
    return 1;
}

static int v9x_stage_fill(void)
{
    struct v9x_status_ops s;
    struct v9x_nv50_push push;
    DWORD get_wait;
    DWORD idle_wait;
    DWORD block_first;
    DWORD rows_per_run = V9X_N5CH_BLOCK / (V9X_FILL_PITCH / 4ul);
    DWORD row;
    DWORD rows;
    DWORD x;
    DWORD word;
    DWORD inside_ok = 0ul;
    DWORD inside_bad = 0ul;
    DWORD outside_ok = 0ul;
    DWORD outside_bad = 0ul;
    DWORD first_bad_x = 0xfffffffful;
    DWORD first_bad_y = 0xfffffffful;
    DWORD first_bad_word = 0ul;
    int inside;

    v9x_ops_begin();
    v9x_push_stage(V9X_PUSH_FILL, &push);
    v9x_nv50_push_fill(&push, V9X_WINDOW_VRAM + V9X_FILL_OFFSET,
                       V9X_FILL_PITCH, V9X_FILL_WIDTH, V9X_FILL_HEIGHT,
                       V9X_502D_FORMAT_X8R8G8B8, V9X_FILL_COLOUR,
                       V9X_FILL_X, V9X_FILL_Y, V9X_FILL_SIZE,
                       V9X_FILL_SIZE);
    v9x_vram_data(V9X_NV50_PUSH + V9X_PUSH_FILL * 4ul, push.words,
                  push.count);
    v9x_ib_write(2ul, V9X_PUSH_FILL, push.count);
    v9x_write(V9X_NV50_USERD(V9X_CHID) + V9X_NV50_USERD_IB_PUT, 3ul);
    get_wait = v9x_wait(V9X_NV50_USERD(V9X_CHID) + V9X_NV50_USERD_IB_GET,
                        0xfffffffful, 3ul);
    /* PGRAPH idle: 400700h zero (gr\g84.c reads it as the busy word). */
    idle_wait = v9x_wait(V9X_PGRAPH_STATUS, 0xfffffffful, 0ul);
    v9x_status_request(&s);
    if (!v9x_run("fill")) {
        v9x_finish("FILL-REFUSED", 7u);
    }
    v9x_put("Fill", "GetWait", get_wait);
    v9x_put("Fill", "IdleWait", idle_wait);
    v9x_status_publish("Fill", &s);
    v9x_flush();

    /* The surface, back through BAR1, a block of rows per run. */
    for (row = 0ul; row < V9X_FILL_HEIGHT; row += rows_per_run) {
        rows = V9X_FILL_HEIGHT - row;
        if (rows > rows_per_run) {
            rows = rows_per_run;
        }
        v9x_ops_begin();
        block_first = v9x_op(V9X_OP_VRAM_BLOCK,
                             V9X_FILL_OFFSET + row * V9X_FILL_PITCH,
                             rows * (V9X_FILL_PITCH / 4ul), 0ul);
        (void)block_first;
        if (!v9x_run("readback")) {
            v9x_finish("READBACK-REFUSED", 7u);
        }
        for (x = 0ul; x < rows * (V9X_FILL_PITCH / 4ul); ++x) {
            DWORD px = x % (V9X_FILL_PITCH / 4ul);
            DWORD py = row + x / (V9X_FILL_PITCH / 4ul);

            word = v9x_result.block[x];
            inside = px >= V9X_FILL_X && px < V9X_FILL_X + V9X_FILL_SIZE &&
                     py >= V9X_FILL_Y && py < V9X_FILL_Y + V9X_FILL_SIZE;
            if (inside) {
                if ((word & V9X_COLOUR_MASK) == V9X_FILL_COLOUR) {
                    ++inside_ok;
                    continue;
                }
                ++inside_bad;
            } else {
                if (word == V9X_FILL_PATTERN) {
                    ++outside_ok;
                    continue;
                }
                ++outside_bad;
            }
            if (first_bad_x == 0xfffffffful) {
                first_bad_x = px;
                first_bad_y = py;
                first_bad_word = word;
            }
        }
    }
    v9x_number("Fill", "InsideOk", inside_ok);
    v9x_number("Fill", "InsideBad", inside_bad);
    v9x_number("Fill", "OutsideOk", outside_ok);
    v9x_number("Fill", "OutsideBad", outside_bad);
    if (first_bad_x != 0xfffffffful) {
        v9x_number("Fill", "FirstBadX", first_bad_x);
        v9x_number("Fill", "FirstBadY", first_bad_y);
        v9x_key("Fill", "FirstBadWord", first_bad_word);
    }
    v9x_flush();
    if (v9x_timed_out(get_wait)) {
        v9x_text("Fill", "Result", "TIMEOUT");
        return 0;
    }
    if (inside_bad == 0ul && outside_bad == 0ul) {
        v9x_text("Fill", "Result", "PASS");
        return 1;
    }
    v9x_text("Fill", "Result", "PIXELS-DIFFER");
    return 0;
}

void __stdcall V9xNv50ChannelEntry(void)
{
    DWORD bus;
    DWORD slot;
    DWORD config = 0ul;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_N5CH_PATH);
    v9x_text("Report", "Tool", "V9XN5CH");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Access", "pfifo-pgraph-bringup");
    v9x_stop_after_fifo = v9x_switch_given("/stop:fifo") ? 1ul : 0ul;
    v9x_stop_after_gr = v9x_switch_given("/stop:gr") ? 1ul : 0ul;

    for (bus = 0ul; bus < 8ul && config == 0ul; ++bus) {
        for (slot = 0ul; slot < 32ul; ++slot) {
            DWORD address = 0x80000000ul | (bus << 16) | (slot << 11);

            if (v9x_config_read(address, 0ul) == 0x0a6510deul) {
                config = address;
                break;
            }
        }
    }
    if (config == 0ul) {
        v9x_finish("NO-GT218", 1u);
    }
    v9x_key("Report", "Config", config);
    v9x_flush();

    v9x_stage_pre();
    v9x_stage_region();
    if (!v9x_stage_fifo()) {
        v9x_finish("FIFO-FAILED", 5u);
    }
    if (v9x_stop_after_fifo != 0ul) {
        v9x_finish("STOPPED-AFTER-FIFO", 0u);
    }
    if (!v9x_stage_gr()) {
        v9x_finish("GR-FAILED", 6u);
    }
    if (v9x_stop_after_gr != 0ul) {
        v9x_finish("STOPPED-AFTER-GR", 0u);
    }
    if (!v9x_stage_fill()) {
        v9x_finish("FILL-FAILED", 7u);
    }
    v9x_finish("COMPLETE", 0u);
}
