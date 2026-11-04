/*
 * V9XRSMM.EXE: ATI Radeon X1200 IGP (RS690, 1002:791F) read-only register
 * snapshot.
 *
 * Phase 2 of docs\plans\ati-rs690-x1200.md. Finds 1002:791F by a PCI scan
 * (configuration mechanism #1, CF8h restored after each read), loads
 * RSMM.VXD, and asks it for register offsets through the 64 KiB register
 * BAR, each read twice. Writes C:\V9XDIAG\V9XRSMM.INI:
 *
 *   [Report]  build, the device found, its BARs, the VxD's status;
 *   [Mmio]    every offset read, as R<offset>=first[,second if it moved],
 *             with Name.<offset> from Linux radeon's headers;
 *   [Decode]  memory size and apertures, where the frame buffer sits in
 *             the GPU's address space (HDP_FB_LOCATION), the 2D engine's
 *             default and current surfaces, the 3D engine's pipes and
 *             vertex mode, and the CP's mode;
 *   [D1] [D2] each display controller: VGA or AVIVO scanout, depth,
 *             pitch, viewport, surface address and its offset into the
 *             frame buffer.
 *
 * What the plan needs from it: whether the 2D and 3D engines are idle and
 * usable as the BIOS left them, where the frame buffer sits in the GPU's
 * address space, and whether a VBE mode scans out through D1GRPH, so that
 * flips are a register write. Nothing on the device is written.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_RSMM_PATH        V9X_DIAG_PATH("V9XRSMM.INI")
#define V9X_RSMM_VXD         "\\\\.\\RSMM.VXD"
#define V9X_RSMM_MAGIC       0x4d4d5352ul
#define V9X_RSMM_CAPTURE     1u
#define V9X_RSMM_OFFSET_MAX  256u
#define V9X_RSMM_HEADER      16u
#define V9X_RSMM_MMIO_BYTES  0x00010000ul
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_PCI_ENABLE       0x80000000ul
#define V9X_PCI_BUSES        16ul
#define V9X_PCI_DEVICES      32ul
#define V9X_VENDOR_ATI       0x1002ul
#define V9X_DEVICE_RS690     0x791ful

#define V9X_ST_ATI             0x00000001ul
#define V9X_ST_BASE_INVALID    0x00000002ul
#define V9X_ST_MMIO_MAPPED     0x00000004ul
#define V9X_ST_IDENTITY_MATCH  0x00000008ul
#define V9X_ST_OFFSETS_REFUSED 0x00000010ul
#define V9X_ST_SNAPSHOT_TAKEN  0x00000020ul
#define V9X_ST_BAR_64          0x00000040ul
#define V9X_ST_NOT_ACCEPTED    0x00000080ul
#define V9X_ST_ADDRESS_INVALID 0x00000100ul
#define V9X_ST_BAR_ABOVE_4G    0x00000200ul

/* Register offsets the decode reads back: radeon_reg.h, r300d.h,
 * r300_reg.h, r500_reg.h and rs690d.h in build\external\rs690\linux-radeon\. */
#define V9X_RS_CONFIG_MEMSIZE      0x00f8ul
#define V9X_RS_CONFIG_APER_0_BASE  0x0100ul
#define V9X_RS_CONFIG_APER_SIZE    0x0108ul
#define V9X_RS_HDP_FB_LOCATION     0x0134ul
#define V9X_RS_CP_CSQ_CNTL         0x0740ul
#define V9X_RS_RBBM_STATUS         0x0e40ul
#define V9X_RS_DST_PITCH_OFFSET    0x142cul
#define V9X_RS_SRC_PITCH_OFFSET    0x1428ul
#define V9X_RS_DEFAULT_OFFSET      0x16e0ul
#define V9X_RS_DEFAULT_PITCH       0x16e4ul
#define V9X_RS_VAP_CNTL_STATUS     0x2140ul
#define V9X_RS_GB_PIPE_SELECT      0x402cul
#define V9X_RS_D1VGA_CONTROL       0x0330ul
#define V9X_RS_D2VGA_CONTROL       0x0338ul
#define V9X_RS_CRTC_CONTROL        0x6080ul
#define V9X_RS_CRTC_STATUS         0x609cul
#define V9X_RS_GRPH_ENABLE         0x6100ul
#define V9X_RS_GRPH_CONTROL        0x6104ul
#define V9X_RS_GRPH_PRIMARY        0x6110ul
#define V9X_RS_GRPH_PITCH          0x6120ul
#define V9X_RS_GRPH_UPDATE         0x6144ul
#define V9X_RS_VIEWPORT_SIZE       0x6584ul

