#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

/* Minimal Win98 Config Manager ABI.  Open Watcom does not ship cfgmgr32.h;
 * including the DDK's full Windows headers beside Watcom's headers conflicts.
 * These scalar handle types and constants are the complete subset used here. */
typedef DWORD CONFIGRET;
typedef DWORD DEVINST;
typedef DEVINST *PDEVINST;
typedef char *DEVINSTID_A;
typedef DWORD LOG_CONF;
typedef LOG_CONF *PLOG_CONF;
typedef DWORD RES_DES;
typedef RES_DES *PRES_DES;
typedef ULONG RESOURCEID;
typedef RESOURCEID *PRESOURCEID;
#define MAX_DEVICE_ID_LEN 200
#define ResType_Mem 0x00000001ul
#define ALLOC_LOG_CONF 0x00000002ul
#define CM_LOCATE_DEVNODE_NORMAL 0x00000000ul
#define CR_SUCCESS 0x00000000ul
#define CR_NO_SUCH_DEVNODE 0x0000000dul
#define CR_NO_MORE_RES_DES 0x0000000ful
#define CR_FAILURE 0x00000013ul

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define ATIMM_MAGIC 0x30495441ul
#define ATIMM_REG_COUNT 29u
#define ATIMM_REQUIRED_STATUS 0x0000001ful

struct atimm_request {
    DWORD assigned_bar0;
    DWORD assigned_bar2;
};

typedef CONFIGRET (WINAPI *atimm_cm_locate_fn)(PDEVINST, DEVINSTID_A, ULONG);
typedef CONFIGRET (WINAPI *atimm_cm_first_fn)(PLOG_CONF, DEVINST, ULONG);
typedef CONFIGRET (WINAPI *atimm_cm_next_fn)(PRES_DES, RES_DES, RESOURCEID,
                                             PRESOURCEID, ULONG);
typedef CONFIGRET (WINAPI *atimm_cm_free_res_fn)(RES_DES);
typedef CONFIGRET (WINAPI *atimm_cm_size_fn)(PULONG, RES_DES, ULONG);
typedef CONFIGRET (WINAPI *atimm_cm_data_fn)(RES_DES, PVOID, ULONG, ULONG);
typedef CONFIGRET (WINAPI *atimm_cm_free_log_fn)(LOG_CONF);

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

static int atimm_starts_with_ci(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        char left = *text++;
        char right = *prefix++;
        if (left >= 'a' && left <= 'z') left = (char)(left - 32);
        if (right >= 'a' && right <= 'z') right = (char)(right - 32);
        if (left != right) return 0;
    }
    return 1;
}

static void atimm_copy(char *destination, const char *source, DWORD capacity)
{
    DWORD index = 0u;
    if (capacity == 0u) return;
    while (index + 1u < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

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

static DWORD atimm_read_u32(const BYTE *data)
{
    return (DWORD)data[0] | ((DWORD)data[1] << 8) |
           ((DWORD)data[2] << 16) | ((DWORD)data[3] << 24);
}

static LONG atimm_find_device(char *device_id, DWORD capacity)
{
    HKEY pci_key;
    HKEY adapter_key;
    DWORD adapter_index = 0u;
    char adapter[160];
    DWORD adapter_length;
    LONG status;

    status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Enum\\PCI", 0, KEY_READ,
                           &pci_key);
    if (status != ERROR_SUCCESS) return status;
    for (;;) {
        adapter_length = sizeof(adapter);
        status = RegEnumKeyExA(pci_key, adapter_index++, adapter,
                               &adapter_length, 0, 0, 0, 0);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS ||
            !atimm_starts_with_ci(adapter, "VEN_1002&DEV_4C4D")) continue;
        if (RegOpenKeyExA(pci_key, adapter, 0, KEY_READ, &adapter_key) ==
            ERROR_SUCCESS) {
            char instance[160];
            DWORD instance_length = sizeof(instance);
            status = RegEnumKeyExA(adapter_key, 0, instance, &instance_length,
                                   0, 0, 0, 0);
            RegCloseKey(adapter_key);
            if (status == ERROR_SUCCESS) {
                atimm_copy(device_id, "PCI\\", capacity);
                atimm_append_text(device_id, adapter, capacity);
                atimm_append_text(device_id, "\\", capacity);
                atimm_append_text(device_id, instance, capacity);
                RegCloseKey(pci_key);
                return ERROR_SUCCESS;
            }
        }
    }
    RegCloseKey(pci_key);
    return ERROR_FILE_NOT_FOUND;
}

