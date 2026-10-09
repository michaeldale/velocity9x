/*
 * MGA2D.EXE: the Matrox MGA-2064W 2D drawing engine write probe.
 *
 * Publishes C:\V9XDIAG\MGA2D.TXT. Runs beside MGA2D.VXD, which executes the
 * op lists built here and refuses anything outside the drawing registers,
 * FIFOSTATUS/STATUS and a 1 MiB window of the framebuffer. It answers what
 * no source settles for this chip before the engine is put behind GDI or
 * DirectDraw (src\chipsets\matrox\mga_engine.c names them):
 *
 *   - whether a TRAP fill's right edge is exclusive (FXBNDRY fxright = x + w)
 *     and a BITBLT's inclusive (fxright = x + w - 1), as FreeBE/AF has it;
 *   - whether AR0/AR3 take full linear pixel addresses from the start of
 *     VRAM, with YDSTORG applying only to the destination;
 *   - whether SGN scanleft and sdy give memmove semantics for each overlap;
 *   - whether dwgengsts clears, and how fast.
 *
 * Every engine write comes from the host-tested builder
 * (src\chipsets\matrox\mga_engine.c), compiled in. Safety contract:
 *
 *   - Run under Velocity9x tier-0, whose display path writes no MGA register,
 *     at 8, 16 or 32 bpp. The engine's depth follows the desktop's.
 *   - The card must be PCI 102B:0519 with Configuration Manager's BAR0 range
 *     exactly 16 KiB (MGABASE1) at the base the VxD reads from config 10h.
 *   - Engine output and CPU test writes stay in a 1 MiB region starting 1 MiB
 *     below the end of VRAM, which must lie beyond the visible desktop.
 *   - Engine state written by the setup (MACCESS, PLNWT, the clip window) is
 *     not restored: the drawing registers are not read back through this
 *     VxD, and tier-0 uses none of them.
 *   - Every wait is bounded in the VxD; a timed-out wait ends the probe.
 *
 * Command line: /nosetup skips v9x_mga_build_setup; /vram:N caps VRAM at N
 * MiB (2-8) when V9XBOOT.INI does not carry the BIOS's figure.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/mga_engine.h"

#include "../../src/chipsets/matrox/mga_engine.c"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

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
#define ALLOC_LOG_CONF 0x00000002ul
#define CM_LOCATE_DEVNODE_NORMAL 0x00000000ul
#define CR_SUCCESS 0x00000000ul
#define CR_NO_SUCH_DEVNODE 0x0000000dul
#define CR_NO_MORE_RES_DES 0x0000000ful
#define CR_FAILURE 0x00000013ul

#define MGA2D_MAGIC 0x44324d47ul
#define MGA2D_OP_MAX 512u
#define MGA2D_TIMEOUT 0xfffffffful

/* Op codes. Must match mga2d.asm. */
#define MGA2D_OP_REGION       1ul
#define MGA2D_OP_MMIO_WRITE32 2ul
#define MGA2D_OP_MMIO_READ32  3ul
#define MGA2D_OP_LFB_WRITE32  4ul
#define MGA2D_OP_LFB_READ32   5ul
#define MGA2D_OP_LFB_FILL32   6ul
#define MGA2D_OP_WAIT_IDLE    7ul
#define MGA2D_OP_WAIT_FIFO    8ul
#define MGA2D_OP_CACHE_FLUSH  9ul

#define MGA2D_MAPPED    0x00000002ul
#define MGA2D_RAN       0x00000004ul
#define MGA2D_TIMED_OUT 0x00000010ul

#define MGA2D_PCI_PREFIX "VEN_102B&DEV_0519"
#define MGA2D_RANGE_MAX 16u
/* MGABASE1 is 16 KiB (MGA-1064SG Table 3-4); the VxD maps BAR1 as 8 MiB. */
#define MGA2D_CONTROL_BYTES 0x00004000ul
#define MGA2D_BAR1_MAP_BYTES 0x00800000ul
#define MGA2D_MIB 0x00100000ul
#define MGA2D_VRAM_MIN_MIB 2ul
#define MGA2D_VRAM_MAX_MIB 8ul
/* V9XBOOT.INI's VbeController mem= counts 64 KiB blocks (VBE 4F00h). */
#define MGA2D_VBE_BLOCK_BYTES 0x00010000ul

/* The region: 1 MiB, 64 KiB-aligned, starting 1 MiB below the end of VRAM. */
#define MGA2D_REGION_BYTES 0x00100000ul
#define MGA2D_REGION_ALIGN 0x00010000ul
/* Region pitch: 1024 pixels is in the linearizer's list (PITCH, p.4-68) and
 * a multiple of 64, so every row is a legal YDSTORG at every depth. */
#define MGA2D_PITCH_PIXELS 1024ul

/*
 * The visible desktop's stride is not known to GDI. This card's BIOS pads
 * 800-wide modes to 960 pixels (docs\decisions\
 * 2026-10-09-mga2064w-tier0-first-boot.md), so width * bpp undercounts.
 * V9XBOOT.INI's Surface line gives the real stride when it describes the
 * current mode; otherwise every row is assumed to be this many pixels, the
 * linearizer's widest pitch.
 */
#define MGA2D_STRIDE_FALLBACK_PIXELS 2048ul

/* Each test owns a band of region rows; at 32 bpp the region holds 256. */
#define MGA2D_BAND_ROWS 36ul
#define MGA2D_SURFACE_ROW 2ul
#define MGA2D_GUARD 2ul
#define MGA2D_WIN_ROWS 40u
#define MGA2D_WIN_COLS 1024u

#define MGA2D_FILL_COLOR 0x5a3cc3a5ul

struct mga2d_op {
    DWORD code;
    DWORD a;
    DWORD b;
    DWORD c;
    DWORD result;
};

struct mga2d_request {
    DWORD count;
    struct mga2d_op ops[MGA2D_OP_MAX];
};