/* Field layouts, same sources. HDP_FB_LOCATION 15:0 the frame buffer's
 * GPU address >> 16 (rs690.c rs690_mc_program). RBBM_STATUS 6:0 free
 * command FIFO entries, bit 31 GUI_ACTIVE (r300d.h). CP_CSQ_CNTL 31:28
 * the CP's mode, 0 for primary and indirect queues disabled
 * (RADEON_CSQ_PRIDIS_INDDIS). A pitch-offset register holds the offset
 * >> 10 in 21:0 and the pitch / 64 in 29:22 (r100.c, Linux's
 * pitch_offset arithmetic). VAP_CNTL_STATUS bit 8 PVS_BYPASS (r300_reg.h).
 * R400_GB_PIPE_SELECT 13:12 the pipe count less one
 * (xf86-video-ati-6.14.6 radeon_accel.c:482). D1VGA_CONTROL bit 0 the
 * VGA scanout mode. CRTC_CONTROL and GRPH_ENABLE bit 0 enable.
 * GRPH_CONTROL 1:0 depth (8, 16, 32, 64 bpp), 10:8 format. GRPH_UPDATE
 * bit 2 surface update pending, bit 16 update lock. CRTC_STATUS bit 0 in
 * vertical blank. */
#define V9X_RS_FB_START_MASK       0x0000fffful
#define V9X_RS_FB_START_SHIFT      16u
#define V9X_RS_CMDFIFO_MASK        0x0000007ful
#define V9X_RS_GUI_ACTIVE          0x80000000ul
#define V9X_RS_CSQ_MODE_SHIFT      28u
#define V9X_RS_PO_OFFSET_MASK      0x003ffffful
#define V9X_RS_PO_OFFSET_SHIFT     10u
#define V9X_RS_PO_PITCH_SHIFT      22u
#define V9X_RS_PO_PITCH_MASK       0xfful
#define V9X_RS_PO_PITCH_UNIT       64ul
#define V9X_RS_PVS_BYPASS          0x00000100ul
#define V9X_RS_PIPES_SHIFT         12u
#define V9X_RS_PIPES_MASK          0x3ul
#define V9X_RS_ENABLE_BIT          0x00000001ul
#define V9X_RS_GRPH_DEPTH_MASK     0x3ul
#define V9X_RS_GRPH_FORMAT_SHIFT   8u
#define V9X_RS_GRPH_FORMAT_MASK    0x7ul
#define V9X_RS_GRPH_PITCH_MASK     0x3ffful
#define V9X_RS_UPDATE_PENDING      0x00000004ul
#define V9X_RS_UPDATE_LOCK         0x00010000ul
#define V9X_RS_IN_VBLANK           0x00000001ul
#define V9X_RS_VIEWPORT_MASK       0x3ffful

/* The two display controllers (rs600 display, radeon_asic.c), the second
 * 0800h above the first (r500_reg.h). */
#define V9X_RS_CRTC_COUNT 2u
static const DWORD v9x_crtc_offsets[V9X_RS_CRTC_COUNT] = {
    0x0000ul, 0x0800ul
};
static const DWORD v9x_vga_controls[V9X_RS_CRTC_COUNT] = {
    V9X_RS_D1VGA_CONTROL, V9X_RS_D2VGA_CONTROL
};

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_rsmm_register {
    DWORD offset;
    const char *name;
};

/* Bus, memory and engine state. The VGA block names other than the
 * render and D1/D2 controls are Evergreen's, carried over as labels, not
 * claims: r500_reg.h names only those three. */
