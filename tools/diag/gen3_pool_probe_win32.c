/*
 * V9XG3PL.EXE: Gen3 texture pool, Phase 0 measurements. Writes nothing to
 * the device.
 *
 * docs\plans\intel-gen3-system-memory-textures.md, Phase 0; authorised by
 * docs\decisions\2026-11-12-intel-gen3-texture-pool-decisions.md. Reads
 * BAR3, BSM and GGC from config space, sizes the pool with the driver's own
 * policy (src\common\i9xx_pool.c) for a 48 MiB aperture mapping, then asks
 * G3PL.VXD to read the GTT across the stolen run's end and the pool's
 * entries, and to allocate, list and free the pool's pages. The page list
 * goes through v9x_i9xx_pool_sort and v9x_i9xx_pool_check exactly as the
 * driver will put it. Writes C:\V9XDIAG\V9XG3PL.INI:
 *
 *   [Report]  build, config words, RAM, the plan, the VxD's status
 *   [Gtt]     the window's raw entries, and the span's valid count, fill
 *             count and hash - run it before and after a mode change
 *   [Pages]   allocation time, free and lockable pages, the physical
 *             spread of the pages and the policy's verdict on them
 *
 * An optional argument is the page count to allocate instead of the
 * plan's, for measuring where _PageAllocate stops.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/i9xx_pool.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_G3PL_PATH        V9X_DIAG_PATH("V9XG3PL.INI")
#define V9X_G3PL_VXD         "\\\\.\\G3PL.VXD"
#define V9X_G3PL_MAGIC       0x4c503347ul
#define V9X_G3PL_CAPTURE     1u
#define V9X_G3PL_HEADER      32u
#define V9X_G3PL_WINDOW      64u
#define V9X_G3PL_PAGE_MAX    8192u
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_HOST_CONFIG      0x80000000ul
#define V9X_IGD_CONFIG       0x80001000ul

/* 945 config: GMADR 18h, GTTADR (BAR3) 1Ch, BSM 5Ch (datasheet 309219
 * 9.1.x); GGC at host 52h. */
#define V9X_CFG_GMADR        0x18ul
#define V9X_CFG_GTTADR       0x1cul
#define V9X_CFG_GGC          0x50ul /* dword holding 52h */
#define V9X_CFG_BSM          0x5cul
#define V9X_GTTADR_MASK      0xfffc0000ul
#define V9X_BSM_MASK         0xfff00000ul

/* The GM965 (Gen4, 8086:2A02) has no GTTADR: config 1Ch is GMADR's high
 * dword, and the GTT is GTTMMADR (10h, 1 MiB) + 512 KiB - datasheet 316273
 * 21.1.10, as runtime.asm V9xPciReadIntelGttConfig reads it. Measured on
 * TOMMOHP (docs\decisions\2026-10-20-gm965-tommohp-tier0-and-mmio.md). */
#define V9X_CFG_ID           0x00ul
#define V9X_CFG_GTTMMADR     0x10ul
#define V9X_GM965_ID         0x2a028086ul
#define V9X_GTTMMADR_MASK    0xfff00000ul
#define V9X_GM965_GTT_OFFSET 0x00080000ul

/* The 945's GMADR is 256 MiB with 65,536 GTT entries
 * (2026-09-12-intel-phase2-gtt-inventory.md). */
#define V9X_G3PL_APERTURE    0x10000000ul
#define V9X_G3PL_GTT_ENTRIES 65536ul
/* Decision 1's mapping: 16 MiB today plus the 32 MiB pool. */
#define V9X_G3PL_MAPPED      0x03000000ul
/* Entries shown before the stolen run's end. */
#define V9X_G3PL_LEAD        32ul

/* The VMM's page-table entry: present is bit 0, the frame bits 31:12. */
#define V9X_PTE_PRESENT      0x00000001ul
#define V9X_PTE_FRAME        0xfffff000ul

#define V9X_ST_GTT_READ      0x00000010ul
#define V9X_ST_ALLOCATED     0x00000040ul
#define V9X_ST_TABLE_COPIED  0x00000100ul
#define V9X_ST_FREED         0x00000400ul

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_g3pl_request {
    DWORD base;
    DWORD first;
    DWORD span;
    DWORD pages;
};

struct v9x_g3pl_result {
    DWORD magic;
    DWORD status;
    DWORD igd_id;
    DWORD base;
    DWORD first;
    DWORD span;
    DWORD gtt_valid;
    DWORD gtt_fill;
    DWORD gtt_hash;
    DWORD pages_asked;
    DWORD free_before;
    DWORD lockable_before;
    DWORD free_allocated;
    DWORD lockable_allocated;
    DWORD alloc_ms;
    DWORD linear;
    DWORD copy_result;
    DWORD free_after;
    DWORD cf8;
    DWORD reserved[13];
    DWORD window[V9X_G3PL_WINDOW];
    DWORD pages[V9X_G3PL_PAGE_MAX];
};

/* The header must be the VxD's G3_HEADER_DWORDS long. */
typedef char v9x_g3pl_header_matches[
    offsetof(struct v9x_g3pl_result, window) ==
    sizeof(DWORD) * V9X_G3PL_HEADER ? 1 : -1];

