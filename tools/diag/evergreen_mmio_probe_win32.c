/*
 * V9XEGMM.EXE: AMD Evergreen (Radeon HD 5450, Cedar) read-only register
 * snapshot.
 *
 * Phase 2 of docs\plans\ati-evergreen-cedar.md. Finds 1002:68F9 by a PCI
 * scan (configuration mechanism #1, CF8h restored after each read), loads
 * EGMM.VXD, and asks it for register offsets through the 128 KiB register
 * BAR, each read twice. Writes C:\V9XDIAG\V9XEGMM.INI:
 *
 *   [Report]  build, the device found, its BARs (both halves of the 64-bit
 *             ones), the VxD's status;
 *   [Mmio]    every offset read, as R<offset>=first[,second if it moved],
 *             with Name.<offset> from Linux radeon's headers;
 *   [Decode]  memory size and width, the memory controller's view of VRAM
 *             and system memory, the GART, the CP and DMA engines' state,
 *             and each of the four display controllers' scanout.
 *
 * What the plan needs from it: whether the DMA engine is usable as the BIOS
 * left it, where VRAM sits in the GPU's address space, and whether the VBE
 * scanout address is that base plus the LFB offset. Nothing on the device
 * is written.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_EGMM_PATH        V9X_DIAG_PATH("V9XEGMM.INI")
#define V9X_EGMM_VXD         "\\\\.\\EGMM.VXD"
#define V9X_EGMM_MAGIC       0x4d4d4745ul
#define V9X_EGMM_CAPTURE     1u
#define V9X_EGMM_OFFSET_MAX  256u
#define V9X_EGMM_HEADER      16u
#define V9X_EGMM_MMIO_BYTES  0x00020000ul
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_PCI_ENABLE       0x80000000ul
#define V9X_PCI_BUSES        16ul
#define V9X_PCI_DEVICES      32ul
#define V9X_VENDOR_ATI       0x1002ul
#define V9X_DEVICE_CEDAR     0x68f9ul

#define V9X_ST_ATI             0x00000001ul
#define V9X_ST_BASE_INVALID    0x00000002ul
#define V9X_ST_MMIO_MAPPED     0x00000004ul
#define V9X_ST_IDENTITY_MATCH  0x00000008ul
#define V9X_ST_OFFSETS_REFUSED 0x00000010ul
#define V9X_ST_SNAPSHOT_TAKEN  0x00000020ul
#define V9X_ST_BAR_64          0x00000040ul
#define V9X_ST_NOT_CEDAR       0x00000080ul
#define V9X_ST_ADDRESS_INVALID 0x00000100ul
#define V9X_ST_BAR_ABOVE_4G    0x00000200ul

/* Register offsets the decode reads back: evergreend.h, evergreen_reg.h
 * and r600d.h in build\external\evergreen\linux-radeon\. */
#define V9X_EG_SRBM_SOFT_RESET     0x0e60ul
#define V9X_EG_VM_L2_CNTL          0x1400ul
#define V9X_EG_VM_CONTEXT0_CNTL    0x1410ul
#define V9X_EG_MC_SHARED_CHMAP     0x2004ul
#define V9X_EG_MC_VM_FB_LOCATION   0x2024ul
#define V9X_EG_MC_ARB_RAMCFG       0x2760ul
#define V9X_EG_CONFIG_MEMSIZE      0x5428ul
#define V9X_EG_GRBM_STATUS         0x8010ul
#define V9X_EG_CP_ME_CNTL          0x86d8ul
#define V9X_EG_DMA_RB_CNTL         0xd000ul
#define V9X_EG_DMA_STATUS_REG      0xd034ul
#define V9X_EG_GRPH_ENABLE         0x6800ul
#define V9X_EG_GRPH_CONTROL        0x6804ul
#define V9X_EG_GRPH_PRIMARY        0x6810ul
#define V9X_EG_GRPH_PITCH          0x6818ul
#define V9X_EG_GRPH_PRIMARY_HIGH   0x681cul
#define V9X_EG_GRPH_UPDATE         0x6844ul
#define V9X_EG_VIEWPORT_SIZE       0x6d74ul
#define V9X_EG_CRTC_CONTROL        0x6e70ul
#define V9X_EG_CRTC_STATUS         0x6e8cul

