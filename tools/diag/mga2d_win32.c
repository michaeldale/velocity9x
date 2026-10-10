/*
 * MGA2D.EXE: the Matrox MGA-2064W / MGA-2164W drawing engine write probe.
 *
 * With /tri it draws trapezoids instead (phase 1 of
 * docs\plans\matrox-mga2164w-hardware-3d.md): flat ones with sloped edges,
 * and Gouraud ones at 32 bpp, each compared with src\chipsets\matrox\
 * mga_3d.c's model of the engine. A Gouraud case is compared twice, once
 * per hypothesis for the per-row colour step (V9X_MGA3D_FOLD_EDGE and
 * _NONE), and the report says which, if either, the card matches. Every
 * case also writes its coverage as text rows, '#' for a changed pixel.
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
 *   - Run under Velocity9x at 8, 16 or 32 bpp. The engine's depth follows
 *     the desktop's. The driver uses the engine too, so the probe's setup
 *     (MACCESS, PLNWT, clip) is what the HAL writes before its own
 *     operations anyway.
 *   - The card must be PCI 102B:0519 or 102B:051B with Configuration
 *     Manager's range for the control aperture exactly 16 KiB (MGABASE1) at
 *     the base the VxD reads: config 10h on the 2064W, 14h on the 2164W.
 *   - Engine output and CPU test writes stay in a 1 MiB region starting 1 MiB
 *     below the end of VRAM, which must lie beyond the visible desktop.
 *   - Engine state written by the setup (MACCESS, PLNWT, the clip window) is
 *     not restored: the drawing registers are not read back through this
 *     VxD, and tier-0 uses none of them.
 *   - Every wait is bounded in the VxD; a timed-out wait ends the probe.
 *
 * Command line: /nosetup skips v9x_mga_build_setup; /vram:N (2-16) sets VRAM
 * to N MiB in place of the BIOS's figure from V9XBOOT.INI, which places the
 * region 1 MiB below N; /tri draws trapezoids instead of fills and copies,
 * /depth Gouraud trapezoids with a Z buffer (32 bpp only), and /tex
 * textured trapezoids (32 bpp, 2164W only).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/mga_engine.h"
#include "velocity9x/mga_3d.h"

#include "../../src/chipsets/matrox/mga_engine.c"
#include "../../src/chipsets/matrox/mga_3d.c"

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

#define MGA2D_PCI_PREFIX_2064W "VEN_102B&DEV_0519"
#define MGA2D_PCI_PREFIX_2164W "VEN_102B&DEV_051B"
#define MGA2D_PCI_ID_2164W 0x051b102bul
#define MGA2D_RANGE_MAX 16u
/* MGABASE1 is 16 KiB (MGA-1064SG Table 3-4). The VxD maps the framebuffer
 * as 8 MiB on the 2064W and 16 MiB on the 2164W. */
