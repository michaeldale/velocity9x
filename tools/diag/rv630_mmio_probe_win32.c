/*
 * V9XRVMM.EXE: ATI Radeon HD 2600 XT (RV630, 1002:9588) read-only register
 * snapshot.
 *
 * Phase 2 of docs\plans\ati-rv630-hd2600xt.md. Finds 1002:9588 by a PCI
 * scan (configuration mechanism #1, CF8h restored after each read), loads
 * RVMM.VXD, and asks it for register offsets through the 64 KiB register
 * BAR, each read twice. Writes C:\V9XDIAG\V9XRVMM.INI:
 *
 *   [Report]  build, the device found, its BARs, the VxD's status;
 *   [Mmio]    every offset read, as R<offset>=first[,second if it moved],
 *             with Name.<offset> from Linux radeon's headers;
 *   [Decode]  memory size and channels, where the frame buffer sits in the
 *             GPU's address space (MC_VM_FB_LOCATION), whether the GPU,
 *             CP and DMA engine are idle or running, and the enabled pipes
 *             and render backends;
 *   [D1] [D2] each display controller: VGA or AVIVO scanout, depth,
 *             pitch, viewport, surface address and its offset into the
 *             frame buffer.
 *
 * What the plan needs from it: the real memory size, where the frame
 * buffer sits in the GPU's address space, whether a VBE mode scans out
 * through D1GRPH (so flips are a register write), and whether the BIOS
 * left the CP and the DMA engine halted. Nothing on the device is written.
 *
 * The rs690_mmio_probe_win32.c publisher is the model: the display block
 * is the same AVIVO one, the rest is R600's register file.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_RVMM_PATH        V9X_DIAG_PATH("V9XRVMM.INI")
#define V9X_RVMM_VXD         "\\\\.\\RVMM.VXD"
#define V9X_RVMM_MAGIC       0x4d4d5652ul
#define V9X_RVMM_CAPTURE     1u
#define V9X_RVMM_OFFSET_MAX  256u
#define V9X_RVMM_HEADER      16u
#define V9X_RVMM_MMIO_BYTES  0x00010000ul
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_PCI_ENABLE       0x80000000ul
#define V9X_PCI_BUSES        16ul
#define V9X_PCI_DEVICES      32ul
#define V9X_VENDOR_ATI       0x1002ul
#define V9X_DEVICE_RV630     0x9588ul

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

/* Register offsets the decode reads back: r600d.h in
 * build\external\evergreen\linux-radeon\ and r500_reg.h in
 * build\external\rs690\linux-radeon\. */
#define V9X_RV_CHMAP               0x2004ul
#define V9X_RV_MC_VM_FB_LOCATION   0x2180ul
#define V9X_RV_RAMCFG              0x2408ul
#define V9X_RV_HDP_NONSURFACE_BASE 0x2c04ul
#define V9X_RV_CONFIG_MEMSIZE      0x5428ul
#define V9X_RV_GRBM_STATUS         0x8010ul
#define V9X_RV_CP_ME_CNTL          0x86d8ul
#define V9X_RV_CC_GC_SHADER_PIPE   0x8950ul
#define V9X_RV_CC_RB_BACKEND       0x98f4ul
#define V9X_RV_CP_RB_CNTL          0xc104ul
#define V9X_RV_DMA_RB_CNTL         0xd000ul
#define V9X_RV_DMA_STATUS_REG      0xd034ul
#define V9X_RV_D1VGA_CONTROL       0x0330ul
#define V9X_RV_D2VGA_CONTROL       0x0338ul
#define V9X_RV_CRTC_CONTROL        0x6080ul
#define V9X_RV_CRTC_STATUS         0x609cul
#define V9X_RV_GRPH_ENABLE         0x6100ul
#define V9X_RV_GRPH_CONTROL        0x6104ul
#define V9X_RV_GRPH_PRIMARY        0x6110ul
#define V9X_RV_GRPH_PITCH          0x6120ul
#define V9X_RV_GRPH_UPDATE         0x6144ul
#define V9X_RV_VIEWPORT_SIZE       0x6584ul