/* Field layouts, same sources. SRBM_SOFT_RESET bit 20 SOFT_RESET_DMA.
 * VM_L2_CNTL and VM_CONTEXT0_CNTL bit 0 enable. MC_SHARED_CHMAP 13:12 the
 * channel count (1, 2, 4, 8). MC_ARB_RAMCFG bit 8 64-bit channels, bit 11
 * the 16-bit override. MC_VM_FB_LOCATION 15:0 start and 31:16 end, both
 * in 16 MiB units of the 40-bit GPU address. GRBM_STATUS bit 31
 * GUI_ACTIVE. CP_ME_CNTL bit 28 ME halted, bit 26 PFP halted. DMA_RB_CNTL
 * bit 0 enable, 5:1 log2 of the ring dwords. DMA_STATUS_REG bit 0 idle.
 * GRPH_CONTROL 1:0 depth (8, 16, 32, 64 bpp), 10:8 format. GRPH_UPDATE
 * bit 2 surface update pending. CRTC_CONTROL bit 0 master enable.
 * CRTC_STATUS bit 0 in vertical blank. */
#define V9X_EG_SOFT_RESET_DMA      0x00100000ul
#define V9X_EG_ENABLE_BIT          0x00000001ul
#define V9X_EG_NOOFCHAN_SHIFT      12u
#define V9X_EG_NOOFCHAN_MASK       0x3ul
#define V9X_EG_CHANSIZE_64         0x00000100ul
#define V9X_EG_CHANSIZE_OVERRIDE   0x00000800ul
#define V9X_EG_FB_FIELD_MASK       0xfffful
#define V9X_EG_FB_UNIT_SHIFT       24u
#define V9X_EG_GUI_ACTIVE          0x80000000ul
#define V9X_EG_CP_ME_HALT          0x10000000ul
#define V9X_EG_CP_PFP_HALT         0x04000000ul
#define V9X_EG_DMA_RB_SIZE_SHIFT   1u
#define V9X_EG_DMA_RB_SIZE_MASK    0x1ful
#define V9X_EG_DMA_IDLE            0x00000001ul
#define V9X_EG_GRPH_DEPTH_MASK     0x3ul
#define V9X_EG_GRPH_FORMAT_SHIFT   8u
#define V9X_EG_GRPH_FORMAT_MASK    0x7ul
#define V9X_EG_UPDATE_PENDING      0x00000004ul
#define V9X_EG_IN_VBLANK           0x00000001ul
#define V9X_EG_VIEWPORT_MASK       0x3ffful

/* Cedar's four display controllers (radeon_asic.c: num_crtc = 4), at
 * these offsets from the first (evergreen_reg.h). */
#define V9X_EG_CRTC_COUNT 4u
static const DWORD v9x_crtc_offsets[V9X_EG_CRTC_COUNT] = {
    0x00000ul, 0x00c00ul, 0x09800ul, 0x0a400ul
};

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_egmm_register {
    DWORD offset;
    const char *name;
};

/* Engine, memory controller and bus state. The VGA block names other than
 * the memory base are Linux avivod.h's, which was not among the sources
 * collected; they are labels, not claims. */
