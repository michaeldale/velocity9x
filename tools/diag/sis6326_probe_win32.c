/*
 * SIS6326.EXE: the SiS 6326 (PCI 1039:6326) read-only register probe.
 *
 * Publishes C:\V9XDIAG\SIS6326.TXT and C:\V9XDIAG\SIS6326.ROM. Runs beside
 * SIS6326.VXD, which does the reads; this side decides which MMIO offsets are
 * read and prints decodes it does not rely on.
 *
 * It is the Phase 0 first contact for the sis family: run under SiS's own
 * driver, it measures which PCI ranges the card claims, which MMIO window the
 * stock driver selected, whether it left the extension registers unlocked,
 * what the memory-size and DRAM-timing registers hold, the clocks the BIOS
 * programmed, and what the 2D and 3D engine registers read back after SiS's
 * driver has used them. Two decodes are printed side by side wherever the
 * datasheet and xf86-video-sis disagree (memory size, MCLK post-scale bit),
 * so the capture settles the disagreement instead of a guess. It writes
 * nothing to the card beyond the VGA index ports, except SR5 under /unlock,
 * so it cannot answer whether a register is writable.
 *
 * Register meanings: docs\specifications\sis6326-registers.md.
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

#define SIS_MAGIC 0x36325349ul
#define SIS_CONFIG_DWORDS 64u
#define SIS_VGA_INDEXES 0x40u
#define SIS_OFFSET_MAX 512u
#define SIS_ROM_BYTES 0x10000u
/* Eight was too few: A8U4I5 filled all eight with the VGA legacy ranges
 * still listed (2026-10-04). */
#define SIS_RANGE_MAX 16u
#define SIS_FLAG_FORCE_3D 0x00000001ul
#define SIS_FLAG_UNLOCK 0x00000002ul

#define SIS_PCI_FOUND        0x00000001ul
#define SIS_VGA_READ         0x00000002ul
#define SIS_MMIO_MAPPED      0x00000004ul
#define SIS_SNAPSHOT_TAKEN   0x00000008ul
#define SIS_ROM_COPIED       0x00000010ul
#define SIS_OFFSETS_REFUSED  0x00000020ul
#define SIS_3D_SKIPPED       0x00000040ul
#define SIS_MMIO_DISABLED    0x00000080ul
#define SIS_MMIO_BAR_INVALID 0x00000100ul
#define SIS_EXT_LOCKED       0x00000200ul
#define SIS_UNLOCKED_BY_PROBE 0x00000400ul

/* BAR1 is a 64 KiB MMIO window and BAR0 the 4 MiB framebuffer (datasheet
 * 7.10.4-5), which is how the Config Manager ranges are told apart. */
#define SIS_MMIO_BYTES 0x00010000ul

/* The MMIO ranges read: the 2D engine and its pattern RAM, the three 3D
 * vertices, the primitive and fire/status registers, the 3D state and
 * texture registers through the index palette, and the stipple pattern
 * (datasheet 7.8.1, 7.14.2). 8AD4h-8AFFh is reserved and 8AFFh is the TEND
 * dummy register, so neither is read. */
static const DWORD sis_ranges_read[][2] = {
    { 0x8280u, 0x8328u },
    { 0x8800u, 0x885cu },
    { 0x89f8u, 0x89fcu },
    { 0x8a00u, 0x8ad0u },
    { 0x8b00u, 0x8b7cu }
};

#define SIS_2D_CMD_STATUS 0x82a8u
#define SIS_3D_STATUS     0x89fcu
#define SIS_3D_ENABLE     0x8a00u
#define SIS_3D_DST_SET    0x8a14u

/* The PLL reference is not stated in the datasheet; 14.31818 MHz reproduces
 * xf86-video-sis's MCLK table (sis6326-registers.md section 4). */
#define SIS_REFERENCE_KHZ 14318ul