#define MGA2D_CONTROL_BYTES 0x00004000ul
#define MGA2D_FB_MAP_BYTES_2064W 0x00800000ul
#define MGA2D_FB_MAP_BYTES_2164W 0x01000000ul
#define MGA2D_MIB 0x00100000ul
#define MGA2D_VRAM_MIN_MIB 2ul
#define MGA2D_VRAM_MAX_MIB 16ul
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
    DWORD pci_id;
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
static int mga2d_is_2164w = 0;
/* The per-mode state, built once; /tri sends it ahead of every case. */
static struct v9x_mga_writes mga2d_setup;
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
 * The wanted-th Enum\PCI instance of 102B:0519 or 051B, counting from zero. Every
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
            (!mga2d_starts_with_ci(adapter, MGA2D_PCI_PREFIX_2064W) &&
             !mga2d_starts_with_ci(adapter, MGA2D_PCI_PREFIX_2164W))) {
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

static void mga2d_report_list(const char *name, const v9x_u32 *offsets,
                              const v9x_u32 *values, DWORD count)
{
    char key[48];
    char text[24];
    DWORD index;

    mga2d_key(key, name, "Writes");
    mga2d_write_decimal(key, count);
    for (index = 0ul; index < count; ++index) {
        mga2d_key(key, name, "Write");
        mga2d_decimal(key + lstrlenA(key), index);
        mga2d_hex(text, offsets[index], 4);
        text[4] = '=';
        mga2d_hex(text + 5, values[index], 8);
        mga2d_write(key, text);
    }
}

static void mga2d_report_writes(const char *name,
                                const struct v9x_mga_writes *writes)
{
    mga2d_report_list(name, writes->offsets, writes->values, writes->count);
}

/*
 * The builder's writes behind a FIFO wait for all of them, then a bounded
 * idle wait and the read-cache flush, so the CPU readback that follows sees
 * what the engine wrote rather than dwords cached before it ran. The VxD
 * accepts a FIFO wait of at most 32 entries, the 2064W's depth, so a longer
 * list waits again before each further 32; the first wait is the one
 * reported.
 */
#define MGA2D_FIFO_WAIT_MAX 32ul

static int mga2d_run_list(const char *name, const v9x_u32 *offsets,
                          const v9x_u32 *values, DWORD count)
{
    char key[48];
    DWORD fifo = 0ul;
    DWORD idle;
    DWORD flush;
    DWORD index;
    DWORD block;

    mga2d_begin();
    for (index = 0ul; index < count; ++index) {
        if ((index % MGA2D_FIFO_WAIT_MAX) == 0ul) {
            block = count - index;
            if (block > MGA2D_FIFO_WAIT_MAX) {
                block = MGA2D_FIFO_WAIT_MAX;
            }
            if (index == 0ul) {
                fifo = mga2d_add(MGA2D_OP_WAIT_FIFO, block, 0ul, 0ul);
            } else {
                mga2d_add(MGA2D_OP_WAIT_FIFO, block, 0ul, 0ul);
            }
        }
        mga2d_add(MGA2D_OP_MMIO_WRITE32, offsets[index], values[index], 0ul);
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

static int mga2d_run_writes(const char *name,
                            const struct v9x_mga_writes *writes)
{
    return mga2d_run_list(name, writes->offsets, writes->values,
                          writes->count);
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

/*
 * /tri: one trapezoid per case, each in its own band of region rows. Edges
 * are (x, dx, dy) as v9x_mga3d_edge; colours are signed 9.15 start, x step
 * and y step. The window is MGA2D_TRI_COLS wide from column 0.
 */
#define MGA2D_TRI_BAND_ROWS 20ul
#define MGA2D_TRI_COLS 64ul
#define MGA2D_LEVEL(n) ((long)(n) * V9X_MGA3D_COLOR_ONE)
/* FCOL's alpha byte, stored with every Gouraud pixel at 32 bpp. */
#define MGA2D_TRI_ALPHA 0x5a000000ul

struct mga2d_tri_case {
    const char *name;
    DWORD band;
    DWORD shade;
    long lx;
    long ldx;
    long ldy;
    long rx;
    long rdx;
    long rdy;
    DWORD length;
    long red[3];
    long green[3];
    long blue[3];
};

static const struct mga2d_tri_case mga2d_tri_cases[] = {
    /* 45-degree edges from a one-pixel top row. */
    { "TriApex", 0ul, V9X_MGA3D_SHADE_FLAT, 20L, -8L, 8L, 21L, 8L, 8L, 8ul,
      { 0L, 0L, 0L }, { 0L, 0L, 0L }, { 0L, 0L, 0L } },
    /* The same from a zero-width top row: is the first row empty? */
    { "TriPoint", 1ul, V9X_MGA3D_SHADE_FLAT, 20L, -8L, 8L, 20L, 8L, 8L, 8ul,
      { 0L, 0L, 0L }, { 0L, 0L, 0L }, { 0L, 0L, 0L } },
    /* Shallow edges of different slopes: where each one steps. */
    { "TriShallow", 2ul, V9X_MGA3D_SHADE_FLAT, 20L, -3L, 8L, 22L, 5L, 8L,
      8ul, { 0L, 0L, 0L }, { 0L, 0L, 0L }, { 0L, 0L, 0L } },
    /* Edges closing on each other, the bottom half of a triangle. */
    { "TriConverge", 3ul, V9X_MGA3D_SHADE_FLAT, 10L, 6L, 8L, 40L, -9L, 8L,
      8ul, { 0L, 0L, 0L }, { 0L, 0L, 0L }, { 0L, 0L, 0L } },
    /* Edges defined over 16 rows, drawn for 8: the long edge of a
     * triangle split in two. */
    { "TriLongEdge", 4ul, V9X_MGA3D_SHADE_FLAT, 20L, -8L, 16L, 21L, 8L, 16L,
      8ul, { 0L, 0L, 0L }, { 0L, 0L, 0L }, { 0L, 0L, 0L } },
    /* Left edge moving right one column a row, red rising two levels a
     * pixel, blue four a row: the fold hypotheses differ by two levels of
     * red per row at the edge. */
    { "GouraudFoldRight", 5ul, V9X_MGA3D_SHADE_GOURAUD,
      4L, 8L, 8L, 40L, 0L, 8L, 8ul,
      { MGA2D_LEVEL(0x20), MGA2D_LEVEL(2), 0L },
      { MGA2D_LEVEL(0x80), 0L, 0L },
      { MGA2D_LEVEL(0x10), 0L, MGA2D_LEVEL(4) } },
    /* The same with the edge moving left. */
    { "GouraudFoldLeft", 6ul, V9X_MGA3D_SHADE_GOURAUD,
      20L, -8L, 8L, 40L, 0L, 8L, 8ul,
      { MGA2D_LEVEL(0x20), MGA2D_LEVEL(3), 0L },
      { MGA2D_LEVEL(0x80), 0L, 0L },
      { MGA2D_LEVEL(0x10), 0L, MGA2D_LEVEL(4) } },
    /* Vertical edges, fractional steps: how the 15 fraction bits
     * accumulate and truncate, and a falling channel. */
    { "GouraudFraction", 7ul, V9X_MGA3D_SHADE_GOURAUD,
      8L, 0L, 8L, 40L, 0L, 8L, 8ul,
      { MGA2D_LEVEL(0x40) + 0x4000L, 0x2000L, 0x1000L },
      { 0L, 0x6000L, 0L },
      { MGA2D_LEVEL(0xff), -MGA2D_LEVEL(1), 0L } }
};

#define MGA2D_TRI_CASE_COUNT (sizeof(mga2d_tri_cases) / sizeof(mga2d_tri_cases[0]))

struct mga2d_plot_target {
    DWORD first_row;
    DWORD window_row0;
    DWORD window_rows;
};

static void mga2d_tri_plot(void *context, v9x_s32 x, v9x_u32 row,
                           v9x_u32 pixel)
{
    struct mga2d_plot_target *target = (struct mga2d_plot_target *)context;
    DWORD image_row = target->first_row + row - target->window_row0;

    if (x < 0L || (DWORD)x >= MGA2D_TRI_COLS ||
        image_row >= target->window_rows) {
        return;
    }
    mga2d_expected[image_row][x] = pixel & mga2d_pixel_mask;
}

/* Mismatches between the card and the model under one fold hypothesis;
 * the first one's place in *first_row, *first_col. */
static DWORD mga2d_tri_compare(const struct v9x_mga3d_trap *trap,
                               DWORD fold, struct mga2d_plot_target *target,
                               const struct mga2d_window *window,
                               DWORD *first_row, DWORD *first_col)
{
    DWORD row;
    DWORD col;
    DWORD mismatches = 0ul;

    mga2d_bytes(mga2d_expected, mga2d_before, sizeof(mga2d_image));
    if (v9x_mga3d_model_trap(trap, fold, mga2d_tri_plot, target, 0) !=
        V9X_STATUS_OK) {
        return 0xfffffffful;
    }
    for (row = 0ul; row < window->rows; ++row) {
        for (col = 0ul; col < window->cols; ++col) {
            if (mga2d_actual[row][col] != mga2d_expected[row][col]) {
                if (mismatches == 0ul) {
                    *first_row = row;
                    *first_col = col;
                }
                ++mismatches;
            }
        }
    }
    return mismatches;
}

static void mga2d_tri_report_mismatch(const char *name, const char *label,
                                      DWORD mismatches, DWORD first_row,
                                      DWORD first_col,
                                      const struct mga2d_window *window,
                                      DWORD destination_row)
{
    char key[64];
    char text[96];

    mga2d_key(key, name, label);
    mga2d_write_decimal(key, mismatches);
    if (mismatches == 0ul || mismatches == 0xfffffffful) {
        return;
    }
    lstrcpyA(text, "x=");
    mga2d_decimal(text + lstrlenA(text), first_col);
    lstrcatA(text, " y=");
    mga2d_signed(text + lstrlenA(text),
                 (long)(window->row0 + first_row) - (long)destination_row);
    lstrcatA(text, " got=0x");
    mga2d_hex(text + lstrlenA(text), mga2d_actual[first_row][first_col],
              (int)(mga2d_bpp * 2ul));
    lstrcatA(text, " want=0x");
    mga2d_hex(text + lstrlenA(text), mga2d_expected[first_row][first_col],
              (int)(mga2d_bpp * 2ul));
    mga2d_key(key, name, label);
    lstrcatA(key, "First");
    mga2d_write(key, text);
}

static void mga2d_tri_test(const struct mga2d_tri_case *test)
{
    struct v9x_mga3d_trap trap;
    struct v9x_mga3d_writes writes;
    struct mga2d_window window;
    struct mga2d_plot_target target;
    char key[64];
    char text[MGA2D_TRI_COLS + 32ul];
    DWORD destination_row = test->band * MGA2D_TRI_BAND_ROWS + MGA2D_GUARD;
    DWORD row;
    DWORD col;
    DWORD first_row = 0ul;
    DWORD first_col = 0ul;
    DWORD edge_mismatches;
    DWORD none_mismatches = 0xfffffffful;
    DWORD index;
    DWORD count;
    v9x_u32 offsets[V9X_MGA_MAX_WRITES + V9X_MGA3D_MAX_WRITES];
    v9x_u32 values[V9X_MGA_MAX_WRITES + V9X_MGA3D_MAX_WRITES];
    v9x_status status;

    if (test->shade == V9X_MGA3D_SHADE_GOURAUD && mga2d_bpp != 4ul) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "SKIPPED-DEPTH");
        return;
    }
    window.row0 = destination_row - MGA2D_GUARD;
    window.rows = test->length + 2ul * MGA2D_GUARD;
    window.cols = MGA2D_TRI_COLS;
    if ((window.row0 + window.rows) * mga2d_pitch_bytes > MGA2D_REGION_BYTES) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "WINDOW-REFUSED");
        ++mga2d_failures;
        return;
    }

    mga2d_zero(&trap, sizeof(trap));
    trap.vram_bytes = mga2d_vram_bytes;
    trap.target_offset = mga2d_row_offset(destination_row);
    trap.pitch_bytes = mga2d_pitch_bytes;
    trap.bytes_per_pixel = mga2d_bpp;
    trap.top = 0ul;
    trap.length = test->length;
    trap.left.x = test->lx;
    trap.left.dx = test->ldx;
    trap.left.dy = test->ldy;
    trap.right.x = test->rx;
    trap.right.dx = test->rdx;
    trap.right.dy = test->rdy;
    trap.shade = test->shade;
    if (test->shade == V9X_MGA3D_SHADE_GOURAUD) {
        trap.color = MGA2D_TRI_ALPHA;
        for (index = 0ul; index < 3ul; ++index) {
            trap.red[index] = test->red[index];
            trap.green[index] = test->green[index];
            trap.blue[index] = test->blue[index];
        }
    } else {
        trap.color = MGA2D_FILL_COLOR & mga2d_pixel_mask;
    }
    status = v9x_mga3d_build_trap(&trap, &writes);
    if (status != V9X_STATUS_OK) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "BUILD-REFUSED");
        mga2d_key(key, test->name, "BuildStatus");
        mga2d_write_decimal(key, status);
        ++mga2d_failures;
        return;
    }
    mga2d_key(key, test->name, "DestinationRow");
    mga2d_write_decimal(key, destination_row);
    mga2d_report_list(test->name, writes.offsets, writes.values,
                      writes.count);

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
    /* The setup again in front of the trapezoid, in the same VxD call: the
     * driver draws GDI and DirectDraw on this engine between the probe's
     * calls and may leave its own clip or pixel width behind. */
    count = 0ul;
    for (index = 0ul; index < mga2d_setup.count; ++index) {
        offsets[count] = mga2d_setup.offsets[index];
        values[count] = mga2d_setup.values[index];
        ++count;
    }
    for (index = 0ul; index < writes.count; ++index) {
        offsets[count] = writes.offsets[index];
        values[count] = writes.values[index];
        ++count;
    }
    if (!mga2d_run_list(test->name, offsets, values, count)) {
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

    /* Coverage, row by row from the window's first row: '#' changed. */
    for (row = 0ul; row < window.rows; ++row) {
        for (col = 0ul; col < window.cols; ++col) {
            text[col] = mga2d_actual[row][col] != mga2d_before[row][col]
                ? '#' : '.';
        }
        text[window.cols] = '\0';
        lstrcpyA(key, test->name);
        lstrcatA(key, "Row");
        mga2d_signed(key + lstrlenA(key),
                     (long)(window.row0 + row) - (long)destination_row);
        mga2d_write(key, text);
    }

    target.first_row = destination_row;
    target.window_row0 = window.row0;
    target.window_rows = window.rows;
    edge_mismatches = mga2d_tri_compare(&trap, V9X_MGA3D_FOLD_EDGE, &target,
                                        &window, &first_row, &first_col);
    mga2d_tri_report_mismatch(test->name, "MismatchesFoldEdge",
                              edge_mismatches, first_row, first_col, &window,
                              destination_row);
    if (test->shade == V9X_MGA3D_SHADE_GOURAUD) {
        none_mismatches = mga2d_tri_compare(&trap, V9X_MGA3D_FOLD_NONE,
                                            &target, &window, &first_row,
                                            &first_col);
        mga2d_tri_report_mismatch(test->name, "MismatchesFoldNone",
                                  none_mismatches, first_row, first_col,
                                  &window, destination_row);

        /* Each row's first drawn pixel and its value, for the fold
         * question read directly. */
        for (row = 0ul; row < test->length; ++row) {
            DWORD image_row = destination_row + row - window.row0;

            for (col = 0ul; col < window.cols; ++col) {
                if (mga2d_actual[image_row][col] !=
                    mga2d_before[image_row][col]) {
                    break;
                }
            }
            lstrcpyA(key, test->name);
            lstrcatA(key, "Left");
            mga2d_decimal(key + lstrlenA(key), row);
            if (col == window.cols) {
                mga2d_write(key, "none");
                continue;
            }
            lstrcpyA(text, "x=");
            mga2d_decimal(text + lstrlenA(text), col);
            lstrcatA(text, " 0x");
            mga2d_hex(text + lstrlenA(text), mga2d_actual[image_row][col], 8);
            mga2d_write(key, text);
        }
    }

    mga2d_key(key, test->name, "Result");
    if (edge_mismatches == 0ul) {
        mga2d_write(key, test->shade == V9X_MGA3D_SHADE_GOURAUD
                    ? "MATCH-FOLD-EDGE" : "PASS");
    } else if (none_mismatches == 0ul) {
        mga2d_write(key, "MATCH-FOLD-NONE");
    } else {
        mga2d_write(key, "FAIL");
        ++mga2d_failures;
    }
}

