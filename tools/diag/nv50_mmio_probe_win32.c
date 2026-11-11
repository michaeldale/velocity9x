/*
 * V9XN5MM.EXE: NVIDIA GT218 (GeForce 210) read-only register snapshot.
 *
 * Phase 2 of docs\plans\nvidia-gt218.md. Finds PCI 10DE:0A65 by a PCI scan
 * (configuration mechanism #1, CF8h restored after each read), dumps its
 * configuration space, loads N5MM.VXD and asks it for register offsets
 * through BAR0, each read twice. Writes C:\V9XDIAG\V9XN5MM.INI:
 *
 *   [Report]  build, the device found, the VxD's status, what was taken;
 *   [Config]  configuration space, C<offset>=dword, all 256 bytes;
 *   [Mmio]    every register read, as R<offset>=first[,second if it moved],
 *             with Name.<offset> from Linux nouveau;
 *   [Pramin]  PRAMIN window samples, P<window offset>, non-zero words only;
 *   [Decode]  chipset, memory size, type and partitions, the PRAMIN window
 *             and BAR VM state, the engines PMC_ENABLE has on, head 0's
 *             scanout state.
 *
 * Captures in order: the always-present units (PMC, PBUS, PTIMER, PFB,
 * PSTRAPS) and PDISP; then PFIFO, PGRAPH and the copy engine, each only if
 * PMC_ENABLE has it on (the NV3 probe's rule); then the PRAMIN window: its
 * first KiB, the KiB at the BAR VM instance if the window covers it, and a
 * coarse sample of the whole window.
 *
 * No register or memory is ever written. Every offset and field is from
 * nouveau (nvkm, Linux master, kept in build\external\nv50\nouveau), not
 * measured; the record of the first run says which held.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_N5MM_PATH        V9X_DIAG_PATH("V9XN5MM.INI")
#define V9X_N5MM_VXD         "\\\\.\\N5MM.VXD"
#define V9X_N5MM_MAGIC       0x4d4d354eul
#define V9X_N5MM_CAPTURE     1u
#define V9X_N5MM_OFFSET_MAX  256u
#define V9X_N5MM_HEADER      16u
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_PCI_ENABLE       0x80000000ul
#define V9X_PCI_BUSES        8ul
#define V9X_PCI_DEVICES      32ul
#define V9X_PCI_CONFIG_BYTES 256ul
#define V9X_GT218_ID         0x0a6510deul

#define V9X_ST_GT218           0x00000001ul
#define V9X_ST_BASE_INVALID    0x00000002ul
#define V9X_ST_MMIO_MAPPED     0x00000004ul
#define V9X_ST_IDENTITY_MATCH  0x00000008ul
#define V9X_ST_OFFSETS_REFUSED 0x00000010ul
#define V9X_ST_SNAPSHOT_TAKEN  0x00000020ul

/* Registers the decode reads back. */
#define V9X_NV50_PMC_BOOT_0       0x000000ul
#define V9X_NV50_PMC_ENABLE       0x000200ul
#define V9X_NV50_PBUS_UNITS       0x001540ul
#define V9X_NV50_PRAMIN_WINDOW    0x001700ul
#define V9X_NV50_BAR_VM           0x001704ul
#define V9X_NV50_BAR1_OBJECT      0x001708ul
#define V9X_NV50_BAR3_OBJECT      0x00170cul
#define V9X_NV50_PFB_CONFIG_0     0x100200ul
#define V9X_NV50_PFB_MEM_SIZE     0x10020cul
#define V9X_NV50_PFB_MEM_TYPE     0x100714ul
#define V9X_NV50_PSTRAPS          0x101000ul
#define V9X_NV50_PRAMIN_START     0x700000ul
#define V9X_NV50_PRAMIN_BYTES     0x100000ul

