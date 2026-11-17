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
#include "velocity9x/nv50_init.h"
#include "velocity9x/nv50_3d.h"
#include "velocity9x/nv50_d3d.h"
#include "velocity9x/nv50_shader.h"
#include "velocity9x/nv50_ps11.h"
#include "velocity9x/ps11.h"

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
#define V9X_OP_FLUSH_PAGE    10ul
#define V9X_OP_TIMEOUT       0xfffffffful

#define V9X_ST_FOUND         0x00000001ul
#define V9X_ST_MAPPED        0x00000002ul
#define V9X_ST_IDENTITY      0x00000004ul
#define V9X_ST_REFUSED       0x00000008ul
#define V9X_ST_COMPLETE      0x00000010ul
#define V9X_ST_TIMEOUT       0x00000040ul

/* The VxD's window: BAR1 + 128 MiB, which is video memory 128 MiB, the
 * first byte past the display driver's 128 MiB BAR1 map and a page the
 * write test measured. Runs 1 and 2 used 192 MiB, never measured; both
 * lost the card at their first access through it. */
#define V9X_WINDOW_VRAM      0x08000000ul
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

/* The semaphore stage fifo releases (cl826f.h), in the window past the
 * fill target and before the BAR1 check page. */
#define V9X_826F_SEMAPHOREA  0x0010ul
#define V9X_826F_DMA_SEMAPHORE 0x0060ul
#define V9X_826F_SEMAPHORED_RELEASE 0x00000002ul
#define V9X_SEMA_OFFSET      0x003f0000ul
#define V9X_SEMA_VALUE       0xfeed0001ul
#define V9X_SEMA_WAIT_LIMIT  1000000ul

/*
 * The VM trap (subdev\fb\nv50.c). nv50_fb_init writes the enable,
 * 000D0FFFh for gt215_fb, without which 100C90h latches nothing.
 * nv50_fb_intr reads a trap when bit 31 is set by writing the enable |
 * word << 24 and reading 100C94h, six words, then acks with bit 31.
 * The probe never acks: the trap stays latched for the next read.
 */
#define V9X_NV50_VM_TRAP        0x100c90ul
#define V9X_NV50_VM_TRAP_DATA   0x100c94ul
#define V9X_NV50_VM_TRAP_ENABLE 0x000d0ffful
#define V9X_NV50_VM_TRAP_PENDING 0x80000000ul
#define V9X_NV50_VM_TRAP_WORD_SHIFT 24u
#define V9X_NV50_VM_TRAP_WORDS  6u

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
/* The resident channel's IB get (fifo\nv04.c nv04_fifo_intr_dma_pusher). */
#define V9X_PFIFO_IB_GET     0x003334ul
#define V9X_PFIFO_CACHE1_PULL 0x003250ul
/* The VM block read in pre: 100C00h-100C9Ch. */
#define V9X_VM_DUMP_WORDS     40ul
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
/* The region goes in two runs, the alias page tables apart (stage
 * region); each part must fit one request. */
typedef char v9x_n5ch_region_fits[
    V9X_NV50_PT_TILED / 4ul <= V9X_N5CH_DATA ? 1 : -1];
typedef char v9x_n5ch_alias_fits[
    (V9X_NV50_REGION_BYTES - V9X_NV50_PT_TILED) / 4ul <= V9X_N5CH_DATA ?
    1 : -1];

static struct v9x_n5ch_request v9x_request;
static struct v9x_n5ch_result v9x_result;
static struct v9x_nv50_chan_layout v9x_layout;
static DWORD v9x_stop_after_region;
static DWORD v9x_push_empty;
static DWORD v9x_flush_none;
static DWORD v9x_ib_unmapped;
static DWORD v9x_push_unmapped;
static DWORD v9x_push_class02;
/* /timeline: run 26's samples inside the kick's own run. Off by default:
 * that run never returned, and a hang there loses the whole put run. */
static DWORD v9x_timeline;
static DWORD v9x_pusher;

/* COMP_VM 3 << 29 | KIND_VM 7Fh << 22 | target VM | access VM | class 02h. */
#define V9X_DMA_FLAGS0_FROM_MEMORY 0x7fc00002ul

/* RAMFC + 50h holds the IB ring's VM address, low word (fifo\g84.c). */
#define V9X_RAMFC_IB_LOW         0x50ul
#define V9X_IB_UNMAPPED_ADDRESS  0x30000000ul
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

static void v9x_log(const char *what, const char *step);

/* Every value also goes to the write-through log: run 6 (boot 87) lost
 * the INI's last sections to the lazy write cache when the machine
 * stopped. */
