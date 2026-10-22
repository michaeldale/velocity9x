/*
 * V9XG5MM.EXE: Gen5 (Ironlake) read-only MMIO and GTT snapshot.
 *
 * Phase 1 of docs\plans\intel-gen4-gen5-bringup.md (Order, step 3) for
 * Gen5. Reads GTTMMADR and BSM from the IGD's config space and GGC, the
 * GTT stolen base and TOLUD from the host bridge's, loads G5MM.VXD, and
 * asks it for register offsets (each read twice) and the head of the GTT.
 * Writes C:\V9XDIAG\V9XG5MM.INI:
 *
 *   [Report]  build, config words, status, GGC's decode and the GTT size;
 *   [Mmio]    every offset read, as R<offset>=first[,second if it moved],
 *             with Name.<offset> from the Ironlake PRM (Linux 6.6
 *             i915_reg.h where the PRM gives none);
 *   [Display] which pipes, planes and PCH transcoders are live, and what
 *             was read for them;
 *   [Gtt]     the head of the table decoded by the 965 PTE layout, the
 *             run of entries mapping stolen memory from BSM, and where
 *             the table sits against stolen memory.
 *
 * Two captures, as V9XG4MM takes. The first reads the registers that are
 * safe whatever the display state: the command streamers, interrupts,
 * clock gating, watermarks, PCH DPLLs, ports, panel power, backlight, and
 * every PIPExCONF, DSPxCNTR, PF_CTRL and TRANSxCONF. The second reads
 * timings, M/N values, panel fitter windows and plane registers only for
 * the pipes, planes and transcoders the first found enabled - the 945's
 * lesson (docs\decisions\2026-10-15-a-disabled-pipes-counters-hang-the-945.md).
 * The counters themselves are outside the VxD's windows altogether.
 *
 * Ironlake's PRM defines neither GGC, BSM nor the global PTE (Vol 1 Part 2
 * 3.6.1 defers to the device specs), so those decodes follow Linux 6.6
 * intel-gtt.c and intel-agp.h, as V9XG4MM's follow the 965 datasheet.
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

#define V9X_G5MM_PATH        V9X_DIAG_PATH("V9XG5MM.INI")
#define V9X_G5MM_VXD         "\\\\.\\G5MM.VXD"
#define V9X_G5MM_MAGIC       0x4d4d3547ul
#define V9X_G5MM_CAPTURE     1u
#define V9X_G5MM_OFFSET_MAX  256u
#define V9X_G5MM_GTT_COPY    4096u
#define V9X_G5MM_HEADER      16u
#define V9X_G5MM_GTT_RAW     16u
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_HOST_CONFIG      0x80000000ul
#define V9X_IGD_CONFIG       0x80001000ul

/* Config offsets. IGD: 10h GTTMMADR, 18h GMADR, 5Ch BSM, 62h MSAC (in the
 * dword at 60h), FCh ASLS. Host: 52h GGC (in the dword at 50h), A4h the
 * stolen base again, A8h the GTT stolen base, B0h TOLUD. The host words
 * are as measured on W9QI2F4F
 * (docs\decisions\2026-10-22-ironlake-w9qi2f4f-audit.md), named by the
 * Ivy Bridge layout they match; the Ironlake datasheet was not to hand. */
#define V9X_CFG_GTTMMADR     0x10ul
#define V9X_CFG_GMADR        0x18ul
#define V9X_CFG_GGC          0x50ul
#define V9X_CFG_BSM          0x5cul
#define V9X_CFG_MSAC         0x60ul
#define V9X_CFG_ASLS         0xfcul
#define V9X_CFG_HOST_A0      0xa0ul
#define V9X_CFG_HOST_A4      0xa4ul
#define V9X_CFG_HOST_A8      0xa8ul
#define V9X_CFG_HOST_AC      0xacul
#define V9X_CFG_TOLUD        0xb0ul
#define V9X_GTTMMADR_MASK    0xffc00000ul
#define V9X_BSM_MASK         0xfff00000ul
#define V9X_TOLUD_MASK       0xfff0ul
#define V9X_TOLUD_SHIFT      16u

