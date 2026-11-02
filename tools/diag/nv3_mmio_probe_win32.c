/*
 * V9XN3MM.EXE: NVIDIA RIVA 128 (NV3) read-only register snapshot.
 *
 * Phase 2 of docs\plans\nvidia-riva128.md. Finds PCI 12D2:0018 by a PCI scan
 * (configuration mechanism #1, CF8h restored after each read), loads
 * N3MM.VXD, and asks it for register offsets through BAR0 and for samples
 * of two BAR1 windows, each read twice. Writes C:\V9XDIAG\V9XN3MM.INI:
 *
 *   [Report]  build, the device found, its BARs, the VxD's status;
 *   [Mmio]    every offset read, as R<offset>=first[,second if it moved],
 *             with Name.<offset> from Linux riva_hw.c and envytools' rnndb;
 *   [Ramin]   BAR1 samples, T<offset> for the top 32 KiB of an 8 MiB card
 *             and R<offset> for BAR1 + C00000h, every 100h bytes;
 *   [Decode]  chip and revision, which engines PMC_ENABLE has on, memory
 *             size, the PFB and PGRAPH surface setup, the FIFO tables and
 *             whether the engine is busy.
 *
 * Captures in order: the always-present units (PMC, PTIMER, PFB, PSTRAPS,
 * PRAMDAC); then PFIFO and the USER free count only if PMC_ENABLE has
 * PFIFO on, and PGRAPH only if it has PGRAPH on, because an engine whose
 * enable bit is off "disappears from MMIO space" (envytools pmc.rst) and
 * what a read of it does is not documented; then the BAR1 samples.
 *
 * Nothing on the device is written.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_N3MM_PATH        V9X_DIAG_PATH("V9XN3MM.INI")
#define V9X_N3MM_VXD         "\\\\.\\N3MM.VXD"
#define V9X_N3MM_MAGIC       0x4d4d334eul
#define V9X_N3MM_CAPTURE     1u
#define V9X_N3MM_OFFSET_MAX  256u
#define V9X_N3MM_HEADER      16u
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_PCI_ENABLE       0x80000000ul
#define V9X_PCI_BUSES        8ul
#define V9X_PCI_DEVICES      32ul
#define V9X_RIVA128_ID       0x001812d2ul

#define V9X_ST_NV3             0x00000001ul
#define V9X_ST_BASE_INVALID    0x00000002ul
#define V9X_ST_MMIO_MAPPED     0x00000004ul
#define V9X_ST_IDENTITY_MATCH  0x00000008ul
#define V9X_ST_OFFSETS_REFUSED 0x00000010ul
#define V9X_ST_SNAPSHOT_TAKEN  0x00000020ul
#define V9X_ST_FB_MAPPED       0x00000040ul
#define V9X_ST_NOT_RIVA128     0x00000080ul
#define V9X_ST_ADDRESS_INVALID 0x00000100ul

/* N3MM.VXD's BAR1 encoding: bit 31 set, then the BAR1 offset. */
#define V9X_N3_BAR1            0x80000000ul
#define V9X_N3_TOP_START       0x007f8000ul
#define V9X_N3_RAMIN_START     0x00c00000ul
#define V9X_N3_WINDOW_BYTES    0x00008000ul
#define V9X_N3_SAMPLE_STEP     0x00000100ul

/* Register offsets the decode reads back. */
#define V9X_NV3_PMC_BOOT_0        0x000000ul
#define V9X_NV3_PMC_ENABLE        0x000200ul
#define V9X_NV3_PFIFO_RAMHT       0x002210ul
#define V9X_NV3_PFIFO_RAMFC       0x002214ul
#define V9X_NV3_PFIFO_RAMRO       0x002218ul
#define V9X_NV3_PFIFO_CACHES      0x002500ul
#define V9X_NV3_CACHE1_PUSH0      0x003200ul
#define V9X_NV3_CACHE1_PULL0      0x003240ul
#define V9X_NV3_PFB_BOOT_0        0x100000ul
#define V9X_NV3_PFB_CONFIG_0      0x100200ul
#define V9X_NV3_PSTRAPS           0x101000ul
#define V9X_NV3_PGRAPH_SURF_CFG   0x4006a8ul
#define V9X_NV3_PGRAPH_STATUS     0x4006b0ul
#define V9X_NV3_USER_FREE         0x800010ul

