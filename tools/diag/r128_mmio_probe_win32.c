/*
 * V9XR1MM.EXE: ATI Rage 128 read-only register snapshot.
 *
 * Phase 2 of docs\plans\ati-rage128.md. Finds the Rage 128 by a PCI scan
 * (configuration mechanism #1, CF8h restored after each read), loads
 * R1MM.VXD, and asks it for register offsets through the 16 KiB register
 * BAR, each read twice. Writes C:\V9XDIAG\V9XR1MM.INI:
 *
 *   [Report]  build, the device found, its BARs, the VxD's status;
 *   [Mmio]    every offset read, as R<offset>=first[,second if it moved],
 *             with Name.<offset> from xf86-video-r128's r128_reg.h;
 *   [Decode]  memory size and type, the CRTC's depth, pitch and start,
 *             the 2D engine's FIFO and busy state, and the CCE's mode.
 *
 * Two captures. The first reads the non-GUI registers (configuration,
 * memory, CRTC, DAC, cursor, the PM4 status block) and GUI_STAT. The
 * second reads the 2D and 3D state registers (14xxh-1Dxxh), which are
 * FIFOed behind the engine. It runs only when the first shows the CCE
 * not fetching (PM4_BUFFER_CNTL mode 0, NONPM4) or when /gui is given, because
 * whether a bus read of a FIFOed register waits on an engine the CCE owns
 * is not documented in any source to hand.
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

#define V9X_R1MM_PATH        V9X_DIAG_PATH("V9XR1MM.INI")
#define V9X_R1MM_VXD         "\\\\.\\R1MM.VXD"
#define V9X_R1MM_MAGIC       0x4d4d3152ul
#define V9X_R1MM_CAPTURE     1u
#define V9X_R1MM_OFFSET_MAX  256u
#define V9X_R1MM_HEADER      16u
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_PCI_ENABLE       0x80000000ul
#define V9X_PCI_BUSES        8ul
#define V9X_PCI_DEVICES      32ul
#define V9X_VENDOR_ATI       0x1002ul

#define V9X_ST_ATI             0x00000001ul
#define V9X_ST_BASE_INVALID    0x00000002ul
#define V9X_ST_MMIO_MAPPED     0x00000004ul
#define V9X_ST_IDENTITY_MATCH  0x00000008ul
#define V9X_ST_OFFSETS_REFUSED 0x00000010ul
#define V9X_ST_SNAPSHOT_TAKEN  0x00000020ul
#define V9X_ST_NOT_RAGE128     0x00000080ul
#define V9X_ST_ADDRESS_INVALID 0x00000100ul

/* Register offsets (r128_reg.h) the decode reads back. */
#define V9X_R128_CRTC_GEN_CNTL   0x0050ul
#define V9X_R128_CRTC_EXT_CNTL   0x0054ul
#define V9X_R128_DAC_CNTL        0x0058ul
#define V9X_R128_CONFIG_MEMSIZE  0x00f8ul
#define V9X_R128_CONFIG_APER     0x0108ul
#define V9X_R128_MEM_CNTL        0x0140ul
#define V9X_R128_CRTC_OFFSET     0x0224ul
#define V9X_R128_CRTC_PITCH      0x022cul
#define V9X_R128_PM4_BUFFER_CNTL 0x0704ul
#define V9X_R128_PM4_STAT        0x07b8ul
#define V9X_R128_PM4_MICRO_CNTL  0x07fcul
#define V9X_R128_GUI_STAT        0x1740ul

/* Field layouts: r128_reg.h, and the Rage 128 PRO RRG where it names
 * them. CRTC_GEN_CNTL pixel width 10:8 (1 4bpp, 2 8, 3 15, 4 16, 5 24,
 * 6 32), CRTC_EN 25, CRTC_EXT_DISP_EN 24. CONFIG_MEMSIZE 25:0 in bytes.
 * MEM_CNTL 1:0 the memory type. PM4_BUFFER_CNTL mode 31:28.
 * PM4_MICRO_CNTL bit 30 free-run. GUI_STAT 11:0 free FIFO entries, 31
 * GUI_ACTIVE. PM4_STAT 11:0 free CCE FIFO, 16 PM4_BUSY, 31 GUI_ACTIVE. */