static void v9x_text(const char *section, const char *key, const char *value)
{
    char name[48];

    WritePrivateProfileStringA(section, key, value, V9X_N5CH_PATH);
    if (lstrlenA(section) + lstrlenA(key) + 2 > (int)sizeof(name)) {
        return;
    }
    lstrcpyA(name, section);
    lstrcatA(name, ".");
    lstrcatA(name, key);
    v9x_log(name, value);
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

/*
 * One line per step, written through and committed before the step runs.
 * Run 2 (boot 84) stopped the machine so soon after start that not even
 * the INI's first writes reached the disk: Win98's lazy write cache loses
 * them on a reset. This log is what survives one.
 */
static void v9x_log(const char *what, const char *step)
{
    HANDLE file;
    DWORD written;
    char line[128];
    char tick[12];

    file = CreateFileA(V9X_DIAG_PATH("V9XN5CH.LOG"), GENERIC_WRITE, 0, 0,
                       OPEN_ALWAYS, FILE_FLAG_WRITE_THROUGH, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    v9x_decimal(tick, GetTickCount());
    lstrcpyA(line, tick);
    lstrcatA(line, " ");
    lstrcatA(line, what);
    lstrcatA(line, " ");
    lstrcatA(line, step);
    lstrcatA(line, "\r\n");
    SetFilePointer(file, 0, 0, FILE_END);
    WriteFile(file, line, (DWORD)lstrlenA(line), &written, 0);
    FlushFileBuffers(file);
    CloseHandle(file);
}

/* count dwords to the log, eight per line, each line named <what><first>. */
static void v9x_log_words(const char *what, const DWORD *words, DWORD count)
{
    char name[16];
    char text[80];
    DWORD first;
    DWORD index;

    for (first = 0ul; first < count; first += 8ul) {
        lstrcpyA(name, what);
        v9x_decimal(name + lstrlenA(name), first);
        text[0] = '\0';
        for (index = first; index < count && index < first + 8ul; ++index) {
            v9x_hex(text + lstrlenA(text), words[index], 8);
            lstrcatA(text, " ");
        }
        v9x_log(name, text);
    }
}

/* The card's configuration address, found at start-up. */
static DWORD v9x_config;

/*
 * After every DIOC run: is the card still in configuration space? Run 1
 * (boot 83) lost it between the region run and the next, which found no
 * 10DE:0A65 at all, and the machine hung after the EXE exited. Each run's
 * configuration dwords 0 and 4 name the run after which it went.
 */
static void v9x_health(const char *stage)
{
    DWORD id;
    char key[32];

    id = v9x_config_read(v9x_config, 0ul);
    lstrcpyA(key, stage);
    lstrcatA(key, ".Cfg0");
    v9x_key("Health", key, id);
    lstrcpyA(key, stage);
    lstrcatA(key, ".Cfg4");
    v9x_key("Health", key, v9x_config_read(v9x_config, 4ul));
    if (id != 0x0a6510deul) {
        lstrcpyA(key, "CARD-LOST-AFTER-");
        lstrcatA(key, stage);
        v9x_finish(key, 8u);
    }
    v9x_flush();
}

/*
 * PCIe error state around the kick (run 21). Runs 14-20 found the
 * card's PCI status bit 13 (received master abort) set by the kick, with
 * no VM trap, and nothing in nouveau's init left to compare. A requester
 * sets that bit when its request completes as Unsupported Request, so
 * the root port above it detected one. This logs, through CF8h only:
 * the root port's status and secondary status, the PCIe Device Status
 * of the root port and of the card (Unsupported Request Detected is bit
 * 3 of the status half), and the root port's Root Status. The address
 * of the request sits in AER header logs, read separately.
 */
#define V9X_CAP_POINTER       0x34ul
#define V9X_CAP_PCIE          0x10ul
#define V9X_PCIE_DEVICE_CTL   0x08ul
#define V9X_PCIE_ROOT_STATUS  0x20ul
#define V9X_BRIDGE_BUSES      0x18ul
#define V9X_BRIDGE_SEC_STATUS 0x1cul

static DWORD v9x_bridge;
static DWORD v9x_gpu_pcie;
static DWORD v9x_bridge_pcie;

static DWORD v9x_find_cap(DWORD device, DWORD id)
{
    DWORD at = v9x_config_read(device, V9X_CAP_POINTER) & 0xfcul;
    DWORD hops;

    for (hops = 0ul; hops < 48ul && at >= 0x40ul; ++hops) {
        DWORD word = v9x_config_read(device, at);

        if ((word & 0xfful) == id) {
            return at;
        }
        at = (word >> 8) & 0xfcul;
    }
    return 0ul;
}

/* The bridge on bus 0 whose secondary bus is the card's. */
static void v9x_find_bridge(void)
{
    DWORD bus = (v9x_config >> 16) & 0xfful;
    DWORD slot;
    DWORD function;

    for (slot = 0ul; slot < 32ul; ++slot) {
        for (function = 0ul; function < 8ul; ++function) {
            DWORD device = 0x80000000ul | (slot << 11) | (function << 8);
            DWORD header;

            if (v9x_config_read(device, 0ul) == 0xfffffffful) {
                continue;
            }
            header = (v9x_config_read(device, 0x0cul) >> 16) & 0x7ful;
            if (header == 1ul &&
                ((v9x_config_read(device, V9X_BRIDGE_BUSES) >> 8) & 0xfful) ==
                    bus) {
                v9x_bridge = device;
                v9x_bridge_pcie = v9x_find_cap(device, V9X_CAP_PCIE);
                return;
            }
        }
    }
}

static void v9x_pcie_key(const char *stage, const char *name, DWORD value)
{
    char key[40];

    lstrcpyA(key, stage);
    lstrcatA(key, ".");
    lstrcatA(key, name);
    v9x_key("Pcie", key, value);
}

static void v9x_pcie_status(const char *stage)
{
    if (v9x_gpu_pcie != 0ul) {
        v9x_pcie_key(stage, "GpuDevCtlSta",
                     v9x_config_read(v9x_config,
                                     v9x_gpu_pcie + V9X_PCIE_DEVICE_CTL));
    }
    if (v9x_bridge != 0ul) {
        v9x_pcie_key(stage, "BridgeCfg4", v9x_config_read(v9x_bridge, 4ul));
        v9x_pcie_key(stage, "BridgeSecStatus",
                     v9x_config_read(v9x_bridge, V9X_BRIDGE_SEC_STATUS));
    }
    if (v9x_bridge_pcie != 0ul) {
        v9x_pcie_key(stage, "BridgeDevCtlSta",
                     v9x_config_read(v9x_bridge,
                                     v9x_bridge_pcie + V9X_PCIE_DEVICE_CTL));
        v9x_pcie_key(stage, "BridgeRootStatus",
                     v9x_config_read(v9x_bridge,
                                     v9x_bridge_pcie + V9X_PCIE_ROOT_STATUS));
    }
    v9x_flush();
}

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
    v9x_log("returned", stage);
    /* The put run's raw results, before any other access: run 12
     * (boot 90) stopped the machine between this run returning and the
     * health check, and the results were lost. */
    if (lstrcmpA(stage, "fifo-put") == 0) {
        v9x_log_words("op", v9x_result.results, v9x_request.count);
        v9x_log_words("block", v9x_result.block, v9x_result.block_used);
    }
    v9x_health(stage);
    v9x_log("healthy", stage);
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
    v9x_log("run", step);
}

static DWORD v9x_value(DWORD op)
{
    return v9x_result.results[op];
}

/* A result into the stage's section as <name>=value. */
static void v9x_put(const char *section, const char *name, DWORD op)
{
    if (op == 0xfffffffful) {
        return;
    }
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

/* Which reads a status request makes besides PMC, PFIFO and the VM. */
#define V9X_STATUS_USERD     0x00000001ul
#define V9X_STATUS_GR        0x00000002ul
#define V9X_STATUS_ALL       (V9X_STATUS_USERD | V9X_STATUS_GR)

/* An op index meaning the read was not made: v9x_put writes no key. */
#define V9X_OP_NOT_READ      0xfffffffful

static void v9x_status_request(struct v9x_status_ops *s, DWORD which)
{
    DWORD index;

    s->ib_get = V9X_OP_NOT_READ;
    s->ib_put = V9X_OP_NOT_READ;
    s->ref = V9X_OP_NOT_READ;
    s->gr_intr = V9X_OP_NOT_READ;
    s->gr_trap = V9X_OP_NOT_READ;
    s->gr_status = V9X_OP_NOT_READ;
    s->gr_ctx = V9X_OP_NOT_READ;
    s->gr_trap_status = V9X_OP_NOT_READ;
    s->gr_trap_addr = V9X_OP_NOT_READ;
    s->gr_trap_data = V9X_OP_NOT_READ;
    s->gr_trap_class = V9X_OP_NOT_READ;
    s->pmc_enable = v9x_read(V9X_NV50_PMC_ENABLE);
    s->pmc_intr = v9x_read(0x000100ul);
    s->pfifo_intr = v9x_read(V9X_NV50_PFIFO_INTR);
    s->caches = v9x_read(V9X_NV50_PFIFO_CACHES);
    s->chan = v9x_read(V9X_NV50_PFIFO_CHAN(V9X_CHID));
    s->runlist = v9x_read(V9X_NV50_PFIFO_RUNLIST_COUNT);
    s->push = v9x_read(V9X_PFIFO_CACHE1_PUSH);
    s->pull = v9x_read(V9X_PFIFO_CACHE1_PULL);
    if ((which & V9X_STATUS_USERD) != 0ul) {
        s->ib_get = v9x_read(V9X_NV50_USERD(V9X_CHID) +
                             V9X_NV50_USERD_IB_GET);
        s->ib_put = v9x_read(V9X_NV50_USERD(V9X_CHID) +
                             V9X_NV50_USERD_IB_PUT);
        s->ref = v9x_read(V9X_NV50_USERD(V9X_CHID) + V9X_NV50_USERD_REF);
    }
    if ((which & V9X_STATUS_GR) != 0ul) {
        s->gr_intr = v9x_read(V9X_PGRAPH_INTR);
        s->gr_trap = v9x_read(V9X_PGRAPH_TRAP);
        s->gr_status = v9x_read(V9X_PGRAPH_STATUS);
        s->gr_ctx = v9x_read(0x40032cul);
        s->gr_trap_status = v9x_read(0x400804ul);
        s->gr_trap_addr = v9x_read(0x400808ul);
        s->gr_trap_data = v9x_read(0x40080cul);
        s->gr_trap_class = v9x_read(0x400814ul);
    }
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
static DWORD v9x_units_found;

/* The window's last page, which neither the region (B0000h bytes) nor the
 * fill target (2 MiB + 36 KiB) reaches: one marker written and read back
 * through BAR1 between runs. */
#define V9X_VRAM_CHECK_OFFSET 0x003ff000ul

static DWORD v9x_vram_check_count;

/* One dword through BAR1 and back. Run 1's region readback was all
 * FFFFFFFF; this says after which run BAR1 stopped returning memory. */
static void v9x_vram_check(const char *after)
{
    DWORD marker[1];
    DWORD read;
    char key[32];

    marker[0] = 0xc0de0000ul | (++v9x_vram_check_count & 0xfffful);
    v9x_ops_begin();
    v9x_vram_data(V9X_VRAM_CHECK_OFFSET, marker, 1ul);
    read = v9x_op(V9X_OP_VRAM_READ, V9X_VRAM_CHECK_OFFSET, 0ul, 0ul);
    lstrcpyA(key, "vram-");
    lstrcatA(key, after);
    if (!v9x_run(key)) {
        v9x_finish("VRAM-CHECK-REFUSED", 3u);
    }
    v9x_put("Vram", after, read);
    v9x_text("Vram", key, v9x_value(read) == marker[0] ? "ok" : "differs");
    v9x_flush();
}

/*
 * Reads before any write, in three runs, BAR1 checked after each. Run 1
 * read all of these in one run; the PGRAPH reads 400100h-400810h and the
 * USERD reads returned FFFFFFFF while 40008Ch, 400814h and 100C9xh read
 * plausible values, and the card was gone two runs later.
 */
static void v9x_stage_pre(void)
{
    struct v9x_status_ops s;
    DWORD enable;
    DWORD intr_en;
    DWORD ctxctl;
    DWORD runlist_addr;
    DWORD index;
    DWORD vm[V9X_VM_DUMP_WORDS];

    v9x_ops_begin();
    enable = v9x_read(V9X_NV50_PMC_ENABLE);
    intr_en = v9x_read(0x000140ul);
    runlist_addr = v9x_read(V9X_NV50_PFIFO_RUNLIST_ADDR);
    v9x_status_request(&s, 0ul);
    /* The VM block, 100C00h-100C9Ch: 100C08h is the sysmem flush page
     * (nv50_fb_sysmem_flush_page_init, a host page nouveau allocates;
     * a bad one causes IOMMU errors). The probe never set it. */
    for (index = 0ul; index < V9X_VM_DUMP_WORDS; ++index) {
        vm[index] = v9x_read(0x100c00ul + index * 4ul);
    }
    if (!v9x_run("pre")) {
        v9x_finish("PRE-REFUSED", 3u);
    }
    for (index = 0ul; index < V9X_VM_DUMP_WORDS; ++index) {
        char key[8];

        v9x_hex(key, 0x100c00ul + index * 4ul, 6);
        v9x_put("Vm", key, vm[index]);
    }
    v9x_key("Report", "Bar0", v9x_result.bar0);
    v9x_key("Report", "Bar1", v9x_result.bar1);
    v9x_key("Report", "Boot0", v9x_result.boot0);
    v9x_put("Pre", "PmcIntrEn", intr_en);
    v9x_put("Pre", "RunlistAddr", runlist_addr);
    v9x_status_publish("Pre", &s);
    v9x_pmc_enable_found = v9x_value(enable);
    v9x_flush();
    v9x_vram_check("pre");

    /*
     * No USERD read before channel 1 is bound. Run 3 (boot 85) read IB
     * GET, IB PUT and REF_CNT at C02088h-C02090h here: the three reads
     * took 20 ms and returned FFFFFFFF, the card was still in
     * configuration space after them, and the next BAR1 access lost it
     * (configuration dword 0 FFFFFFFF). Run 1 read the same words in its
     * pre stage and lost the card at its next BAR1 access too.
     */

    v9x_ops_begin();
    ctxctl = v9x_read(V9X_PGRAPH_CTXCTL);
    v9x_status_request(&s, V9X_STATUS_GR);
    if (!v9x_run("pre-gr")) {
        v9x_finish("PRE-REFUSED", 3u);
    }
    v9x_put("PreGr", "PgraphCtxCtl", ctxctl);
    v9x_status_publish("PreGr", &s);
    v9x_flush();
    v9x_vram_check("pre-gr");
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
    DWORD address = v9x_layout.push_addr + first_word * 4ul;

    /* /push:unmapped, run 23's control: run 22 showed the IB fetch goes
     * through the channel's VM (an unmapped IB trapped, no master abort).
     * This moves the pushbuffer the IB entry names to the same unmapped
     * address, to see whether the push fetch does too. */
    if (v9x_push_unmapped != 0ul) {
        address = V9X_IB_UNMAPPED_ADDRESS;
    }
    v9x_nv50_ib_entry(address, words, entry);
    v9x_vram_data(V9X_NV50_IB + entry_index * 8ul, entry, 2ul);
}

static void v9x_stage_region(void)
{
    struct v9x_nv50_grctx_input grctx;
    struct v9x_nv50_push push;
    DWORD data[4];
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
    v9x_units_found = grctx.units;
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
    /*
     * /ib:unmapped, the control for runs 17-21: the RAMFC's IB address
     * (g84.c, RAMFC + 50h) moved above the 512 MiB the page directory
     * maps, where PDE 1 is zero. If PFIFO fetches the IB through the
     * channel's VM, the fetch must fault, and with the trap enabled
     * (run 17) 100C90h bit 31 must set. If the kick still master-aborts
     * with no trap, the IB fetch does not go through this VM.
     */
    /*
     * /push:class02, run 24: the push DMA object (the one RAMFC + 48h
     * names) as nouveau builds it for Tesla (nouveau_chan.c: target VM,
     * access VM, NV_DMA_FROM_MEMORY; dma\usernv50.c: flags0 7FC00002h).
     * The probe's has class 3Dh, NV_DMA_IN_MEMORY. After run 23 it is the
     * one difference left in that object besides its limit.
     */
    if (v9x_push_class02 != 0ul) {
        v9x_region[(V9X_NV50_INST + V9X_NV50_INST_DMA_VM) / 4ul] =
            V9X_DMA_FLAGS0_FROM_MEMORY;
        v9x_key("Region", "DmaFlags0", V9X_DMA_FLAGS0_FROM_MEMORY);
    }
    if (v9x_ib_unmapped != 0ul) {
        v9x_region[(V9X_NV50_INST + V9X_NV50_INST_RAMFC +
                    V9X_RAMFC_IB_LOW) / 4ul] = V9X_IB_UNMAPPED_ADDRESS;
        v9x_key("Region", "IbUnmapped", V9X_IB_UNMAPPED_ADDRESS);
    }
    v9x_number("Region", "ProgramWords", v9x_layout.program_words);
    v9x_number("Region", "ContextBytes", v9x_layout.grctx_bytes);
    v9x_key("Region", "ChannelEntry", v9x_layout.chan_entry);
    v9x_key("Region", "RunlistReg", v9x_layout.runlist_reg);

    /*
     * Stage fifo's pushbuffer: PFIFO's REF_CNT, then a semaphore release
     * PFIFO performs itself, writing V9X_SEMA_VALUE into the window.
     * That is the fetch's evidence read back through BAR1: run 13
     * (boot 91) showed reads that time out on BAR0 (USERD, unanswered
     * CACHE1 registers) are what loses the card. The semaphore DMA
     * object is the VM one, as nv84_fence binds its VRAM ctxdma with
     * method 60h before the semaphore methods.
     */
    v9x_push_stage(V9X_PUSH_FIFO, &push);
    if (v9x_push_empty != 0ul) {
        /* /push:empty: one word, method 0 with count 0, which carries no
         * method. Run 14 stopped the machine with the methods below and
         * nothing but BAR1 read after the kick; this separates the
         * methods from the kick itself. */
        v9x_region[V9X_NV50_PUSH / 4ul + V9X_PUSH_FIFO] = 0ul;
        push.count = 1ul;
    } else {
        data[0] = V9X_REF_VALUE;
        v9x_nv50_push_method(&push, 0ul, V9X_NV50_MTHD_REF_CNT, 1ul, data);
        data[0] = V9X_NV50_HANDLE_DMA_VM;
        v9x_nv50_push_method(&push, 0ul, V9X_826F_DMA_SEMAPHORE, 1ul,
                             data);
        data[0] = 0ul;
        data[1] = V9X_WINDOW_VRAM + V9X_SEMA_OFFSET;
        data[2] = V9X_SEMA_VALUE;
        data[3] = V9X_826F_SEMAPHORED_RELEASE;
        v9x_nv50_push_method(&push, 0ul, V9X_826F_SEMAPHOREA, 4ul, data);
    }
    v9x_number("Region", "FifoPushWords", push.count);

    /* The alias windows' page tables first, in a run of their own: with
     * them the region (E0000h bytes) is larger than one request's data
     * area (30000h dwords), and they sit at its end. */
    v9x_ops_begin();
    v9x_vram_data(V9X_NV50_PT_TILED, v9x_region + V9X_NV50_PT_TILED / 4ul,
                  (V9X_NV50_REGION_BYTES - V9X_NV50_PT_TILED) / 4ul);
    if (!v9x_run("region-alias")) {
        v9x_finish("REGION-REFUSED", 4u);
    }

    v9x_ops_begin();
    v9x_vram_data(0ul, v9x_region, V9X_NV50_PT_TILED / 4ul);
    v9x_ib_write(0ul, V9X_PUSH_FIFO, push.count);
    v9x_op(V9X_OP_VRAM_FILL, V9X_SEMA_OFFSET, 1ul, 0ul);
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
             v9x_value(read_dma) ==
                 v9x_region[V9X_NV50_INST_DMA_VM / 4ul] ? "ok" : "differs");
    v9x_flush();
}

/*
 * Does the GPU see the region where BAR1 put it? The BAR1 write test
 * showed BAR1 stores and returns data across its 256 MiB, not that BAR1 +
 * x is video memory x. Runs 5 to 8 lost the card 60-100 ms after each
 * kick; if PFIFO reads the RAMFC, page tables and IB from a different
 * place than BAR1 wrote them, it reads garbage, and a garbage PTE can aim
 * the fetch at host memory. PRAMIN (BAR0 700000h, base in 1700h as
 * 1 MiB-aligned address >> 16, subdev\instmem\nv50.c) is the GPU's own
 * view: read the region's words back through it, then restore 1700h.
 */
#define V9X_PRAMIN_BASE_REG  0x001700ul
#define V9X_PRAMIN           0x700000ul
#define V9X_PRAMIN_CHECKS    12u

static int v9x_stage_pramin(void)
{
    static const DWORD offsets[V9X_PRAMIN_CHECKS] = {
        0x00000ul,                         /* engine area */
        0x00020ul,                         /* PGRAPH context pointer */
        V9X_NV50_INST_DMA_VM,              /* DMA object */
        V9X_NV50_INST_PD,                  /* PDE 0 */
        V9X_NV50_INST_RAMFC + 0x48ul,      /* RAMFC push DMA object */
        V9X_NV50_INST_RAMFC + 0x50ul,      /* RAMFC IB address */
        V9X_NV50_INST_RAMFC + 0x80ul,      /* RAMFC RAMHT */
        V9X_NV50_INST_RAMFC + 0x98ul,      /* RAMFC instance */
        V9X_NV50_PT + (V9X_WINDOW_VRAM >> 16) * 8ul, /* the region's PTE */
        V9X_NV50_RUNLIST,                  /* runlist entry */
        V9X_NV50_INST_RAMFC + 0x54ul,      /* RAMFC IB high and length */
        V9X_NV50_PUSH                      /* push word 0 */
    };
    DWORD ops[V9X_PRAMIN_CHECKS];
    DWORD saved;
    DWORD index;
    DWORD matches = 0ul;
    char key[8];

    v9x_ops_begin();
    saved = v9x_read(V9X_PRAMIN_BASE_REG);
    if (!v9x_run("pramin-base")) {
        v9x_finish("PRAMIN-REFUSED", 4u);
    }
    saved = v9x_value(saved);
    v9x_key("Pramin", "Base1700", saved);

    v9x_ops_begin();
    v9x_write(V9X_PRAMIN_BASE_REG, V9X_WINDOW_VRAM >> 16);
    for (index = 0ul; index < V9X_PRAMIN_CHECKS; ++index) {
        ops[index] = v9x_read(V9X_PRAMIN + offsets[index]);
    }
    v9x_write(V9X_PRAMIN_BASE_REG, saved);
    if (!v9x_run("pramin")) {
        v9x_finish("PRAMIN-REFUSED", 4u);
    }
    for (index = 0ul; index < V9X_PRAMIN_CHECKS; ++index) {
        DWORD expected = v9x_region[offsets[index] / 4ul];

        v9x_hex(key, offsets[index], 5);
        v9x_put("Pramin", key, ops[index]);
        if (v9x_value(ops[index]) == expected) {
            ++matches;
        }
    }
    v9x_number("Pramin", "Matches", matches);
    v9x_flush();
    v9x_vram_check("pramin");
    if (matches != V9X_PRAMIN_CHECKS) {
        v9x_text("Pramin", "Result", "DIFFERS");
        return 0;
    }
    v9x_text("Pramin", "Result", "PASS");
    return 1;
}

/*
 * The sysmem flush page. Run 10 (boot 90) read 100C08h = 0: the GPU's
 * flush page was host physical address 0, the real-mode interrupt
 * table. nouveau always points it at a page of its own
 * (nv50_fb_sysmem_flush_page_init). N5CH.VXD allocates one when 100C08h
 * is 0 and never frees it.
 *
 * /flush:none reads 100C08h and allocates nothing. Runs 14 and 15 (boots
 * 93, 94) found PCI status bit 13, a master abort the card received, set
 * by the kick, with a flush page near 2 GiB; a kick with 100C08h left at
 * 0 says whether that page is the address nothing answered.
 */
static void v9x_stage_flush_page(void)
{
    DWORD before;
    DWORD after;
    MEMORYSTATUS memory;

    memory.dwLength = sizeof(memory);
    GlobalMemoryStatus(&memory);
    v9x_key("FlushPage", "TotalPhys", memory.dwTotalPhys);

    v9x_ops_begin();
    before = v9x_read(0x100c08ul);
    if (v9x_flush_none != 0ul) {
        if (!v9x_run("flush-none")) {
            v9x_finish("FLUSH-PAGE-REFUSED", 4u);
        }
        v9x_put("FlushPage", "Before", before);
        v9x_text("FlushPage", "After", "not-set");
        v9x_flush();
        return;
    }
    after = v9x_op(V9X_OP_FLUSH_PAGE, 0ul, 0ul, 0ul);
    if (!v9x_run("flush-page")) {
        v9x_finish("FLUSH-PAGE-REFUSED", 4u);
    }
    v9x_put("FlushPage", "Before", before);
    v9x_put("FlushPage", "After", after);
    v9x_flush();
    if (v9x_value(after) == 0ul) {
        v9x_finish("NO-FLUSH-PAGE", 4u);
    }
    v9x_vram_check("flush-page");
}

/*
 * The BAR channel and the BAR write flush, the two steps nouveau takes
 * before any channel runs that runs 1-17 did not (the source studies in
 * docs\decisions\2026-11-11-gt218-channel-probe-run1-hang.md).
 *
 * 1704h names an instance block of its own, written as
 * nv50_bar_bar2_init writes it: plain, then with bit 30. The block is
 * 128 KiB of zeros (nv50_bar_oneinit's size), so its page directory at
 * + 200h (g84_bar) holds no valid entry. 1708h and 170Ch stay 0 (the VxD refuses
 * them), so BAR1 and BAR3 keep decoding video memory linearly, and the
 * desktop in BAR3 is not involved.
 *
 * Then g84_bar_flush, which nouveau runs after every instance-memory
 * write batch (instmem\nv50.c nv50_instobj_release): 70000h = 1, wait
 * for bit 1 to clear. Here it follows everything the region stage wrote
 * through BAR1.
 */
/*
 * nv50_mc_init (mc\nv50.c, which gt215_mc uses): 200h = FFFFFFFFh,
 * "everything on", before any engine is set up. The VBIOS leaves
 * DFF3F113h; the bits off (200C0EECh) are outside gt215's reset table,
 * so the sources do not name their units. The VxD lets bits be
 * switched on but never off (other than PFIFO and PGRAPH).
 */
/*
 * The card's PCIe extended configuration space, which CF8h cannot reach,
 * through the BAR0 mirror at 88000h (offset = configuration offset). The
 * mirror is recalled from nouveau (pci\nv40.c), not in the saved sources,
 * so its first 16 dwords are first compared with CF8h reads. Then the
 * extended capability list from 100h is walked (id 15:0, next 31:20). If
 * AER (id 1) is there, its 11 dwords are read now and again straight
 * after the kick: uncorrectable status, mask, severity, correctable
 * status, mask, control (first error pointer), and the four-dword header
 * log of the first error, which holds the request's address.
 */
#define V9X_MIRROR            0x088000ul
#define V9X_MIRROR_CHECK_WORDS 16u
#define V9X_EXT_BASE          0x100ul
#define V9X_EXT_LIMIT         0x400ul
#define V9X_EXT_WORDS         ((V9X_EXT_LIMIT - V9X_EXT_BASE) / 4ul)
#define V9X_EXT_CAP_AER       0x0001ul
#define V9X_AER_WORDS         11u

static DWORD v9x_aer;

static void v9x_aer_request(DWORD *ops)
{
    DWORD index;

    for (index = 0ul; index < V9X_AER_WORDS; ++index) {
        ops[index] = v9x_read(V9X_MIRROR + v9x_aer + index * 4ul);
    }
}

static void v9x_aer_publish(const char *stage, const DWORD *ops)
{
    DWORD index;
    char key[40];

    for (index = 0ul; index < V9X_AER_WORDS; ++index) {
        lstrcpyA(key, stage);
        lstrcatA(key, ".Aer");
        v9x_hex(key + lstrlenA(key), index * 4ul, 2);
        v9x_put("Pcie", key, ops[index]);
    }
}

static void v9x_stage_pcie(void)
{
    DWORD check[V9X_MIRROR_CHECK_WORDS];
    DWORD ext[V9X_EXT_WORDS];
    DWORD index;
    DWORD matches = 0ul;
    DWORD at;
    DWORD hops;
    char key[16];

    v9x_find_bridge();
    v9x_gpu_pcie = v9x_find_cap(v9x_config, V9X_CAP_PCIE);
    v9x_key("Pcie", "Bridge", v9x_bridge);
    v9x_key("Pcie", "GpuCap", v9x_gpu_pcie);
    v9x_key("Pcie", "BridgeCap", v9x_bridge_pcie);
    v9x_flush();

    v9x_ops_begin();
    for (index = 0ul; index < V9X_MIRROR_CHECK_WORDS; ++index) {
        check[index] = v9x_read(V9X_MIRROR + index * 4ul);
    }
    for (index = 0ul; index < V9X_EXT_WORDS; ++index) {
        ext[index] = v9x_read(V9X_MIRROR + V9X_EXT_BASE + index * 4ul);
    }
    if (!v9x_run("pcie")) {
        v9x_finish("PCIE-REFUSED", 4u);
    }
    for (index = 0ul; index < V9X_MIRROR_CHECK_WORDS; ++index) {
        if (v9x_value(check[index]) ==
            v9x_config_read(v9x_config, index * 4ul)) {
            ++matches;
        }
    }
    v9x_key("Pcie", "MirrorMatches", matches);

    /* The extended capability headers, as found. */
    at = V9X_EXT_BASE;
    for (hops = 0ul; hops < 32ul && at >= V9X_EXT_BASE &&
                     at < V9X_EXT_LIMIT; ++hops) {
        DWORD header = v9x_value(ext[(at - V9X_EXT_BASE) / 4ul]);

        lstrcpyA(key, "Ext");
        v9x_hex(key + 3, at, 3);
        v9x_key("Pcie", key, header);
        if (header == 0ul || header == 0xfffffffful) {
            break;
        }
        if ((header & 0xfffful) == V9X_EXT_CAP_AER) {
            v9x_aer = at;
        }
        at = (header >> 20) & 0xffcul;
    }
    v9x_key("Pcie", "Aer", v9x_aer);
    if (v9x_aer != 0ul && v9x_aer + V9X_AER_WORDS * 4ul > V9X_EXT_LIMIT) {
        v9x_aer = 0ul;
    }
    if (v9x_aer != 0ul) {
        DWORD words[V9X_AER_WORDS];

        for (index = 0ul; index < V9X_AER_WORDS; ++index) {
            words[index] = ext[(v9x_aer - V9X_EXT_BASE) / 4ul + index];
        }
        v9x_aer_publish("pre", words);
    }
    v9x_flush();
    v9x_pcie_status("pre");
}

#define V9X_NV50_PMC_ENABLE_ALL 0xfffffffful

static void v9x_stage_mc(void)
{
    DWORD enable;

    v9x_ops_begin();
    v9x_write(V9X_NV50_PMC_ENABLE, V9X_NV50_PMC_ENABLE_ALL);
    v9x_op(V9X_OP_DELAY, 1000ul, 0ul, 0ul);
    enable = v9x_read(V9X_NV50_PMC_ENABLE);
    if (!v9x_run("mc")) {
        v9x_finish("MC-REFUSED", 4u);
    }
    v9x_put("Mc", "Enable", enable);
    v9x_flush();
    v9x_pmc_enable_found = v9x_value(enable);
    v9x_vram_check("mc");
}

#define V9X_BAR_INST_OFFSET  0x00100000ul
#define V9X_BAR_INST_BYTES   0x00020000ul
#define V9X_NV50_BAR_CHANNEL 0x001704ul
#define V9X_NV50_BAR_CHANNEL_BIT30 0x40000000ul
#define V9X_G84_BAR_FLUSH    0x070000ul
#define V9X_G84_BAR_FLUSH_BUSY 0x00000002ul

static void v9x_stage_bar(void)
{
    DWORD inst = (V9X_WINDOW_VRAM + V9X_BAR_INST_OFFSET) >> 12;
    DWORD before;
    DWORD channel;
    DWORD flush_wait;
    DWORD flush;

    v9x_ops_begin();
    before = v9x_read(V9X_NV50_BAR_CHANNEL);
    v9x_op(V9X_OP_VRAM_FILL, V9X_BAR_INST_OFFSET, V9X_BAR_INST_BYTES / 4ul,
           0ul);
    v9x_write(V9X_NV50_BAR_CHANNEL, inst);
    v9x_write(V9X_NV50_BAR_CHANNEL, V9X_NV50_BAR_CHANNEL_BIT30 | inst);
    channel = v9x_read(V9X_NV50_BAR_CHANNEL);
    v9x_write(V9X_G84_BAR_FLUSH, 1ul);
    flush_wait = v9x_wait(V9X_G84_BAR_FLUSH, V9X_G84_BAR_FLUSH_BUSY, 0ul);
    flush = v9x_read(V9X_G84_BAR_FLUSH);
    if (!v9x_run("bar")) {
        v9x_finish("BAR-REFUSED", 4u);
    }
    v9x_put("Bar", "ChannelBefore", before);
    v9x_put("Bar", "Channel", channel);
    v9x_put("Bar", "FlushWait", flush_wait);
    v9x_put("Bar", "Flush", flush);
    v9x_flush();
    v9x_vram_check("bar");
    if (v9x_timed_out(flush_wait)) {
        v9x_finish("BAR-FLUSH-TIMEOUT", 4u);
    }
}

/*
 * After-kick samples: registers that have answered on this card in every
 * run, plus one video memory word through BAR1, read about every 20 ms.
 * Two log lines per sample, six values each, in this order:
 *   a: PTIMER low, PMC_INTR, PFIFO_INTR, 2500h, 2604h, 3200h
 *   b: 3250h, 32ECh, 100C90h, PGRAPH_INTR, PGRAPH status 400700h,
 *      the instance block's first word through BAR1
 */
/* 40 samples, 2000 PTIMER reads apart: run 15's put run (1000 reads)
 * took about 1 ms, so this spans roughly the first 80 ms. */
#define V9X_TIMELINE_SAMPLES 40u
#define V9X_TIMELINE_DELAY   2000ul
#define V9X_SAMPLE_COUNT     50u
#define V9X_SAMPLE_GAP_MS    20u
#define V9X_SAMPLE_REGS      11u

static void v9x_sample_line(const char *what, const DWORD *ops, DWORD first)
{
    char text[64];
    DWORD index;

    text[0] = '\0';
    for (index = first; index < first + 6ul; ++index) {
        char word[10];

        v9x_hex(word, v9x_value(ops[index]), 8);
        lstrcatA(text, word);
        if (index + 1ul < first + 6ul) {
            lstrcatA(text, " ");
        }
    }
    v9x_log(what, text);
}

static void v9x_sample_after_kick(void)
{
    static const DWORD regs[V9X_SAMPLE_REGS] = {
        0x009400ul, 0x000100ul, 0x002100ul, 0x002500ul, 0x002604ul,
        0x003200ul, 0x003250ul, 0x0032ecul, 0x100c90ul, 0x400100ul,
        0x400700ul
    };
    DWORD ops[V9X_SAMPLE_REGS + 1u];
    DWORD sample;
    DWORD index;
    char name[8];
    char what[12];

    for (sample = 0ul; sample < V9X_SAMPLE_COUNT; ++sample) {
        Sleep(V9X_SAMPLE_GAP_MS);
        lstrcpyA(name, "s");
        v9x_hex(name + 1, sample, 2);
        v9x_ops_begin();
        for (index = 0ul; index < V9X_SAMPLE_REGS; ++index) {
            ops[index] = v9x_read(regs[index]);
        }
        ops[V9X_SAMPLE_REGS] = v9x_op(V9X_OP_VRAM_READ, V9X_NV50_INST,
                                      0ul, 0ul);
        if (!v9x_run(name)) {
            v9x_finish("SAMPLE-REFUSED", 5u);
        }
        lstrcpyA(what, name);
        lstrcatA(what, "a");
        v9x_sample_line(what, ops, 0ul);
        lstrcpyA(what, name);
        lstrcatA(what, "b");
        v9x_sample_line(what, ops, 6ul);
    }
}

/*
 * /pusher: the DMA pusher's state (run 27 left PFIFO_INTR 1000h
 * latched; run 28 read IB_EMPTY here). The registers and their order are
 * nv04_fifo_intr_dma_pusher's; the error is state bits 31:29
 * (2 INVALID_MTHD, 4 INVALID_CMD, 5 IB_EMPTY, 6 MEM_FAULT). A run of its
 * own, made after the stage's other results are logged.
 */
static void v9x_pusher_state(void)
{
    static const DWORD regs[8] = {
        0x003244ul, 0x003240ul, 0x003220ul, 0x003228ul,
        0x003328ul, 0x003320ul, 0x003334ul, 0x003330ul
    };
    static const char *const names[8] = {
        "DmaGet", "DmaPut", "Push", "State",
        "GetHigh", "PutHigh", "IbGet", "IbPut"
    };
    DWORD ops[8];
    DWORD index;

    if (v9x_pusher == 0ul) {
        return;
    }
    v9x_ops_begin();
    for (index = 0ul; index < 8ul; ++index) {
        ops[index] = v9x_read(regs[index]);
    }
    if (!v9x_run("pusher")) {
        v9x_finish("PUSHER-REFUSED", 5u);
    }
    for (index = 0ul; index < 8ul; ++index) {
        v9x_put("Pusher", names[index], ops[index]);
    }
    v9x_flush();
    v9x_vram_check("pusher");
}

/*
 * The register bring-up, built once by nv50_init.c from the layout, the
 * units mask and PMC_ENABLE as the pre stage read them. The probe replays
 * it in three VxD runs (PFIFO, PGRAPH, the bind) so each part is logged
 * before the next starts; the HAL engine replays the same list.
 */
static struct v9x_nv50_init_op v9x_init_ops[V9X_NV50_INIT_OPS_MAX];
static struct v9x_nv50_init_list v9x_init_list;

static void v9x_init_build(void)
{
    struct v9x_nv50_init_input input;

    input.chipset = (v9x_result.boot0 >> 20) & 0x1fful;
    input.units = v9x_units_found;
    input.pmc_enable = v9x_pmc_enable_found;
    if (v9x_nv50_init_build(&input, &v9x_layout, v9x_init_ops,
                            V9X_NV50_INIT_OPS_MAX, &v9x_init_list) !=
        V9X_STATUS_OK) {
        v9x_finish("INIT-LIST-FAILED", 5u);
    }
    v9x_number("Init", "Ops", v9x_init_list.count);
    v9x_number("Init", "FifoEnd", v9x_init_list.fifo_end);
    v9x_number("Init", "GrEnd", v9x_init_list.gr_end);
}

/* Add ops [from, to) of the list to the run being built. Returns the op
 * index of the last wait among them, for its result. */
static DWORD v9x_replay(DWORD from, DWORD to)
{
    DWORD last_wait = V9X_OP_NOT_READ;
    DWORD at;

    for (at = from; at < to; ++at) {
        if (v9x_init_ops[at].kind == V9X_NV50_INIT_WAIT) {
            last_wait = v9x_wait(v9x_init_ops[at].reg,
                                 v9x_init_ops[at].mask,
                                 v9x_init_ops[at].value);
        } else {
            v9x_write(v9x_init_ops[at].reg, v9x_init_ops[at].value);
        }
    }
    return last_wait;
}

static void v9x_stage_gr_init(void);

static int v9x_stage_fifo(void)
{
    struct v9x_status_ops s;
    DWORD index;
    DWORD runlist_wait;
    DWORD sema_wait;
    DWORD sema;
    /* An op's result lasts only until the next run, so the two the
     * verdict needs are kept when their runs return. Run 30 read them at
     * the end, from later runs' results, and called a fetched push
     * NOT-FETCHED. */
    int runlist_timed_out;
    DWORD sema_value;
    DWORD trap_enable;
    DWORD trap_stale;
    DWORD empty_wait;
    DWORD trap;
    DWORD trap_word[V9X_NV50_VM_TRAP_WORDS];
    DWORD aer[V9X_AER_WORDS];
    DWORD timeline[V9X_TIMELINE_SAMPLES][5];
    char trap_key[8];

    v9x_init_build();
    v9x_ops_begin();
    /* nv50_fifo_init, the empty runlist and the VM trap enable, replayed
     * from the host-tested list (nv50_init.c). The trap register is read
     * before and after: a trap an earlier run left latched shows in the
     * first reading. */
    trap_stale = v9x_read(V9X_NV50_VM_TRAP);
    empty_wait = v9x_replay(0ul, v9x_init_list.fifo_end);
    trap_enable = v9x_read(V9X_NV50_VM_TRAP);
    if (!v9x_run("fifo-init")) {
        v9x_finish("FIFO-REFUSED", 5u);
    }
    v9x_put("Trap", "Before", trap_stale);
    v9x_put("Trap", "Enable", trap_enable);
    v9x_put("Fifo", "EmptyRunlistWait", empty_wait);
    v9x_flush();

    /*
     * No VM TLB flush here. nouveau flushes no unit for PFIFO
     * (vmmnv50.c nv50_vmm_flush), and for PGRAPH on the GT215 family it
     * first stops PGRAPH's FIFO access and waits for its units to idle
     * (gr\g84.c g84_gr_tlb_flush, "hw bug workaround"). The first run of
     * this probe listed a raw PGRAPH flush here; it never ran (the card
     * was lost before stage fifo, see the run 1 record).
     */

    v9x_stage_gr_init();

    /* Channel 1: bound and started (g84.c bind, nv50.c start), then the
     * runlist naming it, and the wait nouveau makes for the update. */
    v9x_ops_begin();
    runlist_wait = v9x_replay(v9x_init_list.gr_end, v9x_init_list.count);
    if (!v9x_run("fifo-bind")) {
        v9x_finish("FIFO-REFUSED", 5u);
    }
    v9x_put("Fifo", "RunlistWait", runlist_wait);
    runlist_timed_out = v9x_timed_out(runlist_wait);
    v9x_flush();
    v9x_pcie_status("fifo-bind");

    /*
     * One IB entry, then the kick: IB PUT = 1. Only registers that have
     * answered on this card are read afterwards. Run 13 (boot 91) logged
     * the put run's results: PFIFO had switched channel 1 in (CACHE1
     * 3204h 00010001h, 3224h and 322Ch holding RAMFC + 3Ch and + 48h),
     * and about 38 of 64 CACHE1 reads returned FFFFFFFF in a run that
     * took 385 ms, after which BAR1 read FFFFFFFF and the card left
     * configuration space. Runs 3 and 6 lost it after USERD reads the same
     * way. So the fetch's evidence is the semaphore PFIFO releases into
     * the window, read through BAR1, and USERD is not read at all.
     */
    v9x_ops_begin();
    v9x_write(V9X_NV50_USERD(V9X_CHID) + V9X_NV50_USERD_IB_PUT, 1ul);
    /* Run 26's timeline, in the kick's own run: run 25 found BAR1 lost
     * by about 50 ms and BAR0 by about 80 ms, and reads of both return
     * FFFFFFFF rather than hang, so the run can sample through the
     * failure. Each sample is PTIMER, the BAR1 word, PMC_INTR, PFIFO_INTR
     * and PGRAPH status, then a PTIMER-read delay. */
    for (index = 0ul; v9x_timeline != 0ul && index < V9X_TIMELINE_SAMPLES; ++index) {
        timeline[index][0] = v9x_read(0x009400ul);
        timeline[index][1] = v9x_op(V9X_OP_VRAM_READ, V9X_NV50_INST,
                                    0ul, 0ul);
        timeline[index][2] = v9x_read(0x000100ul);
        timeline[index][3] = v9x_read(0x002100ul);
        timeline[index][4] = v9x_read(0x400700ul);
        v9x_op(V9X_OP_DELAY, V9X_TIMELINE_DELAY, 0ul, 0ul);
    }
    /* The trap, in the same run: runs 14-16 lost the card within a second
     * of the kick, but this run's results came back each time. The six
     * words are read whether or not bit 31 is set (an op list cannot
     * branch); with it clear they are not a trap. */
    trap = v9x_read(V9X_NV50_VM_TRAP);
    for (index = 0ul; index < V9X_NV50_VM_TRAP_WORDS; ++index) {
        v9x_write(V9X_NV50_VM_TRAP, V9X_NV50_VM_TRAP_ENABLE |
                  (index << V9X_NV50_VM_TRAP_WORD_SHIFT));
        trap_word[index] = v9x_read(V9X_NV50_VM_TRAP_DATA);
    }
    if (v9x_aer != 0ul) {
        v9x_aer_request(aer);
    }
    v9x_status_request(&s, 0ul);
    if (!v9x_run("fifo-put")) {
        v9x_finish("FIFO-REFUSED", 5u);
    }
    if (v9x_aer != 0ul) {
        v9x_aer_publish("fifo-put", aer);
    }
    v9x_pcie_status("fifo-put");
    v9x_put("Trap", "Status", trap);
    for (index = 0ul; index < V9X_NV50_VM_TRAP_WORDS; ++index) {
        lstrcpyA(trap_key, "Word");
        trap_key[4] = (char)('0' + index);
        trap_key[5] = '\0';
        v9x_put("Trap", trap_key, trap_word[index]);
    }
    v9x_status_publish("FifoPut", &s);
    v9x_flush();
    for (index = 0ul; v9x_timeline != 0ul && index < V9X_TIMELINE_SAMPLES; ++index) {
        char text[56];
        char word[10];
        DWORD column;
        char what[8];

        text[0] = '\0';
        for (column = 0ul; column < 5ul; ++column) {
            v9x_hex(word, v9x_value(timeline[index][column]), 8);
            lstrcatA(text, word);
            if (column < 4ul) {
                lstrcatA(text, " ");
            }
        }
        lstrcpyA(what, "t");
        v9x_hex(what + 1, index, 2);
        v9x_log(what, text);
    }

    /* /push:empty: about a second of short sampling runs (run 25), then
     * configuration space and BAR1. Run 24 left the card answering at
     * about 1 ms and lost by 1 s. Each sample is its own VxD run, logged
     * as soon as it returns, so a read that hangs the bus loses only that
     * sample, and the last line before the gap shows what changed. */
    if (v9x_push_empty != 0ul) {
        v9x_sample_after_kick();
        v9x_health("fifo-sleep");
        v9x_log("healthy", "fifo-sleep");
        v9x_pcie_status("fifo-sleep");
        v9x_vram_check("fifo-sleep");
        v9x_ops_begin();
        v9x_status_request(&s, 0ul);
        if (!v9x_run("fifo-after")) {
            v9x_finish("FIFO-REFUSED", 5u);
        }
        v9x_status_publish("FifoAfter", &s);
        v9x_flush();
        v9x_pusher_state();
        v9x_finish("EMPTY-PUSH-SURVIVED", 0u);
    }

    /* The semaphore, through BAR1 only, bounded. */
    v9x_ops_begin();
    sema_wait = v9x_op(V9X_OP_WAIT_VRAM, V9X_SEMA_OFFSET, V9X_SEMA_VALUE,
                       V9X_SEMA_WAIT_LIMIT);
    sema = v9x_op(V9X_OP_VRAM_READ, V9X_SEMA_OFFSET, 0ul, 0ul);
    if (!v9x_run("fifo-sema")) {
        v9x_finish("FIFO-REFUSED", 5u);
    }
    v9x_put("Fifo", "SemaWait", sema_wait);
    v9x_put("Fifo", "Sema", sema);
    sema_value = v9x_value(sema);
    v9x_flush();

    /* About a second of PTIMER reads, then the card checked again. */
    v9x_ops_begin();
    v9x_op(V9X_OP_DELAY, 1000000ul, 0ul, 0ul);
    v9x_status_request(&s, 0ul);
    if (!v9x_run("fifo-settle")) {
        v9x_finish("FIFO-REFUSED", 5u);
    }
    v9x_status_publish("FifoSettle", &s);
    v9x_flush();
    v9x_vram_check("fifo-settle");
    v9x_pusher_state();

    if (runlist_timed_out || sema_value != V9X_SEMA_VALUE) {
        v9x_text("Fifo", "Result", "NOT-FETCHED");
        return 0;
    }
    v9x_text("Fifo", "Result", "PASS");
    return 1;
}

/*
 * PGRAPH reset, nv50_gr_init and the context program, before any channel
 * is bound: nouveau initialises every engine before it creates a
 * channel, and notes that PFIFO "will hang forever if the connected
 * engines don't report that they've processed the context switch
 * request" (fifo\nv50.c, nv50_ectx_bind). Runs 5 to 7 bound and kicked
 * channel 1, whose instance block names a PGRAPH context, with PGRAPH
 * uninitialised, and the machine stopped 60-100 ms after each kick.
 */
static void v9x_stage_gr_init(void)
{
    struct v9x_status_ops s;

    /* PGRAPH reset, nv50_gr_init and the context program, replayed from
     * the host-tested list (nv50_init.c), which is what runs 27-39 ran. */
    v9x_ops_begin();
    v9x_replay(v9x_init_list.fifo_end, v9x_init_list.gr_end);
    v9x_op(V9X_OP_DELAY, 1000ul, 0ul, 0ul);
    v9x_status_request(&s, V9X_STATUS_GR);
    if (!v9x_run("gr-init")) {
        v9x_finish("GR-REFUSED", 6u);
    }
    v9x_status_publish("GrInit", &s);
    v9x_flush();
    v9x_vram_check("gr-init");
}

static int v9x_stage_gr(void)
{
    struct v9x_status_ops s;
    struct v9x_nv50_push push;
    DWORD get_wait;
    DWORD idle_wait;
    DWORD gr_intr;
    int timed_out;

    /* The 502D bind as IB entry 1. */
    v9x_ops_begin();
    v9x_push_stage(V9X_PUSH_GR, &push);
    v9x_nv50_push_bind_2d(&push);
    v9x_vram_data(V9X_NV50_PUSH + V9X_PUSH_GR * 4ul, push.words,
                  push.count);
    v9x_ib_write(1ul, V9X_PUSH_GR, push.count);
    v9x_write(V9X_NV50_USERD(V9X_CHID) + V9X_NV50_USERD_IB_PUT, 2ul);
    /* GET from the pusher's own register, PFIFO 3334h, which holds the
     * resident channel's IB get (run 29 read 1 there after one entry).
     * USERD is not read: those reads lost the card in runs 3 and 6. */
    get_wait = v9x_wait(V9X_PFIFO_IB_GET, 0xfffffffful, 2ul);
    v9x_op(V9X_OP_DELAY, 100000ul, 0ul, 0ul);
    idle_wait = v9x_wait(V9X_PGRAPH_STATUS, 0xfffffffful, 0ul);
    v9x_status_request(&s, V9X_STATUS_GR);
    if (!v9x_run("gr")) {
        v9x_finish("GR-REFUSED", 6u);
    }
    v9x_put("Gr", "GetWait", get_wait);
    v9x_put("Gr", "IdleWait", idle_wait);
    v9x_status_publish("Gr", &s);
    timed_out = v9x_timed_out(get_wait);
    gr_intr = v9x_value(s.gr_intr);
    v9x_flush();
    v9x_vram_check("gr");
    v9x_pusher_state();
    if (timed_out) {
        v9x_text("Gr", "Result", "TIMEOUT");
        return 0;
    }
    if (gr_intr != 0ul) {
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
    int timed_out;

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
    get_wait = v9x_wait(V9X_PFIFO_IB_GET, 0xfffffffful, 3ul);
    v9x_op(V9X_OP_DELAY, 100000ul, 0ul, 0ul);
    /* PGRAPH idle: 400700h zero (gr\g84.c reads it as the busy word). */
    idle_wait = v9x_wait(V9X_PGRAPH_STATUS, 0xfffffffful, 0ul);
    v9x_status_request(&s, V9X_STATUS_GR);
    if (!v9x_run("fill")) {
        v9x_finish("FILL-REFUSED", 7u);
    }
    v9x_put("Fill", "GetWait", get_wait);
    v9x_put("Fill", "IdleWait", idle_wait);
    v9x_status_publish("Fill", &s);
    timed_out = v9x_timed_out(get_wait);
    v9x_flush();
    v9x_vram_check("fill");
    v9x_pusher_state();

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
    if (timed_out) {
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

/*
 * Step 4.0 of docs\plans\nvidia-gt218-nv50-engine.md: a 502D copy, as IB
 * entry 3. The source block, 16x16 at (0, 0), straddles the filled
 * rectangle's corner at (8, 4), so it holds both the pattern and the
 * colour and a copy that lands a pixel off shows. It goes to (100, 50),
 * which the fill left as pattern. Every pixel of the surface is then
 * read back and compared, the top byte excepted (X8R8G8B8).
 */
#define V9X_PUSH_COPY        192ul
#define V9X_COPY_SRC_X       0ul
#define V9X_COPY_SRC_Y       0ul
#define V9X_COPY_DST_X       100ul
#define V9X_COPY_DST_Y       50ul
#define V9X_COPY_SIZE        16ul

static DWORD v9x_copy_expected(DWORD px, DWORD py)
{
    if (px >= V9X_COPY_DST_X && px < V9X_COPY_DST_X + V9X_COPY_SIZE &&
        py >= V9X_COPY_DST_Y && py < V9X_COPY_DST_Y + V9X_COPY_SIZE) {
        px = px - V9X_COPY_DST_X + V9X_COPY_SRC_X;
        py = py - V9X_COPY_DST_Y + V9X_COPY_SRC_Y;
    }
    if (px >= V9X_FILL_X && px < V9X_FILL_X + V9X_FILL_SIZE &&
        py >= V9X_FILL_Y && py < V9X_FILL_Y + V9X_FILL_SIZE) {
        return V9X_FILL_COLOUR;
    }
    return V9X_FILL_PATTERN & V9X_COLOUR_MASK;
}

static int v9x_stage_copy(void)
{
    struct v9x_status_ops s;
    struct v9x_nv50_push push;
    DWORD get_wait;
    DWORD idle_wait;
    DWORD rows_per_run = V9X_N5CH_BLOCK / (V9X_FILL_PITCH / 4ul);
    DWORD row;
    DWORD rows;
    DWORD x;
    DWORD copied_ok = 0ul;
    DWORD copied_bad = 0ul;
    DWORD rest_ok = 0ul;
    DWORD rest_bad = 0ul;
    DWORD first_bad_x = 0xfffffffful;
    DWORD first_bad_y = 0xfffffffful;
    DWORD first_bad_word = 0ul;
    int timed_out;

    v9x_ops_begin();
    v9x_push_stage(V9X_PUSH_COPY, &push);
    v9x_nv50_push_copy(&push, V9X_WINDOW_VRAM + V9X_FILL_OFFSET,
                       V9X_FILL_PITCH, V9X_FILL_WIDTH, V9X_FILL_HEIGHT,
                       V9X_502D_FORMAT_X8R8G8B8,
                       V9X_COPY_SRC_X, V9X_COPY_SRC_Y,
                       V9X_COPY_DST_X, V9X_COPY_DST_Y,
                       V9X_COPY_SIZE, V9X_COPY_SIZE);
    v9x_vram_data(V9X_NV50_PUSH + V9X_PUSH_COPY * 4ul, push.words,
                  push.count);
    v9x_ib_write(3ul, V9X_PUSH_COPY, push.count);
    v9x_write(V9X_NV50_USERD(V9X_CHID) + V9X_NV50_USERD_IB_PUT, 4ul);
    get_wait = v9x_wait(V9X_PFIFO_IB_GET, 0xfffffffful, 4ul);
    v9x_op(V9X_OP_DELAY, 100000ul, 0ul, 0ul);
    idle_wait = v9x_wait(V9X_PGRAPH_STATUS, 0xfffffffful, 0ul);
    v9x_status_request(&s, V9X_STATUS_GR);
    if (!v9x_run("copy")) {
        v9x_finish("COPY-REFUSED", 7u);
    }
    v9x_put("Copy", "GetWait", get_wait);
    v9x_put("Copy", "IdleWait", idle_wait);
    v9x_status_publish("Copy", &s);
    timed_out = v9x_timed_out(get_wait);
    v9x_flush();
    v9x_vram_check("copy");
    v9x_pusher_state();

    for (row = 0ul; row < V9X_FILL_HEIGHT; row += rows_per_run) {
        rows = V9X_FILL_HEIGHT - row;
        if (rows > rows_per_run) {
            rows = rows_per_run;
        }
        v9x_ops_begin();
        v9x_op(V9X_OP_VRAM_BLOCK, V9X_FILL_OFFSET + row * V9X_FILL_PITCH,
               rows * (V9X_FILL_PITCH / 4ul), 0ul);
        if (!v9x_run("copy-readback")) {
            v9x_finish("READBACK-REFUSED", 7u);
        }
        for (x = 0ul; x < rows * (V9X_FILL_PITCH / 4ul); ++x) {
            DWORD px = x % (V9X_FILL_PITCH / 4ul);
            DWORD py = row + x / (V9X_FILL_PITCH / 4ul);
            DWORD word = v9x_result.block[x] & V9X_COLOUR_MASK;
            int copied = px >= V9X_COPY_DST_X &&
                         px < V9X_COPY_DST_X + V9X_COPY_SIZE &&
                         py >= V9X_COPY_DST_Y &&
                         py < V9X_COPY_DST_Y + V9X_COPY_SIZE;

            if (word == v9x_copy_expected(px, py)) {
                if (copied) {
                    ++copied_ok;
                } else {
                    ++rest_ok;
                }
                continue;
            }
            if (copied) {
                ++copied_bad;
            } else {
                ++rest_bad;
            }
            if (first_bad_x == 0xfffffffful) {
                first_bad_x = px;
                first_bad_y = py;
                first_bad_word = v9x_result.block[x];
            }
        }
    }
    v9x_number("Copy", "CopiedOk", copied_ok);
    v9x_number("Copy", "CopiedBad", copied_bad);
    v9x_number("Copy", "RestOk", rest_ok);
    v9x_number("Copy", "RestBad", rest_bad);
    if (first_bad_x != 0xfffffffful) {
        v9x_number("Copy", "FirstBadX", first_bad_x);
        v9x_number("Copy", "FirstBadY", first_bad_y);
        v9x_key("Copy", "FirstBadWord", first_bad_word);
    }
    v9x_flush();
    if (timed_out) {
        v9x_text("Copy", "Result", "TIMEOUT");
        return 0;
    }
    if (copied_bad == 0ul && rest_bad == 0ul) {
        v9x_text("Copy", "Result", "PASS");
        return 1;
    }
    v9x_text("Copy", "Result", "PIXELS-DIFFER");
    return 0;
}

/*
 * Step 4.0 items 2 and 3: overlapping copies, and 16 and 8 bpp. One test
 * per depth on a surface of its own. The CPU writes a gradient, so every
 * pixel differs from its neighbours; the engine then fills a rectangle
 * and makes two copies whose source and destination overlap, one moving
 * down and right, one up and left. The same three operations are applied
 * to a model in memory, with a copy that reads the whole source first
 * (what DirectDraw expects of an overlapping blit), and the surface read
 * back through BAR1 is compared with the model pixel for pixel. Each
 * operation is a kick of its own; a PGRAPH or PFIFO interrupt, a pusher
 * error or a timeout ends the test.
 */
#define V9X_DEPTH_PIXELS     (V9X_FILL_WIDTH * V9X_FILL_HEIGHT)
#define V9X_DEPTH_FIRST      0x00220000ul
#define V9X_DEPTH_STEP       0x00020000ul
#define V9X_PUSH_DEPTH       256ul

static DWORD v9x_ib_next = 4ul;
static DWORD v9x_push_next = V9X_PUSH_DEPTH;
static DWORD v9x_model[V9X_DEPTH_PIXELS];
static DWORD v9x_model_copy[V9X_DEPTH_PIXELS];
static DWORD v9x_depth_words[V9X_DEPTH_PIXELS];

/* Kick one pushbuffer and wait for it. Nonzero when it ran clean. */
static int v9x_kick(const char *section, const struct v9x_nv50_push *push,
                    DWORD first_word)
{
    struct v9x_status_ops s;
    DWORD get_wait;
    DWORD idle_wait;
    DWORD pusher;
    DWORD clean;

    v9x_vram_data(V9X_NV50_PUSH + first_word * 4ul, push->words,
                  push->count);
    /* The ring's index wraps at its 512 entries (RAMFC + 54h). */
    v9x_ib_write(v9x_ib_next, first_word, push->count);
    v9x_ib_next = (v9x_ib_next + 1ul) % V9X_NV50_IB_ENTRIES;
    v9x_write(V9X_NV50_USERD(V9X_CHID) + V9X_NV50_USERD_IB_PUT,
              v9x_ib_next);
    get_wait = v9x_wait(V9X_PFIFO_IB_GET, 0xfffffffful, v9x_ib_next);
    v9x_op(V9X_OP_DELAY, 100000ul, 0ul, 0ul);
    idle_wait = v9x_wait(V9X_PGRAPH_STATUS, 0xfffffffful, 0ul);
    pusher = v9x_read(0x003228ul);
    v9x_status_request(&s, V9X_STATUS_GR);
    if (!v9x_run(section)) {
        v9x_finish("KICK-REFUSED", 7u);
    }
    clean = !v9x_timed_out(get_wait) && !v9x_timed_out(idle_wait) &&
            v9x_value(s.gr_intr) == 0ul &&
            (v9x_value(pusher) >> 29) == 0ul;
    v9x_put(section, "GetWait", get_wait);
    v9x_put(section, "IdleWait", idle_wait);
    v9x_put(section, "PusherState", pusher);
    v9x_status_publish(section, &s);
    v9x_flush();
    return clean != 0ul;
}

/*
 * Step 4.0 item 4: 600 kicks, more than the ring's 512 entries. One
 * small fill, written once; each kick is a new IB entry naming it, PUT
 * moved on by one modulo the ring, and a wait for PFIFO's IB get to
 * reach it. 150 kicks to a VxD run, to stay inside the op list.
 */
#define V9X_MANY_KICKS       600ul
#define V9X_MANY_PER_RUN     150ul

static int v9x_stage_many(void)
{
    struct v9x_nv50_push push;
    DWORD waits[V9X_MANY_PER_RUN];
    DWORD first_word = v9x_push_next;
    DWORD done = 0ul;
    DWORD timeouts = 0ul;
    DWORD fifo_intr;
    DWORD gr_intr;
    DWORD state;
    DWORD ib_get;
    DWORD index;
    DWORD count;
    int clean = 1;

    v9x_push_next += V9X_PUSH_STAGE_MAX;
    while (done < V9X_MANY_KICKS && clean) {
        count = V9X_MANY_KICKS - done;
        if (count > V9X_MANY_PER_RUN) {
            count = V9X_MANY_PER_RUN;
        }
        v9x_ops_begin();
        if (done == 0ul) {
            v9x_push_stage(first_word, &push);
            v9x_nv50_push_fill(&push, V9X_WINDOW_VRAM + V9X_DEPTH_FIRST,
                               V9X_FILL_WIDTH * 4ul, V9X_FILL_WIDTH,
                               V9X_FILL_HEIGHT, V9X_502D_FORMAT_X8R8G8B8,
                               V9X_FILL_COLOUR, 0ul, 0ul, 4ul, 4ul);
            v9x_vram_data(V9X_NV50_PUSH + first_word * 4ul, push.words,
                          push.count);
        }
        for (index = 0ul; index < count; ++index) {
            v9x_ib_write(v9x_ib_next, first_word, push.count);
            v9x_ib_next = (v9x_ib_next + 1ul) % V9X_NV50_IB_ENTRIES;
            v9x_write(V9X_NV50_USERD(V9X_CHID) + V9X_NV50_USERD_IB_PUT,
                      v9x_ib_next);
            waits[index] = v9x_wait(V9X_PFIFO_IB_GET, 0xfffffffful,
                                    v9x_ib_next);
        }
        v9x_wait(V9X_PGRAPH_STATUS, 0xfffffffful, 0ul);
        fifo_intr = v9x_read(V9X_NV50_PFIFO_INTR);
        gr_intr = v9x_read(0x400100ul);
        state = v9x_read(0x003228ul);
        ib_get = v9x_read(V9X_PFIFO_IB_GET);
        if (!v9x_run("many")) {
            v9x_finish("MANY-REFUSED", 7u);
        }
        for (index = 0ul; index < count; ++index) {
            if (v9x_timed_out(waits[index])) {
                ++timeouts;
            }
        }
        done += count;
        v9x_number("Many", "Kicks", done);
        v9x_number("Many", "Timeouts", timeouts);
        v9x_put("Many", "PfifoIntr", fifo_intr);
        v9x_put("Many", "PgraphIntr", gr_intr);
        v9x_put("Many", "PusherState", state);
        v9x_put("Many", "IbGet", ib_get);
        clean = timeouts == 0ul && v9x_value(fifo_intr) == 0ul &&
                v9x_value(gr_intr) == 0ul &&
                (v9x_value(state) >> 29) == 0ul &&
                v9x_value(ib_get) == v9x_ib_next;
        v9x_flush();
        v9x_vram_check("many");
    }
    v9x_text("Many", "Result", clean ? "PASS" : "NOT-CLEAN");
    v9x_flush();
    return clean;
}

static void v9x_model_copy_rect(DWORD sx, DWORD sy, DWORD dx, DWORD dy,
                                DWORD w, DWORD h)
{
    DWORD x;
    DWORD y;

    for (y = 0ul; y < V9X_DEPTH_PIXELS; ++y) {
        v9x_model_copy[y] = v9x_model[y];
    }
    for (y = 0ul; y < h; ++y) {
        for (x = 0ul; x < w; ++x) {
            v9x_model[(dy + y) * V9X_FILL_WIDTH + dx + x] =
                v9x_model_copy[(sy + y) * V9X_FILL_WIDTH + sx + x];
        }
    }
}

/* Pack the model's pixels into dwords, or compare dwords with it. */
static DWORD v9x_depth_pixel(const DWORD *words, DWORD index, DWORD bytes)
{
    if (bytes == 4ul) {
        return words[index] & V9X_COLOUR_MASK;
    }
    if (bytes == 2ul) {
        return (words[index / 2ul] >> ((index & 1ul) * 16ul)) & 0xfffful;
    }
    return (words[index / 4ul] >> ((index & 3ul) * 8ul)) & 0xfful;
}

static int v9x_stage_depth(const char *section, DWORD number, DWORD bytes,
                           DWORD format, DWORD colour)
{
    struct v9x_nv50_push push;
    DWORD offset = V9X_DEPTH_FIRST + number * V9X_DEPTH_STEP;
    DWORD pitch = V9X_FILL_WIDTH * bytes;
    DWORD words = V9X_DEPTH_PIXELS * bytes / 4ul;
    DWORD mask = bytes == 4ul ? V9X_COLOUR_MASK
                              : (bytes == 2ul ? 0xfffful : 0xfful);
    DWORD index;
    DWORD x;
    DWORD y;
    DWORD at;
    DWORD ok = 0ul;
    DWORD bad = 0ul;
    DWORD first_bad = 0xfffffffful;
    DWORD first_bad_word = 0ul;
    char name[24];

    /* The gradient, into the model and, packed, into the surface. */
    for (index = 0ul; index < words; ++index) {
        v9x_depth_words[index] = 0ul;
    }
    for (index = 0ul; index < V9X_DEPTH_PIXELS; ++index) {
        x = index % V9X_FILL_WIDTH;
        y = index / V9X_FILL_WIDTH;
        v9x_model[index] = ((y << 16) | (x << 8) | ((x * 3ul) ^ (y * 7ul)))
                           & mask;
        if (bytes == 2ul) {
            v9x_model[index] = ((y << 8) | x) & mask;
        }
        if (bytes == 4ul) {
            v9x_depth_words[index] = v9x_model[index];
        } else if (bytes == 2ul) {
            v9x_depth_words[index / 2ul] |=
                v9x_model[index] << ((index & 1ul) * 16ul);
        } else {
            v9x_depth_words[index / 4ul] |=
                v9x_model[index] << ((index & 3ul) * 8ul);
        }
    }

    /* The fill. */
    v9x_ops_begin();
    v9x_vram_data(offset, v9x_depth_words, words);
    v9x_push_stage(v9x_push_next, &push);
    v9x_nv50_push_fill(&push, V9X_WINDOW_VRAM + offset, pitch,
                       V9X_FILL_WIDTH, V9X_FILL_HEIGHT, format, colour,
                       V9X_FILL_X, V9X_FILL_Y, V9X_FILL_SIZE,
                       V9X_FILL_SIZE);
    lstrcpyA(name, section);
    lstrcatA(name, "Fill");
    if (!v9x_kick(name, &push, v9x_push_next)) {
        v9x_text(section, "Result", "FILL-NOT-CLEAN");
        return 0;
    }
    v9x_push_next += V9X_PUSH_STAGE_MAX;
    for (y = V9X_FILL_Y; y < V9X_FILL_Y + V9X_FILL_SIZE; ++y) {
        for (x = V9X_FILL_X; x < V9X_FILL_X + V9X_FILL_SIZE; ++x) {
            v9x_model[y * V9X_FILL_WIDTH + x] = colour & mask;
        }
    }

    /* Down and right: 32x32 from (16, 16) to (24, 20). */
    v9x_ops_begin();
    v9x_push_stage(v9x_push_next, &push);
    v9x_nv50_push_copy(&push, V9X_WINDOW_VRAM + offset, pitch,
                       V9X_FILL_WIDTH, V9X_FILL_HEIGHT, format,
                       16ul, 16ul, 24ul, 20ul, 32ul, 32ul);
    lstrcpyA(name, section);
    lstrcatA(name, "CopyDown");
    if (!v9x_kick(name, &push, v9x_push_next)) {
        v9x_text(section, "Result", "COPY-DOWN-NOT-CLEAN");
        return 0;
    }
    v9x_push_next += V9X_PUSH_STAGE_MAX;
    v9x_model_copy_rect(16ul, 16ul, 24ul, 20ul, 32ul, 32ul);

    /* Up and left: 40x30 from (60, 30) to (50, 24). */
    v9x_ops_begin();
    v9x_push_stage(v9x_push_next, &push);
    v9x_nv50_push_copy(&push, V9X_WINDOW_VRAM + offset, pitch,
                       V9X_FILL_WIDTH, V9X_FILL_HEIGHT, format,
                       60ul, 30ul, 50ul, 24ul, 40ul, 30ul);
    lstrcpyA(name, section);
    lstrcatA(name, "CopyUp");
    if (!v9x_kick(name, &push, v9x_push_next)) {
        v9x_text(section, "Result", "COPY-UP-NOT-CLEAN");
        return 0;
    }
    v9x_push_next += V9X_PUSH_STAGE_MAX;
    v9x_model_copy_rect(60ul, 30ul, 50ul, 24ul, 40ul, 30ul);
    v9x_vram_check(section);

    /* Back through BAR1, a block at a time, against the model. */
    for (at = 0ul; at < words; at += V9X_N5CH_BLOCK) {
        DWORD count = words - at;

        if (count > V9X_N5CH_BLOCK) {
            count = V9X_N5CH_BLOCK;
        }
        v9x_ops_begin();
        v9x_op(V9X_OP_VRAM_BLOCK, offset + at * 4ul, count, 0ul);
        if (!v9x_run("depth-readback")) {
            v9x_finish("READBACK-REFUSED", 7u);
        }
        for (index = 0ul; index < count; ++index) {
            v9x_depth_words[at + index] = v9x_result.block[index];
        }
    }
    for (index = 0ul; index < V9X_DEPTH_PIXELS; ++index) {
        DWORD pixel = v9x_depth_pixel(v9x_depth_words, index, bytes);

        if (pixel == v9x_model[index]) {
            ++ok;
            continue;
        }
        ++bad;
        if (first_bad == 0xfffffffful) {
            first_bad = index;
            first_bad_word = pixel;
        }
    }
    v9x_number(section, "PixelsOk", ok);
    v9x_number(section, "PixelsBad", bad);
    if (first_bad != 0xfffffffful) {
        v9x_number(section, "FirstBadX", first_bad % V9X_FILL_WIDTH);
        v9x_number(section, "FirstBadY", first_bad / V9X_FILL_WIDTH);
        v9x_key(section, "FirstBadPixel", first_bad_word);
        v9x_key(section, "FirstBadModel", v9x_model[first_bad]);
    }
    v9x_text(section, "Result", bad == 0ul ? "PASS" : "PIXELS-DIFFER");
    v9x_flush();
    return bad == 0ul;
}

/*
 * Step 4.0 item 5, /check: is a channel an earlier run brought up still
 * there? Nothing is initialised or rewritten. The run rebuilds the region
 * in memory only, to know what the words should be, reads the registers
 * the bring-up set and a sample of the region through BAR1, and logs
 * each beside its expected value. Only if the channel's table entry, the
 * runlist and PGRAPH's context still name this channel, and the sampled
 * words all match, does it go on to use the channel: the 32 bpp depth
 * test, continuing the IB ring from where PFIFO's get stands. Run
 * before and after a desktop mode change, it says what a mode set costs
 * the engine.
 */
static void v9x_stage_check(void)
{
    static const DWORD regs[] = {
        0x000200ul, 0x002500ul, 0x002604ul, 0x0032ecul, 0x0032f4ul,
        0x003200ul, 0x003250ul, 0x003334ul, 0x003330ul, 0x003244ul,
        0x003240ul, 0x003228ul, 0x002100ul, 0x002140ul, 0x002044ul,
        0x00250cul, 0x40008cul, 0x400100ul, 0x400700ul, 0x40032cul,
        0x400500ul, 0x001704ul, 0x100c90ul
    };
    static const DWORD words[] = {
        V9X_NV50_INST_ENG + V9X_NV50_ENG_GR,
        V9X_NV50_INST_ENG + V9X_NV50_ENG_GR + 8ul,
        V9X_NV50_INST_PD,
        V9X_NV50_INST_RAMFC + 0x3cul,
        V9X_NV50_INST_RAMFC + 0x50ul,
        V9X_NV50_INST_RAMFC + 0x80ul,
        V9X_NV50_INST_RAMFC + 0x98ul,
        V9X_NV50_INST_DMA_VM,
        V9X_NV50_INST_OBJ_2D,
        V9X_NV50_PT + 0x800ul * 8ul,
        V9X_NV50_PT + 0x802ul * 8ul,
        V9X_NV50_RUNLIST,
        V9X_NV50_GRCTX + 0x10cul,
        V9X_NV50_GRCTX + 0x220ul
    };
    struct v9x_nv50_grctx_input grctx;
    DWORD reg_ops[sizeof(regs) / sizeof(regs[0])];
    DWORD word_ops[sizeof(words) / sizeof(words[0])];
    DWORD units;
    DWORD index;
    DWORD matches = 0ul;
    DWORD chan;
    DWORD runlist;
    DWORD ctx;
    DWORD ib_get;
    DWORD ib_put;
    char key[16];
    v9x_status status;

    grctx.chipset = (v9x_result.boot0 >> 20) & 0x1fful;
    v9x_ops_begin();
    units = v9x_read(V9X_PBUS_UNITS);
    if (!v9x_run("check-units")) {
        v9x_finish("UNITS-REFUSED", 3u);
    }
    grctx.units = v9x_value(units);
    grctx.gddr5 = 0ul;
    status = v9x_nv50_chan_build(V9X_WINDOW_VRAM, V9X_CHID, &grctx,
                                 v9x_region, V9X_NV50_REGION_WORDS,
                                 &v9x_layout);
    if (status != V9X_STATUS_OK) {
        v9x_finish("BUILD-FAILED", 4u);
    }

    v9x_ops_begin();
    for (index = 0ul; index < sizeof(regs) / sizeof(regs[0]); ++index) {
        reg_ops[index] = v9x_read(regs[index]);
    }
    for (index = 0ul; index < sizeof(words) / sizeof(words[0]); ++index) {
        word_ops[index] = v9x_op(V9X_OP_VRAM_READ, words[index], 0ul, 0ul);
    }
    if (!v9x_run("check")) {
        v9x_finish("CHECK-REFUSED", 4u);
    }
    for (index = 0ul; index < sizeof(regs) / sizeof(regs[0]); ++index) {
        lstrcpyA(key, "R");
        v9x_hex(key + 1, regs[index], 6);
        v9x_put("Check", key, reg_ops[index]);
    }
    for (index = 0ul; index < sizeof(words) / sizeof(words[0]); ++index) {
        lstrcpyA(key, "W");
        v9x_hex(key + 1, words[index], 5);
        v9x_put("Check", key, word_ops[index]);
        lstrcpyA(key, "X");
        v9x_hex(key + 1, words[index], 5);
        v9x_key("Check", key, v9x_region[words[index] / 4ul]);
        if (v9x_value(word_ops[index]) == v9x_region[words[index] / 4ul]) {
            ++matches;
        }
    }
    chan = v9x_value(reg_ops[2]);
    runlist = v9x_value(reg_ops[4]);
    ctx = v9x_value(reg_ops[19]);
    ib_get = v9x_value(reg_ops[7]);
    ib_put = v9x_value(reg_ops[8]);
    v9x_number("Check", "WordsMatching", matches);
    v9x_number("Check", "WordsSampled", sizeof(words) / sizeof(words[0]));
    v9x_key("Check", "ExpectChannel", v9x_layout.chan_entry);
    v9x_key("Check", "ExpectRunlist", v9x_layout.runlist_reg);
    v9x_key("Check", "ExpectContext",
            0x80000000ul | (v9x_layout.inst_addr >> 12));
    v9x_flush();

    if (chan != v9x_layout.chan_entry ||
        runlist != v9x_layout.runlist_reg ||
        ctx != (0x80000000ul | (v9x_layout.inst_addr >> 12)) ||
        ib_get != ib_put ||
        matches != sizeof(words) / sizeof(words[0])) {
        v9x_text("Check", "State", "CHANGED");
        v9x_finish("CHECK-STATE-CHANGED", 0u);
    }
    v9x_text("Check", "State", "HELD");
    v9x_flush();

    /* The channel as found: one fill and two copies on it. */
    v9x_ib_next = ib_get;
    if (!v9x_stage_depth("Check32", 0ul, 4ul, V9X_502D_FORMAT_X8R8G8B8,
                         V9X_FILL_COLOUR)) {
        v9x_finish("CHECK-DRAW-FAILED", 7u);
    }
    v9x_pusher_state();
    v9x_finish("CHECK-HELD-AND-DRAWS", 0u);
}

/*
 * /3d: phase 1 of docs\plans\nvidia-gt218-3d.md. The Tesla 3D object
 * (8597h) bound on its own subchannel and xf86-video-nouveau's setup sent
 * after it, as docs\specifications\gt218-tesla-3d-first-triangle.md lists
 * it, with nothing drawn. EXA's vertex program and PFP_S are written
 * first to the addresses the setup names, 3 MiB into the window, clear of
 * the fill and depth targets, and two of their words read back. Pass when
 * the push is consumed with PGRAPH idle, no PGRAPH interrupt, no pusher
 * error and no VM trap.
 */
#define V9X_3D_SCRATCH       0x00300000ul
#define V9X_3D_PUSH_WORDS    128ul

static int v9x_stage_3d(void)
{
    struct v9x_nv50_3d_scratch scratch;
    struct v9x_nv50_push push;
    DWORD first_word = v9x_push_next;
    DWORD vp_read;
    DWORD fp_read;
    DWORD vm_trap;
    int clean;

    v9x_push_next += V9X_3D_PUSH_WORDS;
    v9x_nv50_3d_scratch_at(V9X_WINDOW_VRAM + V9X_3D_SCRATCH, &scratch);

    /* The programs, and one word of each read back through BAR1. */
    v9x_ops_begin();
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_VP,
                  v9x_nv50_3d_exa_vp, V9X_NV50_3D_EXA_VP_WORDS);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_FP,
                  v9x_nv50_3d_exa_fp_s, V9X_NV50_3D_EXA_FP_S_WORDS);
    vp_read = v9x_op(V9X_OP_VRAM_READ,
                     V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_VP + 59ul * 4ul,
                     0ul, 0ul);
    fp_read = v9x_op(V9X_OP_VRAM_READ,
                     V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_FP + 5ul * 4ul,
                     0ul, 0ul);
    if (!v9x_run("3d-programs")) {
        v9x_finish("3D-PROGRAMS-REFUSED", 8u);
    }
    v9x_put("3D", "VpLastWord", vp_read);
    v9x_put("3D", "FpLastWord", fp_read);
    v9x_flush();
    if (v9x_value(vp_read) != v9x_nv50_3d_exa_vp[59] ||
        v9x_value(fp_read) != v9x_nv50_3d_exa_fp_s[5]) {
        v9x_text("3D", "Result", "PROGRAMS-DIFFER");
        return 0;
    }

    /* The bind and the setup as one push. */
    v9x_ops_begin();
    v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) +
                                  first_word,
                       V9X_3D_PUSH_WORDS);
    v9x_nv50_push_bind_3d(&push);
    v9x_nv50_push_3d_setup(&push, &scratch);
    if (push.overflow != 0ul) {
        v9x_text("3D", "Result", "PUSH-OVERFLOW");
        return 0;
    }
    v9x_number("3D", "PushWords", push.count);
    clean = v9x_kick("3D", &push, first_word);

    /* The VM trap word, which v9x_kick does not read: an address in the
     * setup outside the VM would latch here. */
    v9x_ops_begin();
    vm_trap = v9x_op(V9X_OP_MMIO_READ, V9X_NV50_VM_TRAP, 0ul, 0ul);
    if (!v9x_run("3d-vm")) {
        v9x_finish("3D-VM-REFUSED", 8u);
    }
    v9x_put("3D", "VmTrap", vm_trap);
    v9x_pusher_state();
    if ((v9x_value(vm_trap) & V9X_NV50_VM_TRAP_PENDING) != 0ul) {
        v9x_text("3D", "Result", "VM-TRAP");
        return 0;
    }
    if (!clean) {
        v9x_text("3D", "Result", "NOT-CLEAN");
        return 0;
    }
    v9x_text("3D", "Result", "PASS");
    return 1;
}

/*
 * /3d:draw: phase 2 of docs\plans\nvidia-gt218-3d.md, after /3d's setup.
 * One solid rectangle through the 3D engine at each of 32 and 16 bpp:
 * EXA's PFP_S sampling a 1x1 texture of the colour, into a pitch-linear
 * 128x72 surface the CPU first filled with a pattern. Every pixel is read
 * back: the rectangle must be the colour, everything else the pattern.
 * At 5:6:5 FFA5C3E1h is A61Ch: the card keeps each channel's top bits
 * (A5h >> 3, C3h >> 2, E1h >> 3). The first run expected A61Bh, a x31
 * scaling, and every rectangle pixel read A61Ch
 * (docs\decisions\2026-11-14-gt218-3d-first-triangle.md).
 */
#define V9X_3D_DRAW_FIRST    0x00280000ul
#define V9X_3D_DRAW_STEP     0x00020000ul
#define V9X_3D_DRAW_X        16ul
#define V9X_3D_DRAW_Y        8ul
#define V9X_3D_DRAW_W        40ul
#define V9X_3D_DRAW_H        24ul
#define V9X_3D_DRAW_ARGB     0xffa5c3e1ul

static int v9x_stage_3d_draw(const char *section, DWORD number, DWORD bytes,
                             DWORD format, DWORD expected)
{
    struct v9x_nv50_3d_scratch scratch;
    struct v9x_nv50_3d_solid solid;
    struct v9x_nv50_push push;
    DWORD offset = V9X_3D_DRAW_FIRST + number * V9X_3D_DRAW_STEP;
    DWORD pitch = V9X_FILL_WIDTH * bytes;
    DWORD words = V9X_DEPTH_PIXELS * bytes / 4ul;
    DWORD pattern = bytes == 4ul ? (V9X_FILL_PATTERN & V9X_COLOUR_MASK)
                                 : (V9X_FILL_PATTERN & 0xfffful);
    DWORD first_word = v9x_push_next;
    DWORD block = 0ul;
    DWORD index;
    DWORD pixel;
    DWORD x;
    DWORD y;
    DWORD inside_ok = 0ul;
    DWORD inside_bad = 0ul;
    DWORD outside_ok = 0ul;
    DWORD outside_bad = 0ul;
    DWORD first_bad = 0xfffffffful;
    DWORD first_bad_pixel = 0ul;
    int inside;

    v9x_push_next += V9X_PUSH_STAGE_MAX;
    v9x_nv50_3d_scratch_at(V9X_WINDOW_VRAM + V9X_3D_SCRATCH, &scratch);
    v9x_nv50_3d_solid_tables(&scratch, V9X_3D_DRAW_ARGB, &solid);

    /* The pattern, the solid tables and the constants, then the draw. */
    v9x_ops_begin();
    v9x_op(V9X_OP_VRAM_FILL, offset, words, V9X_FILL_PATTERN);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_TEXEL,
                  &solid.texel, 1ul);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_TIC, solid.tic,
                  V9X_NV50_3D_TIC_WORDS);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_TSC, solid.tsc,
                  V9X_NV50_3D_TSC_WORDS);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_VP_DATA,
                  solid.vp_constants, V9X_NV50_3D_VP_CONSTANT_WORDS);
    v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) +
                                  first_word,
                       V9X_PUSH_STAGE_MAX);
    v9x_nv50_push_3d_solid_rect(&push, V9X_WINDOW_VRAM + offset, pitch,
                                V9X_FILL_HEIGHT, format, V9X_3D_DRAW_X,
                                V9X_3D_DRAW_Y, V9X_3D_DRAW_W,
                                V9X_3D_DRAW_H);
    if (push.overflow != 0ul) {
        v9x_text(section, "Result", "PUSH-OVERFLOW");
        return 0;
    }
    if (!v9x_kick(section, &push, first_word)) {
        v9x_text(section, "Result", "NOT-CLEAN");
        return 0;
    }

    /* Every pixel back, one block of the surface per run. */
    for (block = 0ul; block < words; block += V9X_N5CH_BLOCK) {
        DWORD count = words - block;

        if (count > V9X_N5CH_BLOCK) {
            count = V9X_N5CH_BLOCK;
        }
        v9x_ops_begin();
        v9x_op(V9X_OP_VRAM_BLOCK, offset + block * 4ul, count, 0ul);
        if (!v9x_run("3d-readback")) {
            v9x_finish("3D-READBACK-REFUSED", 8u);
        }
        for (index = block * 4ul / bytes;
             index < (block + count) * 4ul / bytes; ++index) {
            pixel = v9x_depth_pixel(v9x_result.block,
                                    index - block * 4ul / bytes, bytes);
            x = index % V9X_FILL_WIDTH;
            y = index / V9X_FILL_WIDTH;
            inside = x >= V9X_3D_DRAW_X &&
                     x < V9X_3D_DRAW_X + V9X_3D_DRAW_W &&
                     y >= V9X_3D_DRAW_Y &&
                     y < V9X_3D_DRAW_Y + V9X_3D_DRAW_H;
            if (inside && pixel == expected) {
                ++inside_ok;
                continue;
            }
            if (!inside && pixel == pattern) {
                ++outside_ok;
                continue;
            }
            if (inside) {
                ++inside_bad;
            } else {
                ++outside_bad;
            }
            if (first_bad == 0xfffffffful) {
                first_bad = index;
                first_bad_pixel = pixel;
            }
        }
    }
    v9x_number(section, "InsideOk", inside_ok);
    v9x_number(section, "InsideBad", inside_bad);
    v9x_number(section, "OutsideOk", outside_ok);
    v9x_number(section, "OutsideBad", outside_bad);
    if (first_bad != 0xfffffffful) {
        v9x_number(section, "FirstBadX", first_bad % V9X_FILL_WIDTH);
        v9x_number(section, "FirstBadY", first_bad / V9X_FILL_WIDTH);
        v9x_key(section, "FirstBadPixel", first_bad_pixel);
    }
    v9x_flush();
    if (inside_bad == 0ul && outside_bad == 0ul) {
        v9x_text(section, "Result", "PASS");
        return 1;
    }
    v9x_text(section, "Result", "PIXELS-DIFFER");
    return 0;
}