/*
 * /depth: Gouraud trapezoids with a Z buffer (phase 2 of the plan). Colour
 * bands sit in the region's first half; each case's Z rows sit in the
 * second half at the same row index, Z width per pixel, so ZORG works out
 * the same for every case. Each case's Z rows are prefilled, drawn over,
 * and read back with the colour, and both are compared with mga_3d.c's
 * model. Edges run from lx moving ldx over ldy rows to a vertical right
 * edge at rx; colour is constant (0x40, 0x80, 0xC0); depth starts at z
 * (16-bit, 17.15) or z32 (32-bit, 33.15), with [1] per pixel, [2] per row.
 * The prefill is `prefill` everywhere, plus `ramp` times the column.
 */
#define MGA2D_DEPTH_BAND_ROWS 8ul
#define MGA2D_DEPTH_Z_HALF (MGA2D_REGION_BYTES / 2ul)
#define MGA2D_DEPTH_ONE V9X_MGA3D_Z_ONE

struct mga2d_depth_case {
    const char *name;
    DWORD depth;
    DWORD zmode;
    DWORD z_write;
    long lx;
    long ldx;
    long ldy;
    long rx;
    DWORD length;
    long z[3];
    struct v9x_mga3d_z48 z32[3];
    DWORD prefill;
    DWORD ramp;
};

#define MGA2D_Z16(n) ((long)(n) * MGA2D_DEPTH_ONE)
#define MGA2D_NO_Z32 { { 0L, 0ul }, { 0L, 0ul }, { 0L, 0ul } }
#define MGA2D_DEPTH16_MODE(name, mode) \
    { name, V9X_MGA3D_DEPTH_16, mode, 1ul, 8L, 0L, 2L, 40L, 2ul, \
      { MGA2D_Z16(1000), MGA2D_Z16(1), 0L }, MGA2D_NO_Z32, 1010ul, 0ul }

static const struct mga2d_depth_case mga2d_depth_cases[] = {
    /* Always write; a sloped left edge so Z's fold is seen too. The stored
     * values say where ZORG put the buffer, its pitch, and which bits of
     * the 17.15 value are kept. */
    { "Z16Write", V9X_MGA3D_DEPTH_16, V9X_MGA3D_ZMODE_NOZCMP, 1ul,
      8L, 4L, 4L, 40L, 4ul,
      { MGA2D_Z16(1000), MGA2D_Z16(1), MGA2D_Z16(64) }, MGA2D_NO_Z32,
      0xa5a5ul, 0ul },
    MGA2D_DEPTH16_MODE("Z16ModeE", V9X_MGA3D_ZMODE_ZE),
    MGA2D_DEPTH16_MODE("Z16ModeNE", V9X_MGA3D_ZMODE_ZNE),
    MGA2D_DEPTH16_MODE("Z16ModeLT", V9X_MGA3D_ZMODE_ZLT),
    MGA2D_DEPTH16_MODE("Z16ModeLTE", V9X_MGA3D_ZMODE_ZLTE),
    MGA2D_DEPTH16_MODE("Z16ModeGT", V9X_MGA3D_ZMODE_ZGT),
    MGA2D_DEPTH16_MODE("Z16ModeGTE", V9X_MGA3D_ZMODE_ZGTE),
    /* atype I: compared, never written. */
    { "Z16TestOnly", V9X_MGA3D_DEPTH_16, V9X_MGA3D_ZMODE_ZLT, 0ul,
      8L, 0L, 2L, 40L, 2ul,
      { MGA2D_Z16(1000), MGA2D_Z16(1), 0L }, MGA2D_NO_Z32, 1010ul, 0ul },
    /* Compared against a ramp rather than a constant: stored 990 + 2x. */
    { "Z16Ramp", V9X_MGA3D_DEPTH_16, V9X_MGA3D_ZMODE_ZLT, 1ul,
      8L, 0L, 2L, 40L, 2ul,
      { MGA2D_Z16(1000), MGA2D_Z16(1), 0L }, MGA2D_NO_Z32, 974ul, 2ul },
    /* Through 65535: wrap, clamp or the sign bit? */
    { "Z16Top", V9X_MGA3D_DEPTH_16, V9X_MGA3D_ZMODE_NOZCMP, 1ul,
      8L, 0L, 1L, 40L, 1ul,
      { MGA2D_Z16(65520), MGA2D_Z16(1), 0L }, MGA2D_NO_Z32, 0x5a5aul, 0ul },
    /* Up through 0 from below. */
    { "Z16Bottom", V9X_MGA3D_DEPTH_16, V9X_MGA3D_ZMODE_NOZCMP, 1ul,
      8L, 0L, 1L, 40L, 1ul,
      { MGA2D_Z16(-10), MGA2D_Z16(1), 0L }, MGA2D_NO_Z32, 0x5a5aul, 0ul },
    /* A half to start, a quarter a pixel: is the stored value truncated? */
    { "Z16Fraction", V9X_MGA3D_DEPTH_16, V9X_MGA3D_ZMODE_NOZCMP, 1ul,
      8L, 0L, 1L, 40L, 1ul,
      { MGA2D_Z16(1000) + 0x4000L, 0x2000L, 0L }, MGA2D_NO_Z32,
      0x5a5aul, 0ul },
    /* 32-bit Z, the 2164W's: 0x12345678 + 0x10001 a pixel, + 0x01000000 a
     * row, all bits of the stored value distinct. */
    { "Z32Write", V9X_MGA3D_DEPTH_32, V9X_MGA3D_ZMODE_NOZCMP, 1ul,
      8L, 4L, 4L, 40L, 4ul, { 0L, 0L, 0L },
      { { 0x091aL, 0x2b3c0000ul }, { 0L, 0x80008000ul }, { 0x0080L, 0ul } },
      0xa5a5a5a5ul, 0ul },
    /* 0x7FFFFFF0 + x against 0x80000000: an unsigned compare passes the
     * first 16; a signed one would pass none. */
    { "Z32ModeLT", V9X_MGA3D_DEPTH_32, V9X_MGA3D_ZMODE_ZLT, 1ul,
      8L, 0L, 2L, 40L, 2ul, { 0L, 0L, 0L },
      { { 0x3fffL, 0xfff80000ul }, { 0L, 0x00008000ul }, { 0L, 0ul } },
      0x80000000ul, 0ul },
    /* Through 2^32 from 0xFFFFFFF0. */
    { "Z32Top", V9X_MGA3D_DEPTH_32, V9X_MGA3D_ZMODE_NOZCMP, 1ul,
      8L, 0L, 1L, 40L, 1ul, { 0L, 0L, 0L },
      { { 0x7fffL, 0xfff80000ul }, { 0L, 0x00008000ul }, { 0L, 0ul } },
      0x5a5a5a5aul, 0ul },
    /* Up through 0 from -10. */
    { "Z32Bottom", V9X_MGA3D_DEPTH_32, V9X_MGA3D_ZMODE_NOZCMP, 1ul,
      8L, 0L, 1L, 40L, 1ul, { 0L, 0L, 0L },
      { { -1L, 0xfffb0000ul }, { 0L, 0x00008000ul }, { 0L, 0ul } },
      0x5a5a5a5aul, 0ul }
};

