/*
 * VSA100.EXE: the 3dfx VSA-100 (PCI 121A:0009) read-only register probe.
 *
 * Publishes C:\V9XDIAG\VSA100.TXT and C:\V9XDIAG\VSA100.ROM. Runs beside
 * VSA100.VXD, which does the reads; this side decides which BAR0 offsets are
 * read and prints decodes it does not rely on.
 *
 * It is Phase 1 of docs\plans\3dfx-vsa100-family.md: run under 3dfx's own
 * driver, it measures the PCI identity and ranges, how many chips the board
 * carries, what the memory straps decode to, where the BIOS or driver put
 * BAR1's linear/tiled boundary, the clocks, and the video processor's
 * desktop setup. The decodes come from src\chipsets\tdfx\vsa100_decode.c,
 * the same host-tested functions the driver will use, so a capture that
 * disagrees with them is a finding about the decoder, not noise.
 *
 * Switches:
 *   /notile  skip the lfbMemoryConfig selector reads (the probe's only
 *            register write; see vsa100_probe.asm).
 *   /engine  also read the command FIFO and 2D register windows.
 *
 * Register meanings: docs\specifications\vsa100-registers.md.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/tdfx_vsa100.h"

#include "../../src/chipsets/tdfx/vsa100_decode.c"

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

#define VSA_MAGIC 0x30315356ul
#define VSA_CONFIG_DWORDS 64u
#define VSA_FUNCTIONS 8u
#define VSA_VGA_INDEXES 0x40u
#define VSA_OFFSET_MAX 256u
#define VSA_ROM_BYTES 0x10000u
#define VSA_RANGE_MAX 16u
#define VSA_FLAG_TILE 0x00000001ul
#define VSA_FLAG_ENGINE 0x00000002ul

#define VSA_PCI_FOUND       0x00000001ul
#define VSA_VGA_READ        0x00000002ul
#define VSA_IO_MAPPED       0x00000004ul
#define VSA_SNAPSHOT_TAKEN  0x00000008ul
#define VSA_ROM_COPIED      0x00000010ul
#define VSA_OFFSETS_REFUSED 0x00000020ul
#define VSA_BAR0_INVALID    0x00000040ul
#define VSA_FIFO_MAPPED     0x00000080ul
#define VSA_2D_MAPPED       0x00000100ul
#define VSA_TILE_READ       0x00000200ul
#define VSA_TILE_AMBIGUOUS  0x00000400ul

/*
 * The init and video registers read by default: each one named in
 * vsa100-registers.md. dramCommand/dramData (30h/34h) and the CLUT data port
 * are left out, since a read there is not documented as side-effect free.
 * lfbMemoryConfig (0Ch) is in the list as a plain read; the selector reads
 * are separate.
 */
static const DWORD vsa_io_offsets[] = {
    0x000u, 0x004u, 0x00cu, 0x010u, 0x014u, 0x018u, 0x01cu, 0x020u,
    0x024u, 0x028u, 0x02cu, 0x038u, 0x040u, 0x044u, 0x048u, 0x04cu,
    0x05cu, 0x060u, 0x064u, 0x068u, 0x06cu, 0x098u, 0x0e4u, 0x0e8u
};

/* Under /engine: the command FIFO 0 registers and the 2D registers below the
 * write-only launch area (BAR0 80000h + 20h-4Ch, 100000h + 0-7Ch). */
static const DWORD vsa_engine_ranges[][2] = {
    { 0x080020u, 0x08004cu },
    { 0x100000u, 0x10007cu }
};

/* Misc output bits 3:2 select the pixel clock: 0 and 1 are the fixed VGA
 * clocks, 3 the programmable PLL. */
#define VSA_MISC_CLOCK_SHIFT 2
#define VSA_MISC_CLOCK_MASK 3u
#define VSA_VGA_CLOCK0_KHZ 25175ul
#define VSA_VGA_CLOCK1_KHZ 28322ul
#define VSA_CLOCK_PLL 3u

/* vidProcCfg fields printed (vsa100-registers.md, video processor). */
#define VSA_VPC_ENABLE 0x00000001ul
#define VSA_VPC_DESKTOP_ENABLE 0x00000080ul
#define VSA_VPC_OVERLAY_ENABLE 0x00000100ul
#define VSA_VPC_DESKTOP_CLUT_BYPASS 0x00000400ul
#define VSA_VPC_DESKTOP_FORMAT_SHIFT 18
#define VSA_VPC_DESKTOP_FORMAT_MASK 7ul
#define VSA_VPC_DESKTOP_TILED 0x01000000ul
#define VSA_VPC_2X 0x04000000ul
#define VSA_VPC_CURSOR 0x08000000ul