static struct v9x_g3pl_request v9x_request;
static struct v9x_g3pl_result v9x_result;
static DWORD v9x_frames[V9X_G3PL_PAGE_MAX];

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

static void v9x_key(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_hex(text, value, 8);
    WritePrivateProfileStringA(section, key, text, V9X_G3PL_PATH);
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_G3PL_PATH);
}

static void v9x_numbered(const char *section, const char *prefix,
                         DWORD number, DWORD value)
{
    char key[16];

    lstrcpyA(key, prefix);
    v9x_hex(key + lstrlenA(prefix), number, 4);
    v9x_key(section, key, value);
}

/* GGC's GMS, bits 7:4, in MiB: the table intel_gtt16.c decodes. */
static DWORD v9x_stolen_bytes(DWORD ggc)
{
    static const WORD sizes[16] = {
        0u, 1u, 4u, 8u, 16u, 32u, 48u, 64u,
        128u, 256u, 96u, 160u, 224u, 352u, 0u, 0u
    };
    return (DWORD)sizes[(ggc >> 4) & 15ul] * 0x100000ul;
}

static DWORD v9x_parse_count(const char *text)
{
    DWORD value = 0ul;

    while (*text == ' ') {
        ++text;
    }
    while (*text >= '0' && *text <= '9') {
        value = value * 10ul + (DWORD)(*text - '0');
        ++text;
    }
    return value;
}

/* The argument after the program name, or none. */
static const char *v9x_argument(void)
{
    const char *line = GetCommandLineA();

    if (*line == '"') {
        ++line;
        while (*line != '\0' && *line != '"') {
            ++line;
        }
        if (*line == '"') {
            ++line;
        }
    } else {
        while (*line != '\0' && *line != ' ') {
            ++line;
        }
    }
    return line;
}

static int v9x_capture(void)
{
    HANDLE device;
    DWORD returned;
    BOOL ok;

    device = CreateFileA(V9X_G3PL_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        return 0;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_G3PL_CAPTURE, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_G3PL_MAGIC) {
        v9x_text("Report", "Result", "DIOC-FAILED");
        v9x_key("Report", "Returned", returned);
        return 0;
    }
    return 1;
}

static void v9x_publish_gtt(void)
{
    DWORD index;

    v9x_key("Gtt", "First", v9x_result.first);
    v9x_key("Gtt", "Span", v9x_result.span);
    v9x_key("Gtt", "Valid", v9x_result.gtt_valid);
    v9x_key("Gtt", "EqualFirst", v9x_result.gtt_fill);
    v9x_key("Gtt", "Hash", v9x_result.gtt_hash);
    for (index = 0ul; index < V9X_G3PL_WINDOW; ++index) {
        v9x_numbered("Gtt", "E", v9x_result.first + index,
                     v9x_result.window[index]);
    }
}

/*
 * The pages as the driver will see them: frames out of the page-table
 * entries, sorted, then the policy's check. Also their spread: lowest,
 * highest, how many runs of consecutive frames, and the longest.
 */
static void v9x_publish_pages(const struct v9x_i9xx_pool_layout *layout,
                              const struct v9x_i9xx_pool_plan *plan,
                              DWORD count)
{
    DWORD index;
    DWORD absent = 0ul;
    DWORD runs = 0ul;
    DWORD run = 0ul;
    DWORD longest = 0ul;
    DWORD below16 = 0ul;
    v9x_u16 reason;

    v9x_key("Pages", "AllocMs", v9x_result.alloc_ms);
    v9x_key("Pages", "FreeBefore", v9x_result.free_before);
    v9x_key("Pages", "LockableBefore", v9x_result.lockable_before);
    v9x_key("Pages", "FreeAllocated", v9x_result.free_allocated);
    v9x_key("Pages", "LockableAllocated", v9x_result.lockable_allocated);
    v9x_key("Pages", "FreeAfter", v9x_result.free_after);
    v9x_key("Pages", "Linear", v9x_result.linear);
    v9x_key("Pages", "CopyResult", v9x_result.copy_result);
    if ((v9x_result.status & V9X_ST_TABLE_COPIED) == 0ul) {
        v9x_text("Pages", "Verdict", "NO-TABLE");
        return;
    }

    for (index = 0ul; index < count; ++index) {
        DWORD entry = v9x_result.pages[index];

        if ((entry & V9X_PTE_PRESENT) == 0ul) {
            ++absent;
        }
        v9x_frames[index] = entry & V9X_PTE_FRAME;
    }
    for (index = 0ul; index < 8ul && index < count; ++index) {
        v9x_numbered("Pages", "Raw", index, v9x_result.pages[index]);
    }
    v9x_i9xx_pool_sort(v9x_frames, count);
    for (index = 0ul; index < count; ++index) {
        if (v9x_frames[index] < 0x01000000ul) {
            ++below16;
        }
        if (index > 0ul &&
            v9x_frames[index] == v9x_frames[index - 1ul] + 4096ul) {
            ++run;
        } else {
            ++runs;
            run = 1ul;
        }
        if (run > longest) {
            longest = run;
        }
    }
    v9x_key("Pages", "Count", count);
    v9x_key("Pages", "Absent", absent);
    v9x_key("Pages", "Lowest", v9x_frames[0]);
    v9x_key("Pages", "Highest", v9x_frames[count - 1ul]);
    v9x_key("Pages", "Below16M", below16);
    v9x_key("Pages", "Runs", runs);
    v9x_key("Pages", "LongestRun", longest);

    if (count != plan->entry_count) {
        v9x_text("Pages", "Verdict", "NOT-THE-PLAN-COUNT");
        return;
    }
    reason = v9x_i9xx_pool_check(layout, plan, v9x_frames, count);
    v9x_key("Pages", "CheckReason", reason);
    v9x_text("Pages", "Verdict",
             reason == V9X_I9XX_POOL_OK && absent == 0ul ? "ACCEPTED"
                                                         : "REFUSED");
}