/*
 * /3d:gouraud: phase 3's first programs of our own, built by the encoder.
 * A 40x24 rectangle at (16, 8) whose red ramps from 0 to 1 left to right
 * and green from 0 to 1 top to bottom, blue 0.5: pixel (i, j) of it
 * should be red (i + 0.5) / 40, green (j + 0.5) / 24. Every pixel is
 * compared with that model within 2 steps a channel at 8 bits and 1 step
 * at 5 or 6, since the exact float-to-integer rule is not measured; the
 * border must still be the pattern.
 */
static int v9x_channel_near(DWORD got, double model, DWORD steps, DWORD slack)
{
    double want = model * (double)(steps);
    double diff = (double)got - want;

    if (diff < 0.0) {
        diff = -diff;
    }
    return diff <= (double)slack + 0.5;
}

static int v9x_stage_3d_gouraud(const char *section, DWORD number,
                                DWORD bytes, DWORD format)
{
    struct v9x_nv50_3d_scratch scratch;
    struct v9x_nv50_push push;
    DWORD vp[V9X_NV50_3D_GOURAUD_VP_MAX];
    DWORD fp[V9X_NV50_3D_GOURAUD_FP_MAX];
    DWORD vp_count;
    DWORD fp_count;
    DWORD offset = V9X_3D_DRAW_FIRST + (2ul + number) * V9X_3D_DRAW_STEP;
    DWORD pitch = V9X_FILL_WIDTH * bytes;
    DWORD words = V9X_DEPTH_PIXELS * bytes / 4ul;
    DWORD pattern = bytes == 4ul ? (V9X_FILL_PATTERN & V9X_COLOUR_MASK)
                                 : (V9X_FILL_PATTERN & 0xfffful);
    DWORD first_word = v9x_push_next;
    DWORD block;
    DWORD index;
    DWORD pixel;
    DWORD x;
    DWORD y;
    DWORD inside_ok = 0ul;
    DWORD inside_bad = 0ul;
    DWORD outside_ok = 0ul;
    DWORD outside_bad = 0ul;
    DWORD first_bad = 0xfffffffful;
    DWORD first_bad_pixel = 0ul;
    DWORD corner_first = 0xfffffffful;
    DWORD corner_last = 0xfffffffful;
    int inside;
    int good;

    v9x_push_next += V9X_PUSH_STAGE_MAX;
    v9x_nv50_3d_scratch_at(V9X_WINDOW_VRAM + V9X_3D_SCRATCH, &scratch);
    vp_count = v9x_nv50_3d_gouraud_vp(vp, V9X_NV50_3D_GOURAUD_VP_MAX);
    fp_count = v9x_nv50_3d_gouraud_fp(fp, V9X_NV50_3D_GOURAUD_FP_MAX);
    if (vp_count == 0ul || fp_count == 0ul) {
        v9x_text(section, "Result", "ENCODE-FAILED");
        return 0;
    }

    v9x_ops_begin();
    v9x_op(V9X_OP_VRAM_FILL, offset, words, V9X_FILL_PATTERN);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_VP +
                      V9X_NV50_3D_GOURAUD_VP_START,
                  vp, vp_count);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_FP +
                      V9X_NV50_3D_GOURAUD_FP_START,
                  fp, fp_count);
    v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) +
                                  first_word,
                       V9X_PUSH_STAGE_MAX);
    v9x_nv50_push_3d_gouraud_rect(&push, V9X_WINDOW_VRAM + offset, pitch,
                                  V9X_FILL_HEIGHT, format, V9X_3D_DRAW_X,
                                  V9X_3D_DRAW_Y, V9X_3D_DRAW_W,
                                  V9X_3D_DRAW_H);
    if (push.overflow != 0ul) {
        v9x_text(section, "Result", "PUSH-OVERFLOW");
        return 0;
    }
    if (!v9x_kick(section, &push, first_word)) {
        v9x_text(section, "Result", "NOT-CLEAN");
        return 0;
    }

    for (block = 0ul; block < words; block += V9X_N5CH_BLOCK) {
        DWORD count = words - block;

        if (count > V9X_N5CH_BLOCK) {
            count = V9X_N5CH_BLOCK;
        }
        v9x_ops_begin();
        v9x_op(V9X_OP_VRAM_BLOCK, offset + block * 4ul, count, 0ul);
        if (!v9x_run("3d-readback")) {
            v9x_finish("3D-READBACK-REFUSED", 8u);
        }
        for (index = block * 4ul / bytes;
             index < (block + count) * 4ul / bytes; ++index) {
            pixel = v9x_depth_pixel(v9x_result.block,
                                    index - block * 4ul / bytes, bytes);
            x = index % V9X_FILL_WIDTH;
            y = index / V9X_FILL_WIDTH;
            inside = x >= V9X_3D_DRAW_X &&
                     x < V9X_3D_DRAW_X + V9X_3D_DRAW_W &&
                     y >= V9X_3D_DRAW_Y &&
                     y < V9X_3D_DRAW_Y + V9X_3D_DRAW_H;
            if (!inside) {
                if (pixel == pattern) {
                    ++outside_ok;
                    continue;
                }
                ++outside_bad;
            } else {
                double red = ((double)(x - V9X_3D_DRAW_X) + 0.5) /
                             (double)V9X_3D_DRAW_W;
                double green = ((double)(y - V9X_3D_DRAW_Y) + 0.5) /
                               (double)V9X_3D_DRAW_H;

                if (x == V9X_3D_DRAW_X && y == V9X_3D_DRAW_Y) {
                    corner_first = pixel;
                }
                if (x == V9X_3D_DRAW_X + V9X_3D_DRAW_W - 1ul &&
                    y == V9X_3D_DRAW_Y + V9X_3D_DRAW_H - 1ul) {
                    corner_last = pixel;
                }
                if (bytes == 4ul) {
                    good = v9x_channel_near((pixel >> 16) & 0xfful, red,
                                            255ul, 2ul) &&
                           v9x_channel_near((pixel >> 8) & 0xfful, green,
                                            255ul, 2ul) &&
                           v9x_channel_near(pixel & 0xfful, 0.5, 255ul,
                                            2ul);
                } else {
                    good = v9x_channel_near((pixel >> 11) & 0x1ful, red,
                                            31ul, 1ul) &&
                           v9x_channel_near((pixel >> 5) & 0x3ful, green,
                                            63ul, 1ul) &&
                           v9x_channel_near(pixel & 0x1ful, 0.5, 31ul,
                                            1ul);
                }
                if (good) {
                    ++inside_ok;
                    continue;
                }
                ++inside_bad;
            }
            if (first_bad == 0xfffffffful) {
                first_bad = index;
                first_bad_pixel = pixel;
            }
        }
    }
    v9x_number(section, "InsideOk", inside_ok);
    v9x_number(section, "InsideBad", inside_bad);
    v9x_number(section, "OutsideOk", outside_ok);
    v9x_number(section, "OutsideBad", outside_bad);
    v9x_key(section, "TopLeftPixel", corner_first);
    v9x_key(section, "BottomRightPixel", corner_last);
    if (first_bad != 0xfffffffful) {
        v9x_number(section, "FirstBadX", first_bad % V9X_FILL_WIDTH);
        v9x_number(section, "FirstBadY", first_bad / V9X_FILL_WIDTH);
        v9x_key(section, "FirstBadPixel", first_bad_pixel);
    }
    v9x_flush();
    if (inside_bad == 0ul && outside_bad == 0ul) {
        v9x_text(section, "Result", "PASS");
        return 1;
    }
    v9x_text(section, "Result", "PIXELS-DIFFER");
    return 0;
}

