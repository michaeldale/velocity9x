/*
 * ATIRX.EXE: Rage IIC (1002:4757) private engine scenes, Phase 2 of
 * docs\plans\ati-rage-iic-hardware-3d.md. Publishes C:\V9XDIAG\ATIRX.TXT.
 *
 * ATIRX.VXD maps the register window (after CONFIG_CHIP_ID names 4757) and
 * the framebuffer aperture and returns their linear addresses; this program
 * does every engine write from ring 3 through them, as V9XHAL.DLL does. The
 * register streams come from the host-tested builders, compiled in below:
 * mach64_engine.c for the FIFO, idle, known state and reset, and
 * rage2_trap.c for the trapezoid. Nothing in this file encodes a register.
 *
 * The safety contract of the plan, concretely:
 *   - nothing is written unless CONFIG_CHIP_ID reads exactly 7A004757 and
 *     the desktop ends below the scene block;
 *   - the target is a 64x64 16-bpp block at VRAM 2 MiB with a 4 KiB guard
 *     band either side; the scissor lies inside the block, so the block's
 *     own border is a guard as well;
 *   - every batch is written to the report and flushed before it executes;
 *   - FIFO and idle waits are bounded; one timeout gets one reset and
 *     replay, and ends the run.
 *
 * The scenes measure, they do not assume: each draws one flat trapezoid
 * from raw Bresenham terms and the report gives, per row, the runs of
 * pixels the engine wrote.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/ati_rage2.h"

/* The pure builders, linked by inclusion like the host tests' fixtures. */
#include "../../src/chipsets/ati/mach64_engine.c"
#include "../../src/chipsets/ati/rage2_trap.c"
#include "../../src/chipsets/ati/rage2_setup.c"
/* The host test's reference rasteriser: the card is judged by the same
 * function as the model. */
#include "../../tests/host/rage2_reference.c"

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

#define ATIRX_MAGIC          0x58524941ul
#define ATIRX_CHIP_ID        0x7a004757ul
#define ATIRX_STATUS_READY   0x0000000ful

/* The scene block and its guards, in VRAM byte offsets. */
#define ATIRX_BLOCK_OFFSET   0x00200000ul
#define ATIRX_BLOCK_PITCH    128ul          /* 64 pixels */
#define ATIRX_BLOCK_SIZE     64ul
#define ATIRX_GUARD_BYTES    0x00001000ul
#define ATIRX_SCISSOR_LO     8ul
#define ATIRX_SCISSOR_HI     55ul
#define ATIRX_SENTINEL       0x5aa5u
#define ATIRX_GUARD_WORD     0xa55au
#define ATIRX_COLOR          0xf800u
#define ATIRX_SPINS          0x00200000ul
#define ATIRX_CHUNK          8ul

struct atirx_map {
    DWORD magic;
    DWORD status;
    DWORD pci_address;
    DWORD chip_id;
    DWORD mmio_linear;
    DWORD fb_linear;
    DWORD fb_bytes;
    DWORD reserved;
};

struct atirx_scene {
    const char *name;
    struct v9x_r2_flat_trap trap;
};

static HANDLE atirx_out = INVALID_HANDLE_VALUE;
static volatile BYTE *atirx_mmio;
static volatile WORD *atirx_fb;

/* ---- report ------------------------------------------------------------ */

static void atirx_text(const char *text)
{
    DWORD written;

    WriteFile(atirx_out, text, (DWORD)lstrlenA(text), &written, 0);
}

static void atirx_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15u];
    }
    text[digits] = '\0';
}

