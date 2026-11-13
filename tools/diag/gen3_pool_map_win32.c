/*
 * V9XG3PM.EXE: Gen3 texture pool, Phase 2. The pool mapped through the
 * GTT and proved with the CPU alone; no GPU use.
 *
 * docs\plans\intel-gen3-system-memory-textures.md, Phase 2; authorised on
 * MICHAEL-NETBOOK by docs\decisions\2026-11-12-intel-gen3-texture-pool-
 * decisions.md. Drives G3PM.VXD (tools\diag\gen3_pool_map.asm):
 *
 *   1. ALLOC: the VxD checks the pool's GTT entries all hold one value and
 *      allocates the pages; this program sorts the frames and runs the
 *      driver's own v9x_i9xx_pool_check on them.
 *   2. MAP, only if accepted: PTEs written, a pattern written through the
 *      aperture and read back through it and through the pages' own
 *      linear mapping.
 *   3. Mode changes, through ChangeDisplaySettings: 640x480 at the current
 *      depth and back, MODE_CYCLES times. The VBIOS sets every mode.
 *   4. CHECK: every PTE and the pattern again.
 *   5. UNMAP: the saved PTE back in every entry, the pages freed.
 *
 * Each step's name is written to C:\V9XDIAG\V9XG3PM.INI and flushed before
 * the step runs, so after a hang the file says which step did not return.
 * The optional argument is the number of mode cycles (default 2, 0 skips).
 *
 * Two more arguments make a small, staged run, added after the full run
 * froze a GM965 for 6.5 hours inside MAP (docs\issues\2026-11-14-gm965-
 * pool-map-probe-machine-stops.md): "cycles pages stage". `pages` maps
 * that many pages instead of the plan's, from the plan's first entry;
 * `stage` stops MAP early - 1 the PTEs only, 2 and the aperture range
 * mapped but untouched, 3 and one dword through it, 4 everything. The
 * VxD's loops stop themselves after a few seconds and say so (TimedOut).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/i9xx_pool.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_G3PM_PATH        V9X_DIAG_PATH("V9XG3PM.INI")
#define V9X_G3PM_VXD         "\\\\.\\G3PM.VXD"
#define V9X_G3PM_MAGIC       0x4d503347ul
#define V9X_G3PM_ALLOC       2u
#define V9X_G3PM_MAP         3u
#define V9X_G3PM_CHECK       4u
#define V9X_G3PM_UNMAP       5u
#define V9X_G3PM_HEADER      40u
#define V9X_G3PM_PAGE_MAX    8192u
#define V9X_G3PM_CYCLES      2ul
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_HOST_CONFIG      0x80000000ul
#define V9X_IGD_CONFIG       0x80001000ul

#define V9X_CFG_GMADR        0x18ul
#define V9X_CFG_GTTADR       0x1cul
#define V9X_CFG_GGC          0x50ul
#define V9X_CFG_BSM          0x5cul
#define V9X_GMADR_MASK       0xf0000000ul
#define V9X_GTTADR_MASK      0xfffc0000ul
#define V9X_BSM_MASK         0xfff00000ul

/* The GM965 (Gen4, 8086:2A02) has no GTTADR: its GTT is GTTMMADR (10h) +
 * 512 KiB (datasheet 316273 21.1.10; gen3_pool_probe_win32.c). */
#define V9X_CFG_ID           0x00ul
#define V9X_CFG_GTTMMADR     0x10ul
#define V9X_GM965_ID         0x2a028086ul
#define V9X_GTTMMADR_MASK    0xfff00000ul
#define V9X_GM965_GTT_OFFSET 0x00080000ul

/* 256 MiB GMADR, 65,536 entries; the driver maps 64 MiB of it since
 * 01496faa (gma950_hw16.c), and the pool must lie inside that. */
#define V9X_G3PM_APERTURE    0x10000000ul
#define V9X_G3PM_GTT_ENTRIES 65536ul
#define V9X_G3PM_MAPPED      0x04000000ul

#define V9X_PTE_PRESENT      0x00000001ul
#define V9X_PTE_FRAME        0xfffff000ul

#define V9X_ST_TABLE_COPIED  0x00000100ul
#define V9X_ST_MAPPED        0x00000400ul
#define V9X_ST_PATTERN_DONE  0x00001000ul
#define V9X_ST_TIMED_OUT     0x00100000ul
#define V9X_ST_APERTURE_FAILED 0x00000800ul