/* Field layouts. PMC_BOOT_0: revision 7:0, chip 19:16 (pmc.rst). PMC_ENABLE:
 * PFIFO bit 8, PGRAPH bit 12 (pmc.rst, NV3:NV4). PFB_BOOT_0 per Linux
 * riva_hw.c nv3GetConfig: bit 5 with revision >= 22h selects the 128ZX
 * decode of bits 1:0 (2 = 4 MiB, 1 = 2 MiB, else 8 MiB). PFB_CONFIG_0 per
 * riva_hw.c CalcStateExt: width / 32 in 5:0, bytes per pixel (max 3) in
 * 9:8, tiling disable bit 12. RAMHT: base 15:12, size 17:16 (0x1000 << n);
 * RAMFC base 15:9; RAMRO base 15:9, size bit 16 (nv1-pfifo.rst). PGRAPH
 * STATUS bit 0 busy (riva_hw.c nv3Busy). USER free count 15:0 in bytes
 * (riva_hw.h RIVA_FIFO_FREE). */
#define V9X_NV3_BOOT_REV_MASK     0x000000fful
#define V9X_NV3_BOOT_CHIP_SHIFT   16u
#define V9X_NV3_BOOT_CHIP_MASK    0xful
#define V9X_NV3_ENABLE_PFIFO      0x00000100ul
#define V9X_NV3_ENABLE_PGRAPH     0x00001000ul
#define V9X_NV3_PFB_ZX_DECODE     0x00000020ul
#define V9X_NV3_PFB_SIZE_MASK     0x3ul
#define V9X_NV3_ZX_REVISION       0x22ul
#define V9X_NV3_CFG_WIDTH_MASK    0x3ful
#define V9X_NV3_CFG_WIDTH_UNIT    32ul
#define V9X_NV3_CFG_DEPTH_SHIFT   8u
#define V9X_NV3_CFG_DEPTH_MASK    0x3ul
#define V9X_NV3_CFG_TILING_OFF    0x00001000ul
#define V9X_NV3_RAMHT_BASE_MASK   0x0000f000ul
#define V9X_NV3_RAMHT_SIZE_SHIFT  16u
#define V9X_NV3_RAMFC_BASE_MASK   0x0000fe00ul
#define V9X_NV3_RAMRO_SIZE_BIT    0x00010000ul
#define V9X_NV3_STATUS_BUSY       0x00000001ul
#define V9X_NV3_FREE_MASK         0x0000fffful

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_n3mm_register {
    DWORD offset;
    const char *name;
};

/* First capture: units that are always present. */
static const struct v9x_n3mm_register v9x_global[] = {
    { 0x000000ul, "PMC_BOOT_0" },
    { 0x000100ul, "PMC_INTR_0" },
    { 0x000140ul, "PMC_INTR_EN_0" },
    { 0x000200ul, "PMC_ENABLE" },
    { 0x009100ul, "PTIMER_INTR_0" },
    { 0x009140ul, "PTIMER_INTR_EN_0" },
    { 0x009200ul, "PTIMER_NUMERATOR" },
    { 0x009210ul, "PTIMER_DENOMINATOR" },
    { 0x009400ul, "PTIMER_TIME_0" },
    { 0x009404ul, "PTIMER_TIME_1" },
    { 0x100000ul, "PFB_BOOT_0" },
    { 0x100080ul, "PFB_DEBUG_0" },
    { 0x100200ul, "PFB_CONFIG_0" },
    { 0x100204ul, "PFB_CONFIG_1" },
    { 0x101000ul, "PSTRAPS" },
    { 0x680500ul, "PRAMDAC_NVPLL" },
    { 0x680504ul, "PRAMDAC_MPLL" },
    { 0x680508ul, "PRAMDAC_VPLL" },
    { 0x68050cul, "PRAMDAC_PLL_SELECT" },
    { 0x680600ul, "PRAMDAC_GENERAL_CONTROL" }
};

