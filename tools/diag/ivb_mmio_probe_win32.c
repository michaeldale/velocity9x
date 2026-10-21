/*
 * V9XIVMM.EXE: Ivy Bridge (Gen7) read-only display, PCH and GTT snapshot.
 *
 * Step 3 of docs\plans\intel-ivybridge-gen7.md. Reads GTTMMADR, BDSM, MSAC
 * and ASLS from the IGD's config space and GGC from the host bridge's,
 * loads IVMM.VXD, and asks it for register offsets (each read twice) and
 * the GTT. Writes C:\V9XDIAG\V9XIVMM.INI:
 *
 *   [Report]    build, config words, status, the GTT's size;
 *   [Mmio]      every offset read, as R<offset>=first[,second if it moved],
 *               with Name.<offset> from the IVB PRM Vol 3 Pt 3/4;
 *   [Forcewake] the three acknowledge registers, raw;
 *   [Display]   which pipes, planes, transcoders and ports are on;
 *   [Gtt]       the GTT against stolen memory, decoded by the IVB PTE
 *               layout (PRM Vol 1 Pt 2 2.2.2.1), and whether the entries
 *               the Haswell engine open checks hold the same mapping here.
 *
 * Two captures. The first reads what is safe whatever the display state:
 * every PIPE_CONF, PRI_CTL and TRANS_CONF, the ports, PCH clocks, FDI and
 * panel-fitter controls, watermarks, forcewake acknowledges. The second
 * reads timings, link M/N and plane registers only for the pipes, planes
 * and transcoders the first found enabled - the rule the Gen4 probe
 * follows after the 945's disabled-pipe hang
 * (docs\decisions\2026-10-15-a-disabled-pipes-counters-hang-the-945.md).
 * Scanline and frame counters are outside the VxD's windows altogether.
 *
 * Without arguments no GT register is read (below 40000h or from 116000h
 * up, except the forcewake acknowledges), so no forcewake is taken, and
 * nothing on the device is written: see the VxD's header.
 *
 * With /gt on the command line it then runs the VxD's second call: i915's
 * Ivy Bridge forcewake handshake, a fixed list of GT registers read twice
 * under it (rings, MI_MODE, INSTPM, GFX_MODE, RC6 and RPS control, ECOBUS,
 * L3, GT FIFO and status), and the forcewake put - restoring bit 0 if the
 * BIOS had it acknowledged on entry. Those two request registers are the
 * only device writes, and only with /gt. Results go in [Gt].
 *
 * The only config access is mechanism #1 reads, CF8h restored after each,
 * as in V9XHSW.EXE.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_IVMM_PATH        V9X_DIAG_PATH("V9XIVMM.INI")
#define V9X_IVMM_VXD         "\\\\.\\IVMM.VXD"
#define V9X_IVMM_MAGIC       0x4d4d5649ul
#define V9X_IVMM_CAPTURE     1u
#define V9X_IVMM_OFFSET_MAX  256u
#define V9X_IVMM_GTT_COPY    16384u
#define V9X_IVMM_HEADER      16u
#define V9X_IVMM_GTT_RAW     16u
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_HOST_CONFIG      0x80000000ul
#define V9X_IGD_CONFIG       0x80001000ul

/* Config offsets: datasheet 326765 (GGC host 50h, TOLUD BCh) and PRM
 * Vol 3 Pt 2 (GTTMMADR 10h, GMADR 18h, BDSM 5Ch, MSAC 62h, ASLS FCh). */
#define V9X_CFG_GTTMMADR     0x10ul
#define V9X_CFG_GMADR        0x18ul
#define V9X_CFG_GGC          0x50ul
#define V9X_CFG_BDSM         0x5cul
#define V9X_CFG_MSAC         0x60ul /* dword holding 62h */
#define V9X_CFG_ASLS         0xfcul
#define V9X_GTTMMADR_MASK    0xffc00000ul
#define V9X_BDSM_MASK        0xfff00000ul

