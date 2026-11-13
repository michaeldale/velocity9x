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
typedef char v9x_n5ch_region_fits[
    V9X_NV50_REGION_WORDS <= V9X_N5CH_DATA ? 1 : -1];

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

    v9x_ops_begin();
    v9x_vram_data(0ul, v9x_region, V9X_NV50_REGION_WORDS);
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

static void v9x_stage_gr_init(void);

static int v9x_stage_fifo(void)
{
    struct v9x_status_ops s;
    DWORD index;
    DWORD runlist_wait;
    DWORD sema_wait;
    DWORD sema;
    DWORD trap_enable;
    DWORD trap_stale;
    DWORD empty_wait;
    DWORD trap;
    DWORD trap_word[V9X_NV50_VM_TRAP_WORDS];
    DWORD aer[V9X_AER_WORDS];
    DWORD timeline[V9X_TIMELINE_SAMPLES][5];
    char trap_key[8];

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
    /* nv50_fifo_init commits a runlist before CACHE1 is enabled: with no
     * channel yet, an empty one (nv50_runl_commit, count 0), then waits
     * for the update (32ECh bit 8). Runs 1-19 committed only at the bind. */
    v9x_write(V9X_NV50_PFIFO_RUNLIST_ADDR, v9x_layout.runlist_reg);
    v9x_write(V9X_NV50_PFIFO_RUNLIST_COUNT, 0ul);
    empty_wait = v9x_wait(V9X_NV50_PFIFO_RUNLIST_COUNT,
                          V9X_NV50_PFIFO_RUNLIST_BUSY, 0ul);
    v9x_write(V9X_PFIFO_CACHE1_PUSH, 1ul);
    v9x_write(V9X_PFIFO_CACHE1_PULL, 1ul);
    v9x_write(V9X_NV50_PFIFO_CACHES, 1ul);
    /* nv50_fb_init's trap enable. Runs 1-16 never wrote it, so their
     * 100C90h-9Ch readings showed no fault whether there was one or not. */
    /* First nv50_fb_intr's acknowledge (enable | bit 31), so a trap left
     * latched by an earlier run in the same boot (run 22 left one) is not
     * read as this run's. */
    trap_stale = v9x_read(V9X_NV50_VM_TRAP);
    v9x_write(V9X_NV50_VM_TRAP,
              V9X_NV50_VM_TRAP_ENABLE | V9X_NV50_VM_TRAP_PENDING);
    v9x_write(V9X_NV50_VM_TRAP, V9X_NV50_VM_TRAP_ENABLE);
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

    if (v9x_timed_out(runlist_wait) ||
        v9x_value(sema) != V9X_SEMA_VALUE) {
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
    DWORD index;
    DWORD enable_now;
    DWORD read_enable;

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
    v9x_status_request(&s, V9X_STATUS_ALL);
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
    v9x_status_request(&s, V9X_STATUS_ALL);
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
    v9x_stage_region();
    v9x_vram_check("region");
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