/*
 * Field layouts, all nouveau's. PMC_BOOT_0 chipset 28:20, stepping 7:0.
 * PMC_ENABLE: PFIFO 00000100h, PGRAPH 00201000h, the copy engine
 * 00802000h (subdev\mc\gt215.c, gt215_mc_reset). Memory size: 10020Ch, low
 * byte the bits above 32, the rest the low word (ramnv50.c, nv50_ram_ctor).
 * Memory type: 100714h bits 2:0 (0 DDR1, 1 DDR2 or DDR3 by the BIOS, 2
 * GDDR3, 3 GDDR4, 4 GDDR5). Partitions: 1540h bits 23:16, one bit each.
 * Ranks: 100200h bit 2. PRAMIN window: 1700h holds the video memory
 * address >> 16 (instmem\nv50.c). BAR VM: 1704h holds the instance address
 * >> 12, bit 30 set once valid; 1708h (BAR1) and 170Ch (BAR3) hold bit 31
 * valid and an object offset >> 4 (bar\nv50.c).
 */
#define V9X_NV50_CHIPSET_SHIFT    20u
#define V9X_NV50_CHIPSET_MASK     0x1fful
#define V9X_NV50_STEPPING_MASK    0xfful
#define V9X_NV50_ENABLE_PFIFO     0x00000100ul
#define V9X_NV50_ENABLE_PGRAPH    0x00201000ul
#define V9X_NV50_ENABLE_CE        0x00802000ul
#define V9X_NV50_SIZE_HIGH_MASK   0x000000fful
#define V9X_NV50_SIZE_LOW_MASK    0xffffff00ul
#define V9X_NV50_TYPE_MASK        0x7ul
#define V9X_NV50_PARTS_SHIFT      16u
#define V9X_NV50_PARTS_MASK       0xfful
#define V9X_NV50_RANKS_TWO        0x00000004ul
#define V9X_NV50_WINDOW_SHIFT     16u
#define V9X_NV50_VM_VALID         0x40000000ul
#define V9X_NV50_VM_ADDR_MASK     0x0ffffffful
#define V9X_NV50_VM_SHIFT         12u
#define V9X_NV50_OBJECT_VALID     0x80000000ul
#define V9X_NV50_OBJECT_MASK      0x7ffffffful
#define V9X_NV50_OBJECT_SHIFT     4u

/* PRAMIN samples: a dense KiB, and one word every 4 KiB of the window. */
#define V9X_PRAMIN_DENSE_BYTES    0x400ul
#define V9X_PRAMIN_COARSE_STEP    0x1000ul

/*
 * PDISP's core channel state, as nouveau's method dump reads it
 * (engine\disp\nv50.c, nv50_disp_core_mthd_head): per head method, the
 * register holding it, head 1 at + 540h, and the word 4 bytes on, which the
 * dump reads beside it (the channel's prev = 4).
 */
#define V9X_NV50_HEAD_STRIDE      0x540ul
#define V9X_NV50_HEAD_PREV        0x4ul
#define V9X_NV50_HEAD_OFFSET_REG  0x610a84ul /* method 0860h */

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_n5mm_register {
    DWORD offset;
    const char *name;
};

/* First capture: units that are always present. */
static const struct v9x_n5mm_register v9x_global[] = {
    { 0x000000ul, "PMC_BOOT_0" },
    { 0x000004ul, "PMC_BOOT_1" },
    { 0x000100ul, "PMC_INTR_0" },
    { 0x000140ul, "PMC_INTR_EN_0" },
    { 0x000200ul, "PMC_ENABLE" },
    { 0x001540ul, "PBUS_UNITS" },
    { 0x001700ul, "PBUS_PRAMIN_WINDOW" },
    { 0x001704ul, "PBUS_BAR_VM" },
    { 0x001708ul, "PBUS_BAR1_OBJECT" },
    { 0x00170cul, "PBUS_BAR3_OBJECT" },
    { 0x001850ul, "PBUS_ROM_ACCESS" },
    { 0x009400ul, "PTIMER_TIME_0" },
    { 0x009404ul, "PTIMER_TIME_1" },
    { 0x100200ul, "PFB_CONFIG_0" },
    { 0x100204ul, "PFB_CONFIG_1" },
    { 0x100208ul, "PFB_208" },
    { 0x10020cul, "PFB_MEM_SIZE" },
    { 0x100714ul, "PFB_MEM_TYPE" },
    { 0x101000ul, "PSTRAPS" },
    { 0x610020ul, "PDISP_INTR_0" },
    { 0x610024ul, "PDISP_INTR_1" },
    { 0x610028ul, "PDISP_INTR_EN" },
    { 0x610030ul, "PDISP_SUPERVISOR" },
    { 0x610184ul, "PDISP_CAPS" },
    { 0x610200ul, "PDISP_CORE_CTRL" },
    { 0x610bb8ul, "PDISP_CORE_M0084" },
    { 0x610b9cul, "PDISP_CORE_M0088" },
    { 0x610b58ul, "PDISP_DAC0_M0400" },
    { 0x610b70ul, "PDISP_SOR0_M0600" },
    { 0x610b78ul, "PDISP_SOR1_M0600" }
};