#define V9X_ST_IGD_INTEL       0x00000001ul
#define V9X_ST_BASE_INVALID    0x00000002ul
#define V9X_ST_MMIO_MAPPED     0x00000004ul
#define V9X_ST_GTT_MAPPED      0x00000008ul
#define V9X_ST_OFFSETS_REFUSED 0x00000010ul
#define V9X_ST_SNAPSHOT_TAKEN  0x00000020ul
#define V9X_ST_GTT_READ        0x00000040ul
#define V9X_ST_NOT_IVB         0x00000080ul
#define V9X_ST_GGMS_UNUSABLE   0x00000100ul

/* IVB PRM Vol 1 Pt 2 2.2.2.1, the GTT entry: 31:12 address 31:12, 11:4
 * address 39:32, 3 GFDT, 2:1 cacheability (00 reserved, 01 uncached,
 * 10 LLC, 11 LLC and MLC), 0 valid. */
#define V9X_PTE_VALID        0x00000001ul
#define V9X_PTE_CACHE_MASK   0x00000006ul
#define V9X_PTE_CACHE_SHIFT  1u
#define V9X_PTE_GFDT         0x00000008ul
#define V9X_PTE_HIGH_MASK    0x00000ff0ul
#define V9X_PTE_ADDRESS_MASK 0xfffff000ul
#define V9X_PAGE_BYTES       0x1000ul

/* The Haswell engine open (loader.asm) requires GTT entries 6144-6175 to
 * map stolen memory from BDSM + 1800000h linearly, compared under
 * V9X_HSW_PTE_MASK. The same test on IVB words says whether the HSW
 * reserve placement could be reused unchanged. */
#define V9X_HSW_CHECK_FIRST  6144ul
#define V9X_HSW_CHECK_COUNT  32ul
#define V9X_HSW_CHECK_OFFSET 0x01800000ul
#define V9X_HSW_PTE_MASK     0xfffff7f1ul

#define V9X_ENABLE_BIT       0x80000000ul
#define V9X_FORCEWAKE_BIT    0x00000001ul

/* The VxD's second call; layout matches IVGtResult in the VxD. */
#define V9X_IVGT_CAPTURE     2u
#define V9X_IVGT_MAGIC       0x54475649ul
#define V9X_IVGT_COUNT       30u
#define V9X_IVGT_HEADER      24u
#define V9X_ECOBUS           0xa180ul
#define V9X_FORCEWAKE_MT_ENABLE 0x00000020ul /* ECOBUS bit 5, i915 */
#define V9X_RC_CONTROL       0xa090ul
#define V9X_GT_CORE_STATUS   0x138060ul
#define V9X_RC_STATE_MASK    0x00000007ul /* GEN6_RCn_MASK, i915 */

#define V9X_GT_ST_ACK_STUCK      0x00000010ul
#define V9X_GT_ST_SET_TIMEOUT    0x00000020ul
#define V9X_GT_ST_THREAD_TIMEOUT 0x00000040ul
#define V9X_GT_ST_READ_DONE      0x00000080ul
#define V9X_GT_ST_PUT_TIMEOUT    0x00000100ul
#define V9X_GT_ST_RESTORED       0x00000200ul
#define V9X_GT_ST_RESTORE_TIMEOUT 0x00000400ul

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_ivmm_register {
    DWORD offset;
    const char *name;
};

