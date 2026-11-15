/*
 * V9XR2MM.EXE: ATI Radeon 9250 / 9200SE (RV280, 1002:5960 / 5964)
 * read-only register snapshot.
 *
 * Phase 2 of docs\plans\ati-rv280-radeon-9250.md, after the RS690's
 * V9XRSMM. Finds the card by a PCI scan (configuration mechanism #1, CF8h
 * restored after each read), loads R2MM.VXD, and asks it for register
 * offsets through the 64 KiB register BAR, each read twice. Writes
 * C:\V9XDIAG\V9XR2MM.INI:
 *
 *   [Report]  build, the device found, its BARs, the VxD's status;
 *   [Mmio]    every offset read, as R<offset>=first[,second if it moved],
 *             with Name.<offset> from xf86-video-ati's radeon_reg.h;
 *   [Decode]  memory size, where the frame buffer sits in the GPU's
 *             address space (MC_FB_LOCATION), the 2D engine's state, the
 *             CP's mode, and the CRTC's scanout: depth, pitch, offset.
 *
 * What the plan needs from it: whether the 2D engine is idle as the BIOS
 * left it, the GPU address of the VBE scanout, and the card's real memory.
 * Nothing on the device is written.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_R2MM_PATH        V9X_DIAG_PATH("V9XR2MM.INI")
#define V9X_R2MM_VXD         "\\\\.\\R2MM.VXD"
#define V9X_R2MM_MAGIC       0x4d4d3252ul
#define V9X_R2MM_CAPTURE     1u
#define V9X_R2MM_OFFSET_MAX  256u
#define V9X_R2MM_HEADER      16u
#define V9X_R2MM_MMIO_BYTES  0x00010000ul
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_PCI_ENABLE       0x80000000ul
#define V9X_PCI_BUSES        16ul
#define V9X_PCI_DEVICES      32ul
#define V9X_VENDOR_ATI       0x1002ul
#define V9X_DEVICE_9250      0x5960ul
#define V9X_DEVICE_9200SE    0x5964ul

#define V9X_ST_SNAPSHOT_TAKEN  0x00000020ul

/* Register offsets the decode reads back (radeon_reg.h). */
#define V9X_R2_CRTC_GEN_CNTL      0x0050ul
#define V9X_R2_CRTC_STATUS        0x005cul
#define V9X_R2_CONFIG_MEMSIZE     0x00f8ul
#define V9X_R2_CONFIG_APER_0_BASE 0x0100ul
#define V9X_R2_CONFIG_APER_SIZE   0x0108ul
#define V9X_R2_MC_FB_LOCATION     0x0148ul
#define V9X_R2_MC_AGP_LOCATION    0x014cul
#define V9X_R2_CRTC_H_TOTAL_DISP  0x0200ul
#define V9X_R2_CRTC_V_TOTAL_DISP  0x0208ul
#define V9X_R2_CRTC_OFFSET        0x0224ul
#define V9X_R2_CRTC_OFFSET_CNTL   0x0228ul
#define V9X_R2_CRTC_PITCH         0x022cul
#define V9X_R2_DISPLAY_BASE_ADDR  0x023cul
#define V9X_R2_CP_CSQ_CNTL        0x0740ul
#define V9X_R2_RBBM_STATUS        0x0e40ul
#define V9X_R2_PCI_COPY           0x0f00ul
#define V9X_R2_DST_PITCH_OFFSET   0x142cul
#define V9X_R2_SRC_PITCH_OFFSET   0x1428ul
#define V9X_R2_DEFAULT_OFFSET     0x16e0ul
#define V9X_R2_DEFAULT_PITCH      0x16e4ul
#define V9X_R2_RB3D_DSTCACHE      0x325cul