static const struct v9x_rsmm_register v9x_global[] = {
    { 0x0030ul, "BUS_CNTL" },
    { 0x0040ul, "GEN_INT_CNTL" },
    { 0x0044ul, "GEN_INT_STATUS" },
    { 0x00e0ul, "CONFIG_CNTL" },
    { 0x00e4ul, "CONFIG_XSTRAP" },
    { 0x00f0ul, "RBBM_SOFT_RESET" },
    { 0x00f8ul, "CONFIG_MEMSIZE" },
    { 0x0100ul, "CONFIG_APER_0_BASE" },
    { 0x0104ul, "CONFIG_APER_1_BASE" },
    { 0x0108ul, "CONFIG_APER_SIZE" },
    { 0x0130ul, "HOST_PATH_CNTL" },
    { 0x0134ul, "HDP_FB_LOCATION" },
    { 0x0148ul, "MC_FB_LOCATION" },
    { 0x014cul, "MC_AGP_LOCATION" },
    { 0x0300ul, "VGA_RENDER_CONTROL" },
    { 0x0310ul, "VGA_MEMORY_BASE_ADDRESS" },
    { 0x0328ul, "VGA_HDP_CONTROL" },
    { 0x0330ul, "D1VGA_CONTROL" },
    { 0x0338ul, "D2VGA_CONTROL" },
    { 0x0700ul, "CP_RB_BASE" },
    { 0x0704ul, "CP_RB_CNTL" },
    { 0x070cul, "CP_RB_RPTR_ADDR" },
    { 0x0710ul, "CP_RB_RPTR" },
    { 0x0714ul, "CP_RB_WPTR" },
    { 0x0740ul, "CP_CSQ_CNTL" },
    { 0x0b00ul, "SURFACE_CNTL" },
    { 0x0e40ul, "RBBM_STATUS" },
    { 0x0f00ul, "PCI_COPY_ID" },
    { 0x0f04ul, "PCI_COPY_COMMAND_STATUS" },
    { 0x0f08ul, "PCI_COPY_CLASS_REVISION" },
    { 0x0f0cul, "PCI_COPY_HEADER" }
};

/* The 2D engine's state block, read with the engine idle under VBE: GDI
 * draws on the CPU there. */
static const struct v9x_rsmm_register v9x_2d[] = {
    { 0x1404ul, "DST_OFFSET" },
    { 0x1408ul, "DST_PITCH" },
    { 0x1428ul, "SRC_PITCH_OFFSET" },
    { 0x142cul, "DST_PITCH_OFFSET" },
    { 0x146cul, "DP_GUI_MASTER_CNTL" },
    { 0x1478ul, "DP_BRUSH_BKGD_CLR" },
    { 0x147cul, "DP_BRUSH_FRGD_CLR" },
    { 0x15acul, "SRC_OFFSET" },
    { 0x15b0ul, "SRC_PITCH" },
    { 0x15c0ul, "CLR_CMP_CNTL" },
    { 0x15d8ul, "DP_SRC_FRGD_CLR" },
    { 0x15dcul, "DP_SRC_BKGD_CLR" },
    { 0x16c0ul, "DP_CNTL" },
    { 0x16c4ul, "DP_DATATYPE" },
    { 0x16c8ul, "DP_MIX" },
    { 0x16ccul, "DP_WRITE_MASK" },
    { 0x16e0ul, "DEFAULT_OFFSET" },
    { 0x16e4ul, "DEFAULT_PITCH" },
    { 0x16e8ul, "DEFAULT_SC_BOTTOM_RIGHT" },
    { 0x16ecul, "SC_TOP_LEFT" },
    { 0x16f0ul, "SC_BOTTOM_RIGHT" },
    { 0x170cul, "DST_PIPE_CONFIG" },
    { 0x1710ul, "DSTCACHE_MODE" },
    { 0x1714ul, "DSTCACHE_CTLSTAT" }
};