struct mga2d_result {
    DWORD magic;
    DWORD status;
    DWORD bar0;
    DWORD bar1;
    DWORD executed;
    DWORD refused;
    struct mga2d_op ops[MGA2D_OP_MAX];
};

struct mga2d_range {
    DWORD base;
    DWORD bytes;
};

/* A rectangle of the region: rows from row0, columns 0 to cols - 1. */
struct mga2d_window {
    DWORD row0;
    DWORD rows;
    DWORD cols;
};

/* One test. A fill draws at (dx, dy). A copy's source surface starts at the
 * band's surface row, its destination split rows further down; split 0 is
 * one surface, so the builder chooses the scan direction. */
struct mga2d_case {
    const char *name;
    DWORD is_copy;
    DWORD band;
    DWORD split;
    DWORD sx;
    DWORD sy;
    DWORD dx;
    DWORD dy;
    DWORD width;
    DWORD height;
};

typedef DWORD mga2d_image[MGA2D_WIN_ROWS][MGA2D_WIN_COLS];

static struct mga2d_request mga2d_request_buffer;
static struct mga2d_result mga2d_result_buffer;
static HANDLE mga2d_device = INVALID_HANDLE_VALUE;
static HANDLE mga2d_output = INVALID_HANDLE_VALUE;
static DWORD mga2d_failures = 0ul;
static int mga2d_engine_dead = 0;
static int mga2d_refused = 0;
static DWORD mga2d_bpp;
static DWORD mga2d_pixel_mask;
static DWORD mga2d_vram_bytes;
static DWORD mga2d_region_base;
static DWORD mga2d_pitch_bytes;
/* Allocated at start: wlink writes _BSS into the file, and three images as
 * statics made the EXE half a megabyte for the slow agent pushes. */
static DWORD (*mga2d_before)[MGA2D_WIN_COLS];
static DWORD (*mga2d_expected)[MGA2D_WIN_COLS];
static DWORD (*mga2d_actual)[MGA2D_WIN_COLS];

static const struct mga2d_case mga2d_cases[] = {
    /* 1: 13x7 fill at (3,2). */
    { "Fill",        0ul, 0ul,  0ul,  3ul,  2ul,  3ul,  2ul,  13ul, 7ul },
    /* 2: separate surfaces, forward in both axes. */
    { "CopyForward", 1ul, 1ul, 16ul,  2ul,  1ul, 40ul, 10ul,  17ul, 5ul },
    /* 3: one surface, destination lower: bottom to top. */
    { "CopyDown",    1ul, 2ul,  0ul,  4ul,  2ul,  7ul,  4ul,  20ul, 6ul },
    /* 4: one surface, same rows, destination right: right to left. */
    { "CopyRight",   1ul, 3ul,  0ul,  4ul,  2ul,  9ul,  2ul,  20ul, 4ul },
    /* 5: one surface, destination up and left: forward. */
    { "CopyUpLeft",  1ul, 4ul,  0ul,  8ul,  6ul,  5ul,  4ul,  20ul, 6ul },
    /* 6: long rows. */
    { "WideFill",    0ul, 5ul,  0ul,  2ul,  2ul,  2ul,  2ul, 600ul, 3ul },
    { "WideCopy",    1ul, 6ul,  8ul,  2ul,  2ul,  5ul,  2ul, 600ul, 3ul }
};

#define MGA2D_CASE_COUNT (sizeof(mga2d_cases) / sizeof(mga2d_cases[0]))

/* Byte loops: the tool links no C runtime, and CopyMemory and ZeroMemory
 * expand to memcpy and memset. */
static void mga2d_bytes(void *to, const void *from, DWORD count)
{
    BYTE *destination = (BYTE *)to;
    const BYTE *source = (const BYTE *)from;

    while (count-- != 0ul) {
        *destination++ = *source++;
    }
}

static void mga2d_zero(void *to, DWORD count)
{
    BYTE *destination = (BYTE *)to;

    while (count-- != 0ul) {
        *destination++ = 0u;
    }
}

static void mga2d_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15u];
    }
    text[digits] = '\0';
}

static void mga2d_decimal(char *text, DWORD value)
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

static void mga2d_signed(char *text, long value)
{
    if (value < 0l) {
        text[0] = '-';
        mga2d_decimal(text + 1, (DWORD)(0l - value));
        return;
    }
    mga2d_decimal(text, (DWORD)value);
}