/*
 * Field layouts, same source. MC_FB_LOCATION 15:0 the frame buffer's
 * start >> 16, 31:16 its last byte >> 16 (radeon_driver.c:1528). RBBM_STATUS
 * 6:0 free FIFO entries, bit 31 active. CP_CSQ_CNTL 31:28 the CP's mode.
 * A pitch-offset register: offset >> 10 in 21:0, pitch / 64 in 29:22.
 * CRTC_GEN_CNTL 11:8 the pixel width code (2 = 8 bpp, 3 = 15, 4 = 16,
 * 6 = 32), bit 25 CRTC_EN, bit 26 display disable. CRTC_PITCH 10:0 the
 * pitch in units of 8 pixels. CRTC_STATUS bit 0 in vertical blank.
 * CRTC_H_TOTAL_DISP 24:16 (h display / 8) - 1; CRTC_V_TOTAL_DISP 26:16
 * v display - 1. CRTC_OFFSET 26:0 the scanout's byte offset.
 */
#define V9X_R2_FB_MASK            0x0000fffful
#define V9X_R2_FB_SHIFT           16u
#define V9X_R2_FIFO_MASK          0x0000007ful
#define V9X_R2_ACTIVE             0x80000000ul
#define V9X_R2_CSQ_SHIFT          28u
#define V9X_R2_PO_OFFSET_MASK     0x003ffffful
#define V9X_R2_PO_OFFSET_SHIFT    10u
#define V9X_R2_PO_PITCH_SHIFT     22u
#define V9X_R2_PO_PITCH_MASK      0xfful
#define V9X_R2_PO_PITCH_UNIT      64ul
#define V9X_R2_PIX_WIDTH_SHIFT    8u
#define V9X_R2_PIX_WIDTH_MASK     0xful
#define V9X_R2_CRTC_EN            0x02000000ul
#define V9X_R2_PITCH_MASK         0x7fful
#define V9X_R2_PITCH_UNIT         8ul
#define V9X_R2_IN_VBLANK          0x00000001ul
#define V9X_R2_H_DISP_SHIFT       16u
#define V9X_R2_H_DISP_MASK        0x1fful
#define V9X_R2_V_DISP_SHIFT       16u
#define V9X_R2_V_DISP_MASK        0x7fful
#define V9X_R2_OFFSET_MASK        0x07fffffful

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_r2mm_register {
    DWORD offset;
    const char *name;
};