/* The head method table, method and register for head 0. */
static const DWORD v9x_head[][2] = {
    { 0x0800ul, 0x610ad8ul }, { 0x0804ul, 0x610ad0ul },
    { 0x0808ul, 0x610a48ul }, { 0x080cul, 0x610a78ul },
    { 0x0810ul, 0x610ac0ul }, { 0x0814ul, 0x610af8ul },
    { 0x0818ul, 0x610b00ul }, { 0x081cul, 0x610ae8ul },
    { 0x0820ul, 0x610af0ul }, { 0x0824ul, 0x610b08ul },
    { 0x0828ul, 0x610b10ul }, { 0x082cul, 0x610a68ul },
    { 0x0830ul, 0x610a60ul }, { 0x0838ul, 0x610a40ul },
    { 0x0840ul, 0x610a24ul }, { 0x0844ul, 0x610a2cul },
    { 0x0848ul, 0x610aa8ul }, { 0x084cul, 0x610ab0ul },
    { 0x0860ul, 0x610a84ul }, { 0x0864ul, 0x610a90ul },
    { 0x0868ul, 0x610b18ul }, { 0x086cul, 0x610b20ul },
    { 0x0870ul, 0x610ac8ul }, { 0x0874ul, 0x610a38ul },
    { 0x0880ul, 0x610a58ul }, { 0x0884ul, 0x610a9cul },
    { 0x08a0ul, 0x610a70ul }, { 0x08a4ul, 0x610a50ul },
    { 0x08a8ul, 0x610ae0ul }, { 0x08c0ul, 0x610b28ul },
    { 0x08c4ul, 0x610b30ul }, { 0x08c8ul, 0x610b40ul },
    { 0x08d4ul, 0x610b38ul }, { 0x08d8ul, 0x610b48ul },
    { 0x08dcul, 0x610b50ul }, { 0x0900ul, 0x610a18ul },
    { 0x0904ul, 0x610ab8ul }
};

static const struct v9x_n5mm_register v9x_pfifo[] = {
    { 0x002100ul, "PFIFO_INTR" },
    { 0x002140ul, "PFIFO_INTR_EN" },
    { 0x002500ul, "PFIFO_CACHES" },
    { 0x00250cul, "PFIFO_250C" },
    { 0x002600ul, "PFIFO_CHAN_0" },
    { 0x002604ul, "PFIFO_CHAN_1" },
    { 0x002608ul, "PFIFO_CHAN_2" },
    { 0x00260cul, "PFIFO_CHAN_3" },
    { 0x002610ul, "PFIFO_CHAN_4" },
    { 0x002614ul, "PFIFO_CHAN_5" },
    { 0x002618ul, "PFIFO_CHAN_6" },
    { 0x00261cul, "PFIFO_CHAN_7" },
    { 0x003200ul, "PFIFO_CACHE1_PUSH" },
    { 0x003250ul, "PFIFO_CACHE1_PULL" },
    { 0x0032ecul, "PFIFO_32EC" },
    { 0x0032f0ul, "PFIFO_32F0" },
    { 0x0032f4ul, "PFIFO_32F4" },
    { 0x0032f8ul, "PFIFO_32F8" },
    { 0x0032fcul, "PFIFO_32FC" }
};

static const struct v9x_n5mm_register v9x_pgraph[] = {
    { 0x400100ul, "PGRAPH_INTR" },
    { 0x400108ul, "PGRAPH_TRAP" },
    { 0x40032cul, "PGRAPH_CTX_INSTANCE" }
};

static const struct v9x_n5mm_register v9x_copy[] = {
    { 0x104040ul, "PCOPY_FALCON_SSTA" },
    { 0x104100ul, "PCOPY_FALCON_CPUCTL" },
    { 0x104104ul, "PCOPY_FALCON_BOOTVEC" },
    { 0x104108ul, "PCOPY_FALCON_HWCFG" },
    { 0x10410cul, "PCOPY_FALCON_10C" }
};