static void mga2d_write(const char *key, const char *value)
{
    DWORD written;

    WriteFile(mga2d_output, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(mga2d_output, "=", 1u, &written, 0);
    WriteFile(mga2d_output, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(mga2d_output, "\r\n", 2u, &written, 0);
}

static void mga2d_write_hex(const char *key, DWORD value)
{
    char text[12];

    text[0] = '0';
    text[1] = 'x';
    mga2d_hex(text + 2, value, 8);
    mga2d_write(key, text);
}

static void mga2d_write_decimal(const char *key, DWORD value)
{
    char text[16];

    mga2d_decimal(text, value);
    mga2d_write(key, text);
}

static void mga2d_key(char *key, const char *prefix, const char *suffix)
{
    lstrcpyA(key, prefix);
    lstrcatA(key, suffix);
}

static void mga2d_refuse(const char *reason)
{
    mga2d_write("Result", "REFUSED");
    mga2d_write("Reason", reason);
    mga2d_refused = 1;
}

static int mga2d_starts_with_ci(const char *text, const char *prefix)
{
    char left;
    char right;

    while (*prefix != '\0') {
        left = *text++;
        right = *prefix++;
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

/* The text just after the first occurrence of name, or 0. */
static const char *mga2d_find(const char *text, const char *name)
{
    if (text == 0) {
        return 0;
    }
    while (*text != '\0') {
        if (mga2d_starts_with_ci(text, name)) {
            return text + lstrlenA(name);
        }
        ++text;
    }
    return 0;
}

/* Leading decimal digits; *ok is cleared when there are none. */
static DWORD mga2d_parse_decimal(const char *text, int *ok)
{
    DWORD value = 0ul;

    *ok = 0;
    if (text == 0) {
        return 0ul;
    }
    while (*text >= '0' && *text <= '9' && value < 100000000ul) {
        value = value * 10ul + (DWORD)(*text - '0');
        *ok = 1;
        ++text;
    }
    return value;
}

static DWORD mga2d_read_u32(const BYTE *data)
{
    return (DWORD)data[0] | ((DWORD)data[1] << 8) |
           ((DWORD)data[2] << 16) | ((DWORD)data[3] << 24);
}

static void mga2d_copy_text(char *destination, const char *source,
                            DWORD capacity)
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

static void mga2d_append_text(char *destination, const char *source,
                              DWORD capacity)
{
    DWORD used = 0u;

    while (used < capacity && destination[used] != '\0') {
        ++used;
    }
    if (used < capacity) {
        mga2d_copy_text(destination + used, source, capacity - used);
    }
}

/*
 * The wanted-th Enum\PCI instance of 102B:0519, counting from zero. Every
 * card ever fitted keeps its key, so the first match need not be the card in
 * the slot (sis6326_probe_win32.c, A8U4I5 2026-10-04); the caller tries each
 * until Config Manager locates a present devnode.
 */
static LONG mga2d_find_device(char *device_id, DWORD capacity, DWORD wanted)
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
            !mga2d_starts_with_ci(adapter, MGA2D_PCI_PREFIX)) {
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
            mga2d_copy_text(device_id, "PCI\\", capacity);
            mga2d_append_text(device_id, adapter, capacity);
            mga2d_append_text(device_id, "\\", capacity);
            mga2d_append_text(device_id, instance, capacity);
            RegCloseKey(pci_key);
            return ERROR_SUCCESS;
        }
    }
    RegCloseKey(pci_key);
    return ERROR_FILE_NOT_FOUND;
}

/*
 * Every memory range Config Manager allocated to the card. BAR sizing would
 * need a configuration-space write; the allocation already carries each
 * range's length, read-only.
 */
static CONFIGRET mga2d_ranges(struct mga2d_range *ranges, DWORD *count)
{
    typedef CONFIGRET (WINAPI *locate_fn)(PDEVINST, DEVINSTID_A, ULONG);
    typedef CONFIGRET (WINAPI *first_fn)(PLOG_CONF, DEVINST, ULONG);
    typedef CONFIGRET (WINAPI *next_fn)(PRES_DES, RES_DES, RESOURCEID,
                                        PRESOURCEID, ULONG);
    typedef CONFIGRET (WINAPI *free_res_fn)(RES_DES);
    typedef CONFIGRET (WINAPI *size_fn)(PULONG, RES_DES, ULONG);
    typedef CONFIGRET (WINAPI *data_fn)(RES_DES, PVOID, ULONG, ULONG);
    typedef CONFIGRET (WINAPI *free_log_fn)(LOG_CONF);
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
    /* CM_LOCATE_DEVNODE_NORMAL finds only present devnodes. */
    status = CR_NO_SUCH_DEVNODE;
    for (wanted = 0u; status != CR_SUCCESS; ++wanted) {
        if (mga2d_find_device(device_id, sizeof(device_id), wanted) !=
            ERROR_SUCCESS) {
            FreeLibrary(module);
            return CR_NO_SUCH_DEVNODE;
        }
        status = cm_locate(&device, device_id, CM_LOCATE_DEVNODE_NORMAL);
    }
    mga2d_write("DeviceInstance", device_id);

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
        DWORD end_low;

        status = cm_next(&next, current, ResType_Mem, 0, 0);
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
        /* MEM_DES: the allocated base and end as DWORDLONGs at +8, +16. */
        if (cm_size(&size, current, 0) != CR_SUCCESS || size < 24u ||
            size > sizeof(data) ||
            cm_data(current, data, size, 0) != CR_SUCCESS) {
            status = CR_FAILURE;
            break;
        }
        base_low = mga2d_read_u32(data + 8);
        end_low = mga2d_read_u32(data + 16);
        if (*count < MGA2D_RANGE_MAX && end_low >= base_low &&
            mga2d_read_u32(data + 12) == 0u &&
            mga2d_read_u32(data + 20) == 0u) {
            ranges[*count].base = base_low;
            ranges[*count].bytes = end_low - base_low + 1u;
            ++*count;
        }
    }
    if (current != (RES_DES)logical_config) {
        cm_free_res(current);
    }
    cm_free_log(logical_config);
    FreeLibrary(module);
    return status;
}

/* Bytes of the allocated range starting at base, or 0. */
static DWORD mga2d_range_bytes(const struct mga2d_range *ranges, DWORD count,
                               DWORD base)
{
    DWORD index;

    for (index = 0u; index < count; ++index) {
        if (ranges[index].base == base) {
            return ranges[index].bytes;
        }
    }
    return 0ul;
}

static void mga2d_begin(void)
{
    mga2d_request_buffer.count = 0ul;
}

static DWORD mga2d_add(DWORD code, DWORD a, DWORD b, DWORD c)
{
    struct mga2d_op *op;
    DWORD index = mga2d_request_buffer.count;

    if (index >= MGA2D_OP_MAX) {
        return MGA2D_OP_MAX;
    }
    op = &mga2d_request_buffer.ops[index];
    op->code = code;
    op->a = a;
    op->b = b;
    op->c = c;
    op->result = 0ul;
    ++mga2d_request_buffer.count;
    return index;
}

/* Runs the op list. Non-zero when every op executed. A timed-out wait marks
 * the engine dead: nothing more is sent to it. */