/* Field layouts, same sources. MC_VM_FB_LOCATION 15:0 the start and 31:16
 * the top, each the GPU address >> 24 (r600.c r600_mc_program). CHMAP
 * 13:12 NOOFCHAN; RAMCFG bit 7 CHANSIZE, bit 10 CHANSIZE_OVERRIDE
 * (r600_mc_init). GRBM_STATUS bit 31 GUI_ACTIVE. CP_ME_CNTL bit 28
 * CP_ME_HALT, bit 26 CP_PFP_HALT. CP_RB_CNTL and DMA_RB_CNTL: 5:0 / 5:1 the
 * ring size; DMA_RB_CNTL bit 0 DMA_RB_ENABLE. DMA_STATUS_REG bit 0
 * DMA_IDLE. CC_GC_SHADER_PIPE_CONFIG 15:8 INACTIVE_QD_PIPES, 23:16
 * INACTIVE_SIMDS; CC_RB_BACKEND_DISABLE 23:16 the disabled backends.
 * AVIVO display fields as in the RS690 probe (r500_reg.h). */
#define V9X_RV_FB_FIELD_MASK       0xfffful
#define V9X_RV_FB_UNIT_SHIFT       24u
#define V9X_RV_NOOFCHAN_SHIFT      12u
#define V9X_RV_NOOFCHAN_MASK       0x3ul
#define V9X_RV_CHANSIZE_64         0x00000080ul
#define V9X_RV_CHANSIZE_OVERRIDE   0x00000400ul
#define V9X_RV_GUI_ACTIVE          0x80000000ul
#define V9X_RV_CP_ME_HALT          0x10000000ul
#define V9X_RV_CP_PFP_HALT         0x04000000ul
#define V9X_RV_CP_RB_BUFSZ_MASK    0x3ful
#define V9X_RV_DMA_RB_SIZE_SHIFT   1u
#define V9X_RV_DMA_RB_SIZE_MASK    0x1ful
#define V9X_RV_DMA_IDLE            0x00000001ul
#define V9X_RV_QD_PIPES_SHIFT      8u
#define V9X_RV_SIMDS_SHIFT         16u
#define V9X_RV_BACKENDS_SHIFT      16u
#define V9X_RV_BYTE_MASK           0xfful
#define V9X_RV_ENABLE_BIT          0x00000001ul
#define V9X_RV_GRPH_DEPTH_MASK     0x3ul
#define V9X_RV_GRPH_FORMAT_SHIFT   8u
#define V9X_RV_GRPH_FORMAT_MASK    0x7ul
#define V9X_RV_GRPH_PITCH_MASK     0x3ffful
#define V9X_RV_UPDATE_PENDING      0x00000004ul
#define V9X_RV_UPDATE_LOCK         0x00010000ul
#define V9X_RV_IN_VBLANK           0x00000001ul
#define V9X_RV_VIEWPORT_MASK       0x3ffful

/* The two display controllers, the second 0800h above the first
 * (r500_reg.h). */
#define V9X_RV_CRTC_COUNT 2u
static const DWORD v9x_crtc_offsets[V9X_RV_CRTC_COUNT] = {
    0x0000ul, 0x0800ul
};
static const DWORD v9x_vga_controls[V9X_RV_CRTC_COUNT] = {
    V9X_RV_D1VGA_CONTROL, V9X_RV_D2VGA_CONTROL
};

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_rvmm_register {
    DWORD offset;
    const char *name;
};

/* Bus, memory, engine and CP state. The VGA block names other than the
 * render and D1/D2 controls are Evergreen's, carried over as labels, not
 * claims. */