struct sis_request {
    DWORD assigned_mmio;
    DWORD flags;
    DWORD count;
    DWORD offsets[SIS_OFFSET_MAX];
};

struct sis_result {
    DWORD magic;
    DWORD status;
    DWORD pci_address;
    DWORD mmio_window;
    DWORD accepted;
    DWORD config_address_restored;
    DWORD misc_output;
    DWORD sequencer_index_restored;
    DWORD crtc_port;
    DWORD crtc_index_restored;
    DWORD cr80;
    DWORD sr05_found_relocked;
    DWORD config[SIS_CONFIG_DWORDS];
    BYTE sr[SIS_VGA_INDEXES];
    BYTE cr[SIS_VGA_INDEXES];
    DWORD offsets[SIS_OFFSET_MAX];
    DWORD first[SIS_OFFSET_MAX];
    DWORD second[SIS_OFFSET_MAX];
    BYTE rom[SIS_ROM_BYTES];
};

struct sis_range {
    DWORD type;
    DWORD base;
    DWORD bytes;
};

static struct sis_request sis_request_buffer;
static struct sis_result sis_result_buffer;

static void sis_copy(char *destination, const char *source, DWORD capacity)
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

static void sis_append(char *destination, const char *source, DWORD capacity)
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

static int sis_starts_with_ci(const char *text, const char *prefix)
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
static int sis_has_switch(const char *command_line, const char *name)
{
    if (command_line == 0) {
        return 0;
    }
    while (*command_line != '\0') {
        if (sis_starts_with_ci(command_line, name)) {
            return 1;
        }
        ++command_line;
    }
    return 0;
}

static void sis_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15u];
    }
    text[digits] = '\0';
}

static void sis_decimal(char *text, DWORD value)
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

static DWORD sis_read_u32(const BYTE *data)
{
    return (DWORD)data[0] | ((DWORD)data[1] << 8) |
           ((DWORD)data[2] << 16) | ((DWORD)data[3] << 24);
}

static void sis_write(HANDLE file, const char *key, const char *value)
{
    DWORD written;

    WriteFile(file, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(file, "=", 1u, &written, 0);
    WriteFile(file, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(file, "\r\n", 2u, &written, 0);
}

static void sis_write_hex(HANDLE file, const char *key, DWORD value)
{
    char text[12];

    text[0] = '0';
    text[1] = 'x';
    sis_hex(text + 2, value, 8);
    sis_write(file, key, text);
}

static void sis_write_byte(HANDLE file, const char *key, DWORD value)
{
    char text[8];

    text[0] = '0';
    text[1] = 'x';
    sis_hex(text + 2, value & 0xffu, 2);
    sis_write(file, key, text);
}

static void sis_write_decimal(HANDLE file, const char *key, DWORD value)
{
    char text[16];

    sis_decimal(text, value);
    sis_write(file, key, text);
}

static void sis_write_bit(HANDLE file, const char *key, DWORD value,
                          DWORD mask)
{
    sis_write(file, key, (value & mask) != 0ul ? "1" : "0");
}

static LONG sis_find_device(char *device_id, DWORD capacity)
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
            !sis_starts_with_ci(adapter, "VEN_1039&DEV_6326")) {
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
            sis_copy(device_id, "PCI\\", capacity);
            sis_append(device_id, adapter, capacity);
            sis_append(device_id, "\\", capacity);
            sis_append(device_id, instance, capacity);
            RegCloseKey(pci_key);
            return ERROR_SUCCESS;
        }
    }
    RegCloseKey(pci_key);
    return ERROR_FILE_NOT_FOUND;
}

/*
 * Every memory and I/O range Config Manager allocated to the card. The MMIO
 * window is mapped from the allocation rather than the live BAR dword, as
 * the ATI probes do, so a BAR that reads back differently is visible as a
 * mismatch instead of being followed.
 */