static int mga2d_run(void)
{
    DWORD returned = 0ul;

    if (!DeviceIoControl(mga2d_device, 1u, &mga2d_request_buffer,
                         sizeof(mga2d_request_buffer), &mga2d_result_buffer,
                         sizeof(mga2d_result_buffer), &returned, 0) ||
        returned != sizeof(mga2d_result_buffer) ||
        mga2d_result_buffer.magic != MGA2D_MAGIC) {
        return 0;
    }
    if ((mga2d_result_buffer.status & MGA2D_TIMED_OUT) != 0ul) {
        mga2d_engine_dead = 1;
    }
    return (mga2d_result_buffer.status & MGA2D_RAN) != 0ul &&
           mga2d_result_buffer.executed == mga2d_request_buffer.count;
}

static DWORD mga2d_value(DWORD index)
{
    return index < MGA2D_OP_MAX ? mga2d_result_buffer.ops[index].result : 0ul;
}

/*
 * The test pattern at a region pixel: each byte a different linear function
 * of x and y, so a pixel moved by any of the copies' offsets, or by one
 * column or row, reads differently. It never equals the fill colour, so a
 * filled pixel is always distinguishable from an untouched one.
 */
static DWORD mga2d_pattern(DWORD x, DWORD y)
{
    DWORD value;
    DWORD fill = MGA2D_FILL_COLOR & mga2d_pixel_mask;

    value = ((x * 7ul + y * 45ul + 3ul) & 0xfful) |
            (((x * 3ul + y * 101ul + 0x40ul) & 0xfful) << 8) |
            (((x + y * 17ul + 0x80ul) & 0xfful) << 16) |
            (((x * 11ul + y * 5ul + 0xc0ul) & 0xfful) << 24);
    value &= mga2d_pixel_mask;
    if (value == fill) {
        value ^= 1ul;
    }
    return value;
}

static DWORD mga2d_row_offset(DWORD region_row)
{
    return mga2d_region_base + region_row * mga2d_pitch_bytes;
}

/* The dword at index word of a window row, packed little-endian from the
 * image's pixels. */
static DWORD mga2d_pack(mga2d_image image, DWORD row, DWORD word)
{
    DWORD per_word = 4ul / mga2d_bpp;
    DWORD first = word * per_word;
    DWORD value = 0ul;
    DWORD index;

    for (index = 0ul; index < per_word; ++index) {
        value |= (image[row][first + index] & mga2d_pixel_mask) <<
                 (index * mga2d_bpp * 8ul);
    }
    return value;
}

static void mga2d_unpack(mga2d_image image, DWORD row, DWORD word,
                         DWORD value)
{
    DWORD per_word = 4ul / mga2d_bpp;
    DWORD first = word * per_word;
    DWORD index;

    for (index = 0ul; index < per_word; ++index) {
        image[row][first + index] =
            (value >> (index * mga2d_bpp * 8ul)) & mga2d_pixel_mask;
    }
}

/* Write the window from image, as many runs as it takes. */
static int mga2d_stream_write(const struct mga2d_window *window,
                              mga2d_image image)
{
    DWORD words = window->cols * mga2d_bpp / 4ul;
    DWORD row;
    DWORD word;

    mga2d_begin();
    for (row = 0ul; row < window->rows; ++row) {
        for (word = 0ul; word < words; ++word) {
            mga2d_add(MGA2D_OP_LFB_WRITE32,
                      mga2d_row_offset(window->row0 + row) + word * 4ul,
                      mga2d_pack(image, row, word), 0ul);
            if (mga2d_request_buffer.count == MGA2D_OP_MAX) {
                if (!mga2d_run()) {
                    return 0;
                }
                mga2d_begin();
            }
        }
    }
    return mga2d_request_buffer.count == 0ul || mga2d_run();
}

/* Read the window into image, as many runs as it takes. */
static int mga2d_stream_read(const struct mga2d_window *window,
                             mga2d_image image)
{
    DWORD words = window->cols * mga2d_bpp / 4ul;
    DWORD total = window->rows * words;
    DWORD done = 0ul;
    DWORD chunk;
    DWORD index;
    DWORD at;

    while (done < total) {
        chunk = total - done;
        if (chunk > MGA2D_OP_MAX) {
            chunk = MGA2D_OP_MAX;
        }
        mga2d_begin();
        for (index = 0ul; index < chunk; ++index) {
            at = done + index;
            mga2d_add(MGA2D_OP_LFB_READ32,
                      mga2d_row_offset(window->row0 + at / words) +
                      (at % words) * 4ul, 0ul, 0ul);
        }
        if (!mga2d_run()) {
            return 0;
        }
        for (index = 0ul; index < chunk; ++index) {
            at = done + index;
            mga2d_unpack(image, at / words, at % words, mga2d_value(index));
        }
        done += chunk;
    }
    return 1;
}

static void mga2d_report_writes(const char *name,
                                const struct v9x_mga_writes *writes)
{
    char key[48];
    char text[24];
    DWORD index;

    mga2d_key(key, name, "Writes");
    mga2d_write_decimal(key, writes->count);
    for (index = 0ul; index < writes->count; ++index) {
        mga2d_key(key, name, "Write");
        mga2d_decimal(key + lstrlenA(key), index);
        mga2d_hex(text, writes->offsets[index], 4);
        text[4] = '=';
        mga2d_hex(text + 5, writes->values[index], 8);
        mga2d_write(key, text);
    }
}

/*
 * The builder's writes behind a FIFO wait for all of them, then a bounded
 * idle wait and the read-cache flush, so the CPU readback that follows sees
 * what the engine wrote rather than dwords cached before it ran.
 */
