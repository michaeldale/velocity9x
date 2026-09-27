#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define ATIMM_MAGIC 0x30495441ul
#define ATIMM_REG_COUNT 29u
#define ATIMM_REQUIRED_STATUS 0x0000000ful

struct atimm_result {
    DWORD magic;
    DWORD status;
    DWORD pci_address;
    DWORD command_status;
    DWORD revision_class;
    DWORD bar0;
    DWORD bar1;
    DWORD bar2;
    DWORD subsystem;
    DWORD interrupt_info;
    DWORD mmio_base;
    DWORD offsets[ATIMM_REG_COUNT];
    DWORD first[ATIMM_REG_COUNT];
    DWORD second[ATIMM_REG_COUNT];
    DWORD lcd_index_first;
    DWORD lcd_horz_first;
    DWORD lcd_vert_first;
    DWORD lcd_index_second;
    DWORD lcd_horz_second;
    DWORD lcd_vert_second;
    DWORD lcd_index_after;
    DWORD reserved;
};

static const char *atimm_names[ATIMM_REG_COUNT] = {
    "CRTC_OFF_PITCH", "CRTC_GEN_CNTL", "MEM_BUF_CNTL", "BUS_CNTL",
    "MEM_CNTL", "GEN_TEST_CNTL", "CONFIG_CHIP_ID", "CONFIG_STAT0",
    "DST_OFF_PITCH", "DST_CNTL", "Z_OFF_PITCH", "Z_CNTL",
    "ALPHA_TST_CNTL", "SC_LEFT_RIGHT", "SC_TOP_BOTTOM", "DP_WRITE_MASK",
    "DP_CHAIN_MASK", "DP_PIX_WIDTH", "DP_MIX", "DP_SRC",
    "DP_SET_GUI_ENGINE", "CLR_CMP_CNTL", "FIFO_STAT", "GUI_TRAJ_CNTL",
    "GUI_STAT", "TEX_SIZE_PITCH", "TEX_CNTL", "GUI_CNTL", "SETUP_CNTL"
};

static void atimm_hex(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    text[0] = '0'; text[1] = 'x';
    for (shift = 28; shift >= 0; shift -= 4)
        text[2 + (28 - shift) / 4] = digits[(value >> shift) & 15u];
    text[10] = '\0';
}

static void atimm_decimal(char *text, DWORD value)
{
    char reverse[16];
    int count = 0;
    int index;
    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    for (index = 0; index < count; ++index)
        text[index] = reverse[count - index - 1];
    text[count] = '\0';
}

static void atimm_append_text(char *destination, const char *source,
                              DWORD capacity)
{
    DWORD used = 0u;
    DWORD index = 0u;
    while (used < capacity && destination[used] != '\0') ++used;
    while (used + index + 1u < capacity && source[index] != '\0') {
        destination[used + index] = source[index];
        ++index;
    }
    if (used + index < capacity) destination[used + index] = '\0';
}