/* GGC, Linux intel-agp.h: GMS bits 7:4, GGMS bits 11:8. */
#define V9X_GGC_GMS_SHIFT    4u
#define V9X_GGC_GMS_MASK     0xful
#define V9X_GGC_GGMS_SHIFT   8u
#define V9X_GGC_GGMS_MASK    0xful

#define V9X_ST_IGD_INTEL       0x00000001ul
#define V9X_ST_BASE_INVALID    0x00000002ul
#define V9X_ST_MMIO_MAPPED     0x00000004ul
#define V9X_ST_GTT_MAPPED      0x00000008ul
#define V9X_ST_OFFSETS_REFUSED 0x00000010ul
#define V9X_ST_SNAPSHOT_TAKEN  0x00000020ul
#define V9X_ST_GTT_READ        0x00000040ul
#define V9X_ST_NOT_GEN5        0x00000080ul
#define V9X_ST_GTT_SIZE_UNKNOWN 0x00000100ul

/* 965 PTE layout, which intel-gtt.c i965_write_entry() writes on Gen5:
 * 31:12 address 31:12, 7:4 address 35:32, 2:1 type (0 uncached main
 * memory, 3 snooped), 0 valid. */
#define V9X_PTE_VALID        0x00000001ul
#define V9X_PTE_TYPE_MASK    0x00000006ul
#define V9X_PTE_TYPE_SHIFT   1u
#define V9X_PTE_HIGH_MASK    0x000000f0ul
#define V9X_PTE_MBZ_MASK     0x00000f08ul
#define V9X_PTE_ADDRESS_MASK 0xfffff000ul
#define V9X_PAGE_BYTES       0x1000ul

#define V9X_ENABLE_BIT       0x80000000ul
#define V9X_STATE_BIT        0x40000000ul
#define V9X_PLANE_PIPE_SHIFT 24u
#define V9X_PLANE_PIPE_MASK  0x3ul

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_g5mm_register {
    DWORD offset;
    const char *name;
};