static int mga2d_run_writes(const char *name,
                            const struct v9x_mga_writes *writes)
{
    char key[48];
    DWORD fifo;
    DWORD idle;
    DWORD flush;
    DWORD index;

    mga2d_begin();
    fifo = mga2d_add(MGA2D_OP_WAIT_FIFO, writes->count, 0ul, 0ul);
    for (index = 0ul; index < writes->count; ++index) {
        mga2d_add(MGA2D_OP_MMIO_WRITE32, writes->offsets[index],
                  writes->values[index], 0ul);
    }
    idle = mga2d_add(MGA2D_OP_WAIT_IDLE, 0ul, 0ul, 0ul);
    flush = mga2d_add(MGA2D_OP_CACHE_FLUSH, 0ul, 0ul, 0ul);
    if (!mga2d_run()) {
        mga2d_key(key, name, "VxdStatus");
        mga2d_write_hex(key, mga2d_result_buffer.status);
        mga2d_key(key, name, "StoppedAtOp");
        mga2d_write_decimal(key, mga2d_result_buffer.refused);
        mga2d_key(key, name, "FifoWaitReads");
        mga2d_write_hex(key, mga2d_value(fifo));
        mga2d_key(key, name, "IdleWaitReads");
        mga2d_write_hex(key, mga2d_value(idle));
        return 0;
    }
    mga2d_key(key, name, "FifoWaitReads");
    mga2d_write_decimal(key, mga2d_value(fifo));
    mga2d_key(key, name, "IdleWaitReads");
    mga2d_write_decimal(key, mga2d_value(idle));
    mga2d_key(key, name, "CrtcIndex");
    mga2d_write_hex(key, mga2d_value(flush));
    return 1;
}

/* Rows of the destination rectangle in which column col changed from the
 * prepared image, and in which it holds the expected value. */
static void mga2d_report_column(const char *name, const char *label,
                                const struct mga2d_window *window,
                                DWORD first_row, DWORD rows, DWORD col)
{
    char key[64];
    char text[32];
    DWORD row;
    DWORD changed = 0ul;
    DWORD row_index;

    for (row = 0ul; row < rows; ++row) {
        row_index = first_row - window->row0 + row;
        if (mga2d_actual[row_index][col] != mga2d_before[row_index][col]) {
            ++changed;
        }
    }
    mga2d_key(key, name, label);
    mga2d_decimal(text, changed);
    lstrcatA(text, "/");
    mga2d_decimal(text + lstrlenA(text), rows);
    mga2d_write(key, text);
}