struct v9x_n5mm_request {
    DWORD config;
    DWORD count;
    DWORD offsets[V9X_N5MM_OFFSET_MAX];
};

struct v9x_n5mm_result {
    DWORD magic;
    DWORD status;
    DWORD id;
    DWORD bar0;
    DWORD accepted;
    DWORD boot0;
    DWORD cf8;
    DWORD refused_offset;
    DWORD config;
    DWORD reserved_9;
    DWORD class_revision;
    DWORD reserved[5];
    DWORD offsets[V9X_N5MM_OFFSET_MAX];
    DWORD first[V9X_N5MM_OFFSET_MAX];
    DWORD second[V9X_N5MM_OFFSET_MAX];
};

/* The header must be the VxD's N5_HEADER_DWORDS long. */
typedef char v9x_n5mm_header_matches[
    offsetof(struct v9x_n5mm_result, offsets) ==
    sizeof(DWORD) * V9X_N5MM_HEADER ? 1 : -1];

struct v9x_n5mm_value {
    DWORD offset;
    DWORD value;
};

static struct v9x_n5mm_request v9x_request;
static struct v9x_n5mm_result v9x_result;
static struct v9x_n5mm_value v9x_values[V9X_N5MM_OFFSET_MAX * 2u];
static DWORD v9x_value_count;

static DWORD v9x_config_read(DWORD device, DWORD offset)
{
    DWORD saved;
    DWORD value;

    saved = v9x_inpd(V9X_PCI_ADDRESS);
    v9x_outpd(V9X_PCI_ADDRESS, device | (offset & 0xfcul));
    value = v9x_inpd(V9X_PCI_DATA);
    v9x_outpd(V9X_PCI_ADDRESS, saved);
    return value;
}

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

static void v9x_key(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_hex(text, value, 8);
    WritePrivateProfileStringA(section, key, text, V9X_N5MM_PATH);
}

static void v9x_number(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_decimal(text, value);
    WritePrivateProfileStringA(section, key, text, V9X_N5MM_PATH);
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_N5MM_PATH);
}

static void v9x_offset_key(char *key, const char *prefix, DWORD offset,
                           int digits)
{
    lstrcpyA(key, prefix);
    v9x_hex(key + lstrlenA(prefix), offset, digits);
}

static void v9x_request_add(DWORD offset)
{
    if (v9x_request.count < V9X_N5MM_OFFSET_MAX) {
        v9x_request.offsets[v9x_request.count++] = offset;
    }
}

static void v9x_name(DWORD offset, const char *name)
{
    char key[24];

    v9x_offset_key(key, "Name.", offset, 6);
    v9x_text("Mmio", key, name);
}

static void v9x_request_list(const struct v9x_n5mm_register *list,
                             DWORD count)
{
    DWORD index;

    v9x_request.count = 0ul;
    for (index = 0ul; index < count; ++index) {
        v9x_request_add(list[index].offset);
        v9x_name(list[index].offset, list[index].name);
    }
}

/* Both heads' method registers and the word beside each. */
static void v9x_request_heads(void)
{
    char name[24];
    DWORD head;
    DWORD index;
    DWORD offset;

    v9x_request.count = 0ul;
    for (head = 0ul; head < 2ul; ++head) {
        for (index = 0ul; index < sizeof(v9x_head) / sizeof(v9x_head[0]);
             ++index) {
            offset = v9x_head[index][1] + head * V9X_NV50_HEAD_STRIDE;
            lstrcpyA(name, head == 0ul ? "HEAD0_M" : "HEAD1_M");
            v9x_hex(name + lstrlenA(name), v9x_head[index][0], 4);
            v9x_request_add(offset);
            v9x_name(offset, name);
            lstrcatA(name, "_PREV");
            v9x_request_add(offset + V9X_NV50_HEAD_PREV);
            v9x_name(offset + V9X_NV50_HEAD_PREV, name);
        }
    }
}