/*
 * /3d:texture: a 4x4 pitch-linear A8R8G8B8 texture of 16 distinct
 * colours (rows 64 bytes apart) stretched over the 40x24 rectangle with
 * nearest sampling, so texel (i / 10, j / 6) covers pixel (i, j) and no
 * pixel centre lies on a texel edge. At 32 bpp every pixel has one right
 * value. At 16 bpp the expectation is each channel's top bits, which the
 * first triangle's one colour suggested; the stage also counts pixels
 * fitting a x31-and-round conversion, so the 16 colours settle it.
 */
#define V9X_3D_TEX_SIZE      4ul
#define V9X_3D_TEX_PITCH     64ul
#define V9X_3D_TEX_FIRST     0x00340000ul
#define V9X_3D_TEX_PUSH_WORDS 128ul

static DWORD v9x_3d_texel(DWORD column, DWORD row)
{
    DWORD k = row * V9X_3D_TEX_SIZE + column;
    DWORD red = k * 16ul + 7ul;
    DWORD green = 255ul - k * 16ul - 3ul;
    DWORD blue = (k * 37ul + 11ul) & 0xfful;

    return 0xff000000ul | (red << 16) | (green << 8) | blue;
}

static DWORD v9x_3d_565_top(DWORD argb)
{
    return (((argb >> 19) & 0x1ful) << 11) | (((argb >> 10) & 0x3ful) << 5) |
           ((argb >> 3) & 0x1ful);
}

static DWORD v9x_3d_565_round(DWORD argb)
{
    DWORD red = (((argb >> 16) & 0xfful) * 31ul + 127ul) / 255ul;
    DWORD green = (((argb >> 8) & 0xfful) * 63ul + 127ul) / 255ul;
    DWORD blue = ((argb & 0xfful) * 31ul + 127ul) / 255ul;

    return (red << 11) | (green << 5) | blue;
}

static int v9x_stage_3d_texture(const char *section, DWORD number,
                                DWORD bytes, DWORD format)
{
    static DWORD texture[V9X_3D_TEX_SIZE * V9X_3D_TEX_PITCH / 4ul];
    struct v9x_nv50_3d_scratch scratch;
    struct v9x_nv50_3d_solid tables;
    struct v9x_nv50_push push;
    /* Above the scratch at 3 MiB; the draw and Gouraud surfaces fill
     * 2.5 to 3 MiB. */
    DWORD offset = V9X_3D_TEX_FIRST + number * V9X_3D_DRAW_STEP;
    DWORD pitch = V9X_FILL_WIDTH * bytes;
    DWORD words = V9X_DEPTH_PIXELS * bytes / 4ul;
    DWORD pattern = bytes == 4ul ? (V9X_FILL_PATTERN & V9X_COLOUR_MASK)
                                 : (V9X_FILL_PATTERN & 0xfffful);
    DWORD first_word = v9x_push_next;
    DWORD block;
    DWORD index;
    DWORD pixel;
    DWORD texel;
    DWORD expected;
    DWORD x;
    DWORD y;
    DWORD inside_ok = 0ul;
    DWORD inside_bad = 0ul;
    DWORD outside_ok = 0ul;
    DWORD outside_bad = 0ul;
    DWORD fits_round = 0ul;
    DWORD first_bad = 0xfffffffful;
    DWORD first_bad_pixel = 0ul;
    DWORD first_bad_expected = 0ul;
    int inside;

    v9x_push_next += V9X_3D_TEX_PUSH_WORDS;
    for (index = 0ul; index < sizeof(texture) / sizeof(texture[0]);
         ++index) {
        texture[index] = 0ul;
    }
    for (y = 0ul; y < V9X_3D_TEX_SIZE; ++y) {
        for (x = 0ul; x < V9X_3D_TEX_SIZE; ++x) {
            texture[y * (V9X_3D_TEX_PITCH / 4ul) + x] = v9x_3d_texel(x, y);
        }
    }
    v9x_nv50_3d_scratch_at(V9X_WINDOW_VRAM + V9X_3D_SCRATCH, &scratch);
    v9x_nv50_3d_texture_tables(scratch.texture, V9X_3D_TEX_PITCH,
                               V9X_3D_TEX_SIZE, V9X_3D_TEX_SIZE, &tables);

    v9x_ops_begin();
    v9x_op(V9X_OP_VRAM_FILL, offset, words, V9X_FILL_PATTERN);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_TEXTURE, texture,
                  sizeof(texture) / sizeof(texture[0]));
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_TIC, tables.tic,
                  V9X_NV50_3D_TIC_WORDS);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_TSC, tables.tsc,
                  V9X_NV50_3D_TSC_WORDS);
    /* The constants go through the engine, not the CPU: the first run of
     * this stage wrote them to memory and every pixel came out texel
     * (0, 0), as if 1/width were 0. */
    v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) +
                                  first_word,
                       V9X_3D_TEX_PUSH_WORDS);
    v9x_nv50_push_3d_vp_constants(&push, tables.vp_constants,
                                  V9X_NV50_3D_VP_CONSTANT_WORDS);
    v9x_nv50_push_3d_textured_rect(&push, V9X_WINDOW_VRAM + offset, pitch,
                                   V9X_FILL_HEIGHT, format, V9X_3D_DRAW_X,
                                   V9X_3D_DRAW_Y, V9X_3D_DRAW_W,
                                   V9X_3D_DRAW_H, V9X_3D_TEX_SIZE,
                                   V9X_3D_TEX_SIZE);
    if (push.overflow != 0ul) {
        v9x_text(section, "Result", "PUSH-OVERFLOW");
        return 0;
    }
    if (!v9x_kick(section, &push, first_word)) {
        v9x_text(section, "Result", "NOT-CLEAN");
        return 0;
    }

    for (block = 0ul; block < words; block += V9X_N5CH_BLOCK) {
        DWORD count = words - block;

        if (count > V9X_N5CH_BLOCK) {
            count = V9X_N5CH_BLOCK;
        }
        v9x_ops_begin();
        v9x_op(V9X_OP_VRAM_BLOCK, offset + block * 4ul, count, 0ul);
        if (!v9x_run("3d-readback")) {
            v9x_finish("3D-READBACK-REFUSED", 8u);
        }
        for (index = block * 4ul / bytes;
             index < (block + count) * 4ul / bytes; ++index) {
            pixel = v9x_depth_pixel(v9x_result.block,
                                    index - block * 4ul / bytes, bytes);
            x = index % V9X_FILL_WIDTH;
            y = index / V9X_FILL_WIDTH;
            inside = x >= V9X_3D_DRAW_X &&
                     x < V9X_3D_DRAW_X + V9X_3D_DRAW_W &&
                     y >= V9X_3D_DRAW_Y &&
                     y < V9X_3D_DRAW_Y + V9X_3D_DRAW_H;
            if (!inside) {
                expected = pattern;
            } else {
                texel = v9x_3d_texel((x - V9X_3D_DRAW_X) / 10ul,
                                     (y - V9X_3D_DRAW_Y) / 6ul);
                expected = bytes == 4ul ? (texel & V9X_COLOUR_MASK)
                                        : v9x_3d_565_top(texel);
                if (bytes == 2ul && pixel == v9x_3d_565_round(texel)) {
                    ++fits_round;
                }
            }
            if (pixel == expected) {
                if (inside) {
                    ++inside_ok;
                } else {
                    ++outside_ok;
                }
                continue;
            }
            if (inside) {
                ++inside_bad;
            } else {
                ++outside_bad;
            }
            if (first_bad == 0xfffffffful) {
                first_bad = index;
                first_bad_pixel = pixel;
                first_bad_expected = expected;
            }
        }
    }
    v9x_number(section, "InsideOk", inside_ok);
    v9x_number(section, "InsideBad", inside_bad);
    v9x_number(section, "OutsideOk", outside_ok);
    v9x_number(section, "OutsideBad", outside_bad);
    if (bytes == 2ul) {
        v9x_number(section, "FitsTopBits", inside_ok);
        v9x_number(section, "FitsX31Round", fits_round);
    }
    if (first_bad != 0xfffffffful) {
        v9x_number(section, "FirstBadX", first_bad % V9X_FILL_WIDTH);
        v9x_number(section, "FirstBadY", first_bad / V9X_FILL_WIDTH);
        v9x_key(section, "FirstBadPixel", first_bad_pixel);
        v9x_key(section, "FirstBadExpected", first_bad_expected);
    }
    v9x_flush();
    if (inside_bad == 0ul && outside_bad == 0ul) {
        v9x_text(section, "Result", "PASS");
        return 1;
    }
    v9x_text(section, "Result", "PIXELS-DIFFER");
    return 0;
}

/*
 * /3d:d3d: step 5a's builders (nv50_d3d.c) on the card before the HAL
 * depends on them, after /3d's setup. Into a 128x72 X8R8G8B8 linear
 * surface prefilled with the pattern, four rectangles of two triangles
 * each, one kick apiece, in Direct3D's vertex terms (half-pixel centre
 * added by the builder):
 *
 *  A. (16, 8)-(56, 32), the untextured variant, solid red, rhw 0.5 at
 *     every vertex: w must not move the rectangle (the first run showed
 *     the engine does not divide by it) or change the colour.
 *  B. (64, 8)-(104, 32), REPLACE, /3d:texture's 4x4 texture, nearest,
 *     rhw 1, coordinates offset half a pixel so no sample lies on a
 *     texel edge: each pixel its texel exactly.
 *  C. (16, 40)-(56, 64), MODULATE with specular and fog (variant 11),
 *     the same texture, rhw 1 on the left and 0.5 on the right, specular
 *     alpha 0, so all fog: every pixel the fog colour.
 *  D. (64, 40)-(104, 64), REPLACE with rhw 1 on the left and 0.25 on
 *     the right: each pixel the texel the perspective-correct u names.
 *     Pixels whose u lies within 5% of a texel edge are counted apart;
 *     the rest are also counted against the affine u, which differs
 *     across most of the rectangle, so the run says which the engine
 *     interpolates.
 *
 * Colours within one step a channel where the value is interpolated;
 * texels exactly; outside the rectangles the pattern exactly.
 */
#define V9X_D3D_SURFACE      0x00310000ul
#define V9X_D3D_TEXTURE      0x00320000ul
#define V9X_D3D_PUSH_WORDS   320ul
#define V9X_D3D_FOG          0x002050a0ul
#define V9X_D3D_DRAWS        4ul
#define V9X_D3D_RHW_FAR      0.25f
#define V9X_D3D_EDGE         0.05

struct v9x_d3d_rect {
    float left;
    float top;
    float right;
    float bottom;
};

static const struct v9x_d3d_rect v9x_d3d_rects[V9X_D3D_DRAWS] = {
    { 16.0f, 8.0f, 56.0f, 32.0f },
    { 64.0f, 8.0f, 104.0f, 32.0f },
    { 16.0f, 40.0f, 56.0f, 64.0f },
    { 64.0f, 40.0f, 104.0f, 64.0f }
};

/* The coordinates at the rectangles' left and top edges: half a pixel of
 * a 40x24 rectangle in. */
#define V9X_D3D_U0           (0.5 / 40.0)
#define V9X_D3D_V0           (0.5 / 24.0)

/* Two triangles over r: corners 0 1 2, then 2 1 3 (top-left, top-right,
 * bottom-left, bottom-right). */
static void v9x_d3d_quad(struct v9x_nv50_push *push,
                         const struct v9x_d3d_rect *r, DWORD diffuse,
                         DWORD specular, float rhw_left, float rhw_right,
                         DWORD with_specular, DWORD with_texture)
{
    static const DWORD order[6] = { 0ul, 1ul, 2ul, 2ul, 1ul, 3ul };
    float u0 = (float)V9X_D3D_U0;
    float v0 = (float)V9X_D3D_V0;
    DWORD index;

    v9x_nv50_push_d3d_begin(push);
    for (index = 0ul; index < 6ul; ++index) {
        DWORD corner = order[index];
        int right = (corner & 1ul) != 0ul;
        int bottom = (corner & 2ul) != 0ul;

        v9x_nv50_push_d3d_vertex(push, right ? r->right : r->left,
                                 bottom ? r->bottom : r->top, 0.5f,
                                 right ? rhw_right : rhw_left, diffuse,
                                 specular, right ? 1.0f + u0 : u0,
                                 bottom ? 1.0f + v0 : v0, 0.5f,
                                 with_specular, with_texture);
    }
    v9x_nv50_push_d3d_end(push);
}

static void v9x_d3d_base(struct v9x_nv50_d3d_state *state)
{
    unsigned char *bytes = (unsigned char *)state;
    DWORD index;

    for (index = 0ul; index < sizeof(*state); ++index) {
        bytes[index] = 0u;
    }
    state->target.address = V9X_WINDOW_VRAM + V9X_D3D_SURFACE;
    state->target.pitch = V9X_FILL_WIDTH * 4ul;
    state->target.width = V9X_FILL_WIDTH;
    state->target.height = V9X_FILL_HEIGHT;
    state->target_format = V9X_NV50_3D_RT_BGRX8;
    state->scissor_right = V9X_FILL_WIDTH;
    state->scissor_bottom = V9X_FILL_HEIGHT;
    state->color_mask = V9X_NV50_D3D_MASK_RGB;
}

static int v9x_d3d_channels_near(DWORD got, DWORD want)
{
    unsigned shift;

    for (shift = 0u; shift < 24u; shift += 8u) {
        long a = (long)((got >> shift) & 0xfful);
        long b = (long)((want >> shift) & 0xfful);

        if (a - b > 1l || b - a > 1l) {
            return 0;
        }
    }
    return 1;
}

/* The texel column u names, clamped as the sampler clamps; *edge set when
 * u is within V9X_D3D_EDGE of a texel boundary. */
static DWORD v9x_d3d_column(double u, int *edge)
{
    double scaled = u * (double)V9X_3D_TEX_SIZE;
    double fraction;
    DWORD column = 0ul;

    /* By comparison, not a cast: the publisher links without the
     * float-to-integer helper. u is never negative here. */
    while (column < V9X_3D_TEX_SIZE + 1ul &&
           (double)(long)(column + 1ul) <= scaled) {
        ++column;
    }
    fraction = scaled - (double)(long)column;
    *edge = fraction < V9X_D3D_EDGE || fraction > 1.0 - V9X_D3D_EDGE;
    if (column >= V9X_3D_TEX_SIZE) {
        return V9X_3D_TEX_SIZE - 1ul;
    }
    return column;
}

/* The MP trap words after a draw that trapped with 400108h bit 7: TP0's
 * status, then each MP's 10h, status, 20h, pc and opcode (nouveau
 * nv50_gr_mp_trap). Read, not cleared: the run's bring-up starts over. */
static void v9x_mp_trap(const char *section)
{
    static const DWORD offsets[6] = { 0x10ul, 0x14ul, 0x20ul, 0x24ul, 0x70ul,
                                      0x74ul };
    static const char *const names[6] = { "M10", "Status", "M20", "Pc",
                                          "OpLow", "OpHigh" };
    DWORD reads[2][6];
    DWORD tp_status;
    DWORD mp;
    DWORD word;
    char key[32];

    v9x_ops_begin();
    tp_status = v9x_read(0x40831cul);
    for (mp = 0ul; mp < 2ul; ++mp) {
        for (word = 0ul; word < 6ul; ++word) {
            reads[mp][word] = v9x_read(0x408100ul + (mp << 7) + offsets[word]);
        }
    }
    if (!v9x_run("mp-trap")) {
        return;
    }
    v9x_put(section, "MpTp0Status", tp_status);
    for (mp = 0ul; mp < 2ul; ++mp) {
        for (word = 0ul; word < 6ul; ++word) {
            lstrcpyA(key, mp == 0ul ? "Mp0" : "Mp1");
            lstrcatA(key, names[word]);
            v9x_put(section, key, reads[mp][word]);
        }
    }
    v9x_flush();
}

/* Draw `draw`'s state and quad into push. */
static void v9x_d3d_build(struct v9x_nv50_push *push, DWORD draw,
                          const DWORD *fp_registers)
{
    struct v9x_nv50_d3d_state state;
    DWORD variant;

    v9x_d3d_base(&state);
    if (draw == 0ul) {
        variant = v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_NONE, 0ul, 0ul);
    } else if (draw == 2ul) {
        variant = v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_MODULATE, 1ul, 1ul);
        state.textured = 1ul;
        state.fog_color = V9X_D3D_FOG;
    } else {
        variant = v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_REPLACE, 0ul, 0ul);
        state.textured = 1ul;
    }
    state.fp_variant = variant;
    state.fp_registers = fp_registers[variant];
    v9x_nv50_push_d3d_state(push, &state);
    if (draw == 0ul) {
        v9x_d3d_quad(push, &v9x_d3d_rects[0], 0xffff0000ul, 0ul, 0.5f, 0.5f,
                     0ul, 0ul);
    } else if (draw == 1ul) {
        v9x_d3d_quad(push, &v9x_d3d_rects[1], 0xfffffffful, 0ul, 1.0f, 1.0f,
                     0ul, 1ul);
    } else if (draw == 2ul) {
        v9x_d3d_quad(push, &v9x_d3d_rects[2], 0xff808080ul, 0ul, 1.0f, 0.5f,
                     1ul, 1ul);
    } else {
        v9x_d3d_quad(push, &v9x_d3d_rects[3], 0xfffffffful, 0ul, 1.0f,
                     V9X_D3D_RHW_FAR, 0ul, 1ul);
    }
}

