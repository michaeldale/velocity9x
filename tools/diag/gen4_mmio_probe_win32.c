/*
 * V9XG4MM.EXE: Gen4 (965 / GM965) read-only MMIO and GTT snapshot.
 *
 * Phase 1 of docs\plans\intel-gen4-gen5-bringup.md (Order, step 3). Reads
 * GTTMMADR and BSM from the IGD's config space and GGC from the host
 * bridge's, loads G4MM.VXD, and asks it for register offsets (each read
 * twice) and the head of the GTT. Writes C:\V9XDIAG\V9XG4MM.INI:
 *
 *   [Report]  build, config words, status, PGTBL_CTL and the GTT's size;
 *   [Mmio]    every offset read, as R<offset>=first[,second if it moved],
 *             with Name.<offset> from Linux 6.6 i915_reg.h;
 *   [Display] which pipes and planes are live, and what was read for them;
 *   [Gtt]     the head of the table decoded by the 965 PRM Vol 1 8.2.1.4
 *             layout, the run of entries mapping stolen memory from BSM,
 *             and where PGTBL_CTL puts the table itself.
 *
 * Two captures. The first reads the registers that are safe whatever the
 * display state: the command streamer, clock gating, ports, panel power,
 * backlight, both PIPECONFs and both plane controls. The second reads the
 * timings, DPLL and plane registers of the pipes and planes the first
 * found enabled - and only those, because reading a disabled pipe's
 * counters hard-locked the 945
 * (docs\decisions\2026-10-15-a-disabled-pipes-counters-hang-the-945.md).
 * The counters themselves (PIPExDSL, PIPExFRAME*) are outside the VxD's
 * windows altogether.
 *
 * Nothing on the device is written. The only config access is mechanism
 * #1 reads, CF8h restored after each, as in V9XHSW.EXE.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_G4MM_PATH        V9X_DIAG_PATH("V9XG4MM.INI")
#define V9X_G4MM_VXD         "\\\\.\\G4MM.VXD"
#define V9X_G4MM_MAGIC       0x4d4d3447ul
#define V9X_G4MM_CAPTURE     1u
#define V9X_G4MM_OFFSET_MAX  256u
#define V9X_G4MM_GTT_COPY    4096u
#define V9X_G4MM_HEADER      16u
#define V9X_G4MM_GTT_RAW     16u
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_HOST_CONFIG      0x80000000ul
#define V9X_IGD_CONFIG       0x80001000ul

/* Config offsets: datasheet 316273 18.1.14 (GGC, host 52h), 21.1.10
 * (GTTMMADR), 21.1.24 (BSM), 21.1.25 (MSAC), 21.1.37 (ASLS). */
#define V9X_CFG_GTTMMADR     0x10ul
#define V9X_CFG_GMADR        0x18ul
#define V9X_CFG_GGC          0x50ul /* dword holding 52h */
#define V9X_CFG_BSM          0x5cul
#define V9X_CFG_MSAC         0x64ul /* dword holding 66h */
#define V9X_CFG_ASLS         0xfcul
#define V9X_GTTMMADR_MASK    0xfff00000ul
#define V9X_BSM_MASK         0xfff00000ul

#define V9X_ST_IGD_INTEL       0x00000001ul
#define V9X_ST_BASE_INVALID    0x00000002ul
#define V9X_ST_MMIO_MAPPED     0x00000004ul
#define V9X_ST_GTT_MAPPED      0x00000008ul
#define V9X_ST_OFFSETS_REFUSED 0x00000010ul
#define V9X_ST_SNAPSHOT_TAKEN  0x00000020ul
#define V9X_ST_GTT_READ        0x00000040ul
#define V9X_ST_NOT_GEN4        0x00000080ul
#define V9X_ST_GTT_SIZE_RSVD   0x00000100ul

/* 965 PRM Vol 1 8.2.1.4: 31:12 address 31:12, 7:4 address 35:32,
 * 2:1 mapping type (0 uncached main memory, 3 snooped), 0 valid. */