/* Second capture: PFIFO and the channel 0 free count. */
static const struct v9x_n3mm_register v9x_pfifo[] = {
    { 0x002080ul, "PFIFO_DEBUG_0" },
    { 0x002100ul, "PFIFO_INTR_0" },
    { 0x002140ul, "PFIFO_INTR_EN_0" },
    { 0x002200ul, "PFIFO_CONFIG_0" },
    { 0x002210ul, "PFIFO_RAMHT" },
    { 0x002214ul, "PFIFO_RAMFC" },
    { 0x002218ul, "PFIFO_RAMRO" },
    { 0x002400ul, "PFIFO_RUNOUT_STATUS" },
    { 0x002410ul, "PFIFO_RUNOUT_PUT" },
    { 0x002420ul, "PFIFO_RUNOUT_GET" },
    { 0x002500ul, "PFIFO_CACHES" },
    { 0x003000ul, "PFIFO_CACHE0_PUSH0" },
    { 0x003200ul, "PFIFO_CACHE1_PUSH0" },
    { 0x003204ul, "PFIFO_CACHE1_PUSH1" },
    { 0x003210ul, "PFIFO_CACHE1_PUT" },
    { 0x003240ul, "PFIFO_CACHE1_PULL0" },
    { 0x003244ul, "PFIFO_CACHE1_PULL1" },
    { 0x003250ul, "PFIFO_CACHE1_PULL_STATE" },
    { 0x003270ul, "PFIFO_CACHE1_GET" },
    { 0x003280ul, "PFIFO_CACHE1_CTX_0" },
    { 0x003284ul, "PFIFO_CACHE1_CTX_1" },
    { 0x003288ul, "PFIFO_CACHE1_CTX_2" },
    { 0x00328cul, "PFIFO_CACHE1_CTX_3" },
    { 0x003290ul, "PFIFO_CACHE1_CTX_4" },
    { 0x003294ul, "PFIFO_CACHE1_CTX_5" },
    { 0x003298ul, "PFIFO_CACHE1_CTX_6" },
    { 0x00329cul, "PFIFO_CACHE1_CTX_7" },
    { 0x800010ul, "USER_CH0_FREE" }
};

/* Third capture: PGRAPH. */
static const struct v9x_n3mm_register v9x_pgraph[] = {
    { 0x400080ul, "PGRAPH_DEBUG_0" },
    { 0x400084ul, "PGRAPH_DEBUG_1" },
    { 0x400088ul, "PGRAPH_DEBUG_2" },
    { 0x40008cul, "PGRAPH_DEBUG_3" },
    { 0x400100ul, "PGRAPH_INTR_0" },
    { 0x400104ul, "PGRAPH_INTR_1" },
    { 0x400140ul, "PGRAPH_INTR_EN_0" },
    { 0x400144ul, "PGRAPH_INTR_EN_1" },
    { 0x400180ul, "PGRAPH_CTX_SWITCH" },
    { 0x400184ul, "PGRAPH_CTX_184" },
    { 0x400188ul, "PGRAPH_CTX_188" },
    { 0x40018cul, "PGRAPH_CTX_18C" },
    { 0x400190ul, "PGRAPH_CTX_CONTROL" },
    { 0x400194ul, "PGRAPH_CTX_USER" },
    { 0x400198ul, "PGRAPH_CTX_198" },
    { 0x40019cul, "PGRAPH_CTX_19C" },
    { 0x400550ul, "PGRAPH_CANVAS_MIN" },
    { 0x400554ul, "PGRAPH_CANVAS_MAX" },
    { 0x400558ul, "PGRAPH_CANVAS_558" },
    { 0x40055cul, "PGRAPH_CANVAS_55C" },
    { 0x400630ul, "PGRAPH_SURF_OFFSET_0" },
    { 0x400634ul, "PGRAPH_SURF_OFFSET_1" },
    { 0x400638ul, "PGRAPH_SURF_OFFSET_2" },
    { 0x40063cul, "PGRAPH_SURF_OFFSET_3" },
    { 0x400650ul, "PGRAPH_SURF_PITCH_0" },
    { 0x400654ul, "PGRAPH_SURF_PITCH_1" },
    { 0x400658ul, "PGRAPH_SURF_PITCH_2" },
    { 0x40065cul, "PGRAPH_SURF_PITCH_3" },
    { 0x4006a0ul, "PGRAPH_6A0" },
    { 0x4006a4ul, "PGRAPH_FIFO_ENABLE" },
    { 0x4006a8ul, "PGRAPH_SURF_CONFIG" },
    { 0x4006acul, "PGRAPH_6AC" },
    { 0x4006b0ul, "PGRAPH_STATUS" }
};