static CONFIGRET sis_ranges(struct sis_range *ranges, DWORD *count)
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
    if (sis_find_device(device_id, sizeof(device_id)) != ERROR_SUCCESS) {
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
            base_low = sis_read_u32(data + 8);
            end_low = sis_read_u32(data + 16);
            if (*count < SIS_RANGE_MAX && end_low >= base_low &&
                sis_read_u32(data + 12) == 0u &&
                sis_read_u32(data + 20) == 0u) {
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

/* Index of an MMIO offset in the snapshot, or SIS_OFFSET_MAX. */
static DWORD sis_find(const struct sis_result *result, DWORD offset)
{
    DWORD index;

    for (index = 0u; index < result->accepted; ++index) {
        if (result->offsets[index] == offset) {
            return index;
        }
    }
    return SIS_OFFSET_MAX;
}

static DWORD sis_value(const struct sis_result *result, DWORD offset)
{
    DWORD index = sis_find(result, offset);

    return index == SIS_OFFSET_MAX ? 0u : result->first[index];
}

/* SRC D[2:1] per the datasheet (7.7.9): 1 MiB, 2 MiB, 4 MiB, 1 MiB. */
static DWORD sis_vram_datasheet(DWORD src)
{
    static const DWORD sizes[4] = {
        1024ul * 1024ul, 2048ul * 1024ul, 4096ul * 1024ul, 1024ul * 1024ul
    };

    return sizes[(src >> 1) & 3u];
}

/* xf86-video-sis's three-bit code, SRC bit 4 over bits 2:1, and its table;
 * codes 3 and 4 are the ones it calls reserved and are printed as 0. */
static DWORD sis_vram_xorg_code(DWORD src)
{
    return ((src & 0x10u) >> 2) | ((src & 0x06u) >> 1);
}

static DWORD sis_vram_xorg(DWORD src)
{
    static const DWORD sizes[8] = {
        1024ul * 1024ul, 2048ul * 1024ul, 4096ul * 1024ul, 0ul,
        0ul, 2048ul * 1024ul, 4096ul * 1024ul, 8192ul * 1024ul
    };

    return sizes[sis_vram_xorg_code(src)];
}

/* Bus width that xf86-video-sis pairs with each code, in bits. */
static DWORD sis_bus_xorg(DWORD src)
{
    static const DWORD widths[8] = { 32u, 64u, 64u, 0u, 0u, 32u, 32u, 64u };

    return widths[sis_vram_xorg_code(src)];
}

/*
 * One synthesizer, in kHz: fr x (numerator / denominator) x
 * (divider / post-scale), datasheet 4.7. The numerator and denominator
 * registers hold value - 1; the divider bit doubles; post-scale is 1-4, or
 * 6/8 when the extension bit is set (codes 0 and 1 are reserved there and
 * decode as 0).
 */
static DWORD sis_clock_khz(DWORD numerator_reg, DWORD denominator_reg,
                           int extended)
{
    static const DWORD scale_normal[4] = { 1u, 2u, 3u, 4u };
    static const DWORD scale_extended[4] = { 0u, 0u, 6u, 8u };
    DWORD numerator = (numerator_reg & 0x7fu) + 1u;
    DWORD divider = (numerator_reg & 0x80u) != 0u ? 2u : 1u;
    DWORD denominator = (denominator_reg & 0x1fu) + 1u;
    DWORD scale_code = (denominator_reg >> 5) & 3u;
    DWORD scale = extended ? scale_extended[scale_code]
                           : scale_normal[scale_code];

    if (scale == 0u) {
        return 0u;
    }
    return SIS_REFERENCE_KHZ * numerator * divider / (denominator * scale);
}

/* DRAM type from the timing enables, since SRE D[1:0] is reserved
 * (sis6326-registers.md section 3). An inference, named as one. */
static const char *sis_dram_type(const BYTE *sr)
{
    if ((sr[0x33] & 0x01u) != 0u) {
        return "sgram-timing";
    }
    if ((sr[0x33] & 0x08u) != 0u) {
        return "edo-1-cycle";
    }
    if ((sr[0x23] & 0x20u) != 0u) {
        return "edo";
    }
    return "fast-page";
}

static const char *sis_mmio_select(DWORD srb)
{
    static const char * const names[4] = {
        "disabled", "A0000", "B0000", "pci-bar1"
    };

    return names[(srb >> 5) & 3u];
}

static void sis_write_indexed(HANDLE file, const char *prefix,
                              const BYTE *values)
{
    char key[16];
    char text[4];
    DWORD index;

    for (index = 0u; index < SIS_VGA_INDEXES; ++index) {
        sis_copy(key, prefix, sizeof(key));
        sis_hex(text, index, 2);
        sis_append(key, text, sizeof(key));
        sis_write_byte(file, key, values[index]);
    }
}

static void sis_write_rom(const struct sis_result *result)
{
    HANDLE file;
    DWORD written;

    file = CreateFileA(V9X_DIAG_SIS6326_ROM, GENERIC_WRITE, FILE_SHARE_READ,
                       0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    WriteFile(file, result->rom, SIS_ROM_BYTES, &written, 0);
    CloseHandle(file);
}

static void sis_write_decodes(HANDLE output, const struct sis_result *result)
{
    const BYTE *sr = result->sr;
    DWORD src = sr[0x0c];

    sis_write(output, "ExtensionLock",
              sr[0x05] == 0xa1u ? "unlocked"
              : sr[0x05] == 0x21u ? "locked" : "unknown");
    sis_write_bit(output, "LinearAddressing", sr[0x06], 0x80u);
    sis_write_bit(output, "HardwareCursor", sr[0x06], 0x40u);
    sis_write_bit(output, "TrueColourMode", sr[0x06], 0x10u);
    sis_write_bit(output, "Colour64KMode", sr[0x06], 0x08u);
    sis_write_bit(output, "Colour32KMode", sr[0x06], 0x04u);
    sis_write_bit(output, "EnhancedGraphicsMode", sr[0x06], 0x02u);
    sis_write(output, "TrueColourOrder",
              (sr[0x0b] & 0x80u) != 0u ? "BGR" : "RGB");
    sis_write(output, "MmioSelect", sis_mmio_select(sr[0x0b]));
    sis_write_hex(output, "LinearBaseFromSR20SR21",
                  ((DWORD)sr[0x20] << 19) |
                  ((DWORD)(sr[0x21] & 0x1fu) << 27));
    sis_write_decimal(output, "LinearApertureCode", (sr[0x21] >> 5) & 3u);

    sis_write_decimal(output, "MemoryConfigSRC21", (src >> 1) & 3u);
    sis_write_bit(output, "MemoryConfigSRC4", src, 0x10u);
    sis_write_decimal(output, "VramIfDatasheet", sis_vram_datasheet(src));
    sis_write_decimal(output, "VramIfXorgCode", sis_vram_xorg_code(src));
    sis_write_decimal(output, "VramIfXorg", sis_vram_xorg(src));
    sis_write_decimal(output, "BusBitsIfXorg", sis_bus_xorg(src));
    sis_write_decimal(output, "DramSpeedStraps", (sr[0x0e] >> 5) & 7u);
    sis_write(output, "DramTypeInferred", sis_dram_type(sr));

    /* SR13 D7 is MCLK post-scale bit 2 by SR13's own definition; the SR29
     * text names D6 instead. Both decodes are printed. */
    sis_write_decimal(output, "MclkKhzSR13D7",
                      sis_clock_khz(sr[0x28], sr[0x29],
                                    (sr[0x13] & 0x80u) != 0u));
    sis_write_decimal(output, "MclkKhzSR13D6",
                      sis_clock_khz(sr[0x28], sr[0x29],
                                    (sr[0x13] & 0x40u) != 0u));
    sis_write_decimal(output, "VclkBankSR38", sr[0x38] & 3u);
    sis_write_decimal(output, "VclkKhzSR13D6",
                      sis_clock_khz(sr[0x2a], sr[0x2b],
                                    (sr[0x13] & 0x40u) != 0u));
    sis_write_decimal(output, "VclkKhzSR13D7",
                      sis_clock_khz(sr[0x2a], sr[0x2b],
                                    (sr[0x13] & 0x80u) != 0u));

    sis_write_bit(output, "EngineRegisterEnable", sr[0x27], 0x40u);
    sis_write_bit(output, "TurboQueueEnable", sr[0x27], 0x80u);
    sis_write_decimal(output, "LogicalWidthCode", (sr[0x27] >> 4) & 3u);
    sis_write_decimal(output, "TurboQueueBase", sr[0x2c] & 0x7fu);
    sis_write_decimal(output, "TurboQueueSplit", sr[0x3c] & 3u);
    sis_write_bit(output, "Accel3DEnable", sr[0x39], 0x04u);
    sis_write_bit(output, "Pci66Timing", sr[0x3c], 0x04u);
    sis_write_bit(output, "AgpStrap", sr[0x0d], 0x10u);
    sis_write_bit(output, "Agp2xStrap", sr[0x0d], 0x20u);
}

static void sis_write_engine_decodes(HANDLE output,
                                     const struct sis_result *result)
{
    DWORD command = sis_value(result, SIS_2D_CMD_STATUS);
    DWORD status3d = sis_value(result, SIS_3D_STATUS);
    DWORD dst = sis_value(result, SIS_3D_DST_SET);

    /* 82A8h: queue status in D[15:0], Command 0 in D[23:16], Command 1 in
     * D[31:24]; Command 1 D6 is busy, D7 queue empty. */
    sis_write_decimal(output, "Engine2DQueueFree", command & 0x1fu);
    sis_write_bit(output, "Engine2DBusy", command, 0x40000000ul);
    sis_write_bit(output, "Engine2DQueueEmpty", command, 0x80000000ul);
    sis_write_hex(output, "Engine2DCommandWord", command >> 16);
    if ((result->status & SIS_3D_SKIPPED) != 0ul) {
        return;
    }
    sis_write_decimal(output, "Engine3DQueueFreeBytes",
                      ((status3d >> 16) & 0xfffu) * 8u);
    sis_write_bit(output, "Engine3DIdleQueueEmpty", status3d, 0x2u);
    sis_write_bit(output, "Engine3DIdle", status3d, 0x1u);
    sis_write_hex(output, "Engine3DEnable", sis_value(result, SIS_3D_ENABLE));
    sis_write_hex(output, "Engine3DDstFormat", (dst >> 16) & 0x7fu);
    sis_write_decimal(output, "Engine3DDstPitch", dst & 0x3fffu);
}

void WINAPI V9xSis6326ProbeEntry(void)
{
    struct sis_request *request = &sis_request_buffer;
    struct sis_result *result = &sis_result_buffer;
    struct sis_range ranges[SIS_RANGE_MAX];
    DWORD range_count = 0u;
    CONFIGRET ranges_status;
    HANDLE device;
    HANDLE output;
    HDC display;
    DWORD returned = 0u;
    DWORD index;
    DWORD range;
    DWORD offset;
    DWORD delta_count = 0u;
    char key[64];
    char text[16];
    static const char header[] = "[Sis6326Probe]\r\n";

    ranges_status = sis_ranges(ranges, &range_count);

    device = CreateFileA("\\\\.\\SIS6326.VXD", 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        ExitProcess(2u);
    }

    /*
     * First pass: configuration space only - no offsets, no unlock - to learn
     * the live BAR1. The MMIO window is then the Config Manager range at that
     * base. Size alone does not identify it: A8U4I5 listed two 64 KiB ranges,
     * DC000000h and DD000000h, with BAR1 at DD000000h (2026-10-04), and the
     * first build mapped the wrong one. No match, no MMIO read.
     */
    request->assigned_mmio = 0u;
    request->flags = 0u;
    request->count = 0u;
    if (!DeviceIoControl(device, 1u, request, sizeof(*request), result,
                         sizeof(*result), &returned, 0) ||
        returned != sizeof(*result) || result->magic != SIS_MAGIC) {
        CloseHandle(device);
        ExitProcess(3u);
    }
    for (index = 0u; index < range_count; ++index) {
        if (ranges[index].type == ResType_Mem &&
            ranges[index].bytes == SIS_MMIO_BYTES &&
            ranges[index].base == (result->config[5] & 0xfffffff0ul)) {
            request->assigned_mmio = ranges[index].base;
        }
    }

    /* The 3D block is read only when the stock driver enabled it, unless
     * /force3d says otherwise; the extensions are unlocked only with
     * /unlock. See sis6326_probe.asm. */
    if (sis_has_switch(GetCommandLineA(), "/force3d")) {
        request->flags |= SIS_FLAG_FORCE_3D;
    }
    if (sis_has_switch(GetCommandLineA(), "/unlock")) {
        request->flags |= SIS_FLAG_UNLOCK;
    }
    for (range = 0u; range < sizeof(sis_ranges_read) / sizeof(sis_ranges_read[0]);
         ++range) {
        for (offset = sis_ranges_read[range][0];
             offset <= sis_ranges_read[range][1]; offset += 4u) {
            request->offsets[request->count++] = offset;
        }
    }
    if (!DeviceIoControl(device, 1u, request, sizeof(*request), result,
                         sizeof(*result), &returned, 0) ||
        returned != sizeof(*result) || result->magic != SIS_MAGIC) {
        CloseHandle(device);
        ExitProcess(3u);
    }
    CloseHandle(device);

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    if ((result->status & SIS_ROM_COPIED) != 0ul) {
        sis_write_rom(result);
    }
    output = CreateFileA(V9X_DIAG_SIS6326_TXT, GENERIC_WRITE, FILE_SHARE_READ,
                         0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (output == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    WriteFile(output, header, (DWORD)lstrlenA(header), &returned, 0);
    sis_write(output, "Build", V9X_BUILD_ID);
    sis_write(output, "Access", "read-only, VGA index ports restored");
    sis_write(output, "Target", "PCI-1039-6326");
    sis_write(output, "Force3D",
              (request->flags & SIS_FLAG_FORCE_3D) != 0ul ? "1" : "0");
    sis_write(output, "Unlock",
              (request->flags & SIS_FLAG_UNLOCK) != 0ul ? "1" : "0");
    sis_write_hex(output, "Status", result->status);

    display = GetDC(0);
    if (display != 0) {
        sis_write_decimal(output, "DesktopWidth",
                          (DWORD)GetDeviceCaps(display, HORZRES));
        sis_write_decimal(output, "DesktopHeight",
                          (DWORD)GetDeviceCaps(display, VERTRES));
        sis_write_decimal(output, "DesktopBpp",
                          (DWORD)(GetDeviceCaps(display, BITSPIXEL) *
                                  GetDeviceCaps(display, PLANES)));
        ReleaseDC(0, display);
    }

    sis_write_hex(output, "ConfigManagerStatus", ranges_status);
    sis_write_decimal(output, "RangeCount", range_count);
    for (index = 0u; index < range_count; ++index) {
        key[0] = '\0';
        sis_append(key, "Range", sizeof(key));
        sis_decimal(text, index);
        sis_append(key, text, sizeof(key));
        sis_write(output, key,
                  ranges[index].type == ResType_Mem ? "mem" : "io");
        sis_append(key, "Base", sizeof(key));
        sis_write_hex(output, key, ranges[index].base);
        key[lstrlenA(key) - 4] = '\0';
        sis_append(key, "Bytes", sizeof(key));
        sis_write_hex(output, key, ranges[index].bytes);
    }
    sis_write_hex(output, "AssignedMmio", request->assigned_mmio);

    sis_write_hex(output, "PciConfigAddress", result->pci_address);
    sis_write_decimal(output, "PciBus", (result->pci_address >> 16) & 0xffu);
    sis_write_decimal(output, "PciDevice",
                      (result->pci_address >> 11) & 0x1fu);
    sis_write_hex(output, "ConfigAddressFoundAndRestored",
                  result->config_address_restored);
    sis_write_byte(output, "PciRevision", result->config[2]);
    for (index = 0u; index < SIS_CONFIG_DWORDS; ++index) {
        key[0] = '\0';
        sis_append(key, "Cfg", sizeof(key));
        sis_hex(text, index * 4u, 2);
        sis_append(key, text, sizeof(key));
        sis_write_hex(output, key, result->config[index]);
    }

    if ((result->status & SIS_VGA_READ) != 0ul) {
        sis_write_byte(output, "MiscOutput", result->misc_output);
        sis_write_byte(output, "SequencerIndexRestored",
                       result->sequencer_index_restored);
        sis_write_hex(output, "CrtcPort", result->crtc_port);
        sis_write_byte(output, "CrtcIndexRestored",
                       result->crtc_index_restored);
        sis_write_indexed(output, "SR", result->sr);
        sis_write_indexed(output, "CR", result->cr);
        sis_write_byte(output, "CR80", result->cr80);
        sis_write_byte(output, "SR05Found", result->sr05_found_relocked);
        if ((result->status & SIS_UNLOCKED_BY_PROBE) != 0ul) {
            sis_write_byte(output, "SR05AfterRelock",
                           result->sr05_found_relocked >> 8);
        }
        /* Locked, every SR06+ read returns the lock value: nothing to
         * decode, and the decodes would only mislead. */
        if ((result->status & SIS_EXT_LOCKED) != 0ul) {
            sis_write(output, "Decodes", "suppressed-extensions-locked");
        } else {
            sis_write_decodes(output, result);
        }
    }

    sis_write_hex(output, "MmioWindow", result->mmio_window);
    sis_write_decimal(output, "OffsetsRequested", request->count);
    sis_write_decimal(output, "OffsetsAccepted", result->accepted);
    if ((result->status & SIS_SNAPSHOT_TAKEN) != 0ul) {
        for (index = 0u; index < result->accepted; ++index) {
            DWORD delta = result->first[index] ^ result->second[index];

            if ((result->status & SIS_3D_SKIPPED) != 0ul &&
                result->offsets[index] >= 0x8800u) {
                continue;
            }
            sis_hex(text, result->offsets[index], 4);
            key[0] = 'M'; key[1] = '_'; key[2] = '\0';
            sis_append(key, text, sizeof(key));
            sis_write_hex(output, key, result->first[index]);
            if (delta != 0u) {
                key[0] = 'D';
                sis_write_hex(output, key, delta);
                ++delta_count;
            }
        }
        sis_write_decimal(output, "RepeatDeltaCount", delta_count);
        sis_write_engine_decodes(output, result);
    }

    sis_write(output, "Rom",
              (result->status & SIS_ROM_COPIED) != 0ul ? "SIS6326.ROM"
                                                       : "not-copied");
    sis_write(output, "Result",
              (result->status & SIS_SNAPSHOT_TAKEN) != 0ul ? "SNAPSHOT"
              : (result->status & SIS_EXT_LOCKED) != 0ul ? "LOCKED"
              : (result->status & SIS_VGA_READ) != 0ul ? "VGA-ONLY"
              : "NOT-FOUND");
    CloseHandle(output);
    ExitProcess((result->status & SIS_SNAPSHOT_TAKEN) != 0ul ? 0u : 1u);
}