#define MGA2D_DEPTH_CASE_COUNT \
    (sizeof(mga2d_depth_cases) / sizeof(mga2d_depth_cases[0]))

/* The Z images: before the draw, the model's after, and the card's. */
static DWORD (*mga2d_zbefore)[MGA2D_WIN_COLS];
static DWORD (*mga2d_zexpected)[MGA2D_WIN_COLS];
static DWORD (*mga2d_zactual)[MGA2D_WIN_COLS];

struct mga2d_z_target {
    DWORD first_row;
    DWORD window_row0;
    DWORD window_rows;
};

static v9x_u32 mga2d_z_read(void *context, v9x_s32 x, v9x_u32 row)
{
    struct mga2d_z_target *target = (struct mga2d_z_target *)context;
    DWORD image_row = target->first_row + row - target->window_row0;

    if (x < 0L || (DWORD)x >= MGA2D_TRI_COLS ||
        image_row >= target->window_rows) {
        return 0ul;
    }
    return mga2d_zexpected[image_row][x];
}

static void mga2d_z_write(void *context, v9x_s32 x, v9x_u32 row,
                          v9x_u32 value)
{
    struct mga2d_z_target *target = (struct mga2d_z_target *)context;
    DWORD image_row = target->first_row + row - target->window_row0;

    if (x < 0L || (DWORD)x >= MGA2D_TRI_COLS ||
        image_row >= target->window_rows) {
        return;
    }
    mga2d_zexpected[image_row][x] = value;
}

/* A window of Z values: rows from row0 of the Z half, cols from column 0,
 * z_bytes a value. Written from or read into an image. */
static int mga2d_z_stream(const struct mga2d_window *window, DWORD z_bytes,
                          DWORD (*image)[MGA2D_WIN_COLS], int write)
{
    DWORD per_word = 4ul / z_bytes;
    DWORD mask = z_bytes == 4ul ? 0xfffffffful : 0xfffful;
    DWORD words = window->cols / per_word;
    DWORD row;
    DWORD word;
    DWORD index;
    DWORD value;
    DWORD first_op = 0ul;
    DWORD first_row = 0ul;
    DWORD first_word = 0ul;
    DWORD ops;
    DWORD op;

    mga2d_begin();
    for (row = 0ul; row < window->rows; ++row) {
        for (word = 0ul; word < words; ++word) {
            DWORD offset = mga2d_region_base + MGA2D_DEPTH_Z_HALF +
                (window->row0 + row) * MGA2D_PITCH_PIXELS * z_bytes +
                word * 4ul;

            if (mga2d_request_buffer.count == 0ul) {
                first_row = row;
                first_word = word;
            }
            if (write) {
                value = 0ul;
                for (index = 0ul; index < per_word; ++index) {
                    value |= (image[row][word * per_word + index] & mask) <<
                             (index * z_bytes * 8ul);
                }
                mga2d_add(MGA2D_OP_LFB_WRITE32, offset, value, 0ul);
            } else {
                mga2d_add(MGA2D_OP_LFB_READ32, offset, 0ul, 0ul);
            }
            if (mga2d_request_buffer.count == MGA2D_OP_MAX ||
                (row + 1ul == window->rows && word + 1ul == words)) {
                ops = mga2d_request_buffer.count;
                if (!mga2d_run()) {
                    return 0;
                }
                if (!write) {
                    /* Unpack this run's reads, from where it started. */
                    DWORD at_row = first_row;
                    DWORD at_word = first_word;

                    for (op = first_op; op < ops; ++op) {
                        value = mga2d_value(op);
                        for (index = 0ul; index < per_word; ++index) {
                            image[at_row][at_word * per_word + index] =
                                (value >> (index * z_bytes * 8ul)) & mask;
                        }
                        if (++at_word == words) {
                            at_word = 0ul;
                            ++at_row;
                        }
                    }
                }
                mga2d_begin();
            }
        }
    }
    return 1;
}

static void mga2d_report_z_row(const char *name, DWORD row_label,
                               DWORD image_row, DWORD first_col,
                               DWORD count, DWORD z_bytes)
{
    char key[64];
    char text[24u * 9u + 8u];
    DWORD index;

    lstrcpyA(key, name);
    lstrcatA(key, "Z");
    mga2d_decimal(key + lstrlenA(key), row_label);
    text[0] = '\0';
    for (index = 0ul; index < count; ++index) {
        if (index != 0ul) {
            lstrcatA(text, " ");
        }
        mga2d_hex(text + lstrlenA(text),
                  mga2d_zactual[image_row][first_col + index],
                  (int)(z_bytes * 2ul));
    }
    mga2d_write(key, text);
}