#define V9X_R128_CRTC_PIX_SHIFT  8u
#define V9X_R128_CRTC_PIX_MASK   0x7ul
#define V9X_R128_CRTC_EN         0x02000000ul
#define V9X_R128_CRTC_EXT_DISP   0x01000000ul
#define V9X_R128_MEMSIZE_MASK    0x03fffffful
#define V9X_R128_MEM_TYPE_MASK   0x3ul
#define V9X_R128_PM4_MODE_SHIFT  28u
#define V9X_R128_PM4_MODE_MASK   0xful
#define V9X_R128_MICRO_FREERUN   0x40000000ul
#define V9X_R128_FIFO_MASK       0x00000ffful
#define V9X_R128_GUI_ACTIVE      0x80000000ul
#define V9X_R128_PM4_BUSY        0x00010000ul
#define V9X_R128_PITCH_MASK      0x3fful
#define V9X_R128_PITCH_UNIT      8ul

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_r1mm_register {
    DWORD offset;
    const char *name;
};

/* First capture: safe whatever the engine is doing. */
static const struct v9x_r1mm_register v9x_global[] = {
    { 0x0008ul, "CLOCK_CNTL_INDEX" },
    { 0x0010ul, "BIOS_0_SCRATCH" },
    { 0x0014ul, "BIOS_1_SCRATCH" },
    { 0x0018ul, "BIOS_2_SCRATCH" },
    { 0x001cul, "BIOS_3_SCRATCH" },
    { 0x0020ul, "BIOS_4_SCRATCH" },
    { 0x0024ul, "BIOS_5_SCRATCH" },
    { 0x0028ul, "BIOS_6_SCRATCH" },
    { 0x002cul, "BIOS_7_SCRATCH" },
    { 0x0030ul, "BUS_CNTL" },
    { 0x0034ul, "BUS_CNTL1" },
    { 0x0038ul, "MEM_VGA_WP_SEL" },
    { 0x003cul, "MEM_VGA_RP_SEL" },
    { 0x0040ul, "GEN_INT_CNTL" },
    { 0x0044ul, "GEN_INT_STATUS" },
    { 0x0050ul, "CRTC_GEN_CNTL" },
    { 0x0054ul, "CRTC_EXT_CNTL" },
    { 0x0058ul, "DAC_CNTL" },
    { 0x005cul, "CRTC_STATUS" },
    { 0x0068ul, "GPIO_MONID" },
    { 0x006cul, "GPIO_MONIDB" },
    { 0x0094ul, "I2C_CNTL_1" },
    { 0x00b0ul, "PALETTE_INDEX" },
    { 0x00e0ul, "CONFIG_CNTL" },
    { 0x00e4ul, "CONFIG_XSTRAP" },
    { 0x00e8ul, "CONFIG_BONDS" },
    { 0x00f0ul, "GEN_RESET_CNTL" },
    { 0x00f8ul, "CONFIG_MEMSIZE" },
    { 0x0108ul, "CONFIG_APER_SIZE" },
    { 0x0110ul, "CONFIG_REG_APER_SIZE" },
    { 0x0114ul, "CONFIG_MEMSIZE_EMBEDDED" },
    { 0x0120ul, "TEST_DEBUG_CNTL" },
    { 0x0130ul, "HOST_PATH_CNTL" },
    { 0x0140ul, "MEM_CNTL" },
    { 0x0150ul, "MEM_STR_CNTL" },
    { 0x0154ul, "MEM_INIT_LAT_TIMER" },
    { 0x0158ul, "MEM_SDRAM_MODE_REG" },
    { 0x0170ul, "AGP_BASE" },
    { 0x0174ul, "AGP_CNTL" },
    { 0x0178ul, "AGP_APER_OFFSET" },
    { 0x0184ul, "PC_NGUI_CTLSTAT" },
    { 0x0200ul, "CRTC_H_TOTAL_DISP" },
    { 0x0204ul, "CRTC_H_SYNC_STRT_WID" },
    { 0x0208ul, "CRTC_V_TOTAL_DISP" },
    { 0x020cul, "CRTC_V_SYNC_STRT_WID" },
    { 0x0210ul, "CRTC_VLINE_CRNT_VLINE" },
    { 0x0214ul, "CRTC_CRNT_FRAME" },
    { 0x0218ul, "CRTC_GUI_TRIG_VLINE" },
    { 0x021cul, "CRTC_DEBUG" },
    { 0x0224ul, "CRTC_OFFSET" },
    { 0x0228ul, "CRTC_OFFSET_CNTL" },
    { 0x022cul, "CRTC_PITCH" },
    { 0x0260ul, "CUR_OFFSET" },
    { 0x0264ul, "CUR_HORZ_VERT_POSN" },
    { 0x0268ul, "CUR_HORZ_VERT_OFF" },
    { 0x026cul, "CUR_CLR0" },
    { 0x0270ul, "CUR_CLR1" },
    { 0x0284ul, "FP_GEN_CNTL" },
    { 0x02d0ul, "LVDS_GEN_CNTL" },
    { 0x02e0ul, "DDA_CONFIG" },
    { 0x02e4ul, "DDA_ON_OFF" },
    { 0x0700ul, "PM4_BUFFER_OFFSET" },
    { 0x0704ul, "PM4_BUFFER_CNTL" },
    { 0x0708ul, "PM4_BUFFER_WM_CNTL" },
    { 0x070cul, "PM4_BUFFER_DL_RPTR_ADDR" },
    { 0x0710ul, "PM4_BUFFER_DL_RPTR" },
    { 0x0714ul, "PM4_BUFFER_DL_WPTR" },
    { 0x0718ul, "PM4_BUFFER_DL_WPTR_DELAY" },
    { 0x071cul, "PM4_VC_FPU_SETUP" },
    { 0x07a4ul, "PM4_VC_DEBUG_CONFIG" },
    { 0x07a8ul, "PM4_VC_STAT" },
    { 0x07b8ul, "PM4_STAT" },
    { 0x07f0ul, "PM4_BUFFER_ADDR" },
    { 0x07fcul, "PM4_MICRO_CNTL" },
    { 0x0f00ul, "PCI_ID_COPY" },
    { 0x0f04ul, "PCI_CMD_STATUS_COPY" },
    { 0x0f08ul, "PCI_CLASS_REV_COPY" },
    { 0x0f10ul, "PCI_BAR0_COPY" },
    { 0x0f14ul, "PCI_BAR1_COPY" },
    { 0x0f18ul, "PCI_BAR2_COPY" },
    { 0x0f2cul, "PCI_SUBSYS_COPY" },
    { 0x0f54ul, "AGP_STATUS" },
    { 0x0f58ul, "AGP_COMMAND" },
    { 0x1740ul, "GUI_STAT" }
};