#define V9X_STAGE_PTES       1ul
#define V9X_STAGE_APERTURE   2ul
#define V9X_STAGE_ONE_DWORD  3ul
#define V9X_STAGE_PATTERN    4ul
#define V9X_ST_RESTORED      0x00004000ul
#define V9X_ST_FREED         0x00008000ul

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_g3pm_request {
    DWORD bar3;
    DWORD first;
    DWORD count;
    DWORD gmadr;
    DWORD bsm;
    DWORD stolen;
    DWORD accept;
    DWORD stage;
};

struct v9x_g3pm_result {
    DWORD magic;
    DWORD status;
    DWORD igd_id;
    DWORD saved;
    DWORD uniform;
    DWORD pages;
    DWORD alloc_ms;
    DWORD linear;
    DWORD copy_result;
    DWORD last_pte;
    DWORD pte_bad;
    DWORD write_ms;
    DWORD ap_bad;
    DWORD ap_first;
    DWORD ap_actual;
    DWORD ram_bad;
    DWORD ram_first;
    DWORD ram_actual;
    DWORD chk_pte_bad;
    DWORD chk_pte_first;
    DWORD chk_pte_actual;
    DWORD chk_ap_bad;
    DWORD restore_bad;
    DWORD restore_last;
    DWORD refused_index;
    DWORD refused_frame;
    DWORD read_ms;
    DWORD cf8;
    DWORD aperture;
    DWORD write_kcyc;
    DWORD read_kcyc;
    DWORD dwords;
    DWORD pte_kcyc;
    DWORD pte_check_kcyc;
    DWORD reserved[6];
    DWORD page_table[V9X_G3PM_PAGE_MAX];
};

typedef char v9x_g3pm_header_matches[
    offsetof(struct v9x_g3pm_result, page_table) ==
    sizeof(DWORD) * V9X_G3PM_HEADER ? 1 : -1];

static struct v9x_g3pm_request v9x_request;
static struct v9x_g3pm_result v9x_result;
static DWORD v9x_frames[V9X_G3PM_PAGE_MAX];

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

static void v9x_hex(char *text, DWORD value)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < 8; ++index) {
        text[index] = hex[(value >> ((7 - index) * 4)) & 15ul];
    }
    text[8] = '\0';
}

static void v9x_flush(void)
{
    WritePrivateProfileStringA(0, 0, 0, V9X_G3PM_PATH);
}

static void v9x_key(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_hex(text, value);
    WritePrivateProfileStringA(section, key, text, V9X_G3PM_PATH);
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_G3PM_PATH);
}

/* The step about to run, on disk before it runs. */
static void v9x_step(const char *name)
{
    v9x_text("Report", "Step", name);
    v9x_flush();
}

static DWORD v9x_stolen_bytes(DWORD ggc)
{
    static const WORD sizes[16] = {
        0u, 1u, 4u, 8u, 16u, 32u, 48u, 64u,
        128u, 256u, 96u, 160u, 224u, 352u, 0u, 0u
    };
    return (DWORD)sizes[(ggc >> 4) & 15ul] * 0x100000ul;
}

static DWORD v9x_parse_count(const char *text, DWORD fallback)
{
    DWORD value = 0ul;
    int digits = 0;

    while (*text == ' ') {
        ++text;
    }
    while (*text >= '0' && *text <= '9') {
        value = value * 10ul + (DWORD)(*text - '0');
        ++text;
        ++digits;
    }
    return digits != 0 ? value : fallback;
}

/* The text after one number and the blanks before it. */
static const char *v9x_skip_count(const char *text)
{
    while (*text == ' ') {
        ++text;
    }
    while (*text >= '0' && *text <= '9') {
        ++text;
    }
    return text;
}

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

static int v9x_call(HANDLE device, DWORD code)
{
    DWORD returned = 0ul;
    BOOL ok;

    ok = DeviceIoControl(device, code, &v9x_request, sizeof(v9x_request),
                         &v9x_result, sizeof(v9x_result), &returned, 0);
    v9x_key("Report", "Status", v9x_result.status);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_G3PM_MAGIC) {
        v9x_text("Report", "Result", "DIOC-FAILED");
        v9x_key("Report", "FailedCall", code);
        v9x_flush();
        return 0;
    }
    return 1;
}

/* 640x480 at the current depth and back, `cycles` times, as Display
 * Properties would. Returns the number of switches that failed. */