static int v9x_stage_3d_d3d(void)
{
    static DWORD texture[V9X_3D_TEX_SIZE * V9X_3D_TEX_PITCH / 4ul];
    static const char letters[V9X_D3D_DRAWS + 1ul] = {
        'A', 'B', 'C', 'D', 'P'
    };
    DWORD vp[V9X_NV50_D3D_VP_WORDS_MAX];
    DWORD fp[V9X_NV50_D3D_FP_WORDS_MAX];
    DWORD fp_registers[V9X_NV50_D3D_FP_VARIANTS];
    DWORD tic[V9X_NV50_3D_TIC_WORDS];
    DWORD tsc[V9X_NV50_3D_TSC_WORDS];
    struct v9x_nv50_push push;
    const char *section = "D3D";
    DWORD words = V9X_DEPTH_PIXELS;
    DWORD pattern = V9X_FILL_PATTERN & V9X_COLOUR_MASK;
    DWORD first_word = v9x_push_next;
    DWORD vp_count;
    DWORD count;
    DWORD variant;
    DWORD draw;
    DWORD block;
    DWORD index;
    DWORD pixel;
    DWORD expected;
    DWORD x;
    DWORD y;
    DWORD ok[V9X_D3D_DRAWS + 1ul];
    DWORD bad[V9X_D3D_DRAWS + 1ul];
    DWORD d_edge = 0ul;
    DWORD d_affine = 0ul;
    DWORD first_bad = 0xfffffffful;
    DWORD first_bad_pixel = 0ul;
    DWORD first_bad_expected = 0ul;
    DWORD a_centre = 0ul;
    DWORD b_centre = 0ul;
    char key[16];

    v9x_push_next += V9X_D3D_PUSH_WORDS;
    for (index = 0ul; index <= V9X_D3D_DRAWS; ++index) {
        ok[index] = 0ul;
        bad[index] = 0ul;
    }
    for (index = 0ul; index < sizeof(texture) / sizeof(texture[0]);
         ++index) {
        texture[index] = 0ul;
    }
    for (y = 0ul; y < V9X_3D_TEX_SIZE; ++y) {
        for (x = 0ul; x < V9X_3D_TEX_SIZE; ++x) {
            texture[y * (V9X_3D_TEX_PITCH / 4ul) + x] = v9x_3d_texel(x, y);
        }
    }
    vp_count = v9x_nv50_d3d_vp(vp, V9X_NV50_D3D_VP_WORDS_MAX);
    if (vp_count == 0ul ||
        !v9x_nv50_d3d_texture(V9X_NV50_D3D_TEX_B8G8R8A8,
                              V9X_WINDOW_VRAM + V9X_D3D_TEXTURE,
                              V9X_3D_TEX_PITCH, V9X_3D_TEX_SIZE,
                              V9X_3D_TEX_SIZE, V9X_NV50_D3D_WRAP_CLAMP, 0ul,
                              0ul, tic, tsc)) {
        v9x_text(section, "Result", "ENCODE-FAILED");
        return 0;
    }

    /* The pattern, the programs, the texture and its tables. */
    v9x_ops_begin();
    v9x_op(V9X_OP_VRAM_FILL, V9X_D3D_SURFACE, words, V9X_FILL_PATTERN);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_VP +
                      V9X_NV50_D3D_VP_START,
                  vp, vp_count);
    for (variant = 0ul; variant < V9X_NV50_D3D_FP_SCRATCH_VARIANTS; ++variant) {
        count = v9x_nv50_d3d_fp(variant, fp, V9X_NV50_D3D_FP_WORDS_MAX,
                                &fp_registers[variant]);
        if (count == 0ul) {
            v9x_text(section, "Result", "ENCODE-FAILED");
            return 0;
        }
        v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_FP +
                          v9x_nv50_d3d_fp_start(variant),
                      fp, count);
    }
    v9x_vram_data(V9X_D3D_TEXTURE, texture,
                  sizeof(texture) / sizeof(texture[0]));
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_TIC, tic,
                  V9X_NV50_3D_TIC_WORDS);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_TSC, tsc,
                  V9X_NV50_3D_TSC_WORDS);
    if (!v9x_run("d3d-data")) {
        v9x_finish("D3D-DATA-REFUSED", 8u);
    }

    for (draw = 0ul; draw < V9X_D3D_DRAWS; ++draw) {
        v9x_ops_begin();
        v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) +
                                      first_word,
                           V9X_D3D_PUSH_WORDS);
        v9x_d3d_build(&push, draw, fp_registers);
        if (push.overflow != 0ul) {
            v9x_text(section, "Result", "PUSH-OVERFLOW");
            return 0;
        }
        if (draw == 0ul) {
            v9x_number(section, "PushWordsA", push.count);
        }
        if (!v9x_kick(section, &push, first_word)) {
            v9x_number(section, "FailedDraw", draw);
            v9x_mp_trap(section);
            v9x_text(section, "Result", "NOT-CLEAN");
            return 0;
        }
    }

    for (block = 0ul; block < words; block += V9X_N5CH_BLOCK) {
        DWORD run = words - block;

        if (run > V9X_N5CH_BLOCK) {
            run = V9X_N5CH_BLOCK;
        }
        v9x_ops_begin();
        v9x_op(V9X_OP_VRAM_BLOCK, V9X_D3D_SURFACE + block * 4ul, run, 0ul);
        if (!v9x_run("d3d-readback")) {
            v9x_finish("D3D-READBACK-REFUSED", 8u);
        }
        for (index = block; index < block + run; ++index) {
            DWORD which = V9X_D3D_DRAWS;
            int matches;

            pixel = v9x_result.block[index - block] & V9X_COLOUR_MASK;
            x = index % V9X_FILL_WIDTH;
            y = index / V9X_FILL_WIDTH;
            for (draw = 0ul; draw < V9X_D3D_DRAWS; ++draw) {
                if ((float)(long)x >= v9x_d3d_rects[draw].left &&
                    (float)(long)x < v9x_d3d_rects[draw].right &&
                    (float)(long)y >= v9x_d3d_rects[draw].top &&
                    (float)(long)y < v9x_d3d_rects[draw].bottom) {
                    which = draw;
                }
            }
            if (which == 0ul) {
                expected = 0x00ff0000ul;
                if (x == 36ul && y == 20ul) {
                    a_centre = pixel;
                }
                matches = v9x_d3d_channels_near(pixel, expected);
            } else if (which == 1ul) {
                expected = v9x_3d_texel((x - 64ul) / 10ul, (y - 8ul) / 6ul) &
                           V9X_COLOUR_MASK;
                if (x == 84ul && y == 20ul) {
                    b_centre = pixel;
                }
                matches = pixel == expected;
            } else if (which == 2ul) {
                expected = V9X_D3D_FOG;
                matches = v9x_d3d_channels_near(pixel, expected);
            } else if (which == 3ul) {
                /* Perspective-correct within the triangle the pixel is
                 * in, weights rhw: 1 on the left, V9X_D3D_RHW_FAR on the
                 * right. The quad is not a projected rectangle (v/w is
                 * not planar across it), so v depends on the triangle
                 * as well as on y; the first model took v as linear in y
                 * and blamed the card for its own error. Triangle one is
                 * top-left, top-right, bottom-left (t + s < 1), two is
                 * bottom-left, top-right, bottom-right. The affine
                 * count uses the same barycentrics without weights. */
                double t = ((double)x - 64.0) / 40.0;
                double s = ((double)y - 40.0) / 24.0;
                double q1 = (double)V9X_D3D_RHW_FAR;
                double u0 = V9X_D3D_U0;
                double u1 = 1.0 + V9X_D3D_U0;
                double v0 = V9X_D3D_V0;
                double v1 = 1.0 + V9X_D3D_V0;
                double b_left_top;
                double b_right_top;
                double b_left_bottom;
                double b_right_bottom;
                double weight;
                double u;
                double v;
                double affine_u;
                double affine_v;
                int edge_u;
                int edge_v;
                int affine_edge_u;
                int affine_edge_v;
                DWORD column;
                DWORD row;
                DWORD affine_column;
                DWORD affine_row;
                int diagonal = (t + s - 1.0) * 40.0 < 0.5 &&
                               (t + s - 1.0) * 40.0 > -0.5;

                if (t + s < 1.0) {
                    b_left_top = 1.0 - t - s;
                    b_right_top = t;
                    b_left_bottom = s;
                    b_right_bottom = 0.0;
                } else {
                    b_left_top = 0.0;
                    b_right_top = 1.0 - s;
                    b_left_bottom = 1.0 - t;
                    b_right_bottom = t + s - 1.0;
                }
                weight = b_left_top + b_left_bottom +
                         q1 * (b_right_top + b_right_bottom);
                u = (u0 * (b_left_top + b_left_bottom) +
                     q1 * u1 * (b_right_top + b_right_bottom)) / weight;
                v = (v0 * b_left_top + q1 * v0 * b_right_top +
                     v1 * b_left_bottom + q1 * v1 * b_right_bottom) / weight;
                affine_u = u0 * (b_left_top + b_left_bottom) +
                           u1 * (b_right_top + b_right_bottom);
                affine_v = v0 * (b_left_top + b_right_top) +
                           v1 * (b_left_bottom + b_right_bottom);
                column = v9x_d3d_column(u, &edge_u);
                row = v9x_d3d_column(v, &edge_v);
                affine_column = v9x_d3d_column(affine_u, &affine_edge_u);
                affine_row = v9x_d3d_column(affine_v, &affine_edge_v);

                expected = v9x_3d_texel(column, row) & V9X_COLOUR_MASK;
                if (!affine_edge_u && !affine_edge_v &&
                    pixel == (v9x_3d_texel(affine_column, affine_row) &
                              V9X_COLOUR_MASK)) {
                    ++d_affine;
                }
                if (edge_u || edge_v || diagonal) {
                    ++d_edge;
                    continue;
                }
                matches = pixel == expected;
            } else {
                expected = pattern;
                matches = pixel == expected;
            }
            if (matches) {
                ++ok[which];
                continue;
            }
            ++bad[which];
            if (first_bad == 0xfffffffful) {
                first_bad = index;
                first_bad_pixel = pixel;
                first_bad_expected = expected;
            }
        }
    }
    for (draw = 0ul; draw <= V9X_D3D_DRAWS; ++draw) {
        key[0] = letters[draw];
        lstrcpyA(key + 1, "Ok");
        v9x_number(section, key, ok[draw]);
        lstrcpyA(key + 1, "Bad");
        v9x_number(section, key, bad[draw]);
    }
    v9x_number(section, "DEdge", d_edge);
    v9x_number(section, "DFitsAffine", d_affine);
    v9x_key(section, "ACentrePixel", a_centre);
    v9x_key(section, "BCentrePixel", b_centre);
    if (first_bad != 0xfffffffful) {
        v9x_number(section, "FirstBadX", first_bad % V9X_FILL_WIDTH);
        v9x_number(section, "FirstBadY", first_bad / V9X_FILL_WIDTH);
        v9x_key(section, "FirstBadPixel", first_bad_pixel);
        v9x_key(section, "FirstBadExpected", first_bad_expected);
    }
    v9x_flush();
    for (draw = 0ul; draw <= V9X_D3D_DRAWS; ++draw) {
        if (bad[draw] != 0ul) {
            v9x_text(section, "Result", "PIXELS-DIFFER");
            return 0;
        }
    }
    v9x_text(section, "Result", "PASS");
    return 1;
}

/*
 * /3d:ps11: step 3 of docs\plans\nvidia-gt218-ps11.md. Pixel shader 1.1
 * programs translated by v9x_nv50_ps11_build (nv50_ps11.c) drawn straight
 * to the engine, after /3d's setup, and every pixel judged against the
 * reference interpreter (d3d_ps11_ref.c, linked here) given the same
 * inputs and nearest samples of the same texels: the analogue of
 * V9XHSWMM /ps11 on the HD 4600.
 *
 * Five programs: Arith (every arithmetic instruction and source
 * modifier, shifts, saturation, the [-1, 1] clamp, co-issue, cnd and a
 * def), FourTex (four textures, samplers and binding entries), Kill
 * (texkill over part of the rectangle), Bem (a dependent read through
 * stage 1's bump matrix) and Cnd (a select either way). Each is drawn
 * twice, as written and with mov r0.rgb, r0.a appended, since an X8R8G8B8
 * or R5G6B5 target keeps no alpha; at 32 and then 16 bpp. Each draw
 * fills the 128x72 surface with the pattern and draws one rectangle,
 * (16, 8)-(112, 64), as two triangles with w 1, whose six attributes
 * (v0, v1, coordinate sets 0-3) are planes over it, so each pixel's
 * inputs are known exactly at its centre.
 *
 * Textures: four 8x8 A8R8G8B8 textures, pitch 64, nearest and clamped,
 * of distinct texels; a sample within V9X_PS11_MARGIN of a texel edge,
 * and a kill coordinate within V9X_PS11_KILL_MARGIN of zero, leave the
 * pixel unjudged rather than guessed. A judged pixel must be within two
 * steps a channel at 8 bits and one at 5 or 6 (the float-to-unorm rule
 * is not measured); a killed one must still be the pattern; outside the
 * rectangle, the pattern.
 *
 * Layout in the window: the surface at 330000h, the textures from
 * 339000h 200h apart, the fragment code at 33A000h (its own
 * FP_ADDRESS, since the scratch's 4 KiB code area holds the Direct3D
 * variants), the vertex program in the scratch at 600h.
 */
#define V9X_PS11_SURFACE      0x00330000ul
#define V9X_PS11_TEXTURES     0x00339000ul
#define V9X_PS11_TEX_STEP     0x00000200ul
#define V9X_PS11_CODE         0x0033a000ul
#define V9X_PS11_CODE_WORDS   1024ul
#define V9X_PS11_VP_START     0x0600ul
#define V9X_PS11_VP_WORDS     64ul
#define V9X_PS11_PUSH_WORDS   640ul
#define V9X_PS11_SIDE         8ul
#define V9X_PS11_TEX_PITCH    64ul
#define V9X_PS11_STAGES       4ul
#define V9X_PS11_ATTRIBUTES   6ul     /* v0, v1, sets 0-3 */
#define V9X_PS11_RESULTS      28ul    /* position and the six, x y z w */
#define V9X_PS11_X0           16ul
#define V9X_PS11_Y0           8ul
#define V9X_PS11_X1           112ul
#define V9X_PS11_Y1           64ul
#define V9X_PS11_MARGIN       0.05f
#define V9X_PS11_KILL_MARGIN  0.01f
#define V9X_PS11_VARIANTS     5ul
/* The fragment program's constant slot 0, which /3d's setup binds to
 * buffer 2 (nv50_d3d.c's programs read their fog colour there). */
#define V9X_PS11_CB_SLOT      0ul

/* Methods past nv50_3d.h's (Mesa nv50_3d.xml.h). */
#define V9X_PS11_VTX_ATTR_4F(n)    (0x0500ul + 0x10ul * (n))
#define V9X_PS11_FP_CONTROL        0x19a8ul
#define V9X_PS11_FP_USES_KIL       0x00100000ul
#define V9X_PS11_CODE_CB_FLUSH     0x1440ul
#define V9X_PS11_TSC_FLUSH         0x1330ul
/* FP_INTERPOLANT_CTRL: UMASK W, 24 non-flat interpolants from map entry
 * 4 (the position's four first), 24 in all: nv50_ps11.h's v[] layout. */
#define V9X_PS11_INTERPOLANTS      0x08180418ul
/* Attributes 0-6, all four components each. */
#define V9X_PS11_ATTR_EN0          0x0ffffffful
/* BIND_TIC(2): TIC entry N for texture unit N, valid. */
#define V9X_PS11_BIND(n)           (((n) << 9) | ((n) << 1) | 1ul)

/* Shader tokens, as test_d3d_ps11.c builds them. */
#define V9X_PT_VER(minor)    (0xffff0100ul | (minor))
#define V9X_PT_END           0x0000fffful
#define V9X_PT_COISSUE       0x40000000ul
#define V9X_PT_DST(file, reg, mask, mod, shift) \
    (0x80000000ul | ((DWORD)(file) << 28) | ((DWORD)(shift) << 24) | \
     ((DWORD)(mod) << 20) | ((DWORD)(mask) << 16) | (DWORD)(reg))
#define V9X_PT_SRC(file, reg, swizzle, mod) \
    (0x80000000ul | ((DWORD)(file) << 28) | ((DWORD)(mod) << 24) | \
     ((DWORD)(swizzle) << 16) | (DWORD)(reg))
#define V9X_PT_R  0ul
#define V9X_PT_V  1ul
#define V9X_PT_C  2ul
#define V9X_PT_T  3ul
#define V9X_PT_XYZW 0xe4ul
#define V9X_PT_AAAA 0xfful
#define V9X_PT_OP_MOV  1ul
#define V9X_PT_OP_ADD  2ul
#define V9X_PT_OP_SUB  3ul
#define V9X_PT_OP_MAD  4ul
#define V9X_PT_OP_MUL  5ul
#define V9X_PT_OP_DP3  8ul
#define V9X_PT_OP_LRP  18ul
#define V9X_PT_OP_CND  80ul
#define V9X_PT_OP_DEF  81ul
#define V9X_PT_OP_TEXKILL 65ul
#define V9X_PT_OP_TEX     66ul
#define V9X_PT_OP_TEXBEM  67ul
#define V9X_PT_TO_R(n, mask) V9X_PT_DST(V9X_PT_R, n, mask, 0ul, 0ul)
#define V9X_PT_TO_T(n)       V9X_PT_DST(V9X_PT_T, n, 0xful, 0ul, 0ul)
#define V9X_PT_SRC_PLAIN(file, n) V9X_PT_SRC(file, n, V9X_PT_XYZW, 0ul)

struct v9x_ps11_probe_variant {
    const char *name;
    DWORD tokens[48];
};

static const struct v9x_ps11_probe_variant
    v9x_ps11_probe_variants[V9X_PS11_VARIANTS] = {
    /* test_nv50_ps11.c's "arithmetic". */
    { "Arith", {
        V9X_PT_VER(1ul),
        V9X_PT_OP_DEF, V9X_PT_DST(V9X_PT_C, 1ul, 0xful, 0ul, 0ul),
        0x3f400000ul, 0xbf000000ul, 0x3e800000ul, 0x3fc00000ul,
        V9X_PT_OP_MOV, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_V, 0ul),
        V9X_PT_OP_MAD, V9X_PT_DST(V9X_PT_R, 1ul, 0xful, 1ul, 1ul),
        V9X_PT_SRC(V9X_PT_C, 1ul, V9X_PT_XYZW, 4ul),
        V9X_PT_SRC(V9X_PT_V, 1ul, V9X_PT_XYZW, 6ul),
        V9X_PT_SRC(V9X_PT_R, 0ul, V9X_PT_AAAA, 1ul),
        V9X_PT_OP_DP3, V9X_PT_DST(V9X_PT_R, 0ul, 0x7ul, 0ul, 15ul),
        V9X_PT_SRC(V9X_PT_R, 1ul, V9X_PT_XYZW, 2ul),
        V9X_PT_SRC(V9X_PT_C, 2ul, V9X_PT_XYZW, 3ul),
        V9X_PT_OP_LRP | V9X_PT_COISSUE, V9X_PT_TO_R(0ul, 0x8ul),
        V9X_PT_SRC_PLAIN(V9X_PT_V, 0ul), V9X_PT_SRC_PLAIN(V9X_PT_R, 1ul),
        V9X_PT_SRC_PLAIN(V9X_PT_C, 2ul),
        V9X_PT_OP_CND, V9X_PT_TO_R(1ul, 0xful),
        V9X_PT_SRC(V9X_PT_R, 0ul, V9X_PT_AAAA, 0ul),
        V9X_PT_SRC_PLAIN(V9X_PT_R, 0ul),
        V9X_PT_SRC(V9X_PT_V, 1ul, V9X_PT_XYZW, 5ul),
        V9X_PT_OP_ADD, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_R, 1ul),
        V9X_PT_SRC(V9X_PT_C, 3ul, V9X_PT_XYZW, 1ul),
        V9X_PT_OP_SUB, V9X_PT_TO_R(0ul, 0x7ul),
        V9X_PT_SRC_PLAIN(V9X_PT_R, 0ul), V9X_PT_SRC_PLAIN(V9X_PT_C, 1ul),
        V9X_PT_OP_MUL, V9X_PT_DST(V9X_PT_R, 0ul, 0xful, 0ul, 2ul),
        V9X_PT_SRC_PLAIN(V9X_PT_R, 0ul), V9X_PT_SRC_PLAIN(V9X_PT_V, 0ul),
        V9X_PT_END } },
    /* tex t0-t3; mul r1, t0, t1; mul r0, t2, t3; lrp r0, c0, r1, r0. */
    { "FourTex", {
        V9X_PT_VER(1ul),
        V9X_PT_OP_TEX, V9X_PT_TO_T(0ul), V9X_PT_OP_TEX, V9X_PT_TO_T(1ul),
        V9X_PT_OP_TEX, V9X_PT_TO_T(2ul), V9X_PT_OP_TEX, V9X_PT_TO_T(3ul),
        V9X_PT_OP_MUL, V9X_PT_TO_R(1ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_T, 0ul), V9X_PT_SRC_PLAIN(V9X_PT_T, 1ul),
        V9X_PT_OP_MUL, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_T, 2ul), V9X_PT_SRC_PLAIN(V9X_PT_T, 3ul),
        V9X_PT_OP_LRP, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_C, 0ul), V9X_PT_SRC_PLAIN(V9X_PT_R, 1ul),
        V9X_PT_SRC_PLAIN(V9X_PT_R, 0ul),
        V9X_PT_END } },
    /* tex t0; texkill t1; mov r0, t0. */
    { "Kill", {
        V9X_PT_VER(0ul),
        V9X_PT_OP_TEX, V9X_PT_TO_T(0ul), V9X_PT_OP_TEXKILL, V9X_PT_TO_T(1ul),
        V9X_PT_OP_MOV, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_T, 0ul),
        V9X_PT_END } },
    /* tex t0; texbem t1, t0; mov r0, t1. */
    { "Bem", {
        V9X_PT_VER(1ul),
        V9X_PT_OP_TEX, V9X_PT_TO_T(0ul),
        V9X_PT_OP_TEXBEM, V9X_PT_TO_T(1ul), V9X_PT_SRC_PLAIN(V9X_PT_T, 0ul),
        V9X_PT_OP_MOV, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_T, 1ul),
        V9X_PT_END } },
    /* add r0.a, v0.a, v1.a; cnd r0, r0.a, c0, v1. */
    { "Cnd", {
        V9X_PT_VER(1ul),
        V9X_PT_OP_ADD, V9X_PT_TO_R(0ul, 0x8ul),
        V9X_PT_SRC(V9X_PT_V, 0ul, V9X_PT_AAAA, 0ul),
        V9X_PT_SRC(V9X_PT_V, 1ul, V9X_PT_AAAA, 0ul),
        V9X_PT_OP_CND, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC(V9X_PT_R, 0ul, V9X_PT_AAAA, 0ul),
        V9X_PT_SRC_PLAIN(V9X_PT_C, 0ul), V9X_PT_SRC_PLAIN(V9X_PT_V, 1ul),
        V9X_PT_END } }
};

#ifdef __WATCOMC__
/* Float to long by fistp, rounding to nearest: a cast would call
 * Watcom's __CHP, from the C runtime the probe does not link (the HD
 * 4600 probe's helper). */
static long v9x_ps11_probe_long(float value);
#pragma aux v9x_ps11_probe_long = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];
#else
static long v9x_ps11_probe_long(float value)
{
    return (long)(value < 0.0f ? value - 0.5f : value + 0.5f);
}
#endif

static DWORD v9x_ps11_probe_bits(float value)
{
    union {
        float f;
        DWORD u;
    } convert;

    convert.f = value;
    return convert.u;
}

/* Stage N's texel (x, y), A8R8G8B8: distinct across each texture. */
static DWORD v9x_ps11_probe_texel(DWORD stage, DWORD x, DWORD y)
{
    DWORD a;
    DWORD r;
    DWORD g;
    DWORD b;

    if (stage == 0ul) {
        a = 0xfful - x * 8ul;
        r = 0x58ul + x * 12ul;
        g = 0x58ul + y * 12ul;
        b = 0xe0ul - (x + y) * 4ul;
    } else if (stage == 1ul) {
        a = 0x90ul + y * 8ul;
        r = x * 32ul + 16ul;
        g = 0xfful - y * 32ul;
        b = 0x40ul + ((x + y) & 1ul) * 0x80ul;
    } else if (stage == 2ul) {
        a = 0x70ul + x * 16ul;
        r = 0x20ul + y * 28ul;
        g = 0x30ul + x * 20ul;
        b = 0xc0ul - y * 16ul;
    } else {
        a = 0xd0ul - (x + y) * 4ul;
        r = 0xf0ul - x * 16ul;
        g = 0x10ul + x * y * 3ul;
        b = y * 32ul + 8ul;
    }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/*
 * Attribute a's component c as a plane over the rectangle: f = P + Q fx
 * + R fy, fx and fy the position's fraction across and down. Colours
 * stay in [0, 1]; coordinate x and y sweep a little past [0, 1], set 1's
 * x from below zero so Kill kills part of the rectangle; z is positive;
 * w is 1.
 */
static float v9x_ps11_probe_plane(DWORD a, DWORD c, float fx, float fy)
{
    float p;
    float q;
    float r;

    if (a < 2ul) {
        p = 0.05f + 0.1f * (float)(long)c + 0.05f * (float)(long)a;
        q = 0.4f - 0.05f * (float)(long)c;
        r = 0.2f + 0.1f * (float)(long)a;
    } else if (c == 0ul) {
        p = a == 3ul ? -0.3f : -0.05f + 0.02f * (float)(long)a;
        q = 1.15f;
        r = 0.1f;
    } else if (c == 1ul) {
        p = -0.04f;
        q = 0.08f;
        r = 1.1f - 0.03f * (float)(long)a;
    } else if (c == 2ul) {
        p = 0.3f;
        q = -0.2f;
        r = 0.25f;
    } else {
        return 1.0f;
    }
    return p + q * fx + r * fy;
}

static void v9x_ps11_probe_env(DWORD variant, V9X_PS11_ENV *env)
{
    unsigned char *bytes = (unsigned char *)env;
    DWORD k;

    for (k = 0ul; k < sizeof(*env); ++k) {
        bytes[k] = 0u;
    }
    for (k = 0ul; k < 8ul; ++k) {
        env->c[k][0] = 0.1f * (float)(long)k - 0.3f;
        env->c[k][1] = 1.5f - 0.2f * (float)(long)k;
        env->c[k][2] = -1.25f + 0.25f * (float)(long)k;
        env->c[k][3] = 0.45f;
    }
    if (variant == 1ul) {
        env->c[0][0] = 0.25f;
        env->c[0][1] = 0.5f;
        env->c[0][2] = 0.75f;
        env->c[0][3] = 0.6f;
    }
    env->bump[1][0] = 0.25f;
    env->bump[1][1] = -0.1f;
    env->bump[1][2] = 0.05f;
    env->bump[1][3] = 0.3f;
}

struct v9x_ps11_probe_lookup {
    int unjudged;
};

/* A nearest texel index under CLAMP; near a texel edge, unjudged. */
static DWORD v9x_ps11_probe_index(float coord, int *unjudged)
{
    float scaled = coord * (float)(long)V9X_PS11_SIDE;
    long index;
    float fraction;

    if (scaled <= 0.0f) {
        if (scaled > -V9X_PS11_MARGIN) {
            *unjudged = 1;
        }
        return 0ul;
    }
    if (scaled >= (float)(long)V9X_PS11_SIDE) {
        if (scaled < (float)(long)V9X_PS11_SIDE + V9X_PS11_MARGIN) {
            *unjudged = 1;
        }
        return V9X_PS11_SIDE - 1ul;
    }
    index = v9x_ps11_probe_long(scaled - 0.5f);
    fraction = scaled - (float)index;
    if (fraction < V9X_PS11_MARGIN || fraction > 1.0f - V9X_PS11_MARGIN) {
        *unjudged = 1;
    }
    if (index < 0l) {
        index = 0l;
    }
    if (index >= (long)V9X_PS11_SIDE) {
        index = (long)V9X_PS11_SIDE - 1l;
    }
    return (DWORD)index;
}

static void v9x_ps11_probe_sample(void *user, v9x_u32 stage, v9x_u32 count,
                                  const float *coords, float rgba[4])
{
    struct v9x_ps11_probe_lookup *lookup =
        (struct v9x_ps11_probe_lookup *)user;
    DWORD texel;

    (void)count;
    texel = v9x_ps11_probe_texel(
        stage, v9x_ps11_probe_index(coords[0], &lookup->unjudged),
        v9x_ps11_probe_index(coords[1], &lookup->unjudged));
    rgba[0] = (float)(long)((texel >> 16) & 0xfful) / 255.0f;
    rgba[1] = (float)(long)((texel >> 8) & 0xfful) / 255.0f;
    rgba[2] = (float)(long)(texel & 0xfful) / 255.0f;
    rgba[3] = (float)(long)(texel >> 24) / 255.0f;
}

/* value on [0, 1] as an integer of steps steps, rounded. */
static long v9x_ps11_probe_unorm(float value, DWORD steps)
{
    if (!(value > 0.0f)) {
        return 0l;
    }
    if (value >= 1.0f) {
        return (long)steps;
    }
    return v9x_ps11_probe_long(value * (float)(long)steps);
}

/* The vertex program: every attribute to the result of the same number. */
static DWORD v9x_ps11_probe_vp(DWORD *words, DWORD max)
{
    struct v9x_nv50_sh sh;
    DWORD index;

    v9x_nv50_sh_init(&sh, words, max);
    for (index = 0ul; index < V9X_PS11_RESULTS; ++index) {
        v9x_nv50_sh_mov(&sh, V9X_NV50_SH_LONG, V9X_NV50_SH_O(index * 4ul),
                        V9X_NV50_SH_A(index * 4ul));
    }
    v9x_nv50_sh_exit(&sh);
    if (sh.invalid != 0ul || sh.overflow != 0ul) {
        return 0ul;
    }
    return sh.count;
}

static void v9x_ps11_probe_one(struct v9x_nv50_push *push, DWORD method,
                               DWORD value)
{
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, method, 1ul, &value);
}