/* Second capture: the FIFOed engine state. */
static const struct v9x_r1mm_register v9x_engine[] = {
    { 0x1404ul, "DST_OFFSET" },
    { 0x1408ul, "DST_PITCH" },
    { 0x1428ul, "SRC_PITCH_OFFSET" },
    { 0x142cul, "DST_PITCH_OFFSET" },
    { 0x146cul, "DP_GUI_MASTER_CNTL" },
    { 0x147cul, "DP_BRUSH_FRGD_CLR" },
    { 0x15d8ul, "DP_SRC_FRGD_CLR" },
    { 0x1660ul, "AUX_SC_CNTL" },
    { 0x16c0ul, "DP_CNTL" },
    { 0x16c4ul, "DP_DATATYPE" },
    { 0x16c8ul, "DP_MIX" },
    { 0x16ccul, "DP_WRITE_MASK" },
    { 0x16e0ul, "DEFAULT_OFFSET" },
    { 0x16e4ul, "DEFAULT_PITCH" },
    { 0x16e8ul, "DEFAULT_SC_BOTTOM_RIGHT" },
    { 0x16ecul, "SC_TOP_LEFT" },
    { 0x16f0ul, "SC_BOTTOM_RIGHT" },
    { 0x1748ul, "PC_GUI_CTLSTAT" },
    { 0x1a00ul, "SCALE_3D_CNTL" },
    { 0x1bc4ul, "SETUP_CNTL" },
    { 0x1bccul, "WINDOW_XY_OFFSET" },
    { 0x1c80ul, "DST_PITCH_OFFSET_C" },
    { 0x1c84ul, "DP_GUI_MASTER_CNTL_C" },
    { 0x1c88ul, "SC_TOP_LEFT_C" },
    { 0x1c8cul, "SC_BOTTOM_RIGHT_C" },
    { 0x1c90ul, "Z_OFFSET_C" },
    { 0x1c94ul, "Z_PITCH_C" },
    { 0x1c98ul, "Z_STEN_CNTL_C" },
    { 0x1c9cul, "TEX_CNTL_C" },
    { 0x1ca0ul, "MISC_3D_STATE_CNTL_REG" },
    { 0x1ca4ul, "TEXTURE_CLR_CMP_CLR_C" },
    { 0x1ca8ul, "TEXTURE_CLR_CMP_MSK_C" },
    { 0x1cacul, "FOG_COLOR_C" },
    { 0x1cb0ul, "PRIM_TEX_CNTL_C" },
    { 0x1cb4ul, "PRIM_TEXTURE_COMBINE_CNTL_C" },
    { 0x1cb8ul, "TEX_SIZE_PITCH_C" },
    { 0x1cbcul, "PRIM_TEX_0_OFFSET_C" },
    { 0x1d00ul, "SEC_TEX_CNTL_C" },
    { 0x1d04ul, "SEC_TEX_COMBINE_CNTL_C" },
    { 0x1d08ul, "SEC_TEX_0_OFFSET_C" },
    { 0x1d34ul, "CONSTANT_COLOR_C" },
    { 0x1d38ul, "PRIM_TEXTURE_BORDER_COLOR_C" },
    { 0x1d3cul, "SEC_TEXTURE_BORDER_COLOR_C" },
    { 0x1d40ul, "STEN_REF_MASK_C" },
    { 0x1d44ul, "PLANE_3D_MASK_C" }
};

