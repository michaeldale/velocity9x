/*
 * ATIIC.EXE: the ATI Rage IIC (PCI 1002:4757) read-only register probe.
 *
 * Publishes C:\V9XDIAG\ATIIC.TXT and C:\V9XDIAG\ATIIC.ROM. Runs beside
 * ATIIC.VXD, which does the reads; this side decides which offsets are read
 * and decodes nothing it then relies on.
 *
 * It exists to measure, on the stock ATI driver, what the Velocity9x ati
 * family assumes about the Rage Mobility-M and has never checked on a Rage
 * II-class part: which PCI ranges the card claims, which window decodes the
 * registers, what CONFIG_CHIP_ID says, how GUI_STAT and FIFO_STAT report the
 * command FIFO, and what the block 1 offsets the Rage Pro setup engine lives
 * at read back as after ATI's own Direct3D HAL has run. It writes nothing to
 * the card, so it cannot answer whether a register is writable; that needs a
 * write probe and a decision.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

/* The Config Manager subset ati_mmio_fingerprint_win32.c established for
 * Open Watcom against Win98's CFGMGR32.DLL. */
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
#define ResType_IO 0x00000002ul
#define ALLOC_LOG_CONF 0x00000002ul
#define CM_LOCATE_DEVNODE_NORMAL 0x00000000ul
#define CR_SUCCESS 0x00000000ul
#define CR_NO_SUCH_DEVNODE 0x0000000dul
#define CR_NO_MORE_RES_DES 0x0000000ful
#define CR_FAILURE 0x00000013ul

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define ATIIC_MAGIC 0x43494941ul
#define ATIIC_HEADER_DWORDS 12u
#define ATIIC_CONFIG_DWORDS 64u
#define ATIIC_OFFSET_MAX 512u
#define ATIIC_ROM_BYTES 0x10000u
#define ATIIC_RANGE_MAX 8u

#define ATIIC_PCI_FOUND       0x00000001ul
#define ATIIC_BAR2_MAPPED     0x00000002ul
#define ATIIC_BAR2_MATCH      0x00000004ul
#define ATIIC_APERTURE_MAPPED 0x00000008ul
#define ATIIC_APERTURE_MATCH  0x00000010ul
#define ATIIC_SNAPSHOT_TAKEN  0x00000020ul
#define ATIIC_ROM_COPIED      0x00000040ul
#define ATIIC_OFFSETS_REFUSED 0x00000080ul

/* Window offsets: block 1 at +000h, block 0 at +400h. */
#define ATIIC_WINDOW_BYTES  0x800u
#define ATIIC_BLOCK0        0x400u
#define ATIIC_CRTC_OFF_PITCH 0x414u
#define ATIIC_BUS_CNTL      0x4a0u
#define ATIIC_MEM_CNTL      0x4b0u
#define ATIIC_CONFIG_CHIP_ID 0x4e0u
#define ATIIC_CONFIG_STAT0  0x4e4u
#define ATIIC_FIFO_STAT     0x710u
#define ATIIC_GUI_STAT      0x738u
#define ATIIC_BUS_EXT_REG_EN 0x08000000ul

struct atiic_request {
    DWORD assigned_bar0;
    DWORD assigned_bar2;
    DWORD count;
    DWORD offsets[ATIIC_OFFSET_MAX];
};

struct atiic_result {
    DWORD magic;
    DWORD status;
    DWORD pci_address;
    DWORD bar2_window;
    DWORD bar2_chip_id;
    DWORD aperture_window;
    DWORD aperture_chip_id;
    DWORD snapshot_window;
    DWORD accepted;
    DWORD config_address_restored;
    DWORD reserved[2];
    DWORD config[ATIIC_CONFIG_DWORDS];
    DWORD offsets[ATIIC_OFFSET_MAX];
    DWORD first[ATIIC_OFFSET_MAX];
    DWORD second[ATIIC_OFFSET_MAX];
    BYTE rom[ATIIC_ROM_BYTES];
};

struct atiic_range {
    DWORD type;
    DWORD base;
    DWORD bytes;
};