/*
 * What one draw covers: count vertices (three a triangle) at xy, x then
 * y; the frame the attribute planes are fractions of (fx = (x - x0) /
 * width); and the scissor, half-open in target pixels.
 */
struct v9x_probe_geometry {
    DWORD count;
    const float *xy;
    float x0;
    float y0;
    float width;
    float height;
    DWORD scissor_left;
    DWORD scissor_top;
    DWORD scissor_right;
    DWORD scissor_bottom;
    /* Null, or each vertex's v0 r g b a (in place of the plane) and its
     * weight, the position's fourth word (in place of 1). */
    const float *colour;
    const float *rhw;
};

/* /3d:ps11's rectangle, two triangles over the whole of its frame. */
static const float v9x_ps11_rect_xy[12] = {
    16.0f, 8.0f, 112.0f, 8.0f, 16.0f, 64.0f,
    16.0f, 64.0f, 112.0f, 8.0f, 112.0f, 64.0f
};
static const struct v9x_probe_geometry v9x_ps11_rect = {
    6ul, v9x_ps11_rect_xy, 16.0f, 8.0f, 96.0f, 56.0f,
    0ul, 0ul, V9X_FILL_WIDTH, V9X_FILL_HEIGHT, 0, 0
};

/* Vertex index's attributes: the planes at (x, y), or its own colour. */
static void v9x_ps11_probe_vertex(struct v9x_nv50_push *push, float x,
                                  float y,
                                  const struct v9x_probe_geometry *geometry,
                                  DWORD index)
{
    float fx = (x - geometry->x0) / geometry->width;
    float fy = (y - geometry->y0) / geometry->height;
    DWORD data[4];
    DWORD a;
    DWORD c;

    for (a = 0ul; a < V9X_PS11_ATTRIBUTES; ++a) {
        for (c = 0ul; c < 4ul; ++c) {
            data[c] = v9x_ps11_probe_bits(
                a == 0ul && geometry->colour != 0
                    ? geometry->colour[index * 4ul + c]
                    : v9x_ps11_probe_plane(a, c, fx, fy));
        }
        v9x_nv50_push_method(push, V9X_NV50_SUBC_3D,
                             V9X_PS11_VTX_ATTR_4F(a + 1ul), 4ul, data);
    }
    /* The position last: attribute 0 emits the vertex; w 1 or the
     * vertex's own weight. */
    data[0] = v9x_ps11_probe_bits(x);
    data[1] = v9x_ps11_probe_bits(y);
    data[2] = v9x_ps11_probe_bits(0.5f);
    data[3] = v9x_ps11_probe_bits(geometry->rhw != 0 ? geometry->rhw[index]
                                                     : 1.0f);
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_PS11_VTX_ATTR_4F(0ul),
                         4ul, data);
}

/* One draw's push: state, the program's constants and textures, and the
 * geometry's triangles. */
static void v9x_ps11_probe_push(struct v9x_nv50_push *push, DWORD bytes,
                                DWORD format, const DWORD *constants,
                                const struct v9x_nv50_ps11_info *info,
                                const struct v9x_nv50_3d_scratch *scratch,
                                const struct v9x_probe_geometry *geometry)
{
    struct v9x_nv50_d3d_state state;
    unsigned char *raw = (unsigned char *)&state;
    DWORD map[V9X_PS11_RESULTS / 4ul];
    DWORD data[3];
    DWORD header_at;
    DWORD index;

    for (index = 0ul; index < sizeof(state); ++index) {
        raw[index] = 0u;
    }
    state.target.address = V9X_WINDOW_VRAM + V9X_PS11_SURFACE;
    state.target.pitch = V9X_FILL_WIDTH * bytes;
    state.target.width = V9X_FILL_WIDTH;
    state.target.height = V9X_FILL_HEIGHT;
    state.target_format = format;
    state.scissor_left = geometry->scissor_left;
    state.scissor_top = geometry->scissor_top;
    state.scissor_right = geometry->scissor_right;
    state.scissor_bottom = geometry->scissor_bottom;
    state.color_mask = V9X_NV50_D3D_MASK_RGB;
    state.fp_registers = info->registers;
    v9x_nv50_push_d3d_state(push, &state);

    /* The programs and their interface, over the Direct3D state's. */
    v9x_ps11_probe_one(push, V9X_8597_VP_START_ID, V9X_PS11_VP_START);
    data[0] = 0ul;
    data[1] = V9X_WINDOW_VRAM + V9X_PS11_CODE;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_FP_ADDRESS_HIGH,
                         2ul, data);
    v9x_ps11_probe_one(push, V9X_PS11_CODE_CB_FLUSH, 0ul);
    v9x_ps11_probe_one(push, V9X_8597_FP_START_ID, 0ul);
    data[0] = V9X_PS11_ATTR_EN0;
    data[1] = 0ul;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_VP_ATTR_EN0, 2ul,
                         data);
    v9x_ps11_probe_one(push, V9X_8597_VP_REG_ALLOC_RESULT, V9X_PS11_RESULTS);
    v9x_ps11_probe_one(push, V9X_8597_VP_RESULT_MAP_SIZE, V9X_PS11_RESULTS);
    for (index = 0ul; index < V9X_PS11_RESULTS / 4ul; ++index) {
        DWORD first = index * 4ul;

        map[index] = first | ((first + 1ul) << 8) | ((first + 2ul) << 16) |
                     ((first + 3ul) << 24);
    }
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_VP_RESULT_MAP0,
                         V9X_PS11_RESULTS / 4ul, map);
    data[0] = V9X_PS11_INTERPOLANTS;
    data[1] = info->registers;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D,
                         V9X_8597_FP_INTERPOLANT_CTRL, 2ul, data);
    v9x_ps11_probe_one(push, V9X_PS11_FP_CONTROL,
                       info->kills != 0ul ? V9X_PS11_FP_USES_KIL : 0ul);

    /* The constant block into the fragment program's buffer. */
    v9x_ps11_probe_one(push, V9X_8597_CB_ADDR,
                       (0ul << V9X_8597_CB_ADDR_ID_SHIFT) | V9X_8597_CB_FP);
    header_at = push->count;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_CB_DATA0,
                         V9X_NV50_PS11_C_FLOATS, constants);
    if (push->overflow == 0ul) {
        push->words[header_at] |= V9X_NV50_PUSH_NONINC;
    }

    /* Four texture and sampler entries (LINKED_TSC: sampler N is TSC N,
     * so the TSC limit goes from the setup's 0 to 3). */
    data[0] = 0ul;
    data[1] = scratch->tsc;
    data[2] = V9X_PS11_STAGES - 1ul;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_TSC_ADDRESS_HIGH,
                         3ul, data);
    v9x_ps11_probe_one(push, V9X_8597_TIC_FLUSH, 0ul);
    v9x_ps11_probe_one(push, V9X_PS11_TSC_FLUSH, 0ul);
    for (index = 0ul; index < V9X_PS11_STAGES; ++index) {
        v9x_ps11_probe_one(push, V9X_8597_BIND_TIC2, V9X_PS11_BIND(index));
    }

    v9x_nv50_push_d3d_begin(push);
    for (index = 0ul; index < geometry->count; ++index) {
        v9x_ps11_probe_vertex(push, geometry->xy[2ul * index],
                              geometry->xy[2ul * index + 1ul], geometry,
                              index);
    }
    v9x_nv50_push_d3d_end(push);
}

/* The stages texkill reads. */
static DWORD v9x_ps11_probe_kills(const V9X_PS11_PROGRAM *program)
{
    DWORD stages = 0ul;
    DWORD i;

    for (i = 0ul; i < program->count; ++i) {
        if (program->inst[i].op == V9X_PS11_TEXKILL) {
            stages |= 1ul << program->inst[i].dst_reg;
        }
    }
    return stages;
}

struct v9x_ps11_probe_tally {
    DWORD ok;
    DWORD bad;
    DWORD unjudged;
    DWORD killed;
    DWORD outside_bad;
    long max_diff;
    DWORD first_bad;
    DWORD first_bad_pixel;
    DWORD first_bad_expected;
};

/* Judge one read-back pixel at index against the interpreter. */
static void v9x_ps11_probe_judge(const V9X_PS11_PROGRAM *program,
                                 const V9X_PS11_ENV *env, DWORD kills,
                                 DWORD index, DWORD pixel, DWORD bytes,
                                 struct v9x_ps11_probe_tally *tally)
{
    DWORD x = index % V9X_FILL_WIDTH;
    DWORD y = index / V9X_FILL_WIDTH;
    DWORD pattern = bytes == 4ul ? (V9X_FILL_PATTERN & V9X_COLOUR_MASK)
                                 : (V9X_FILL_PATTERN & 0xfffful);
    struct v9x_ps11_probe_lookup lookup;
    V9X_PS11_PIXEL inputs;
    float out[4];
    float fx;
    float fy;
    long want[3];
    long got[3];
    long diff;
    long slack = bytes == 4ul ? 2l : 1l;
    DWORD expected = 0ul;
    DWORD a;
    DWORD c;
    int kept;
    int good = 1;

    if (x < V9X_PS11_X0 || x >= V9X_PS11_X1 || y < V9X_PS11_Y0 ||
        y >= V9X_PS11_Y1) {
        if (pixel != pattern) {
            ++tally->outside_bad;
        }
        return;
    }
    fx = ((float)(long)(x - V9X_PS11_X0) + 0.5f) /
         (float)(long)(V9X_PS11_X1 - V9X_PS11_X0);
    fy = ((float)(long)(y - V9X_PS11_Y0) + 0.5f) /
         (float)(long)(V9X_PS11_Y1 - V9X_PS11_Y0);
    lookup.unjudged = 0;
    for (a = 0ul; a < V9X_PS11_ATTRIBUTES; ++a) {
        for (c = 0ul; c < 4ul; ++c) {
            float value = v9x_ps11_probe_plane(a, c, fx, fy);

            if (a < 2ul) {
                inputs.v[a][c] = value;
            } else {
                inputs.tc[a - 2ul][c] = value;
                if ((kills & (1ul << (a - 2ul))) != 0ul && c < 3ul &&
                    value < V9X_PS11_KILL_MARGIN &&
                    value > -V9X_PS11_KILL_MARGIN) {
                    lookup.unjudged = 1;
                }
            }
        }
    }
    kept = v9x_ps11_run(program, env, &inputs, v9x_ps11_probe_sample,
                        &lookup, out);
    if (lookup.unjudged) {
        ++tally->unjudged;
        return;
    }
    if (!kept) {
        if (pixel == pattern) {
            ++tally->killed;
            return;
        }
        good = 0;
        expected = pattern;
    } else if (bytes == 4ul) {
        for (c = 0ul; c < 3ul; ++c) {
            want[c] = v9x_ps11_probe_unorm(out[c], 255ul);
        }
        got[0] = (long)((pixel >> 16) & 0xfful);
        got[1] = (long)((pixel >> 8) & 0xfful);
        got[2] = (long)(pixel & 0xfful);
        expected = ((DWORD)want[0] << 16) | ((DWORD)want[1] << 8) |
                   (DWORD)want[2];
    } else {
        want[0] = v9x_ps11_probe_unorm(out[0], 31ul);
        want[1] = v9x_ps11_probe_unorm(out[1], 63ul);
        want[2] = v9x_ps11_probe_unorm(out[2], 31ul);
        got[0] = (long)((pixel >> 11) & 0x1ful);
        got[1] = (long)((pixel >> 5) & 0x3ful);
        got[2] = (long)(pixel & 0x1ful);
        expected = ((DWORD)want[0] << 11) | ((DWORD)want[1] << 5) |
                   (DWORD)want[2];
    }
    if (kept) {
        for (c = 0ul; c < 3ul; ++c) {
            diff = got[c] - want[c];
            if (diff < 0l) {
                diff = -diff;
            }
            if (diff > tally->max_diff) {
                tally->max_diff = diff;
            }
            if (diff > slack) {
                good = 0;
            }
        }
    }
    if (good) {
        ++tally->ok;
        return;
    }
    ++tally->bad;
    if (tally->first_bad == 0xfffffffful) {
        tally->first_bad = index;
        tally->first_bad_pixel = pixel;
        tally->first_bad_expected = expected;
    }
}

/* One program, one pass, at one depth; nonzero when every judged pixel
 * agrees. */
static int v9x_ps11_probe_trapped = 0;

static int v9x_ps11_probe_draw(const char *section, DWORD variant,
                               DWORD alpha_pass, DWORD bytes, DWORD format,
                               const struct v9x_nv50_3d_scratch *scratch,
                               DWORD first_word)
{
    static DWORD code[V9X_PS11_CODE_WORDS];
    static V9X_PS11_PROGRAM program;
    const struct v9x_ps11_probe_variant *v = &v9x_ps11_probe_variants[variant];
    struct v9x_nv50_ps11_desc desc;
    struct v9x_nv50_ps11_info info;
    struct v9x_ps11_probe_tally tally;
    struct v9x_nv50_push push;
    V9X_PS11_ENV env;
    DWORD constants[V9X_NV50_PS11_C_FLOATS];
    DWORD words = V9X_DEPTH_PIXELS * bytes / 4ul;
    DWORD dwords = 0ul;
    DWORD where = 0ul;
    DWORD kills;
    DWORD block;
    DWORD index;
    char key[48];

    while (v->tokens[dwords] != V9X_PT_END) {
        ++dwords;
    }
    ++dwords;
    if (v9x_ps11_parse(v->tokens, dwords, &program, &where) != V9X_PS11_OK) {
        lstrcpyA(key, v->name);
        lstrcatA(key, "Parse");
        v9x_number(section, key, where);
        return 0;
    }
    if (alpha_pass != 0ul) {
        V9X_PS11_INST *inst = &program.inst[program.count];
        unsigned char *raw = (unsigned char *)inst;

        for (index = 0ul; index < sizeof(*inst); ++index) {
            raw[index] = 0u;
        }
        inst->op = (v9x_u8)V9X_PS11_MOV;
        inst->dst_file = (v9x_u8)V9X_PS11_FILE_TEMP;
        inst->dst_mask = 0x7u;
        inst->sources = 1u;
        inst->src[0].file = (v9x_u8)V9X_PS11_FILE_TEMP;
        inst->src[0].swizzle = (v9x_u8)V9X_PT_AAAA;
        ++program.count;
    }
    v9x_ps11_probe_env(variant, &env);
    desc.cube = 0ul;
    desc.projected = 0ul;
    desc.unbound = 0ul;
    desc.slot = V9X_PS11_CB_SLOT;
    desc.fog = 0ul;
    if (v9x_nv50_ps11_build(code, V9X_PS11_CODE_WORDS, &program, &desc,
                            &info) != V9X_STATUS_OK) {
        lstrcpyA(key, v->name);
        lstrcatA(key, "Build");
        v9x_text(section, key, "FAILED");
        return 0;
    }
    v9x_nv50_ps11_constants(&program, &env, constants);
    kills = v9x_ps11_probe_kills(&program);

    v9x_ops_begin();
    v9x_op(V9X_OP_VRAM_FILL, V9X_PS11_SURFACE, words, V9X_FILL_PATTERN);
    v9x_vram_data(V9X_PS11_CODE, code, info.words);
    if (!v9x_run("ps11-data")) {
        v9x_finish("PS11-DATA-REFUSED", 8u);
    }
    v9x_ops_begin();
    v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) +
                                  first_word,
                       V9X_PS11_PUSH_WORDS);
    v9x_ps11_probe_push(&push, bytes, format, constants, &info, scratch,
                        &v9x_ps11_rect);
    if (push.overflow != 0ul) {
        v9x_text(section, "Result", "PUSH-OVERFLOW");
        return 0;
    }
    lstrcpyA(key, v->name);
    lstrcatA(key, alpha_pass != 0ul ? "AlphaWords" : "Words");
    v9x_number(section, key, info.words);
    if (!v9x_kick(section, &push, first_word)) {
        lstrcpyA(key, v->name);
        lstrcatA(key, alpha_pass != 0ul ? "AlphaKick" : "Kick");
        v9x_text(section, key, "NOT-CLEAN");
        v9x_mp_trap(section);
        v9x_ps11_probe_trapped = 1;
        return 0;
    }

    tally.ok = 0ul;
    tally.bad = 0ul;
    tally.unjudged = 0ul;
    tally.killed = 0ul;
    tally.outside_bad = 0ul;
    tally.max_diff = 0l;
    tally.first_bad = 0xfffffffful;
    tally.first_bad_pixel = 0ul;
    tally.first_bad_expected = 0ul;
    for (block = 0ul; block < words; block += V9X_N5CH_BLOCK) {
        DWORD run = words - block;

        if (run > V9X_N5CH_BLOCK) {
            run = V9X_N5CH_BLOCK;
        }
        v9x_ops_begin();
        v9x_op(V9X_OP_VRAM_BLOCK, V9X_PS11_SURFACE + block * 4ul, run, 0ul);
        if (!v9x_run("ps11-readback")) {
            v9x_finish("PS11-READBACK-REFUSED", 8u);
        }
        for (index = block * 4ul / bytes;
             index < (block + run) * 4ul / bytes; ++index) {
            v9x_ps11_probe_judge(
                &program, &env, kills, index,
                v9x_depth_pixel(v9x_result.block,
                                index - block * 4ul / bytes, bytes),
                bytes, &tally);
        }
    }

    lstrcpyA(key, v->name);
    lstrcatA(key, alpha_pass != 0ul ? "A" : "");
    {
        char name[64];

        lstrcpyA(name, key);
        lstrcatA(name, "Ok");
        v9x_number(section, name, tally.ok);
        lstrcpyA(name, key);
        lstrcatA(name, "Bad");
        v9x_number(section, name, tally.bad);
        lstrcpyA(name, key);
        lstrcatA(name, "Unjudged");
        v9x_number(section, name, tally.unjudged);
        lstrcpyA(name, key);
        lstrcatA(name, "Killed");
        v9x_number(section, name, tally.killed);
        lstrcpyA(name, key);
        lstrcatA(name, "OutsideBad");
        v9x_number(section, name, tally.outside_bad);
        lstrcpyA(name, key);
        lstrcatA(name, "MaxDiff");
        v9x_number(section, name, (DWORD)tally.max_diff);
        if (tally.first_bad != 0xfffffffful) {
            lstrcpyA(name, key);
            lstrcatA(name, "FirstBadX");
            v9x_number(section, name, tally.first_bad % V9X_FILL_WIDTH);
            lstrcpyA(name, key);
            lstrcatA(name, "FirstBadY");
            v9x_number(section, name, tally.first_bad / V9X_FILL_WIDTH);
            lstrcpyA(name, key);
            lstrcatA(name, "FirstBadPixel");
            v9x_key(section, name, tally.first_bad_pixel);
            lstrcpyA(name, key);
            lstrcatA(name, "FirstBadExpected");
            v9x_key(section, name, tally.first_bad_expected);
        }
    }
    v9x_flush();
    return tally.bad == 0ul && tally.outside_bad == 0ul && tally.ok != 0ul;
}

static int v9x_stage_3d_ps11(const char *section, DWORD bytes, DWORD format)
{
    static DWORD texture[V9X_PS11_SIDE * V9X_PS11_TEX_PITCH / 4ul];
    struct v9x_nv50_3d_scratch scratch;
    DWORD vp[V9X_PS11_VP_WORDS];
    DWORD tic[V9X_NV50_3D_TIC_WORDS];
    DWORD tsc[V9X_NV50_3D_TSC_WORDS];
    DWORD vp_count;
    DWORD stage;
    DWORD variant;
    DWORD x;
    DWORD y;
    DWORD first_word = v9x_push_next;
    int all = 1;

    v9x_push_next += V9X_PS11_PUSH_WORDS;
    v9x_nv50_3d_scratch_at(V9X_WINDOW_VRAM + V9X_3D_SCRATCH, &scratch);
    vp_count = v9x_ps11_probe_vp(vp, V9X_PS11_VP_WORDS);
    if (vp_count == 0ul) {
        v9x_text(section, "Result", "ENCODE-FAILED");
        return 0;
    }

    /* The vertex program, and the four textures and their tables. */
    v9x_ops_begin();
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_VP +
                      V9X_PS11_VP_START,
                  vp, vp_count);
    for (stage = 0ul; stage < V9X_PS11_STAGES; ++stage) {
        for (y = 0ul; y < V9X_PS11_SIDE; ++y) {
            for (x = 0ul; x < V9X_PS11_TEX_PITCH / 4ul; ++x) {
                texture[y * (V9X_PS11_TEX_PITCH / 4ul) + x] =
                    x < V9X_PS11_SIDE ? v9x_ps11_probe_texel(stage, x, y)
                                      : 0ul;
            }
        }
        if (!v9x_nv50_d3d_texture(V9X_NV50_D3D_TEX_B8G8R8A8,
                                  V9X_WINDOW_VRAM + V9X_PS11_TEXTURES +
                                      stage * V9X_PS11_TEX_STEP,
                                  V9X_PS11_TEX_PITCH, V9X_PS11_SIDE,
                                  V9X_PS11_SIDE, V9X_NV50_D3D_WRAP_CLAMP,
                                  0ul, 0ul, tic, tsc)) {
            v9x_text(section, "Result", "ENCODE-FAILED");
            return 0;
        }
        v9x_vram_data(V9X_PS11_TEXTURES + stage * V9X_PS11_TEX_STEP, texture,
                      sizeof(texture) / sizeof(texture[0]));
        v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_TIC +
                          stage * V9X_NV50_3D_TIC_WORDS * 4ul,
                      tic, V9X_NV50_3D_TIC_WORDS);
        v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_TSC +
                          stage * V9X_NV50_3D_TSC_WORDS * 4ul,
                      tsc, V9X_NV50_3D_TSC_WORDS);
    }
    if (!v9x_run("ps11-tables")) {
        v9x_finish("PS11-TABLES-REFUSED", 8u);
    }

    /* Every program both passes; a draw that does not run clean ends the
     * stage, since a trap stays latched for what follows. */
    for (variant = 0ul; variant < V9X_PS11_VARIANTS; ++variant) {
        DWORD pass;

        for (pass = 0ul; pass < 2ul; ++pass) {
            int ok = v9x_ps11_probe_draw(section, variant, pass, bytes,
                                         format, &scratch, first_word);

            if (!ok) {
                all = 0;
                if (v9x_ps11_probe_trapped) {
                    v9x_text(section, "Result", "NOT-CLEAN");
                    return 0;
                }
            }
        }
    }
    v9x_text(section, "Result", all ? "PASS" : "PIXELS-DIFFER");
    return all;
}

/*
 * /3d:guard: step 5 of docs\plans\nvidia-gt218-ps11.md, the guard band a
 * clip_in_core of 0 needs, measured as Gen3's was (docs\decisions\
 * 2026-11-01-gen3-guard-band-step1.md). After /3d:ps11's plumbing: its
 * vertex program, and the translated `mov r0, v0` with v0 a plane over
 * the whole target, so a pixel's colour says where the engine thinks it
 * is. For each distance D, two triangles, one draw each, into the
 * pattern-filled 128x72 X8R8G8B8 surface with the scissor 8 pixels in
 * from every edge:
 *
 *   Cover   (-D, -D), (2D + 300, -D), (-D, 2D + 300): every vertex off
 *           the target, the scissor wholly inside;
 *   Sliver  (-D, 36), (100, 12), (100, 60): one vertex D off the left
 *           edge, the long edges crossing the target.
 *
 * Judged: nothing outside the scissor may change (a leak); inside it, a
 * pixel centre more than 0.1 pixel inside the triangle must be the plane's
 * colour within two steps, one more than 0.1 outside the pattern, and the
 * rest are not judged. Distances run up to 32,768 (coordinates to
 * 65,836); every one is drawn even after one fails, unless a kick traps.
 */
#define V9X_GUARD_DISTANCES   8ul
#define V9X_GUARD_INSET       8ul
#define V9X_GUARD_MARGIN      0.1

static const DWORD v9x_guard_distances[V9X_GUARD_DISTANCES] = {
    0ul, 512ul, 1024ul, 2048ul, 4096ul, 8192ul, 16384ul, 32768ul
};

/* The perspective cases: red, green, blue at the three vertices, and
 * weights (rhw) as a near vertex and far ones give: PCover's two far
 * corners at a twentieth, PSliver's far point at a fiftieth. */
static const float v9x_guard_colours[12] = {
    1.0f, 0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 1.0f
};
static const float v9x_guard_rhw_cover[3] = { 1.0f, 0.05f, 0.05f };
static const float v9x_guard_rhw_sliver[3] = { 0.02f, 1.0f, 1.0f };

/* Signed distance of (px, py) from the edge a -> b, positive on the left
 * as the triangle winds. */
static double v9x_guard_edge(const float *a, const float *b, double px,
                             double py)
{
    double ex = (double)b[0] - (double)a[0];
    double ey = (double)b[1] - (double)a[1];
    double length = ex * ex + ey * ey;
    double cross = ex * (py - (double)a[1]) - ey * (px - (double)a[0]);
    double root = length;
    int step;

    /* sqrt by Newton, the publisher links no maths library. */
    if (length <= 0.0) {
        return 0.0;
    }
    for (step = 0; step < 40; ++step) {
        root = 0.5 * (root + length / root);
    }
    return cross / root;
}

/* 1 inside by more than the margin, -1 outside by more, 0 neither. */
static int v9x_guard_cover(const float *xy, double px, double py)
{
    double d0 = v9x_guard_edge(&xy[0], &xy[2], px, py);
    double d1 = v9x_guard_edge(&xy[2], &xy[4], px, py);
    double d2 = v9x_guard_edge(&xy[4], &xy[0], px, py);
    double area = ((double)xy[2] - (double)xy[0]) *
                      ((double)xy[5] - (double)xy[1]) -
                  ((double)xy[4] - (double)xy[0]) *
                      ((double)xy[3] - (double)xy[1]);

    if (area < 0.0) {
        d0 = -d0;
        d1 = -d1;
        d2 = -d2;
    }
    if (d0 > V9X_GUARD_MARGIN && d1 > V9X_GUARD_MARGIN &&
        d2 > V9X_GUARD_MARGIN) {
        return 1;
    }
    if (d0 < -V9X_GUARD_MARGIN || d1 < -V9X_GUARD_MARGIN ||
        d2 < -V9X_GUARD_MARGIN) {
        return -1;
    }
    return 0;
}

/*
 * The perspective-correct colour at (px, py) from per-vertex colours and
 * weights, as the engine interpolates: screen barycentrics b, then
 * sum(b w c) / sum(b w), in doubles.
 */
static void v9x_guard_perspective(const float *xy, const float *colour,
                                  const float *rhw, double px, double py,
                                  float out[3])
{
    double area = ((double)xy[2] - (double)xy[0]) *
                      ((double)xy[5] - (double)xy[1]) -
                  ((double)xy[4] - (double)xy[0]) *
                      ((double)xy[3] - (double)xy[1]);
    double b[3];
    double weight = 0.0;
    DWORD i;
    DWORD c;

    b[0] = (((double)xy[2] - px) * ((double)xy[5] - py) -
            ((double)xy[4] - px) * ((double)xy[3] - py)) / area;
    b[1] = (((double)xy[4] - px) * ((double)xy[1] - py) -
            ((double)xy[0] - px) * ((double)xy[5] - py)) / area;
    b[2] = 1.0 - b[0] - b[1];
    for (i = 0ul; i < 3ul; ++i) {
        weight += b[i] * (double)rhw[i];
    }
    for (c = 0ul; c < 3ul; ++c) {
        double sum = 0.0;

        for (i = 0ul; i < 3ul; ++i) {
            sum += b[i] * (double)rhw[i] * (double)colour[i * 4ul + c];
        }
        out[c] = (float)(sum / weight);
    }
}