static void mga2d_depth_test(const struct mga2d_depth_case *test,
                             DWORD band)
{
    struct v9x_mga3d_trap trap;
    struct v9x_mga3d_writes writes;
    struct mga2d_window window;
    struct mga2d_plot_target target;
    struct mga2d_z_target z_target;
    struct v9x_mga3d_depth_io depth_io;
    char key[64];
    char text[MGA2D_TRI_COLS + 32ul];
    DWORD destination_row = band * MGA2D_DEPTH_BAND_ROWS + MGA2D_GUARD;
    DWORD z_bytes = test->depth == V9X_MGA3D_DEPTH_32 ? 4ul : 2ul;
    DWORD row;
    DWORD col;
    DWORD index;
    DWORD count;
    DWORD color_mismatches = 0ul;
    DWORD z_mismatches = 0ul;
    DWORD first_row = 0ul;
    DWORD first_col = 0ul;
    DWORD z_first_row = 0ul;
    DWORD z_first_col = 0ul;
    v9x_u32 offsets[V9X_MGA_MAX_WRITES + V9X_MGA3D_MAX_WRITES];
    v9x_u32 values[V9X_MGA_MAX_WRITES + V9X_MGA3D_MAX_WRITES];
    v9x_status status;

    if (mga2d_bpp != 4ul) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "SKIPPED-DEPTH");
        return;
    }
    if (test->depth == V9X_MGA3D_DEPTH_32 && !mga2d_is_2164w) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "SKIPPED-CHIP");
        return;
    }
    window.row0 = destination_row - MGA2D_GUARD;
    window.rows = test->length + 2ul * MGA2D_GUARD;
    window.cols = MGA2D_TRI_COLS;
    if ((window.row0 + window.rows) * mga2d_pitch_bytes >
            MGA2D_DEPTH_Z_HALF ||
        (window.row0 + window.rows) * MGA2D_PITCH_PIXELS * z_bytes >
            MGA2D_DEPTH_Z_HALF) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "WINDOW-REFUSED");
        ++mga2d_failures;
        return;
    }

    mga2d_zero(&trap, sizeof(trap));
    trap.vram_bytes = mga2d_vram_bytes;
    trap.target_offset = mga2d_row_offset(destination_row);
    trap.pitch_bytes = mga2d_pitch_bytes;
    trap.bytes_per_pixel = mga2d_bpp;
    trap.length = test->length;
    trap.left.x = test->lx;
    trap.left.dx = test->ldx;
    trap.left.dy = test->ldy;
    trap.right.x = test->rx;
    trap.right.dy = test->ldy;
    trap.shade = V9X_MGA3D_SHADE_GOURAUD;
    trap.color = MGA2D_TRI_ALPHA;
    trap.red[0] = MGA2D_LEVEL(0x40);
    trap.green[0] = MGA2D_LEVEL(0x80);
    trap.blue[0] = MGA2D_LEVEL(0xc0);
    trap.depth = test->depth;
    trap.zmode = test->zmode;
    trap.z_write = test->z_write;
    trap.z_offset = mga2d_region_base + MGA2D_DEPTH_Z_HALF +
        destination_row * MGA2D_PITCH_PIXELS * z_bytes;
    for (index = 0ul; index < 3ul; ++index) {
        trap.z[index] = test->z[index];
        trap.z32[index] = test->z32[index];
    }
    status = v9x_mga3d_build_trap(&trap, &writes);
    if (status != V9X_STATUS_OK) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "BUILD-REFUSED");
        mga2d_key(key, test->name, "BuildStatus");
        mga2d_write_decimal(key, status);
        ++mga2d_failures;
        return;
    }
    mga2d_key(key, test->name, "DestinationRow");
    mga2d_write_decimal(key, destination_row);
    mga2d_report_list(test->name, writes.offsets, writes.values,
                      writes.count);

    for (row = 0ul; row < window.rows; ++row) {
        for (col = 0ul; col < window.cols; ++col) {
            mga2d_before[row][col] = mga2d_pattern(col, window.row0 + row);
            mga2d_zbefore[row][col] = (test->prefill + test->ramp * col) &
                (z_bytes == 4ul ? 0xfffffffful : 0xfffful);
        }
    }
    if (!mga2d_stream_write(&window, mga2d_before) ||
        !mga2d_z_stream(&window, z_bytes, mga2d_zbefore, 1)) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "PREPARE-FAILED");
        ++mga2d_failures;
        return;
    }
    count = 0ul;
    for (index = 0ul; index < mga2d_setup.count; ++index) {
        offsets[count] = mga2d_setup.offsets[index];
        values[count] = mga2d_setup.values[index];
        ++count;
    }
    for (index = 0ul; index < writes.count; ++index) {
        offsets[count] = writes.offsets[index];
        values[count] = writes.values[index];
        ++count;
    }
    if (!mga2d_run_list(test->name, offsets, values, count)) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, mga2d_engine_dead ? "ENGINE-TIMEOUT" : "RUN-FAILED");
        ++mga2d_failures;
        return;
    }
    if (!mga2d_stream_read(&window, mga2d_actual) ||
        !mga2d_z_stream(&window, z_bytes, mga2d_zactual, 0)) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "READBACK-FAILED");
        ++mga2d_failures;
        return;
    }

    for (row = 0ul; row < window.rows; ++row) {
        for (col = 0ul; col < window.cols; ++col) {
            text[col] = mga2d_actual[row][col] != mga2d_before[row][col]
                ? '#' : '.';
        }
        text[window.cols] = '\0';
        lstrcpyA(key, test->name);
        lstrcatA(key, "Row");
        mga2d_signed(key + lstrlenA(key),
                     (long)(window.row0 + row) - (long)destination_row);
        mga2d_write(key, text);
    }
    for (row = 0ul; row < test->length; ++row) {
        mga2d_report_z_row(test->name, row,
                           destination_row + row - window.row0,
                           (DWORD)test->lx, 24ul, z_bytes);
    }

    /* The model, run against the prefilled Z, then both images compared. */
    mga2d_bytes(mga2d_expected, mga2d_before, sizeof(mga2d_image));
    mga2d_bytes(mga2d_zexpected, mga2d_zbefore, sizeof(mga2d_image));
    target.first_row = destination_row;
    target.window_row0 = window.row0;
    target.window_rows = window.rows;
    z_target.first_row = destination_row;
    z_target.window_row0 = window.row0;
    z_target.window_rows = window.rows;
    depth_io.read = mga2d_z_read;
    depth_io.write = mga2d_z_write;
    depth_io.context = &z_target;
    depth_io.texel = 0;
    depth_io.texel_context = 0;
    status = v9x_mga3d_model_trap(&trap, V9X_MGA3D_FOLD_EDGE, mga2d_tri_plot,
                                  &target, &depth_io);
    if (status != V9X_STATUS_OK) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "MODEL-REFUSED");
        ++mga2d_failures;
        return;
    }
    for (row = 0ul; row < window.rows; ++row) {
        for (col = 0ul; col < window.cols; ++col) {
            if (mga2d_actual[row][col] != mga2d_expected[row][col]) {
                if (color_mismatches++ == 0ul) {
                    first_row = row;
                    first_col = col;
                }
            }
            if (mga2d_zactual[row][col] != mga2d_zexpected[row][col]) {
                if (z_mismatches++ == 0ul) {
                    z_first_row = row;
                    z_first_col = col;
                }
            }
        }
    }
    mga2d_tri_report_mismatch(test->name, "ColorMismatches",
                              color_mismatches, first_row, first_col,
                              &window, destination_row);
    mga2d_key(key, test->name, "ZMismatches");
    mga2d_write_decimal(key, z_mismatches);
    if (z_mismatches != 0ul) {
        lstrcpyA(text, "x=");
        mga2d_decimal(text + lstrlenA(text), z_first_col);
        lstrcatA(text, " y=");
        mga2d_signed(text + lstrlenA(text),
                     (long)(window.row0 + z_first_row) -
                     (long)destination_row);
        lstrcatA(text, " got=0x");
        mga2d_hex(text + lstrlenA(text),
                  mga2d_zactual[z_first_row][z_first_col], 8);
        lstrcatA(text, " want=0x");
        mga2d_hex(text + lstrlenA(text),
                  mga2d_zexpected[z_first_row][z_first_col], 8);
        mga2d_key(key, test->name, "ZMismatchFirst");
        mga2d_write(key, text);
    }
    mga2d_key(key, test->name, "Result");
    if (color_mismatches == 0ul && z_mismatches == 0ul) {
        mga2d_write(key, "PASS");
    } else {
        mga2d_write(key, "FAIL");
        ++mga2d_failures;
    }
}

/*
 * /tex: textured trapezoids (phase 3 of the plan), at 32 bpp on the 2164W.
 * Every register and field used here is a hypothesis from 86Box; Matrox's
 * public specification omits texturing. Each case uploads a 16 x 16
 * texture whose texels are all distinct into the region's second half,
 * draws a rectangle of rows over it and compares the colour with the model
 * (RGB only: the model writes no alpha byte, and what the card writes there
 * is reported). For TW16 each drawn pixel is also decoded back to the
 * texel it shows, red giving s and green t, and written as "s.t".
 */
#define MGA2D_TEX_BAND_ROWS 7ul
/* Colour bands in the region's first 768 KiB, textures in the last 256,
 * 8 KiB a case. */
#define MGA2D_TEX_AREA 0x000c0000ul
#define MGA2D_TEX_SIZE 16ul
#define MGA2D_TEX_ONE 0x00010000L   /* one texel of a 16-texel side */
#define MGA2D_TEX_Q_ONE 0x00010000L

struct mga2d_tex_case {
    const char *name;
    DWORD format;
    DWORD width;
    DWORD length;
    long tmr[9];
    DWORD clamp_u;
    DWORD perspective;
    DWORD modulate;
    DWORD key_s;            /* key the texel at (key_s, 0) when not 0xFFFF */
    DWORD key_mask;
    DWORD alpha_mask;       /* TEXCTL tamask */
    DWORD alpha_key;        /* TEXCTL takey */
    DWORD log2_size;        /* 0 means 4: 16 x 16 */
    DWORD log2_pitch;       /* 0 means the size */
    DWORD org_extra;        /* bytes added to the 8 KiB-aligned TEXORG */
    DWORD known_divergence; /* mismatches reported, not failed */
};

#define MGA2D_TEX_NOKEY 0xfffful
#define MGA2D_TEX_STEPS(ds, dsdy, s0) \
    { (ds), (dsdy), 0L, MGA2D_TEX_ONE, 0L, 0L, (s0), 0L, MGA2D_TEX_Q_ONE }