static const struct v9x_egmm_register v9x_global[] = {
    { 0x0300ul, "VGA_RENDER_CONTROL" },
    { 0x0310ul, "VGA_MEMORY_BASE_ADDRESS" },
    { 0x0324ul, "VGA_MEMORY_BASE_ADDRESS_HIGH" },
    { 0x0328ul, "VGA_HDP_CONTROL" },
    { 0x0330ul, "D1VGA_CONTROL" },
    { 0x0338ul, "D2VGA_CONTROL" },
    { 0x0e50ul, "SRBM_STATUS" },
    { 0x0e60ul, "SRBM_SOFT_RESET" },
    { 0x0ec4ul, "SRBM_STATUS2" },
    { 0x1400ul, "VM_L2_CNTL" },
    { 0x1410ul, "VM_CONTEXT0_CNTL" },
    { 0x2004ul, "MC_SHARED_CHMAP" },
    { 0x2024ul, "MC_VM_FB_LOCATION" },
    { 0x2028ul, "MC_VM_AGP_TOP" },
    { 0x202cul, "MC_VM_AGP_BOT" },
    { 0x2030ul, "MC_VM_AGP_BASE" },
    { 0x2034ul, "MC_VM_SYSTEM_APERTURE_LOW_ADDR" },
    { 0x2038ul, "MC_VM_SYSTEM_APERTURE_HIGH_ADDR" },
    { 0x203cul, "MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR" },
    { 0x2760ul, "MC_ARB_RAMCFG" },
    { 0x2c04ul, "HDP_NONSURFACE_BASE" },
    { 0x2c08ul, "HDP_NONSURFACE_INFO" },
    { 0x2c0cul, "HDP_NONSURFACE_SIZE" },
    { 0x2f48ul, "HDP_ADDR_CONFIG" },
    { 0x3f00ul, "RLC_CNTL" },
    { 0x5420ul, "BUS_CNTL" },
    { 0x5424ul, "CONFIG_CNTL" },
    { 0x5428ul, "CONFIG_MEMSIZE" },
    { 0x5490ul, "BIF_FB_EN" },
    { 0x8010ul, "GRBM_STATUS" },
    { 0x8014ul, "GRBM_STATUS_SE0" },
    { 0x8020ul, "GRBM_SOFT_RESET" },
    { 0x8680ul, "CP_STAT" },
    { 0x86d8ul, "CP_ME_CNTL" },
    { 0x98f8ul, "GB_ADDR_CONFIG" },
    { 0xc100ul, "CP_RB_BASE" },
    { 0xc104ul, "CP_RB_CNTL" },
    { 0xc114ul, "CP_RB_WPTR" },
    { 0xd000ul, "DMA_RB_CNTL" },
    { 0xd004ul, "DMA_RB_BASE" },
    { 0xd008ul, "DMA_RB_RPTR" },
    { 0xd00cul, "DMA_RB_WPTR" },
    { 0xd01cul, "DMA_RB_RPTR_ADDR_HI" },
    { 0xd020ul, "DMA_RB_RPTR_ADDR_LO" },
    { 0xd024ul, "DMA_IB_CNTL" },
    { 0xd028ul, "DMA_IB_RPTR" },
    { 0xd02cul, "DMA_CNTL" },
    { 0xd034ul, "DMA_STATUS_REG" },
    { 0xd044ul, "DMA_SEM_INCOMPLETE_TIMER_CNTL" },
    { 0xd048ul, "DMA_SEM_WAIT_FAIL_TIMER_CNTL" },
    { 0xd0b8ul, "DMA_TILING_CONFIG" },
    { 0xd0bcul, "DMA_MODE" }
};

/* One display controller's block, at the first controller's offsets. */
static const struct v9x_egmm_register v9x_crtc[] = {
    { 0x6800ul, "GRPH_ENABLE" },
    { 0x6804ul, "GRPH_CONTROL" },
    { 0x680cul, "GRPH_SWAP_CONTROL" },
    { 0x6810ul, "GRPH_PRIMARY_SURFACE_ADDRESS" },
    { 0x6814ul, "GRPH_SECONDARY_SURFACE_ADDRESS" },
    { 0x6818ul, "GRPH_PITCH" },
    { 0x681cul, "GRPH_PRIMARY_SURFACE_ADDRESS_HIGH" },
    { 0x6820ul, "GRPH_SECONDARY_SURFACE_ADDRESS_HIGH" },
    { 0x6824ul, "GRPH_SURFACE_OFFSET_X" },
    { 0x6828ul, "GRPH_SURFACE_OFFSET_Y" },
    { 0x682cul, "GRPH_X_START" },
    { 0x6830ul, "GRPH_Y_START" },
    { 0x6834ul, "GRPH_X_END" },
    { 0x6838ul, "GRPH_Y_END" },
    { 0x6844ul, "GRPH_UPDATE" },
    { 0x6848ul, "GRPH_FLIP_CONTROL" },
    { 0x6998ul, "CUR_CONTROL" },
    { 0x699cul, "CUR_SURFACE_ADDRESS" },
    { 0x69a0ul, "CUR_SIZE" },
    { 0x69a4ul, "CUR_SURFACE_ADDRESS_HIGH" },
    { 0x69a8ul, "CUR_POSITION" },
    { 0x69acul, "CUR_HOT_SPOT" },
    { 0x69b0ul, "CUR_COLOR1" },
    { 0x69b4ul, "CUR_COLOR2" },
    { 0x69b8ul, "CUR_UPDATE" },
    { 0x69e0ul, "DC_LUT_RW_MODE" },
    { 0x69e4ul, "DC_LUT_RW_INDEX" },
    { 0x69f4ul, "DC_LUT_VGA_ACCESS_ENABLE" },
    { 0x69f8ul, "DC_LUT_WRITE_EN_MASK" },
    { 0x69fcul, "DC_LUT_AUTOFILL" },
    { 0x6a00ul, "DC_LUT_CONTROL" },
    { 0x6d70ul, "VIEWPORT_START" },
    { 0x6d74ul, "VIEWPORT_SIZE" },
    { 0x6e34ul, "CRTC_V_BLANK_START_END" },
    { 0x6e70ul, "CRTC_CONTROL" },
    { 0x6e74ul, "CRTC_BLANK_CONTROL" },
    { 0x6e8cul, "CRTC_STATUS" },
    { 0x6e90ul, "CRTC_STATUS_POSITION" },
    { 0x6ea0ul, "CRTC_STATUS_HV_COUNT" },
    { 0x6ed4ul, "CRTC_UPDATE_LOCK" },
    { 0x6ef4ul, "MASTER_UPDATE_LOCK" },
    { 0x6ef8ul, "MASTER_UPDATE_MODE" }
};