/* PRAMIN window offsets from start, bytes long, every step bytes. */
static void v9x_request_pramin(DWORD start, DWORD bytes, DWORD step)
{
    DWORD at;

    v9x_request.count = 0ul;
    for (at = 0ul; at < bytes && start + at < V9X_NV50_PRAMIN_BYTES;
         at += step) {
        v9x_request_add(V9X_NV50_PRAMIN_START + start + at);
    }
}

/* One DIOC call of v9x_request. Returns nonzero on a snapshot. */
static int v9x_capture(void)
{
    HANDLE device;
    DWORD returned;
    BOOL ok;

    device = CreateFileA(V9X_N5MM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        return 0;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_N5MM_CAPTURE, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_N5MM_MAGIC) {
        v9x_text("Report", "Result", "DIOC-FAILED");
        v9x_key("Report", "Returned", returned);
        return 0;
    }
    return (v9x_result.status & V9X_ST_SNAPSHOT_TAKEN) != 0ul;
}

static void v9x_remember(DWORD offset, DWORD value)
{
    if (v9x_value_count < sizeof(v9x_values) / sizeof(v9x_values[0])) {
        v9x_values[v9x_value_count].offset = offset;
        v9x_values[v9x_value_count].value = value;
        ++v9x_value_count;
    }
}

static DWORD v9x_value(DWORD offset)
{
    DWORD index;

    for (index = 0ul; index < v9x_value_count; ++index) {
        if (v9x_values[index].offset == offset) {
            return v9x_values[index].value;
        }
    }
    return 0ul;
}

static void v9x_pair(char *text, DWORD index)
{
    v9x_hex(text, v9x_result.first[index], 8);
    if (v9x_result.second[index] != v9x_result.first[index]) {
        text[8] = ',';
        v9x_hex(text + 9, v9x_result.second[index], 8);
    }
}

static void v9x_publish_mmio(void)
{
    char key[24];
    char text[24];
    DWORD index;

    for (index = 0ul; index < v9x_result.accepted; ++index) {
        v9x_offset_key(key, "R", v9x_result.offsets[index], 6);
        v9x_pair(text, index);
        v9x_text("Mmio", key, text);
        v9x_remember(v9x_result.offsets[index], v9x_result.first[index]);
    }
}

/* Non-zero words only, keyed by their offset in the window. */
static void v9x_publish_pramin(void)
{
    char key[24];
    char text[24];
    DWORD index;

    for (index = 0ul; index < v9x_result.accepted; ++index) {
        if (v9x_result.first[index] == 0ul &&
            v9x_result.second[index] == 0ul) {
            continue;
        }
        v9x_offset_key(key, "P",
                       v9x_result.offsets[index] - V9X_NV50_PRAMIN_START, 5);
        v9x_pair(text, index);
        v9x_text("Pramin", key, text);
    }
}

/* The first GT218 on buses 0-7, function 0; 0 if none. */
static DWORD v9x_find_gt218(void)
{
    DWORD bus;
    DWORD slot;
    DWORD config;

    for (bus = 0ul; bus < V9X_PCI_BUSES; ++bus) {
        for (slot = 0ul; slot < V9X_PCI_DEVICES; ++slot) {
            config = V9X_PCI_ENABLE | (bus << 16) | (slot << 11);
            if (v9x_config_read(config, 0ul) == V9X_GT218_ID) {
                return config;
            }
        }
    }
    return 0ul;
}

static void v9x_publish_config(DWORD config)
{
    char key[8];
    DWORD offset;

    for (offset = 0ul; offset < V9X_PCI_CONFIG_BYTES; offset += 4ul) {
        v9x_offset_key(key, "C", offset, 2);
        v9x_key("Config", key, v9x_config_read(config, offset));
    }
}

static DWORD v9x_bits_set(DWORD value)
{
    DWORD count = 0ul;

    while (value != 0ul) {
        count += value & 1ul;
        value >>= 1;
    }
    return count;
}