struct v9x_n3mm_request {
    DWORD config;
    DWORD count;
    DWORD offsets[V9X_N3MM_OFFSET_MAX];
};

struct v9x_n3mm_result {
    DWORD magic;
    DWORD status;
    DWORD id;
    DWORD bar0;
    DWORD accepted;
    DWORD boot0;
    DWORD cf8;
    DWORD refused_offset;
    DWORD config;
    DWORD bar1;
    DWORD class_revision;
    DWORD reserved[5];
    DWORD offsets[V9X_N3MM_OFFSET_MAX];
    DWORD first[V9X_N3MM_OFFSET_MAX];
    DWORD second[V9X_N3MM_OFFSET_MAX];
};

/* The header must be the VxD's N3_HEADER_DWORDS long. */
typedef char v9x_n3mm_header_matches[
    offsetof(struct v9x_n3mm_result, offsets) ==
    sizeof(DWORD) * V9X_N3MM_HEADER ? 1 : -1];

/* Values the decode reads, by offset; BAR0 offsets past 1 MiB fold into
 * small tables of their own. */
struct v9x_n3mm_value {
    DWORD offset;
    DWORD value;
    int present;
};

static struct v9x_n3mm_request v9x_request;
static struct v9x_n3mm_result v9x_result;
static struct v9x_n3mm_value v9x_values[V9X_N3MM_OFFSET_MAX];
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
    WritePrivateProfileStringA(section, key, text, V9X_N3MM_PATH);
}

static void v9x_number(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_decimal(text, value);
    WritePrivateProfileStringA(section, key, text, V9X_N3MM_PATH);
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_N3MM_PATH);
}

static void v9x_offset_key(char *key, const char *prefix, DWORD offset)
{
    lstrcpyA(key, prefix);
    v9x_hex(key + lstrlenA(prefix), offset, 6);
}

static void v9x_request_list(const struct v9x_n3mm_register *list,
                             DWORD count)
{
    char key[24];
    DWORD index;

    v9x_request.count = 0ul;
    for (index = 0ul; index < count && index < V9X_N3MM_OFFSET_MAX;
         ++index) {
        v9x_request.offsets[v9x_request.count++] = list[index].offset;
        v9x_offset_key(key, "Name.", list[index].offset);
        v9x_text("Mmio", key, list[index].name);
    }
}

/* The BAR1 samples: every V9X_N3_SAMPLE_STEP bytes of both windows. */
static void v9x_request_ramin(void)
{
    DWORD at;

    v9x_request.count = 0ul;
    for (at = 0ul; at < V9X_N3_WINDOW_BYTES; at += V9X_N3_SAMPLE_STEP) {
        v9x_request.offsets[v9x_request.count++] =
            V9X_N3_BAR1 | (V9X_N3_TOP_START + at);
    }
    for (at = 0ul; at < V9X_N3_WINDOW_BYTES; at += V9X_N3_SAMPLE_STEP) {
        v9x_request.offsets[v9x_request.count++] =
            V9X_N3_BAR1 | (V9X_N3_RAMIN_START + at);
    }
}

/*
 * /dense: one stretch of the BAR1 + C00000h window at every dword, for the
 * places the BIOS's own PFIFO setup names (the first capture on M-7V636C,
 * boot 10: RAMHT at RAMIN 0, RAMFC at 1C00h, CACHE1 contexts naming
 * instances 478h and 479h, so objects at 4780h).
 */