static void atirx_decimal(char *text, DWORD value)
{
    char reverse[12];
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

static void atirx_key_hex(const char *key, DWORD value)
{
    char text[12];

    atirx_text(key);
    atirx_text("=0x");
    atirx_hex(text, value, 8);
    atirx_text(text);
    atirx_text("\r\n");
}

static void atirx_key_dec(const char *key, DWORD value)
{
    char text[12];

    atirx_text(key);
    atirx_text("=");
    atirx_decimal(text, value);
    atirx_text(text);
    atirx_text("\r\n");
}

static void atirx_key(const char *key, const char *value)
{
    atirx_text(key);
    atirx_text("=");
    atirx_text(value);
    atirx_text("\r\n");
}

static void atirx_flush(void)
{
    FlushFileBuffers(atirx_out);
}

/* ---- the engine through the mapped window ------------------------------ */

static v9x_u32 atirx_read(void *context, v9x_u32 offset)
{
    (void)context;
    return (v9x_u32)*(volatile DWORD *)(atirx_mmio + offset);
}

static void atirx_write(void *context, v9x_u32 offset, v9x_u32 value)
{
    (void)context;
    *(volatile DWORD *)(atirx_mmio + offset) = (DWORD)value;
}

/* Record a batch, flush it, then emit it in FIFO-checked chunks. */
static v9x_status atirx_emit(struct v9x_m64_engine *engine, const char *label,
                             const v9x_u32 *offsets, const v9x_u32 *values,
                             v9x_u32 count)
{
    char text[12];
    v9x_u32 index;
    v9x_u32 done;
    v9x_u32 chunk;
    v9x_status status;

    for (index = 0ul; index < count; ++index) {
        atirx_text("W ");
        atirx_text(label);
        atirx_text(" +");
        atirx_hex(text, offsets[index], 3);
        atirx_text(text);
        atirx_text(" ");
        atirx_hex(text, values[index], 8);
        atirx_text(text);
        atirx_text("\r\n");
    }
    atirx_flush();
    for (done = 0ul; done < count; done += chunk) {
        chunk = count - done;
        if (chunk > ATIRX_CHUNK) {
            chunk = ATIRX_CHUNK;
        }
        status = v9x_m64_emit_batch(engine, offsets + done, values + done,
                                    chunk, ATIRX_SPINS);
        if (status != V9X_STATUS_OK) {
            return status;
        }
    }
    return V9X_STATUS_OK;
}

/* ---- the block --------------------------------------------------------- */

static volatile WORD *atirx_pixel(DWORD x, DWORD y)
{
    return atirx_fb + (ATIRX_BLOCK_OFFSET + y * ATIRX_BLOCK_PITCH) / 2ul + x;
}

static void atirx_prepare_block(void)
{
    DWORD index;
    DWORD x;
    DWORD y;
    volatile WORD *guard;

    guard = atirx_fb + (ATIRX_BLOCK_OFFSET - ATIRX_GUARD_BYTES) / 2ul;
    for (index = 0ul; index < ATIRX_GUARD_BYTES / 2ul; ++index) {
        guard[index] = ATIRX_GUARD_WORD;
    }
    guard = atirx_fb + (ATIRX_BLOCK_OFFSET +
                        ATIRX_BLOCK_SIZE * ATIRX_BLOCK_PITCH) / 2ul;
    for (index = 0ul; index < ATIRX_GUARD_BYTES / 2ul; ++index) {
        guard[index] = ATIRX_GUARD_WORD;
    }
    for (y = 0ul; y < ATIRX_BLOCK_SIZE; ++y) {
        for (x = 0ul; x < ATIRX_BLOCK_SIZE; ++x) {
            *atirx_pixel(x, y) = ATIRX_SENTINEL;
        }
    }
}

static int atirx_inside_scissor(DWORD x, DWORD y)
{
    return x >= ATIRX_SCISSOR_LO && x <= ATIRX_SCISSOR_HI &&
           y >= ATIRX_SCISSOR_LO && y <= ATIRX_SCISSOR_HI;
}

/* Per row, the runs of pixels equal to the colour; everything else counted. */
static void atirx_report_block(const char *prefix)
{
    char key[48];
    char text[16];
    char line[256];
    DWORD x;
    DWORD y;
    DWORD drawn = 0ul;
    DWORD other = 0ul;
    DWORD outside = 0ul;
    DWORD guard_bad = 0ul;
    DWORD index;
    volatile WORD *guard;

    for (y = 0ul; y < ATIRX_BLOCK_SIZE; ++y) {
        DWORD start = 0xfffffffful;

        line[0] = '\0';
        for (x = 0ul; x <= ATIRX_BLOCK_SIZE; ++x) {
            WORD value = x < ATIRX_BLOCK_SIZE ? *atirx_pixel(x, y)
                                              : ATIRX_SENTINEL;
            int is_color = x < ATIRX_BLOCK_SIZE && value == ATIRX_COLOR;

            if (x < ATIRX_BLOCK_SIZE && value != ATIRX_SENTINEL) {
                if (is_color) {
                    ++drawn;
                } else {
                    ++other;
                }
                if (!atirx_inside_scissor(x, y)) {
                    ++outside;
                }
            }
            if (is_color && start == 0xfffffffful) {
                start = x;
            }
            if (!is_color && start != 0xfffffffful) {
                if (lstrlenA(line) < 220) {
                    atirx_decimal(text, start);
                    lstrcatA(line, text);
                    lstrcatA(line, "-");
                    atirx_decimal(text, x - 1ul);
                    lstrcatA(line, text);
                    lstrcatA(line, " ");
                }
                start = 0xfffffffful;
            }
        }
        if (line[0] != '\0') {
            lstrcpyA(key, prefix);
            lstrcatA(key, "Row");
            atirx_decimal(text, y);
            lstrcatA(key, text);
            atirx_key(key, line);
        }
    }

    guard = atirx_fb + (ATIRX_BLOCK_OFFSET - ATIRX_GUARD_BYTES) / 2ul;
    for (index = 0ul; index < ATIRX_GUARD_BYTES / 2ul; ++index) {
        if (guard[index] != ATIRX_GUARD_WORD) {
            ++guard_bad;
        }
    }
    guard = atirx_fb + (ATIRX_BLOCK_OFFSET +
                        ATIRX_BLOCK_SIZE * ATIRX_BLOCK_PITCH) / 2ul;
    for (index = 0ul; index < ATIRX_GUARD_BYTES / 2ul; ++index) {
        if (guard[index] != ATIRX_GUARD_WORD) {
            ++guard_bad;
        }
    }
    lstrcpyA(key, prefix);
    lstrcatA(key, "Drawn");
    atirx_key_dec(key, drawn);
    lstrcpyA(key, prefix);
    lstrcatA(key, "OtherValue");
    atirx_key_dec(key, other);
    lstrcpyA(key, prefix);
    lstrcatA(key, "OutsideScissor");
    atirx_key_dec(key, outside);
    lstrcpyA(key, prefix);
    lstrcatA(key, "GuardMismatches");
    atirx_key_dec(key, guard_bad);
}

/* ---- Config Manager: the assigned BARs --------------------------------- */

static int atirx_starts_with_ci(const char *text, const char *prefix)
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

static int atirx_has_switch(const char *command_line, const char *name)
{
    if (command_line == 0) {
        return 0;
    }
    while (*command_line != '\0') {
        if (atirx_starts_with_ci(command_line, name)) {
            return 1;
        }
        ++command_line;
    }
    return 0;
}

static DWORD atirx_u32(const BYTE *data)
{
    return (DWORD)data[0] | ((DWORD)data[1] << 8) |
           ((DWORD)data[2] << 16) | ((DWORD)data[3] << 24);
}

static LONG atirx_find_device(char *device_id, DWORD capacity)
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
            !atirx_starts_with_ci(adapter, "VEN_1002&DEV_4757")) {
            continue;
        }
        if (RegOpenKeyExA(pci_key, adapter, 0, KEY_READ, &adapter_key) !=
            ERROR_SUCCESS) {
            continue;
        }
        length = sizeof(instance);
        status = RegEnumKeyExA(adapter_key, 0, instance, &length, 0, 0, 0, 0);
        RegCloseKey(adapter_key);
        if (status == ERROR_SUCCESS && capacity > 0u) {
            lstrcpynA(device_id, "PCI\\", (int)capacity);
            lstrcatA(device_id, adapter);
            lstrcatA(device_id, "\\");
            lstrcatA(device_id, instance);
            RegCloseKey(pci_key);
            return ERROR_SUCCESS;
        }
    }
    RegCloseKey(pci_key);
    return ERROR_FILE_NOT_FOUND;
}