static DWORD v9x_mode_cycles(DWORD cycles)
{
    DEVMODEA mode;
    HDC screen;
    DWORD width;
    DWORD height;
    DWORD bpp;
    DWORD cycle;
    DWORD failed = 0ul;

    screen = GetDC(0);
    width = (DWORD)GetDeviceCaps(screen, HORZRES);
    height = (DWORD)GetDeviceCaps(screen, VERTRES);
    bpp = (DWORD)GetDeviceCaps(screen, BITSPIXEL);
    ReleaseDC(0, screen);
    v9x_key("Modes", "StartWidth", width);
    v9x_key("Modes", "StartHeight", height);
    v9x_key("Modes", "StartBpp", bpp);

    for (cycle = 0ul; cycle < cycles; ++cycle) {
        BYTE *bytes = (BYTE *)&mode;
        DWORD at;

        /* No C runtime is linked, so no memset behind ZeroMemory. */
        for (at = 0ul; at < sizeof(mode); ++at) {
            bytes[at] = 0u;
        }
        mode.dmSize = sizeof(mode);
        mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
        mode.dmBitsPerPel = bpp;
        mode.dmPelsWidth = 640ul;
        mode.dmPelsHeight = 480ul;
        if (ChangeDisplaySettingsA(&mode, 0) != DISP_CHANGE_SUCCESSFUL) {
            ++failed;
        }
        Sleep(1000ul);
        mode.dmPelsWidth = width;
        mode.dmPelsHeight = height;
        if (ChangeDisplaySettingsA(&mode, 0) != DISP_CHANGE_SUCCESSFUL) {
            ++failed;
        }
        Sleep(1000ul);
    }
    v9x_key("Modes", "Cycles", cycles);
    v9x_key("Modes", "Failed", failed);
    return failed;
}

/* The frames as the driver would see them, sorted, then its policy. */
static v9x_u16 v9x_judge(const struct v9x_i9xx_pool_layout *layout,
                         const struct v9x_i9xx_pool_plan *plan)
{
    DWORD index;
    DWORD absent = 0ul;

    for (index = 0ul; index < plan->entry_count; ++index) {
        if ((v9x_result.page_table[index] & V9X_PTE_PRESENT) == 0ul) {
            ++absent;
        }
        v9x_frames[index] = v9x_result.page_table[index] & V9X_PTE_FRAME;
    }
    v9x_i9xx_pool_sort(v9x_frames, plan->entry_count);
    v9x_key("Pages", "Absent", absent);
    v9x_key("Pages", "Lowest", v9x_frames[0]);
    v9x_key("Pages", "Highest", v9x_frames[plan->entry_count - 1ul]);
    if (absent != 0ul) {
        return V9X_I9XX_POOL_ZERO_PAGE;
    }
    return v9x_i9xx_pool_check(layout, plan, v9x_frames, plan->entry_count);
}

/* A small run's pages: fewer than the plan's, so the whole-pool check does
 * not apply; each page still goes through the driver's per-page check. */
static v9x_u16 v9x_judge_few(const struct v9x_i9xx_pool_layout *layout,
                             DWORD count)
{
    DWORD index;
    v9x_u16 reason;

    for (index = 0ul; index < count; ++index) {
        if ((v9x_result.page_table[index] & V9X_PTE_PRESENT) == 0ul) {
            return V9X_I9XX_POOL_ZERO_PAGE;
        }
        reason = v9x_i9xx_pool_check_page(
            layout, v9x_result.page_table[index] & V9X_PTE_FRAME);
        if (reason != V9X_I9XX_POOL_OK) {
            return reason;
        }
    }
    v9x_key("Pages", "First", v9x_result.page_table[0] & V9X_PTE_FRAME);
    return V9X_I9XX_POOL_OK;
}