static void v9x_request_dense(DWORD start, DWORD bytes)
{
    DWORD at;

    v9x_request.count = 0ul;
    for (at = 0ul; at < bytes && v9x_request.count < V9X_N3MM_OFFSET_MAX;
         at += 4ul) {
        v9x_request.offsets[v9x_request.count++] =
            V9X_N3_BAR1 | (V9X_N3_RAMIN_START + start + at);
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

/* One DIOC capture of v9x_request. Returns nonzero on a snapshot. */
static int v9x_capture(void)
{
    HANDLE device;
    DWORD returned;
    BOOL ok;

    device = CreateFileA(V9X_N3MM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        return 0;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_N3MM_CAPTURE, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_N3MM_MAGIC) {
        v9x_text("Report", "Result", "DIOC-FAILED");
        v9x_key("Report", "Returned", returned);
        return 0;
    }
    return (v9x_result.status & V9X_ST_SNAPSHOT_TAKEN) != 0ul;
}

static void v9x_remember(DWORD offset, DWORD value)
{
    if (v9x_value_count < V9X_N3MM_OFFSET_MAX) {
        v9x_values[v9x_value_count].offset = offset;
        v9x_values[v9x_value_count].value = value;
        v9x_values[v9x_value_count].present = 1;
        ++v9x_value_count;
    }
}

static void v9x_publish_mmio(void)
{
    char key[24];
    char text[24];
    DWORD index;
    DWORD offset;

    for (index = 0ul; index < v9x_result.accepted; ++index) {
        offset = v9x_result.offsets[index];
        v9x_offset_key(key, "R", offset);
        v9x_hex(text, v9x_result.first[index], 8);
        if (v9x_result.second[index] != v9x_result.first[index]) {
            text[8] = ',';
            v9x_hex(text + 9, v9x_result.second[index], 8);
        }
        v9x_text("Mmio", key, text);
        v9x_remember(offset, v9x_result.first[index]);
    }
}

/* skip_zero: leave out words that read zero both times. */
static void v9x_publish_ramin(const char *section, int skip_zero)
{
    char key[24];
    char text[24];
    DWORD index;
    DWORD offset;

    for (index = 0ul; index < v9x_result.accepted; ++index) {
        if (skip_zero && v9x_result.first[index] == 0ul &&
            v9x_result.second[index] == 0ul) {
            continue;
        }
        offset = v9x_result.offsets[index] & ~V9X_N3_BAR1;
        if (offset >= V9X_N3_RAMIN_START) {
            v9x_offset_key(key, "R", offset - V9X_N3_RAMIN_START);
        } else {
            v9x_offset_key(key, "T", offset - V9X_N3_TOP_START);
        }
        v9x_hex(text, v9x_result.first[index], 8);
        if (v9x_result.second[index] != v9x_result.first[index]) {
            text[8] = ',';
            v9x_hex(text + 9, v9x_result.second[index], 8);
        }
        v9x_text(section, key, text);
    }
}

/* The /dense stretches; each fits one capture. */
static const DWORD v9x_dense[][2] = {
    { 0x0000ul, 0x0400ul },     /* RAMHT, first quarter */
    { 0x0400ul, 0x0400ul },
    { 0x0800ul, 0x0400ul },
    { 0x0c00ul, 0x0400ul },     /* RAMHT, last quarter */
    { 0x1c00ul, 0x0200ul },     /* RAMFC */
    { 0x1e00ul, 0x0200ul },     /* RAMRO */
    { 0x4700ul, 0x0200ul }      /* the BIOS's objects, 4780h and 4790h */
};

/* A value read in an earlier capture; *present says whether it was. */
static DWORD v9x_value(DWORD offset, int *present)
{
    DWORD index;

    for (index = 0ul; index < v9x_value_count; ++index) {
        if (v9x_values[index].offset == offset) {
            *present = 1;
            return v9x_values[index].value;
        }
    }
    *present = 0;
    return 0ul;
}

/* The first RIVA 128 on buses 0-7, function 0; 0 if none. */
static DWORD v9x_find_riva128(void)
{
    DWORD bus;
    DWORD slot;
    DWORD config;

    for (bus = 0ul; bus < V9X_PCI_BUSES; ++bus) {
        for (slot = 0ul; slot < V9X_PCI_DEVICES; ++slot) {
            config = V9X_PCI_ENABLE | (bus << 16) | (slot << 11);
            if (v9x_config_read(config, 0ul) == V9X_RIVA128_ID) {
                return config;
            }
        }
    }
    return 0ul;
}

static void v9x_decode_global(DWORD revision)
{
    DWORD value;
    DWORD size;
    int present;

    value = v9x_value(V9X_NV3_PMC_BOOT_0, &present);
    v9x_number("Decode", "BootChip",
               (value >> V9X_NV3_BOOT_CHIP_SHIFT) & V9X_NV3_BOOT_CHIP_MASK);
    v9x_key("Decode", "BootRevision", value & V9X_NV3_BOOT_REV_MASK);
    v9x_key("Decode", "PciRevision", revision);

    value = v9x_value(V9X_NV3_PMC_ENABLE, &present);
    v9x_text("Decode", "PfifoEnabled",
             (value & V9X_NV3_ENABLE_PFIFO) != 0ul ? "yes" : "no");
    v9x_text("Decode", "PgraphEnabled",
             (value & V9X_NV3_ENABLE_PGRAPH) != 0ul ? "yes" : "no");

    value = v9x_value(V9X_NV3_PFB_BOOT_0, &present);
    if ((value & V9X_NV3_PFB_ZX_DECODE) != 0ul &&
        revision >= V9X_NV3_ZX_REVISION) {
        size = value & V9X_NV3_PFB_SIZE_MASK;
        v9x_number("Decode", "MemoryBytes",
                   size == 2ul ? 0x400000ul :
                   size == 1ul ? 0x200000ul : 0x800000ul);
        v9x_text("Decode", "MemoryDecode", "128zx");
    } else {
        v9x_text("Decode", "MemoryDecode", "not-128zx");
    }

    value = v9x_value(V9X_NV3_PFB_CONFIG_0, &present);
    v9x_number("Decode", "PfbWidthPixels",
               (value & V9X_NV3_CFG_WIDTH_MASK) * V9X_NV3_CFG_WIDTH_UNIT);
    v9x_number("Decode", "PfbBytesPerPixel",
               (value >> V9X_NV3_CFG_DEPTH_SHIFT) & V9X_NV3_CFG_DEPTH_MASK);
    v9x_text("Decode", "PfbTilingDisabled",
             (value & V9X_NV3_CFG_TILING_OFF) != 0ul ? "yes" : "no");
    v9x_key("Decode", "Straps", v9x_value(V9X_NV3_PSTRAPS, &present));
}

static void v9x_decode_pfifo(void)
{
    DWORD value;
    int present;

    value = v9x_value(V9X_NV3_PFIFO_RAMHT, &present);
    v9x_key("Decode", "RamhtBase", value & V9X_NV3_RAMHT_BASE_MASK);
    v9x_number("Decode", "RamhtBytes",
               0x1000ul << ((value >> V9X_NV3_RAMHT_SIZE_SHIFT) & 3ul));
    v9x_key("Decode", "RamfcBase",
            v9x_value(V9X_NV3_PFIFO_RAMFC, &present) &
            V9X_NV3_RAMFC_BASE_MASK);
    value = v9x_value(V9X_NV3_PFIFO_RAMRO, &present);
    v9x_key("Decode", "RamroBase", value & V9X_NV3_RAMFC_BASE_MASK);
    v9x_number("Decode", "RamroBytes",
               (value & V9X_NV3_RAMRO_SIZE_BIT) != 0ul ? 0x2000ul : 0x200ul);
    v9x_key("Decode", "Caches", v9x_value(V9X_NV3_PFIFO_CACHES, &present));
    v9x_key("Decode", "Cache1Push0",
            v9x_value(V9X_NV3_CACHE1_PUSH0, &present));
    v9x_key("Decode", "Cache1Pull0",
            v9x_value(V9X_NV3_CACHE1_PULL0, &present));
    v9x_number("Decode", "UserFreeEntries",
               (v9x_value(V9X_NV3_USER_FREE, &present) &
                V9X_NV3_FREE_MASK) >> 2);
}

static void v9x_decode_pgraph(void)
{
    int present;

    v9x_key("Decode", "SurfConfig",
            v9x_value(V9X_NV3_PGRAPH_SURF_CFG, &present));
    v9x_text("Decode", "PgraphBusy",
             (v9x_value(V9X_NV3_PGRAPH_STATUS, &present) &
              V9X_NV3_STATUS_BUSY) != 0ul ? "yes" : "no");
}

static void v9x_finish(const char *result, UINT code)
{
    v9x_text("Report", "Result", result);
    WritePrivateProfileStringA(0, 0, 0, V9X_N3MM_PATH);
    ExitProcess(code);
}

void __stdcall V9xNv3MmioEntry(void)
{
    DWORD config;
    DWORD revision;
    DWORD enable;
    int present;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_N3MM_PATH);
    v9x_text("Report", "Tool", "V9XN3MM");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Access", "mmio-read-only");

    config = v9x_find_riva128();
    if (config == 0ul) {
        v9x_finish("NO-RIVA128", 1u);
    }
    revision = v9x_config_read(config, 0x08ul) & 0xfful;
    v9x_key("Report", "Config", config);
    v9x_key("Report", "Id", v9x_config_read(config, 0x00ul));
    v9x_key("Report", "CommandStatus", v9x_config_read(config, 0x04ul));
    v9x_key("Report", "ClassRevision", v9x_config_read(config, 0x08ul));
    v9x_key("Report", "Bar0", v9x_config_read(config, 0x10ul));
    v9x_key("Report", "Bar1", v9x_config_read(config, 0x14ul));
    v9x_key("Report", "Subsystem", v9x_config_read(config, 0x2cul));
    v9x_key("Report", "RomBase", v9x_config_read(config, 0x30ul));

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
    v9x_decode_global(revision);

    enable = v9x_value(V9X_NV3_PMC_ENABLE, &present);
    if ((enable & V9X_NV3_ENABLE_PFIFO) != 0ul) {
        v9x_request_list(v9x_pfifo,
                         sizeof(v9x_pfifo) / sizeof(v9x_pfifo[0]));
        if (!v9x_capture()) {
            v9x_finish("PFIFO-CAPTURE-FAILED", 3u);
        }
        v9x_publish_mmio();
        v9x_decode_pfifo();
        v9x_text("Report", "PfifoCapture", "taken");
    } else {
        v9x_text("Report", "PfifoCapture", "skipped-disabled");
    }

    if ((enable & V9X_NV3_ENABLE_PGRAPH) != 0ul) {
        v9x_request_list(v9x_pgraph,
                         sizeof(v9x_pgraph) / sizeof(v9x_pgraph[0]));
        if (!v9x_capture()) {
            v9x_finish("PGRAPH-CAPTURE-FAILED", 4u);
        }
        v9x_publish_mmio();
        v9x_decode_pgraph();
        v9x_text("Report", "PgraphCapture", "taken");
    } else {
        v9x_text("Report", "PgraphCapture", "skipped-disabled");
    }

    v9x_request_ramin();
    if (!v9x_capture()) {
        v9x_finish("RAMIN-CAPTURE-FAILED", 5u);
    }
    v9x_text("Report", "RaminCapture",
             (v9x_result.status & V9X_ST_FB_MAPPED) != 0ul ? "taken" :
                                                              "unmapped");
    v9x_publish_ramin("Ramin", 0);

    if (v9x_switch_given("/dense")) {
        DWORD index;

        for (index = 0ul; index < sizeof(v9x_dense) / sizeof(v9x_dense[0]);
             ++index) {
            v9x_request_dense(v9x_dense[index][0], v9x_dense[index][1]);
            if (!v9x_capture()) {
                v9x_finish("DENSE-CAPTURE-FAILED", 6u);
            }
            v9x_publish_ramin("RaminDense", 1);
        }
        v9x_text("Report", "DenseCapture", "taken");
    }
    v9x_finish("COMPLETE", 0u);
}