void WINAPI V9xGen3PoolEntry(void)
{
    struct v9x_i9xx_pool_layout layout;
    struct v9x_i9xx_pool_plan plan;
    MEMORYSTATUS memory;
    DWORD ggc;
    DWORD bsm;
    DWORD bar3;
    DWORD stolen_entries;
    DWORD pages;
    DWORD end;
    v9x_u16 reason;

    WritePrivateProfileStringA(0, 0, 0, V9X_G3PL_PATH);
    DeleteFileA(V9X_G3PL_PATH);
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Result", "STARTED");

    ggc = (v9x_config_read(V9X_HOST_CONFIG, V9X_CFG_GGC) >> 16) & 0xfffful;
    bsm = v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_BSM) & V9X_BSM_MASK;
    if (v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_ID) == V9X_GM965_ID) {
        bar3 = (v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GTTMMADR) &
                V9X_GTTMMADR_MASK) + V9X_GM965_GTT_OFFSET;
        v9x_text("Report", "GttFrom", "GTTMMADR+512K");
    } else {
        bar3 = v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GTTADR) &
               V9X_GTTADR_MASK;
        v9x_text("Report", "GttFrom", "BAR3");
    }
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatus(&memory);
    v9x_key("Report", "Ggc", ggc);
    v9x_key("Report", "Bsm", bsm);
    v9x_key("Report", "Bar3", bar3);
    v9x_key("Report", "Bar2",
            v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GMADR));
    v9x_key("Report", "RamBytes", memory.dwTotalPhys);

    layout.bsm = bsm;
    layout.stolen_bytes = v9x_stolen_bytes(ggc);
    layout.aperture_bytes = V9X_G3PL_APERTURE;
    layout.gtt_entries = V9X_G3PL_GTT_ENTRIES;
    layout.mapped_bytes = V9X_G3PL_MAPPED;
    layout.ram_bytes = memory.dwTotalPhys;
    layout.excluded_count = 0u;
    reason = v9x_i9xx_pool_size(&layout, &plan);
    v9x_key("Report", "StolenBytes", layout.stolen_bytes);
    v9x_key("Report", "PlanReason", reason);
    v9x_key("Report", "PlanOffset", plan.aperture_offset);
    v9x_key("Report", "PlanBytes", plan.bytes);
    v9x_key("Report", "PlanFirstEntry", plan.first_entry);
    v9x_key("Report", "PlanEntries", plan.entry_count);

    pages = v9x_parse_count(v9x_argument());
    if (pages == 0ul) {
        pages = plan.entry_count;
    }
    if (pages > V9X_G3PL_PAGE_MAX) {
        pages = V9X_G3PL_PAGE_MAX;
    }

    /* From a little before the stolen run's end to the pool's end, or 4096
     * entries when there is no plan. */
    stolen_entries = layout.stolen_bytes / 4096ul;
    v9x_request.base = bar3;
    v9x_request.first = stolen_entries > V9X_G3PL_LEAD
                            ? stolen_entries - V9X_G3PL_LEAD : 0ul;
    end = reason == V9X_I9XX_POOL_OK ? plan.first_entry + plan.entry_count
                                     : v9x_request.first + 4096ul;
    if (end > V9X_G3PL_GTT_ENTRIES) {
        end = V9X_G3PL_GTT_ENTRIES;
    }
    v9x_request.span = end - v9x_request.first;
    v9x_request.pages = pages;

    if (!v9x_capture()) {
        WritePrivateProfileStringA(0, 0, 0, V9X_G3PL_PATH);
        ExitProcess(2u);
    }
    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "IgdId", v9x_result.igd_id);
    v9x_key("Report", "PagesAsked", v9x_result.pages_asked);
    if ((v9x_result.status & V9X_ST_GTT_READ) != 0ul) {
        v9x_publish_gtt();
    }
    if ((v9x_result.status & V9X_ST_ALLOCATED) != 0ul) {
        v9x_publish_pages(&layout, &plan, pages);
    }
    v9x_text("Report", "Result",
             (v9x_result.status & V9X_ST_FREED) != 0ul ? "COMPLETE"
                                                       : "INCOMPLETE");
    WritePrivateProfileStringA(0, 0, 0, V9X_G3PL_PATH);
    ExitProcess(0u);
}