static int v9x_guard_draw(const char *section, const char *name,
                          const float *xy, const float *colour,
                          const float *rhw, const DWORD *code,
                          const struct v9x_nv50_ps11_info *info,
                          const DWORD *constants,
                          const struct v9x_nv50_3d_scratch *scratch,
                          DWORD first_word)
{
    struct v9x_probe_geometry geometry;
    struct v9x_nv50_push push;
    DWORD words = V9X_DEPTH_PIXELS;
    DWORD pattern = V9X_FILL_PATTERN & V9X_COLOUR_MASK;
    DWORD ok = 0ul;
    DWORD bad = 0ul;
    DWORD leaks = 0ul;
    DWORD unjudged = 0ul;
    DWORD first_bad = 0xfffffffful;
    DWORD first_bad_pixel = 0ul;
    long max_diff = 0l;
    DWORD block;
    DWORD index;
    char key[48];

    geometry.count = 3ul;
    geometry.xy = xy;
    geometry.x0 = 0.0f;
    geometry.y0 = 0.0f;
    geometry.width = (float)(long)V9X_FILL_WIDTH;
    geometry.height = (float)(long)V9X_FILL_HEIGHT;
    geometry.scissor_left = V9X_GUARD_INSET;
    geometry.scissor_top = V9X_GUARD_INSET;
    geometry.scissor_right = V9X_FILL_WIDTH - V9X_GUARD_INSET;
    geometry.scissor_bottom = V9X_FILL_HEIGHT - V9X_GUARD_INSET;
    geometry.colour = colour;
    geometry.rhw = rhw;

    v9x_ops_begin();
    v9x_op(V9X_OP_VRAM_FILL, V9X_PS11_SURFACE, words, V9X_FILL_PATTERN);
    v9x_vram_data(V9X_PS11_CODE, code, info->words);
    if (!v9x_run("guard-data")) {
        v9x_finish("GUARD-DATA-REFUSED", 8u);
    }
    v9x_ops_begin();
    v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) +
                                  first_word,
                       V9X_PS11_PUSH_WORDS);
    v9x_ps11_probe_push(&push, 4ul, V9X_NV50_3D_RT_BGRX8, constants, info,
                        scratch, &geometry);
    if (push.overflow != 0ul) {
        v9x_text(section, "Result", "PUSH-OVERFLOW");
        return 0;
    }
    if (!v9x_kick(section, &push, first_word)) {
        lstrcpyA(key, name);
        lstrcatA(key, "Kick");
        v9x_text(section, key, "NOT-CLEAN");
        v9x_mp_trap(section);
        v9x_ps11_probe_trapped = 1;
        return 0;
    }

    for (block = 0ul; block < words; block += V9X_N5CH_BLOCK) {
        DWORD run = words - block;

        if (run > V9X_N5CH_BLOCK) {
            run = V9X_N5CH_BLOCK;
        }
        v9x_ops_begin();
        v9x_op(V9X_OP_VRAM_BLOCK, V9X_PS11_SURFACE + block * 4ul, run, 0ul);
        if (!v9x_run("guard-readback")) {
            v9x_finish("GUARD-READBACK-REFUSED", 8u);
        }
        for (index = block; index < block + run; ++index) {
            DWORD pixel = v9x_result.block[index - block] & V9X_COLOUR_MASK;
            DWORD x = index % V9X_FILL_WIDTH;
            DWORD y = index / V9X_FILL_WIDTH;
            double px = (double)(long)x + 0.5;
            double py = (double)(long)y + 0.5;
            int cover;
            int good = 1;

            if (x < geometry.scissor_left || x >= geometry.scissor_right ||
                y < geometry.scissor_top || y >= geometry.scissor_bottom) {
                if (pixel != pattern) {
                    ++leaks;
                }
                continue;
            }
            cover = v9x_guard_cover(xy, px, py);
            if (cover == 0) {
                ++unjudged;
                continue;
            }
            if (cover < 0) {
                good = pixel == pattern;
            } else {
                float fx = (float)px / geometry.width;
                float fy = (float)py / geometry.height;
                float perspective[3];
                long got[3];
                long want[3];
                long diff;
                DWORD c;

                if (colour != 0) {
                    v9x_guard_perspective(xy, colour, rhw, px, py,
                                          perspective);
                }
                got[0] = (long)((pixel >> 16) & 0xfful);
                got[1] = (long)((pixel >> 8) & 0xfful);
                got[2] = (long)(pixel & 0xfful);
                for (c = 0ul; c < 3ul; ++c) {
                    want[c] = v9x_ps11_probe_unorm(
                        colour != 0 ? perspective[c]
                                    : v9x_ps11_probe_plane(0ul, c, fx, fy),
                        255ul);
                    diff = got[c] - want[c];
                    if (diff < 0l) {
                        diff = -diff;
                    }
                    if (diff > max_diff) {
                        max_diff = diff;
                    }
                    if (diff > 2l) {
                        good = 0;
                    }
                }
            }
            if (good) {
                ++ok;
                continue;
            }
            ++bad;
            if (first_bad == 0xfffffffful) {
                first_bad = index;
                first_bad_pixel = pixel;
            }
        }
    }
    {
        char field[64];

        lstrcpyA(field, name);
        lstrcatA(field, "Ok");
        v9x_number(section, field, ok);
        lstrcpyA(field, name);
        lstrcatA(field, "Bad");
        v9x_number(section, field, bad);
        lstrcpyA(field, name);
        lstrcatA(field, "Leaks");
        v9x_number(section, field, leaks);
        lstrcpyA(field, name);
        lstrcatA(field, "Unjudged");
        v9x_number(section, field, unjudged);
        lstrcpyA(field, name);
        lstrcatA(field, "MaxDiff");
        v9x_number(section, field, (DWORD)max_diff);
        if (first_bad != 0xfffffffful) {
            lstrcpyA(field, name);
            lstrcatA(field, "FirstBadX");
            v9x_number(section, field, first_bad % V9X_FILL_WIDTH);
            lstrcpyA(field, name);
            lstrcatA(field, "FirstBadY");
            v9x_number(section, field, first_bad / V9X_FILL_WIDTH);
            lstrcpyA(field, name);
            lstrcatA(field, "FirstBadPixel");
            v9x_key(section, field, first_bad_pixel);
        }
    }
    v9x_flush();
    return bad == 0ul && leaks == 0ul && ok != 0ul;
}

/* A decimal for a key name; value under 100000. */
static void v9x_guard_decimal(char *out, DWORD value)
{
    char digits[8];
    int count = 0;
    int at = 0;

    do {
        digits[count++] = (char)('0' + (int)(value % 10ul));
        value /= 10ul;
    } while (value != 0ul && count < 7);
    while (count > 0) {
        out[at++] = digits[--count];
    }
    out[at] = '\0';
}

static int v9x_stage_3d_guard(void)
{
    static DWORD code[V9X_PS11_CODE_WORDS];
    static V9X_PS11_PROGRAM program;
    static const DWORD tokens[] = {
        V9X_PT_VER(1ul),
        V9X_PT_OP_MOV, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_V, 0ul),
        V9X_PT_END
    };
    const char *section = "Guard";
    struct v9x_nv50_3d_scratch scratch;
    struct v9x_nv50_ps11_desc desc;
    struct v9x_nv50_ps11_info info;
    V9X_PS11_ENV env;
    DWORD constants[V9X_NV50_PS11_C_FLOATS];
    DWORD vp[V9X_PS11_VP_WORDS];
    DWORD vp_count;
    DWORD first_word = v9x_push_next;
    DWORD where = 0ul;
    DWORD index;
    DWORD first_fail = 0xfffffffful;
    float xy[6];
    char name[32];

    v9x_push_next += V9X_PS11_PUSH_WORDS;
    v9x_nv50_3d_scratch_at(V9X_WINDOW_VRAM + V9X_3D_SCRATCH, &scratch);
    vp_count = v9x_ps11_probe_vp(vp, V9X_PS11_VP_WORDS);
    if (vp_count == 0ul ||
        v9x_ps11_parse(tokens, sizeof(tokens) / sizeof(tokens[0]), &program,
                       &where) != V9X_PS11_OK) {
        v9x_text(section, "Result", "ENCODE-FAILED");
        return 0;
    }
    desc.cube = 0ul;
    desc.projected = 0ul;
    desc.unbound = 0ul;
    desc.slot = V9X_PS11_CB_SLOT;
    desc.fog = 0ul;
    v9x_ps11_probe_env(0ul, &env);
    if (v9x_nv50_ps11_build(code, V9X_PS11_CODE_WORDS, &program, &desc,
                            &info) != V9X_STATUS_OK) {
        v9x_text(section, "Result", "ENCODE-FAILED");
        return 0;
    }
    v9x_nv50_ps11_constants(&program, &env, constants);
    v9x_ops_begin();
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_VP +
                      V9X_PS11_VP_START,
                  vp, vp_count);
    if (!v9x_run("guard-vp")) {
        v9x_finish("GUARD-VP-REFUSED", 8u);
    }

    for (index = 0ul; index < V9X_GUARD_DISTANCES; ++index) {
        float d = (float)(long)v9x_guard_distances[index];
        int ok;

        xy[0] = -d;
        xy[1] = -d;
        xy[2] = 2.0f * d + 300.0f;
        xy[3] = -d;
        xy[4] = -d;
        xy[5] = 2.0f * d + 300.0f;
        lstrcpyA(name, "Cover");
        v9x_guard_decimal(name + lstrlenA(name), v9x_guard_distances[index]);
        ok = v9x_guard_draw(section, name, xy, 0, 0, code, &info, constants,
                            &scratch, first_word);
        if (v9x_ps11_probe_trapped) {
            v9x_text(section, "Result", "NOT-CLEAN");
            return 0;
        }
        /* The same in perspective: the far corners far away (a sky's
         * shape), each vertex its own colour. */
        lstrcpyA(name, "PCover");
        v9x_guard_decimal(name + lstrlenA(name), v9x_guard_distances[index]);
        if (!v9x_guard_draw(section, name, xy, v9x_guard_colours,
                            v9x_guard_rhw_cover, code, &info, constants,
                            &scratch, first_word)) {
            ok = 0;
        }
        if (v9x_ps11_probe_trapped) {
            v9x_text(section, "Result", "NOT-CLEAN");
            return 0;
        }
        xy[0] = -d;
        xy[1] = 36.0f;
        xy[2] = 100.0f;
        xy[3] = 12.0f;
        xy[4] = 100.0f;
        xy[5] = 60.0f;
        lstrcpyA(name, "Sliver");
        v9x_guard_decimal(name + lstrlenA(name), v9x_guard_distances[index]);
        if (!v9x_guard_draw(section, name, xy, 0, 0, code, &info, constants,
                            &scratch, first_word)) {
            ok = 0;
        }
        if (v9x_ps11_probe_trapped) {
            v9x_text(section, "Result", "NOT-CLEAN");
            return 0;
        }
        lstrcpyA(name, "PSliver");
        v9x_guard_decimal(name + lstrlenA(name), v9x_guard_distances[index]);
        if (!v9x_guard_draw(section, name, xy, v9x_guard_colours,
                            v9x_guard_rhw_sliver, code, &info, constants,
                            &scratch, first_word)) {
            ok = 0;
        }
        if (v9x_ps11_probe_trapped) {
            v9x_text(section, "Result", "NOT-CLEAN");
            return 0;
        }
        if (!ok && first_fail == 0xfffffffful) {
            first_fail = v9x_guard_distances[index];
        }
    }
    if (first_fail != 0xfffffffful) {
        v9x_number(section, "FirstFailDistance", first_fail);
        v9x_text(section, "Result", "FAILS-PAST-BAND");
        return 0;
    }
    v9x_text(section, "Result", "PASS");
    return 1;
}

/*
 * /z:tiled2d: step 2 of docs\plans\nvidia-gt218-3d-z.md, the first use of
 * an alias window, no 3D. A 128x72 X8R8G8B8 gradient in linear surface A
 * is copied by 502D into T, a block-linear surface (tile mode 10h, eight
 * rows a block) addressed through the kind-70h window, and back out into
 * linear surface B, prefilled with the pattern. B must equal A pixel for
 * pixel (the top byte excepted). T's raw bytes, read through BAR1, are
 * counted against A: none differing would mean the window's kind changed
 * nothing. The copy out is the present path's copy.
 */
#define V9X_Z_A_OFFSET       0x00380000ul
#define V9X_Z_T_OFFSET       0x003a0000ul
#define V9X_Z_B_OFFSET       0x003c0000ul
#define V9X_Z_TILE_MODE      0x10ul
#define V9X_Z_PUSH_WORDS     128ul

static DWORD v9x_z_gradient(DWORD x, DWORD y)
{
    return ((y << 16) | (x << 8) | ((x * 3ul) ^ (y * 7ul))) &
           V9X_COLOUR_MASK;
}

static int v9x_stage_z_tiled2d(void)
{
    static DWORD gradient[V9X_DEPTH_PIXELS];
    struct v9x_nv50_surface a;
    struct v9x_nv50_surface t;
    struct v9x_nv50_surface b;
    struct v9x_nv50_push push;
    DWORD first_word = v9x_push_next;
    DWORD words = V9X_DEPTH_PIXELS;
    DWORD block;
    DWORD index;
    DWORD ok = 0ul;
    DWORD bad = 0ul;
    DWORD raw_differs = 0ul;
    DWORD first_bad = 0xfffffffful;
    DWORD first_bad_word = 0ul;
    DWORD pass;

    v9x_push_next += V9X_Z_PUSH_WORDS;
    for (index = 0ul; index < words; ++index) {
        gradient[index] = v9x_z_gradient(index % V9X_FILL_WIDTH,
                                         index / V9X_FILL_WIDTH);
    }
    a.address = V9X_WINDOW_VRAM + V9X_Z_A_OFFSET;
    a.pitch = V9X_FILL_WIDTH * 4ul;
    a.width = V9X_FILL_WIDTH;
    a.height = V9X_FILL_HEIGHT;
    a.tile_mode = 0ul;
    a.tiled = 0ul;
    b = a;
    b.address = V9X_WINDOW_VRAM + V9X_Z_B_OFFSET;
    t = a;
    t.address = V9X_NV50_VM_TILED + V9X_WINDOW_VRAM + V9X_Z_T_OFFSET;
    t.tile_mode = V9X_Z_TILE_MODE;
    t.tiled = 1ul;

    v9x_ops_begin();
    v9x_vram_data(V9X_Z_A_OFFSET, gradient, words);
    v9x_op(V9X_OP_VRAM_FILL, V9X_Z_T_OFFSET, words, 0ul);
    v9x_op(V9X_OP_VRAM_FILL, V9X_Z_B_OFFSET, words, V9X_FILL_PATTERN);
    v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) +
                                  first_word,
                       V9X_Z_PUSH_WORDS);
    v9x_nv50_push_2d_surface_blit(&push, &a, &t, V9X_502D_FORMAT_X8R8G8B8,
                                  0ul, 0ul, 0ul, 0ul, V9X_FILL_WIDTH,
                                  V9X_FILL_HEIGHT);
    v9x_nv50_push_2d_surface_blit(&push, &t, &b, V9X_502D_FORMAT_X8R8G8B8,
                                  0ul, 0ul, 0ul, 0ul, V9X_FILL_WIDTH,
                                  V9X_FILL_HEIGHT);
    if (push.overflow != 0ul) {
        v9x_text("ZTiled2d", "Result", "PUSH-OVERFLOW");
        return 0;
    }
    v9x_number("ZTiled2d", "PushWords", push.count);
    if (!v9x_kick("ZTiled2d", &push, first_word)) {
        v9x_text("ZTiled2d", "Result", "NOT-CLEAN");
        return 0;
    }

    /* B against A, then T's raw words against A. */
    for (pass = 0ul; pass < 2ul; ++pass) {
        DWORD offset = pass == 0ul ? V9X_Z_B_OFFSET : V9X_Z_T_OFFSET;

        for (block = 0ul; block < words; block += V9X_N5CH_BLOCK) {
            DWORD count = words - block;

            if (count > V9X_N5CH_BLOCK) {
                count = V9X_N5CH_BLOCK;
            }
            v9x_ops_begin();
            v9x_op(V9X_OP_VRAM_BLOCK, offset + block * 4ul, count, 0ul);
            if (!v9x_run("z-readback")) {
                v9x_finish("Z-READBACK-REFUSED", 12u);
            }
            for (index = 0ul; index < count; ++index) {
                DWORD word = v9x_result.block[index] & V9X_COLOUR_MASK;

                if (pass == 1ul) {
                    if (word != gradient[block + index]) {
                        ++raw_differs;
                    }
                    continue;
                }
                if (word == gradient[block + index]) {
                    ++ok;
                    continue;
                }
                ++bad;
                if (first_bad == 0xfffffffful) {
                    first_bad = block + index;
                    first_bad_word = v9x_result.block[index];
                }
            }
        }
    }
    v9x_number("ZTiled2d", "RoundTripOk", ok);
    v9x_number("ZTiled2d", "RoundTripBad", bad);
    v9x_number("ZTiled2d", "TiledRawDiffers", raw_differs);
    if (first_bad != 0xfffffffful) {
        v9x_number("ZTiled2d", "FirstBadX", first_bad % V9X_FILL_WIDTH);
        v9x_number("ZTiled2d", "FirstBadY", first_bad / V9X_FILL_WIDTH);
        v9x_key("ZTiled2d", "FirstBadWord", first_bad_word);
    }
    v9x_flush();
    if (bad != 0ul) {
        v9x_text("ZTiled2d", "Result", "PIXELS-DIFFER");
        return 0;
    }
    if (raw_differs == 0ul) {
        v9x_text("ZTiled2d", "Result", "NOT-TILED");
        return 0;
    }
    v9x_text("ZTiled2d", "Result", "PASS");
    return 1;
}

/*
 * /z:tiledrt (with /3d): step 3 of docs\plans\nvidia-gt218-3d-z.md, the
 * 3D engine drawing into a block-linear target. Three kicks, each waited
 * for: 502D copies a pattern-filled linear surface into the tiled surface
 * T (through the kind-70h window); the 3D engine draws phase 2's solid
 * rectangle into T; 502D copies T out to a linear surface O. O must be the
 * colour inside the rectangle and the pattern everywhere else.
 */
static int v9x_stage_z_tiledrt(void)
{
    struct v9x_nv50_3d_scratch scratch;
    struct v9x_nv50_3d_solid solid;
    struct v9x_nv50_surface pattern;
    struct v9x_nv50_surface t;
    struct v9x_nv50_surface out;
    struct v9x_nv50_push push;
    DWORD words = V9X_DEPTH_PIXELS;
    DWORD first;
    DWORD block;
    DWORD index;
    DWORD x;
    DWORD y;
    DWORD inside_ok = 0ul;
    DWORD inside_bad = 0ul;
    DWORD outside_ok = 0ul;
    DWORD outside_bad = 0ul;
    DWORD first_bad = 0xfffffffful;
    DWORD first_bad_word = 0ul;
    int inside;

    pattern.address = V9X_WINDOW_VRAM + V9X_Z_B_OFFSET;
    pattern.pitch = V9X_FILL_WIDTH * 4ul;
    pattern.width = V9X_FILL_WIDTH;
    pattern.height = V9X_FILL_HEIGHT;
    pattern.tile_mode = 0ul;
    pattern.tiled = 0ul;
    out = pattern;
    out.address = V9X_WINDOW_VRAM + V9X_Z_A_OFFSET;
    t = pattern;
    t.address = V9X_NV50_VM_TILED + V9X_WINDOW_VRAM + V9X_Z_T_OFFSET;
    t.tile_mode = V9X_Z_TILE_MODE;
    t.tiled = 1ul;
    v9x_nv50_3d_scratch_at(V9X_WINDOW_VRAM + V9X_3D_SCRATCH, &scratch);
    v9x_nv50_3d_solid_tables(&scratch, V9X_3D_DRAW_ARGB, &solid);

    v9x_ops_begin();
    v9x_op(V9X_OP_VRAM_FILL, V9X_Z_B_OFFSET, words, V9X_FILL_PATTERN);
    v9x_op(V9X_OP_VRAM_FILL, V9X_Z_A_OFFSET, words, 0ul);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_TEXEL,
                  &solid.texel, 1ul);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_TIC, solid.tic,
                  V9X_NV50_3D_TIC_WORDS);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_TSC, solid.tsc,
                  V9X_NV50_3D_TSC_WORDS);
    if (!v9x_run("z-rt-prepare")) {
        v9x_finish("Z-RT-REFUSED", 13u);
    }

    /* A fresh op list for each kick: v9x_kick only adds to it, and the
     * first run of this stage replayed every earlier kick's ops, PUT
     * writes included, so the third kick moved PUT backwards and the
     * pusher stopped with error 5. */
    v9x_ops_begin();
    first = v9x_push_next;
    v9x_push_next += V9X_PUSH_STAGE_MAX;
    v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) + first,
                       V9X_PUSH_STAGE_MAX);
    v9x_nv50_push_2d_surface_blit(&push, &pattern, &t,
                                  V9X_502D_FORMAT_X8R8G8B8, 0ul, 0ul, 0ul,
                                  0ul, V9X_FILL_WIDTH, V9X_FILL_HEIGHT);
    if (push.overflow != 0ul || !v9x_kick("ZTiledRtFill", &push, first)) {
        v9x_text("ZTiledRt", "Result", "FILL-NOT-CLEAN");
        return 0;
    }

    /* A fresh op list for each kick: v9x_kick only adds to it, and the
     * first run of this stage replayed every earlier kick's ops, PUT
     * writes included, so the third kick moved PUT backwards and the
     * pusher stopped with error 5. */
    v9x_ops_begin();
    first = v9x_push_next;
    v9x_push_next += V9X_PUSH_STAGE_MAX;
    v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) + first,
                       V9X_PUSH_STAGE_MAX);
    v9x_nv50_push_3d_rect_on(&push, &t, V9X_NV50_3D_RT_BGRX8, V9X_3D_DRAW_X,
                             V9X_3D_DRAW_Y, V9X_3D_DRAW_W, V9X_3D_DRAW_H,
                             V9X_3D_DRAW_W, V9X_3D_DRAW_H);
    if (push.overflow != 0ul || !v9x_kick("ZTiledRtDraw", &push, first)) {
        v9x_text("ZTiledRt", "Result", "DRAW-NOT-CLEAN");
        return 0;
    }

    /* A fresh op list for each kick: v9x_kick only adds to it, and the
     * first run of this stage replayed every earlier kick's ops, PUT
     * writes included, so the third kick moved PUT backwards and the
     * pusher stopped with error 5. */
    v9x_ops_begin();
    first = v9x_push_next;
    v9x_push_next += V9X_PUSH_STAGE_MAX;
    v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) + first,
                       V9X_PUSH_STAGE_MAX);
    v9x_nv50_push_2d_surface_blit(&push, &t, &out,
                                  V9X_502D_FORMAT_X8R8G8B8, 0ul, 0ul, 0ul,
                                  0ul, V9X_FILL_WIDTH, V9X_FILL_HEIGHT);
    if (push.overflow != 0ul || !v9x_kick("ZTiledRtCopy", &push, first)) {
        v9x_text("ZTiledRt", "Result", "COPY-NOT-CLEAN");
        return 0;
    }

    for (block = 0ul; block < words; block += V9X_N5CH_BLOCK) {
        DWORD count = words - block;

        if (count > V9X_N5CH_BLOCK) {
            count = V9X_N5CH_BLOCK;
        }
        v9x_ops_begin();
        v9x_op(V9X_OP_VRAM_BLOCK, V9X_Z_A_OFFSET + block * 4ul, count, 0ul);
        if (!v9x_run("z-rt-readback")) {
            v9x_finish("Z-RT-READBACK-REFUSED", 13u);
        }
        for (index = 0ul; index < count; ++index) {
            DWORD word = v9x_result.block[index] & V9X_COLOUR_MASK;
            DWORD want;

            x = (block + index) % V9X_FILL_WIDTH;
            y = (block + index) / V9X_FILL_WIDTH;
            inside = x >= V9X_3D_DRAW_X &&
                     x < V9X_3D_DRAW_X + V9X_3D_DRAW_W &&
                     y >= V9X_3D_DRAW_Y &&
                     y < V9X_3D_DRAW_Y + V9X_3D_DRAW_H;
            want = inside ? (V9X_3D_DRAW_ARGB & V9X_COLOUR_MASK)
                          : (V9X_FILL_PATTERN & V9X_COLOUR_MASK);
            if (word == want) {
                if (inside) {
                    ++inside_ok;
                } else {
                    ++outside_ok;
                }
                continue;
            }
            if (inside) {
                ++inside_bad;
            } else {
                ++outside_bad;
            }
            if (first_bad == 0xfffffffful) {
                first_bad = block + index;
                first_bad_word = v9x_result.block[index];
            }
        }
    }
    v9x_number("ZTiledRt", "InsideOk", inside_ok);
    v9x_number("ZTiledRt", "InsideBad", inside_bad);
    v9x_number("ZTiledRt", "OutsideOk", outside_ok);
    v9x_number("ZTiledRt", "OutsideBad", outside_bad);
    if (first_bad != 0xfffffffful) {
        v9x_number("ZTiledRt", "FirstBadX", first_bad % V9X_FILL_WIDTH);
        v9x_number("ZTiledRt", "FirstBadY", first_bad / V9X_FILL_WIDTH);
        v9x_key("ZTiledRt", "FirstBadWord", first_bad_word);
    }
    v9x_flush();
    if (inside_bad == 0ul && outside_bad == 0ul) {
        v9x_text("ZTiledRt", "Result", "PASS");
        return 1;
    }
    v9x_text("ZTiledRt", "Result", "PIXELS-DIFFER");
    return 0;
}

/*
 * /z:depth (with /3d): step 4 of docs\plans\nvidia-gt218-3d-z.md. Into the
 * tiled colour target T, with a tiled Z buffer through its kind's alias
 * window: clear depth to 1.0, draw a red rectangle at z 0.25, then a green
 * one at z 0.75 overlapping it, depth test LESS. Where they overlap the
 * red one, drawn first, must remain; the rest of the green one is green;
 * everything else keeps the pattern. Run for Z16 and Z24S8.
 */
#define V9X_Z_ZETA_OFFSET    0x003e0000ul
#define V9X_Z_NEAR_X         16ul
#define V9X_Z_NEAR_Y         8ul
#define V9X_Z_FAR_X          36ul
#define V9X_Z_FAR_Y          20ul
#define V9X_Z_RECT_W         40ul
#define V9X_Z_RECT_H         24ul
#define V9X_Z_DEPTH_PUSH     192ul
#define V9X_FLOAT_ONE        0x3f800000ul
#define V9X_FLOAT_QUARTER    0x3e800000ul
#define V9X_FLOAT_THREE_QUARTERS 0x3f400000ul

/* The PROP trap words for TP0 and TP1 (nv50_gr_prop_trap): status, then
 * e0c, address low, address high, e18, e1c, e20, e24. Status bits
 * (nv50_gr_trap_prop): 20h ZETA_FAULT, 40h RT_FAULT, 200h
 * ZETA_STORAGE_TYPE_MISMATCH, 400h RT_STORAGE_TYPE_MISMATCH, 1000h
 * RT_LINEAR_MISMATCH, 4h and 8h surface width and height overrun. */
static void v9x_prop_trap(const char *section)
{
    static const char *const names[8] = {
        "Status", "E0c", "AddressLow", "AddressHigh", "E18", "E1c", "E20",
        "E24"
    };
    DWORD reads[2][8];
    DWORD tp;
    DWORD word;
    char key[32];

    v9x_ops_begin();
    for (tp = 0ul; tp < 2ul; ++tp) {
        for (word = 0ul; word < 8ul; ++word) {
            reads[tp][word] = v9x_read(0x408708ul + (tp << 11) + word * 4ul);
        }
    }
    if (!v9x_run("prop-trap")) {
        return;
    }
    for (tp = 0ul; tp < 2ul; ++tp) {
        for (word = 0ul; word < 8ul; ++word) {
            lstrcpyA(key, tp == 0ul ? "PropTp0" : "PropTp1");
            lstrcatA(key, names[word]);
            v9x_put(section, key, reads[tp][word]);
        }
    }
    v9x_flush();
}

static int v9x_z_in(DWORD x, DWORD y, DWORD left, DWORD top)
{
    return x >= left && x < left + V9X_Z_RECT_W && y >= top &&
           y < top + V9X_Z_RECT_H;
}

/*
 * /z:tall: where a Z16 the height of a Quake 3 frame puts its depth. A
 * 256-wide Z16 (the window holds no wider), tiled 10h, 600 rows (/zt:592 for 592), at ZT_BASE with
 * marker words from 64 KiB below it to 384 KiB above; the hardware clear
 * to 1.0, then a rectangle at depth 0.25 on rows 0-7 and one at 0.75 on
 * the last 8 rows. Every word that is no longer the marker is classed
 * (clear, top rectangle, bottom rectangle, other) and reported with its
 * lowest and highest offset from ZT_BASE, signed. Quake 3 at 800x600 had
 * its first tile row of Z never written and depth below the Z's base
 * (W9QI0DAD boot 39, 2026-11-16).
 */
#define V9X_ZT_BASE          0x00140000ul
#define V9X_ZT_BELOW         0x00010000ul
#define V9X_ZT_ABOVE         0x00060000ul
#define V9X_ZT_COLOUR        0x00250000ul
#define V9X_ZT_WIDTH         256ul

static int v9x_zt_half(DWORD half, DWORD centre)
{
    return half + 0x10ul >= centre && half <= centre + 0x10ul;
}

/*
 * /z:map: which 8-row band of the Z each 512-byte block of memory holds.
 * The Z as /z:tall's (/zt:592, and /zt:up64 to put it 64 KiB higher);
 * cleared to 1.0, then band k (rows 8k to 8k+7, full width) drawn at
 * depth (k+1)/128, so its Z16 is about (k+1)*512. Each block from 64 KiB
 * below the Z to 384 KiB above is reported as two hex digits in MapNN
 * keys, 32 blocks a key: the band, FF for the clear, EE for the marker,
 * DD for anything else.
 */