static CONFIGRET atimm_assigned_bars(struct atimm_request *request)
{
    char device_id[MAX_DEVICE_ID_LEN];
    DEVINST device;
    LOG_CONF logical_config;
    RES_DES current;
    RES_DES next;
    CONFIGRET status;
    HMODULE module;
    atimm_cm_locate_fn cm_locate;
    atimm_cm_first_fn cm_first;
    atimm_cm_next_fn cm_next;
    atimm_cm_free_res_fn cm_free_res;
    atimm_cm_size_fn cm_size;
    atimm_cm_data_fn cm_data;
    atimm_cm_free_log_fn cm_free_log;

    request->assigned_bar0 = 0u;
    request->assigned_bar2 = 0u;
    module = LoadLibraryA("CFGMGR32.DLL");
    if (module == 0) return CR_FAILURE;
    cm_locate = (atimm_cm_locate_fn)GetProcAddress(module, "CM_Locate_DevNodeA");
    cm_first = (atimm_cm_first_fn)GetProcAddress(module, "CM_Get_First_Log_Conf");
    cm_next = (atimm_cm_next_fn)GetProcAddress(module, "CM_Get_Next_Res_Des");
    cm_free_res = (atimm_cm_free_res_fn)GetProcAddress(module, "CM_Free_Res_Des_Handle");
    cm_size = (atimm_cm_size_fn)GetProcAddress(module, "CM_Get_Res_Des_Data_Size");
    cm_data = (atimm_cm_data_fn)GetProcAddress(module, "CM_Get_Res_Des_Data");
    cm_free_log = (atimm_cm_free_log_fn)GetProcAddress(module, "CM_Free_Log_Conf_Handle");
    if (cm_locate == 0 || cm_first == 0 || cm_next == 0 ||
        cm_free_res == 0 || cm_size == 0 || cm_data == 0 || cm_free_log == 0) {
        FreeLibrary(module);
        return CR_FAILURE;
    }
    if (atimm_find_device(device_id, sizeof(device_id)) != ERROR_SUCCESS)
        status = CR_NO_SUCH_DEVNODE;
    else
        status = cm_locate(&device, device_id, CM_LOCATE_DEVNODE_NORMAL);
    if (status != CR_SUCCESS) {
        FreeLibrary(module);
        return status;
    }
    status = cm_first(&logical_config, device, ALLOC_LOG_CONF);
    if (status != CR_SUCCESS) {
        FreeLibrary(module);
        return status;
    }
    current = (RES_DES)logical_config;
    for (;;) {
        BYTE data[256];
        ULONG size = 0u;
        DWORD base_low;
        DWORD base_high;
        DWORD end_low;
        DWORD end_high;
        DWORD bytes;

        status = cm_next(&next, current, ResType_Mem, 0, 0);
        if (current != (RES_DES)logical_config) cm_free_res(current);
        if (status == CR_NO_MORE_RES_DES) {
            status = CR_SUCCESS;
            break;
        }
        if (status != CR_SUCCESS) break;
        current = next;
        if (cm_size(&size, current, 0) != CR_SUCCESS ||
            size < 32u || size > sizeof(data) ||
            cm_data(current, data, size, 0) != CR_SUCCESS) {
            status = CR_FAILURE;
            break;
        }
        base_low = atimm_read_u32(data + 8);
        base_high = atimm_read_u32(data + 12);
        end_low = atimm_read_u32(data + 16);
        end_high = atimm_read_u32(data + 20);
        bytes = (base_high == 0u && end_high == 0u && end_low >= base_low)
            ? end_low - base_low + 1u : 0u;
        if (bytes == 0x01000000ul) request->assigned_bar0 = base_low;
        if (bytes == 0x00001000ul || bytes == 0x00004000ul)
            request->assigned_bar2 = base_low;
    }
    if (current != (RES_DES)logical_config) cm_free_res(current);
    cm_free_log(logical_config);
    if (status == CR_SUCCESS &&
        (request->assigned_bar0 == 0u || request->assigned_bar2 == 0u))
        status = CR_FAILURE;
    FreeLibrary(module);
    return status;
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
    struct atimm_request request;
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

    if (atimm_assigned_bars(&request) != CR_SUCCESS) ExitProcess(5u);

    device = CreateFileA("\\\\.\\ATIMM.VXD", 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) ExitProcess(2u);
    if (!DeviceIoControl(device, 1u, &request, sizeof(request),
                         &result, sizeof(result),
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
    atimm_write_hex(output, "AssignedBar0", request.assigned_bar0);
    atimm_write_hex(output, "AssignedBar2", request.assigned_bar2);
    atimm_write(output, "MmioProvenance",
                (result.status & 0x04u) == 0u
                    ? "unvalidated-candidate"
                    : (result.status & 0x20u) != 0u
                    ? "ConfigManager-BAR0-in-aperture"
                    : "ConfigManager-allocated-BAR2");
    atimm_write_hex(output, "PciSubsystem", result.subsystem);
    atimm_write_hex(output, "PciInterruptInfo", result.interrupt_info);
    atimm_write_hex(output, "MmioCandidateBase", result.mmio_base);
    atimm_write_hex(output, "MmioPhysicalBase",
                    (result.status & 0x04u) != 0u ? result.mmio_base : 0u);
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

    pass = (result.status & ATIMM_REQUIRED_STATUS) == ATIMM_REQUIRED_STATUS &&
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