#define V9X_GLOBAL_COUNT (sizeof(v9x_global) / sizeof(v9x_global[0]))
#define V9X_CRTC_REG_COUNT (sizeof(v9x_crtc) / sizeof(v9x_crtc[0]))

/* Every offset fits one request: the VxD takes 256. */
typedef char v9x_egmm_list_fits[
    V9X_GLOBAL_COUNT + V9X_CRTC_REG_COUNT * V9X_EG_CRTC_COUNT <=
    V9X_EGMM_OFFSET_MAX ? 1 : -1];

static const char * const v9x_grph_depths[4] = { "8", "16", "32", "64" };

struct v9x_egmm_request {
    DWORD config;
    DWORD count;
    DWORD offsets[V9X_EGMM_OFFSET_MAX];
};

struct v9x_egmm_result {
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
    DWORD offsets[V9X_EGMM_OFFSET_MAX];
    DWORD first[V9X_EGMM_OFFSET_MAX];
    DWORD second[V9X_EGMM_OFFSET_MAX];
};

/* The header must be the VxD's EG_HEADER_DWORDS long. */
typedef char v9x_egmm_header_matches[
    offsetof(struct v9x_egmm_result, offsets) ==
    sizeof(DWORD) * V9X_EGMM_HEADER ? 1 : -1];

static struct v9x_egmm_request v9x_request;
static struct v9x_egmm_result v9x_result;
static DWORD v9x_values[V9X_EGMM_MMIO_BYTES / 4u];

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
    WritePrivateProfileStringA(section, key, text, V9X_EGMM_PATH);
}

static void v9x_number(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_decimal(text, value);
    WritePrivateProfileStringA(section, key, text, V9X_EGMM_PATH);
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_EGMM_PATH);
}

static void v9x_yes_no(const char *section, const char *key, int value)
{
    v9x_text(section, key, value ? "yes" : "no");
}

/* Offsets reach 112FCh, so five hex digits. */
static void v9x_offset_key(char *key, const char *prefix, DWORD offset)
{
    lstrcpyA(key, prefix);
    v9x_hex(key + lstrlenA(prefix), offset, 5);
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

static void v9x_request_build(void)
{
    static const char * const prefixes[V9X_EG_CRTC_COUNT] = {
        "D1.", "D2.", "D3.", "D4."
    };
    DWORD index;
    DWORD crtc;

    v9x_request.count = 0ul;
    for (index = 0ul; index < V9X_GLOBAL_COUNT; ++index) {
        v9x_request_add(v9x_global[index].offset, "", v9x_global[index].name);
    }
    for (crtc = 0ul; crtc < V9X_EG_CRTC_COUNT; ++crtc) {
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

    device = CreateFileA(V9X_EGMM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        return 0;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_EGMM_CAPTURE, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_EGMM_MAGIC) {
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

/* The first 1002:68F9 on buses 0-15, function 0; 0 if none. A PCIe card
 * sits behind a root port, so the bus is rarely 0 (M-BXMFA8's Enum key
 * reads 000800). */
static DWORD v9x_find_cedar(void)
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
                (id >> 16) == V9X_DEVICE_CEDAR) {
                return config;
            }
        }
    }
    return 0ul;
}