/* Read in the first capture, whatever the display state. */
static const struct v9x_ivmm_register v9x_global[] = {
    { 0x041000ul, "VGA_CONTROL" },
    { 0x045000ul, "ARB_CTL" },
    { 0x045004ul, "ARB_CTL2" },
    { 0x045010ul, "MSG_CTL" },
    { 0x045100ul, "WM0_PIPE_A" },
    { 0x045104ul, "WM0_PIPE_B" },
    { 0x045200ul, "WM0_PIPE_C" },
    { 0x045108ul, "WM_LP1" },
    { 0x04510cul, "WM_LP2" },
    { 0x045110ul, "WM_LP3" },
    { 0x045120ul, "WM_LP1_SPR" },
    { 0x045124ul, "WM_LP2_SPR" },
    { 0x045128ul, "WM_LP3_SPR" },
    { 0x060100ul, "FDI_TX_CTL_A" },
    { 0x061100ul, "FDI_TX_CTL_B" },
    { 0x062100ul, "FDI_TX_CTL_C" },
    { 0x064000ul, "DP_CTL_A" },
    { 0x068070ul, "PF_WIN_POS_0" },
    { 0x068074ul, "PF_WIN_SZ_0" },
    { 0x068080ul, "PF_CTL_0" },
    { 0x068870ul, "PF_WIN_POS_1" },
    { 0x068874ul, "PF_WIN_SZ_1" },
    { 0x068880ul, "PF_CTL_1" },
    { 0x070008ul, "PIPE_CONF_A" },
    { 0x071008ul, "PIPE_CONF_B" },
    { 0x072008ul, "PIPE_CONF_C" },
    { 0x070180ul, "PRI_CTL_A" },
    { 0x071180ul, "PRI_CTL_B" },
    { 0x072180ul, "PRI_CTL_C" },
    { 0x0c6014ul, "PCH_DPLL_A" },
    { 0x0c6018ul, "PCH_DPLL_B" },
    { 0x0c6040ul, "PCH_FP0_A" },
    { 0x0c6044ul, "PCH_FP1_A" },
    { 0x0c6048ul, "PCH_FP0_B" },
    { 0x0c604cul, "PCH_FP1_B" },
    { 0x0c6200ul, "DREF_CTL" },
    { 0x0c6204ul, "RAWCLK_FREQ" },
    { 0x0c7000ul, "DPLL_SEL" },
    { 0x0e1100ul, "DAC_CTL" },
    { 0x0e1140ul, "HDMI_CTL_B" },
    { 0x0e1150ul, "HDMI_CTL_C" },
    { 0x0e1160ul, "HDMI_CTL_D" },
    { 0x0e1180ul, "LVDS_CTL" },
    { 0x0e4100ul, "DP_CTL_B" },
    { 0x0e4200ul, "DP_CTL_C" },
    { 0x0e4300ul, "DP_CTL_D" },
    { 0x0f0008ul, "TRANS_CONF_A" },
    { 0x0f1008ul, "TRANS_CONF_B" },
    { 0x0f2008ul, "TRANS_CONF_C" },
    { 0x0f000cul, "FDI_RX_CTL_A" },
    { 0x0f100cul, "FDI_RX_CTL_B" },
    { 0x0f200cul, "FDI_RX_CTL_C" },
    { 0x130040ul, "FORCEWAKE_MT_ACK" },
    { 0x130044ul, "FORCEWAKE_ACK_HSW" },
    { 0x130090ul, "FORCEWAKE_ACK" }
};

/* Per live CPU pipe; offsets are pipe A's, B and C add the stride. */
static const struct v9x_ivmm_register v9x_pipe[] = {
    { 0x060000ul, "PIPE_HTOTAL" },
    { 0x060004ul, "PIPE_HBLANK" },
    { 0x060008ul, "PIPE_HSYNC" },
    { 0x06000cul, "PIPE_VTOTAL" },
    { 0x060010ul, "PIPE_VBLANK" },
    { 0x060014ul, "PIPE_VSYNC" },
    { 0x06001cul, "PIPE_SRCSZ" },
    { 0x060028ul, "PIPE_VSYNCSHIFT" },
    { 0x060030ul, "PIPE_DATAM1" },
    { 0x060034ul, "PIPE_DATAN1" },
    { 0x060040ul, "PIPE_LINKM1" },
    { 0x060044ul, "PIPE_LINKN1" }
};
#define V9X_PIPE_STRIDE 0x1000ul

/* Per live primary plane; IVB PRM Vol 3 Pt 3 5.3. */
static const struct v9x_ivmm_register v9x_plane[] = {
    { 0x070184ul, "PRI_LINOFF" },
    { 0x070188ul, "PRI_STRIDE" },
    { 0x07019cul, "PRI_SURF" },
    { 0x0701a4ul, "PRI_TILEOFF" },
    { 0x0701acul, "PRI_SURFLIVE" }
};