/* Read in the first capture, whatever the display state. */
static const struct v9x_g5mm_register v9x_global[] = {
    { 0x02020ul, "PGTBL_CTL" },
    { 0x02024ul, "PGTBL_ER" },
    { 0x02028ul, "EXCC" },
    { 0x02030ul, "RCS_TAIL" },
    { 0x02034ul, "RCS_HEAD" },
    { 0x02038ul, "RCS_START" },
    { 0x0203cul, "RCS_CTL" },
    { 0x02064ul, "IPEIR" },
    { 0x02068ul, "IPEHR" },
    { 0x0206cul, "INSTDONE" },
    { 0x02070ul, "INSTPS" },
    { 0x02074ul, "ACTHD" },
    { 0x02078ul, "DMA_FADD" },
    { 0x0207cul, "INSTDONE1" },
    { 0x02080ul, "HWS_PGA" },
    { 0x02098ul, "HWSTAM" },
    { 0x0209cul, "MI_MODE" },
    { 0x020a8ul, "IMR" },
    { 0x020b0ul, "EIR" },
    { 0x020b4ul, "EMR" },
    { 0x020b8ul, "ESR" },
    { 0x020c0ul, "INSTPM" },
    { 0x020c4ul, "PGTBL_CTL2" },
    { 0x04030ul, "BSD_TAIL" },
    { 0x04034ul, "BSD_HEAD" },
    { 0x04038ul, "BSD_START" },
    { 0x0403cul, "BSD_CTL" },
    { 0x04074ul, "BSD_ACTHD" },
    { 0x04080ul, "BSD_HWS_PGA" },
    { 0x0409cul, "BSD_MI_MODE" },
    { 0x41000ul, "VGACNTRL" },
    { 0x42004ul, "ILK_DISPLAY_CHICKEN2" },
    { 0x42020ul, "ILK_DSPCLK_GATE_D" },
    { 0x44000ul, "DEISR" },
    { 0x44004ul, "DEIMR" },
    { 0x44008ul, "DEIIR" },
    { 0x4400cul, "DEIER" },
    { 0x44010ul, "GTISR" },
    { 0x44014ul, "GTIMR" },
    { 0x44018ul, "GTIIR" },
    { 0x4401cul, "GTIER" },
    { 0x44030ul, "CPU_HOTPLUG" },
    { 0x44040ul, "GTT_FAULT" },
    { 0x45000ul, "DISP_ARB_CTL" },
    { 0x45100ul, "WM0_PIPE_A" },
    { 0x45104ul, "WM0_PIPE_B" },
    { 0x45108ul, "WM1" },
    { 0x4510cul, "WM2" },
    { 0x45110ul, "WM3" },
    { 0x45120ul, "WM1S" },
    { 0x46000ul, "FDIPLL0" },
    { 0x4600cul, "DPPLL0" },
    { 0x46020ul, "3DCGDIS0" },
    { 0x46024ul, "3DCGDIS1" },
    { 0x46028ul, "3DRAMCGDIS0" },
    { 0x46030ul, "FDIPLLFREQCTL" },
    { 0x48250ul, "BLC_PWM_CTL2" },
    { 0x48254ul, "BLC_PWM_CTL" },
    { 0x68060ul, "PF_PWR_GATE_CTRL" },
    { 0x68080ul, "PF_CTRL_1_A" },
    { 0x68880ul, "PF_CTRL_1_B" },
    { 0x70008ul, "PIPEACONF" },
    { 0x71008ul, "PIPEBCONF" },
    { 0x70180ul, "DSPACNTR" },
    { 0x71180ul, "DSPBCNTR" },
    { 0xc2020ul, "SOUTH_DSPCLK_GATE_D" },
    { 0xc4000ul, "SDEISR" },
    { 0xc4004ul, "SDEIMR" },
    { 0xc4008ul, "SDEIIR" },
    { 0xc400cul, "SDEIER" },
    { 0xc4030ul, "PCH_HOTPLUG" },
    { 0xc6014ul, "DPLLA_CTRL" },
    { 0xc6018ul, "DPLLB_CTRL" },
    { 0xc6040ul, "FPA0" },
    { 0xc6044ul, "FPA1" },
    { 0xc6048ul, "FPB0" },
    { 0xc604cul, "FPB1" },
    { 0xc6200ul, "DREF_CONTROL" },
    { 0xc6204ul, "RAWCLK_FREQ" },
    { 0xc7200ul, "PP_STATUS" },
    { 0xc7204ul, "PP_CONTROL" },
    { 0xc7208ul, "PP_ON_DELAYS" },
    { 0xc720cul, "PP_OFF_DELAYS" },
    { 0xc7210ul, "PP_DIVISOR" },
    { 0xc8250ul, "BLC_PWM_PCH_CTL1" },
    { 0xc8254ul, "BLC_PWM_PCH_CTL2" },
    { 0xe1100ul, "ADPA" },
    { 0xe1140ul, "HDMIB" },
    { 0xe1150ul, "HDMIC" },
    { 0xe1160ul, "HDMID" },
    { 0xe1180ul, "LVDS" },
    { 0xe4100ul, "DPB" },
    { 0xe4200ul, "DPC" },
    { 0xe4300ul, "DPD" },
    { 0xf0008ul, "TRANSACONF" },
    { 0xf000cul, "FDI_RXA_CTL" },
    { 0xf0010ul, "FDI_RXA_MISC" },
    { 0xf0014ul, "FDI_RXA_ISR" },
    { 0xf1008ul, "TRANSBCONF" },
    { 0xf100cul, "FDI_RXB_CTL" },
    { 0xf1010ul, "FDI_RXB_MISC" },
    { 0xf1014ul, "FDI_RXB_ISR" }
};