/* The 3D engine's vertex and geometry blocks, and the display globals. */
static const struct v9x_rsmm_register v9x_3d[] = {
    { 0x2080ul, "VAP_CNTL" },
    { 0x2084ul, "VAP_VF_CNTL" },
    { 0x2140ul, "VAP_CNTL_STATUS" },
    { 0x342cul, "RB2D_DSTCACHE_CTLSTAT" },
    { 0x4008ul, "GB_ENABLE" },
    { 0x4018ul, "GB_TILE_CONFIG" },
    { 0x401cul, "GB_SELECT" },
    { 0x4020ul, "GB_AA_CONFIG" },
    { 0x4024ul, "GB_FIFO_SIZE" },
    { 0x402cul, "GB_PIPE_SELECT" },
    { 0x4e4cul, "RB3D_DSTCACHE_CTLSTAT" },
    { 0x60f8ul, "DC_CRTC_MASTER_EN" },
    { 0x6480ul, "DC_LUT_RW_SELECT" },
    { 0x6484ul, "DC_LUT_RW_MODE" },
    { 0x6488ul, "DC_LUT_RW_INDEX" },
    { 0x6498ul, "DC_LUT_READ_PIPE_SELECT" },
    { 0x649cul, "DC_LUT_WRITE_EN_MASK" },
    { 0x64c0ul, "DC_LUTA_CONTROL" },
    { 0x6cc0ul, "DC_LUTB_CONTROL" },
    { 0x6520ul, "DC_LB_MEMORY_SPLIT" }
};

/* One display controller's block, at the first controller's offsets. */
static const struct v9x_rsmm_register v9x_crtc[] = {
    { 0x6000ul, "CRTC_H_TOTAL" },
    { 0x6020ul, "CRTC_V_TOTAL" },
    { 0x6024ul, "CRTC_V_BLANK_START_END" },
    { 0x6080ul, "CRTC_CONTROL" },
    { 0x6084ul, "CRTC_BLANK_CONTROL" },
    { 0x609cul, "CRTC_STATUS" },
    { 0x60a0ul, "CRTC_STATUS_POSITION" },
    { 0x60a4ul, "CRTC_FRAME_COUNT" },
    { 0x60acul, "CRTC_STATUS_HV_COUNT" },
    { 0x60e0ul, "MODE_MASTER_UPDATE_LOCK" },
    { 0x60e4ul, "MODE_MASTER_UPDATE_MODE" },
    { 0x60e8ul, "CRTC_UPDATE_LOCK" },
    { 0x6100ul, "GRPH_ENABLE" },
    { 0x6104ul, "GRPH_CONTROL" },
    { 0x6108ul, "GRPH_LUT_SEL" },
    { 0x6110ul, "GRPH_PRIMARY_SURFACE_ADDRESS" },
    { 0x6118ul, "GRPH_SECONDARY_SURFACE_ADDRESS" },
    { 0x6120ul, "GRPH_PITCH" },
    { 0x6124ul, "GRPH_SURFACE_OFFSET_X" },
    { 0x6128ul, "GRPH_SURFACE_OFFSET_Y" },
    { 0x612cul, "GRPH_X_START" },
    { 0x6130ul, "GRPH_Y_START" },
    { 0x6134ul, "GRPH_X_END" },
    { 0x6138ul, "GRPH_Y_END" },
    { 0x6144ul, "GRPH_UPDATE" },
    { 0x6148ul, "GRPH_FLIP_CONTROL" },
    { 0x6400ul, "CUR_CONTROL" },
    { 0x6408ul, "CUR_SURFACE_ADDRESS" },
    { 0x6410ul, "CUR_SIZE" },
    { 0x6414ul, "CUR_POSITION" },
    { 0x6528ul, "MODE_DATA_FORMAT" },
    { 0x652cul, "MODE_DESKTOP_HEIGHT" },
    { 0x6534ul, "MODE_VBLANK_STATUS" },
    { 0x6580ul, "MODE_VIEWPORT_START" },
    { 0x6584ul, "MODE_VIEWPORT_SIZE" }
};

#define V9X_GLOBAL_COUNT (sizeof(v9x_global) / sizeof(v9x_global[0]))
#define V9X_2D_COUNT (sizeof(v9x_2d) / sizeof(v9x_2d[0]))
#define V9X_3D_COUNT (sizeof(v9x_3d) / sizeof(v9x_3d[0]))
#define V9X_CRTC_REG_COUNT (sizeof(v9x_crtc) / sizeof(v9x_crtc[0]))

/* Every offset fits one request: the VxD takes 256. */
typedef char v9x_rsmm_list_fits[
    V9X_GLOBAL_COUNT + V9X_2D_COUNT + V9X_3D_COUNT +
    V9X_CRTC_REG_COUNT * V9X_RS_CRTC_COUNT <=
    V9X_RSMM_OFFSET_MAX ? 1 : -1];