static const struct mga2d_tex_case mga2d_tex_cases[] = {
    /* Coordinates, read through modulation by white Gouraud so the texel
     * shows whatever decal does: one texel a pixel across, one a row
     * down. */
    { "TexIdentity", V9X_MGA3D_TEX_TW16, 16ul, 4ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, 0L), 0ul, 0ul, 1ul,
      MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 0ul, 0ul },
    /* Half a texel a pixel: magnification, and the sample point. */
    { "TexMagnify", V9X_MGA3D_TEX_TW16, 32ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE / 2L, 0L, 0L), 0ul, 0ul, 1ul,
      MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 0ul, 0ul },
    /* Starting half a texel in. */
    { "TexHalfStart", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, MGA2D_TEX_ONE / 2L), 0ul, 0ul,
      1ul, MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul },
    /* 40 texels across a 16-texel texture: wrap. */
    { "TexWrap", V9X_MGA3D_TEX_TW16, 40ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, 0L), 0ul, 0ul, 1ul,
      MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 0ul, 0ul },
    /* The same clamped. */
    { "TexClampU", V9X_MGA3D_TEX_TW16, 40ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, 0L), 1ul, 0ul, 1ul,
      MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 0ul, 0ul },
    /* From s = -4: wrap below zero. */
    { "TexNegative", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, -4L * MGA2D_TEX_ONE), 0ul, 0ul, 1ul,
      MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 0ul, 0ul },
    /* s also gains a texel a row: TMR1. */
    { "TexSkew", V9X_MGA3D_TEX_TW16, 16ul, 4ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, MGA2D_TEX_ONE, 0L), 0ul, 0ul, 1ul,
      MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 0ul, 0ul },
    /* Perspective on, q = 1: the identity, if q is 16.16. */
    { "TexPerspective", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, 0L), 0ul, 1ul, 1ul,
      MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 0ul, 0ul },
    /* Perspective, q = 2 and s, t doubled: the identity again. */
    { "TexPerspectiveQ2", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      { 2L * MGA2D_TEX_ONE, 0L, 0L, 2L * MGA2D_TEX_ONE, 0L, 0L, 0L, 0L,
        2L * MGA2D_TEX_Q_ONE }, 0ul, 1ul, 1ul, MGA2D_TEX_NOKEY, 0xfffful,
      0ul, 0ul, 0ul, 0ul, 0ul },
    /* Texel (3, 0) keyed out with a full mask. */
    { "TexKey", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, 0L), 0ul, 0ul, 1ul, 3ul, 0xfffful,
      0ul, 0ul, 0ul, 0ul, 0ul },
    /* TEXTRANS zero: an empty mask keys every texel out. */
    { "TexZeroTrans", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, 0L), 0ul, 0ul, 0ul, 0ul, 0ul,
      0ul, 0ul, 0ul, 0ul, 0ul },
    /* Decal on 565 under each tamask/takey: which shows the texel? */
    { "DecalMask0", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, 0L), 0ul, 0ul, 0ul,
      MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 0ul, 0ul },
    { "DecalMask1Key0", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, 0L), 0ul, 0ul, 0ul,
      MGA2D_TEX_NOKEY, 0xfffful, 1ul, 0ul, 0ul, 0ul, 0ul },
    { "DecalMask1Key1", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, 0L), 0ul, 0ul, 0ul,
      MGA2D_TEX_NOKEY, 0xfffful, 1ul, 1ul, 0ul, 0ul, 0ul },
    /* 1555 decal, the alpha bit set on odd columns. */
    { "Decal15Key0", V9X_MGA3D_TEX_TW15, 16ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, 0L), 0ul, 0ul, 0ul,
      MGA2D_TEX_NOKEY, 0xfffful, 1ul, 0ul, 0ul, 0ul, 0ul },
    { "Decal15Key1", V9X_MGA3D_TEX_TW15, 16ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, 0L), 0ul, 0ul, 0ul,
      MGA2D_TEX_NOKEY, 0xfffful, 1ul, 1ul, 0ul, 0ul, 0ul },
    { "Tex15Modulate", V9X_MGA3D_TEX_TW15, 16ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, 0L), 0ul, 0ul, 1ul,
      MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 0ul, 0ul },
    /* True perspective: q from 1 rising 1/16 a pixel, s one texel a
     * pixel, so the texel is x / (1 + x / 16). */
    { "TexPerspectiveRamp", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      { MGA2D_TEX_ONE, 0L, 0L, MGA2D_TEX_ONE, MGA2D_TEX_Q_ONE / 16L, 0L,
        0L, 0L, MGA2D_TEX_Q_ONE }, 0ul, 1ul, 1ul, MGA2D_TEX_NOKEY,
      0xfffful, 0ul, 0ul, 0ul, 0ul, 0ul },
    /* 32 x 32: one texel a pixel is 1 << 15 at this size. */
    { "TexSize32", V9X_MGA3D_TEX_TW16, 32ul, 4ul,
      { MGA2D_TEX_ONE / 2L, 0L, 0L, MGA2D_TEX_ONE / 2L, 0L, 0L, 0L, 0L,
        MGA2D_TEX_Q_ONE }, 0ul, 0ul, 1ul, MGA2D_TEX_NOKEY, 0xfffful,
      0ul, 0ul, 5ul, 0ul, 0ul },
    /* 16 x 16 in rows of 64 texels. */
    { "TexPitch64", V9X_MGA3D_TEX_TW16, 16ul, 4ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, 0L), 0ul, 0ul, 1ul,
      MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 6ul, 0ul },
    /* TEXORG 64 and 32 bytes past an 8 KiB boundary. 8 and 16 bytes past
     * fetched early (b385, MGA2D-tex-round4.TXT): the builder refuses
     * them. */
    { "TexOrg64", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, 0L), 0ul, 0ul, 1ul,
      MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 0ul, 0x40ul },
    { "TexOrg32", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, 0L), 0ul, 0ul, 1ul,
      MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 0ul, 0x20ul },
    /* Linear, starting 7/8 of a texel in: floor, or a bias as in the
     * perspective path? */
    { "TexStart7of8", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      MGA2D_TEX_STEPS(MGA2D_TEX_ONE, 0L, (MGA2D_TEX_ONE * 7L) / 8L), 0ul,
      0ul, 1ul, MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 0ul, 0ul },
    /* The perspective ramp on a 32-texel texture: is the bias 1/8 of a
     * texel, or of a fixed coordinate step? */
    { "TexPerspectiveRamp32", V9X_MGA3D_TEX_TW16, 32ul, 2ul,
      { MGA2D_TEX_ONE / 2L, 0L, 0L, MGA2D_TEX_ONE / 2L,
        MGA2D_TEX_Q_ONE / 32L, 0L, 0L, 0L, MGA2D_TEX_Q_ONE }, 0ul, 1ul, 1ul,
      MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 5ul, 0ul, 0ul },
    /* A steeper ramp, q 1 to 3 over 16 pixels, and a start offset. The
     * card's divider differs from the model at 2 pixels a row, where the
     * quotient is exactly an integer: a known divergence, reported, not
     * failed. */
    { "TexPerspectiveSteep", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      { 3L * MGA2D_TEX_ONE, 0L, 0L, MGA2D_TEX_ONE, MGA2D_TEX_Q_ONE / 8L, 0L,
        MGA2D_TEX_ONE / 4L, 0L, MGA2D_TEX_Q_ONE }, 0ul, 1ul, 1ul,
      MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 0ul, 0ul, 1ul },
    /* The ramp with s, t and q all 256 and 1024 times larger, q to 2^27
     * and s to 2^30: does the divider use q's high bits, as the setup
     * now scales them to keep q's step precise (mga_setup.c)? It does,
     * and the scaled ramps lost the 1/8 texel at pixel 1 (boot 391): the
     * eighth is added before the divide, not after. */
    { "TexPerspectiveRampK256", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      { 256L * MGA2D_TEX_ONE, 0L, 0L, 256L * MGA2D_TEX_ONE,
        16L * MGA2D_TEX_Q_ONE, 0L, 0L, 0L, 256L * MGA2D_TEX_Q_ONE }, 0ul,
      1ul, 1ul, MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 0ul, 0ul },
    { "TexPerspectiveRampK1024", V9X_MGA3D_TEX_TW16, 16ul, 2ul,
      { 1024L * MGA2D_TEX_ONE, 0L, 0L, 1024L * MGA2D_TEX_ONE,
        64L * MGA2D_TEX_Q_ONE, 0L, 0L, 0L, 1024L * MGA2D_TEX_Q_ONE }, 0ul,
      1ul, 1ul, MGA2D_TEX_NOKEY, 0xfffful, 0ul, 0ul, 0ul, 0ul, 0ul }
};

#define MGA2D_TEX_CASE_COUNT \
    (sizeof(mga2d_tex_cases) / sizeof(mga2d_tex_cases[0]))

/* The texel at (s, t) of each format's test texture: all distinct up to
 * 32 x 32, red carrying s and green t.
 * TW12's alpha nibble is F; TW15's alpha bit is set on odd columns. */
static DWORD mga2d_tex_texel(DWORD format, DWORD s, DWORD t)
{
    if (format == V9X_MGA3D_TEX_TW15) {
        return ((s & 1ul) << 15) | (s << 10) | (t << 5) | ((s + t) & 0x1ful);
    }
    if (format == V9X_MGA3D_TEX_TW12) {
        return 0xf000ul | (s << 8) | (t << 4) | ((s + t) & 0x0ful);
    }
    return (s << 11) | (t << 5) | ((s + t) & 0x1ful);
}

static DWORD mga2d_tex_format;

static v9x_u32 mga2d_tex_texel_fn(void *context, v9x_u32 s, v9x_u32 t)
{
    (void)context;
    return mga2d_tex_texel(mga2d_tex_format, s, t);
}

static v9x_u32 mga2d_tex_no_z(void *context, v9x_s32 x, v9x_u32 row)
{
    (void)context;
    (void)x;
    (void)row;
    return 0ul;
}

static void mga2d_tex_no_z_write(void *context, v9x_s32 x, v9x_u32 row,
                                 v9x_u32 value)
{
    (void)context;
    (void)x;
    (void)row;
    (void)value;
}

/* A 5- or 6-bit channel widened by replication, as the card does it. */
static DWORD mga2d_widen(DWORD value, DWORD bits)
{
    return (value << (8ul - bits)) | (value >> (2ul * bits - 8ul));
}

/*
 * Which texel a drawn pixel shows: every (s, t) of the test texture tried,
 * as the card renders it - widened, and under modulation by white times
 * 255 >> 8. Non-zero when exactly one texel matches.
 */