static const struct v9x_rvmm_register v9x_global[] = {
    { 0x0300ul, "VGA_RENDER_CONTROL" },
    { 0x0310ul, "VGA_MEMORY_BASE_ADDRESS" },
    { 0x0328ul, "VGA_HDP_CONTROL" },
    { 0x0330ul, "D1VGA_CONTROL" },
    { 0x0338ul, "D2VGA_CONTROL" },
    { 0x0e50ul, "SRBM_STATUS" },
    { 0x0e60ul, "SRBM_SOFT_RESET" },
    { 0x1400ul, "VM_L2_CNTL" },
    { 0x1404ul, "VM_L2_CNTL2" },
    { 0x1408ul, "VM_L2_CNTL3" },
    { 0x1410ul, "VM_CONTEXT0_CNTL" },
    { 0x2004ul, "CHMAP" },
    { 0x2180ul, "MC_VM_FB_LOCATION" },
    { 0x2184ul, "MC_VM_AGP_TOP" },
    { 0x2188ul, "MC_VM_AGP_BOT" },
    { 0x218cul, "MC_VM_AGP_BASE" },
    { 0x2190ul, "MC_VM_SYSTEM_APERTURE_LOW_ADDR" },
    { 0x2194ul, "MC_VM_SYSTEM_APERTURE_HIGH_ADDR" },
    { 0x2198ul, "MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR" },
    { 0x219cul, "MC_VM_L1_TLB_MCD_RD_A_CNTL" },
    { 0x2408ul, "RAMCFG" },
    { 0x2c00ul, "HDP_HOST_PATH_CNTL" },
    { 0x2c04ul, "HDP_NONSURFACE_BASE" },
    { 0x2c08ul, "HDP_NONSURFACE_INFO" },
    { 0x2c0cul, "HDP_NONSURFACE_SIZE" },
    { 0x3f00ul, "RLC_CNTL" },
    { 0x5420ul, "BUS_CNTL" },
    { 0x5424ul, "CONFIG_CNTL" },
    { 0x5428ul, "CONFIG_MEMSIZE" },
    { 0x8010ul, "GRBM_STATUS" },
    { 0x8014ul, "GRBM_STATUS2" },
    { 0x8020ul, "GRBM_SOFT_RESET" },
    { 0x8500ul, "SCRATCH_REG0" },
    { 0x8540ul, "SCRATCH_UMSK" },
    { 0x8680ul, "CP_STAT" },
    { 0x86d8ul, "CP_ME_CNTL" },
    { 0x8700ul, "CP_RB_RPTR" },
    { 0x8704ul, "CP_RB_WPTR_DELAY" },
    { 0x8760ul, "CP_QUEUE_THRESHOLDS" },
    { 0x8950ul, "CC_GC_SHADER_PIPE_CONFIG" },
    { 0x8954ul, "GC_USER_SHADER_PIPE_CONFIG" },
    { 0x98f0ul, "GB_TILING_CONFIG" },
    { 0x98f4ul, "CC_RB_BACKEND_DISABLE" },
    { 0xc100ul, "CP_RB_BASE" },
    { 0xc104ul, "CP_RB_CNTL" },
    { 0xc108ul, "CP_RB_RPTR_WR" },
    { 0xc10cul, "CP_RB_RPTR_ADDR" },
    { 0xc114ul, "CP_RB_WPTR" },
    { 0xd000ul, "DMA_RB_CNTL" },
    { 0xd004ul, "DMA_RB_BASE" },
    { 0xd008ul, "DMA_RB_RPTR" },
    { 0xd00cul, "DMA_RB_WPTR" },
    { 0xd024ul, "DMA_IB_CNTL" },
    { 0xd02cul, "DMA_CNTL" },
    { 0xd034ul, "DMA_STATUS_REG" }
};

/* The display globals. */
static const struct v9x_rvmm_register v9x_display[] = {
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
static const struct v9x_rvmm_register v9x_crtc[] = {
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
#define V9X_DISPLAY_COUNT (sizeof(v9x_display) / sizeof(v9x_display[0]))
#define V9X_CRTC_REG_COUNT (sizeof(v9x_crtc) / sizeof(v9x_crtc[0]))

/* Every offset fits one request: the VxD takes 256. */
typedef char v9x_rvmm_list_fits[
    V9X_GLOBAL_COUNT + V9X_DISPLAY_COUNT +
    V9X_CRTC_REG_COUNT * V9X_RV_CRTC_COUNT <=
    V9X_RVMM_OFFSET_MAX ? 1 : -1];

static const char * const v9x_grph_depths[4] = { "8", "16", "32", "64" };

struct v9x_rvmm_request {
    DWORD config;
    DWORD count;
    DWORD offsets[V9X_RVMM_OFFSET_MAX];
};

struct v9x_rvmm_result {
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
    DWORD reserved[6];
    DWORD offsets[V9X_RVMM_OFFSET_MAX];
    DWORD first[V9X_RVMM_OFFSET_MAX];
    DWORD second[V9X_RVMM_OFFSET_MAX];
};

/* The header must be the VxD's RV_HEADER_DWORDS long. */
typedef char v9x_rvmm_header_matches[
    offsetof(struct v9x_rvmm_result, offsets) ==
    sizeof(DWORD) * V9X_RVMM_HEADER ? 1 : -1];

static struct v9x_rvmm_request v9x_request;
static struct v9x_rvmm_result v9x_result;
static DWORD v9x_values[V9X_RVMM_MMIO_BYTES / 4u];

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
    WritePrivateProfileStringA(section, key, text, V9X_RVMM_PATH);
}

static void v9x_number(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_decimal(text, value);
    WritePrivateProfileStringA(section, key, text, V9X_RVMM_PATH);
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_RVMM_PATH);
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

static void v9x_request_list(const struct v9x_rvmm_register *list,
                             DWORD count)
{
    DWORD index;

    for (index = 0ul; index < count; ++index) {
        v9x_request_add(list[index].offset, "", list[index].name);
    }
}

static void v9x_request_build(void)
{
    static const char * const prefixes[V9X_RV_CRTC_COUNT] = {
        "D1.", "D2."
    };
    DWORD index;
    DWORD crtc;

    v9x_request.count = 0ul;
    v9x_request_list(v9x_global, V9X_GLOBAL_COUNT);
    v9x_request_list(v9x_display, V9X_DISPLAY_COUNT);
    for (crtc = 0ul; crtc < V9X_RV_CRTC_COUNT; ++crtc) {
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

    device = CreateFileA(V9X_RVMM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        return 0;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_RVMM_CAPTURE, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_RVMM_MAGIC) {
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

/* The first 1002:9588 on buses 0-15, function 0; 0 if none. A PCI Express
 * card sits behind a root port's bridge, so not on bus 0. */
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
                (id >> 16) == V9X_DEVICE_RV630) {
                return config;
            }
        }
    }
    return 0ul;
}