static const struct v9x_r2mm_register v9x_registers[] = {
    { 0x0010ul, "BIOS_0_SCRATCH" },
    { 0x0014ul, "BIOS_1_SCRATCH" },
    { 0x0018ul, "BIOS_2_SCRATCH" },
    { 0x001cul, "BIOS_3_SCRATCH" },
    { 0x0020ul, "BIOS_4_SCRATCH" },
    { 0x0024ul, "BIOS_5_SCRATCH" },
    { 0x0028ul, "BIOS_6_SCRATCH" },
    { 0x002cul, "BIOS_7_SCRATCH" },
    { 0x0030ul, "BUS_CNTL" },
    { 0x0040ul, "GEN_INT_CNTL" },
    { 0x0044ul, "GEN_INT_STATUS" },
    { 0x0050ul, "CRTC_GEN_CNTL" },
    { 0x0054ul, "CRTC_EXT_CNTL" },
    { 0x0058ul, "DAC_CNTL" },
    { 0x005cul, "CRTC_STATUS" },
    { 0x00e0ul, "CONFIG_CNTL" },
    { 0x00e4ul, "CONFIG_XSTRAP" },
    { 0x00f0ul, "RBBM_SOFT_RESET" },
    { 0x00f8ul, "CONFIG_MEMSIZE" },
    { 0x0100ul, "CONFIG_APER_0_BASE" },
    { 0x0104ul, "CONFIG_APER_1_BASE" },
    { 0x0108ul, "CONFIG_APER_SIZE" },
    { 0x0130ul, "HOST_PATH_CNTL" },
    { 0x0140ul, "MEM_CNTL" },
    { 0x0148ul, "MC_FB_LOCATION" },
    { 0x014cul, "MC_AGP_LOCATION" },
    { 0x0200ul, "CRTC_H_TOTAL_DISP" },
    { 0x0204ul, "CRTC_H_SYNC_STRT_WID" },
    { 0x0208ul, "CRTC_V_TOTAL_DISP" },
    { 0x020cul, "CRTC_V_SYNC_STRT_WID" },
    { 0x0210ul, "CRTC_VLINE_CRNT_VLINE" },
    { 0x0214ul, "CRTC_CRNT_FRAME" },
    { 0x0218ul, "CRTC_GUI_TRIG_VLINE" },
    { 0x0224ul, "CRTC_OFFSET" },
    { 0x0228ul, "CRTC_OFFSET_CNTL" },
    { 0x022cul, "CRTC_PITCH" },
    { 0x023cul, "DISPLAY_BASE_ADDR" },
    { 0x0700ul, "CP_RB_BASE" },
    { 0x0704ul, "CP_RB_CNTL" },
    { 0x0710ul, "CP_RB_RPTR" },
    { 0x0714ul, "CP_RB_WPTR" },
    { 0x0740ul, "CP_CSQ_CNTL" },
    { 0x0b00ul, "SURFACE_CNTL" },
    { 0x0e40ul, "RBBM_STATUS" },
    { 0x0f00ul, "PCI_COPY_ID" },
    { 0x0f04ul, "PCI_COPY_COMMAND" },
    { 0x0f08ul, "PCI_COPY_CLASS" },
    { 0x1428ul, "SRC_PITCH_OFFSET" },
    { 0x142cul, "DST_PITCH_OFFSET" },
    { 0x146cul, "DP_GUI_MASTER_CNTL" },
    { 0x147cul, "DP_BRUSH_FRGD_CLR" },
    { 0x15c0ul, "SRC_SC_BOTTOM_RIGHT" },
    { 0x16c0ul, "DP_CNTL" },
    { 0x16c4ul, "DP_DATATYPE" },
    { 0x16ccul, "DP_WRITE_MASK" },
    { 0x16e0ul, "DEFAULT_OFFSET" },
    { 0x16e4ul, "DEFAULT_PITCH" },
    { 0x16e8ul, "DEFAULT_SC_BOTTOM_RIGHT" },
    { 0x1714ul, "DSTCACHE_CTLSTAT" },
    { 0x325cul, "RB3D_DSTCACHE_CTLSTAT" },
    { 0x3428ul, "RB2D_DSTCACHE_MODE" },
    { 0x342cul, "RB2D_DSTCACHE_CTLSTAT" }
};

#define V9X_REGISTER_COUNT (sizeof(v9x_registers) / sizeof(v9x_registers[0]))

typedef char v9x_r2mm_list_fits[
    V9X_REGISTER_COUNT <= V9X_R2MM_OFFSET_MAX ? 1 : -1];

/* CRTC_GEN_CNTL's pixel width code, as bits per pixel; 0 = not one this
 * driver uses. */
static const DWORD v9x_pix_widths[16] = {
    0ul, 0ul, 8ul, 15ul, 16ul, 24ul, 32ul, 0ul,
    0ul, 0ul, 0ul, 0ul, 0ul, 0ul, 0ul, 0ul
};

struct v9x_r2mm_request {
    DWORD config;
    DWORD count;
    DWORD offsets[V9X_R2MM_OFFSET_MAX];
};

struct v9x_r2mm_result {
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
    DWORD offsets[V9X_R2MM_OFFSET_MAX];
    DWORD first[V9X_R2MM_OFFSET_MAX];
    DWORD second[V9X_R2MM_OFFSET_MAX];
};

/* The header must be the VxD's RS_HEADER_DWORDS long. */
typedef char v9x_r2mm_header_matches[
    offsetof(struct v9x_r2mm_result, offsets) ==
    sizeof(DWORD) * V9X_R2MM_HEADER ? 1 : -1];