/* R1MM.VXD's R1Devices, for the scan. */
static const WORD v9x_rage128_ids[] = {
    0x5245u, 0x5246u, 0x5247u, 0x524bu, 0x524cu,
    0x5345u, 0x5346u, 0x5347u, 0x5348u, 0x534bu, 0x534cu, 0x534du, 0x534eu,
    0x5041u, 0x5042u, 0x5043u, 0x5044u, 0x5045u, 0x5046u, 0x5047u, 0x5048u,
    0x5049u, 0x504au, 0x504bu, 0x504cu, 0x504du, 0x504eu, 0x504fu, 0x5050u,
    0x5051u, 0x5052u, 0x5053u, 0x5054u, 0x5055u, 0x5056u, 0x5057u, 0x5058u,
    0x5446u, 0x544cu, 0x5452u, 0x5453u, 0x5454u, 0x5455u,
    0x4c45u, 0x4c46u, 0x4d46u, 0x4d4cu
};

/* PM4_BUFFER_CNTL modes, r128_reg.h R128_PM4_*. */
static const char * const v9x_pm4_modes[16] = {
    "NONPM4", "192PIO", "192BM", "128PIO_64INDBM", "128BM_64INDBM",
    "64PIO_128INDBM", "64BM_128INDBM", "64PIO_64VCBM_64INDBM",
    "64BM_64VCBM_64INDBM", "mode-9", "mode-10", "mode-11", "mode-12",
    "mode-13", "64PIO_64VCPIO_64INDPIO", "mode-15"
};

static const char * const v9x_crtc_depths[8] = {
    "0", "4", "8", "15", "16", "24", "32", "7"
};

static const char * const v9x_mem_types[4] = {
    "sdr", "sdr-sgram-2to1", "ddr-sgram", "type-3"
};

struct v9x_r1mm_request {
    DWORD config;
    DWORD count;
    DWORD offsets[V9X_R1MM_OFFSET_MAX];
};

struct v9x_r1mm_result {
    DWORD magic;
    DWORD status;
    DWORD id;
    DWORD bar2;
    DWORD accepted;
    DWORD copy_id;
    DWORD cf8;
    DWORD refused_offset;
    DWORD config;
    DWORD reserved[7];
    DWORD offsets[V9X_R1MM_OFFSET_MAX];
    DWORD first[V9X_R1MM_OFFSET_MAX];
    DWORD second[V9X_R1MM_OFFSET_MAX];
};

/* The header must be the VxD's R1_HEADER_DWORDS long. */
typedef char v9x_r1mm_header_matches[
    offsetof(struct v9x_r1mm_result, offsets) ==
    sizeof(DWORD) * V9X_R1MM_HEADER ? 1 : -1];

static struct v9x_r1mm_request v9x_request;
static struct v9x_r1mm_result v9x_result;
static DWORD v9x_values[0x2000u / 4u];

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
    WritePrivateProfileStringA(section, key, text, V9X_R1MM_PATH);
}