/* The memory controller, the GART and the engines. FB_LOCATION's fields
 * are 16 MiB units of a 40-bit address, so the low 32 bits of the start
 * are the field's low byte shifted up and bits 39:32 are its high byte. */
static void v9x_decode_global(void)
{
    DWORD value;
    DWORD channels;
    DWORD width;

    v9x_number("Decode", "MemoryMiB", v9x_value(V9X_EG_CONFIG_MEMSIZE));

    channels = 1ul << ((v9x_value(V9X_EG_MC_SHARED_CHMAP) >>
                        V9X_EG_NOOFCHAN_SHIFT) & V9X_EG_NOOFCHAN_MASK);
    value = v9x_value(V9X_EG_MC_ARB_RAMCFG);
    if ((value & V9X_EG_CHANSIZE_OVERRIDE) != 0ul) {
        width = 16ul;
    } else if ((value & V9X_EG_CHANSIZE_64) != 0ul) {
        width = 64ul;
    } else {
        width = 32ul;
    }
    v9x_number("Decode", "MemoryChannels", channels);
    v9x_number("Decode", "MemoryWidthBits", channels * width);

    value = v9x_value(V9X_EG_MC_VM_FB_LOCATION);
    v9x_key("Decode", "FbStartHigh",
            (value & V9X_EG_FB_FIELD_MASK) >> 8);
    v9x_key("Decode", "FbStartLow",
            (value & 0xfful) << V9X_EG_FB_UNIT_SHIFT);
    v9x_key("Decode", "FbEndHigh",
            ((value >> 16) & V9X_EG_FB_FIELD_MASK) >> 8);
    v9x_key("Decode", "FbEndLow",
            (((value >> 16) & 0xfful) << V9X_EG_FB_UNIT_SHIFT) | 0x00fffffful);

    v9x_yes_no("Decode", "GartL2Enabled",
               (v9x_value(V9X_EG_VM_L2_CNTL) & V9X_EG_ENABLE_BIT) != 0ul);
    v9x_yes_no("Decode", "GartContext0Enabled",
               (v9x_value(V9X_EG_VM_CONTEXT0_CNTL) & V9X_EG_ENABLE_BIT) != 0ul);
    v9x_yes_no("Decode", "GuiActive",
               (v9x_value(V9X_EG_GRBM_STATUS) & V9X_EG_GUI_ACTIVE) != 0ul);

    value = v9x_value(V9X_EG_CP_ME_CNTL);
    v9x_yes_no("Decode", "CpMeHalted", (value & V9X_EG_CP_ME_HALT) != 0ul);
    v9x_yes_no("Decode", "CpPfpHalted", (value & V9X_EG_CP_PFP_HALT) != 0ul);

    v9x_yes_no("Decode", "DmaHeldInReset",
               (v9x_value(V9X_EG_SRBM_SOFT_RESET) &
                V9X_EG_SOFT_RESET_DMA) != 0ul);
    value = v9x_value(V9X_EG_DMA_RB_CNTL);
    v9x_yes_no("Decode", "DmaRingEnabled",
               (value & V9X_EG_ENABLE_BIT) != 0ul);
    v9x_number("Decode", "DmaRingLog2Dwords",
               (value >> V9X_EG_DMA_RB_SIZE_SHIFT) & V9X_EG_DMA_RB_SIZE_MASK);
    v9x_yes_no("Decode", "DmaIdle",
               (v9x_value(V9X_EG_DMA_STATUS_REG) & V9X_EG_DMA_IDLE) != 0ul);
}

/* Each display controller's scanout. LfbOffset is the primary surface
 * address less the FB start, which the plan expects to equal the VBE
 * framebuffer's offset into BAR0 for the controller VBE drives. */