static const char * const v9x_grph_depths[4] = { "8", "16", "32", "64" };

struct v9x_rsmm_request {
    DWORD config;
    DWORD count;
    DWORD offsets[V9X_RSMM_OFFSET_MAX];
};

struct v9x_rsmm_result {
    DWORD magic;
    DWORD status;
    DWORD id;
    DWORD bar2;
    DWORD accepted;
    DWORD memsize;
    DWORD cf8;
    DWORD refused_offset;
    DWORD config;
    DWORD bar2_high;
    DWORD pci_copy;
    DWORD reserved[5];
    DWORD offsets[V9X_RSMM_OFFSET_MAX];
    DWORD first[V9X_RSMM_OFFSET_MAX];
    DWORD second[V9X_RSMM_OFFSET_MAX];
};

/* The header must be the VxD's RS_HEADER_DWORDS long. */
typedef char v9x_rsmm_header_matches[
    offsetof(struct v9x_rsmm_result, offsets) ==
    sizeof(DWORD) * V9X_RSMM_HEADER ? 1 : -1];

static struct v9x_rsmm_request v9x_request;
static struct v9x_rsmm_result v9x_result;
static DWORD v9x_values[V9X_RSMM_MMIO_BYTES / 4u];

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
    WritePrivateProfileStringA(section, key, text, V9X_RSMM_PATH);
}

static void v9x_number(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_decimal(text, value);
    WritePrivateProfileStringA(section, key, text, V9X_RSMM_PATH);
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_RSMM_PATH);
}

static void v9x_yes_no(const char *section, const char *key, int value)
{
    v9x_text(section, key, value ? "yes" : "no");
}

/* Offsets stay below 10000h, so four hex digits. */
static void v9x_offset_key(char *key, const char *prefix, DWORD offset)
{
    lstrcpyA(key, prefix);
    v9x_hex(key + lstrlenA(prefix), offset, 4);
}

/* Appends one offset and its name to the request; the name is written
 * with a display controller's prefix where it has one. */
static void v9x_request_add(DWORD offset, const char *prefix,
                            const char *name)
{
    char key[24];
    char label[64];

    v9x_request.offsets[v9x_request.count++] = offset;
    v9x_offset_key(key, "Name.", offset);
    lstrcpyA(label, prefix);
    lstrcatA(label, name);
    v9x_text("Mmio", key, label);
}

static void v9x_request_list(const struct v9x_rsmm_register *list,
                             DWORD count)
{
    DWORD index;

    for (index = 0ul; index < count; ++index) {
        v9x_request_add(list[index].offset, "", list[index].name);
    }
}

static void v9x_request_build(void)
{
    static const char * const prefixes[V9X_RS_CRTC_COUNT] = {
        "D1.", "D2."
    };
    DWORD index;
    DWORD crtc;

    v9x_request.count = 0ul;
    v9x_request_list(v9x_global, V9X_GLOBAL_COUNT);
    v9x_request_list(v9x_2d, V9X_2D_COUNT);
    v9x_request_list(v9x_3d, V9X_3D_COUNT);
    for (crtc = 0ul; crtc < V9X_RS_CRTC_COUNT; ++crtc) {
        for (index = 0ul; index < V9X_CRTC_REG_COUNT; ++index) {
            v9x_request_add(v9x_crtc[index].offset + v9x_crtc_offsets[crtc],
                            prefixes[crtc], v9x_crtc[index].name);
        }
    }
}