static int mga2d_tex_decode(DWORD format, DWORD modulate, DWORD size,
                            DWORD pixel, DWORD *s_out, DWORD *t_out)
{
    DWORD s;
    DWORD t;
    DWORD texel;
    DWORD red;
    DWORD green;
    DWORD blue;
    DWORD matches = 0ul;

    for (t = 0ul; t < size; ++t) {
        for (s = 0ul; s < size; ++s) {
            texel = mga2d_tex_texel(format, s, t);
            if (format == V9X_MGA3D_TEX_TW16) {
                red = mga2d_widen(texel >> 11, 5ul);
                green = mga2d_widen((texel >> 5) & 0x3ful, 6ul);
            } else {
                red = mga2d_widen((texel >> 10) & 0x1ful, 5ul);
                green = mga2d_widen((texel >> 5) & 0x1ful, 5ul);
            }
            blue = mga2d_widen(texel & 0x1ful, 5ul);
            if (modulate) {
                red = (red * 0xfful) >> 8;
                green = (green * 0xfful) >> 8;
                blue = (blue * 0xfful) >> 8;
            }
            if (((red << 16) | (green << 8) | blue) == pixel) {
                *s_out = s;
                *t_out = t;
                ++matches;
            }
        }
    }
    return matches == 1ul;
}

/* Upload the case's texture: size rows of size texels, pitch texels a
 * row. The VxD takes dword stores, so an offset that is not a multiple of
 * four is refused here (TexOrg8's 8 is). */