#define V9X_PTE_VALID        0x00000001ul
#define V9X_PTE_TYPE_MASK    0x00000006ul
#define V9X_PTE_TYPE_SHIFT   1u
#define V9X_PTE_HIGH_MASK    0x000000f0ul
#define V9X_PTE_MBZ_MASK     0x00000f08ul
#define V9X_PTE_ADDRESS_MASK 0xfffff000ul
#define V9X_PAGE_BYTES       0x1000ul

#define V9X_PGTBL_CTL        0x2020ul
#define V9X_PGTBL_BASE_MASK  0xfffff000ul
#define V9X_ENABLE_BIT       0x80000000ul
#define V9X_PLANE_PIPE_SHIFT 24u
#define V9X_PLANE_PIPE_MASK  0x3ul

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_g4mm_register {
    DWORD offset;
    const char *name;
};

/* Read in the first capture, whatever the display state. */
static const struct v9x_g4mm_register v9x_global[] = {
    { 0x02020ul, "PGTBL_CTL" },
    { 0x02024ul, "PGTBL_ER" },
    { 0x02030ul, "PRB0_TAIL" },
    { 0x02034ul, "PRB0_HEAD" },
    { 0x02038ul, "PRB0_START" },
    { 0x0203cul, "PRB0_CTL" },
    { 0x02064ul, "IPEIR_I965" },
    { 0x02068ul, "IPEHR_I965" },
    { 0x0206cul, "INSTDONE_I965" },
    { 0x02070ul, "INSTPS" },
    { 0x02074ul, "ACTHD_I965" },
    { 0x02078ul, "DMA_FADD_I965" },
    { 0x0207cul, "INSTDONE1" },
    { 0x02080ul, "HWS_PGA" },
    { 0x0209cul, "MI_MODE" },
    { 0x020b0ul, "EIR" },
    { 0x020b4ul, "EMR" },
    { 0x020b8ul, "ESR" },
    { 0x020c0ul, "INSTPM" },
    { 0x020c4ul, "PGTBL_CTL2" },
    { 0x020d8ul, "FW_BLC" },
    { 0x020dcul, "FW_BLC2" },
    { 0x020e0ul, "FW_BLC_SELF" },
    { 0x020e4ul, "MI_ARB_STATE" },
    { 0x06200ul, "DSPCLK_GATE_D" },
    { 0x06204ul, "RENCLK_GATE_D1" },
    { 0x06208ul, "RENCLK_GATE_D2" },
    { 0x06210ul, "RAMCLK_GATE_D" },
    { 0x06214ul, "DEUC" },
    { 0x61100ul, "ADPA" },
    { 0x61110ul, "PORT_HOTPLUG_EN" },
    { 0x61114ul, "PORT_HOTPLUG_STAT" },
    { 0x61140ul, "GEN3_SDVOB" },
    { 0x61160ul, "GEN3_SDVOC" },
    { 0x61180ul, "LVDS" },
    { 0x61200ul, "PP_STATUS" },
    { 0x61204ul, "PP_CONTROL" },
    { 0x61208ul, "PP_ON_DELAYS" },
    { 0x6120cul, "PP_OFF_DELAYS" },
    { 0x61210ul, "PP_DIVISOR" },
    { 0x61230ul, "PFIT_CONTROL" },
    { 0x61234ul, "PFIT_PGM_RATIOS" },
    { 0x61238ul, "PFIT_AUTO_RATIOS" },
    { 0x61250ul, "BLC_PWM_CTL2" },
    { 0x61254ul, "BLC_PWM_CTL" },
    { 0x70008ul, "PIPEACONF" },
    { 0x71008ul, "PIPEBCONF" },
    { 0x70030ul, "DSPARB" },
    { 0x70034ul, "DSPFW1" },
    { 0x70038ul, "DSPFW2" },
    { 0x7003cul, "DSPFW3" },
    { 0x70180ul, "DSPACNTR" },
    { 0x71180ul, "DSPBCNTR" },
    { 0x71400ul, "VGACNTRL" }
};