static void v9x_number(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_decimal(text, value);
    WritePrivateProfileStringA(section, key, text, V9X_R1MM_PATH);
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_R1MM_PATH);
}

static void v9x_offset_key(char *key, const char *prefix, DWORD offset)
{
    lstrcpyA(key, prefix);
    v9x_hex(key + lstrlenA(prefix), offset, 4);
}

static void v9x_request_list(const struct v9x_r1mm_register *list,
                             DWORD count)
{
    char key[24];
    DWORD index;

    v9x_request.count = 0ul;
    for (index = 0ul; index < count && index < V9X_R1MM_OFFSET_MAX;
         ++index) {
        v9x_request.offsets[v9x_request.count++] = list[index].offset;
        v9x_offset_key(key, "Name.", list[index].offset);
        v9x_text("Mmio", key, list[index].name);
    }
}

/* One DIOC capture of v9x_request. Returns nonzero on a snapshot. */
static int v9x_capture(void)
{
    HANDLE device;
    DWORD returned;
    BOOL ok;

    device = CreateFileA(V9X_R1MM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        return 0;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_R1MM_CAPTURE, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_R1MM_MAGIC) {
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
        v9x_values[offset >> 2] = v9x_result.first[index];
    }
}

static DWORD v9x_value(DWORD offset)
{
    return v9x_values[offset >> 2];
}

/* The first Rage 128 on buses 0-7, function 0; 0 if none. */
static DWORD v9x_find_rage128(void)
{
    DWORD bus;
    DWORD slot;
    DWORD config;
    DWORD id;
    DWORD index;

    for (bus = 0ul; bus < V9X_PCI_BUSES; ++bus) {
        for (slot = 0ul; slot < V9X_PCI_DEVICES; ++slot) {
            config = V9X_PCI_ENABLE | (bus << 16) | (slot << 11);
            id = v9x_config_read(config, 0ul);
            if ((id & 0xfffful) != V9X_VENDOR_ATI) {
                continue;
            }
            for (index = 0ul;
                 index < sizeof(v9x_rage128_ids) / sizeof(v9x_rage128_ids[0]);
                 ++index) {
                if ((id >> 16) == (DWORD)v9x_rage128_ids[index]) {
                    return config;
                }
            }
        }
    }
    return 0ul;
}