static CONFIGRET atirx_bars(DWORD *bar0, DWORD *bar2)
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

    *bar0 = 0u;
    *bar2 = 0u;
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
        cm_size == 0 || cm_data == 0 || cm_free_log == 0 ||
        atirx_find_device(device_id, sizeof(device_id)) != ERROR_SUCCESS) {
        FreeLibrary(module);
        return CR_NO_SUCH_DEVNODE;
    }
    status = cm_locate(&device, device_id, CM_LOCATE_DEVNODE_NORMAL);
    if (status == CR_SUCCESS) {
        status = cm_first(&logical_config, device, ALLOC_LOG_CONF);
    }
    if (status != CR_SUCCESS) {
        FreeLibrary(module);
        return status;
    }
    current = (RES_DES)logical_config;
    for (;;) {
        BYTE data[256];
        ULONG size = 0u;
        DWORD base;
        DWORD end;

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
        if (cm_size(&size, current, 0) != CR_SUCCESS || size < 24u ||
            size > sizeof(data) ||
            cm_data(current, data, size, 0) != CR_SUCCESS) {
            status = CR_FAILURE;
            break;
        }
        base = atirx_u32(data + 8);
        end = atirx_u32(data + 16);
        if (atirx_u32(data + 12) == 0u && atirx_u32(data + 20) == 0u &&
            end >= base) {
            if (end - base + 1u == 0x01000000ul) {
                *bar0 = base;
            }
            if (end - base + 1u == 0x00001000ul) {
                *bar2 = base;
            }
        }
    }
    if (current != (RES_DES)logical_config) {
        cm_free_res(current);
    }
    cm_free_log(logical_config);
    FreeLibrary(module);
    return status;
}

/* ---- scenes ------------------------------------------------------------ */

#define ATIRX_CNTL_BASE (V9X_M64_DST_X_DIR | V9X_M64_DST_Y_DIR | \
                         V9X_R2_TRAIL_X_DIR | V9X_R2_TRAP_FILL_DIR)

/*
 * Terms chosen so each edge's behaviour is unambiguous under the RRG's
 * stepping rule (negative error: axial step, add INC; otherwise diagonal
 * step, add DEC): -1 with INC 0 never leaves the axial case, and 0 with
 * DEC 0 never leaves the diagonal case.
 */
static const struct atirx_scene atirx_scenes[] = {
    /* Both edges vertical, Y major: a rectangle if the model is right. */
    { "S1VertYMajor",
      { 16ul, 16ul, 8ul, 32ul, -1l, 0l, -1l, -1l, 0l, -1l,
        ATIRX_CNTL_BASE | V9X_R2_DST_Y_MAJOR } },
    /* The same without Y_MAJOR: does the flag apply to the leading edge? */
    { "S2VertNoYMajor",
      { 16ul, 16ul, 8ul, 32ul, -1l, 0l, -1l, -1l, 0l, -1l,
        ATIRX_CNTL_BASE } },
    /* Leading edge diagonal every step, trailing vertical. */
    { "S3LeadDiagonal",
      { 16ul, 16ul, 8ul, 40ul, 0l, 0l, 0l, -1l, 0l, -1l,
        ATIRX_CNTL_BASE | V9X_R2_DST_Y_MAJOR } },
    /* Leading vertical, trailing diagonal every step. */
    { "S4TrailDiagonal",
      { 16ul, 16ul, 8ul, 32ul, -1l, 0l, -1l, 0l, 0l, 0l,
        ATIRX_CNTL_BASE | V9X_R2_DST_Y_MAJOR } },
    /* Trailing edge left of the leading edge: TRAP_FILL_DIR clear. */
    { "S5FillLeft",
      { 40ul, 16ul, 8ul, 24ul, -1l, 0l, -1l, -1l, 0l, -1l,
        V9X_M64_DST_X_DIR | V9X_M64_DST_Y_DIR | V9X_R2_TRAIL_X_DIR |
        V9X_R2_DST_Y_MAJOR } }
};