static void mga2d_test(const struct mga2d_case *test)
{
    struct v9x_mga_fill fill;
    struct v9x_mga_copy copy;
    struct v9x_mga_writes writes;
    struct mga2d_window window;
    char key[64];
    char text[96];
    DWORD source_row = test->band * MGA2D_BAND_ROWS + MGA2D_SURFACE_ROW;
    DWORD destination_row = source_row + test->split;
    DWORD top;
    DWORD bottom;
    DWORD right;
    DWORD row;
    DWORD col;
    DWORD mismatches = 0ul;
    DWORD first_row = 0ul;
    DWORD first_col = 0ul;
    DWORD min_row = 0xfffffffful;
    DWORD max_row = 0ul;
    DWORD min_col = 0xfffffffful;
    DWORD max_col = 0ul;
    v9x_status status;

    /* The window: both rectangles and a guard of MGA2D_GUARD pixels on every
     * side, from column 0 to a dword boundary. */
    top = destination_row + test->dy;
    bottom = destination_row + test->dy + test->height - 1ul;
    right = test->dx + test->width - 1ul;
    if (test->is_copy) {
        if (source_row + test->sy < top) {
            top = source_row + test->sy;
        }
        if (source_row + test->sy + test->height - 1ul > bottom) {
            bottom = source_row + test->sy + test->height - 1ul;
        }
        if (test->sx + test->width - 1ul > right) {
            right = test->sx + test->width - 1ul;
        }
    }
    window.row0 = top - MGA2D_GUARD;
    window.rows = bottom + MGA2D_GUARD - window.row0 + 1ul;
    window.cols = (right + MGA2D_GUARD + 1ul + 3ul) & ~3ul;
    if (window.rows > MGA2D_WIN_ROWS || window.cols > MGA2D_WIN_COLS ||
        window.cols > MGA2D_PITCH_PIXELS ||
        (window.row0 + window.rows) * mga2d_pitch_bytes > MGA2D_REGION_BYTES) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "WINDOW-REFUSED");
        ++mga2d_failures;
        return;
    }

    mga2d_zero(&fill, sizeof(fill));
    mga2d_zero(&copy, sizeof(copy));
    if (test->is_copy) {
        copy.vram_bytes = mga2d_vram_bytes;
        copy.source_offset = mga2d_row_offset(source_row);
        copy.source_pitch_bytes = mga2d_pitch_bytes;
        copy.destination_offset = mga2d_row_offset(destination_row);
        copy.destination_pitch_bytes = mga2d_pitch_bytes;
        copy.bytes_per_pixel = mga2d_bpp;
        copy.source_left = test->sx;
        copy.source_top = test->sy;
        copy.destination_left = test->dx;
        copy.destination_top = test->dy;
        copy.width = test->width;
        copy.height = test->height;
        status = v9x_mga_build_copy(&copy, &writes);
    } else {
        fill.vram_bytes = mga2d_vram_bytes;
        fill.target_offset = mga2d_row_offset(destination_row);
        fill.pitch_bytes = mga2d_pitch_bytes;
        fill.bytes_per_pixel = mga2d_bpp;
        fill.left = test->dx;
        fill.top = test->dy;
        fill.width = test->width;
        fill.height = test->height;
        fill.color = MGA2D_FILL_COLOR & mga2d_pixel_mask;
        status = v9x_mga_build_fill(&fill, &writes);
    }
    if (status != V9X_STATUS_OK) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "BUILD-REFUSED");
        mga2d_key(key, test->name, "BuildStatus");
        mga2d_write_decimal(key, status);
        ++mga2d_failures;
        return;
    }
    mga2d_key(key, test->name, "SourceRow");
    mga2d_write_decimal(key, source_row);
    mga2d_key(key, test->name, "DestinationRow");
    mga2d_write_decimal(key, destination_row);
    if (!test->is_copy) {
        mga2d_key(key, test->name, "Color");
        mga2d_write_hex(key, fill.color);
    }
    mga2d_report_writes(test->name, &writes);

    /* Prepare, draw, read back. */
    for (row = 0ul; row < window.rows; ++row) {
        for (col = 0ul; col < window.cols; ++col) {
            mga2d_before[row][col] = mga2d_pattern(col, window.row0 + row);
        }
    }
    if (!mga2d_stream_write(&window, mga2d_before)) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "PREPARE-FAILED");
        ++mga2d_failures;
        return;
    }
    if (!mga2d_run_writes(test->name, &writes)) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, mga2d_engine_dead ? "ENGINE-TIMEOUT" : "RUN-FAILED");
        ++mga2d_failures;
        return;
    }
    if (!mga2d_stream_read(&window, mga2d_actual)) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "READBACK-FAILED");
        ++mga2d_failures;
        return;
    }

    /* The semantic intent: the rectangle exactly covered, with memmove
     * semantics for a copy, and nothing else touched. */
    mga2d_bytes(mga2d_expected, mga2d_before, sizeof(mga2d_image));
    for (row = 0ul; row < test->height; ++row) {
        for (col = 0ul; col < test->width; ++col) {
            mga2d_expected[destination_row + test->dy + row - window.row0]
                          [test->dx + col] = test->is_copy
                ? mga2d_before[source_row + test->sy + row - window.row0]
                              [test->sx + col]
                : fill.color;
        }
    }

    for (row = 0ul; row < window.rows; ++row) {
        for (col = 0ul; col < window.cols; ++col) {
            if (mga2d_actual[row][col] != mga2d_expected[row][col]) {
                if (mismatches == 0ul) {
                    first_row = row;
                    first_col = col;
                }
                ++mismatches;
            }
            if (mga2d_actual[row][col] != mga2d_before[row][col]) {
                if (row < min_row) {
                    min_row = row;
                }
                if (row > max_row) {
                    max_row = row;
                }
                if (col < min_col) {
                    min_col = col;
                }
                if (col > max_col) {
                    max_col = col;
                }
            }
        }
    }

    mga2d_key(key, test->name, "Result");
    mga2d_write(key, mismatches == 0ul ? "PASS" : "FAIL");
    if (mismatches != 0ul) {
        ++mga2d_failures;
    }
    mga2d_key(key, test->name, "Mismatches");
    mga2d_write_decimal(key, mismatches);
    if (mismatches != 0ul) {
        /* x from column 0, y from the destination surface's origin; the
         * source area of a two-surface copy is at negative y. */
        lstrcpyA(text, "x=");
        mga2d_decimal(text + lstrlenA(text), first_col);
        lstrcatA(text, " y=");
        mga2d_signed(text + lstrlenA(text),
                     (long)(window.row0 + first_row) - (long)destination_row);
        lstrcatA(text, " got=0x");
        mga2d_hex(text + lstrlenA(text), mga2d_actual[first_row][first_col],
                  (int)(mga2d_bpp * 2ul));
        lstrcatA(text, " want=0x");
        mga2d_hex(text + lstrlenA(text), mga2d_expected[first_row][first_col],
                  (int)(mga2d_bpp * 2ul));
        mga2d_key(key, test->name, "FirstMismatch");
        mga2d_write(key, text);
    }
    mga2d_key(key, test->name, "Changed");
    if (min_row == 0xfffffffful) {
        mga2d_write(key, "none");
    } else {
        lstrcpyA(text, "x=");
        mga2d_decimal(text + lstrlenA(text), min_col);
        lstrcatA(text, "-");
        mga2d_decimal(text + lstrlenA(text), max_col);
        lstrcatA(text, " y=");
        mga2d_signed(text + lstrlenA(text),
                     (long)(window.row0 + min_row) - (long)destination_row);
        lstrcatA(text, "-");
        mga2d_signed(text + lstrlenA(text),
                     (long)(window.row0 + max_row) - (long)destination_row);
        mga2d_write(key, text);
    }

    /* The right-edge question: was the column just right of the rectangle
     * written, and was its own last column? Counted over the rectangle's
     * rows as changed/total. */
    mga2d_report_column(test->name, "ColumnRightOfRectWritten", &window,
                        destination_row + test->dy, test->height,
                        test->dx + test->width);
    mga2d_report_column(test->name, "LastColumnWritten", &window,
                        destination_row + test->dy, test->height,
                        test->dx + test->width - 1ul);
}

/* FIFOSTATUS and STATUS, read through the VxD's two readable offsets. */
static int mga2d_report_state(const char *prefix)
{
    char key[48];
    DWORD fifo;
    DWORD status;

    mga2d_begin();
    fifo = mga2d_add(MGA2D_OP_MMIO_READ32, V9X_MGA_FIFOSTATUS, 0ul, 0ul);
    status = mga2d_add(MGA2D_OP_MMIO_READ32, V9X_MGA_STATUS, 0ul, 0ul);
    if (!mga2d_run()) {
        return 0;
    }
    mga2d_key(key, prefix, "FifoStatus");
    mga2d_write_hex(key, mga2d_value(fifo));
    mga2d_key(key, prefix, "Status");
    mga2d_write_hex(key, mga2d_value(status));
    return mga2d_value(status) != 0xfffffffful;
}

/* The VBE memory figure the driver recorded at boot, in bytes, or 0. */
static DWORD mga2d_vbe_vram_bytes(void)
{
    char text[128];
    DWORD blocks;
    int ok;

    GetPrivateProfileStringA("Velocity9x", "VbeController", "", text,
                             sizeof(text), V9X_DIAG_BOOT_INI);
    blocks = mga2d_parse_decimal(mga2d_find(text, "mem="), &ok);
    if (!ok || blocks == 0ul ||
        blocks > MGA2D_BAR1_MAP_BYTES / MGA2D_VBE_BLOCK_BYTES) {
        return 0ul;
    }
    return blocks * MGA2D_VBE_BLOCK_BYTES;
}

/* The desktop stride from V9XBOOT.INI's Surface line when it describes the
 * current mode, or 0. */