static DWORD v9x_fb_start(void)
{
    return (v9x_value(V9X_RV_MC_VM_FB_LOCATION) & V9X_RV_FB_FIELD_MASK) <<
           V9X_RV_FB_UNIT_SHIFT;
}

/* Memory, the engines and the CP. */
static void v9x_decode_global(void)
{
    DWORD value;

    value = v9x_value(V9X_RV_CONFIG_MEMSIZE);
    v9x_key("Decode", "MemoryBytes", value);
    v9x_number("Decode", "MemoryMiB", value >> 20);

    value = v9x_value(V9X_RV_MC_VM_FB_LOCATION);
    v9x_key("Decode", "FbStart", v9x_fb_start());
    v9x_key("Decode", "FbTop",
            ((value >> 16) & V9X_RV_FB_FIELD_MASK) << V9X_RV_FB_UNIT_SHIFT);
    v9x_key("Decode", "HdpNonsurfaceBase",
            v9x_value(V9X_RV_HDP_NONSURFACE_BASE));

    v9x_number("Decode", "MemoryChannelsField",
               (v9x_value(V9X_RV_CHMAP) >> V9X_RV_NOOFCHAN_SHIFT) &
               V9X_RV_NOOFCHAN_MASK);
    value = v9x_value(V9X_RV_RAMCFG);
    v9x_yes_no("Decode", "ChannelSize64", (value & V9X_RV_CHANSIZE_64) != 0ul);
    v9x_yes_no("Decode", "ChannelSizeOverride",
               (value & V9X_RV_CHANSIZE_OVERRIDE) != 0ul);

    v9x_yes_no("Decode", "GuiActive",
               (v9x_value(V9X_RV_GRBM_STATUS) & V9X_RV_GUI_ACTIVE) != 0ul);
    value = v9x_value(V9X_RV_CP_ME_CNTL);
    v9x_yes_no("Decode", "CpMeHalted", (value & V9X_RV_CP_ME_HALT) != 0ul);
    v9x_yes_no("Decode", "CpPfpHalted", (value & V9X_RV_CP_PFP_HALT) != 0ul);
    v9x_number("Decode", "CpRbBufSzField",
               v9x_value(V9X_RV_CP_RB_CNTL) & V9X_RV_CP_RB_BUFSZ_MASK);

    value = v9x_value(V9X_RV_DMA_RB_CNTL);
    v9x_yes_no("Decode", "DmaRingEnabled", (value & V9X_RV_ENABLE_BIT) != 0ul);
    v9x_number("Decode", "DmaRbSizeField",
               (value >> V9X_RV_DMA_RB_SIZE_SHIFT) & V9X_RV_DMA_RB_SIZE_MASK);
    v9x_yes_no("Decode", "DmaIdle",
               (v9x_value(V9X_RV_DMA_STATUS_REG) & V9X_RV_DMA_IDLE) != 0ul);

    value = v9x_value(V9X_RV_CC_GC_SHADER_PIPE);
    v9x_key("Decode", "InactiveQdPipes",
            (value >> V9X_RV_QD_PIPES_SHIFT) & V9X_RV_BYTE_MASK);
    v9x_key("Decode", "InactiveSimds",
            (value >> V9X_RV_SIMDS_SHIFT) & V9X_RV_BYTE_MASK);
    v9x_key("Decode", "DisabledBackends",
            (v9x_value(V9X_RV_CC_RB_BACKEND) >> V9X_RV_BACKENDS_SHIFT) &
            V9X_RV_BYTE_MASK);
}