static void v9x_decode_global(void)
{
    DWORD value;
    DWORD mode;
    DWORD pitch;

    value = v9x_value(V9X_R128_CONFIG_MEMSIZE) & V9X_R128_MEMSIZE_MASK;
    v9x_number("Decode", "MemoryBytes", value);
    v9x_number("Decode", "ApertureBytes", v9x_value(V9X_R128_CONFIG_APER));
    v9x_text("Decode", "MemoryType",
             v9x_mem_types[v9x_value(V9X_R128_MEM_CNTL) &
                           V9X_R128_MEM_TYPE_MASK]);

    value = v9x_value(V9X_R128_CRTC_GEN_CNTL);
    v9x_text("Decode", "CrtcEnabled",
             (value & V9X_R128_CRTC_EN) != 0ul ? "yes" : "no");
    v9x_text("Decode", "CrtcExtendedDisplay",
             (value & V9X_R128_CRTC_EXT_DISP) != 0ul ? "yes" : "no");
    v9x_text("Decode", "CrtcBitsPerPixel",
             v9x_crtc_depths[(value >> V9X_R128_CRTC_PIX_SHIFT) &
                             V9X_R128_CRTC_PIX_MASK]);
    pitch = v9x_value(V9X_R128_CRTC_PITCH) & V9X_R128_PITCH_MASK;
    v9x_number("Decode", "CrtcPitchPixels", pitch * V9X_R128_PITCH_UNIT);
    v9x_key("Decode", "CrtcOffset", v9x_value(V9X_R128_CRTC_OFFSET));
    v9x_text("Decode", "Dac8Bit",
             (v9x_value(V9X_R128_DAC_CNTL) & 0x100ul) != 0ul ? "yes" : "no");

    value = v9x_value(V9X_R128_GUI_STAT);
    v9x_number("Decode", "GuiFifoFree", value & V9X_R128_FIFO_MASK);
    v9x_text("Decode", "GuiActive",
             (value & V9X_R128_GUI_ACTIVE) != 0ul ? "yes" : "no");

    mode = (v9x_value(V9X_R128_PM4_BUFFER_CNTL) >> V9X_R128_PM4_MODE_SHIFT) &
           V9X_R128_PM4_MODE_MASK;
    v9x_text("Decode", "Pm4Mode", v9x_pm4_modes[mode]);
    v9x_text("Decode", "Pm4MicroFreeRun",
             (v9x_value(V9X_R128_PM4_MICRO_CNTL) & V9X_R128_MICRO_FREERUN) !=
             0ul ? "yes" : "no");
    value = v9x_value(V9X_R128_PM4_STAT);
    v9x_number("Decode", "Pm4FifoFree", value & V9X_R128_FIFO_MASK);
    v9x_text("Decode", "Pm4Busy",
             (value & V9X_R128_PM4_BUSY) != 0ul ? "yes" : "no");
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

void __stdcall V9xRage128MmioEntry(void)
{
    DWORD config;
    DWORD mode;
    int cce_running;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_R1MM_PATH);
    v9x_text("Report", "Tool", "V9XR1MM");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Access", "mmio-read-only");

    config = v9x_find_rage128();
    if (config == 0ul) {
        v9x_text("Report", "Result", "NO-RAGE128");
        WritePrivateProfileStringA(0, 0, 0, V9X_R1MM_PATH);
        ExitProcess(1u);
    }
    v9x_key("Report", "Config", config);
    v9x_key("Report", "Id", v9x_config_read(config, 0x00ul));
    v9x_key("Report", "CommandStatus", v9x_config_read(config, 0x04ul));
    v9x_key("Report", "ClassRevision", v9x_config_read(config, 0x08ul));
    v9x_key("Report", "Bar0", v9x_config_read(config, 0x10ul));
    v9x_key("Report", "Bar1", v9x_config_read(config, 0x14ul));
    v9x_key("Report", "Bar2", v9x_config_read(config, 0x18ul));
    v9x_key("Report", "Subsystem", v9x_config_read(config, 0x2cul));
    v9x_key("Report", "RomBase", v9x_config_read(config, 0x30ul));

    v9x_request.config = config;
    v9x_request_list(v9x_global, sizeof(v9x_global) / sizeof(v9x_global[0]));
    if (!v9x_capture()) {
        v9x_key("Report", "Status", v9x_result.status);
        v9x_key("Report", "CopyId", v9x_result.copy_id);
        v9x_key("Report", "RefusedOffset", v9x_result.refused_offset);
        WritePrivateProfileStringA(0, 0, 0, V9X_R1MM_PATH);
        ExitProcess(2u);
    }
    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "CopyId", v9x_result.copy_id);
    v9x_number("Report", "Accepted", v9x_result.accepted);
    v9x_publish_mmio();
    v9x_decode_global();

    mode = (v9x_value(V9X_R128_PM4_BUFFER_CNTL) >> V9X_R128_PM4_MODE_SHIFT) &
           V9X_R128_PM4_MODE_MASK;
    /* The mode, not the microengine's free-run bit: A8U4I5's card read
     * free-run set with NONPM4 under tier-0 (boot 519), and in NONPM4 the
     * engine takes direct register access, which is how aty128fb and X's
     * non-DRI path drive it. */
    cce_running = mode != 0ul;
    if (cce_running && !v9x_switch_given("/gui")) {
        v9x_text("Report", "EngineCapture", "skipped-cce-running");
        v9x_text("Report", "Result", "COMPLETE");
        WritePrivateProfileStringA(0, 0, 0, V9X_R1MM_PATH);
        ExitProcess(0u);
    }

    v9x_request_list(v9x_engine, sizeof(v9x_engine) / sizeof(v9x_engine[0]));
    if (!v9x_capture()) {
        v9x_text("Report", "EngineCapture", "failed");
        v9x_text("Report", "Result", "ENGINE-CAPTURE-FAILED");
        WritePrivateProfileStringA(0, 0, 0, V9X_R1MM_PATH);
        ExitProcess(3u);
    }
    v9x_text("Report", "EngineCapture", cce_running ? "forced" : "taken");
    v9x_publish_mmio();
    v9x_text("Report", "Result", "COMPLETE");
    WritePrivateProfileStringA(0, 0, 0, V9X_R1MM_PATH);
    ExitProcess(0u);
}