/* Per live CPU pipe; offsets are pipe A's, pipe B adds the stride. */
static const struct v9x_g5mm_register v9x_pipe[] = {
    { 0x60000ul, "HTOTAL" },
    { 0x60004ul, "HBLANK" },
    { 0x60008ul, "HSYNC" },
    { 0x6000cul, "VTOTAL" },
    { 0x60010ul, "VBLANK" },
    { 0x60014ul, "VSYNC" },
    { 0x6001cul, "PIPESRC" },
    { 0x60028ul, "VSYNCSHIFT" },
    { 0x60030ul, "DATA_M1" },
    { 0x60034ul, "DATA_N1" },
    { 0x60040ul, "LINK_M1" },
    { 0x60044ul, "LINK_N1" }
};
#define V9X_PIPE_STRIDE 0x1000ul

/* Per live pipe, its panel fitter window; pipe B adds 800h. */
static const struct v9x_g5mm_register v9x_fitter[] = {
    { 0x68070ul, "PF_WIN_POS" },
    { 0x68074ul, "PF_WIN_SZ" }
};
#define V9X_FITTER_STRIDE 0x800ul

/* Per live plane. */
static const struct v9x_g5mm_register v9x_plane[] = {
    { 0x70184ul, "DSPLINOFF" },
    { 0x70188ul, "DSPSTRIDE" },
    { 0x7019cul, "DSPSURF" },
    { 0x701a4ul, "DSPTILEOFF" }
};

/* Per live PCH transcoder; B adds 1000h. */
static const struct v9x_g5mm_register v9x_transcoder[] = {
    { 0xe0000ul, "TRANS_HTOTAL" },
    { 0xe0004ul, "TRANS_HBLANK" },
    { 0xe0008ul, "TRANS_HSYNC" },
    { 0xe000cul, "TRANS_VTOTAL" },
    { 0xe0010ul, "TRANS_VBLANK" },
    { 0xe0014ul, "TRANS_VSYNC" },
    { 0xe0028ul, "TRANS_VSYNCSHIFT" },
    { 0xe0030ul, "TRANS_DATA_M1" },
    { 0xe0034ul, "TRANS_DATA_N1" },
    { 0xe0040ul, "TRANS_LINK_M1" },
    { 0xe0044ul, "TRANS_LINK_N1" }
};
#define V9X_TRANSCODER_STRIDE 0x1000ul

struct v9x_g5mm_request {
    DWORD base;
    DWORD count;
    DWORD offsets[V9X_G5MM_OFFSET_MAX];
};

struct v9x_g5mm_result {
    DWORD magic;
    DWORD status;
    DWORD igd_id;
    DWORD base;
    DWORD accepted;
    DWORD gtt_valid;
    DWORD cf8;
    DWORD refused_offset;
    DWORD host_ggc_dword;
    DWORD gtt_bytes;
    DWORD gtt_entries;
    DWORD reserved[5];
    DWORD offsets[V9X_G5MM_OFFSET_MAX];
    DWORD first[V9X_G5MM_OFFSET_MAX];
    DWORD second[V9X_G5MM_OFFSET_MAX];
    DWORD gtt[V9X_G5MM_GTT_COPY];
};

/* The header must be the VxD's G5_HEADER_DWORDS long. */
typedef char v9x_g5mm_header_matches[
    offsetof(struct v9x_g5mm_result, offsets) ==
    sizeof(DWORD) * V9X_G5MM_HEADER ? 1 : -1];