/* Per live pipe; offsets are pipe A's, pipe B adds the stride. */
static const struct v9x_g4mm_register v9x_pipe[] = {
    { 0x60000ul, "HTOTAL" },
    { 0x60004ul, "HBLANK" },
    { 0x60008ul, "HSYNC" },
    { 0x6000cul, "VTOTAL" },
    { 0x60010ul, "VBLANK" },
    { 0x60014ul, "VSYNC" },
    { 0x6001cul, "PIPESRC" },
    { 0x70024ul, "PIPESTAT" }
};
#define V9X_PIPE_STRIDE 0x1000ul

/* Per live pipe, the clock: DPLL_A/B, the MD word, FP0 and FP1. */
static const DWORD v9x_dpll[2] = { 0x6014ul, 0x6018ul };
static const DWORD v9x_dpll_md[2] = { 0x601cul, 0x6020ul };
static const DWORD v9x_fp0[2] = { 0x6040ul, 0x6048ul };
static const DWORD v9x_fp1[2] = { 0x6044ul, 0x604cul };

/* Per live plane; 965 PRM Vol 3 2.10.5. */
static const struct v9x_g4mm_register v9x_plane[] = {
    { 0x70184ul, "DSPLINOFF" },
    { 0x70188ul, "DSPSTRIDE" },
    { 0x7019cul, "DSPSURF" },
    { 0x701a4ul, "DSPTILEOFF" }
};

struct v9x_g4mm_request {
    DWORD base;
    DWORD count;
    DWORD offsets[V9X_G4MM_OFFSET_MAX];
};

struct v9x_g4mm_result {
    DWORD magic;
    DWORD status;
    DWORD igd_id;
    DWORD base;
    DWORD accepted;
    DWORD gtt_valid;
    DWORD cf8;
    DWORD refused_offset;
    DWORD pgtbl_ctl;
    DWORD gtt_bytes;
    DWORD gtt_entries;
    DWORD reserved[5];
    DWORD offsets[V9X_G4MM_OFFSET_MAX];
    DWORD first[V9X_G4MM_OFFSET_MAX];
    DWORD second[V9X_G4MM_OFFSET_MAX];
    DWORD gtt[V9X_G4MM_GTT_COPY];
};

/* The header must be the VxD's G4_HEADER_DWORDS long. */
typedef char v9x_g4mm_header_matches[
    offsetof(struct v9x_g4mm_result, offsets) ==
    sizeof(DWORD) * V9X_G4MM_HEADER ? 1 : -1];

static struct v9x_g4mm_request v9x_request;
static struct v9x_g4mm_result v9x_result;

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
    WritePrivateProfileStringA(section, key, text, V9X_G4MM_PATH);
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_G4MM_PATH);
}

static void v9x_offset_key(char *key, const char *prefix, DWORD offset)
{
    lstrcpyA(key, prefix);
    v9x_hex(key + lstrlenA(prefix), offset, 6);
}

static void v9x_add(DWORD offset)
{
    DWORD index;

    for (index = 0ul; index < v9x_request.count; ++index) {
        if (v9x_request.offsets[index] == offset) {
            return;
        }
    }
    if (v9x_request.count < V9X_G4MM_OFFSET_MAX) {
        v9x_request.offsets[v9x_request.count++] = offset;
    }
}

static void v9x_name(DWORD offset, const char *name, const char *suffix)
{
    char key[24];
    char text[40];

    v9x_offset_key(key, "Name.", offset);
    lstrcpyA(text, name);
    lstrcatA(text, suffix);
    v9x_text("Mmio", key, text);
}

/* The value the first read of the last capture returned, or 0. */
static DWORD v9x_value(DWORD offset)
{
    DWORD index;

    for (index = 0ul; index < v9x_result.accepted; ++index) {
        if (v9x_result.offsets[index] == offset) {
            return v9x_result.first[index];
        }
    }
    return 0ul;
}