static void atimm_write(HANDLE file, const char *key, const char *value)
{
    DWORD written;
    WriteFile(file, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(file, "=", 1u, &written, 0);
    WriteFile(file, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(file, "\r\n", 2u, &written, 0);
}

static void atimm_write_hex(HANDLE file, const char *key, DWORD value)
{
    char text[11];
    atimm_hex(text, value);
    atimm_write(file, key, text);
}

static void atimm_write_decimal(HANDLE file, const char *key, DWORD value)
{
    char text[16];
    atimm_decimal(text, value);
    atimm_write(file, key, text);
}

static void atimm_write_register(HANDLE file, const char *prefix, UINT index,
                                 DWORD offset, DWORD value)
{
    char key[64];
    char number[11];
    key[0] = '\0';
    atimm_append_text(key, prefix, sizeof(key));
    atimm_append_text(key, atimm_names[index], sizeof(key));
    atimm_hex(number, offset);
    atimm_append_text(key, "_", sizeof(key));
    atimm_append_text(key, number, sizeof(key));
    atimm_write_hex(file, key, value);
}

static DWORD atimm_decode_vram(DWORD mem_cntl)
{
    DWORD code = mem_cntl & 15u;
    if (code < 8u) return (code + 1u) * 512u * 1024u;
    if (code < 12u) return (code - 3u) * 1024u * 1024u;
    return (code - 7u) * 2u * 1024u * 1024u;
}

void WINAPI V9xAtiMmioFingerprintEntry(void)
{
    struct atimm_result result;
    HANDLE device;
    HANDLE output;
    DWORD returned = 0u;
    DWORD index;
    DWORD delta_mask = 0u;
    DWORD unexpected_delta_mask = 0u;
    DWORD vram_bytes;
    DWORD pitch_pixels;
    DWORD target_offset;
    DWORD horz_panel;
    DWORD vert_panel;
    DWORD desktop_width = 0u;
    DWORD desktop_height = 0u;
    DWORD desktop_bpp = 0u;
    DWORD raw_all_zero = 1u;
    DWORD raw_all_ones = 1u;
    int pass;
    HDC display;
    char header[] = "[AtiMobilityFingerprint]\r\n";
    char key[64];

    device = CreateFileA("\\\\.\\ATIMM.VXD", 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) ExitProcess(2u);
    if (!DeviceIoControl(device, 1u, 0, 0, &result, sizeof(result),
                         &returned, 0) || returned != sizeof(result) ||
        result.magic != ATIMM_MAGIC) {
        CloseHandle(device);
        ExitProcess(3u);
    }
    CloseHandle(device);

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    output = CreateFileA(V9X_DIAG_ATIMM_TXT, GENERIC_WRITE, FILE_SHARE_READ,
                         0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (output == INVALID_HANDLE_VALUE) ExitProcess(4u);
    WriteFile(output, header, (DWORD)lstrlenA(header), &returned, 0);

    display = GetDC(0);
    if (display != 0) {
        desktop_width = (DWORD)GetDeviceCaps(display, HORZRES);
        desktop_height = (DWORD)GetDeviceCaps(display, VERTRES);
        desktop_bpp = (DWORD)(GetDeviceCaps(display, BITSPIXEL) *
                              GetDeviceCaps(display, PLANES));
        ReleaseDC(0, display);
    }

    vram_bytes = atimm_decode_vram(result.first[4]);
    pitch_pixels = ((result.first[0] >> 22) & 0x3ffu) * 8u;
    target_offset = (result.first[0] & 0x000ffffful) * 8u;
    horz_panel = (result.lcd_horz_first & 0x0ff00000ul) >> 20;
    vert_panel = (result.lcd_vert_first & 0x003ff800ul) >> 11;

    atimm_write(output, "Build", V9X_BUILD_ID);
    atimm_write(output, "Access", "read-only-except-restored-lcd-selector");
    atimm_write(output, "Target", "PCI-1002-4C4D-revision-64");
    atimm_write_hex(output, "Status", result.status);
    atimm_write_hex(output, "PciConfigAddress", result.pci_address);
    atimm_write_decimal(output, "PciBus", (result.pci_address >> 16) & 0xffu);
    atimm_write_decimal(output, "PciDevice", (result.pci_address >> 11) & 0x1fu);
    atimm_write_decimal(output, "PciFunction", (result.pci_address >> 8) & 7u);
    atimm_write_hex(output, "PciCommandStatus", result.command_status);
    atimm_write_hex(output, "PciRevisionClass", result.revision_class);
    atimm_write_hex(output, "PciBar0Raw", result.bar0);
    atimm_write_hex(output, "PciBar1Raw", result.bar1);
    atimm_write_hex(output, "PciBar2Raw", result.bar2);
    atimm_write_hex(output, "PciSubsystem", result.subsystem);
    atimm_write_hex(output, "PciInterruptInfo", result.interrupt_info);
    atimm_write_hex(output, "MmioPhysicalBase", result.mmio_base);
    atimm_write_decimal(output, "DesktopWidth", desktop_width);
    atimm_write_decimal(output, "DesktopHeight", desktop_height);
    atimm_write_decimal(output, "DesktopBpp", desktop_bpp);

    for (index = 0u; index < ATIMM_REG_COUNT; ++index) {
        DWORD delta = result.first[index] ^ result.second[index];
        atimm_write_register(output, "A_", (UINT)index,
                             result.offsets[index], result.first[index]);
        atimm_write_register(output, "B_", (UINT)index,
                             result.offsets[index], result.second[index]);
        atimm_write_register(output, "D_", (UINT)index,
                             result.offsets[index], delta);
        if (result.first[index] != 0u) raw_all_zero = 0u;
        if (result.first[index] != 0xfffffffful) raw_all_ones = 0u;
        if (delta != 0u && index < 32u) {
            delta_mask |= (1ul << index);
            if (index != 22u && index != 24u)
                unexpected_delta_mask |= (1ul << index);
        }
    }

    atimm_write_hex(output, "LcdIndexA", result.lcd_index_first);
    atimm_write_hex(output, "LcdHorzStretchA", result.lcd_horz_first);
    atimm_write_hex(output, "LcdVertStretchA", result.lcd_vert_first);
    atimm_write_hex(output, "LcdIndexB", result.lcd_index_second);
    atimm_write_hex(output, "LcdHorzStretchB", result.lcd_horz_second);
    atimm_write_hex(output, "LcdVertStretchB", result.lcd_vert_second);
    atimm_write_hex(output, "LcdIndexAfter", result.lcd_index_after);
    atimm_write_decimal(output, "HorzPanelSizeField", horz_panel);
    atimm_write_decimal(output, "VertPanelSizeField", vert_panel);
    atimm_write_decimal(output, "PanelWidth", (horz_panel + 1u) * 8u);
    atimm_write_decimal(output, "PanelHeight", vert_panel + 1u);
    atimm_write_decimal(output, "MemoryType", result.first[7] & 7u);
    atimm_write_decimal(output, "VramBytes", vram_bytes);
    atimm_write_decimal(output, "ActivePitchPixels", pitch_pixels);
    atimm_write_decimal(output, "ActiveOffsetBytes", target_offset);
    atimm_write_decimal(output, "GuiFifoFreeA",
                        (result.first[24] >> 16) & 0x3ffu);
    atimm_write_decimal(output, "GuiFifoFreeB",
                        (result.second[24] >> 16) & 0x3ffu);
    atimm_write_hex(output, "RepeatDeltaMask", delta_mask);
    atimm_write_hex(output, "UnexpectedDeltaMask", unexpected_delta_mask);
    atimm_write(output, "ExpectedLiveRegisters", "FIFO_STAT,GUI_STAT");

    pass = result.status == ATIMM_REQUIRED_STATUS &&
           (result.command_status & 3u) == 3u &&
           (result.revision_class & 0xffu) == 0x64u &&
           (result.first[6] & 0xffffu) == 0x4c4du &&
           vram_bytes == 4u * 1024u * 1024u &&
           (result.first[7] & 7u) == 4u &&
           horz_panel == 127u && vert_panel == 767u &&
           result.lcd_horz_first == result.lcd_horz_second &&
           result.lcd_vert_first == result.lcd_vert_second &&
           result.lcd_index_first == result.lcd_index_after &&
           raw_all_zero == 0u && raw_all_ones == 0u &&
           unexpected_delta_mask == 0u && desktop_bpp == 16u &&
           pitch_pixels >= desktop_width && target_offset < vram_bytes;
    if (!pass) {
        key[0] = '\0';
        if (result.status != ATIMM_REQUIRED_STATUS)
            atimm_append_text(key, "capture-status;", sizeof(key));
        if ((result.command_status & 3u) != 3u)
            atimm_append_text(key, "pci-command;", sizeof(key));
        if ((result.revision_class & 0xffu) != 0x64u)
            atimm_append_text(key, "pci-revision;", sizeof(key));
        if ((result.first[6] & 0xffffu) != 0x4c4du)
            atimm_append_text(key, "chip-id;", sizeof(key));
        if (vram_bytes != 4u * 1024u * 1024u)
            atimm_append_text(key, "vram;", sizeof(key));
        if ((result.first[7] & 7u) != 4u)
            atimm_append_text(key, "memory-type;", sizeof(key));
        if (horz_panel != 127u || vert_panel != 767u)
            atimm_append_text(key, "panel-size;", sizeof(key));
        if (unexpected_delta_mask != 0u)
            atimm_append_text(key, "unstable-register;", sizeof(key));
        if (desktop_bpp != 16u || pitch_pixels < desktop_width ||
            target_offset >= vram_bytes)
            atimm_append_text(key, "active-target;", sizeof(key));
        if (raw_all_zero != 0u || raw_all_ones != 0u)
            atimm_append_text(key, "aliased-window;", sizeof(key));
        atimm_write(output, "ReviewReasons", key);
    }
    atimm_write(output, "Result", pass ? "PASS" : "REVIEW");
    CloseHandle(output);
    ExitProcess(pass ? 0u : 1u);
}