static struct v9x_g5mm_request v9x_request;
static struct v9x_g5mm_result v9x_result;

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
    WritePrivateProfileStringA(section, key, text, V9X_G5MM_PATH);
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_G5MM_PATH);
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
    if (v9x_request.count < V9X_G5MM_OFFSET_MAX) {
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

    device = CreateFileA(V9X_G5MM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        return 0;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_G5MM_CAPTURE, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_G5MM_MAGIC) {
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
 * entries with the 35:32 extension set, and entries with MBZ bits set.
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
    if (copied > V9X_G5MM_GTT_COPY) {
        copied = V9X_G5MM_GTT_COPY;
    }
    for (index = 0ul; index < V9X_G5MM_GTT_RAW && index < copied; ++index) {
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

/*
 * GGC's two fields against the other stolen-memory words. GMS by Linux
 * intel-gtt.c intel_stolen_size(): 1, 4, 8, 16, 32, 48, 64, 128, 256,
 * 96, 160, 224, 352 MB for 1 to 0Dh. The GTT's own size is what the VxD
 * mapped from GGMS.
 */
static void v9x_publish_stolen(DWORD ggc, DWORD bsm, DWORD gsm, DWORD tolud)
{
    static const DWORD stolen_mb[16] = { 0ul, 1ul, 4ul, 8ul, 16ul, 32ul,
                                         48ul, 64ul, 128ul, 256ul, 96ul,
                                         160ul, 224ul, 352ul, 0ul, 0ul };
    DWORD stolen;
    DWORD base;
    DWORD top;

    stolen = stolen_mb[(ggc >> V9X_GGC_GMS_SHIFT) & V9X_GGC_GMS_MASK] << 20;
    base = bsm & V9X_BSM_MASK;
    top = (tolud & V9X_TOLUD_MASK) << V9X_TOLUD_SHIFT;
    v9x_key("Gtt", "Gms", (ggc >> V9X_GGC_GMS_SHIFT) & V9X_GGC_GMS_MASK);
    v9x_key("Gtt", "Ggms", (ggc >> V9X_GGC_GGMS_SHIFT) & V9X_GGC_GGMS_MASK);
    v9x_key("Gtt", "StolenBytesFromGgc", stolen);
    v9x_key("Gtt", "TableBytes", v9x_result.gtt_bytes);
    v9x_key("Gtt", "TableEntries", v9x_result.gtt_entries);
    v9x_key("Gtt", "ValidWholeTable", v9x_result.gtt_valid);
    v9x_key("Gtt", "Tolud", top);
    v9x_text("Gtt", "StolenEndsAtTolud",
             stolen != 0ul && base + stolen == top ? "yes" : "no");
    v9x_key("Gtt", "TableStolenBase", gsm & V9X_BSM_MASK);
    v9x_text("Gtt", "TableStolenBelowData",
             v9x_result.gtt_bytes != 0ul &&
             (gsm & V9X_BSM_MASK) + v9x_result.gtt_bytes == base ?
             "yes" : "no");
}

/* Second capture: the live pipes', planes' and transcoders' registers. */
static int v9x_live_display(DWORD base, const DWORD *pipe_conf,
                            const DWORD *plane_cntr, const DWORD *trans_conf)
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
        if ((pipe_conf[pipe] & V9X_ENABLE_BIT) == 0ul ||
            (pipe_conf[pipe] & V9X_STATE_BIT) == 0ul) {
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
        for (index = 0ul;
             index < sizeof(v9x_fitter) / sizeof(v9x_fitter[0]); ++index) {
            offset = v9x_fitter[index].offset + pipe * V9X_FITTER_STRIDE;
            v9x_add(offset);
            v9x_name(offset, v9x_fitter[index].name,
                     pipe == 0ul ? "_A" : "_B");
        }
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
    for (pipe = 0ul; pipe < 2ul; ++pipe) {
        lstrcpyA(key, "Transcoder");
        key[10] = (char)('A' + pipe);
        key[11] = '\0';
        if ((trans_conf[pipe] & V9X_ENABLE_BIT) == 0ul ||
            (trans_conf[pipe] & V9X_STATE_BIT) == 0ul) {
            v9x_text("Display", key, "off");
            continue;
        }
        v9x_text("Display", key, "live");
        for (index = 0ul;
             index < sizeof(v9x_transcoder) / sizeof(v9x_transcoder[0]);
             ++index) {
            offset = v9x_transcoder[index].offset +
                     pipe * V9X_TRANSCODER_STRIDE;
            v9x_add(offset);
            v9x_name(offset, v9x_transcoder[index].name,
                     pipe == 0ul ? "_A" : "_B");
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

void __stdcall V9xGen5MmioEntry(void)
{
    DWORD bar0;
    DWORD bsm;
    DWORD ggc;
    DWORD gsm;
    DWORD tolud;
    DWORD index;
    DWORD pipe_conf[2];
    DWORD plane_cntr[2];
    DWORD trans_conf[2];

    DeleteFileA(V9X_G5MM_PATH);
    v9x_text("Report", "Tool", "V9XG5MM");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Access", "mmio-read-only");

    bar0 = v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GTTMMADR);
    bsm = v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_BSM);
    ggc = (v9x_config_read(V9X_HOST_CONFIG, V9X_CFG_GGC) >> 16) & 0xfffful;
    gsm = v9x_config_read(V9X_HOST_CONFIG, V9X_CFG_HOST_A8);
    tolud = v9x_config_read(V9X_HOST_CONFIG, V9X_CFG_TOLUD) & 0xfffful;
    v9x_key("Report", "Bar0", bar0);
    v9x_key("Report", "Bar0High",
            v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GTTMMADR + 4ul));
    v9x_key("Report", "Bar2", v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GMADR));
    v9x_key("Report", "Bsm", bsm);
    v9x_key("Report", "Ggc", ggc);
    v9x_key("Report", "Msac",
            (v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_MSAC) >> 16) & 0xfful);
    v9x_key("Report", "Asls", v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_ASLS));
    v9x_key("Report", "HostA0",
            v9x_config_read(V9X_HOST_CONFIG, V9X_CFG_HOST_A0));
    v9x_key("Report", "HostA4",
            v9x_config_read(V9X_HOST_CONFIG, V9X_CFG_HOST_A4));
    v9x_key("Report", "HostA8", gsm);
    v9x_key("Report", "HostAC",
            v9x_config_read(V9X_HOST_CONFIG, V9X_CFG_HOST_AC));
    v9x_key("Report", "HostB0", tolud);

    v9x_request.base = bar0 & V9X_GTTMMADR_MASK;
    v9x_request.count = 0ul;
    for (index = 0ul; index < sizeof(v9x_global) / sizeof(v9x_global[0]);
         ++index) {
        v9x_add(v9x_global[index].offset);
        v9x_name(v9x_global[index].offset, v9x_global[index].name, "");
    }
    if (!v9x_capture()) {
        v9x_key("Report", "Status", v9x_result.status);
        WritePrivateProfileStringA(0, 0, 0, V9X_G5MM_PATH);
        ExitProcess(2u);
    }

    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "IgdId", v9x_result.igd_id);
    v9x_key("Report", "Base", v9x_result.base);
    v9x_key("Report", "Accepted", v9x_result.accepted);
    v9x_key("Report", "RefusedOffset", v9x_result.refused_offset);
    v9x_key("Report", "HostGgcDword", v9x_result.host_ggc_dword);
    v9x_publish_mmio();
    v9x_publish_stolen(ggc, bsm, gsm, tolud);
    if ((v9x_result.status & V9X_ST_GTT_READ) != 0ul) {
        v9x_publish_gtt(bsm);
    }

    pipe_conf[0] = v9x_value(0x70008ul);
    pipe_conf[1] = v9x_value(0x71008ul);
    plane_cntr[0] = v9x_value(0x70180ul);
    plane_cntr[1] = v9x_value(0x71180ul);
    trans_conf[0] = v9x_value(0xf0008ul);
    trans_conf[1] = v9x_value(0xf1008ul);
    if (!v9x_live_display(bar0 & V9X_GTTMMADR_MASK, pipe_conf, plane_cntr,
                          trans_conf)) {
        v9x_text("Report", "Result", "DISPLAY-CAPTURE-FAILED");
        WritePrivateProfileStringA(0, 0, 0, V9X_G5MM_PATH);
        ExitProcess(3u);
    }

    v9x_text("Report", "Result", "COMPLETE");
    WritePrivateProfileStringA(0, 0, 0, V9X_G5MM_PATH);
    ExitProcess(0u);
}