/* One DIOC capture of v9x_request. Returns nonzero on a snapshot. */
static int v9x_capture(void)
{
    HANDLE device;
    DWORD returned;
    BOOL ok;

    device = CreateFileA(V9X_G4MM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        return 0;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_G4MM_CAPTURE, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_G4MM_MAGIC) {
        v9x_text("Report", "Result", "DIOC-FAILED");
        v9x_key("Report", "Returned", returned);
        return 0;
    }
    return (v9x_result.status & V9X_ST_SNAPSHOT_TAKEN) != 0ul;
}

static void v9x_publish_mmio(void)
{
    char key[24];
    char text[24];
    DWORD index;

    for (index = 0ul; index < v9x_result.accepted; ++index) {
        v9x_offset_key(key, "R", v9x_result.offsets[index]);
        v9x_hex(text, v9x_result.first[index], 8);
        if (v9x_result.second[index] != v9x_result.first[index]) {
            text[8] = ',';
            v9x_hex(text + 9, v9x_result.second[index], 8);
        }
        v9x_text("Mmio", key, text);
    }
}

/*
 * The head of the GTT against stolen memory. The BIOS maps the VBE
 * framebuffer through the aperture, so entry 0 onward should be valid and
 * point at consecutive pages from BSM. Also counted: each mapping type,
 * entries with the 35:32 extension set, and entries with MBZ bits set -
 * the Gen3 decode would misread either of the last two.
 */
static void v9x_publish_gtt(DWORD bsm)
{
    char key[16];
    DWORD index;
    DWORD raw;
    DWORD run;
    DWORD stolen_run;
    DWORD expect;
    DWORD valid;
    DWORD copied;
    DWORD types[4];
    DWORD high;
    DWORD mbz;

    copied = v9x_result.gtt_entries;
    if (copied > V9X_G4MM_GTT_COPY) {
        copied = V9X_G4MM_GTT_COPY;
    }
    for (index = 0ul; index < V9X_G4MM_GTT_RAW && index < copied; ++index) {
        lstrcpyA(key, "Entry");
        v9x_hex(key + 5, index, 4);
        v9x_key("Gtt", key, v9x_result.gtt[index]);
    }

    run = 0ul;
    stolen_run = 0ul;
    valid = 0ul;
    high = 0ul;
    mbz = 0ul;
    types[0] = types[1] = types[2] = types[3] = 0ul;
    expect = bsm & V9X_BSM_MASK;
    for (index = 0ul; index < copied; ++index) {
        raw = v9x_result.gtt[index];
        if ((raw & V9X_PTE_VALID) == 0ul) {
            continue;
        }
        ++valid;
        ++types[(raw & V9X_PTE_TYPE_MASK) >> V9X_PTE_TYPE_SHIFT];
        if ((raw & V9X_PTE_HIGH_MASK) != 0ul) {
            ++high;
        }
        if ((raw & V9X_PTE_MBZ_MASK) != 0ul) {
            ++mbz;
        }
        if (run == index) {
            ++run;
            if (stolen_run + 1ul == run &&
                (raw & (V9X_PTE_ADDRESS_MASK | V9X_PTE_HIGH_MASK)) == expect) {
                ++stolen_run;
                expect += V9X_PAGE_BYTES;
            }
        }
    }
    v9x_key("Gtt", "CopiedEntries", copied);
    v9x_key("Gtt", "ValidInCopy", valid);
    v9x_key("Gtt", "ValidRunFromZero", run);
    v9x_key("Gtt", "StolenRunFromZero", stolen_run);
    v9x_key("Gtt", "StolenBase", bsm & V9X_BSM_MASK);
    v9x_key("Gtt", "Type0Uncached", types[0]);
    v9x_key("Gtt", "Type1Reserved", types[1]);
    v9x_key("Gtt", "Type2Reserved", types[2]);
    v9x_key("Gtt", "Type3Snooped", types[3]);
    v9x_key("Gtt", "HighAddressSet", high);
    v9x_key("Gtt", "MbzSet", mbz);

    /* Where the stolen run ends: the entries either side of the break,
     * and how many later entries in the copy repeat the first one past
     * it (a scratch page mapped over the rest would). */
    if (stolen_run == 0ul || stolen_run >= copied) {
        return;
    }
    for (index = stolen_run - 1ul;
         index < stolen_run + 3ul && index < copied; ++index) {
        lstrcpyA(key, "Entry");
        v9x_hex(key + 5, index, 4);
        v9x_key("Gtt", key, v9x_result.gtt[index]);
    }
    expect = v9x_result.gtt[stolen_run];
    run = 0ul;
    for (index = stolen_run; index < copied; ++index) {
        if (v9x_result.gtt[index] == expect) {
            ++run;
        }
    }
    v9x_key("Gtt", "BreakRepeatsInCopy", run);
}