#define V9X_ZM_BANDS_A_KICK  6ul
#define V9X_ZM_PUSH          512ul

static int v9x_stage_z_map(void)
{
    static const char section[] = "ZMap";
    static const char digits[] = "0123456789ABCDEF";
    struct v9x_nv50_3d_scratch scratch;
    struct v9x_nv50_surface t;
    struct v9x_nv50_surface zeta;
    struct v9x_nv50_push push;
    DWORD vp[V9X_NV50_3D_GOURAUD_VP_MAX];
    DWORD fp[V9X_NV50_3D_GOURAUD_FP_MAX];
    DWORD height = v9x_switch_given("/zt:592") ? 592ul : 600ul;
    DWORD base = V9X_ZT_BASE +
                 (v9x_switch_given("/zt:up64") ? 0x10000ul : 0ul) +
                 (v9x_switch_given("/zt:up128") ? 0x20000ul : 0ul) +
                 (v9x_switch_given("/zt:up16") ? 0x4000ul : 0ul);
    DWORD bands = height / 8ul;
    DWORD words = (V9X_ZT_BELOW + V9X_ZT_ABOVE) / 4ul;
    DWORD start = base - V9X_ZT_BELOW;
    DWORD vp_count;
    DWORD fp_count;
    DWORD first;
    DWORD band;
    DWORD block;
    DWORD index;
    DWORD chunk = 0ul;
    char line[80];
    char key[16];

    t.address = V9X_NV50_VM_TILED + V9X_WINDOW_VRAM + V9X_ZT_COLOUR;
    t.pitch = 0ul;
    t.width = V9X_ZT_WIDTH;
    t.height = height;
    t.tile_mode = V9X_Z_TILE_MODE;
    t.tiled = 1ul;
    zeta = t;
    zeta.address = V9X_NV50_VM_Z16 + V9X_WINDOW_VRAM + base;

    vp_count = v9x_nv50_3d_depth_vp(vp, V9X_NV50_3D_GOURAUD_VP_MAX);
    fp_count = v9x_nv50_3d_gouraud_fp(fp, V9X_NV50_3D_GOURAUD_FP_MAX);
    if (vp_count == 0ul || fp_count == 0ul) {
        v9x_text(section, "Result", "ENCODE-FAILED");
        return 0;
    }
    v9x_nv50_3d_scratch_at(V9X_WINDOW_VRAM + V9X_3D_SCRATCH, &scratch);
    v9x_number(section, "Height", height);
    v9x_key(section, "Base", base);

    v9x_ops_begin();
    v9x_op(V9X_OP_VRAM_FILL, start, words, V9X_FILL_PATTERN);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_VP +
                      V9X_NV50_3D_DEPTH_VP_START,
                  vp, vp_count);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_FP +
                      V9X_NV50_3D_GOURAUD_FP_START,
                  fp, fp_count);
    if (!v9x_run("z-map-prepare")) {
        v9x_finish("Z-MAP-REFUSED", 14u);
    }

    /* One push slot, reused: each kick has finished before the next. */
    first = v9x_push_next;
    v9x_push_next += V9X_ZM_PUSH;
    for (band = 0ul; band < bands; band += V9X_ZM_BANDS_A_KICK) {
        DWORD last = band + V9X_ZM_BANDS_A_KICK;

        v9x_ops_begin();
        v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) + first,
                           V9X_ZM_PUSH);
        v9x_nv50_push_3d_target(&push, &t, V9X_NV50_3D_RT_BGRX8);
        v9x_nv50_push_3d_zeta(&push, &zeta, V9X_NV50_3D_ZETA_Z16);
        if (band == 0ul) {
            v9x_nv50_push_3d_clear_depth(&push, V9X_FLOAT_ONE);
        }
        if (last > bands) {
            last = bands;
        }
        for (index = band; index < last; ++index) {
            float z = (float)(index + 1ul) / 128.0f;

            v9x_nv50_push_3d_depth_rect(&push, 0ul, index * 8ul,
                                        V9X_ZT_WIDTH, 8ul, *(DWORD *)&z,
                                        V9X_FLOAT_ONE, 0ul, 0ul);
        }
        if (push.overflow != 0ul || !v9x_kick("ZMapDraw", &push, first)) {
            v9x_prop_trap(section);
            v9x_text(section, "Result", "DRAW-NOT-CLEAN");
            return 0;
        }
    }

    for (block = 0ul; block < words; block += V9X_N5CH_BLOCK) {
        DWORD count = words - block;

        if (count > V9X_N5CH_BLOCK) {
            count = V9X_N5CH_BLOCK;
        }
        v9x_ops_begin();
        v9x_op(V9X_OP_VRAM_BLOCK, start + block * 4ul, count, 0ul);
        if (!v9x_run("z-map-readback")) {
            v9x_finish("Z-MAP-READBACK-REFUSED", 14u);
        }
        /* 128 words a 512-byte block; V9X_N5CH_BLOCK is a multiple. */
        for (index = 0ul; index < count; index += 128ul) {
            DWORD word = v9x_result.block[index];
            DWORD code;
            DWORD at = (chunk % 32ul) * 2ul;

            if (word == V9X_FILL_PATTERN) {
                code = 0xeeul;
            } else if (word == 0xfffffffful) {
                code = 0xfful;
            } else {
                DWORD z = word & 0xfffful;

                code = (z + 256ul) / 512ul;
                code = code >= 1ul && code <= bands + 1ul &&
                               ((word >> 16) & 0xfffful) / 256ul ==
                                   z / 256ul ?
                           code - 1ul : 0xddul;
            }
            line[at] = digits[(code >> 4) & 0xful];
            line[at + 1ul] = digits[code & 0xful];
            ++chunk;
            if (chunk % 32ul == 0ul) {
                line[64] = '\0';
                key[0] = 'M';
                key[1] = 'a';
                key[2] = 'p';
                key[3] = (char)('0' + (chunk / 32ul - 1ul) / 10ul);
                key[4] = (char)('0' + (chunk / 32ul - 1ul) % 10ul);
                key[5] = '\0';
                v9x_text(section, key, line);
            }
        }
    }
    v9x_flush();
    v9x_text(section, "Result", "MAPPED");
    return 1;
}

static int v9x_stage_z_tall(void)
{
    static const char section[] = "ZTall";
    static const char *names[4] = { "Clear", "Top", "Bottom", "Other" };
    struct v9x_nv50_3d_scratch scratch;
    struct v9x_nv50_surface t;
    struct v9x_nv50_surface zeta;
    struct v9x_nv50_push push;
    DWORD vp[V9X_NV50_3D_GOURAUD_VP_MAX];
    DWORD fp[V9X_NV50_3D_GOURAUD_FP_MAX];
    DWORD count_of[4];
    DWORD low_of[4];
    DWORD high_of[4];
    DWORD below = 0ul;
    DWORD above = 0ul;
    DWORD height = v9x_switch_given("/zt:592") ? 592ul : 600ul;
    DWORD z_bytes = (V9X_ZT_WIDTH * 2ul) * height;
    DWORD words = (V9X_ZT_BELOW + V9X_ZT_ABOVE) / 4ul;
    DWORD start = V9X_ZT_BASE - V9X_ZT_BELOW;
    DWORD vp_count;
    DWORD fp_count;
    DWORD first;
    DWORD block;
    DWORD index;
    DWORD kind;
    char key[32];

    for (kind = 0ul; kind < 4ul; ++kind) {
        count_of[kind] = 0ul;
        low_of[kind] = 0x7ffffffful;
        high_of[kind] = 0x80000000ul;
    }
    t.address = V9X_NV50_VM_TILED + V9X_WINDOW_VRAM + V9X_ZT_COLOUR;
    t.pitch = 0ul;
    t.width = V9X_ZT_WIDTH;
    t.height = height;
    t.tile_mode = V9X_Z_TILE_MODE;
    t.tiled = 1ul;
    zeta = t;
    zeta.address = V9X_NV50_VM_Z16 + V9X_WINDOW_VRAM + V9X_ZT_BASE;

    vp_count = v9x_nv50_3d_depth_vp(vp, V9X_NV50_3D_GOURAUD_VP_MAX);
    fp_count = v9x_nv50_3d_gouraud_fp(fp, V9X_NV50_3D_GOURAUD_FP_MAX);
    if (vp_count == 0ul || fp_count == 0ul) {
        v9x_text(section, "Result", "ENCODE-FAILED");
        return 0;
    }
    v9x_nv50_3d_scratch_at(V9X_WINDOW_VRAM + V9X_3D_SCRATCH, &scratch);
    v9x_number(section, "Height", height);
    v9x_number(section, "ZBytes", z_bytes);

    v9x_ops_begin();
    v9x_op(V9X_OP_VRAM_FILL, start, words, V9X_FILL_PATTERN);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_VP +
                      V9X_NV50_3D_DEPTH_VP_START,
                  vp, vp_count);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_FP +
                      V9X_NV50_3D_GOURAUD_FP_START,
                  fp, fp_count);
    if (!v9x_run("z-tall-prepare")) {
        v9x_finish("Z-TALL-REFUSED", 14u);
    }

    v9x_ops_begin();
    first = v9x_push_next;
    v9x_push_next += V9X_Z_DEPTH_PUSH;
    v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) + first,
                       V9X_Z_DEPTH_PUSH);
    v9x_nv50_push_3d_target(&push, &t, V9X_NV50_3D_RT_BGRX8);
    v9x_nv50_push_3d_zeta(&push, &zeta, V9X_NV50_3D_ZETA_Z16);
    v9x_nv50_push_3d_clear_depth(&push, V9X_FLOAT_ONE);
    v9x_nv50_push_3d_depth_rect(&push, 0ul, 0ul, 64ul, 8ul,
                                V9X_FLOAT_QUARTER, V9X_FLOAT_ONE, 0ul, 0ul);
    v9x_nv50_push_3d_depth_rect(&push, 0ul, height - 8ul, 64ul, 8ul,
                                V9X_FLOAT_THREE_QUARTERS, 0ul, V9X_FLOAT_ONE,
                                0ul);
    v9x_number(section, "DrawPushWords", push.count);
    if (push.overflow != 0ul || !v9x_kick("ZTallDraw", &push, first)) {
        v9x_prop_trap(section);
        v9x_text(section, "Result", "DRAW-NOT-CLEAN");
        return 0;
    }

    for (block = 0ul; block < words; block += V9X_N5CH_BLOCK) {
        DWORD count = words - block;

        if (count > V9X_N5CH_BLOCK) {
            count = V9X_N5CH_BLOCK;
        }
        v9x_ops_begin();
        v9x_op(V9X_OP_VRAM_BLOCK, start + block * 4ul, count, 0ul);
        if (!v9x_run("z-tall-readback")) {
            v9x_finish("Z-TALL-READBACK-REFUSED", 14u);
        }
        for (index = 0ul; index < count; ++index) {
            DWORD word = v9x_result.block[index];
            DWORD lowh = word & 0xfffful;
            DWORD highh = word >> 16;
            long rel = (long)((block + index) * 4ul) - (long)V9X_ZT_BELOW;

            if (word == V9X_FILL_PATTERN) {
                continue;
            }
            if (word == 0xfffffffful) {
                kind = 0ul;
            } else if (v9x_zt_half(lowh, 0x4000ul) ||
                       v9x_zt_half(highh, 0x4000ul)) {
                kind = 1ul;
            } else if (v9x_zt_half(lowh, 0xc000ul) ||
                       v9x_zt_half(highh, 0xc000ul)) {
                kind = 2ul;
            } else {
                kind = 3ul;
            }
            ++count_of[kind];
            if ((long)low_of[kind] > rel) {
                low_of[kind] = (DWORD)rel;
            }
            if ((long)high_of[kind] < rel) {
                high_of[kind] = (DWORD)rel;
            }
            if (rel < 0l) {
                ++below;
            } else if ((DWORD)rel >= z_bytes) {
                ++above;
            }
        }
    }
    for (kind = 0ul; kind < 4ul; ++kind) {
        lstrcpyA(key, names[kind]);
        lstrcatA(key, "Words");
        v9x_number(section, key, count_of[kind]);
        if (count_of[kind] != 0ul) {
            lstrcpyA(key, names[kind]);
            lstrcatA(key, "Low");
            v9x_key(section, key, low_of[kind]);
            lstrcpyA(key, names[kind]);
            lstrcatA(key, "High");
            v9x_key(section, key, high_of[kind]);
        }
    }
    v9x_number(section, "WrittenBelowBase", below);
    v9x_number(section, "WrittenPastZ", above);
    v9x_flush();
    v9x_text(section, "Result", below == 0ul && above == 0ul ? "INSIDE"
                                                             : "OUTSIDE");
    return 1;
}

static int v9x_stage_z_depth(const char *section, DWORD window,
                             DWORD bytes_per_z, DWORD zformat)
{
    struct v9x_nv50_3d_scratch scratch;
    struct v9x_nv50_surface pattern;
    struct v9x_nv50_surface t;
    struct v9x_nv50_surface out;
    struct v9x_nv50_surface zeta;
    struct v9x_nv50_push push;
    DWORD vp[V9X_NV50_3D_GOURAUD_VP_MAX];
    DWORD fp[V9X_NV50_3D_GOURAUD_FP_MAX];
    DWORD vp_count;
    DWORD fp_count;
    DWORD words = V9X_DEPTH_PIXELS;
    DWORD first;
    DWORD block;
    DWORD index;
    DWORD x;
    DWORD y;
    DWORD red_ok = 0ul;
    DWORD green_ok = 0ul;
    DWORD pattern_ok = 0ul;
    DWORD bad = 0ul;
    DWORD overlap_red = 0ul;
    DWORD first_bad = 0xfffffffful;
    DWORD first_bad_word = 0ul;
    char name[32];

    pattern.address = V9X_WINDOW_VRAM + V9X_Z_B_OFFSET;
    pattern.pitch = V9X_FILL_WIDTH * 4ul;
    pattern.width = V9X_FILL_WIDTH;
    pattern.height = V9X_FILL_HEIGHT;
    pattern.tile_mode = 0ul;
    pattern.tiled = 0ul;
    out = pattern;
    out.address = V9X_WINDOW_VRAM + V9X_Z_A_OFFSET;
    t = pattern;
    t.address = V9X_NV50_VM_TILED + V9X_WINDOW_VRAM + V9X_Z_T_OFFSET;
    t.tile_mode = V9X_Z_TILE_MODE;
    t.tiled = 1ul;
    zeta = t;
    zeta.address = window + V9X_WINDOW_VRAM + V9X_Z_ZETA_OFFSET;
    zeta.pitch = V9X_FILL_WIDTH * bytes_per_z;

    vp_count = v9x_nv50_3d_depth_vp(vp, V9X_NV50_3D_GOURAUD_VP_MAX);
    fp_count = v9x_nv50_3d_gouraud_fp(fp, V9X_NV50_3D_GOURAUD_FP_MAX);
    if (vp_count == 0ul || fp_count == 0ul) {
        v9x_text(section, "Result", "ENCODE-FAILED");
        return 0;
    }
    v9x_nv50_3d_scratch_at(V9X_WINDOW_VRAM + V9X_3D_SCRATCH, &scratch);

    v9x_ops_begin();
    v9x_op(V9X_OP_VRAM_FILL, V9X_Z_B_OFFSET, words, V9X_FILL_PATTERN);
    v9x_op(V9X_OP_VRAM_FILL, V9X_Z_A_OFFSET, words, 0ul);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_VP +
                      V9X_NV50_3D_DEPTH_VP_START,
                  vp, vp_count);
    v9x_vram_data(V9X_3D_SCRATCH + V9X_NV50_3D_SCRATCH_FP +
                      V9X_NV50_3D_GOURAUD_FP_START,
                  fp, fp_count);
    if (!v9x_run("z-depth-prepare")) {
        v9x_finish("Z-DEPTH-REFUSED", 14u);
    }

    v9x_ops_begin();
    first = v9x_push_next;
    v9x_push_next += V9X_PUSH_STAGE_MAX;
    v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) + first,
                       V9X_PUSH_STAGE_MAX);
    v9x_nv50_push_2d_surface_blit(&push, &pattern, &t,
                                  V9X_502D_FORMAT_X8R8G8B8, 0ul, 0ul, 0ul,
                                  0ul, V9X_FILL_WIDTH, V9X_FILL_HEIGHT);
    lstrcpyA(name, section);
    lstrcatA(name, "Fill");
    if (push.overflow != 0ul || !v9x_kick(name, &push, first)) {
        v9x_text(section, "Result", "FILL-NOT-CLEAN");
        return 0;
    }

    v9x_ops_begin();
    first = v9x_push_next;
    v9x_push_next += V9X_Z_DEPTH_PUSH;
    v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) + first,
                       V9X_Z_DEPTH_PUSH);
    v9x_nv50_push_3d_target(&push, &t, V9X_NV50_3D_RT_BGRX8);
    v9x_nv50_push_3d_zeta(&push, &zeta, zformat);
    v9x_nv50_push_3d_clear_depth(&push, V9X_FLOAT_ONE);
    v9x_nv50_push_3d_depth_rect(&push, V9X_Z_NEAR_X, V9X_Z_NEAR_Y,
                                V9X_Z_RECT_W, V9X_Z_RECT_H,
                                V9X_FLOAT_QUARTER, V9X_FLOAT_ONE, 0ul, 0ul);
    v9x_nv50_push_3d_depth_rect(&push, V9X_Z_FAR_X, V9X_Z_FAR_Y,
                                V9X_Z_RECT_W, V9X_Z_RECT_H,
                                V9X_FLOAT_THREE_QUARTERS, 0ul, V9X_FLOAT_ONE,
                                0ul);
    lstrcpyA(name, section);
    lstrcatA(name, "Draw");
    v9x_number(section, "DrawPushWords", push.count);
    if (push.overflow != 0ul || !v9x_kick(name, &push, first)) {
        v9x_prop_trap(section);
        v9x_text(section, "Result", "DRAW-NOT-CLEAN");
        return 0;
    }

    v9x_ops_begin();
    first = v9x_push_next;
    v9x_push_next += V9X_PUSH_STAGE_MAX;
    v9x_nv50_push_init(&push, v9x_region + (V9X_NV50_PUSH / 4ul) + first,
                       V9X_PUSH_STAGE_MAX);
    v9x_nv50_push_2d_surface_blit(&push, &t, &out,
                                  V9X_502D_FORMAT_X8R8G8B8, 0ul, 0ul, 0ul,
                                  0ul, V9X_FILL_WIDTH, V9X_FILL_HEIGHT);
    lstrcpyA(name, section);
    lstrcatA(name, "Copy");
    if (push.overflow != 0ul || !v9x_kick(name, &push, first)) {
        v9x_text(section, "Result", "COPY-NOT-CLEAN");
        return 0;
    }

    for (block = 0ul; block < words; block += V9X_N5CH_BLOCK) {
        DWORD count = words - block;

        if (count > V9X_N5CH_BLOCK) {
            count = V9X_N5CH_BLOCK;
        }
        v9x_ops_begin();
        v9x_op(V9X_OP_VRAM_BLOCK, V9X_Z_A_OFFSET + block * 4ul, count, 0ul);
        if (!v9x_run("z-depth-readback")) {
            v9x_finish("Z-DEPTH-READBACK-REFUSED", 14u);
        }
        for (index = 0ul; index < count; ++index) {
            DWORD word = v9x_result.block[index] & V9X_COLOUR_MASK;
            DWORD want;
            int near_in;
            int far_in;

            x = (block + index) % V9X_FILL_WIDTH;
            y = (block + index) / V9X_FILL_WIDTH;
            near_in = v9x_z_in(x, y, V9X_Z_NEAR_X, V9X_Z_NEAR_Y);
            far_in = v9x_z_in(x, y, V9X_Z_FAR_X, V9X_Z_FAR_Y);
            if (near_in) {
                want = 0x00ff0000ul;
            } else if (far_in) {
                want = 0x0000ff00ul;
            } else {
                want = V9X_FILL_PATTERN & V9X_COLOUR_MASK;
            }
            if (word == want) {
                if (near_in) {
                    ++red_ok;
                    if (far_in) {
                        ++overlap_red;
                    }
                } else if (far_in) {
                    ++green_ok;
                } else {
                    ++pattern_ok;
                }
                continue;
            }
            ++bad;
            if (first_bad == 0xfffffffful) {
                first_bad = block + index;
                first_bad_word = v9x_result.block[index];
            }
        }
    }
    v9x_number(section, "RedOk", red_ok);
    v9x_number(section, "OverlapRedOk", overlap_red);
    v9x_number(section, "GreenOk", green_ok);
    v9x_number(section, "PatternOk", pattern_ok);
    v9x_number(section, "Bad", bad);
    if (first_bad != 0xfffffffful) {
        v9x_number(section, "FirstBadX", first_bad % V9X_FILL_WIDTH);
        v9x_number(section, "FirstBadY", first_bad / V9X_FILL_WIDTH);
        v9x_key(section, "FirstBadWord", first_bad_word);
    }
    v9x_flush();
    if (bad == 0ul) {
        v9x_text(section, "Result", "PASS");
        return 1;
    }
    v9x_text(section, "Result", "PIXELS-DIFFER");
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
    v9x_stop_after_region = v9x_switch_given("/stop:region") ? 1ul : 0ul;
    v9x_push_empty = v9x_switch_given("/push:empty") ? 1ul : 0ul;
    v9x_flush_none = v9x_switch_given("/flush:none") ? 1ul : 0ul;
    v9x_ib_unmapped = v9x_switch_given("/ib:unmapped") ? 1ul : 0ul;
    v9x_push_unmapped = v9x_switch_given("/push:unmapped") ? 1ul : 0ul;
    v9x_push_class02 = v9x_switch_given("/push:class02") ? 1ul : 0ul;
    v9x_timeline = v9x_switch_given("/timeline") ? 1ul : 0ul;
    v9x_pusher = v9x_switch_given("/pusher") ? 1ul : 0ul;
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
    v9x_config = config;
    v9x_key("Report", "Config", config);
    v9x_flush();

    v9x_stage_pre();
    if (v9x_switch_given("/check")) {
        v9x_stage_check();
    }
    v9x_stage_region();
    v9x_vram_check("region");
    /* /lean: only what the HAL engine will do. The region is written and
     * the bring-up list replayed; the PRAMIN check, the flush page, the
     * PCIe reads, PMC_ENABLE = FFFFFFFFh, the BAR channel and the BAR
     * flush are all left out, as nv50_init.c leaves them out. */
    if (!v9x_switch_given("/lean")) {
        if (!v9x_stage_pramin()) {
            v9x_finish("PRAMIN-DIFFERS", 4u);
        }
        v9x_stage_flush_page();
        if (v9x_stop_after_region != 0ul) {
            v9x_finish("STOPPED-AFTER-REGION", 0u);
        }
        v9x_stage_pcie();
        v9x_stage_mc();
        v9x_stage_bar();
    }
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
    if (!v9x_stage_copy()) {
        v9x_finish("COPY-FAILED", 7u);
    }
    /* /many: carry the IB ring past its end before the depth tests, so
     * they run on a ring that has wrapped. */
    if (v9x_switch_given("/many") && !v9x_stage_many()) {
        v9x_finish("MANY-FAILED", 7u);
    }
    /* Every depth runs even if one fails: a failure at 8 bpp says
     * nothing about 16. The run's result names the first that failed. */
    {
        int ok32 = v9x_stage_depth("Depth32", 0ul, 4ul,
                                   V9X_502D_FORMAT_X8R8G8B8,
                                   V9X_FILL_COLOUR);
        int ok16 = v9x_stage_depth("Depth16", 1ul, 2ul,
                                   V9X_502D_FORMAT_R5G6B5, 0xa5c3ul);
        int ok8 = v9x_stage_depth("Depth8", 2ul, 1ul,
                                  V9X_502D_FORMAT_Y8, 0x5dul);

        v9x_pusher_state();
        if (!ok32) {
            v9x_finish("DEPTH32-FAILED", 7u);
        }
        if (!ok16) {
            v9x_finish("DEPTH16-FAILED", 7u);
        }
        if (!ok8) {
            v9x_finish("DEPTH8-FAILED", 7u);
        }
    }
    if (v9x_switch_given("/3d") && !v9x_stage_3d()) {
        v9x_finish("3D-FAILED", 8u);
    }
    /* /3d:draw needs /3d's setup first; both depths run even if one
     * fails, as the 2D depths do. */
    if (v9x_switch_given("/3d") && v9x_switch_given("/3d:draw")) {
        int ok32 = v9x_stage_3d_draw("Draw32", 0ul, 4ul,
                                     V9X_NV50_3D_RT_BGRX8,
                                     V9X_3D_DRAW_ARGB & V9X_COLOUR_MASK);
        int ok16 = v9x_stage_3d_draw("Draw16", 1ul, 2ul,
                                     V9X_NV50_3D_RT_B5G6R5, 0xa61cul);

        v9x_pusher_state();
        if (!ok32) {
            v9x_finish("DRAW32-FAILED", 9u);
        }
        if (!ok16) {
            v9x_finish("DRAW16-FAILED", 9u);
        }
    }
    if (v9x_switch_given("/3d") && v9x_switch_given("/3d:gouraud")) {
        int ok32 = v9x_stage_3d_gouraud("Gouraud32", 0ul, 4ul,
                                        V9X_NV50_3D_RT_BGRX8);
        int ok16 = v9x_stage_3d_gouraud("Gouraud16", 1ul, 2ul,
                                        V9X_NV50_3D_RT_B5G6R5);

        v9x_pusher_state();
        if (!ok32) {
            v9x_finish("GOURAUD32-FAILED", 10u);
        }
        if (!ok16) {
            v9x_finish("GOURAUD16-FAILED", 10u);
        }
    }
    if (v9x_switch_given("/z:tiled2d") && !v9x_stage_z_tiled2d()) {
        v9x_finish("Z-TILED2D-FAILED", 12u);
    }
    if (v9x_switch_given("/3d") && v9x_switch_given("/z:tiledrt") &&
        !v9x_stage_z_tiledrt()) {
        v9x_finish("Z-TILEDRT-FAILED", 13u);
    }
    /* One Z format per run: a PGRAPH trap stays latched and spoils
     * whatever follows, and each run's bring-up clears it. */
    if (v9x_switch_given("/3d") && v9x_switch_given("/z:depth16")) {
        if (!v9x_stage_z_depth("ZDepth16", V9X_NV50_VM_Z16, 2ul,
                               V9X_NV50_3D_ZETA_Z16)) {
            v9x_finish("ZDEPTH16-FAILED", 14u);
        }
    }
    if (v9x_switch_given("/3d") && v9x_switch_given("/z:map")) {
        if (!v9x_stage_z_map()) {
            v9x_finish("ZMAP-FAILED", 14u);
        }
    }
    if (v9x_switch_given("/3d") && v9x_switch_given("/z:tall")) {
        if (!v9x_stage_z_tall()) {
            v9x_finish("ZTALL-FAILED", 14u);
        }
    }
    if (v9x_switch_given("/3d") && v9x_switch_given("/z:depth24")) {
        if (!v9x_stage_z_depth("ZDepth24S8", V9X_NV50_VM_Z24S8, 4ul,
                               V9X_NV50_3D_ZETA_Z24S8)) {
            v9x_finish("ZDEPTH24S8-FAILED", 14u);
        }
    }
    if (v9x_switch_given("/3d") && v9x_switch_given("/3d:texture")) {
        int ok32 = v9x_stage_3d_texture("Texture32", 0ul, 4ul,
                                        V9X_NV50_3D_RT_BGRX8);
        int ok16 = v9x_stage_3d_texture("Texture16", 1ul, 2ul,
                                        V9X_NV50_3D_RT_B5G6R5);

        v9x_pusher_state();
        if (!ok32) {
            v9x_finish("TEXTURE32-FAILED", 11u);
        }
        if (!ok16) {
            v9x_finish("TEXTURE16-FAILED", 11u);
        }
    }
    if (v9x_switch_given("/3d") && v9x_switch_given("/3d:d3d")) {
        int ok = v9x_stage_3d_d3d();

        v9x_pusher_state();
        if (!ok) {
            v9x_finish("D3D-FAILED", 15u);
        }
    }
    /* /3d:ps11 last: it moves FP_ADDRESS and the TSC limit off the
     * setup's values. 16 bpp only if 32 bpp ran clean, since a trap
     * stays latched. */
    if (v9x_switch_given("/3d") && v9x_switch_given("/3d:ps11")) {
        int ok32 = v9x_stage_3d_ps11("Ps11Bpp32", 4ul, V9X_NV50_3D_RT_BGRX8);
        int ok16 = 0;

        if (!v9x_ps11_probe_trapped) {
            ok16 = v9x_stage_3d_ps11("Ps11Bpp16", 2ul, V9X_NV50_3D_RT_B5G6R5);
        }
        v9x_pusher_state();
        if (!ok32) {
            v9x_finish("PS11-32-FAILED", 16u);
        }
        if (!ok16) {
            v9x_finish("PS11-16-FAILED", 16u);
        }
    }
    /* /3d:guard: a measurement, so a band that fails somewhere is a
     * result, not a stop; only a trap ends the run early. */
    if (v9x_switch_given("/3d") && v9x_switch_given("/3d:guard") &&
        !v9x_ps11_probe_trapped) {
        (void)v9x_stage_3d_guard();
        v9x_pusher_state();
    }
    v9x_finish("COMPLETE", 0u);
}