static void v9x_publish_map(void)
{
    v9x_key("Map", "LastPte", v9x_result.last_pte);
    v9x_key("Map", "PteMismatches", v9x_result.pte_bad);
    v9x_key("Map", "PteWriteKcyc", v9x_result.pte_kcyc);
    v9x_key("Map", "PteCheckKcyc", v9x_result.pte_check_kcyc);
    v9x_key("Map", "Dwords", v9x_result.dwords);
    v9x_key("Map", "WriteKcyc", v9x_result.write_kcyc);
    v9x_key("Map", "ReadKcyc", v9x_result.read_kcyc);
    v9x_text("Map", "TimedOut",
             (v9x_result.status & V9X_ST_TIMED_OUT) != 0ul ? "yes" : "no");
    v9x_key("Map", "Aperture", v9x_result.aperture);
    v9x_key("Map", "WriteMs", v9x_result.write_ms);
    v9x_key("Map", "ReadMs", v9x_result.read_ms);
    v9x_key("Map", "ApertureMismatches", v9x_result.ap_bad);
    v9x_key("Map", "ApertureFirst", v9x_result.ap_first);
    v9x_key("Map", "ApertureActual", v9x_result.ap_actual);
    v9x_key("Map", "RamMismatches", v9x_result.ram_bad);
    v9x_key("Map", "RamFirst", v9x_result.ram_first);
    v9x_key("Map", "RamActual", v9x_result.ram_actual);
    v9x_key("Map", "RefusedIndex", v9x_result.refused_index);
    v9x_key("Map", "RefusedFrame", v9x_result.refused_frame);
}