/* Where PGTBL_CTL puts the table, against stolen memory. */
static void v9x_publish_table(DWORD bsm, DWORD ggc)
{
    static const DWORD stolen_mb[8] = { 0ul, 1ul, 4ul, 8ul, 16ul, 32ul,
                                        48ul, 64ul };
    DWORD ctl;
    DWORD table;
    DWORD stolen;
    DWORD base;

    ctl = v9x_result.pgtbl_ctl;
    table = (ctl & V9X_PGTBL_BASE_MASK);
    base = bsm & V9X_BSM_MASK;
    stolen = stolen_mb[(ggc >> 4) & 7ul] << 20;
    v9x_key("Gtt", "PgtblCtl", ctl);
    v9x_key("Gtt", "PgtblEnabled", ctl & 1ul);
    v9x_key("Gtt", "PgtblBaseHigh", (ctl >> 4) & 0xful);
    v9x_key("Gtt", "TablePhysical", table);
    v9x_key("Gtt", "TableBytes", v9x_result.gtt_bytes);
    v9x_key("Gtt", "TableEntries", v9x_result.gtt_entries);
    v9x_key("Gtt", "ValidWholeTable", v9x_result.gtt_valid);
    v9x_key("Gtt", "StolenBytesFromGgc", stolen);
    if (table >= base && table < base + stolen) {
        v9x_text("Gtt", "TableInStolen", "yes");
        v9x_key("Gtt", "TableOffsetInStolen", table - base);
        v9x_key("Gtt", "StolenAfterTable",
                base + stolen - table - v9x_result.gtt_bytes);
    } else {
        v9x_text("Gtt", "TableInStolen", "no");
    }
}

/* Second capture: the live pipes' timings and clocks, the live planes. */
static int v9x_live_display(DWORD base, DWORD *pipe_conf, DWORD *plane_cntr)
{
    char key[24];
    DWORD pipe;
    DWORD index;
    DWORD offset;
    DWORD live;

    v9x_request.base = base;
    v9x_request.count = 0ul;
    live = 0ul;
    for (pipe = 0ul; pipe < 2ul; ++pipe) {
        lstrcpyA(key, "Pipe");
        key[4] = (char)('A' + pipe);
        key[5] = '\0';
        if ((pipe_conf[pipe] & V9X_ENABLE_BIT) == 0ul) {
            v9x_text("Display", key, "off");
            continue;
        }
        v9x_text("Display", key, "live");
        ++live;
        for (index = 0ul; index < sizeof(v9x_pipe) / sizeof(v9x_pipe[0]);
             ++index) {
            offset = v9x_pipe[index].offset + pipe * V9X_PIPE_STRIDE;
            v9x_add(offset);
            v9x_name(offset, v9x_pipe[index].name, pipe == 0ul ? "_A" : "_B");
        }
        v9x_add(v9x_dpll[pipe]);
        v9x_name(v9x_dpll[pipe], "DPLL", pipe == 0ul ? "_A" : "_B");
        v9x_add(v9x_dpll_md[pipe]);
        v9x_name(v9x_dpll_md[pipe], "DPLL_MD", pipe == 0ul ? "_A" : "_B");
        v9x_add(v9x_fp0[pipe]);
        v9x_name(v9x_fp0[pipe], "FP0", pipe == 0ul ? "_A" : "_B");
        v9x_add(v9x_fp1[pipe]);
        v9x_name(v9x_fp1[pipe], "FP1", pipe == 0ul ? "_A" : "_B");
    }
    for (pipe = 0ul; pipe < 2ul; ++pipe) {
        lstrcpyA(key, "Plane");
        key[5] = (char)('A' + pipe);
        key[6] = '\0';
        if ((plane_cntr[pipe] & V9X_ENABLE_BIT) == 0ul) {
            v9x_text("Display", key, "off");
            continue;
        }
        v9x_text("Display", key,
                 ((plane_cntr[pipe] >> V9X_PLANE_PIPE_SHIFT) &
                  V9X_PLANE_PIPE_MASK) == 0ul ? "live-on-pipe-A" :
                                                "live-on-pipe-B");
        for (index = 0ul; index < sizeof(v9x_plane) / sizeof(v9x_plane[0]);
             ++index) {
            offset = v9x_plane[index].offset + pipe * V9X_PIPE_STRIDE;
            v9x_add(offset);
            v9x_name(offset, v9x_plane[index].name, pipe == 0ul ? "_A" : "_B");
        }
    }
    v9x_key("Display", "LivePipes", live);
    if (v9x_request.count == 0ul) {
        return 1;
    }
    if (!v9x_capture()) {
        return 0;
    }
    v9x_publish_mmio();
    return 1;
}