static void v9x_decode_crtc(DWORD crtc)
{
    static const char * const sections[V9X_EG_CRTC_COUNT] = {
        "D1", "D2", "D3", "D4"
    };
    const char *section;
    DWORD base;
    DWORD value;
    DWORD fb_low;

    section = sections[crtc];
    base = v9x_crtc_offsets[crtc];

    v9x_yes_no(section, "CrtcEnabled",
               (v9x_value(V9X_EG_CRTC_CONTROL + base) &
                V9X_EG_ENABLE_BIT) != 0ul);
    v9x_yes_no(section, "GrphEnabled",
               (v9x_value(V9X_EG_GRPH_ENABLE + base) &
                V9X_EG_ENABLE_BIT) != 0ul);

    value = v9x_value(V9X_EG_GRPH_CONTROL + base);
    v9x_text(section, "BitsPerPixel",
             v9x_grph_depths[value & V9X_EG_GRPH_DEPTH_MASK]);
    v9x_number(section, "Format",
               (value >> V9X_EG_GRPH_FORMAT_SHIFT) & V9X_EG_GRPH_FORMAT_MASK);
    v9x_number(section, "PitchPixels", v9x_value(V9X_EG_GRPH_PITCH + base));

    value = v9x_value(V9X_EG_VIEWPORT_SIZE + base);
    v9x_number(section, "ViewportWidth",
               (value >> 16) & V9X_EG_VIEWPORT_MASK);
    v9x_number(section, "ViewportHeight", value & V9X_EG_VIEWPORT_MASK);

    v9x_key(section, "SurfaceHigh",
            v9x_value(V9X_EG_GRPH_PRIMARY_HIGH + base));
    v9x_key(section, "SurfaceLow", v9x_value(V9X_EG_GRPH_PRIMARY + base));
    fb_low = (v9x_value(V9X_EG_MC_VM_FB_LOCATION) & 0xfful) <<
             V9X_EG_FB_UNIT_SHIFT;
    v9x_key(section, "LfbOffset",
            v9x_value(V9X_EG_GRPH_PRIMARY + base) - fb_low);

    v9x_yes_no(section, "UpdatePending",
               (v9x_value(V9X_EG_GRPH_UPDATE + base) &
                V9X_EG_UPDATE_PENDING) != 0ul);
    v9x_yes_no(section, "InVblank",
               (v9x_value(V9X_EG_CRTC_STATUS + base) &
                V9X_EG_IN_VBLANK) != 0ul);
}

void __stdcall V9xEvergreenMmioEntry(void)
{
    DWORD config;
    DWORD crtc;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_EGMM_PATH);
    v9x_text("Report", "Tool", "V9XEGMM");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Access", "mmio-read-only");

    config = v9x_find_cedar();
    if (config == 0ul) {
        v9x_text("Report", "Result", "NO-CEDAR");
        WritePrivateProfileStringA(0, 0, 0, V9X_EGMM_PATH);
        ExitProcess(1u);
    }
    v9x_key("Report", "Config", config);
    v9x_key("Report", "Id", v9x_config_read(config, 0x00ul));
    v9x_key("Report", "CommandStatus", v9x_config_read(config, 0x04ul));
    v9x_key("Report", "ClassRevision", v9x_config_read(config, 0x08ul));
    v9x_key("Report", "Bar0", v9x_config_read(config, 0x10ul));
    v9x_key("Report", "Bar0High", v9x_config_read(config, 0x14ul));
    v9x_key("Report", "Bar2", v9x_config_read(config, 0x18ul));
    v9x_key("Report", "Bar2High", v9x_config_read(config, 0x1cul));
    v9x_key("Report", "Bar4", v9x_config_read(config, 0x20ul));
    v9x_key("Report", "Subsystem", v9x_config_read(config, 0x2cul));
    v9x_key("Report", "RomBase", v9x_config_read(config, 0x30ul));

    v9x_request.config = config;
    v9x_request_build();
    if (!v9x_capture()) {
        v9x_key("Report", "Status", v9x_result.status);
        v9x_key("Report", "MemSize", v9x_result.memsize);
        v9x_key("Report", "RefusedOffset", v9x_result.refused_offset);
        WritePrivateProfileStringA(0, 0, 0, V9X_EGMM_PATH);
        ExitProcess(2u);
    }
    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "MemSize", v9x_result.memsize);
    v9x_number("Report", "Accepted", v9x_result.accepted);
    v9x_publish_mmio();
    v9x_decode_global();
    for (crtc = 0ul; crtc < V9X_EG_CRTC_COUNT; ++crtc) {
        v9x_decode_crtc(crtc);
    }
    v9x_text("Report", "Result", "COMPLETE");
    WritePrivateProfileStringA(0, 0, 0, V9X_EGMM_PATH);
    ExitProcess(0u);
}