/*
 * Set 2, after set 1 (boot 135) showed length counts scanlines, spans run
 * from the leading pixel to the pixel before the trailing one, and an
 * error of 0 with DEC 0 does not step an edge. Classic self-terminating
 * Bresenham terms only, for a Y-major edge of dx over dy rows:
 * ERR = 2dx - dy, INC = 2dx, DEC = 2(dx - dy), so DEC <= 0 always pulls
 * the error back and at most one X step happens per row.
 */
static const struct atirx_scene atirx_scenes_2[] = {
    /* Leading edge 4 right over 8 rows, Y major. */
    { "A1LeadHalfYMajor",
      { 16ul, 16ul, 8ul, 40ul, 0l, 8l, -8l, -1l, 0l, -1l,
        ATIRX_CNTL_BASE | V9X_R2_DST_Y_MAJOR } },
    /* The same without Y_MAJOR. */
    { "A2LeadHalfNoYMajor",
      { 16ul, 16ul, 8ul, 40ul, 0l, 8l, -8l, -1l, 0l, -1l,
        ATIRX_CNTL_BASE } },
    /* Leading edge 45 degrees: dx = dy = 8. */
    { "A3LeadFull",
      { 16ul, 16ul, 8ul, 40ul, 8l, 16l, 0l, -1l, 0l, -1l,
        ATIRX_CNTL_BASE | V9X_R2_DST_Y_MAJOR } },
    /* Trailing edge 4 right over 8 rows. */
    { "A4TrailHalf",
      { 16ul, 16ul, 8ul, 32ul, -1l, 0l, -1l, 0l, 8l, -8l,
        ATIRX_CNTL_BASE | V9X_R2_DST_Y_MAJOR } },
    /* Leading edge 4 left over 8 rows: DST_X_DIR clear. */
    { "A5LeadHalfLeft",
      { 24ul, 16ul, 8ul, 40ul, 0l, 8l, -8l, -1l, 0l, -1l,
        V9X_M64_DST_Y_DIR | V9X_R2_TRAIL_X_DIR | V9X_R2_TRAP_FILL_DIR |
        V9X_R2_DST_Y_MAJOR } },
    /* Leading edge X major, 16 right over 8 rows: ERR = 2dy - dx,
     * INC = 2dy, DEC = 2(dy - dx). Y_MAJOR clear. */
    { "A6LeadXMajor",
      { 16ul, 16ul, 8ul, 48ul, 0l, 16l, -16l, -1l, 0l, -1l,
        ATIRX_CNTL_BASE } }
};

/*
 * Set 3 tests one model fitted to sets 1-2 (boot 135): per row the edge
 * adds DEC to its error, and if the result is negative it steps X once and
 * adds INC. Each scene states what that model predicts for the leading
 * edge's X on rows 16..23 (start 16, moving right). Every scene's error
 * returns to non-negative within a step or diverges negative at one step
 * per row, so no scene can run an edge more than 16 pixels.
 */
static const struct atirx_scene atirx_scenes_3[] = {
    /* -1-8 < 0 every row: 17..24. */
    { "B1ErrMinus1",
      { 16ul, 16ul, 8ul, 48ul, -1l, 8l, -8l, -1l, 0l, -1l,
        ATIRX_CNTL_BASE } },
    /* INC 0: error diverges negative, still one step a row: 17..24. */
    { "B2IncZero",
      { 16ul, 16ul, 8ul, 48ul, 0l, 0l, -8l, -1l, 0l, -1l,
        ATIRX_CNTL_BASE } },
    /* DEC 0: never negative, never steps: 16 throughout. */
    { "B3DecZero",
      { 16ul, 16ul, 8ul, 48ul, 0l, 8l, 0l, -1l, 0l, -1l,
        ATIRX_CNTL_BASE } },
    /* DDA 2 over 8: errors 5 3 1 -1 / 5 3 1 -1: steps on rows 19 and 23,
     * so 16 16 16 17 17 17 17 18. */
    { "B4Dda2of8a",
      { 16ul, 16ul, 8ul, 48ul, 7l, 8l, -2l, -1l, 0l, -1l,
        ATIRX_CNTL_BASE } },
    /* The same from error 3: steps on rows 17 and 21: 16 17 17 17 17 18
     * 18 18. */
    { "B5Dda2of8b",
      { 16ul, 16ul, 8ul, 48ul, 3l, 8l, -2l, -1l, 0l, -1l,
        ATIRX_CNTL_BASE } },
    /* Zero after the add: 8-8 = 0 on row 16. Non-negative zero: no step
     * then, steps on every later row: 16 17 18 .. 23. */
    { "B6ZeroSign",
      { 16ul, 16ul, 8ul, 48ul, 8l, 8l, -8l, -1l, 0l, -1l,
        ATIRX_CNTL_BASE } },
    /* The same with DST_BRES_SIGN, which the RRG says makes a zero
     * negative: if so, steps on every row: 17 .. 24. */
    { "B7ZeroSignBit",
      { 16ul, 16ul, 8ul, 48ul, 8l, 8l, -8l, -1l, 0l, -1l,
        ATIRX_CNTL_BASE | V9X_R2_DST_BRES_SIGN } },
    /* -16 then +8 is still negative: one step a row gives 17 .. 24; an
     * engine that steps until non-negative gives two a row, 18 .. 32. */
    { "B8TwoPerRow",
      { 16ul, 16ul, 8ul, 48ul, 0l, 8l, -16l, -1l, 0l, -1l,
        ATIRX_CNTL_BASE } },
    /* The trailing edge on the same DDA as B4: its exclusive end 32 32 32
     * 33 33 33 33 34, so spans end 31 31 31 32 32 32 32 33. */
    { "B9TrailDda",
      { 16ul, 16ul, 8ul, 32ul, -1l, 0l, -1l, 7l, 8l, -2l,
        ATIRX_CNTL_BASE } }
};