struct vsa_request {
    DWORD bar0;
    DWORD flags;
    DWORD count;
    DWORD offsets[VSA_OFFSET_MAX];
};

struct vsa_result {
    DWORD magic;
    DWORD status;
    DWORD pci_address;
    DWORD bar0;
    DWORD accepted;
    DWORD config_address_restored;
    DWORD misc_output;
    DWORD sequencer_index_restored;
    DWORD crtc_port;
    DWORD crtc_index_restored;
    DWORD function_count;
    DWORD lfb_plain;
    DWORD tile_control;
    DWORD tile_compare;
    DWORD tile_selector_restored;
    DWORD reserved;
    DWORD config[VSA_CONFIG_DWORDS];
    DWORD functions[VSA_FUNCTIONS];
    BYTE sr[VSA_VGA_INDEXES];
    BYTE cr[VSA_VGA_INDEXES];
    DWORD offsets[VSA_OFFSET_MAX];
    DWORD first[VSA_OFFSET_MAX];
    DWORD second[VSA_OFFSET_MAX];
    BYTE rom[VSA_ROM_BYTES];
};

struct vsa_range {
    DWORD type;
    DWORD base;
    DWORD bytes;
};

static struct vsa_request vsa_request_buffer;
static struct vsa_result vsa_result_buffer;

static void vsa_copy(char *destination, const char *source, DWORD capacity)
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

static void vsa_append(char *destination, const char *source, DWORD capacity)
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

static int vsa_starts_with_ci(const char *text, const char *prefix)
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
static int vsa_has_switch(const char *command_line, const char *name)
{
    if (command_line == 0) {
        return 0;
    }
    while (*command_line != '\0') {
        if (vsa_starts_with_ci(command_line, name)) {
            return 1;
        }
        ++command_line;
    }
    return 0;
}

static void vsa_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15u];
    }
    text[digits] = '\0';
}

static void vsa_decimal(char *text, DWORD value)
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

static DWORD vsa_read_u32(const BYTE *data)
{
    return (DWORD)data[0] | ((DWORD)data[1] << 8) |
           ((DWORD)data[2] << 16) | ((DWORD)data[3] << 24);
}

static void vsa_write(HANDLE file, const char *key, const char *value)
{
    DWORD written;

    WriteFile(file, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(file, "=", 1u, &written, 0);
    WriteFile(file, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(file, "\r\n", 2u, &written, 0);
}

static void vsa_write_hex(HANDLE file, const char *key, DWORD value)
{
    char text[12];

    text[0] = '0';
    text[1] = 'x';
    vsa_hex(text + 2, value, 8);
    vsa_write(file, key, text);
}

static void vsa_write_byte(HANDLE file, const char *key, DWORD value)
{
    char text[8];

    text[0] = '0';
    text[1] = 'x';
    vsa_hex(text + 2, value & 0xffu, 2);
    vsa_write(file, key, text);
}

static void vsa_write_decimal(HANDLE file, const char *key, DWORD value)
{
    char text[16];

    vsa_decimal(text, value);
    vsa_write(file, key, text);
}

static void vsa_write_bit(HANDLE file, const char *key, DWORD value,
                          DWORD mask)
{
    vsa_write(file, key, (value & mask) != 0ul ? "1" : "0");
}

/*
 * The wanted-th Enum\PCI instance of 121A:0009, counting from zero. Every
 * card ever fitted keeps its key, so the first match need not be the card
 * in the slot; the caller tries each until Config Manager locates a present
 * devnode.
 */
static LONG vsa_find_device(char *device_id, DWORD capacity, DWORD wanted)
{
    HKEY pci_key;
    HKEY adapter_key;
    DWORD adapter_index = 0u;
    DWORD matched = 0u;
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
            !vsa_starts_with_ci(adapter, "VEN_121A&DEV_0009")) {
            continue;
        }
        if (matched++ != wanted) {
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
            vsa_copy(device_id, "PCI\\", capacity);
            vsa_append(device_id, adapter, capacity);
            vsa_append(device_id, "\\", capacity);
            vsa_append(device_id, instance, capacity);
            RegCloseKey(pci_key);
            return ERROR_SUCCESS;
        }
    }
    RegCloseKey(pci_key);
    return ERROR_FILE_NOT_FOUND;
}