static void v9x_decode_global(void)
{
    static const char *const types[] = {
        "ddr1", "ddr2-or-ddr3", "gddr3", "gddr4", "gddr5",
        "unknown-5", "unknown-6", "unknown-7"
    };
    DWORD value;

    value = v9x_value(V9X_NV50_PMC_BOOT_0);
    v9x_key("Decode", "Chipset",
            (value >> V9X_NV50_CHIPSET_SHIFT) & V9X_NV50_CHIPSET_MASK);
    v9x_key("Decode", "Stepping", value & V9X_NV50_STEPPING_MASK);

    value = v9x_value(V9X_NV50_PMC_ENABLE);
    v9x_text("Decode", "PfifoEnabled",
             (value & V9X_NV50_ENABLE_PFIFO) == V9X_NV50_ENABLE_PFIFO ?
             "yes" : "no");
    v9x_text("Decode", "PgraphEnabled",
             (value & V9X_NV50_ENABLE_PGRAPH) == V9X_NV50_ENABLE_PGRAPH ?
             "yes" : "no");
    v9x_text("Decode", "CopyEnabled",
             (value & V9X_NV50_ENABLE_CE) == V9X_NV50_ENABLE_CE ?
             "yes" : "no");

    /* Memory size: bits 39:32 in the low byte. Bytes as two keys, and
     * MiB, which is what a 32-bit driver can use. */
    value = v9x_value(V9X_NV50_PFB_MEM_SIZE);
    v9x_key("Decode", "MemoryBytesHigh", value & V9X_NV50_SIZE_HIGH_MASK);
    v9x_key("Decode", "MemoryBytesLow", value & V9X_NV50_SIZE_LOW_MASK);
    v9x_number("Decode", "MemoryMiB",
               ((value & V9X_NV50_SIZE_HIGH_MASK) << 12) |
               ((value & V9X_NV50_SIZE_LOW_MASK) >> 20));
    v9x_text("Decode", "MemoryType",
             types[v9x_value(V9X_NV50_PFB_MEM_TYPE) & V9X_NV50_TYPE_MASK]);
    v9x_number("Decode", "MemoryPartitions",
               v9x_bits_set((v9x_value(V9X_NV50_PBUS_UNITS) >>
                             V9X_NV50_PARTS_SHIFT) & V9X_NV50_PARTS_MASK));
    v9x_number("Decode", "MemoryRanks",
               (v9x_value(V9X_NV50_PFB_CONFIG_0) & V9X_NV50_RANKS_TWO) != 0ul
               ? 2ul : 1ul);
    v9x_key("Decode", "Straps", v9x_value(V9X_NV50_PSTRAPS));

    v9x_key("Decode", "PraminWindowBase",
            v9x_value(V9X_NV50_PRAMIN_WINDOW) << V9X_NV50_WINDOW_SHIFT);
    value = v9x_value(V9X_NV50_BAR_VM);
    v9x_text("Decode", "BarVmValid",
             (value & V9X_NV50_VM_VALID) != 0ul ? "yes" : "no");
    v9x_key("Decode", "BarVmInstance",
            (value & V9X_NV50_VM_ADDR_MASK) << V9X_NV50_VM_SHIFT);
    value = v9x_value(V9X_NV50_BAR1_OBJECT);
    v9x_text("Decode", "Bar1ObjectValid",
             (value & V9X_NV50_OBJECT_VALID) != 0ul ? "yes" : "no");
    v9x_key("Decode", "Bar1ObjectOffset",
            (value & V9X_NV50_OBJECT_MASK) << V9X_NV50_OBJECT_SHIFT);
    value = v9x_value(V9X_NV50_BAR3_OBJECT);
    v9x_text("Decode", "Bar3ObjectValid",
             (value & V9X_NV50_OBJECT_VALID) != 0ul ? "yes" : "no");
    v9x_key("Decode", "Bar3ObjectOffset",
            (value & V9X_NV50_OBJECT_MASK) << V9X_NV50_OBJECT_SHIFT);
}

/* Each head's surface offset method, read as the address >> 8. That shift
 * is assumed, not taken from a fetched source; the raw words are in
 * [Mmio] for the record to check against VBE's framebuffer. */
static void v9x_decode_heads(void)
{
    v9x_key("Decode", "Head0SurfaceOffset",
            v9x_value(V9X_NV50_HEAD_OFFSET_REG) << 8);
    v9x_key("Decode", "Head1SurfaceOffset",
            v9x_value(V9X_NV50_HEAD_OFFSET_REG + V9X_NV50_HEAD_STRIDE) << 8);
}

static void v9x_finish(const char *result, UINT code)
{
    v9x_text("Report", "Result", result);
    WritePrivateProfileStringA(0, 0, 0, V9X_N5MM_PATH);
    ExitProcess(code);
}