/* ---- Phase 3: triangles through rage2_setup.c -------------------------- */

#define ATIRX_P(n) ((v9x_s32)(n) * V9X_R2_SUBPIXEL)
#define ATIRX_TRI_RANDOM 64u
#define ATIRX_TRI_LO     ATIRX_P(ATIRX_SCISSOR_LO)
#define ATIRX_TRI_SPAN   ((ATIRX_SCISSOR_HI + 1ul - ATIRX_SCISSOR_LO) * 16ul)

/* Named triangles, all inside the scissor (pixels 8..55), in 1/16 pixel.
 * The same shapes as the host test's, moved into the scissor. */
static const v9x_s32 atirx_named[][6] = {
    { ATIRX_P(10), ATIRX_P(10), ATIRX_P(40), ATIRX_P(12), ATIRX_P(20), ATIRX_P(40) },
    { ATIRX_P(10), ATIRX_P(10), ATIRX_P(20), ATIRX_P(40), ATIRX_P(40), ATIRX_P(12) },
    { ATIRX_P(8), ATIRX_P(8), ATIRX_P(48), ATIRX_P(8), ATIRX_P(28), ATIRX_P(40) },
    { ATIRX_P(28), ATIRX_P(8), ATIRX_P(8), ATIRX_P(40), ATIRX_P(48), ATIRX_P(40) },
    { ATIRX_P(16), ATIRX_P(16), ATIRX_P(16), ATIRX_P(48), ATIRX_P(48), ATIRX_P(48) },
    { ATIRX_P(48), ATIRX_P(16), ATIRX_P(48), ATIRX_P(48), ATIRX_P(16), ATIRX_P(48) },
    { ATIRX_P(8) + 8, ATIRX_P(8) + 8, ATIRX_P(40) + 8, ATIRX_P(20) + 8,
      ATIRX_P(12) + 8, ATIRX_P(44) + 8 },
    { ATIRX_P(9), ATIRX_P(9), ATIRX_P(10), ATIRX_P(54), ATIRX_P(9) + 3, ATIRX_P(30) },
    { ATIRX_P(9), ATIRX_P(30), ATIRX_P(54), ATIRX_P(31), ATIRX_P(30), ATIRX_P(30) + 5 },
    { ATIRX_P(8), ATIRX_P(8), ATIRX_P(56), ATIRX_P(8), ATIRX_P(8), ATIRX_P(56) },
    { ATIRX_P(56), ATIRX_P(56), ATIRX_P(8), ATIRX_P(56), ATIRX_P(56), ATIRX_P(8) },
    { ATIRX_P(10) + 3, ATIRX_P(9) + 11, ATIRX_P(51) + 13, ATIRX_P(19) + 2,
      ATIRX_P(23) + 7, ATIRX_P(54) + 9 }
};

static DWORD atirx_lcg = 0x2545f491ul;

static v9x_s32 atirx_random_coord(void)
{
    atirx_lcg = atirx_lcg * 1103515245ul + 12345ul;
    return ATIRX_TRI_LO + (v9x_s32)((atirx_lcg >> 8) % (ATIRX_TRI_SPAN + 1ul));
}