/*
 * Every memory and I/O range Config Manager allocated to the card. BAR0 is
 * mapped from the allocation rather than the live BAR dword, as the SiS and
 * ATI probes do, so a BAR that reads back differently is visible as a
 * mismatch instead of being followed.
 */
static CONFIGRET vsa_ranges(struct vsa_range *ranges, DWORD *count)
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
    DWORD wanted;

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
    /* CM_LOCATE_DEVNODE_NORMAL finds only present devnodes, so the first
     * instance it accepts is the card in the slot. */
    status = CR_NO_SUCH_DEVNODE;
    for (wanted = 0u; status != CR_SUCCESS; ++wanted) {
        if (vsa_find_device(device_id, sizeof(device_id), wanted) !=
            ERROR_SUCCESS) {
            FreeLibrary(module);
            return CR_NO_SUCH_DEVNODE;
        }
        status = cm_locate(&device, device_id, CM_LOCATE_DEVNODE_NORMAL);
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
            base_low = vsa_read_u32(data + 8);
            end_low = vsa_read_u32(data + 16);
            if (*count < VSA_RANGE_MAX && end_low >= base_low &&
                vsa_read_u32(data + 12) == 0u &&
                vsa_read_u32(data + 20) == 0u) {
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

/* The first-pass value at a BAR0 offset, or 0 if it was not read. */
static DWORD vsa_value(const struct vsa_result *result, DWORD offset)
{
    DWORD index;

    for (index = 0u; index < result->accepted; ++index) {
        if (result->offsets[index] == offset) {
            return result->first[index];
        }
    }
    return 0u;
}

static void vsa_write_rom(const struct vsa_result *result)
{
    HANDLE file;
    DWORD written;

    file = CreateFileA(V9X_DIAG_VSA100_ROM, GENERIC_WRITE, FILE_SHARE_READ,
                       0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    WriteFile(file, result->rom, VSA_ROM_BYTES, &written, 0);
    CloseHandle(file);
}

static void vsa_write_indexed(HANDLE file, const char *prefix,
                              const BYTE *values)
{
    char key[16];
    char text[4];
    DWORD index;

    for (index = 0u; index < VSA_VGA_INDEXES; ++index) {
        vsa_copy(key, prefix, sizeof(key));
        vsa_hex(text, index, 2);
        vsa_append(key, text, sizeof(key));
        vsa_write_byte(file, key, values[index]);
    }
}

/*
 * CRTC timing with the 3dfx extension bits (VelocityNT's decode of the
 * Mini-Me's BIOS mode, vsa100-registers.md): CR1A bit 0 and bit 2 extend
 * horizontal total and display, CR1B bit 0 and bit 2 vertical total and
 * display.
 */
static void vsa_write_crtc_decodes(HANDLE output, const BYTE *cr)
{
    DWORD htotal = ((DWORD)cr[0x00] + ((cr[0x1a] & 0x01u) != 0u ? 256u : 0u) +
                    5u) * 8u;
    DWORD hdisp = ((DWORD)cr[0x01] + ((cr[0x1a] & 0x04u) != 0u ? 256u : 0u) +
                   1u) * 8u;
    DWORD vtotal = (DWORD)cr[0x06] + ((cr[0x07] & 0x01u) != 0u ? 256u : 0u) +
                   ((cr[0x07] & 0x20u) != 0u ? 512u : 0u) +
                   ((cr[0x1b] & 0x01u) != 0u ? 1024u : 0u) + 2u;
    DWORD vdisp = (DWORD)cr[0x12] + ((cr[0x07] & 0x02u) != 0u ? 256u : 0u) +
                  ((cr[0x07] & 0x40u) != 0u ? 512u : 0u) +
                  ((cr[0x1b] & 0x04u) != 0u ? 1024u : 0u) + 1u;

    vsa_write_decimal(output, "CrtcHorizontalTotal", htotal);
    vsa_write_decimal(output, "CrtcHorizontalDisplay", hdisp);
    vsa_write_decimal(output, "CrtcVerticalTotal", vtotal);
    vsa_write_decimal(output, "CrtcVerticalDisplay", vdisp);
}

static void vsa_write_decodes(HANDLE output, const struct vsa_result *result)
{
    DWORD dram0 = vsa_value(result, V9X_TDFX_DRAM_INIT0);
    DWORD dram1 = vsa_value(result, V9X_TDFX_DRAM_INIT1);
    DWORD vpc = vsa_value(result, V9X_TDFX_VID_PROC_CFG);
    DWORD screen = vsa_value(result, V9X_TDFX_VID_SCREEN_SIZE);
    DWORD stride = vsa_value(result, V9X_TDFX_VID_DESKTOP_STRIDE);
    DWORD clock = (result->misc_output >> VSA_MISC_CLOCK_SHIFT) &
                  VSA_MISC_CLOCK_MASK;
    DWORD pixel_khz = 0u;

    vsa_write_decimal(output, "MemoryParts",
                      (dram0 & 0x04000000ul) != 0ul ? 8u : 4u);
    vsa_write_decimal(output, "MemoryPartMbit",
                      ((dram0 >> 27) & 7ul) <= 4ul ? 8ul << ((dram0 >> 27) & 7ul)
                                                   : 0ul);
    vsa_write_decimal(output, "MemoryDecodedBytes",
                      v9x_tdfx_vsa100_decode_memory(dram0));
    vsa_write(output, "MemoryType",
              (dram1 & 0x40000000ul) != 0ul ? "sdram" : "sgram");

    if ((result->status & VSA_TILE_READ) != 0ul) {
        vsa_write_hex(output, "TileFirstPage",
                      v9x_tdfx_vsa100_tile_first_page(result->tile_control,
                                                      result->tile_compare));
        vsa_write_decimal(output, "LinearUsableBytes",
                          v9x_tdfx_vsa100_usable_memory(dram0,
                                                        result->tile_control,
                                                        result->tile_compare));
    }

    vsa_write_decimal(output, "Pll0Khz",
                      v9x_tdfx_vsa100_pll_khz(vsa_value(result,
                                                        V9X_TDFX_PLL_CTRL0)));
    vsa_write_decimal(output, "Pll1Khz",
                      v9x_tdfx_vsa100_pll_khz(vsa_value(result, 0x044u)));
    /* 48h is pllCtrl2 in the spec's register list and Glide, iMatchCtrl in
     * its init-register section (vsa100-registers.md, disagreements). */
    vsa_write_decimal(output, "Pll2Khz",
                      v9x_tdfx_vsa100_pll_khz(vsa_value(result, 0x048u)));
    vsa_write_decimal(output, "PixelClockSelect", clock);
    if (clock == 0u) {
        pixel_khz = VSA_VGA_CLOCK0_KHZ;
    } else if (clock == 1u) {
        pixel_khz = VSA_VGA_CLOCK1_KHZ;
    } else if (clock == VSA_CLOCK_PLL) {
        pixel_khz = v9x_tdfx_vsa100_pll_khz(vsa_value(result,
                                                      V9X_TDFX_PLL_CTRL0));
    }
    vsa_write_decimal(output, "PixelClockKhz", pixel_khz);

    vsa_write_bit(output, "VidProcEnable", vpc, VSA_VPC_ENABLE);
    vsa_write_bit(output, "DesktopEnable", vpc, VSA_VPC_DESKTOP_ENABLE);
    vsa_write_bit(output, "OverlayEnable", vpc, VSA_VPC_OVERLAY_ENABLE);
    vsa_write_bit(output, "DesktopClutBypass", vpc,
                  VSA_VPC_DESKTOP_CLUT_BYPASS);
    vsa_write_decimal(output, "DesktopFormatCode",
                      (vpc >> VSA_VPC_DESKTOP_FORMAT_SHIFT) &
                      VSA_VPC_DESKTOP_FORMAT_MASK);
    vsa_write_bit(output, "DesktopTiled", vpc, VSA_VPC_DESKTOP_TILED);
    vsa_write_bit(output, "Video2x", vpc, VSA_VPC_2X);
    vsa_write_bit(output, "HardwareCursor", vpc, VSA_VPC_CURSOR);
    vsa_write_decimal(output, "ScreenWidth", screen & 0xfffu);
    vsa_write_decimal(output, "ScreenHeight", (screen >> 12) & 0xfffu);
    vsa_write_decimal(output, "DesktopStrideBytes", stride & 0x3fffu);
    vsa_write_hex(output, "DesktopStart",
                  vsa_value(result, V9X_TDFX_VID_DESKTOP_START));
    vsa_write_bit(output, "StatusBusy", vsa_value(result, V9X_TDFX_STATUS),
                  V9X_TDFX_STATUS_BUSY);
}

void WINAPI V9xVsa100ProbeEntry(void)
{
    struct vsa_request *request = &vsa_request_buffer;
    struct vsa_result *result = &vsa_result_buffer;
    struct vsa_range ranges[VSA_RANGE_MAX];
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
    static const char header[] = "[Vsa100Probe]\r\n";

    ranges_status = vsa_ranges(ranges, &range_count);

    device = CreateFileA("\\\\.\\VSA100.VXD", 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        ExitProcess(2u);
    }

    /*
     * First pass: configuration space only, no offsets and no selector
     * write, to learn the live BAR0. BAR0 is then the Config Manager memory
     * range at that base. No match, no MMIO read.
     */
    request->bar0 = 0u;
    request->flags = 0u;
    request->count = 0u;
    if (!DeviceIoControl(device, 1u, request, sizeof(*request), result,
                         sizeof(*result), &returned, 0) ||
        returned != sizeof(*result) || result->magic != VSA_MAGIC) {
        CloseHandle(device);
        ExitProcess(3u);
    }
    for (index = 0u; index < range_count; ++index) {
        if (ranges[index].type == ResType_Mem &&
            ranges[index].base == (result->config[4] & 0xfffffff0ul)) {
            request->bar0 = ranges[index].base;
        }
    }

    if (!vsa_has_switch(GetCommandLineA(), "/notile")) {
        request->flags |= VSA_FLAG_TILE;
    }
    if (vsa_has_switch(GetCommandLineA(), "/engine")) {
        request->flags |= VSA_FLAG_ENGINE;
    }
    for (index = 0u; index < sizeof(vsa_io_offsets) / sizeof(vsa_io_offsets[0]);
         ++index) {
        request->offsets[request->count++] = vsa_io_offsets[index];
    }
    if ((request->flags & VSA_FLAG_ENGINE) != 0ul) {
        for (range = 0u;
             range < sizeof(vsa_engine_ranges) / sizeof(vsa_engine_ranges[0]);
             ++range) {
            for (offset = vsa_engine_ranges[range][0];
                 offset <= vsa_engine_ranges[range][1]; offset += 4u) {
                request->offsets[request->count++] = offset;
            }
        }
    }
    if (!DeviceIoControl(device, 1u, request, sizeof(*request), result,
                         sizeof(*result), &returned, 0) ||
        returned != sizeof(*result) || result->magic != VSA_MAGIC) {
        CloseHandle(device);
        ExitProcess(3u);
    }
    CloseHandle(device);

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    if ((result->status & VSA_ROM_COPIED) != 0ul) {
        vsa_write_rom(result);
    }
    output = CreateFileA(V9X_DIAG_VSA100_TXT, GENERIC_WRITE, FILE_SHARE_READ,
                         0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (output == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    WriteFile(output, header, (DWORD)lstrlenA(header), &returned, 0);
    vsa_write(output, "Build", V9X_BUILD_ID);
    vsa_write(output, "Access",
              (request->flags & VSA_FLAG_TILE) != 0ul
                  ? "read-only, VGA index ports and lfbMemoryConfig selector restored"
                  : "read-only, VGA index ports restored");
    vsa_write(output, "Target", "PCI-121A-0009");
    vsa_write(output, "Engine",
              (request->flags & VSA_FLAG_ENGINE) != 0ul ? "1" : "0");
    vsa_write_hex(output, "Status", result->status);

    display = GetDC(0);
    if (display != 0) {
        vsa_write_decimal(output, "DesktopWidth",
                          (DWORD)GetDeviceCaps(display, HORZRES));
        vsa_write_decimal(output, "DesktopHeight",
                          (DWORD)GetDeviceCaps(display, VERTRES));
        vsa_write_decimal(output, "DesktopBpp",
                          (DWORD)(GetDeviceCaps(display, BITSPIXEL) *
                                  GetDeviceCaps(display, PLANES)));
        vsa_write_decimal(output, "DesktopRefresh",
                          (DWORD)GetDeviceCaps(display, VREFRESH));
        ReleaseDC(0, display);
    }

    vsa_write_hex(output, "ConfigManagerStatus", ranges_status);
    vsa_write_decimal(output, "RangeCount", range_count);
    for (index = 0u; index < range_count; ++index) {
        key[0] = '\0';
        vsa_append(key, "Range", sizeof(key));
        vsa_decimal(text, index);
        vsa_append(key, text, sizeof(key));
        vsa_write(output, key,
                  ranges[index].type == ResType_Mem ? "mem" : "io");
        vsa_append(key, "Base", sizeof(key));
        vsa_write_hex(output, key, ranges[index].base);
        key[lstrlenA(key) - 4] = '\0';
        vsa_append(key, "Bytes", sizeof(key));
        vsa_write_hex(output, key, ranges[index].bytes);
    }
    vsa_write_hex(output, "AssignedBar0", request->bar0);

    vsa_write_hex(output, "PciConfigAddress", result->pci_address);
    vsa_write_decimal(output, "PciBus", (result->pci_address >> 16) & 0xffu);
    vsa_write_decimal(output, "PciDevice",
                      (result->pci_address >> 11) & 0x1fu);
    vsa_write_hex(output, "ConfigAddressFoundAndRestored",
                  result->config_address_restored);
    vsa_write_byte(output, "PciRevision", result->config[2]);
    vsa_write_hex(output, "PciSubsystem", result->config[11]);
    vsa_write_decimal(output, "ChipFunctions", result->function_count);
    for (index = 0u; index < VSA_FUNCTIONS; ++index) {
        key[0] = '\0';
        vsa_append(key, "Function", sizeof(key));
        vsa_decimal(text, index);
        vsa_append(key, text, sizeof(key));
        vsa_append(key, "Id", sizeof(key));
        vsa_write_hex(output, key, result->functions[index]);
    }
    for (index = 0u; index < VSA_CONFIG_DWORDS; ++index) {
        key[0] = '\0';
        vsa_append(key, "Cfg", sizeof(key));
        vsa_hex(text, index * 4u, 2);
        vsa_append(key, text, sizeof(key));
        vsa_write_hex(output, key, result->config[index]);
    }

    if ((result->status & VSA_VGA_READ) != 0ul) {
        vsa_write_byte(output, "MiscOutput", result->misc_output);
        vsa_write_byte(output, "SequencerIndexRestored",
                       result->sequencer_index_restored);
        vsa_write_hex(output, "CrtcPort", result->crtc_port);
        vsa_write_byte(output, "CrtcIndexRestored",
                       result->crtc_index_restored);
        vsa_write_indexed(output, "SR", result->sr);
        vsa_write_indexed(output, "CR", result->cr);
        vsa_write_crtc_decodes(output, result->cr);
    }

    if ((result->status & VSA_TILE_READ) != 0ul) {
        vsa_write_hex(output, "LfbMemoryConfigPlain", result->lfb_plain);
        vsa_write_hex(output, "TileControl", result->tile_control);
        vsa_write_hex(output, "TileCompare", result->tile_compare);
        vsa_write_hex(output, "TileSelectorRestored",
                      result->tile_selector_restored);
        vsa_write_bit(output, "TileAmbiguous", result->status,
                      VSA_TILE_AMBIGUOUS);
    }

    vsa_write_hex(output, "Bar0Used", result->bar0);
    vsa_write_decimal(output, "OffsetsRequested", request->count);
    vsa_write_decimal(output, "OffsetsAccepted", result->accepted);
    if ((result->status & VSA_SNAPSHOT_TAKEN) != 0ul) {
        for (index = 0u; index < result->accepted; ++index) {
            DWORD delta = result->first[index] ^ result->second[index];

            vsa_hex(text, result->offsets[index], 6);
            key[0] = 'M'; key[1] = '_'; key[2] = '\0';
            vsa_append(key, text, sizeof(key));
            vsa_write_hex(output, key, result->first[index]);
            if (delta != 0u) {
                key[0] = 'D';
                vsa_write_hex(output, key, delta);
                ++delta_count;
            }
        }
        vsa_write_decimal(output, "RepeatDeltaCount", delta_count);
        vsa_write_decodes(output, result);
    }

    vsa_write(output, "Rom",
              (result->status & VSA_ROM_COPIED) != 0ul ? "VSA100.ROM"
                                                       : "not-copied");
    vsa_write(output, "Result",
              (result->status & VSA_SNAPSHOT_TAKEN) != 0ul ? "SNAPSHOT"
              : (result->status & VSA_BAR0_INVALID) != 0ul ? "BAR0-INVALID"
              : (result->status & VSA_VGA_READ) != 0ul ? "VGA-ONLY"
              : "NOT-FOUND");
    CloseHandle(output);
    ExitProcess((result->status & VSA_SNAPSHOT_TAKEN) != 0ul ? 0u : 1u);
}