static void v9x_optional(const struct v9x_n5mm_register *list, DWORD count,
                         DWORD enable, DWORD mask, const char *report_key,
                         const char *failure)
{
    if ((enable & mask) != mask) {
        v9x_text("Report", report_key, "skipped-disabled");
        return;
    }
    v9x_request_list(list, count);
    if (!v9x_capture()) {
        v9x_finish(failure, 3u);
    }
    v9x_publish_mmio();
    v9x_text("Report", report_key, "taken");
}

void __stdcall V9xNv50MmioEntry(void)
{
    DWORD config;
    DWORD enable;
    DWORD window;
    DWORD instance;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_N5MM_PATH);
    v9x_text("Report", "Tool", "V9XN5MM");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Access", "mmio-read-only");

    config = v9x_find_gt218();
    if (config == 0ul) {
        v9x_finish("NO-GT218", 1u);
    }
    v9x_key("Report", "Config", config);
    v9x_publish_config(config);

    v9x_request.config = config;
    v9x_request_list(v9x_global, sizeof(v9x_global) / sizeof(v9x_global[0]));
    if (!v9x_capture()) {
        v9x_key("Report", "Status", v9x_result.status);
        v9x_key("Report", "Boot0", v9x_result.boot0);
        v9x_key("Report", "RefusedOffset", v9x_result.refused_offset);
        v9x_finish("GLOBAL-CAPTURE-FAILED", 2u);
    }
    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "Boot0", v9x_result.boot0);
    v9x_publish_mmio();
    v9x_decode_global();

    v9x_request_heads();
    if (!v9x_capture()) {
        v9x_finish("HEAD-CAPTURE-FAILED", 3u);
    }
    v9x_publish_mmio();
    v9x_decode_heads();

    enable = v9x_value(V9X_NV50_PMC_ENABLE);
    v9x_optional(v9x_pfifo, sizeof(v9x_pfifo) / sizeof(v9x_pfifo[0]),
                 enable, V9X_NV50_ENABLE_PFIFO, "PfifoCapture",
                 "PFIFO-CAPTURE-FAILED");
    v9x_optional(v9x_pgraph, sizeof(v9x_pgraph) / sizeof(v9x_pgraph[0]),
                 enable, V9X_NV50_ENABLE_PGRAPH, "PgraphCapture",
                 "PGRAPH-CAPTURE-FAILED");
    v9x_optional(v9x_copy, sizeof(v9x_copy) / sizeof(v9x_copy[0]),
                 enable, V9X_NV50_ENABLE_CE, "CopyCapture",
                 "COPY-CAPTURE-FAILED");

    /* The window's first KiB. */
    v9x_request_pramin(0ul, V9X_PRAMIN_DENSE_BYTES, 4ul);
    if (!v9x_capture()) {
        v9x_finish("PRAMIN-CAPTURE-FAILED", 4u);
    }
    v9x_publish_pramin();

    /* The BAR VM instance's first KiB, when the window covers it. */
    window = v9x_value(V9X_NV50_PRAMIN_WINDOW) << V9X_NV50_WINDOW_SHIFT;
    instance = (v9x_value(V9X_NV50_BAR_VM) & V9X_NV50_VM_ADDR_MASK) <<
               V9X_NV50_VM_SHIFT;
    if (instance >= window && instance - window < V9X_NV50_PRAMIN_BYTES) {
        v9x_request_pramin(instance - window, V9X_PRAMIN_DENSE_BYTES, 4ul);
        if (!v9x_capture()) {
            v9x_finish("PRAMIN-CAPTURE-FAILED", 4u);
        }
        v9x_publish_pramin();
        v9x_text("Report", "InstanceCapture", "taken");
    } else {
        v9x_text("Report", "InstanceCapture", "outside-window");
    }

    /* One word every 4 KiB of the window. */
    v9x_request_pramin(0ul, V9X_NV50_PRAMIN_BYTES, V9X_PRAMIN_COARSE_STEP);
    if (!v9x_capture()) {
        v9x_finish("PRAMIN-CAPTURE-FAILED", 4u);
    }
    v9x_publish_pramin();

    v9x_finish("COMPLETE", 0u);
}