/* Draw one triangle; compare every block pixel with the reference. */
static int atirx_triangle(struct v9x_m64_engine *engine,
                          const struct v9x_r2_target *target,
                          const struct v9x_r2_vertex *v, UINT index,
                          DWORD *total_mismatch)
{
    struct v9x_r2_flat_trap traps[V9X_R2_SETUP_TRAPS];
    v9x_u32 offsets[V9X_M64_ENGINE_INIT_GT_DWORDS];
    v9x_u32 values[V9X_M64_ENGINE_INIT_GT_DWORDS];
    v9x_u32 written;
    v9x_u32 count = 0ul;
    v9x_u32 trap;
    v9x_status status;
    DWORD x;
    DWORD y;
    DWORD drawn = 0ul;
    DWORD expected = 0ul;
    DWORD mismatch = 0ul;
    DWORD other = 0ul;
    char key[48];
    char text[16];
    char line[200];
    char label[16];

    lstrcpyA(label, "T");
    atirx_decimal(text, index);
    lstrcatA(label, text);

    atirx_prepare_block();
    if (v9x_r2_setup_triangle(target, v, traps, &count) != V9X_STATUS_OK ||
        v9x_r2_build_flat_state(target, ATIRX_COLOR, offsets, values,
                                V9X_M64_ENGINE_INIT_GT_DWORDS,
                                &written) != V9X_STATUS_OK) {
        atirx_key(label, "SETUP-REFUSED");
        return 0;
    }
    status = atirx_emit(engine, label, offsets, values, written);
    for (trap = 0ul; trap < count && status == V9X_STATUS_OK; ++trap) {
        if (v9x_r2_build_trap(target, &traps[trap], offsets, values,
                              V9X_M64_ENGINE_INIT_GT_DWORDS,
                              &written) != V9X_STATUS_OK) {
            atirx_key(label, "TRAP-REFUSED");
            return 0;
        }
        status = atirx_emit(engine, label, offsets, values, written);
    }
    if (status == V9X_STATUS_OK) {
        status = v9x_m64_wait_idle(engine, ATIRX_SPINS);
    }
    if (status != V9X_STATUS_OK) {
        atirx_key_hex("FailStatus", (DWORD)status);
        atirx_key_hex("FailGuiStat", atirx_read(0, V9X_M64_GUI_STAT));
        atirx_key_hex("FailFifoStat", atirx_read(0, V9X_M64_FIFO_STAT));
        atirx_flush();
        status = v9x_m64_reset_replay(engine, ATIRX_SPINS);
        atirx_key_hex("ResetStatus", (DWORD)status);
        atirx_key("Result", "TIMEOUT-STOPPED");
        atirx_flush();
        return 0;
    }
    (void)v9x_m64_cpu_read_barrier(engine, ATIRX_SPINS);

    line[0] = '\0';
    for (y = 0ul; y < ATIRX_BLOCK_SIZE; ++y) {
        for (x = 0ul; x < ATIRX_BLOCK_SIZE; ++x) {
            WORD value = *atirx_pixel(x, y);
            int want = v9x_r2_ref_covers(v, (v9x_s32)x, (v9x_s32)y);
            int got = value == ATIRX_COLOR;

            if (value != ATIRX_COLOR && value != ATIRX_SENTINEL) {
                ++other;
            }
            if (got) {
                ++drawn;
            }
            if (want) {
                ++expected;
            }
            if (got != want) {
                ++mismatch;
                if (lstrlenA(line) < 160) {
                    lstrcatA(line, got ? "+" : "-");
                    atirx_decimal(text, x);
                    lstrcatA(line, text);
                    lstrcatA(line, ",");
                    atirx_decimal(text, y);
                    lstrcatA(line, text);
                    lstrcatA(line, " ");
                }
            }
        }
    }
    lstrcpyA(key, label);
    lstrcatA(key, "_Traps");
    atirx_key_dec(key, count);
    lstrcpyA(key, label);
    lstrcatA(key, "_Drawn");
    atirx_key_dec(key, drawn);
    lstrcpyA(key, label);
    lstrcatA(key, "_Expected");
    atirx_key_dec(key, expected);
    lstrcpyA(key, label);
    lstrcatA(key, "_Mismatches");
    atirx_key_dec(key, mismatch);
    if (other != 0ul) {
        lstrcpyA(key, label);
        lstrcatA(key, "_OtherValue");
        atirx_key_dec(key, other);
    }
    if (line[0] != '\0') {
        lstrcpyA(key, label);
        lstrcatA(key, "_First");
        atirx_key(key, line);
    }
    *total_mismatch += mismatch + other;
    atirx_flush();
    return 1;
}

static int atirx_run_triangles(struct v9x_m64_engine *engine,
                               const struct v9x_r2_target *target)
{
    struct v9x_r2_vertex v[3];
    DWORD mismatches = 0ul;
    UINT index;
    UINT run = 0u;
    UINT named = sizeof(atirx_named) / sizeof(atirx_named[0]);
    DWORD guard_before = 0ul;
    DWORD offset;
    volatile WORD *guard;

    atirx_key("SceneSet", "triangles");
    for (index = 0u; index < named + ATIRX_TRI_RANDOM; ++index) {
        if (index < named) {
            v[0].x = atirx_named[index][0];
            v[0].y = atirx_named[index][1];
            v[1].x = atirx_named[index][2];
            v[1].y = atirx_named[index][3];
            v[2].x = atirx_named[index][4];
            v[2].y = atirx_named[index][5];
        } else {
            v[0].x = atirx_random_coord();
            v[0].y = atirx_random_coord();
            v[1].x = atirx_random_coord();
            v[1].y = atirx_random_coord();
            v[2].x = atirx_random_coord();
            v[2].y = atirx_random_coord();
        }
        if (!atirx_triangle(engine, target, v, index, &mismatches)) {
            break;
        }
        ++run;

        /* The guards after every triangle, not once at the end. */
        guard = atirx_fb + (ATIRX_BLOCK_OFFSET - ATIRX_GUARD_BYTES) / 2ul;
        for (offset = 0ul; offset < ATIRX_GUARD_BYTES / 2ul; ++offset) {
            if (guard[offset] != ATIRX_GUARD_WORD) {
                ++guard_before;
            }
        }
        guard = atirx_fb + (ATIRX_BLOCK_OFFSET +
                            ATIRX_BLOCK_SIZE * ATIRX_BLOCK_PITCH) / 2ul;
        for (offset = 0ul; offset < ATIRX_GUARD_BYTES / 2ul; ++offset) {
            if (guard[offset] != ATIRX_GUARD_WORD) {
                ++guard_before;
            }
        }
    }
    atirx_key_dec("TrianglesRun", run);
    atirx_key_dec("TotalMismatches", mismatches);
    atirx_key_dec("GuardMismatches", guard_before);
    atirx_key("Result", run == named + ATIRX_TRI_RANDOM
                        ? (mismatches == 0ul && guard_before == 0ul
                           ? "TRIANGLES-MATCH" : "TRIANGLES-DIFFER")
                        : "TRIANGLES-STOPPED");
    return run == named + ATIRX_TRI_RANDOM;
}