/* One DIOC capture of v9x_request. Returns nonzero on a snapshot. */
static int v9x_capture(void)
{
    HANDLE device;
    DWORD returned;
    BOOL ok;

    device = CreateFileA(V9X_RSMM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        return 0;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_RSMM_CAPTURE, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_RSMM_MAGIC) {
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

/* The first 1002:791F on buses 0-15, function 0; 0 if none. An IGP sits
 * behind the northbridge's internal bridge, so not on bus 0. */
static DWORD v9x_find_device(void)
{
    DWORD bus;
    DWORD slot;
    DWORD config;
    DWORD id;

    for (bus = 0ul; bus < V9X_PCI_BUSES; ++bus) {
        for (slot = 0ul; slot < V9X_PCI_DEVICES; ++slot) {
            config = V9X_PCI_ENABLE | (bus << 16) | (slot << 11);
            id = v9x_config_read(config, 0ul);
            if ((id & 0xfffful) == V9X_VENDOR_ATI &&
                (id >> 16) == V9X_DEVICE_RS690) {
                return config;
            }
        }
    }
    return 0ul;
}

static DWORD v9x_fb_start(void)
{
    return (v9x_value(V9X_RS_HDP_FB_LOCATION) & V9X_RS_FB_START_MASK) <<
           V9X_RS_FB_START_SHIFT;
}

/* A pitch-offset register as its byte offset and byte pitch. */
static void v9x_decode_pitch_offset(const char *name, DWORD value)
{
    char key[40];

    lstrcpyA(key, name);
    lstrcatA(key, "Offset");
    v9x_key("Decode", key,
            (value & V9X_RS_PO_OFFSET_MASK) << V9X_RS_PO_OFFSET_SHIFT);
    lstrcpyA(key, name);
    lstrcatA(key, "PitchBytes");
    v9x_number("Decode", key,
               ((value >> V9X_RS_PO_PITCH_SHIFT) & V9X_RS_PO_PITCH_MASK) *
               V9X_RS_PO_PITCH_UNIT);
}

/* Memory, the engines and the CP. */
static void v9x_decode_global(void)
{
    DWORD value;

    value = v9x_value(V9X_RS_CONFIG_MEMSIZE);
    v9x_key("Decode", "MemoryBytes", value);
    v9x_number("Decode", "MemoryMiB", value >> 20);
    v9x_key("Decode", "Aperture0Base", v9x_value(V9X_RS_CONFIG_APER_0_BASE));
    v9x_key("Decode", "ApertureSize", v9x_value(V9X_RS_CONFIG_APER_SIZE));
    v9x_key("Decode", "FbStart", v9x_fb_start());

    value = v9x_value(V9X_RS_RBBM_STATUS);
    v9x_yes_no("Decode", "GuiActive", (value & V9X_RS_GUI_ACTIVE) != 0ul);
    v9x_number("Decode", "CmdFifoFree", value & V9X_RS_CMDFIFO_MASK);
    v9x_number("Decode", "CpCsqMode",
               v9x_value(V9X_RS_CP_CSQ_CNTL) >> V9X_RS_CSQ_MODE_SHIFT);

    v9x_decode_pitch_offset("Dst", v9x_value(V9X_RS_DST_PITCH_OFFSET));
    v9x_decode_pitch_offset("Src", v9x_value(V9X_RS_SRC_PITCH_OFFSET));
    v9x_key("Decode", "DefaultOffset", v9x_value(V9X_RS_DEFAULT_OFFSET));
    v9x_key("Decode", "DefaultPitch", v9x_value(V9X_RS_DEFAULT_PITCH));

    v9x_number("Decode", "GbPipes",
               ((v9x_value(V9X_RS_GB_PIPE_SELECT) >> V9X_RS_PIPES_SHIFT) &
                V9X_RS_PIPES_MASK) + 1ul);
    v9x_yes_no("Decode", "VapPvsBypass",
               (v9x_value(V9X_RS_VAP_CNTL_STATUS) & V9X_RS_PVS_BYPASS) != 0ul);
}

/* Each display controller's scanout. FbOffset is the primary surface
 * address less the frame buffer's start, which the plan expects to equal
 * the VBE frame buffer's offset into BAR0 for the controller VBE drives. */
static void v9x_decode_crtc(DWORD crtc)
{
    static const char * const sections[V9X_RS_CRTC_COUNT] = { "D1", "D2" };
    const char *section;
    DWORD base;
    DWORD value;

    section = sections[crtc];
    base = v9x_crtc_offsets[crtc];

    v9x_yes_no(section, "VgaMode",
               (v9x_value(v9x_vga_controls[crtc]) & V9X_RS_ENABLE_BIT) != 0ul);
    v9x_yes_no(section, "CrtcEnabled",
               (v9x_value(V9X_RS_CRTC_CONTROL + base) &
                V9X_RS_ENABLE_BIT) != 0ul);
    v9x_yes_no(section, "GrphEnabled",
               (v9x_value(V9X_RS_GRPH_ENABLE + base) &
                V9X_RS_ENABLE_BIT) != 0ul);

    value = v9x_value(V9X_RS_GRPH_CONTROL + base);
    v9x_text(section, "BitsPerPixel",
             v9x_grph_depths[value & V9X_RS_GRPH_DEPTH_MASK]);
    v9x_number(section, "Format",
               (value >> V9X_RS_GRPH_FORMAT_SHIFT) & V9X_RS_GRPH_FORMAT_MASK);
    v9x_number(section, "PitchPixels",
               v9x_value(V9X_RS_GRPH_PITCH + base) & V9X_RS_GRPH_PITCH_MASK);

    value = v9x_value(V9X_RS_VIEWPORT_SIZE + base);
    v9x_number(section, "ViewportWidth",
               (value >> 16) & V9X_RS_VIEWPORT_MASK);
    v9x_number(section, "ViewportHeight", value & V9X_RS_VIEWPORT_MASK);

    v9x_key(section, "Surface", v9x_value(V9X_RS_GRPH_PRIMARY + base));
    v9x_key(section, "FbOffset",
            v9x_value(V9X_RS_GRPH_PRIMARY + base) - v9x_fb_start());

    value = v9x_value(V9X_RS_GRPH_UPDATE + base);
    v9x_yes_no(section, "UpdatePending",
               (value & V9X_RS_UPDATE_PENDING) != 0ul);
    v9x_yes_no(section, "UpdateLocked", (value & V9X_RS_UPDATE_LOCK) != 0ul);
    v9x_yes_no(section, "InVblank",
               (v9x_value(V9X_RS_CRTC_STATUS + base) &
                V9X_RS_IN_VBLANK) != 0ul);
}

void __stdcall V9xRs690MmioEntry(void)
{
    DWORD config;
    DWORD crtc;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_RSMM_PATH);
    v9x_text("Report", "Tool", "V9XRSMM");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Access", "mmio-read-only");

    config = v9x_find_device();
    if (config == 0ul) {
        v9x_text("Report", "Result", "NO-RS690");
        WritePrivateProfileStringA(0, 0, 0, V9X_RSMM_PATH);
        ExitProcess(1u);
    }
    v9x_key("Report", "Config", config);
    v9x_key("Report", "Id", v9x_config_read(config, 0x00ul));
    v9x_key("Report", "CommandStatus", v9x_config_read(config, 0x04ul));
    v9x_key("Report", "ClassRevision", v9x_config_read(config, 0x08ul));
    v9x_key("Report", "Bar0", v9x_config_read(config, 0x10ul));
    v9x_key("Report", "Bar1", v9x_config_read(config, 0x14ul));
    v9x_key("Report", "Bar2", v9x_config_read(config, 0x18ul));
    v9x_key("Report", "Bar3", v9x_config_read(config, 0x1cul));
    v9x_key("Report", "Bar4", v9x_config_read(config, 0x20ul));
    v9x_key("Report", "Bar5", v9x_config_read(config, 0x24ul));
    v9x_key("Report", "Subsystem", v9x_config_read(config, 0x2cul));
    v9x_key("Report", "RomBase", v9x_config_read(config, 0x30ul));

    v9x_request.config = config;
    v9x_request_build();
    if (!v9x_capture()) {
        v9x_key("Report", "Status", v9x_result.status);
        v9x_key("Report", "MemSize", v9x_result.memsize);
        v9x_key("Report", "PciCopy", v9x_result.pci_copy);
        v9x_key("Report", "RefusedOffset", v9x_result.refused_offset);
        WritePrivateProfileStringA(0, 0, 0, V9X_RSMM_PATH);
        ExitProcess(2u);
    }
    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "MemSize", v9x_result.memsize);
    v9x_key("Report", "PciCopy", v9x_result.pci_copy);
    v9x_number("Report", "Accepted", v9x_result.accepted);
    v9x_publish_mmio();
    v9x_decode_global();
    for (crtc = 0ul; crtc < V9X_RS_CRTC_COUNT; ++crtc) {
        v9x_decode_crtc(crtc);
    }
    v9x_text("Report", "Result", "COMPLETE");
    WritePrivateProfileStringA(0, 0, 0, V9X_RSMM_PATH);
    ExitProcess(0u);
}