static DWORD mga2d_surface_pitch(DWORD width, DWORD height, DWORD bits)
{
    char text[160];
    DWORD pitch;
    int ok;

    GetPrivateProfileStringA("Velocity9x", "Surface", "", text, sizeof(text),
                             V9X_DIAG_BOOT_INI);
    if (mga2d_parse_decimal(mga2d_find(text, " bpp="), &ok) != bits || !ok ||
        mga2d_parse_decimal(mga2d_find(text, " w="), &ok) != width || !ok ||
        mga2d_parse_decimal(mga2d_find(text, " h="), &ok) != height || !ok) {
        return 0ul;
    }
    pitch = mga2d_parse_decimal(mga2d_find(text, "pitch="), &ok);
    return ok ? pitch : 0ul;
}

void WINAPI V9xMga2dProbeEntry(void)
{
    HDC display;
    const char *command_line;
    struct mga2d_range ranges[MGA2D_RANGE_MAX];
    struct v9x_mga_writes setup;
    DWORD range_count = 0ul;
    DWORD width = 0ul;
    DWORD height = 0ul;
    DWORD bits = 0ul;
    DWORD bar0;
    DWORD bar1;
    DWORD bar0_bytes;
    DWORD bar1_bytes;
    DWORD vbe_bytes;
    DWORD option_mib;
    DWORD stride;
    DWORD desktop_guard;
    DWORD index;
    int no_setup;
    int have_option;
    CONFIGRET cm_status;
    static const char header[] = "[Mga2dProbe]\r\n";
    DWORD written;
    char key[32];

    display = GetDC(0);
    if (display != 0) {
        width = (DWORD)GetDeviceCaps(display, HORZRES);
        height = (DWORD)GetDeviceCaps(display, VERTRES);
        bits = (DWORD)(GetDeviceCaps(display, BITSPIXEL) *
                       GetDeviceCaps(display, PLANES));
        ReleaseDC(0, display);
    }
    command_line = GetCommandLineA();
    no_setup = mga2d_find(command_line, "/nosetup") != 0;
    option_mib = mga2d_parse_decimal(mga2d_find(command_line, "/vram:"),
                                     &have_option);

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    mga2d_output = CreateFileA(V9X_DIAG_MGA2D_TXT, GENERIC_WRITE,
                               FILE_SHARE_READ, 0, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, 0);
    if (mga2d_output == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    WriteFile(mga2d_output, header, (DWORD)lstrlenA(header), &written, 0);
    mga2d_write("Build", V9X_BUILD_ID);
    mga2d_write("NoSetup", no_setup ? "1" : "0");
    if (have_option) {
        mga2d_write_decimal("VramOptionMiB", option_mib);
    }
    mga2d_write_decimal("DesktopWidth", width);
    mga2d_write_decimal("DesktopHeight", height);
    mga2d_write_decimal("DesktopBpp", bits);

    /* The engine works at the desktop's depth; 24 bpp has no pwidth on this
     * chip and 4 bpp is planar. */
    if (bits != 8ul && bits != 16ul && bits != 32ul) {
        mga2d_refuse("desktop-depth");
        ExitProcess(5u);
    }
    mga2d_bpp = bits / 8ul;
    mga2d_pixel_mask = bits == 32ul ? 0xfffffffful : (1ul << bits) - 1ul;
    mga2d_pitch_bytes = MGA2D_PITCH_PIXELS * mga2d_bpp;
    if (have_option &&
        (option_mib < MGA2D_VRAM_MIN_MIB || option_mib > MGA2D_VRAM_MAX_MIB)) {
        mga2d_refuse("vram-option-out-of-range");
        ExitProcess(5u);
    }
    mga2d_before = (DWORD (*)[MGA2D_WIN_COLS])VirtualAlloc(
        0, sizeof(mga2d_image), MEM_COMMIT, PAGE_READWRITE);
    mga2d_expected = (DWORD (*)[MGA2D_WIN_COLS])VirtualAlloc(
        0, sizeof(mga2d_image), MEM_COMMIT, PAGE_READWRITE);
    mga2d_actual = (DWORD (*)[MGA2D_WIN_COLS])VirtualAlloc(
        0, sizeof(mga2d_image), MEM_COMMIT, PAGE_READWRITE);
    if (mga2d_before == 0 || mga2d_expected == 0 || mga2d_actual == 0) {
        mga2d_refuse("out-of-memory");
        ExitProcess(5u);
    }

    cm_status = mga2d_ranges(ranges, &range_count);
    if (cm_status != CR_SUCCESS) {
        mga2d_write_hex("ConfigManagerError", cm_status);
        mga2d_refuse("config-manager");
        ExitProcess(5u);
    }
    mga2d_write_decimal("MemoryRanges", range_count);
    for (index = 0ul; index < range_count; ++index) {
        lstrcpyA(key, "Range");
        mga2d_decimal(key + lstrlenA(key), index);
        lstrcatA(key, "Base");
        mga2d_write_hex(key, ranges[index].base);
        lstrcpyA(key, "Range");
        mga2d_decimal(key + lstrlenA(key), index);
        lstrcatA(key, "Bytes");
        mga2d_write_hex(key, ranges[index].bytes);
    }

    mga2d_device = CreateFileA("\\\\.\\MGA2D.VXD", 0, 0, 0, CREATE_NEW,
                               FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (mga2d_device == INVALID_HANDLE_VALUE) {
        mga2d_refuse("no-vxd");
        ExitProcess(2u);
    }

    /* An empty list: the VxD finds the card and maps it, nothing more. */
    mga2d_begin();
    if (!mga2d_run()) {
        mga2d_write_hex("VxdStatus", mga2d_result_buffer.status);
        mga2d_write_hex("Bar0Raw", mga2d_result_buffer.bar0);
        mga2d_write_hex("Bar1Raw", mga2d_result_buffer.bar1);
        if (mga2d_result_buffer.magic != MGA2D_MAGIC) {
            mga2d_refuse("vxd-call-failed");
        } else if ((mga2d_result_buffer.status & 1ul) == 0ul) {
            mga2d_refuse("no-102b-0519-on-pci");
        } else {
            mga2d_refuse("bar-unmappable");
        }
        goto close;
    }
    bar0 = mga2d_result_buffer.bar0 & 0xfffffff0ul;
    bar1 = mga2d_result_buffer.bar1 & 0xfffffff0ul;
    mga2d_write_hex("Bar0", bar0);
    mga2d_write_hex("Bar1", bar1);

    /* The live BARs must be the ranges Configuration Manager allocated, and
     * BAR0 must be MGABASE1's 16 KiB. */
    bar0_bytes = mga2d_range_bytes(ranges, range_count, bar0);
    bar1_bytes = mga2d_range_bytes(ranges, range_count, bar1);
    mga2d_write_hex("Bar0Bytes", bar0_bytes);
    mga2d_write_hex("Bar1Bytes", bar1_bytes);
    if (bar0_bytes != MGA2D_CONTROL_BYTES) {
        mga2d_refuse("bar0-range-not-16k");
        goto close;
    }
    if (bar1_bytes < MGA2D_VRAM_MIN_MIB * MGA2D_MIB ||
        bar1_bytes > MGA2D_BAR1_MAP_BYTES) {
        mga2d_refuse("bar1-range");
        goto close;
    }

    /*
     * VRAM: BAR1's length is the aperture, not the memory - 86Box gives a
     * 4 MiB card the same 8 MiB BAR and wraps addresses into it - so it is
     * capped by the BIOS's figure from V9XBOOT.INI, or by /vram:N. With
     * neither, the region could alias into the desktop, so the probe stops.
     */
    mga2d_vram_bytes = bar1_bytes;
    vbe_bytes = mga2d_vbe_vram_bytes();
    mga2d_write_hex("VbeVramBytes", vbe_bytes);
    if (vbe_bytes == 0ul && !have_option) {
        mga2d_refuse("vram-unknown-pass-vram-option");
        goto close;
    }
    if (vbe_bytes != 0ul && vbe_bytes < mga2d_vram_bytes) {
        mga2d_vram_bytes = vbe_bytes;
    }
    if (have_option && option_mib * MGA2D_MIB < mga2d_vram_bytes) {
        mga2d_vram_bytes = option_mib * MGA2D_MIB;
    }
    mga2d_write_hex("VramBytes", mga2d_vram_bytes);
    if (mga2d_vram_bytes < MGA2D_VRAM_MIN_MIB * MGA2D_MIB) {
        mga2d_refuse("vram-too-small");
        goto close;
    }
    mga2d_region_base = (mga2d_vram_bytes - MGA2D_REGION_BYTES) &
                        ~(MGA2D_REGION_ALIGN - 1ul);

    /* The desktop must end at or before the region. */
    stride = mga2d_surface_pitch(width, height, bits);
    mga2d_write_hex("DesktopBytes", width * height * mga2d_bpp);
    if (stride != 0ul) {
        mga2d_write("StrideSource", "V9XBOOT.INI");
    } else {
        stride = (width > MGA2D_STRIDE_FALLBACK_PIXELS
                  ? width : MGA2D_STRIDE_FALLBACK_PIXELS) * mga2d_bpp;
        mga2d_write("StrideSource", "fallback-2048-pixels");
    }
    if (stride < width * mga2d_bpp) {
        stride = width * mga2d_bpp;
    }
    desktop_guard = stride * height;
    mga2d_write_decimal("DesktopStrideBytes", stride);
    mga2d_write_hex("DesktopGuardBytes", desktop_guard);
    mga2d_write_hex("RegionBase", mga2d_region_base);
    mga2d_write_hex("RegionBytes", MGA2D_REGION_BYTES);
    mga2d_write_decimal("RegionPitchBytes", mga2d_pitch_bytes);
    if (desktop_guard > mga2d_region_base) {
        mga2d_refuse("region-overlaps-desktop");
        goto close;
    }

    mga2d_begin();
    mga2d_add(MGA2D_OP_REGION, mga2d_region_base, MGA2D_REGION_BYTES, 0ul);
    if (!mga2d_run()) {
        mga2d_write_hex("VxdStatus", mga2d_result_buffer.status);
        mga2d_refuse("vxd-refused-region");
        goto close;
    }
    if (!mga2d_report_state("Before")) {
        mga2d_refuse("engine-not-responding");
        goto close;
    }

    if (no_setup) {
        mga2d_write("Setup", "skipped");
    } else {
        if (v9x_mga_build_setup(mga2d_bpp, &setup) != V9X_STATUS_OK) {
            mga2d_refuse("setup-build");
            goto close;
        }
        mga2d_report_writes("Setup", &setup);
        if (!mga2d_run_writes("Setup", &setup)) {
            mga2d_write("Result", "FAIL");
            mga2d_write("Reason", mga2d_engine_dead ? "setup-engine-timeout"
                                                    : "setup-run-failed");
            goto close;
        }
        mga2d_write("Setup", "emitted");
    }

    for (index = 0ul; index < MGA2D_CASE_COUNT; ++index) {
        mga2d_test(&mga2d_cases[index]);
        if (mga2d_engine_dead) {
            break;
        }
    }

    if (!mga2d_engine_dead) {
        mga2d_report_state("After");
    }
    mga2d_write_decimal("FailedCases", mga2d_failures);
    if (mga2d_engine_dead) {
        mga2d_write("Result", "FAIL");
        mga2d_write("Reason", "engine-timeout");
    } else if (mga2d_failures != 0ul) {
        mga2d_write("Result", "FAIL");
        mga2d_write("Reason", "case-mismatch");
    } else {
        mga2d_write("Result", "PASS");
    }

close:
    CloseHandle(mga2d_device);
    CloseHandle(mga2d_output);
    if (mga2d_refused) {
        ExitProcess(3u);
    }
    ExitProcess(mga2d_failures == 0ul && !mga2d_engine_dead ? 0u : 1u);
}