/* Each display controller's scanout. FbOffset is the primary surface
 * address less the frame buffer's start, which the plan expects to equal
 * the VBE frame buffer's offset into BAR0 for the controller VBE drives. */
static void v9x_decode_crtc(DWORD crtc)
{
    static const char * const sections[V9X_RV_CRTC_COUNT] = { "D1", "D2" };
    const char *section;
    DWORD base;
    DWORD value;

    section = sections[crtc];
    base = v9x_crtc_offsets[crtc];

    v9x_yes_no(section, "VgaMode",
               (v9x_value(v9x_vga_controls[crtc]) & V9X_RV_ENABLE_BIT) != 0ul);
    v9x_yes_no(section, "CrtcEnabled",
               (v9x_value(V9X_RV_CRTC_CONTROL + base) &
                V9X_RV_ENABLE_BIT) != 0ul);
    v9x_yes_no(section, "GrphEnabled",
               (v9x_value(V9X_RV_GRPH_ENABLE + base) &
                V9X_RV_ENABLE_BIT) != 0ul);

    value = v9x_value(V9X_RV_GRPH_CONTROL + base);
    v9x_text(section, "BitsPerPixel",
             v9x_grph_depths[value & V9X_RV_GRPH_DEPTH_MASK]);
    v9x_number(section, "Format",
               (value >> V9X_RV_GRPH_FORMAT_SHIFT) & V9X_RV_GRPH_FORMAT_MASK);
    v9x_number(section, "PitchPixels",
               v9x_value(V9X_RV_GRPH_PITCH + base) & V9X_RV_GRPH_PITCH_MASK);

    value = v9x_value(V9X_RV_VIEWPORT_SIZE + base);
    v9x_number(section, "ViewportWidth",
               (value >> 16) & V9X_RV_VIEWPORT_MASK);
    v9x_number(section, "ViewportHeight", value & V9X_RV_VIEWPORT_MASK);

    v9x_key(section, "Surface", v9x_value(V9X_RV_GRPH_PRIMARY + base));
    v9x_key(section, "FbOffset",
            v9x_value(V9X_RV_GRPH_PRIMARY + base) - v9x_fb_start());

    value = v9x_value(V9X_RV_GRPH_UPDATE + base);
    v9x_yes_no(section, "UpdatePending",
               (value & V9X_RV_UPDATE_PENDING) != 0ul);
    v9x_yes_no(section, "UpdateLocked", (value & V9X_RV_UPDATE_LOCK) != 0ul);
    v9x_yes_no(section, "InVblank",
               (v9x_value(V9X_RV_CRTC_STATUS + base) &
                V9X_RV_IN_VBLANK) != 0ul);
}

void __stdcall V9xRv630MmioEntry(void)
{
    DWORD config;
    DWORD crtc;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_RVMM_PATH);
    v9x_text("Report", "Tool", "V9XRVMM");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Access", "mmio-read-only");

    config = v9x_find_device();
    if (config == 0ul) {
        v9x_text("Report", "Result", "NO-RV630");
        WritePrivateProfileStringA(0, 0, 0, V9X_RVMM_PATH);
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
        v9x_key("Report", "RefusedOffset", v9x_result.refused_offset);
        WritePrivateProfileStringA(0, 0, 0, V9X_RVMM_PATH);
        ExitProcess(2u);
    }
    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "MemSize", v9x_result.memsize);
    v9x_number("Report", "Accepted", v9x_result.accepted);
    v9x_publish_mmio();
    v9x_decode_global();
    for (crtc = 0ul; crtc < V9X_RV_CRTC_COUNT; ++crtc) {
        v9x_decode_crtc(crtc);
    }
    v9x_text("Report", "Result", "COMPLETE");
    WritePrivateProfileStringA(0, 0, 0, V9X_RVMM_PATH);
    ExitProcess(0u);
}