void __stdcall V9xGen4MmioEntry(void)
{
    DWORD bar0;
    DWORD bsm;
    DWORD ggc;
    DWORD index;
    DWORD pipe_conf[2];
    DWORD plane_cntr[2];

    DeleteFileA(V9X_G4MM_PATH);
    v9x_text("Report", "Tool", "V9XG4MM");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Access", "mmio-read-only");

    bar0 = v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GTTMMADR);
    bsm = v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_BSM);
    ggc = v9x_config_read(V9X_HOST_CONFIG, V9X_CFG_GGC) >> 16;
    v9x_key("Report", "Bar0", bar0);
    v9x_key("Report", "Bar0High",
            v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GTTMMADR + 4ul));
    v9x_key("Report", "Bar2", v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GMADR));
    v9x_key("Report", "Bsm", bsm);
    v9x_key("Report", "Ggc", ggc & 0xfffful);
    v9x_key("Report", "Msac",
            (v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_MSAC) >> 16) & 0xfful);
    v9x_key("Report", "Asls", v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_ASLS));

    v9x_request.base = bar0 & V9X_GTTMMADR_MASK;
    v9x_request.count = 0ul;
    for (index = 0ul; index < sizeof(v9x_global) / sizeof(v9x_global[0]);
         ++index) {
        v9x_add(v9x_global[index].offset);
        v9x_name(v9x_global[index].offset, v9x_global[index].name, "");
    }
    if (!v9x_capture()) {
        v9x_key("Report", "Status", v9x_result.status);
        WritePrivateProfileStringA(0, 0, 0, V9X_G4MM_PATH);
        ExitProcess(2u);
    }

    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "IgdId", v9x_result.igd_id);
    v9x_key("Report", "Base", v9x_result.base);
    v9x_key("Report", "Accepted", v9x_result.accepted);
    v9x_key("Report", "RefusedOffset", v9x_result.refused_offset);
    v9x_publish_mmio();
    v9x_publish_table(bsm, ggc);
    if ((v9x_result.status & V9X_ST_GTT_READ) != 0ul) {
        v9x_publish_gtt(bsm);
    }

    pipe_conf[0] = v9x_value(0x70008ul);
    pipe_conf[1] = v9x_value(0x71008ul);
    plane_cntr[0] = v9x_value(0x70180ul);
    plane_cntr[1] = v9x_value(0x71180ul);
    if (!v9x_live_display(bar0 & V9X_GTTMMADR_MASK, pipe_conf, plane_cntr)) {
        v9x_text("Report", "Result", "DISPLAY-CAPTURE-FAILED");
        WritePrivateProfileStringA(0, 0, 0, V9X_G4MM_PATH);
        ExitProcess(3u);
    }

    v9x_text("Report", "Result", "COMPLETE");
    WritePrivateProfileStringA(0, 0, 0, V9X_G4MM_PATH);
    ExitProcess(0u);
}