static struct v9x_r2mm_request v9x_request;
static struct v9x_r2mm_result v9x_result;
static DWORD v9x_values[V9X_R2MM_MMIO_BYTES / 4u];

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
    WritePrivateProfileStringA(section, key, text, V9X_R2MM_PATH);
}

static void v9x_number(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_decimal(text, value);
    WritePrivateProfileStringA(section, key, text, V9X_R2MM_PATH);
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_R2MM_PATH);
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

static void v9x_request_build(void)
{
    char key[24];
    DWORD index;

    v9x_request.count = 0ul;
    for (index = 0ul; index < V9X_REGISTER_COUNT; ++index) {
        v9x_request.offsets[v9x_request.count++] =
            v9x_registers[index].offset;
        v9x_offset_key(key, "Name.", v9x_registers[index].offset);
        v9x_text("Mmio", key, v9x_registers[index].name);
    }
}

/* One DIOC capture of v9x_request. Returns nonzero on a snapshot. */
static int v9x_capture(void)
{
    HANDLE device;
    DWORD returned;
    BOOL ok;

    device = CreateFileA(V9X_R2MM_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        return 0;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_R2MM_CAPTURE, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_R2MM_MAGIC) {
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

/* The first RV280 on buses 0-15, function 0; 0 if none. An AGP card sits
 * behind the north bridge's AGP bridge, so not on bus 0. */
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
                ((id >> 16) == V9X_DEVICE_9250 ||
                 (id >> 16) == V9X_DEVICE_9200SE)) {
                return config;
            }
        }
    }
    return 0ul;
}

static DWORD v9x_fb_start(void)
{
    return (v9x_value(V9X_R2_MC_FB_LOCATION) & V9X_R2_FB_MASK) <<
           V9X_R2_FB_SHIFT;
}

/* A pitch-offset register as its byte offset and byte pitch. */
static void v9x_decode_pitch_offset(const char *name, DWORD value)
{
    char key[40];

    lstrcpyA(key, name);
    lstrcatA(key, "Offset");
    v9x_key("Decode", key,
            (value & V9X_R2_PO_OFFSET_MASK) << V9X_R2_PO_OFFSET_SHIFT);
    lstrcpyA(key, name);
    lstrcatA(key, "PitchBytes");
    v9x_number("Decode", key,
               ((value >> V9X_R2_PO_PITCH_SHIFT) & V9X_R2_PO_PITCH_MASK) *
               V9X_R2_PO_PITCH_UNIT);
}