/* Per live PCH transcoder. */
static const struct v9x_ivmm_register v9x_trans[] = {
    { 0x0e0000ul, "TRANS_HTOTAL" },
    { 0x0e0004ul, "TRANS_HBLANK" },
    { 0x0e0008ul, "TRANS_HSYNC" },
    { 0x0e000cul, "TRANS_VTOTAL" },
    { 0x0e0010ul, "TRANS_VBLANK" },
    { 0x0e0014ul, "TRANS_VSYNC" },
    { 0x0e0300ul, "TRANS_DP_CTL" }
};
#define V9X_TRANS_STRIDE 0x1000ul

static const char *const v9x_suffix[3] = { "_A", "_B", "_C" };

/* Ports whose enable bit is 31, and the [Display] key each reports to. */
static const struct v9x_ivmm_register v9x_port[] = {
    { 0x0e1100ul, "PortCrt" },
    { 0x0e1140ul, "PortHdmiB" },
    { 0x0e1150ul, "PortHdmiC" },
    { 0x0e1160ul, "PortHdmiD" },
    { 0x0e1180ul, "PortLvds" },
    { 0x064000ul, "PortDpA" },
    { 0x0e4100ul, "PortDpB" },
    { 0x0e4200ul, "PortDpC" },
    { 0x0e4300ul, "PortDpD" }
};

struct v9x_ivmm_request {
    DWORD base;
    DWORD count;
    DWORD offsets[V9X_IVMM_OFFSET_MAX];
};

struct v9x_ivmm_result {
    DWORD magic;
    DWORD status;
    DWORD igd_id;
    DWORD base;
    DWORD accepted;
    DWORD gtt_valid;
    DWORD cf8;
    DWORD refused_offset;
    DWORD ggc;
    DWORD gtt_bytes;
    DWORD gtt_entries;
    DWORD reserved[5];
    DWORD offsets[V9X_IVMM_OFFSET_MAX];
    DWORD first[V9X_IVMM_OFFSET_MAX];
    DWORD second[V9X_IVMM_OFFSET_MAX];
    DWORD gtt[V9X_IVMM_GTT_COPY];
};

/* The header must be the VxD's IV_HEADER_DWORDS long. */
typedef char v9x_ivmm_header_matches[
    offsetof(struct v9x_ivmm_result, offsets) ==
    sizeof(DWORD) * V9X_IVMM_HEADER ? 1 : -1];

struct v9x_ivgt_result {
    DWORD magic;
    DWORD status;
    DWORD igd_id;
    DWORD base;
    DWORD entry_mt_ack;
    DWORD entry_legacy_ack;
    DWORD entry_thread;
    DWORD ecobus_unwoken;
    DWORD polls_clear;
    DWORD polls_set;
    DWORD polls_thread;
    DWORD polls_put;
    DWORD polls_restore;
    DWORD exit_mt_ack;
    DWORD exit_legacy_ack;
    DWORD exit_thread;
    DWORD read_count;
    DWORD timeout_ack;
    DWORD reserved[6];
    DWORD offsets[V9X_IVGT_COUNT];
    DWORD first[V9X_IVGT_COUNT];
    DWORD second[V9X_IVGT_COUNT];
};

typedef char v9x_ivgt_header_matches[
    offsetof(struct v9x_ivgt_result, offsets) ==
    sizeof(DWORD) * V9X_IVGT_HEADER ? 1 : -1];

/* Names for the VxD's IVGtOffsets, in its order. */
static const char *const v9x_gt_names[V9X_IVGT_COUNT] = {
    "RCS_TAIL", "RCS_HEAD", "RCS_START", "RCS_CTL", "RCS_ACTHD",
    "MI_MODE", "INSTPM", "GFX_MODE",
    "BCS_TAIL", "BCS_HEAD", "BCS_START", "BCS_CTL", "BCS_ACTHD",
    "GAM_ECOCHK", "GEN7_COMMON_SLICE_CHICKEN1",
    "GEN6_RPSTAT1", "GEN6_RP_CONTROL", "GEN6_RC_CONTROL", "GEN6_RC_STATE",
    "ECOBUS",
    "GEN7_L3CNTLREG1", "GEN7_L3CNTLREG2", "GEN7_L3CNTLREG3",
    "GEN7_L3_CHICKEN_MODE", "GEN7_L3SQCREG4",
    "GTFIFODBG", "GTFIFOCTL", "GT_THREAD_STATUS", "GT_CORE_STATUS",
    "GEN6_GT_GFX_RC6"
};