/*
 * Offsets not read, as [first, last] window offsets inclusive. Each entry is
 * a register whose read is known or suspected to change state.
 *
 * DAC_REGS (+4C0h): its byte 1 is the palette data port, and a read
 * advances the RAMDAC's read index.
 * HOST_DATA0-15 (+600h-+63Ch, 0_80-0_8F): the host-data FIFO ports. Until
 * 2026-10-02 this said +640h-+67Ch, which is HOST_CNTL and the pattern
 * registers, so the first runs skipped those and read the host-data ports;
 * the reads did no harm that the agent or the desktop showed.
 * Block 1 +180h-+1FCh: the VTB/GTB bus-master registers (BM_* from 1_60,
 * BM_GUI_TABLE 1_6E, BM_SYSTEM_TABLE 1_6F per xf86-video-mach64
 * atiregs.h) and their neighbours. No source says these reads are inert.
 */
static const DWORD atiic_skip[][2] = {
    { 0x180u, 0x1fcu },
    { 0x4c0u, 0x4c0u },
    { 0x600u, 0x63cu }
};

static struct atiic_request atiic_request_buffer;
static struct atiic_result atiic_result_buffer;

static void atiic_copy(char *destination, const char *source, DWORD capacity)
{
    DWORD index = 0u;

    if (capacity == 0u) {
        return;
    }
    while (index + 1u < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static void atiic_append(char *destination, const char *source, DWORD capacity)
{
    DWORD used = 0u;
    DWORD index = 0u;

    while (used < capacity && destination[used] != '\0') {
        ++used;
    }
    while (used + index + 1u < capacity && source[index] != '\0') {
        destination[used + index] = source[index];
        ++index;
    }
    if (used + index < capacity) {
        destination[used + index] = '\0';
    }
}

static int atiic_starts_with_ci(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        char left = *text++;
        char right = *prefix++;

        if (left >= 'a' && left <= 'z') {
            left = (char)(left - 32);
        }
        if (right >= 'a' && right <= 'z') {
            right = (char)(right - 32);
        }
        if (left != right) {
            return 0;
        }
    }
    return 1;
}

/* Case-insensitive search for a switch anywhere on the command line. */
static int atiic_has_switch(const char *command_line, const char *name)
{
    if (command_line == 0) {
        return 0;
    }
    while (*command_line != '\0') {
        if (atiic_starts_with_ci(command_line, name)) {
            return 1;
        }
        ++command_line;
    }
    return 0;
}

static void atiic_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15u];
    }
    text[digits] = '\0';
}

static void atiic_decimal(char *text, DWORD value)
{
    char reverse[16];
    int count = 0;
    int index;

    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    for (index = 0; index < count; ++index) {
        text[index] = reverse[count - index - 1];
    }
    text[count] = '\0';
}

static DWORD atiic_read_u32(const BYTE *data)
{
    return (DWORD)data[0] | ((DWORD)data[1] << 8) |
           ((DWORD)data[2] << 16) | ((DWORD)data[3] << 24);
}