void WINAPI V9xGen3PoolMapEntry(void)
{
    struct v9x_i9xx_pool_layout layout;
    struct v9x_i9xx_pool_plan plan;
    MEMORYSTATUS memory;
    HANDLE device;
    DWORD ggc;
    DWORD cycles;
    DWORD pages;
    DWORD stage;
    DWORD first;
    DWORD mode_failures = 0ul;
    const char *arguments;
    v9x_u16 reason;
    int pass;

    DeleteFileA(V9X_G3PM_PATH);
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Result", "STARTED");
    arguments = v9x_argument();
    cycles = v9x_parse_count(arguments, V9X_G3PM_CYCLES);
    arguments = v9x_skip_count(arguments);
    pages = v9x_parse_count(arguments, 0ul);
    arguments = v9x_skip_count(arguments);
    stage = v9x_parse_count(arguments, V9X_STAGE_PATTERN);
    if (stage < V9X_STAGE_PTES || stage > V9X_STAGE_PATTERN) {
        stage = V9X_STAGE_PATTERN;
    }
    /* A fourth number moves a small run to another first entry (decimal),
     * for a machine whose driver already has a pool in the plan's. */
    arguments = v9x_skip_count(arguments);
    first = v9x_parse_count(arguments, 0ul);

    ggc = (v9x_config_read(V9X_HOST_CONFIG, V9X_CFG_GGC) >> 16) & 0xfffful;
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatus(&memory);
    layout.bsm = v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_BSM) & V9X_BSM_MASK;
    layout.stolen_bytes = v9x_stolen_bytes(ggc);
    layout.aperture_bytes = V9X_G3PM_APERTURE;
    layout.gtt_entries = V9X_G3PM_GTT_ENTRIES;
    layout.mapped_bytes = V9X_G3PM_MAPPED;
    layout.ram_bytes = memory.dwTotalPhys;
    layout.excluded_count = 0u;
    reason = v9x_i9xx_pool_size(&layout, &plan);
    v9x_key("Report", "Ggc", ggc);
    v9x_key("Report", "Bsm", layout.bsm);
    v9x_key("Report", "StolenBytes", layout.stolen_bytes);
    v9x_key("Report", "RamBytes", memory.dwTotalPhys);
    v9x_key("Report", "PlanReason", reason);
    v9x_key("Report", "PlanOffset", plan.aperture_offset);
    v9x_key("Report", "PlanBytes", plan.bytes);
    v9x_key("Report", "PlanFirstEntry", plan.first_entry);
    v9x_key("Report", "PlanEntries", plan.entry_count);
    if (reason != V9X_I9XX_POOL_OK ||
        plan.entry_count > V9X_G3PM_PAGE_MAX) {
        v9x_text("Report", "Result", "NO-PLAN");
        v9x_flush();
        ExitProcess(2u);
    }

    if (v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_ID) == V9X_GM965_ID) {
        v9x_request.bar3 =
            (v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GTTMMADR) &
             V9X_GTTMMADR_MASK) + V9X_GM965_GTT_OFFSET;
    } else {
        v9x_request.bar3 =
            v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GTTADR) & V9X_GTTADR_MASK;
    }
    v9x_request.gmadr =
        v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GMADR) & V9X_GMADR_MASK;
    if (pages == 0ul || pages > plan.entry_count) {
        pages = plan.entry_count;
    }
    v9x_request.first = plan.first_entry;
    if (first != 0ul && pages != plan.entry_count &&
        first >= plan.first_entry &&
        first + pages <= V9X_G3PM_GTT_ENTRIES) {
        v9x_request.first = first;
    }
    v9x_key("Report", "FirstEntry", v9x_request.first);
    v9x_request.count = pages;
    v9x_request.bsm = layout.bsm;
    v9x_request.stolen = layout.stolen_bytes;
    v9x_request.accept = 0ul;
    v9x_request.stage = stage;
    v9x_key("Report", "Pages", pages);
    v9x_key("Report", "Stage", stage);
    v9x_key("Report", "Cycles", cycles);
    v9x_key("Report", "Bar3", v9x_request.bar3);
    v9x_key("Report", "Gmadr", v9x_request.gmadr);

    v9x_step("load");
    device = CreateFileA(V9X_G3PM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        v9x_flush();
        ExitProcess(3u);
    }

    v9x_step("alloc");
    if (!v9x_call(device, V9X_G3PM_ALLOC)) {
        CloseHandle(device);
        ExitProcess(4u);
    }
    v9x_key("Alloc", "IgdId", v9x_result.igd_id);
    v9x_key("Alloc", "SavedPte", v9x_result.saved);
    v9x_key("Alloc", "Uniform", v9x_result.uniform);
    v9x_key("Alloc", "AllocMs", v9x_result.alloc_ms);
    v9x_key("Alloc", "Linear", v9x_result.linear);
    pass = (v9x_result.status & V9X_ST_TABLE_COPIED) != 0ul;
    if (pass) {
        reason = pages == plan.entry_count ? v9x_judge(&layout, &plan)
                                           : v9x_judge_few(&layout, pages);
        v9x_key("Pages", "CheckReason", reason);
        v9x_request.accept = reason == V9X_I9XX_POOL_OK ? 1ul : 0ul;
        v9x_text("Pages", "Verdict",
                 v9x_request.accept != 0ul ? "ACCEPTED" : "REFUSED");
    }

    if (pass && v9x_request.accept != 0ul) {
        v9x_step("map");
        pass = v9x_call(device, V9X_G3PM_MAP);
        v9x_publish_map();
        /* What a stage has to have reached: the PTEs read back; then the
         * aperture mapped; then the pattern, one dword or all, compared. */
        pass = pass && (v9x_result.status & V9X_ST_MAPPED) != 0ul &&
               (v9x_result.status & V9X_ST_TIMED_OUT) == 0ul;
        if (stage >= V9X_STAGE_APERTURE) {
            pass = pass && v9x_result.aperture != 0ul &&
                   (v9x_result.status & V9X_ST_APERTURE_FAILED) == 0ul;
        }
        if (stage >= V9X_STAGE_ONE_DWORD) {
            pass = pass && (v9x_result.status & V9X_ST_PATTERN_DONE) != 0ul;
        }
    } else {
        pass = 0;
    }

    if (pass && cycles != 0ul && stage == V9X_STAGE_PATTERN) {
        v9x_step("modes");
        mode_failures = v9x_mode_cycles(cycles);
    }
    if (pass) {
        v9x_step("check");
        if (v9x_call(device, V9X_G3PM_CHECK)) {
            v9x_key("Check", "PteMismatches", v9x_result.chk_pte_bad);
            v9x_key("Check", "PteFirst", v9x_result.chk_pte_first);
            v9x_key("Check", "PteActual", v9x_result.chk_pte_actual);
            v9x_key("Check", "ApertureMismatches", v9x_result.chk_ap_bad);
        }
    }

    v9x_step("unmap");
    (void)v9x_call(device, V9X_G3PM_UNMAP);
    v9x_key("Unmap", "RestoreMismatches", v9x_result.restore_bad);
    v9x_key("Unmap", "RestoreLast", v9x_result.restore_last);
    CloseHandle(device);

    v9x_step("done");
    if (!pass) {
        v9x_text("Report", "Result", "INCOMPLETE");
    } else if (v9x_result.pte_bad == 0ul && v9x_result.ap_bad == 0ul &&
               v9x_result.ram_bad == 0ul &&
               v9x_result.chk_pte_bad == 0ul &&
               v9x_result.chk_ap_bad == 0ul &&
               v9x_result.restore_bad == 0ul && mode_failures == 0ul &&
               (v9x_result.status & V9X_ST_RESTORED) != 0ul &&
               (v9x_result.status & V9X_ST_FREED) != 0ul) {
        v9x_text("Report", "Result", "PASS");
    } else {
        v9x_text("Report", "Result", "FAIL");
    }
    v9x_flush();
    ExitProcess(0u);
}