static struct v9x_ivmm_request v9x_request;
static struct v9x_ivmm_result v9x_result;
static struct v9x_ivgt_result v9x_gt;

/* The first capture's words, kept for the decisions the second needs. */
static DWORD v9x_global_offsets[V9X_IVMM_OFFSET_MAX];
static DWORD v9x_global_values[V9X_IVMM_OFFSET_MAX];
static DWORD v9x_global_count;

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
    WritePrivateProfileStringA(section, key, text, V9X_IVMM_PATH);
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_IVMM_PATH);
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
    if (v9x_request.count < V9X_IVMM_OFFSET_MAX) {
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

/* The first capture's value at an offset, or 0. */
static DWORD v9x_value(DWORD offset)
{
    DWORD index;

    for (index = 0ul; index < v9x_global_count; ++index) {
        if (v9x_global_offsets[index] == offset) {
            return v9x_global_values[index];
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

    device = CreateFileA(V9X_IVMM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        return 0;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_IVMM_CAPTURE, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_IVMM_MAGIC) {
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

static void v9x_keep_global(void)
{
    DWORD index;

    v9x_global_count = v9x_result.accepted;
    for (index = 0ul; index < v9x_global_count; ++index) {
        v9x_global_offsets[index] = v9x_result.offsets[index];
        v9x_global_values[index] = v9x_result.first[index];
    }
}

/* Bit 0 of each acknowledge set means that forcewake is held now. */
static void v9x_publish_forcewake(void)
{
    v9x_key("Forcewake", "MtAck130040", v9x_value(0x130040ul));
    v9x_key("Forcewake", "HswAck130044", v9x_value(0x130044ul));
    v9x_key("Forcewake", "LegacyAck130090", v9x_value(0x130090ul));
    v9x_key("Forcewake", "MtHeld",
            v9x_value(0x130040ul) & V9X_FORCEWAKE_BIT);
    v9x_key("Forcewake", "LegacyHeld",
            v9x_value(0x130090ul) & V9X_FORCEWAKE_BIT);
}

static void v9x_publish_ports(void)
{
    DWORD index;

    for (index = 0ul; index < sizeof(v9x_port) / sizeof(v9x_port[0]);
         ++index) {
        v9x_text("Display", v9x_port[index].name,
                 (v9x_value(v9x_port[index].offset) & V9X_ENABLE_BIT) != 0ul ?
                 "on" : "off");
    }
}

/*
 * The GTT against stolen memory. The BIOS maps the VBE framebuffer through
 * the aperture, so entry 0 onward should be valid and point at consecutive
 * pages from BDSM. Counted: each cacheability value, GFDT, and entries
 * with address bits 39:32 set.
 */
static void v9x_publish_gtt(DWORD bdsm)
{
    char key[16];
    DWORD index;
    DWORD raw;
    DWORD run;
    DWORD stolen_run;
    DWORD expect;
    DWORD valid;
    DWORD copied;
    DWORD cache[4];
    DWORD high;
    DWORD gfdt;
    DWORD matched;

    copied = v9x_result.gtt_entries;
    if (copied > V9X_IVMM_GTT_COPY) {
        copied = V9X_IVMM_GTT_COPY;
    }
    for (index = 0ul; index < V9X_IVMM_GTT_RAW && index < copied; ++index) {
        lstrcpyA(key, "Entry");
        v9x_hex(key + 5, index, 4);
        v9x_key("Gtt", key, v9x_result.gtt[index]);
    }

    run = 0ul;
    stolen_run = 0ul;
    valid = 0ul;
    high = 0ul;
    gfdt = 0ul;
    cache[0] = cache[1] = cache[2] = cache[3] = 0ul;
    expect = bdsm & V9X_BDSM_MASK;
    for (index = 0ul; index < copied; ++index) {
        raw = v9x_result.gtt[index];
        if ((raw & V9X_PTE_VALID) == 0ul) {
            continue;
        }
        ++valid;
        ++cache[(raw & V9X_PTE_CACHE_MASK) >> V9X_PTE_CACHE_SHIFT];
        if ((raw & V9X_PTE_HIGH_MASK) != 0ul) {
            ++high;
        }
        if ((raw & V9X_PTE_GFDT) != 0ul) {
            ++gfdt;
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
    v9x_key("Gtt", "TableBytes", v9x_result.gtt_bytes);
    v9x_key("Gtt", "TableEntries", v9x_result.gtt_entries);
    v9x_key("Gtt", "ValidWholeTable", v9x_result.gtt_valid);
    v9x_key("Gtt", "CopiedEntries", copied);
    v9x_key("Gtt", "ValidInCopy", valid);
    v9x_key("Gtt", "ValidRunFromZero", run);
    v9x_key("Gtt", "StolenRunFromZero", stolen_run);
    v9x_key("Gtt", "StolenBase", bdsm & V9X_BDSM_MASK);
    v9x_key("Gtt", "Cache00Reserved", cache[0]);
    v9x_key("Gtt", "Cache01Uncached", cache[1]);
    v9x_key("Gtt", "Cache10Llc", cache[2]);
    v9x_key("Gtt", "Cache11LlcMlc", cache[3]);
    v9x_key("Gtt", "GfdtSet", gfdt);
    v9x_key("Gtt", "HighAddressSet", high);

    /* The Haswell open's test, on these words. */
    matched = 0ul;
    for (index = 0ul; index < V9X_HSW_CHECK_COUNT; ++index) {
        if (V9X_HSW_CHECK_FIRST + index >= copied) {
            break;
        }
        expect = ((bdsm & V9X_BDSM_MASK) + V9X_HSW_CHECK_OFFSET +
                  index * V9X_PAGE_BYTES) | V9X_PTE_VALID;
        if ((v9x_result.gtt[V9X_HSW_CHECK_FIRST + index] &
             V9X_HSW_PTE_MASK) == expect) {
            ++matched;
        }
    }
    v9x_key("Gtt", "HswOpenCheckMatched", matched);
    v9x_key("Gtt", "HswOpenCheckOf", V9X_HSW_CHECK_COUNT);
    if (V9X_HSW_CHECK_FIRST < copied) {
        v9x_key("Gtt", "Entry1800", v9x_result.gtt[V9X_HSW_CHECK_FIRST]);
    }

    /* Where the stolen run ends: the entries either side of the break,
     * and how many later entries in the copy repeat the first one past it
     * (a scratch page mapped over the rest would). */
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

/* Queue one register block for unit `unit` (0 = A) of a live pipe, plane or
 * transcoder, naming each offset. */
static void v9x_queue(const struct v9x_ivmm_register *block, DWORD count,
                      DWORD unit, DWORD stride)
{
    DWORD index;
    DWORD offset;

    for (index = 0ul; index < count; ++index) {
        offset = block[index].offset + unit * stride;
        v9x_add(offset);
        v9x_name(offset, block[index].name, v9x_suffix[unit]);
    }
}

/* Second capture: the live pipes' timings and link M/N, the live planes,
 * the live PCH transcoders. */
static int v9x_live_display(DWORD base)
{
    static const char *const pipe_key[3] = { "PipeA", "PipeB", "PipeC" };
    static const char *const plane_key[3] = { "PlaneA", "PlaneB", "PlaneC" };
    static const char *const trans_key[3] = { "TransA", "TransB", "TransC" };
    DWORD unit;
    DWORD live;

    v9x_request.base = base;
    v9x_request.count = 0ul;
    live = 0ul;
    for (unit = 0ul; unit < 3ul; ++unit) {
        if ((v9x_value(0x070008ul + unit * V9X_PIPE_STRIDE) &
             V9X_ENABLE_BIT) == 0ul) {
            v9x_text("Display", pipe_key[unit], "off");
            continue;
        }
        v9x_text("Display", pipe_key[unit], "on");
        ++live;
        v9x_queue(v9x_pipe, sizeof(v9x_pipe) / sizeof(v9x_pipe[0]), unit,
                  V9X_PIPE_STRIDE);
    }
    for (unit = 0ul; unit < 3ul; ++unit) {
        if ((v9x_value(0x070180ul + unit * V9X_PIPE_STRIDE) &
             V9X_ENABLE_BIT) == 0ul) {
            v9x_text("Display", plane_key[unit], "off");
            continue;
        }
        v9x_text("Display", plane_key[unit], "on");
        v9x_queue(v9x_plane, sizeof(v9x_plane) / sizeof(v9x_plane[0]), unit,
                  V9X_PIPE_STRIDE);
    }
    for (unit = 0ul; unit < 3ul; ++unit) {
        if ((v9x_value(0x0f0008ul + unit * V9X_TRANS_STRIDE) &
             V9X_ENABLE_BIT) == 0ul) {
            v9x_text("Display", trans_key[unit], "off");
            continue;
        }
        v9x_text("Display", trans_key[unit], "on");
        v9x_queue(v9x_trans, sizeof(v9x_trans) / sizeof(v9x_trans[0]), unit,
                  V9X_TRANS_STRIDE);
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

/* The value a GT capture read at an offset, or 0. */
static DWORD v9x_gt_value(DWORD offset)
{
    DWORD index;

    for (index = 0ul; index < v9x_gt.read_count && index < V9X_IVGT_COUNT;
         ++index) {
        if (v9x_gt.offsets[index] == offset) {
            return v9x_gt.first[index];
        }
    }
    return 0ul;
}

/* The VxD's second call: forcewake, GT reads, put. See the header. */
static void v9x_gt_capture(DWORD base)
{
    char key[40];
    char text[24];
    HANDLE device;
    DWORD returned;
    DWORD index;
    BOOL ok;

    v9x_text("Gt", "Requested", "yes");
    device = CreateFileA(V9X_IVMM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Gt", "Result", "VXD-LOAD-FAILED");
        return;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_IVGT_CAPTURE, &base, sizeof(base),
                         &v9x_gt, sizeof(v9x_gt), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_gt) || v9x_gt.magic != V9X_IVGT_MAGIC) {
        v9x_text("Gt", "Result", "DIOC-FAILED");
        v9x_key("Gt", "Returned", returned);
        return;
    }

    v9x_key("Gt", "Status", v9x_gt.status);
    v9x_key("Gt", "EntryMtAck", v9x_gt.entry_mt_ack);
    v9x_key("Gt", "EntryLegacyAck", v9x_gt.entry_legacy_ack);
    v9x_key("Gt", "EntryThreadStatus", v9x_gt.entry_thread);
    v9x_key("Gt", "EcobusBeforeWake", v9x_gt.ecobus_unwoken);
    v9x_key("Gt", "PollsAckClear", v9x_gt.polls_clear);
    v9x_key("Gt", "PollsAckSet", v9x_gt.polls_set);
    v9x_key("Gt", "PollsThreadC0", v9x_gt.polls_thread);
    v9x_key("Gt", "PollsPutClear", v9x_gt.polls_put);
    v9x_key("Gt", "PollsRestoreSet", v9x_gt.polls_restore);
    v9x_key("Gt", "ExitMtAck", v9x_gt.exit_mt_ack);
    v9x_key("Gt", "ExitLegacyAck", v9x_gt.exit_legacy_ack);
    v9x_key("Gt", "ExitThreadStatus", v9x_gt.exit_thread);
    v9x_key("Gt", "AckAtTimeout", v9x_gt.timeout_ack);
    v9x_key("Gt", "Read", v9x_gt.read_count);

    for (index = 0ul; index < v9x_gt.read_count && index < V9X_IVGT_COUNT;
         ++index) {
        v9x_offset_key(key, "R", v9x_gt.offsets[index]);
        v9x_hex(text, v9x_gt.first[index], 8);
        if (v9x_gt.second[index] != v9x_gt.first[index]) {
            text[8] = ',';
            v9x_hex(text + 9, v9x_gt.second[index], 8);
        }
        v9x_text("Gt", key, text);
        v9x_offset_key(key, "Name.", v9x_gt.offsets[index]);
        v9x_text("Gt", key, v9x_gt_names[index]);
    }

    if ((v9x_gt.status & V9X_GT_ST_ACK_STUCK) != 0ul) {
        v9x_text("Gt", "Result", "ACK-STUCK-REFUSED");
        return;
    }
    if ((v9x_gt.status & V9X_GT_ST_SET_TIMEOUT) != 0ul) {
        v9x_text("Gt", "Result", "ACK-SET-TIMEOUT");
        return;
    }
    if ((v9x_gt.status & V9X_GT_ST_THREAD_TIMEOUT) != 0ul) {
        v9x_text("Gt", "Result", "THREAD-C0-TIMEOUT");
        return;
    }
    if ((v9x_gt.status & V9X_GT_ST_READ_DONE) == 0ul) {
        v9x_text("Gt", "Result", "NOT-READ");
        return;
    }
    v9x_text("Gt", "MtForcewakeEnabled",
             (v9x_gt_value(V9X_ECOBUS) & V9X_FORCEWAKE_MT_ENABLE) != 0ul ?
             "yes" : "no");
    v9x_key("Gt", "RcControl", v9x_gt_value(V9X_RC_CONTROL));
    v9x_key("Gt", "RcStateNow",
            v9x_gt_value(V9X_GT_CORE_STATUS) & V9X_RC_STATE_MASK);
    if ((v9x_gt.status & (V9X_GT_ST_PUT_TIMEOUT |
                          V9X_GT_ST_RESTORE_TIMEOUT)) != 0ul) {
        v9x_text("Gt", "Result", "READ-BUT-EXIT-TIMEOUT");
        return;
    }
    v9x_text("Gt", "Result", "COMPLETE");
}

/* Whether the command line carries /gt (any case). */
static int v9x_wants_gt(void)
{
    const char *line;

    line = GetCommandLineA();
    for (; line != 0 && *line != '\0'; ++line) {
        if (line[0] == '/' && (line[1] == 'g' || line[1] == 'G') &&
            (line[2] == 't' || line[2] == 'T')) {
            return 1;
        }
    }
    return 0;
}

void __stdcall V9xIvbMmioEntry(void)
{
    DWORD bar0;
    DWORD bdsm;
    DWORD index;

    DeleteFileA(V9X_IVMM_PATH);
    v9x_text("Report", "Tool", "V9XIVMM");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Access",
             v9x_wants_gt() ? "mmio-read-plus-forcewake-handshake" :
                              "mmio-read-only-no-forcewake");

    bar0 = v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GTTMMADR);
    bdsm = v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_BDSM);
    v9x_key("Report", "Bar0", bar0);
    v9x_key("Report", "Bar0High",
            v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GTTMMADR + 4ul));
    v9x_key("Report", "Bar2", v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GMADR));
    v9x_key("Report", "Bdsm", bdsm);
    v9x_key("Report", "Ggc",
            v9x_config_read(V9X_HOST_CONFIG, V9X_CFG_GGC) & 0xfffful);
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
        v9x_key("Report", "RefusedOffset", v9x_result.refused_offset);
        WritePrivateProfileStringA(0, 0, 0, V9X_IVMM_PATH);
        ExitProcess(2u);
    }

    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "IgdId", v9x_result.igd_id);
    v9x_key("Report", "Base", v9x_result.base);
    v9x_key("Report", "GgcAsRead", v9x_result.ggc);
    v9x_key("Report", "Accepted", v9x_result.accepted);
    v9x_key("Report", "RefusedOffset", v9x_result.refused_offset);
    v9x_publish_mmio();
    v9x_keep_global();
    v9x_publish_forcewake();
    v9x_publish_ports();
    if ((v9x_result.status & V9X_ST_GTT_READ) != 0ul) {
        v9x_publish_gtt(bdsm);
    }

    if (!v9x_live_display(bar0 & V9X_GTTMMADR_MASK)) {
        v9x_text("Report", "Result", "DISPLAY-CAPTURE-FAILED");
        WritePrivateProfileStringA(0, 0, 0, V9X_IVMM_PATH);
        ExitProcess(3u);
    }

    if (v9x_wants_gt()) {
        v9x_gt_capture(bar0 & V9X_GTTMMADR_MASK);
    }

    v9x_text("Report", "Result", "COMPLETE");
    WritePrivateProfileStringA(0, 0, 0, V9X_IVMM_PATH);
    ExitProcess(0u);
}