static void atirx_prefix(char *prefix, const char *name)
{
    lstrcpyA(prefix, name);
    lstrcatA(prefix, "_");
}

void WINAPI V9xAtiRage2SceneEntry(void)
{
    struct atirx_map map;
    struct v9x_m64_engine engine;
    struct v9x_m64_io io;
    struct v9x_r2_target target;
    DWORD request[2];
    DWORD returned = 0u;
    HANDLE device;
    HDC display;
    DWORD desktop_end = 0u;
    v9x_u32 offsets[V9X_M64_ENGINE_INIT_GT_DWORDS];
    v9x_u32 values[V9X_M64_ENGINE_INIT_GT_DWORDS];
    v9x_u32 written;
    v9x_status status;
    UINT scene;
    UINT scene_count;
    const struct atirx_scene *scenes;
    char prefix[48];
    static const char header[] = "[AtiRage2Scene]\r\n";

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    atirx_out = CreateFileA(V9X_DIAG_ATIRX_TXT, GENERIC_WRITE,
                            FILE_SHARE_READ, 0, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, 0);
    if (atirx_out == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    atirx_text(header);
    atirx_key("Build", V9X_BUILD_ID);

    if (atirx_bars(&request[0], &request[1]) != CR_SUCCESS ||
        request[0] == 0u || request[1] == 0u) {
        atirx_key("Result", "NO-BARS");
        CloseHandle(atirx_out);
        ExitProcess(5u);
    }
    atirx_key_hex("Bar0", request[0]);
    atirx_key_hex("Bar2", request[1]);

    /* Not FILE_FLAG_DELETE_ON_CLOSE: the VxD stays loaded, so its cached
     * mappings are reused rather than made again on every run. */
    device = CreateFileA("\\\\.\\ATIRX.VXD", 0, 0, 0, CREATE_NEW, 0, 0);
    if (device == INVALID_HANDLE_VALUE ||
        !DeviceIoControl(device, 1u, request, sizeof(request), &map,
                         sizeof(map), &returned, 0) ||
        returned != sizeof(map) || map.magic != ATIRX_MAGIC) {
        atirx_key("Result", "NO-VXD");
        CloseHandle(atirx_out);
        ExitProcess(2u);
    }
    CloseHandle(device);
    atirx_key_hex("MapStatus", map.status);
    atirx_key_hex("ChipId", map.chip_id);
    atirx_key_hex("MmioLinear", map.mmio_linear);
    atirx_key_hex("FbLinear", map.fb_linear);

    display = GetDC(0);
    if (display != 0) {
        desktop_end = (DWORD)GetDeviceCaps(display, HORZRES) *
                      (DWORD)GetDeviceCaps(display, VERTRES) *
                      (DWORD)(GetDeviceCaps(display, BITSPIXEL) / 8);
        ReleaseDC(0, display);
    }
    atirx_key_hex("DesktopBytes", desktop_end);

    if ((map.status & ATIRX_STATUS_READY) != ATIRX_STATUS_READY ||
        map.chip_id != ATIRX_CHIP_ID || map.mmio_linear == 0u ||
        map.fb_linear == 0u || map.fb_bytes < 0x00400000ul ||
        desktop_end == 0u ||
        desktop_end > ATIRX_BLOCK_OFFSET - ATIRX_GUARD_BYTES) {
        atirx_key("Result", "REVIEW-PRECONDITION");
        CloseHandle(atirx_out);
        ExitProcess(1u);
    }
    atirx_mmio = (volatile BYTE *)map.mmio_linear;
    atirx_fb = (volatile WORD *)map.fb_linear;

    io.context = 0;
    io.read = atirx_read;
    io.write = atirx_write;
    if (v9x_m64_engine_init(&engine, &io, V9X_M64_FIFO_PRE_VTB) !=
        V9X_STATUS_OK) {
        atirx_key("Result", "ENGINE-INIT");
        CloseHandle(atirx_out);
        ExitProcess(1u);
    }
    atirx_key_hex("BeforeGuiStat", atirx_read(0, V9X_M64_GUI_STAT));
    atirx_key_hex("BeforeFifoStat", atirx_read(0, V9X_M64_FIFO_STAT));
    if (v9x_m64_wait_idle(&engine, ATIRX_SPINS) != V9X_STATUS_OK) {
        atirx_key("Result", "BUSY-BEFORE-START");
        CloseHandle(atirx_out);
        ExitProcess(1u);
    }

    /* The known state, as the HAL writes it at validate. */
    if (v9x_m64_build_engine_init_gt(offsets, values,
                                     V9X_M64_ENGINE_INIT_GT_DWORDS,
                                     &written) != V9X_STATUS_OK ||
        atirx_emit(&engine, "init", offsets, values, written) !=
        V9X_STATUS_OK) {
        atirx_key("Result", "INIT-EMIT");
        CloseHandle(atirx_out);
        ExitProcess(1u);
    }

    target.offset = ATIRX_BLOCK_OFFSET;
    target.pitch_bytes = ATIRX_BLOCK_PITCH;
    target.width = ATIRX_BLOCK_SIZE;
    target.height = ATIRX_BLOCK_SIZE;
    target.vram_bytes = map.fb_bytes;
    target.scissor_left = ATIRX_SCISSOR_LO;
    target.scissor_top = ATIRX_SCISSOR_LO;
    target.scissor_right = ATIRX_SCISSOR_HI;
    target.scissor_bottom = ATIRX_SCISSOR_HI;

    if (atirx_has_switch(GetCommandLineA(), "/tri")) {
        int completed = atirx_run_triangles(&engine, &target);

        atirx_key_dec("FifoTimeouts", engine.fifo_timeouts);
        atirx_key_dec("IdleTimeouts", engine.idle_timeouts);
        atirx_key_dec("Resets", engine.reset_count);
        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }

    /* /set2 picks the second scene set; the default stays set 1. */
    scenes = atirx_scenes;
    scene_count = sizeof(atirx_scenes) / sizeof(atirx_scenes[0]);
    if (atirx_has_switch(GetCommandLineA(), "/set2")) {
        scenes = atirx_scenes_2;
        scene_count = sizeof(atirx_scenes_2) / sizeof(atirx_scenes_2[0]);
    }
    if (atirx_has_switch(GetCommandLineA(), "/set3")) {
        scenes = atirx_scenes_3;
        scene_count = sizeof(atirx_scenes_3) / sizeof(atirx_scenes_3[0]);
    }
    atirx_key("SceneSet", scenes == atirx_scenes ? "1" :
                          scenes == atirx_scenes_2 ? "2" : "3");
    for (scene = 0u; scene < scene_count; ++scene) {
        const struct atirx_scene *current = &scenes[scene];

        atirx_prefix(prefix, current->name);
        atirx_key("Scene", current->name);
        atirx_prepare_block();

        if (v9x_r2_build_flat_state(&target, ATIRX_COLOR, offsets, values,
                                    V9X_M64_ENGINE_INIT_GT_DWORDS,
                                    &written) != V9X_STATUS_OK) {
            atirx_key("Result", "STATE-BUILD");
            break;
        }
        status = atirx_emit(&engine, current->name, offsets, values, written);
        if (status == V9X_STATUS_OK) {
            if (v9x_r2_build_trap(&target, &current->trap, offsets, values,
                                  V9X_M64_ENGINE_INIT_GT_DWORDS,
                                  &written) != V9X_STATUS_OK) {
                atirx_key("Result", "TRAP-BUILD");
                break;
            }
            status = atirx_emit(&engine, current->name, offsets, values,
                                written);
        }
        if (status == V9X_STATUS_OK) {
            status = v9x_m64_wait_idle(&engine, ATIRX_SPINS);
        }
        if (status != V9X_STATUS_OK) {
            atirx_key_hex("FailStatus", (DWORD)status);
            atirx_key_hex("FailGuiStat", atirx_read(0, V9X_M64_GUI_STAT));
            atirx_key_hex("FailFifoStat", atirx_read(0, V9X_M64_FIFO_STAT));
            atirx_flush();
            status = v9x_m64_reset_replay(&engine, ATIRX_SPINS);
            atirx_key_hex("ResetStatus", (DWORD)status);
            atirx_key("Result", "TIMEOUT-STOPPED");
            atirx_flush();
            break;
        }
        (void)v9x_m64_cpu_read_barrier(&engine, ATIRX_SPINS);
        atirx_key_hex("AfterDstYX", atirx_read(0, V9X_M64_DST_Y_X));
        atirx_key_hex("AfterGuiStat", atirx_read(0, V9X_M64_GUI_STAT));
        atirx_report_block(prefix);
        atirx_flush();
    }
    if (scene == scene_count) {
        atirx_key("Result", "SCENES-RUN");
    }
    atirx_key_dec("FifoTimeouts", engine.fifo_timeouts);
    atirx_key_dec("IdleTimeouts", engine.idle_timeouts);
    atirx_key_dec("Resets", engine.reset_count);
    CloseHandle(atirx_out);
    ExitProcess(scene == scene_count ? 0u : 1u);
}