static int mga2d_tex_upload(DWORD offset, DWORD format, DWORD size,
                            DWORD pitch)
{
    DWORD t;
    DWORD s;

    if ((offset & 3ul) != 0ul) {
        return 0;
    }
    mga2d_begin();
    for (t = 0ul; t < size; ++t) {
        for (s = 0ul; s < size; s += 2ul) {
            mga2d_add(MGA2D_OP_LFB_WRITE32, offset + (t * pitch + s) * 2ul,
                      mga2d_tex_texel(format, s, t) |
                      (mga2d_tex_texel(format, s + 1ul, t) << 16), 0ul);
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

static void mga2d_tex_test(const struct mga2d_tex_case *test, DWORD band)
{
    struct v9x_mga3d_trap trap;
    struct v9x_mga3d_writes writes;
    struct mga2d_window window;
    struct mga2d_plot_target target;
    struct v9x_mga3d_depth_io io;
    char key[64];
    char text[MGA2D_TRI_COLS * 6ul + 32ul];
    DWORD destination_row = band * MGA2D_TEX_BAND_ROWS + MGA2D_GUARD;
    DWORD log2_size = test->log2_size != 0ul ? test->log2_size : 4ul;
    DWORD log2_pitch = test->log2_pitch != 0ul ? test->log2_pitch
                                               : log2_size;
    DWORD texture_offset = mga2d_region_base + MGA2D_TEX_AREA +
        band * 0x2000ul + test->org_extra;
    DWORD left = 8ul;
    DWORD row;
    DWORD col;
    DWORD index;
    DWORD count;
    DWORD mismatches = 0ul;
    DWORD first_row = 0ul;
    DWORD first_col = 0ul;
    DWORD alpha_seen = 0xfffffffful;
    DWORD image_row;
    DWORD pixel;
    DWORD s;
    DWORD t;
    v9x_u32 offsets[V9X_MGA_MAX_WRITES + V9X_MGA3D_MAX_WRITES];
    v9x_u32 values[V9X_MGA_MAX_WRITES + V9X_MGA3D_MAX_WRITES];
    v9x_status status;

    if (mga2d_bpp != 4ul) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "SKIPPED-DEPTH");
        return;
    }
    if (!mga2d_is_2164w) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "SKIPPED-CHIP");
        return;
    }
    window.row0 = destination_row - MGA2D_GUARD;
    window.rows = test->length + 2ul * MGA2D_GUARD;
    window.cols = MGA2D_TRI_COLS;
    if ((window.row0 + window.rows) * mga2d_pitch_bytes >
            MGA2D_TEX_AREA ||
        left + test->width > MGA2D_TRI_COLS) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "WINDOW-REFUSED");
        ++mga2d_failures;
        return;
    }

    mga2d_zero(&trap, sizeof(trap));
    trap.vram_bytes = mga2d_vram_bytes;
    trap.target_offset = mga2d_row_offset(destination_row);
    trap.pitch_bytes = mga2d_pitch_bytes;
    trap.bytes_per_pixel = mga2d_bpp;
    trap.length = test->length;
    trap.left.x = (long)left;
    trap.left.dy = (long)test->length;
    trap.right.x = (long)(left + test->width);
    trap.right.dy = (long)test->length;
    trap.shade = V9X_MGA3D_SHADE_GOURAUD;
    trap.color = MGA2D_TRI_ALPHA;
    /* White under modulation, so the texel shows; 80/FF/40 under decal, so
     * the Gouraud colour is told from any texel. */
    trap.red[0] = MGA2D_LEVEL(test->modulate ? 0xff : 0x80);
    trap.green[0] = MGA2D_LEVEL(0xff);
    trap.blue[0] = MGA2D_LEVEL(test->modulate ? 0xff : 0x40);
    trap.texture.enabled = 1ul;
    trap.texture.offset = texture_offset;
    trap.texture.format = test->format;
    trap.texture.log2_width = log2_size;
    trap.texture.log2_height = log2_size;
    trap.texture.log2_pitch = log2_pitch;
    trap.texture.clamp_u = test->clamp_u;
    trap.texture.perspective = test->perspective;
    trap.texture.modulate = test->modulate;
    trap.texture.key = test->key_s == MGA2D_TEX_NOKEY
        ? (test->key_mask == 0ul ? 0ul : 0xfffful)
        : mga2d_tex_texel(test->format, test->key_s, 0ul);
    trap.texture.key_mask = test->key_mask;
    trap.texture.alpha_mask = test->alpha_mask;
    trap.texture.alpha_key = test->alpha_key;
    for (index = 0ul; index < 9ul; ++index) {
        trap.texture.tmr[index] = test->tmr[index];
    }
    status = v9x_mga3d_build_trap(&trap, &writes);
    if (status != V9X_STATUS_OK) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "BUILD-REFUSED");
        mga2d_key(key, test->name, "BuildStatus");
        mga2d_write_decimal(key, status);
        ++mga2d_failures;
        return;
    }
    mga2d_key(key, test->name, "DestinationRow");
    mga2d_write_decimal(key, destination_row);
    mga2d_report_list(test->name, writes.offsets, writes.values,
                      writes.count);

    for (row = 0ul; row < window.rows; ++row) {
        for (col = 0ul; col < window.cols; ++col) {
            mga2d_before[row][col] = mga2d_pattern(col, window.row0 + row);
        }
    }
    if (!mga2d_stream_write(&window, mga2d_before) ||
        !mga2d_tex_upload(texture_offset, test->format, 1ul << log2_size,
                          1ul << log2_pitch)) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "PREPARE-FAILED");
        ++mga2d_failures;
        return;
    }
    count = 0ul;
    for (index = 0ul; index < mga2d_setup.count; ++index) {
        offsets[count] = mga2d_setup.offsets[index];
        values[count] = mga2d_setup.values[index];
        ++count;
    }
    for (index = 0ul; index < writes.count; ++index) {
        offsets[count] = writes.offsets[index];
        values[count] = writes.values[index];
        ++count;
    }
    if (!mga2d_run_list(test->name, offsets, values, count)) {
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

    for (row = 0ul; row < window.rows; ++row) {
        for (col = 0ul; col < window.cols; ++col) {
            text[col] = mga2d_actual[row][col] != mga2d_before[row][col]
                ? '#' : '.';
        }
        text[window.cols] = '\0';
        lstrcpyA(key, test->name);
        lstrcatA(key, "Row");
        mga2d_signed(key + lstrlenA(key),
                     (long)(window.row0 + row) - (long)destination_row);
        mga2d_write(key, text);
    }

    /* Each drawn row as the card shows it: decoded texels for TW16, raw
     * RGB otherwise. */
    for (row = 0ul; row < test->length; ++row) {
        image_row = destination_row + row - window.row0;
        text[0] = '\0';
        for (col = left; col < left + test->width; ++col) {
            pixel = mga2d_actual[image_row][col];
            if (col != left) {
                lstrcatA(text, " ");
            }
            if (pixel == mga2d_before[image_row][col]) {
                lstrcatA(text, "--");
                continue;
            }
            if (alpha_seen == 0xfffffffful) {
                alpha_seen = pixel >> 24;
            }
            if (test->format != V9X_MGA3D_TEX_TW12 &&
                mga2d_tex_decode(test->format, test->modulate,
                                 1ul << log2_size, pixel & 0x00fffffful,
                                 &s, &t)) {
                mga2d_decimal(text + lstrlenA(text), s);
                lstrcatA(text, ".");
                mga2d_decimal(text + lstrlenA(text), t);
            } else {
                mga2d_hex(text + lstrlenA(text), pixel & 0x00fffffful, 6);
            }
        }
        lstrcpyA(key, test->name);
        lstrcatA(key, "Texels");
        mga2d_decimal(key + lstrlenA(key), row);
        mga2d_write(key, text);
    }
    if (alpha_seen != 0xfffffffful) {
        mga2d_key(key, test->name, "AlphaByte");
        mga2d_write_hex(key, alpha_seen);
    }

    /* The model, RGB only. */
    mga2d_bytes(mga2d_expected, mga2d_before, sizeof(mga2d_image));
    target.first_row = destination_row;
    target.window_row0 = window.row0;
    target.window_rows = window.rows;
    mga2d_tex_format = test->format;
    io.read = mga2d_tex_no_z;
    io.write = mga2d_tex_no_z_write;
    io.context = 0;
    io.texel = mga2d_tex_texel_fn;
    io.texel_context = 0;
    status = v9x_mga3d_model_trap(&trap, V9X_MGA3D_FOLD_EDGE, mga2d_tri_plot,
                                  &target, &io);
    if (status != V9X_STATUS_OK) {
        mga2d_key(key, test->name, "Result");
        mga2d_write(key, "MODEL-REFUSED");
        ++mga2d_failures;
        return;
    }
    for (row = 0ul; row < window.rows; ++row) {
        for (col = 0ul; col < window.cols; ++col) {
            if (((mga2d_actual[row][col] ^ mga2d_expected[row][col]) &
                 0x00fffffful) != 0ul ||
                ((mga2d_actual[row][col] == mga2d_before[row][col]) !=
                 (mga2d_expected[row][col] == mga2d_before[row][col]))) {
                if (mismatches++ == 0ul) {
                    first_row = row;
                    first_col = col;
                }
            }
        }
    }
    mga2d_tri_report_mismatch(test->name, "Mismatches", mismatches,
                              first_row, first_col, &window,
                              destination_row);
    mga2d_key(key, test->name, "Result");
    if (mismatches == 0ul) {
        mga2d_write(key, "PASS");
    } else if (test->known_divergence) {
        mga2d_write(key, "KNOWN-DIVERGENCE");
    } else {
        mga2d_write(key, "FAIL");
        ++mga2d_failures;
    }
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
        blocks > MGA2D_FB_MAP_BYTES_2164W / MGA2D_VBE_BLOCK_BYTES) {
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
    DWORD range_count = 0ul;
    DWORD width = 0ul;
    DWORD height = 0ul;
    DWORD bits = 0ul;
    DWORD bar0;
    DWORD bar1;
    DWORD control;
    DWORD framebuffer;
    DWORD control_bytes;
    DWORD framebuffer_bytes;
    DWORD fb_map_bytes;
    DWORD vbe_bytes;
    DWORD option_mib;
    DWORD stride;
    DWORD desktop_guard;
    DWORD index;
    int no_setup;
    int trapezoids;
    int depth;
    int textures;
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
    trapezoids = mga2d_find(command_line, "/tri") != 0;
    depth = mga2d_find(command_line, "/depth") != 0;
    textures = mga2d_find(command_line, "/tex") != 0;
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
    mga2d_write("Mode", textures ? "textures" : depth ? "depth"
                        : trapezoids ? "trapezoids" : "fill-copy");
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
    mga2d_zbefore = (DWORD (*)[MGA2D_WIN_COLS])VirtualAlloc(
        0, sizeof(mga2d_image), MEM_COMMIT, PAGE_READWRITE);
    mga2d_zexpected = (DWORD (*)[MGA2D_WIN_COLS])VirtualAlloc(
        0, sizeof(mga2d_image), MEM_COMMIT, PAGE_READWRITE);
    mga2d_zactual = (DWORD (*)[MGA2D_WIN_COLS])VirtualAlloc(
        0, sizeof(mga2d_image), MEM_COMMIT, PAGE_READWRITE);
    if (mga2d_before == 0 || mga2d_expected == 0 || mga2d_actual == 0 ||
        mga2d_zbefore == 0 || mga2d_zexpected == 0 || mga2d_zactual == 0) {
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
            mga2d_refuse("no-102b-0519-or-051b-on-pci");
        } else {
            mga2d_refuse("bar-unmappable");
        }
        goto close;
    }
    bar0 = mga2d_result_buffer.bar0 & 0xfffffff0ul;
    bar1 = mga2d_result_buffer.bar1 & 0xfffffff0ul;
    mga2d_write_hex("PciId", mga2d_result_buffer.pci_id);
    mga2d_write_hex("Bar0", bar0);
    mga2d_write_hex("Bar1", bar1);

    /* The control aperture and the framebuffer by chip: BAR0 and BAR1 on
     * the 2064W, the other way round on the 2164W. */
    mga2d_is_2164w = mga2d_result_buffer.pci_id == MGA2D_PCI_ID_2164W;
    mga2d_write("Chip", mga2d_is_2164w ? "MGA-2164W" : "MGA-2064W");
    control = mga2d_is_2164w ? bar1 : bar0;
    framebuffer = mga2d_is_2164w ? bar0 : bar1;
    fb_map_bytes = mga2d_is_2164w ? MGA2D_FB_MAP_BYTES_2164W
                                  : MGA2D_FB_MAP_BYTES_2064W;

    /* The live BARs must be the ranges Configuration Manager allocated, and
     * the control aperture MGABASE1's 16 KiB. */
    control_bytes = mga2d_range_bytes(ranges, range_count, control);
    framebuffer_bytes = mga2d_range_bytes(ranges, range_count, framebuffer);
    mga2d_write_hex("ControlBytes", control_bytes);
    mga2d_write_hex("FramebufferBytes", framebuffer_bytes);
    if (control_bytes != MGA2D_CONTROL_BYTES) {
        mga2d_refuse("control-range-not-16k");
        goto close;
    }
    if (framebuffer_bytes < MGA2D_VRAM_MIN_MIB * MGA2D_MIB ||
        framebuffer_bytes > fb_map_bytes) {
        mga2d_refuse("framebuffer-range");
        goto close;
    }

    /*
     * VRAM: the framebuffer range is the aperture, not the memory - 86Box
     * gives a 4 MiB card the same 8 MiB BAR and wraps addresses into it -
     * so it is capped by the BIOS's figure from V9XBOOT.INI, or by /vram:N.
     * With neither, the region could alias into the desktop, so the probe
     * stops.
     */
    mga2d_vram_bytes = framebuffer_bytes;
    vbe_bytes = mga2d_vbe_vram_bytes();
    mga2d_write_hex("VbeVramBytes", vbe_bytes);
    if (vbe_bytes == 0ul && !have_option) {
        mga2d_refuse("vram-unknown-pass-vram-option");
        goto close;
    }
    /* /vram:N wins over the BIOS when given, in either direction: a 2164W
     * BIOS was measured reporting 4 MiB of 8 (docs\issues\
     * 2026-10-10-mga2164w-vbe-reports-half-its-memory.md). It is still
     * capped by the framebuffer range. */
    if (vbe_bytes != 0ul && vbe_bytes < mga2d_vram_bytes) {
        mga2d_vram_bytes = vbe_bytes;
    }
    if (have_option && option_mib * MGA2D_MIB <= framebuffer_bytes) {
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
        if (v9x_mga_build_setup(mga2d_bpp, &mga2d_setup) != V9X_STATUS_OK) {
            mga2d_refuse("setup-build");
            goto close;
        }
        mga2d_report_writes("Setup", &mga2d_setup);
        if (!mga2d_run_writes("Setup", &mga2d_setup)) {
            mga2d_write("Result", "FAIL");
            mga2d_write("Reason", mga2d_engine_dead ? "setup-engine-timeout"
                                                    : "setup-run-failed");
            goto close;
        }
        mga2d_write("Setup", "emitted");
    }

    if (textures) {
        for (index = 0ul; index < MGA2D_TEX_CASE_COUNT; ++index) {
            mga2d_tex_test(&mga2d_tex_cases[index], index);
            if (mga2d_engine_dead) {
                break;
            }
        }
    } else if (depth) {
        for (index = 0ul; index < MGA2D_DEPTH_CASE_COUNT; ++index) {
            mga2d_depth_test(&mga2d_depth_cases[index], index);
            if (mga2d_engine_dead) {
                break;
            }
        }
        /* The cases leave MACCESS with whatever Z width the last one set;
         * the setup puts the driver's back. */
        if (!mga2d_engine_dead && mga2d_setup.count != 0ul) {
            mga2d_run_writes("Restore", &mga2d_setup);
        }
    } else if (trapezoids) {
        for (index = 0ul; index < MGA2D_TRI_CASE_COUNT; ++index) {
            mga2d_tri_test(&mga2d_tri_cases[index]);
            if (mga2d_engine_dead) {
                break;
            }
        }
    } else {
        for (index = 0ul; index < MGA2D_CASE_COUNT; ++index) {
            mga2d_test(&mga2d_cases[index]);
            if (mga2d_engine_dead) {
                break;
            }
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