static void v9x_decode(void)
{
    DWORD value;
    DWORD width;
    DWORD bpp;

    value = v9x_value(V9X_R2_CONFIG_MEMSIZE);
    v9x_key("Decode", "MemoryBytes", value);
    v9x_number("Decode", "MemoryMiB", value >> 20);
    v9x_key("Decode", "Aperture0Base", v9x_value(V9X_R2_CONFIG_APER_0_BASE));
    v9x_key("Decode", "ApertureSize", v9x_value(V9X_R2_CONFIG_APER_SIZE));
    v9x_key("Decode", "FbStart", v9x_fb_start());
    v9x_key("Decode", "FbLast",
            (v9x_value(V9X_R2_MC_FB_LOCATION) & 0xffff0000ul) | 0xfffful);
    v9x_key("Decode", "AgpLocation", v9x_value(V9X_R2_MC_AGP_LOCATION));
    value = v9x_value(V9X_R2_RBBM_STATUS);
    v9x_yes_no("Decode", "GuiActive", (value & V9X_R2_ACTIVE) != 0ul);
    v9x_number("Decode", "CmdFifoFree", value & V9X_R2_FIFO_MASK);
    v9x_number("Decode", "CpCsqMode",
               v9x_value(V9X_R2_CP_CSQ_CNTL) >> V9X_R2_CSQ_SHIFT);
    v9x_decode_pitch_offset("Dst", v9x_value(V9X_R2_DST_PITCH_OFFSET));
    v9x_decode_pitch_offset("Src", v9x_value(V9X_R2_SRC_PITCH_OFFSET));
    v9x_key("Decode", "DefaultOffset", v9x_value(V9X_R2_DEFAULT_OFFSET));
    v9x_key("Decode", "DefaultPitch", v9x_value(V9X_R2_DEFAULT_PITCH));
    v9x_key("Decode", "Rb3dDstcache", v9x_value(V9X_R2_RB3D_DSTCACHE));
    v9x_key("Decode", "PciCopyId", v9x_value(V9X_R2_PCI_COPY));

    value = v9x_value(V9X_R2_CRTC_GEN_CNTL);
    v9x_yes_no("Crtc", "Enabled", (value & V9X_R2_CRTC_EN) != 0ul);
    bpp = v9x_pix_widths[(value >> V9X_R2_PIX_WIDTH_SHIFT) &
                         V9X_R2_PIX_WIDTH_MASK];
    v9x_number("Crtc", "BitsPerPixel", bpp);
    width = (((v9x_value(V9X_R2_CRTC_H_TOTAL_DISP) >> V9X_R2_H_DISP_SHIFT) &
              V9X_R2_H_DISP_MASK) + 1ul) * 8ul;
    v9x_number("Crtc", "Width", width);
    v9x_number("Crtc", "Height",
               ((v9x_value(V9X_R2_CRTC_V_TOTAL_DISP) >> V9X_R2_V_DISP_SHIFT) &
                V9X_R2_V_DISP_MASK) + 1ul);
    v9x_number("Crtc", "PitchPixels",
               (v9x_value(V9X_R2_CRTC_PITCH) & V9X_R2_PITCH_MASK) *
               V9X_R2_PITCH_UNIT);
    v9x_key("Crtc", "Offset",
            v9x_value(V9X_R2_CRTC_OFFSET) & V9X_R2_OFFSET_MASK);
    v9x_key("Crtc", "OffsetCntl", v9x_value(V9X_R2_CRTC_OFFSET_CNTL));
    v9x_key("Crtc", "DisplayBaseAddr", v9x_value(V9X_R2_DISPLAY_BASE_ADDR));
    v9x_yes_no("Crtc", "InVblank",
               (v9x_value(V9X_R2_CRTC_STATUS) & V9X_R2_IN_VBLANK) != 0ul);
}

void __stdcall V9xRv280MmioEntry(void)
{
    DWORD config;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_R2MM_PATH);
    v9x_text("Report", "Tool", "V9XR2MM");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Access", "mmio-read-only");

    config = v9x_find_device();
    if (config == 0ul) {
        v9x_text("Report", "Result", "NO-RV280");
        WritePrivateProfileStringA(0, 0, 0, V9X_R2MM_PATH);
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
    v9x_request_build();
    if (!v9x_capture()) {
        v9x_key("Report", "Status", v9x_result.status);
        v9x_key("Report", "MemSize", v9x_result.memsize);
        v9x_key("Report", "PciCopy", v9x_result.pci_copy);
        v9x_key("Report", "RefusedOffset", v9x_result.refused_offset);
        WritePrivateProfileStringA(0, 0, 0, V9X_R2MM_PATH);
        ExitProcess(2u);
    }
    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "MemSize", v9x_result.memsize);
    v9x_key("Report", "PciCopy", v9x_result.pci_copy);
    v9x_number("Report", "Accepted", v9x_result.accepted);
    v9x_publish_mmio();
    v9x_decode();
    v9x_text("Report", "Result", "COMPLETE");
    WritePrivateProfileStringA(0, 0, 0, V9X_R2MM_PATH);
    ExitProcess(0u);
}