static void atiic_write(HANDLE file, const char *key, const char *value)
{
    DWORD written;

    WriteFile(file, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(file, "=", 1u, &written, 0);
    WriteFile(file, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(file, "\r\n", 2u, &written, 0);
}

static void atiic_write_hex(HANDLE file, const char *key, DWORD value)
{
    char text[12];

    text[0] = '0';
    text[1] = 'x';
    atiic_hex(text + 2, value, 8);
    atiic_write(file, key, text);
}

static void atiic_write_decimal(HANDLE file, const char *key, DWORD value)
{
    char text[16];

    atiic_decimal(text, value);
    atiic_write(file, key, text);
}

static LONG atiic_find_device(char *device_id, DWORD capacity)
{
    HKEY pci_key;
    HKEY adapter_key;
    DWORD adapter_index = 0u;
    char adapter[160];
    char instance[160];
    DWORD length;
    LONG status;

    status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Enum\\PCI", 0, KEY_READ,
                           &pci_key);
    if (status != ERROR_SUCCESS) {
        return status;
    }
    for (;;) {
        length = sizeof(adapter);
        status = RegEnumKeyExA(pci_key, adapter_index++, adapter, &length,
                               0, 0, 0, 0);
        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (status != ERROR_SUCCESS ||
            !atiic_starts_with_ci(adapter, "VEN_1002&DEV_4757")) {
            continue;
        }
        if (RegOpenKeyExA(pci_key, adapter, 0, KEY_READ, &adapter_key) !=
            ERROR_SUCCESS) {
            continue;
        }
        length = sizeof(instance);
        status = RegEnumKeyExA(adapter_key, 0, instance, &length, 0, 0, 0, 0);
        RegCloseKey(adapter_key);
        if (status == ERROR_SUCCESS) {
            atiic_copy(device_id, "PCI\\", capacity);
            atiic_append(device_id, adapter, capacity);
            atiic_append(device_id, "\\", capacity);
            atiic_append(device_id, instance, capacity);
            RegCloseKey(pci_key);
            return ERROR_SUCCESS;
        }
    }
    RegCloseKey(pci_key);
    return ERROR_FILE_NOT_FOUND;
}

/*
 * Every memory and I/O range Config Manager allocated to the card. The live
 * BAR dwords are recorded too, but the Mobility showed them reading zero
 * under ATI's driver while the allocation stood, so the allocation is what
 * the windows are mapped from.
 */
static CONFIGRET atiic_ranges(struct atiic_range *ranges, DWORD *count)
{
    typedef CONFIGRET (WINAPI *locate_fn)(PDEVINST, DEVINSTID_A, ULONG);
    typedef CONFIGRET (WINAPI *first_fn)(PLOG_CONF, DEVINST, ULONG);
    typedef CONFIGRET (WINAPI *next_fn)(PRES_DES, RES_DES, RESOURCEID,
                                        PRESOURCEID, ULONG);
    typedef CONFIGRET (WINAPI *free_res_fn)(RES_DES);
    typedef CONFIGRET (WINAPI *size_fn)(PULONG, RES_DES, ULONG);
    typedef CONFIGRET (WINAPI *data_fn)(RES_DES, PVOID, ULONG, ULONG);
    typedef CONFIGRET (WINAPI *free_log_fn)(LOG_CONF);
    static const DWORD types[2] = { ResType_Mem, ResType_IO };
    char device_id[MAX_DEVICE_ID_LEN];
    DEVINST device;
    LOG_CONF logical_config;
    RES_DES current;
    RES_DES next;
    CONFIGRET status;
    HMODULE module;
    locate_fn cm_locate;
    first_fn cm_first;
    next_fn cm_next;
    free_res_fn cm_free_res;
    size_fn cm_size;
    data_fn cm_data;
    free_log_fn cm_free_log;
    DWORD type_index;

    *count = 0u;
    module = LoadLibraryA("CFGMGR32.DLL");
    if (module == 0) {
        return CR_FAILURE;
    }
    cm_locate = (locate_fn)GetProcAddress(module, "CM_Locate_DevNodeA");
    cm_first = (first_fn)GetProcAddress(module, "CM_Get_First_Log_Conf");
    cm_next = (next_fn)GetProcAddress(module, "CM_Get_Next_Res_Des");
    cm_free_res = (free_res_fn)GetProcAddress(module, "CM_Free_Res_Des_Handle");
    cm_size = (size_fn)GetProcAddress(module, "CM_Get_Res_Des_Data_Size");
    cm_data = (data_fn)GetProcAddress(module, "CM_Get_Res_Des_Data");
    cm_free_log = (free_log_fn)GetProcAddress(module, "CM_Free_Log_Conf_Handle");
    if (cm_locate == 0 || cm_first == 0 || cm_next == 0 || cm_free_res == 0 ||
        cm_size == 0 || cm_data == 0 || cm_free_log == 0) {
        FreeLibrary(module);
        return CR_FAILURE;
    }
    if (atiic_find_device(device_id, sizeof(device_id)) != ERROR_SUCCESS) {
        FreeLibrary(module);
        return CR_NO_SUCH_DEVNODE;
    }
    status = cm_locate(&device, device_id, CM_LOCATE_DEVNODE_NORMAL);
    if (status != CR_SUCCESS) {
        FreeLibrary(module);
        return status;
    }

    for (type_index = 0u; type_index < 2u; ++type_index) {
        status = cm_first(&logical_config, device, ALLOC_LOG_CONF);
        if (status != CR_SUCCESS) {
            break;
        }
        current = (RES_DES)logical_config;
        for (;;) {
            BYTE data[256];
            ULONG size = 0u;
            DWORD base_low;
            DWORD end_low;

            status = cm_next(&next, current, types[type_index], 0, 0);
            if (current != (RES_DES)logical_config) {
                cm_free_res(current);
            }
            current = (RES_DES)logical_config;
            if (status == CR_NO_MORE_RES_DES) {
                status = CR_SUCCESS;
                break;
            }
            if (status != CR_SUCCESS) {
                break;
            }
            current = next;
            /* MEM_DES and IO_DES agree on the layout read here: the
             * allocated base and end as DWORDLONGs at +8 and +16. */
            if (cm_size(&size, current, 0) != CR_SUCCESS || size < 24u ||
                size > sizeof(data) ||
                cm_data(current, data, size, 0) != CR_SUCCESS) {
                status = CR_FAILURE;
                break;
            }
            base_low = atiic_read_u32(data + 8);
            end_low = atiic_read_u32(data + 16);
            if (*count < ATIIC_RANGE_MAX && end_low >= base_low &&
                atiic_read_u32(data + 12) == 0u &&
                atiic_read_u32(data + 20) == 0u) {
                ranges[*count].type = types[type_index];
                ranges[*count].base = base_low;
                ranges[*count].bytes = end_low - base_low + 1u;
                ++*count;
            }
        }
        if (current != (RES_DES)logical_config) {
            cm_free_res(current);
        }
        cm_free_log(logical_config);
        if (status != CR_SUCCESS) {
            break;
        }
    }
    FreeLibrary(module);
    return status;
}

static int atiic_skipped(DWORD offset)
{
    DWORD index;

    for (index = 0u; index < sizeof(atiic_skip) / sizeof(atiic_skip[0]);
         ++index) {
        if (offset >= atiic_skip[index][0] && offset <= atiic_skip[index][1]) {
            return 1;
        }
    }
    return 0;
}

/* Index of a window offset in the snapshot, or ATIIC_OFFSET_MAX. */
static DWORD atiic_find(const struct atiic_result *result, DWORD offset)
{
    DWORD index;

    for (index = 0u; index < result->accepted; ++index) {
        if (result->offsets[index] == offset) {
            return index;
        }
    }
    return ATIIC_OFFSET_MAX;
}

static DWORD atiic_value(const struct atiic_result *result, DWORD offset)
{
    DWORD index = atiic_find(result, offset);

    return index == ATIIC_OFFSET_MAX ? 0u : result->first[index];
}

static DWORD atiic_popcount(DWORD value)
{
    DWORD bits = 0u;

    while (value != 0u) {
        bits += value & 1u;
        value >>= 1;
    }
    return bits;
}

/* The two MEM_CNTL size tables the hardware audit contrasts: three-bit for
 * pre-VTB parts, four-bit CTL_MEM_SIZEB for VTB/GTB/LT. Which one a Rage IIC
 * uses is not assumed; both are printed. */
static DWORD atiic_vram_pre_vtb(DWORD mem_cntl)
{
    static const DWORD sizes[8] = {
        512ul * 1024ul, 1024ul * 1024ul, 2048ul * 1024ul, 4096ul * 1024ul,
        6144ul * 1024ul, 8192ul * 1024ul, 12288ul * 1024ul, 16384ul * 1024ul
    };

    return sizes[mem_cntl & 7u];
}

static DWORD atiic_vram_vtb(DWORD mem_cntl)
{
    DWORD code = mem_cntl & 15u;

    if (code < 8u) {
        return (code + 1u) * 512u * 1024u;
    }
    if (code < 12u) {
        return (code - 3u) * 1024u * 1024u;
    }
    return (code - 7u) * 2u * 1024u * 1024u;
}

static void atiic_write_rom(const struct atiic_result *result)
{
    HANDLE file;
    DWORD written;

    file = CreateFileA(V9X_DIAG_ATIIC_ROM, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    WriteFile(file, result->rom, ATIIC_ROM_BYTES, &written, 0);
    CloseHandle(file);
}

void WINAPI V9xAtiRageIicProbeEntry(void)
{
    struct atiic_request *request = &atiic_request_buffer;
    struct atiic_result *result = &atiic_result_buffer;
    struct atiic_range ranges[ATIIC_RANGE_MAX];
    DWORD range_count = 0u;
    CONFIGRET ranges_status;
    HANDLE device;
    HANDLE output;
    HDC display;
    DWORD returned = 0u;
    DWORD index;
    DWORD offset;
    DWORD first_offset;
    DWORD delta_count = 0u;
    DWORD gui_stat;
    DWORD fifo_stat;
    DWORD crtc;
    DWORD mem_cntl;
    char key[64];
    char text[16];
    static const char header[] = "[AtiRageIicProbe]\r\n";

    request->assigned_bar0 = 0u;
    request->assigned_bar2 = 0u;
    ranges_status = atiic_ranges(ranges, &range_count);
    for (index = 0u; index < range_count; ++index) {
        if (ranges[index].type != ResType_Mem) {
            continue;
        }
        if (ranges[index].bytes >= 0x00800000ul) {
            request->assigned_bar0 = ranges[index].base;
        }
        if (ranges[index].bytes == 0x00001000ul) {
            request->assigned_bar2 = ranges[index].base;
        }
    }

    /*
     * Block 0 only unless /block1 is given. Block 0 is the Mach64 register
     * file every member of the family decodes; what a Rage II-class part
     * puts in block 1, and whether reading it is harmless, is the open
     * question, so it is read only when asked for.
     */
    request->count = 0u;
    first_offset = atiic_has_switch(GetCommandLineA(), "/block1")
        ? 0u : ATIIC_BLOCK0;
    for (offset = first_offset; offset < ATIIC_WINDOW_BYTES; offset += 4u) {
        if (!atiic_skipped(offset)) {
            request->offsets[request->count++] = offset;
        }
    }

    device = CreateFileA("\\\\.\\ATIIC.VXD", 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        ExitProcess(2u);
    }
    if (!DeviceIoControl(device, 1u, request, sizeof(*request), result,
                         sizeof(*result), &returned, 0) ||
        returned != sizeof(*result) || result->magic != ATIIC_MAGIC) {
        CloseHandle(device);
        ExitProcess(3u);
    }
    CloseHandle(device);

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    if ((result->status & ATIIC_ROM_COPIED) != 0ul) {
        atiic_write_rom(result);
    }
    output = CreateFileA(V9X_DIAG_ATIIC_TXT, GENERIC_WRITE, FILE_SHARE_READ,
                         0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (output == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    WriteFile(output, header, (DWORD)lstrlenA(header), &returned, 0);
    atiic_write(output, "Build", V9X_BUILD_ID);
    atiic_write(output, "Access", "read-only");
    atiic_write(output, "Target", "PCI-1002-4757");
    atiic_write(output, "Blocks", first_offset == 0u ? "1,0" : "0");
    atiic_write_hex(output, "Status", result->status);

    display = GetDC(0);
    if (display != 0) {
        atiic_write_decimal(output, "DesktopWidth",
                            (DWORD)GetDeviceCaps(display, HORZRES));
        atiic_write_decimal(output, "DesktopHeight",
                            (DWORD)GetDeviceCaps(display, VERTRES));
        atiic_write_decimal(output, "DesktopBpp",
                            (DWORD)(GetDeviceCaps(display, BITSPIXEL) *
                                    GetDeviceCaps(display, PLANES)));
        ReleaseDC(0, display);
    }

    atiic_write_hex(output, "ConfigManagerStatus", ranges_status);
    atiic_write_decimal(output, "RangeCount", range_count);
    for (index = 0u; index < range_count; ++index) {
        key[0] = '\0';
        atiic_append(key, "Range", sizeof(key));
        atiic_decimal(text, index);
        atiic_append(key, text, sizeof(key));
        atiic_write(output, key,
                    ranges[index].type == ResType_Mem ? "mem" : "io");
        atiic_append(key, "Base", sizeof(key));
        atiic_write_hex(output, key, ranges[index].base);
        key[lstrlenA(key) - 4] = '\0';
        atiic_append(key, "Bytes", sizeof(key));
        atiic_write_hex(output, key, ranges[index].bytes);
    }
    atiic_write_hex(output, "AssignedBar0", request->assigned_bar0);
    atiic_write_hex(output, "AssignedBar2", request->assigned_bar2);

    atiic_write_hex(output, "PciConfigAddress", result->pci_address);
    atiic_write_decimal(output, "PciBus", (result->pci_address >> 16) & 0xffu);
    atiic_write_decimal(output, "PciDevice",
                        (result->pci_address >> 11) & 0x1fu);
    atiic_write_hex(output, "ConfigAddressFoundAndRestored",
                    result->config_address_restored);
    for (index = 0u; index < ATIIC_CONFIG_DWORDS; ++index) {
        key[0] = '\0';
        atiic_append(key, "Cfg", sizeof(key));
        atiic_hex(text, index * 4u, 2);
        atiic_append(key, text, sizeof(key));
        atiic_write_hex(output, key, result->config[index]);
    }

    atiic_write_hex(output, "Bar2Window", result->bar2_window);
    atiic_write_hex(output, "Bar2ChipId", result->bar2_chip_id);
    atiic_write_hex(output, "ApertureWindow", result->aperture_window);
    atiic_write_hex(output, "ApertureChipId", result->aperture_chip_id);
    atiic_write_hex(output, "SnapshotWindow", result->snapshot_window);
    atiic_write_decimal(output, "OffsetsRequested", request->count);
    atiic_write_decimal(output, "OffsetsAccepted", result->accepted);

    if ((result->status & ATIIC_SNAPSHOT_TAKEN) != 0ul) {
        for (index = 0u; index < result->accepted; ++index) {
            DWORD delta = result->first[index] ^ result->second[index];

            atiic_hex(text, result->offsets[index], 3);
            key[0] = 'A'; key[1] = '_'; key[2] = '\0';
            atiic_append(key, text, sizeof(key));
            atiic_write_hex(output, key, result->first[index]);
            if (delta != 0u) {
                key[0] = 'D';
                atiic_write_hex(output, key, delta);
                ++delta_count;
            }
        }
        atiic_write_decimal(output, "RepeatDeltaCount", delta_count);

        crtc = atiic_value(result, ATIIC_CRTC_OFF_PITCH);
        mem_cntl = atiic_value(result, ATIIC_MEM_CNTL);
        gui_stat = atiic_value(result, ATIIC_GUI_STAT);
        fifo_stat = atiic_value(result, ATIIC_FIFO_STAT);
        atiic_write_hex(output, "ChipType",
                        atiic_value(result, ATIIC_CONFIG_CHIP_ID) & 0xffffu);
        atiic_write_hex(output, "ChipClass",
                        (atiic_value(result, ATIIC_CONFIG_CHIP_ID) >> 16) &
                        0xffu);
        atiic_write_hex(output, "ChipRevision",
                        atiic_value(result, ATIIC_CONFIG_CHIP_ID) >> 24);
        atiic_write_decimal(output, "MemoryTypeField",
                            atiic_value(result, ATIIC_CONFIG_STAT0) & 7u);
        atiic_write_decimal(output, "VramIfPreVtbTable",
                            atiic_vram_pre_vtb(mem_cntl));
        atiic_write_decimal(output, "VramIfVtbTable", atiic_vram_vtb(mem_cntl));
        atiic_write(output, "BusExtRegEn",
                    (atiic_value(result, ATIIC_BUS_CNTL) &
                     ATIIC_BUS_EXT_REG_EN) != 0ul ? "1" : "0");
        atiic_write_decimal(output, "CrtcPitchPixels",
                            ((crtc >> 22) & 0x3ffu) * 8u);
        atiic_write_decimal(output, "CrtcOffsetBytes",
                            (crtc & 0x000ffffful) * 8u);
        atiic_write_decimal(output, "GuiStatField25to16",
                            (gui_stat >> 16) & 0x3ffu);
        atiic_write_decimal(output, "FifoStatLowBitsSet",
                            atiic_popcount(fifo_stat & 0xffffu));
    }

    atiic_write(output, "Rom",
                (result->status & ATIIC_ROM_COPIED) != 0ul ? "ATIIC.ROM"
                                                           : "not-copied");
    atiic_write(output, "Result",
                (result->status & ATIIC_SNAPSHOT_TAKEN) != 0ul
                    ? "SNAPSHOT" : "NO-WINDOW");
    CloseHandle(output);
    ExitProcess((result->status & ATIIC_SNAPSHOT_TAKEN) != 0ul ? 0u : 1u);
}
