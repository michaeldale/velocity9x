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
#include "../../src/chipsets/ati/rage2_draw.c"
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

static void atirx_prefix(char *prefix, const char *name);

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

/* ---- Phase 3: which interpolator bits exist ----------------------------- */

/*
 * The register notes lost the interpolator bit diagrams to the PDF
 * extraction, so the formats are measured: write a pattern, wait idle,
 * read it back. SCALE_3D_CNTL carries a shading function throughout,
 * because the RRG allows accumulator writes only with SCALE_3D_FCN
 * non-zero; nothing here writes DST_BRES_LNTH or DST_HEIGHT_WIDTH, so no
 * draw can start. SCALE_3D_CNTL returns to 0 at the end.
 */
#define ATIRX_SCALE_3D_SHADE 0x000000c0ul   /* SCALE_3D_FCN = 3, shading */

static const DWORD atirx_regs_probe[] = {
    0x7c0ul, 0x7c4ul, 0x7c8ul,   /* RED_X_INC, RED_Y_INC, RED_START */
    0x7ccul, 0x7d0ul, 0x7d4ul,   /* GREEN */
    0x7d8ul, 0x7dcul, 0x7e0ul,   /* BLUE */
    0x7e4ul, 0x7e8ul, 0x7ecul,   /* Z */
    0x7f0ul, 0x7f4ul, 0x7f8ul,   /* ALPHA / FOG */
    0x740ul, 0x744ul, 0x748ul, 0x74cul, 0x750ul, 0x754ul,   /* S */
    0x758ul, 0x75cul, 0x760ul, 0x764ul, 0x768ul, 0x76cul,   /* T */
    0x770ul,                     /* TEX_SIZE_PITCH */
    0x548ul, 0x54cul             /* Z_OFF_PITCH, Z_CNTL */
};

static int atirx_run_regs(struct v9x_m64_engine *engine)
{
    static const DWORD patterns[3] = { 0xfffffffful, 0ul, 0x55555555ul };
    v9x_u32 offsets[2];
    v9x_u32 values[2];
    UINT index;
    UINT pattern;
    char key[32];
    char text[12];

    atirx_key("SceneSet", "regs");
    offsets[0] = V9X_M64_SCALE_3D_CNTL;
    values[0] = ATIRX_SCALE_3D_SHADE;
    if (atirx_emit(engine, "regs", offsets, values, 1ul) != V9X_STATUS_OK) {
        return 0;
    }
    for (index = 0u;
         index < sizeof(atirx_regs_probe) / sizeof(atirx_regs_probe[0]);
         ++index) {
        for (pattern = 0u; pattern < 3u; ++pattern) {
            offsets[0] = atirx_regs_probe[index];
            values[0] = patterns[pattern];
            if (atirx_emit(engine, "regs", offsets, values, 1ul) !=
                    V9X_STATUS_OK ||
                v9x_m64_wait_idle(engine, ATIRX_SPINS) != V9X_STATUS_OK) {
                atirx_key("Result", "REGS-STOPPED");
                return 0;
            }
            lstrcpyA(key, "R");
            atirx_hex(text, atirx_regs_probe[index], 3);
            lstrcatA(key, text);
            lstrcatA(key, pattern == 0u ? "_Ones" :
                          pattern == 1u ? "_Zero" : "_Fives");
            atirx_key_hex(key, atirx_read(0, atirx_regs_probe[index]));
        }
        /* Leave each register zero. */
        offsets[0] = atirx_regs_probe[index];
        values[0] = 0ul;
        if (atirx_emit(engine, "regs", offsets, values, 1ul) !=
            V9X_STATUS_OK) {
            return 0;
        }
    }
    offsets[0] = V9X_M64_SCALE_3D_CNTL;
    values[0] = 0ul;
    offsets[1] = V9X_M64_Z_CNTL;
    values[1] = 0ul;
    if (atirx_emit(engine, "regs", offsets, values, 2ul) != V9X_STATUS_OK ||
        v9x_m64_wait_idle(engine, ATIRX_SPINS) != V9X_STATUS_OK) {
        return 0;
    }
    atirx_key("Result", "REGS-READ");
    return 1;
}

/* ---- Phase 3: Gouraud, measured ---------------------------------------- */

#define ATIRX_FX(n) ((v9x_s32)(n) * 65536l)
#define ATIRX_SHADE_DWORDS 24u

struct atirx_shade_scene {
    const char *name;
    struct v9x_r2_flat_trap trap;
    struct v9x_r2_shade shade;
};

/* A rectangle: columns 16..47, rows 16..23, both edges vertical. */
#define ATIRX_RECT { 16ul, 16ul, 8ul, 48ul, -1l, 0l, -1l, -1l, 0l, -1l, \
    V9X_M64_DST_X_DIR | V9X_M64_DST_Y_DIR | V9X_R2_TRAIL_X_DIR |         \
    V9X_R2_TRAP_FILL_DIR }
/* The same with the leading edge one pixel right per row. */
#define ATIRX_SLOPE { 16ul, 16ul, 8ul, 48ul, 0l, 8l, -8l, -1l, 0l, -1l, \
    V9X_M64_DST_X_DIR | V9X_M64_DST_Y_DIR | V9X_R2_TRAIL_X_DIR |         \
    V9X_R2_TRAP_FILL_DIR }

static const struct atirx_shade_scene atirx_shade_scenes[] = {
    /* Red 0 at the leading pixel, +8.0 a pixel along the span. */
    { "G1RedX", ATIRX_RECT,
      { { 0l, 0l, 0l }, { ATIRX_FX(8), 0l, 0l }, { 0l, 0l, 0l } } },
    /* Green 0, +32.0 a row down the leading edge. */
    { "G2GreenY", ATIRX_RECT,
      { { 0l, 0l, 0l }, { 0l, 0l, 0l }, { 0l, ATIRX_FX(32), 0l } } },
    /* Constant 255/128/64: the 8-bit to 565 conversion. */
    { "G3Const", ATIRX_RECT,
      { { ATIRX_FX(255), ATIRX_FX(128), ATIRX_FX(64) },
        { 0l, 0l, 0l }, { 0l, 0l, 0l } } },
    /* Red +0.5 a pixel: how fractions accumulate and truncate. */
    { "G4RedHalf", ATIRX_RECT,
      { { 0l, 0l, 0l }, { 0x8000l, 0l, 0l }, { 0l, 0l, 0l } } },
    /* Blue 248, -8.0 a pixel: a negative increment. */
    { "G5BlueDown", ATIRX_RECT,
      { { 0l, 0l, ATIRX_FX(248) }, { 0l, 0l, -ATIRX_FX(8) },
        { 0l, 0l, 0l } } },
    /* Red +8.0 a pixel, no Y increment, on the sloped leading edge: does
     * an edge X step add X_INC? */
    { "G6SlopeRedX", ATIRX_SLOPE,
      { { 0l, 0l, 0l }, { ATIRX_FX(8), 0l, 0l }, { 0l, 0l, 0l } } },
    /* Red 200, +8.0 a pixel: past 255 by column 23. Saturate or wrap? */
    { "G7RedOver", ATIRX_RECT,
      { { ATIRX_FX(200), 0l, 0l }, { ATIRX_FX(8), 0l, 0l },
        { 0l, 0l, 0l } } },
    /* Green 64, -8.0 a pixel: below 0 by column 25. */
    { "G8GreenUnder", ATIRX_RECT,
      { { 0l, ATIRX_FX(64), 0l }, { 0l, -ATIRX_FX(8), 0l },
        { 0l, 0l, 0l } } }
};

/* Rows 16..23, columns 15..49, as 565 hex words. */
static void atirx_dump_rows(const char *prefix)
{
    char key[48];
    char text[8];
    char line[200];
    DWORD x;
    DWORD y;

    for (y = 16ul; y < 24ul; ++y) {
        line[0] = '\0';
        for (x = 15ul; x < 50ul; ++x) {
            atirx_hex(text, *atirx_pixel(x, y), 4);
            lstrcatA(line, text);
            lstrcatA(line, " ");
        }
        lstrcpyA(key, prefix);
        lstrcatA(key, "Px");
        atirx_decimal(text, y);
        lstrcatA(key, text);
        atirx_key(key, line);
    }
}

static int atirx_run_shade(struct v9x_m64_engine *engine,
                           const struct v9x_r2_target *target)
{
    v9x_u32 offsets[ATIRX_SHADE_DWORDS];
    v9x_u32 values[ATIRX_SHADE_DWORDS];
    v9x_u32 written;
    v9x_status status;
    UINT index;
    char prefix[48];
    UINT count = sizeof(atirx_shade_scenes) / sizeof(atirx_shade_scenes[0]);

    atirx_key("SceneSet", "shade");
    for (index = 0u; index < count; ++index) {
        const struct atirx_shade_scene *scene = &atirx_shade_scenes[index];

        atirx_prefix(prefix, scene->name);
        atirx_key("Scene", scene->name);
        atirx_prepare_block();
        if (v9x_r2_build_shade_state(target, &scene->shade, offsets, values,
                                     ATIRX_SHADE_DWORDS, &written) !=
            V9X_STATUS_OK) {
            atirx_key("Result", "SHADE-BUILD");
            return 0;
        }
        status = atirx_emit(engine, scene->name, offsets, values, written);
        if (status == V9X_STATUS_OK) {
            if (v9x_r2_build_trap(target, &scene->trap, offsets, values,
                                  ATIRX_SHADE_DWORDS, &written) !=
                V9X_STATUS_OK) {
                atirx_key("Result", "TRAP-BUILD");
                return 0;
            }
            status = atirx_emit(engine, scene->name, offsets, values,
                                written);
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
        atirx_dump_rows(prefix);
        atirx_report_block(prefix);
        atirx_flush();
    }
    /* Back to the 2D datapath, as the HAL's 2D mode reset expects. */
    offsets[0] = V9X_M64_SCALE_3D_CNTL;
    values[0] = 0ul;
    if (atirx_emit(engine, "end", offsets, values, 1ul) != V9X_STATUS_OK ||
        v9x_m64_wait_idle(engine, ATIRX_SPINS) != V9X_STATUS_OK) {
        return 0;
    }
    atirx_key("Result", "SHADE-RUN");
    return 1;
}

/* ---- Phase 3: Gouraud triangles ---------------------------------------- */

#define ATIRX_GOURAUD_COUNT 60u

/* The engine model's 565 for pixel (x, y) of trapezoid `trap`: the masked
 * register values summed modulo the field, then the measured channel rule
 * and 565 truncation. */
static WORD atirx_model_565(const struct v9x_r2_shade *shade,
                            const struct v9x_r2_flat_trap *trap,
                            DWORD x, DWORD y)
{
    v9x_u32 out[3];
    v9x_u32 channel;

    for (channel = 0ul; channel < 3ul; ++channel) {
        v9x_u32 start = (v9x_u32)shade->start[channel] & V9X_R2_COLOR_MASK;
        v9x_u32 xi = (v9x_u32)shade->x_inc[channel] & V9X_R2_COLOR_MASK;
        v9x_u32 yi = (v9x_u32)shade->y_inc[channel] & V9X_R2_COLOR_MASK;

        /* X_INC counts steps in DST_X_DIR's direction (measured). */
        v9x_u32 steps = (trap->dst_cntl & V9X_M64_DST_X_DIR) != 0ul
            ? x - trap->x : trap->x - x;

        out[channel] = v9x_r2_channel_out((v9x_s32)(
            start + steps * xi + (y - trap->y) * yi));
    }
    return (WORD)(((out[0] >> 3) << 11) | ((out[1] >> 2) << 5) |
                  (out[2] >> 3));
}

static v9x_u32 atirx_random_color(void)
{
    atirx_lcg = atirx_lcg * 1103515245ul + 12345ul;
    return (atirx_lcg >> 4) & 0x00fffffful;
}

static int atirx_run_gouraud(struct v9x_m64_engine *engine,
                             const struct v9x_r2_target *target)
{
    struct v9x_r2_vertex v[3];
    struct v9x_r2_flat_trap traps[V9X_R2_SETUP_TRAPS];
    struct v9x_r2_shade shades[V9X_R2_SETUP_TRAPS];
    v9x_u32 colors[3];
    v9x_u32 offsets[ATIRX_SHADE_DWORDS];
    v9x_u32 values[ATIRX_SHADE_DWORDS];
    v9x_u32 written;
    v9x_u32 count;
    v9x_u32 trap;
    v9x_status status;
    UINT index;
    UINT drawn_triangles = 0u;
    UINT skipped = 0u;
    DWORD total_mismatch = 0ul;
    DWORD total_pixels = 0ul;
    char label[16];
    char text[16];
    char key[48];
    char line[200];

    atirx_key("SceneSet", "gouraud");
    for (index = 0u; index < ATIRX_GOURAUD_COUNT; ++index) {
        DWORD x;
        DWORD y;
        DWORD mismatch = 0ul;
        DWORD pixels = 0ul;
        int steep = 0;

        v[0].x = atirx_random_coord();
        v[0].y = atirx_random_coord();
        v[1].x = atirx_random_coord();
        v[1].y = atirx_random_coord();
        v[2].x = atirx_random_coord();
        v[2].y = atirx_random_coord();
        colors[0] = atirx_random_color();
        colors[1] = atirx_random_color();
        colors[2] = atirx_random_color();
        if (index == 0u) {
            /* One constant-colour triangle: no gradient at all. */
            colors[1] = colors[0];
            colors[2] = colors[0];
        }

        lstrcpyA(label, "G");
        atirx_decimal(text, index);
        lstrcatA(label, text);

        if (v9x_r2_setup_triangle(target, v, traps, &count) != V9X_STATUS_OK) {
            atirx_key(label, "SETUP-REFUSED");
            return 0;
        }
        for (trap = 0ul; trap < count; ++trap) {
            status = v9x_r2_setup_shade(v, colors, &traps[trap],
                                        &shades[trap]);
            if (status == V9X_STATUS_UNSUPPORTED) {
                steep = 1;
            } else if (status != V9X_STATUS_OK) {
                atirx_key(label, "SHADE-REFUSED");
                return 0;
            }
        }
        if (steep || count == 0ul) {
            ++skipped;
            continue;
        }

        atirx_prepare_block();
        status = V9X_STATUS_OK;
        for (trap = 0ul; trap < count && status == V9X_STATUS_OK; ++trap) {
            if (v9x_r2_build_shade_state(target, &shades[trap], offsets,
                                         values, ATIRX_SHADE_DWORDS,
                                         &written) != V9X_STATUS_OK) {
                atirx_key(label, "STATE-BUILD");
                return 0;
            }
            status = atirx_emit(engine, label, offsets, values, written);
            if (status != V9X_STATUS_OK) {
                break;
            }
            if (v9x_r2_build_trap(target, &traps[trap], offsets, values,
                                  ATIRX_SHADE_DWORDS, &written) !=
                V9X_STATUS_OK) {
                atirx_key(label, "TRAP-BUILD");
                return 0;
            }
            status = atirx_emit(engine, label, offsets, values, written);
        }
        if (status == V9X_STATUS_OK) {
            status = v9x_m64_wait_idle(engine, ATIRX_SPINS);
        }
        if (status != V9X_STATUS_OK) {
            atirx_key_hex("FailStatus", (DWORD)status);
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
                WORD got = *atirx_pixel(x, y);
                WORD want = ATIRX_SENTINEL;

                if (v9x_r2_ref_covers(v, (v9x_s32)x, (v9x_s32)y)) {
                    for (trap = 0ul; trap < count; ++trap) {
                        if (y >= traps[trap].y &&
                            y < traps[trap].y + traps[trap].length) {
                            want = atirx_model_565(&shades[trap],
                                                   &traps[trap], x, y);
                        }
                    }
                    ++pixels;
                }
                if (got != want) {
                    ++mismatch;
                    if (lstrlenA(line) < 150) {
                        atirx_decimal(text, x);
                        lstrcatA(line, text);
                        lstrcatA(line, ",");
                        atirx_decimal(text, y);
                        lstrcatA(line, text);
                        lstrcatA(line, ":");
                        atirx_hex(text, got, 4);
                        lstrcatA(line, text);
                        lstrcatA(line, "/");
                        atirx_hex(text, want, 4);
                        lstrcatA(line, text);
                        lstrcatA(line, " ");
                    }
                }
            }
        }
        lstrcpyA(key, label);
        lstrcatA(key, "_Pixels");
        atirx_key_dec(key, pixels);
        lstrcpyA(key, label);
        lstrcatA(key, "_Mismatches");
        atirx_key_dec(key, mismatch);
        if (line[0] != '\0') {
            lstrcpyA(key, label);
            lstrcatA(key, "_First");
            atirx_key(key, line);
        }
        total_mismatch += mismatch;
        total_pixels += pixels;
        ++drawn_triangles;
        atirx_flush();
    }

    offsets[0] = V9X_M64_SCALE_3D_CNTL;
    values[0] = 0ul;
    if (atirx_emit(engine, "end", offsets, values, 1ul) != V9X_STATUS_OK ||
        v9x_m64_wait_idle(engine, ATIRX_SPINS) != V9X_STATUS_OK) {
        return 0;
    }
    atirx_key_dec("TrianglesDrawn", drawn_triangles);
    atirx_key_dec("TrianglesTooSteep", skipped);
    atirx_key_dec("PixelsCompared", total_pixels);
    atirx_key_dec("TotalMismatches", total_mismatch);
    atirx_key("Result", total_mismatch == 0ul ? "GOURAUD-MATCH"
                                              : "GOURAUD-DIFFER");
    return 1;
}

/* ---- Phase 3: Z16, measured --------------------------------------------- */

#define ATIRX_ZBLOCK_OFFSET  0x00210000ul
#define ATIRX_ZBAND_LO       0x3000u
#define ATIRX_ZBAND_MID      0x4000u
#define ATIRX_ZBAND_HI       0x5000u
#define ATIRX_ZCLEAR         0xffffu
#define ATIRX_ZGUARD_WORD    0x6996u

struct atirx_z_scene {
    const char *name;
    int banded;          /* Z block pre-filled with column bands */
    DWORD z_cntl;
    v9x_s32 start;       /* 16.16 depth units */
    v9x_s32 x_inc;
    v9x_s32 y_inc;
};

#define ATIRX_ZT(test) (V9X_R2_Z_EN | ((DWORD)(test) << V9X_R2_Z_TEST_SHIFT))

/*
 * The bands: columns 16-23 hold 3000, 24-31 4000, 32-39 5000, 40-47 4000.
 * Every compare scene draws Z 4000 with writes off, so each test's pass
 * set is a known union of bands, if "less" means incoming < stored.
 */
static const struct atirx_z_scene atirx_z_scenes[] = {
    { "Z1WriteConst", 0, ATIRX_ZT(7) | V9X_R2_Z_WRITE,
      0x1234l << 16, 0l, 0l },
    { "Z2WriteGradient", 0, ATIRX_ZT(7) | V9X_R2_Z_WRITE,
      0x1000l << 16, 0x100l << 16, 0x10l << 16 },
    { "Z3Never", 1, ATIRX_ZT(0), 0x4000l << 16, 0l, 0l },
    { "Z4Less", 1, ATIRX_ZT(1), 0x4000l << 16, 0l, 0l },
    { "Z5LessEqual", 1, ATIRX_ZT(2), 0x4000l << 16, 0l, 0l },
    { "Z6Equal", 1, ATIRX_ZT(3), 0x4000l << 16, 0l, 0l },
    { "Z7GreaterEqual", 1, ATIRX_ZT(4), 0x4000l << 16, 0l, 0l },
    { "Z8Greater", 1, ATIRX_ZT(5), 0x4000l << 16, 0l, 0l },
    { "Z9NotEqual", 1, ATIRX_ZT(6), 0x4000l << 16, 0l, 0l },
    { "Z10Always", 1, ATIRX_ZT(7), 0x4000l << 16, 0l, 0l },
    { "Z11LessWrite", 1, ATIRX_ZT(1) | V9X_R2_Z_WRITE,
      0x4000l << 16, 0l, 0l }
};

static volatile WORD *atirx_zpixel(DWORD x, DWORD y)
{
    return atirx_fb + (ATIRX_ZBLOCK_OFFSET + y * ATIRX_BLOCK_PITCH) / 2ul + x;
}

static WORD atirx_zband(DWORD x)
{
    if (x >= 16ul && x < 24ul) {
        return ATIRX_ZBAND_LO;
    }
    if (x >= 32ul && x < 40ul) {
        return ATIRX_ZBAND_HI;
    }
    return ATIRX_ZBAND_MID;
}

static void atirx_prepare_z(int banded)
{
    volatile WORD *guard;
    DWORD index;
    DWORD x;
    DWORD y;

    guard = atirx_fb + (ATIRX_ZBLOCK_OFFSET - ATIRX_GUARD_BYTES) / 2ul;
    for (index = 0ul; index < ATIRX_GUARD_BYTES / 2ul; ++index) {
        guard[index] = ATIRX_ZGUARD_WORD;
    }
    guard = atirx_fb + (ATIRX_ZBLOCK_OFFSET +
                        ATIRX_BLOCK_SIZE * ATIRX_BLOCK_PITCH) / 2ul;
    for (index = 0ul; index < ATIRX_GUARD_BYTES / 2ul; ++index) {
        guard[index] = ATIRX_ZGUARD_WORD;
    }
    for (y = 0ul; y < ATIRX_BLOCK_SIZE; ++y) {
        for (x = 0ul; x < ATIRX_BLOCK_SIZE; ++x) {
            *atirx_zpixel(x, y) = banded ? atirx_zband(x) : ATIRX_ZCLEAR;
        }
    }
}

/* Z guards, and Z pixels outside the drawn rectangle that changed. */
static void atirx_report_z(const char *prefix, int banded)
{
    volatile WORD *guard;
    DWORD index;
    DWORD x;
    DWORD y;
    DWORD guard_bad = 0ul;
    DWORD outside = 0ul;
    char key[48];
    char text[8];
    char line[200];

    guard = atirx_fb + (ATIRX_ZBLOCK_OFFSET - ATIRX_GUARD_BYTES) / 2ul;
    for (index = 0ul; index < ATIRX_GUARD_BYTES / 2ul; ++index) {
        if (guard[index] != ATIRX_ZGUARD_WORD) {
            ++guard_bad;
        }
    }
    guard = atirx_fb + (ATIRX_ZBLOCK_OFFSET +
                        ATIRX_BLOCK_SIZE * ATIRX_BLOCK_PITCH) / 2ul;
    for (index = 0ul; index < ATIRX_GUARD_BYTES / 2ul; ++index) {
        if (guard[index] != ATIRX_ZGUARD_WORD) {
            ++guard_bad;
        }
    }
    for (y = 0ul; y < ATIRX_BLOCK_SIZE; ++y) {
        for (x = 0ul; x < ATIRX_BLOCK_SIZE; ++x) {
            WORD before = banded ? atirx_zband(x) : ATIRX_ZCLEAR;
            int in_rect = x >= 16ul && x < 48ul && y >= 16ul && y < 24ul;

            if (!in_rect && *atirx_zpixel(x, y) != before) {
                ++outside;
            }
        }
    }
    for (y = 16ul; y < 24ul; y += 7ul) {
        line[0] = '\0';
        for (x = 15ul; x < 50ul; ++x) {
            atirx_hex(text, *atirx_zpixel(x, y), 4);
            lstrcatA(line, text);
            lstrcatA(line, " ");
        }
        lstrcpyA(key, prefix);
        lstrcatA(key, "Z");
        atirx_decimal(text, y);
        lstrcatA(key, text);
        atirx_key(key, line);
    }
    lstrcpyA(key, prefix);
    lstrcatA(key, "ZOutsideRect");
    atirx_key_dec(key, outside);
    lstrcpyA(key, prefix);
    lstrcatA(key, "ZGuardMismatches");
    atirx_key_dec(key, guard_bad);
}

static int atirx_finish_draw(struct v9x_m64_engine *engine)
{
    v9x_status status = v9x_m64_wait_idle(engine, ATIRX_SPINS);

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
    return 1;
}

static int atirx_run_z(struct v9x_m64_engine *engine,
                       const struct v9x_r2_target *target)
{
    static const struct v9x_r2_flat_trap rect = ATIRX_RECT;
    struct v9x_r2_shade shade;
    struct v9x_r2_depth depth;
    struct v9x_m64_fill fill;
    v9x_u32 offsets[ATIRX_SHADE_DWORDS];
    v9x_u32 values[ATIRX_SHADE_DWORDS];
    v9x_u32 written;
    UINT index;
    UINT count = sizeof(atirx_z_scenes) / sizeof(atirx_z_scenes[0]);
    char prefix[48];
    DWORD x;
    DWORD y;
    DWORD cleared = 0ul;

    atirx_key("SceneSet", "z");
    for (index = 0u; index < 3u; ++index) {
        shade.start[index] = 0l;
        shade.x_inc[index] = 0l;
        shade.y_inc[index] = 0l;
    }
    shade.start[0] = 248l << 16;          /* red F800 */

    for (index = 0u; index < count; ++index) {
        const struct atirx_z_scene *scene = &atirx_z_scenes[index];

        atirx_prefix(prefix, scene->name);
        atirx_key("Scene", scene->name);
        atirx_prepare_block();
        atirx_prepare_z(scene->banded);
        depth.offset = ATIRX_ZBLOCK_OFFSET;
        depth.z_cntl = scene->z_cntl;
        depth.start = scene->start;
        depth.x_inc = scene->x_inc;
        depth.y_inc = scene->y_inc;

        if (v9x_r2_build_shade_state(target, &shade, offsets, values,
                                     ATIRX_SHADE_DWORDS, &written) !=
                V9X_STATUS_OK ||
            atirx_emit(engine, scene->name, offsets, values, written) !=
                V9X_STATUS_OK ||
            v9x_r2_build_z_state(target, &depth, offsets, values,
                                 ATIRX_SHADE_DWORDS, &written) !=
                V9X_STATUS_OK ||
            atirx_emit(engine, scene->name, offsets, values, written) !=
                V9X_STATUS_OK ||
            v9x_r2_build_trap(target, &rect, offsets, values,
                              ATIRX_SHADE_DWORDS, &written) !=
                V9X_STATUS_OK ||
            atirx_emit(engine, scene->name, offsets, values, written) !=
                V9X_STATUS_OK) {
            atirx_key("Result", "Z-EMIT");
            return 0;
        }
        if (!atirx_finish_draw(engine)) {
            return 0;
        }
        atirx_report_block(prefix);
        atirx_report_z(prefix, scene->banded);
        atirx_flush();
    }

    /* A Z clear: the 2D engine fill on the Z surface, Z off. */
    atirx_key("Scene", "Z12Clear");
    atirx_prepare_z(1);
    offsets[0] = V9X_M64_SCALE_3D_CNTL;  values[0] = 0ul;
    offsets[1] = V9X_M64_Z_CNTL;         values[1] = 0ul;
    if (atirx_emit(engine, "Z12Clear", offsets, values, 2ul) !=
        V9X_STATUS_OK) {
        return 0;
    }
    fill.vram_bytes = target->vram_bytes;
    fill.target_offset = ATIRX_ZBLOCK_OFFSET;
    fill.target_pitch_bytes = ATIRX_BLOCK_PITCH;
    fill.target_width = ATIRX_BLOCK_SIZE;
    fill.target_height = ATIRX_BLOCK_SIZE;
    fill.left = 16ul;
    fill.top = 16ul;
    fill.right = 48ul;
    fill.bottom = 24ul;
    fill.color = 0x0000abcdul;
    if (v9x_m64_build_fill(&fill, offsets, values, ATIRX_SHADE_DWORDS,
                           &written) != V9X_STATUS_OK ||
        atirx_emit(engine, "Z12Clear", offsets, values, written) !=
        V9X_STATUS_OK) {
        atirx_key("Result", "Z-CLEAR-EMIT");
        return 0;
    }
    if (!atirx_finish_draw(engine)) {
        return 0;
    }
    for (y = 16ul; y < 24ul; ++y) {
        for (x = 16ul; x < 48ul; ++x) {
            if (*atirx_zpixel(x, y) == 0xabcdu) {
                ++cleared;
            }
        }
    }
    atirx_key_dec("Z12Clear_Filled", cleared);
    atirx_report_z("Z12Clear_", 1);

    offsets[0] = V9X_M64_SCALE_3D_CNTL;  values[0] = 0ul;
    offsets[1] = V9X_M64_Z_CNTL;         values[1] = 0ul;
    if (atirx_emit(engine, "end", offsets, values, 2ul) != V9X_STATUS_OK ||
        v9x_m64_wait_idle(engine, ATIRX_SPINS) != V9X_STATUS_OK) {
        return 0;
    }
    atirx_key("Result", "Z-RUN");
    return 1;
}

/* ---- Phase 4: textures, measured -------------------------------------- */

#define ATIRX_TEX_OFFSET   0x00220000ul
#define ATIRX_TEX_SIZE     32ul
#define ATIRX_TEX_LOG2     5ul
/* A second map, 16x16 at pitch 16, past the first and its guard. */
#define ATIRX_TEX16_OFFSET 0x00221000ul
/* A third, 16 wide and 32 high at pitch 16. */
#define ATIRX_TALL_OFFSET  0x00221800ul

struct atirx_tex_scene {
    const char *name;
    v9x_u32 offset;          /* ATIRX_TEX_OFFSET or ATIRX_TEX16_OFFSET */
    v9x_u32 log2_width;
    v9x_u32 log2_height;
    v9x_u32 log2_pitch;
    struct v9x_r2_flat_trap trap;
    struct v9x_r2_st st;
};

/* Boot 136's first run put one texel of the 32-wide map at 2^21: S and
 * T count texels of a 1024-wide map in 16.16, it seems. ATIRX_TX(n) is
 * n texels of the 32-wide map in that unit. */
#define ATIRX_TX(n) ((v9x_s32)((n) * 2097152l))

#define ATIRX_MAP32 ATIRX_TEX_OFFSET, 5ul, 5ul, 5ul

/* The leading edge leaning left one pixel a row from 40 (DST_X_DIR
 * clear), trailing at 48. */
#define ATIRX_SLOPE_LEFT { 40ul, 16ul, 8ul, 48ul, 0l, 8l, -8l, -1l, 0l, -1l,     V9X_M64_DST_Y_DIR | V9X_R2_TRAIL_X_DIR | V9X_R2_TRAP_FILL_DIR }

/* Fields in order: start, xinc_start, y_inc, x_inc2, y_inc2, xy_inc2,
 * each { S, T }. */
static const struct atirx_tex_scene atirx_tex_scenes[] = {
    /* S +1 texel a pixel, T +1 a row, both from 0. */
    { "T1Unit", ATIRX_MAP32, ATIRX_RECT,
      { { 0l, 0l }, { ATIRX_TX(1), 0l }, { 0l, ATIRX_TX(1) },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* Both from half a texel: where is the sample point? */
    { "T2Half", ATIRX_MAP32, ATIRX_RECT,
      { { ATIRX_TX(1) / 2l, ATIRX_TX(1) / 2l }, { ATIRX_TX(1), 0l },
        { 0l, ATIRX_TX(1) }, { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* S +0.5 a pixel: each texel twice. */
    { "T3HalfStep", ATIRX_MAP32, ATIRX_RECT,
      { { 0l, 0l }, { ATIRX_TX(1) / 2l, 0l }, { 0l, ATIRX_TX(1) },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* S from 20: past 32 by column 28. Wrap? */
    { "T4Wrap", ATIRX_MAP32, ATIRX_RECT,
      { { ATIRX_TX(20), 0l }, { ATIRX_TX(1), 0l }, { 0l, ATIRX_TX(1) },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* The left-leaning leading edge with S +1: does S follow DST_X_DIR
     * the way colour does? */
    { "T5SlopeLeft", ATIRX_MAP32, ATIRX_SLOPE_LEFT,
      { { ATIRX_TX(8), 0l }, { ATIRX_TX(1), 0l }, { 0l, ATIRX_TX(1) },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* S_XINC_START 0, S_X_INC2 1/8 texel: S quadratic along the span. */
    { "T6Quadratic", ATIRX_MAP32, ATIRX_RECT,
      { { 0l, 0l }, { 0l, 0l }, { 0l, ATIRX_TX(1) },
        { ATIRX_TX(1) / 8l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* T +1 a pixel along the span, S +1 a row: the axes swapped. */
    { "T7Swapped", ATIRX_MAP32, ATIRX_RECT,
      { { 0l, 0l }, { 0l, ATIRX_TX(1) }, { ATIRX_TX(1), 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* A true 16x16 map at pitch 16, same S/T: does the unit follow the
     * map's size (u = n/2) or stay absolute (u = n mod 16)? */
    { "T9Map16", ATIRX_TEX16_OFFSET, 4ul, 4ul, 4ul, ATIRX_RECT,
      { { 0l, 0l }, { ATIRX_TX(1), 0l }, { 0l, ATIRX_TX(1) },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* The 32x32 map declared 32 wide, 16 high. */
    { "T10Wide", ATIRX_TEX_OFFSET, 5ul, 4ul, 5ul, ATIRX_RECT,
      { { 0l, 0l }, { ATIRX_TX(1), 0l }, { 0l, ATIRX_TX(1) },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* S_Y_INC2 1/4 alone, T +1 a row: is S quadratic in y? */
    { "T12YInc2", ATIRX_MAP32, ATIRX_RECT,
      { { 0l, 0l }, { 0l, 0l }, { 0l, ATIRX_TX(1) },
        { 0l, 0l }, { ATIRX_TX(1) / 4l, 0l }, { 0l, 0l } } },
    /* S_XY_INC2 1/4 alone: does it grow the row's X increment? */
    { "T13XYInc2", ATIRX_MAP32, ATIRX_RECT,
      { { 0l, 0l }, { 0l, 0l }, { 0l, ATIRX_TX(1) },
        { 0l, 0l }, { 0l, 0l }, { ATIRX_TX(1) / 4l, 0l } } },
    /* T5 with one S_START LSB (32) more on both: if the span adds the
     * ones' complement of X_INC, -1 a pixel, every sample is exact. */
    { "T5bBias", ATIRX_MAP32, ATIRX_SLOPE_LEFT,
      { { ATIRX_TX(8) + 32l, 32l }, { ATIRX_TX(1), 0l },
        { 0l, ATIRX_TX(1) }, { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* A real 16x32 map at pitch 16, T from 20: what does u past 15 read? */
    { "T14Tall16", ATIRX_TALL_OFFSET, 4ul, 5ul, 4ul, ATIRX_RECT,
      { { 0l, ATIRX_TX(20) }, { ATIRX_TX(1), 0l }, { 0l, ATIRX_TX(1) },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* The 32x32 data declared 32x16, T from 12: does v wrap at 16? */
    { "T15WideWrap", ATIRX_TEX_OFFSET, 5ul, 4ul, 5ul, ATIRX_RECT,
      { { 0l, ATIRX_TX(12) }, { ATIRX_TX(1), 0l }, { 0l, ATIRX_TX(1) },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* The leading edge one pixel right a row, S_X_INC2 1/8 alone: does
     * an edge step grow the row's X increment as a pixel step does? */
    { "T16SlopeX2", ATIRX_MAP32, ATIRX_SLOPE,
      { { 0l, 0l }, { 0l, 0l }, { 0l, ATIRX_TX(1) },
        { ATIRX_TX(1) / 8l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* The same edge, S_XY_INC2 1/4 alone. */
    { "T17SlopeXY", ATIRX_MAP32, ATIRX_SLOPE,
      { { 0l, 0l }, { 0l, 0l }, { 0l, ATIRX_TX(1) },
        { 0l, 0l }, { 0l, 0l }, { ATIRX_TX(1) / 4l, 0l } } },
    /* The left-leaning edge (spans against DST_X_DIR), S from 8 so
     * nothing goes negative, X_INC2 1/8 alone. */
    { "T18LeftX2", ATIRX_MAP32, ATIRX_SLOPE_LEFT,
      { { ATIRX_TX(8), ATIRX_TX(1) / 2l }, { 0l, 0l }, { 0l, ATIRX_TX(1) },
        { ATIRX_TX(1) / 8l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* The same, XY_INC2 -1/4 alone. */
    { "T19LeftXY", ATIRX_MAP32, ATIRX_SLOPE_LEFT,
      { { ATIRX_TX(8), ATIRX_TX(1) / 2l }, { 0l, 0l }, { 0l, ATIRX_TX(1) },
        { 0l, 0l }, { 0l, 0l }, { -ATIRX_TX(1) / 4l, 0l } } }
};

/* Texel (u, v): red u, green v, blue 31. Blue 31 can never be the
 * sentinel 5AA5 (blue 5), so a sampled pixel is unmistakable. */
static WORD atirx_texel(DWORD u, DWORD v)
{
    return (WORD)((u << 11) | (v << 5) | 31u);
}

static void atirx_write_texture(DWORD offset, DWORD width, DWORD height)
{
    DWORD u;
    DWORD v;
    volatile WORD *base = atirx_fb + offset / 2ul;

    for (v = 0ul; v < height; ++v) {
        for (u = 0ul; u < width; ++u) {
            base[v * width + u] = atirx_texel(u, v);
        }
    }
}

static DWORD atirx_texture_damage(DWORD offset, DWORD width, DWORD height)
{
    DWORD u;
    DWORD v;
    DWORD bad = 0ul;
    volatile WORD *base = atirx_fb + offset / 2ul;

    for (v = 0ul; v < height; ++v) {
        for (u = 0ul; u < width; ++u) {
            if (base[v * width + u] != atirx_texel(u, v)) {
                ++bad;
            }
        }
    }
    return bad;
}

/* Rows 16, 17, 20 and 23, columns 15..49, as u.v for a texel (blue 31)
 * and '-' for anything else. */
static void atirx_dump_uv(const char *prefix)
{
    static const DWORD rows[4] = { 16ul, 17ul, 20ul, 23ul };
    char key[48];
    char text[8];
    char line[256];
    DWORD x;
    UINT index;

    for (index = 0u; index < 4u; ++index) {
        line[0] = '\0';
        for (x = 15ul; x < 50ul; ++x) {
            WORD value = *atirx_pixel(x, rows[index]);

            if ((value & 31u) != 31u) {
                lstrcatA(line, "- ");
                continue;
            }
            atirx_decimal(text, value >> 11);
            lstrcatA(line, text);
            lstrcatA(line, ".");
            atirx_decimal(text, (value >> 5) & 63u);
            lstrcatA(line, text);
            lstrcatA(line, " ");
        }
        lstrcpyA(key, prefix);
        lstrcatA(key, "UV");
        atirx_decimal(text, rows[index]);
        lstrcatA(key, text);
        atirx_key(key, line);
    }
}

/*
 * /texprec: the resolution each S term is applied at. START sits one
 * 32-unit step below texel 1 and a single term is 2^j; full precision
 * reaches texel 1 at a known column (X terms, row 16 or 23) or row (Y
 * terms). A term whose low j bits are dropped never does. Boot 136's
 * /textri differed by one texel on 3 of 9,753 pixels, all perspective,
 * the card lower than the model by 100-200 units.
 */
#define ATIRX_PREC_TERMS 5u
#define ATIRX_PREC_BITS  8u

static struct atirx_tex_scene atirx_prec_scenes[ATIRX_PREC_TERMS *
                                                ATIRX_PREC_BITS];
static char atirx_prec_names[ATIRX_PREC_TERMS * ATIRX_PREC_BITS][16];

static UINT atirx_build_prec_scenes(void)
{
    static const char *terms[ATIRX_PREC_TERMS] = {
        "PX1_", "PY1_", "PX2_", "PY2_", "PXY_"
    };
    static const struct v9x_r2_flat_trap rect = ATIRX_RECT;
    UINT term;
    UINT bit;
    UINT at = 0u;
    char text[8];

    for (term = 0u; term < ATIRX_PREC_TERMS; ++term) {
        for (bit = 0u; bit < ATIRX_PREC_BITS; ++bit) {
            struct atirx_tex_scene *scene = &atirx_prec_scenes[at];
            v9x_s32 one = (v9x_s32)(1ul << bit);

            lstrcpyA(atirx_prec_names[at], terms[term]);
            atirx_decimal(text, bit);
            lstrcatA(atirx_prec_names[at], text);
            scene->name = atirx_prec_names[at];
            scene->offset = ATIRX_TEX_OFFSET;
            scene->log2_width = 5ul;
            scene->log2_height = 5ul;
            scene->log2_pitch = 5ul;
            scene->trap = rect;
            scene->st.start[0] = ATIRX_TX(1) - 32l;
            scene->st.start[1] = 0l;
            scene->st.xinc_start[0] = term == 0u ? one : 0l;
            scene->st.xinc_start[1] = 0l;
            scene->st.y_inc[0] = term == 1u ? one : 0l;
            scene->st.y_inc[1] = 0l;
            scene->st.x_inc2[0] = term == 2u ? one : 0l;
            scene->st.x_inc2[1] = 0l;
            scene->st.y_inc2[0] = term == 3u ? one : 0l;
            scene->st.y_inc2[1] = 0l;
            scene->st.xy_inc2[0] = term == 4u ? one : 0l;
            scene->st.xy_inc2[1] = 0l;
            ++at;
        }
    }
    return at;
}

static int atirx_run_tex(struct v9x_m64_engine *engine,
                         const struct v9x_r2_target *target,
                         const struct atirx_tex_scene *scenes, UINT count,
                         const char *set)
{
    struct v9x_r2_texture texture;
    v9x_u32 offsets[32];
    v9x_u32 values[32];
    v9x_u32 written;
    UINT index;
    char prefix[48];

    atirx_key("SceneSet", set);
    texture.format = V9X_R2_TEX_FORMAT_565;
    texture.scale_3d_extra = 0ul;
    atirx_write_texture(ATIRX_TEX_OFFSET, ATIRX_TEX_SIZE, ATIRX_TEX_SIZE);
    atirx_write_texture(ATIRX_TEX16_OFFSET, 16ul, 16ul);
    atirx_write_texture(ATIRX_TALL_OFFSET, 16ul, 32ul);

    for (index = 0u; index < count; ++index) {
        const struct atirx_tex_scene *scene = &scenes[index];

        atirx_prefix(prefix, scene->name);
        atirx_key("Scene", scene->name);
        atirx_prepare_block();
        texture.offset = scene->offset;
        texture.log2_width = scene->log2_width;
        texture.log2_height = scene->log2_height;
        texture.log2_pitch = scene->log2_pitch;
        if (v9x_r2_build_texture_state(target, &texture, &scene->st,
                                       offsets, values, 32ul, &written) !=
                V9X_STATUS_OK ||
            atirx_emit(engine, scene->name, offsets, values, written) !=
                V9X_STATUS_OK ||
            v9x_r2_build_trap(target, &scene->trap, offsets, values, 32ul,
                              &written) != V9X_STATUS_OK ||
            atirx_emit(engine, scene->name, offsets, values, written) !=
                V9X_STATUS_OK) {
            atirx_key("Result", "TEX-EMIT");
            return 0;
        }
        if (!atirx_finish_draw(engine)) {
            return 0;
        }
        atirx_dump_uv(prefix);
        atirx_report_block(prefix);
        atirx_flush();
    }
    atirx_key_dec("TextureDamage",
                  atirx_texture_damage(ATIRX_TEX_OFFSET, ATIRX_TEX_SIZE,
                                       ATIRX_TEX_SIZE) +
                  atirx_texture_damage(ATIRX_TEX16_OFFSET, 16ul, 16ul) +
                  atirx_texture_damage(ATIRX_TALL_OFFSET, 16ul, 32ul));

    offsets[0] = V9X_M64_SCALE_3D_CNTL;
    values[0] = 0ul;
    if (atirx_emit(engine, "end", offsets, values, 1ul) != V9X_STATUS_OK ||
        v9x_m64_wait_idle(engine, ATIRX_SPINS) != V9X_STATUS_OK) {
        return 0;
    }
    atirx_key("Result", "TEX-RUN");
    return 1;
}

/* ---- Phase 4: textured triangles ---------------------------------------- */

#define ATIRX_TEXTRI_COUNT  60u
#define ATIRX_TEXTRI_AFFINE 20u

/* A non-negative double below 2^31 to the nearest integer, without the
 * float-to-int cast this freestanding build has no runtime for. */
static DWORD atirx_round(double value)
{
    union {
        double d;
        DWORD w[2];
    } pun;

    if (!(value >= 0.0 && value < 2147483647.0)) {
        return 0xfffffffful;
    }
    pun.d = value + 6755399441055744.0;
    return pun.w[0];
}

static double atirx_random_unit(void)
{
    atirx_lcg = atirx_lcg * 1103515245ul + 12345ul;
    return (double)(LONG)((atirx_lcg >> 8) & 0xffffl) / 65536.0;
}

/* The engine model's image of one textured triangle: each trapezoid's
 * S/T walked as the engine walks them (rage2_reference.c), the texel they
 * select from the self-describing map. */
static WORD atirx_tex_image[ATIRX_BLOCK_SIZE][ATIRX_BLOCK_SIZE];

static void atirx_tex_pixel(void *context, v9x_s32 x, v9x_s32 y, v9x_u32 s,
                            v9x_u32 t)
{
    (void)context;
    if (x < 0l || y < 0l || x >= (v9x_s32)ATIRX_BLOCK_SIZE ||
        y >= (v9x_s32)ATIRX_BLOCK_SIZE) {
        return;
    }
    atirx_tex_image[y][x] = atirx_texel(
        v9x_r2_texel_index(s, ATIRX_TEX_LOG2, ATIRX_TEX_LOG2),
        v9x_r2_texel_index(t, ATIRX_TEX_LOG2, ATIRX_TEX_LOG2));
}

static int atirx_run_textri(struct v9x_m64_engine *engine,
                            const struct v9x_r2_target *target,
                            double q_range)
{
    struct v9x_r2_vertex v[3];
    struct v9x_r2_tex_coord c[3];
    struct v9x_r2_flat_trap traps[V9X_R2_SETUP_TRAPS];
    struct v9x_r2_st sts[V9X_R2_SETUP_TRAPS];
    struct v9x_r2_texture texture;
    v9x_u32 offsets[32];
    v9x_u32 values[32];
    v9x_u32 written;
    v9x_u32 count;
    v9x_u32 trap;
    v9x_u32 affine;
    v9x_status status;
    UINT index;
    UINT drawn_triangles = 0u;
    UINT slivers = 0u;
    UINT refused = 0u;
    DWORD total_mismatch = 0ul;
    DWORD total_pixels = 0ul;
    char label[16];
    char text[16];
    char key[48];
    char line[200];

    atirx_key("SceneSet", "textri");
    texture.offset = ATIRX_TEX_OFFSET;
    texture.log2_width = ATIRX_TEX_LOG2;
    texture.log2_height = ATIRX_TEX_LOG2;
    texture.log2_pitch = ATIRX_TEX_LOG2;
    texture.format = V9X_R2_TEX_FORMAT_565;
    texture.scale_3d_extra = 0ul;
    atirx_write_texture(ATIRX_TEX_OFFSET, ATIRX_TEX_SIZE, ATIRX_TEX_SIZE);

    for (index = 0u; index < ATIRX_TEXTRI_COUNT; ++index) {
        DWORD x;
        DWORD y;
        DWORD mismatch = 0ul;
        DWORD pixels = 0ul;
        double m[4];
        double offset_u = atirx_random_unit() * 128.0 - 64.0;
        double offset_v = atirx_random_unit() * 128.0 - 64.0;
        double qa = atirx_random_unit();
        double qb = atirx_random_unit() * (1.0 - qa);
        double estimate = 0.0;
        UINT k;
        int skip = 0;

        for (k = 0u; k < 4u; ++k) {
            m[k] = atirx_random_unit() * 4.0 - 2.0;
        }
        /* Planar-projective coordinates, as the host test makes them:
         * q screen-linear in 1..1+q_range (1 for the first twenty), and
         * tu*q, tv*q screen-linear. */
        for (k = 0u; k < 3u; ++k) {
            double px;
            double py;

            v[k].x = atirx_random_coord();
            v[k].y = atirx_random_coord();
            px = (double)v[k].x / 16.0;
            py = (double)v[k].y / 16.0;
            c[k].q = index < ATIRX_TEXTRI_AFFINE
                ? 1.0 : 1.0 + q_range * (qa * px + qb * py) / 64.0;
            c[k].tu = (offset_u + m[0] * px + m[1] * py) / (c[k].q * 32.0);
            c[k].tv = (offset_v + m[2] * px + m[3] * py) / (c[k].q * 32.0);
        }

        lstrcpyA(label, "X");
        atirx_decimal(text, index);
        lstrcatA(label, text);

        if (v9x_r2_setup_triangle(target, v, traps, &count) != V9X_STATUS_OK) {
            atirx_key(label, "SETUP-REFUSED");
            return 0;
        }
        for (trap = 0ul; trap < count; ++trap) {
            status = v9x_r2_setup_texture(v, c, &texture, &traps[trap],
                                          &sts[trap], &affine);
            if (status == V9X_STATUS_UNSUPPORTED) {
                skip = 1;
            } else if (status != V9X_STATUS_OK) {
                atirx_key(label, "TEXTURE-REFUSED");
                return 0;
            } else if (affine != 0ul) {
                ++slivers;
            }
        }
        if (skip || count == 0ul) {
            ++refused;
            continue;
        }
        (void)v9x_r2_texture_error(v, c, &texture, &estimate);

        atirx_prepare_block();
        status = V9X_STATUS_OK;
        for (trap = 0ul; trap < count && status == V9X_STATUS_OK; ++trap) {
            if (v9x_r2_build_texture_state(target, &texture, &sts[trap],
                                           offsets, values, 32ul,
                                           &written) != V9X_STATUS_OK) {
                atirx_key(label, "STATE-BUILD");
                return 0;
            }
            status = atirx_emit(engine, label, offsets, values, written);
            if (status != V9X_STATUS_OK) {
                break;
            }
            if (v9x_r2_build_trap(target, &traps[trap], offsets, values,
                                  32ul, &written) != V9X_STATUS_OK) {
                atirx_key(label, "TRAP-BUILD");
                return 0;
            }
            status = atirx_emit(engine, label, offsets, values, written);
        }
        if (status == V9X_STATUS_OK) {
            status = v9x_m64_wait_idle(engine, ATIRX_SPINS);
        }
        if (status != V9X_STATUS_OK) {
            atirx_key_hex("FailStatus", (DWORD)status);
            atirx_flush();
            status = v9x_m64_reset_replay(engine, ATIRX_SPINS);
            atirx_key_hex("ResetStatus", (DWORD)status);
            atirx_key("Result", "TIMEOUT-STOPPED");
            atirx_flush();
            return 0;
        }
        (void)v9x_m64_cpu_read_barrier(engine, ATIRX_SPINS);

        for (y = 0ul; y < ATIRX_BLOCK_SIZE; ++y) {
            for (x = 0ul; x < ATIRX_BLOCK_SIZE; ++x) {
                atirx_tex_image[y][x] = ATIRX_SENTINEL;
            }
        }
        for (trap = 0ul; trap < count; ++trap) {
            (void)v9x_r2_ref_walk_st(&traps[trap], &sts[trap],
                                     atirx_tex_pixel, 0);
        }
        line[0] = '\0';
        for (y = 0ul; y < ATIRX_BLOCK_SIZE; ++y) {
            for (x = 0ul; x < ATIRX_BLOCK_SIZE; ++x) {
                WORD got = *atirx_pixel(x, y);
                WORD want = ATIRX_SENTINEL;

                if (v9x_r2_ref_covers(v, (v9x_s32)x, (v9x_s32)y)) {
                    want = atirx_tex_image[y][x];
                    ++pixels;
                }
                if (got != want) {
                    ++mismatch;
                    if (lstrlenA(line) < 150) {
                        atirx_decimal(text, x);
                        lstrcatA(line, text);
                        lstrcatA(line, ",");
                        atirx_decimal(text, y);
                        lstrcatA(line, text);
                        lstrcatA(line, ":");
                        atirx_hex(text, got, 4);
                        lstrcatA(line, text);
                        lstrcatA(line, "/");
                        atirx_hex(text, want, 4);
                        lstrcatA(line, text);
                        lstrcatA(line, " ");
                    }
                }
            }
        }
        lstrcpyA(key, label);
        lstrcatA(key, "_Pixels");
        atirx_key_dec(key, pixels);
        lstrcpyA(key, label);
        lstrcatA(key, "_Mismatches");
        atirx_key_dec(key, mismatch);
        /* The fit's own distance from exact perspective, millitexels. */
        lstrcpyA(key, label);
        lstrcatA(key, "_FitErrorMilli");
        atirx_key_dec(key, atirx_round(estimate * 1000.0));
        if (line[0] != '\0') {
            lstrcpyA(key, label);
            lstrcatA(key, "_First");
            atirx_key(key, line);
        }
        total_mismatch += mismatch;
        total_pixels += pixels;
        ++drawn_triangles;
        atirx_flush();
    }

    offsets[0] = V9X_M64_SCALE_3D_CNTL;
    values[0] = 0ul;
    if (atirx_emit(engine, "end", offsets, values, 1ul) != V9X_STATUS_OK ||
        v9x_m64_wait_idle(engine, ATIRX_SPINS) != V9X_STATUS_OK) {
        return 0;
    }
    atirx_key_dec("TrianglesDrawn", drawn_triangles);
    atirx_key_dec("TrianglesRefused", refused);
    atirx_key_dec("SliverTraps", slivers);
    atirx_key_dec("PixelsCompared", total_pixels);
    atirx_key_dec("TotalMismatches", total_mismatch);
    atirx_key("Result", total_mismatch == 0ul ? "TEXTRI-MATCH"
                                              : "TEXTRI-DIFFER");
    return 1;
}

/* ---- Phase 4: bilinear, measured ---------------------------------------- */

/* A 32x32 checker in each channel: red 31 on odd u, green 63 on odd v,
 * blue 31 everywhere. A blend of two neighbours reads its weight to five
 * bits in red and six in green. */
#define ATIRX_CHECKER_OFFSET 0x00222000ul
/* A second checker for the 2x2 weights: red 31 where u + v is odd, green
 * 63 on odd u. */
#define ATIRX_CHECKER2_OFFSET 0x00222800ul
/* A third, pseudo-random 565 texels (the LCG below from a fixed seed), to
 * tell the 2x2 combination orders apart. */
#define ATIRX_NOISE_OFFSET    0x00223000ul

struct atirx_bil_scene {
    const char *name;
    v9x_u32 extra;           /* SCALE_3D_CNTL filter bits */
    struct v9x_r2_st st;
};

#define ATIRX_BIL (V9X_R2_BILINEAR_TEX_EN | V9X_R2_TEX_BLEND_2X2)

/* Fields: start, xinc_start, y_inc, x_inc2, y_inc2, xy_inc2, each {S, T}. */
static const struct atirx_bil_scene atirx_bil_scenes[] = {
    /* S from 0 by 1/16 texel a pixel, T in the middle of row 0. */
    { "B1SNearest", 0ul,
      { { 0l, ATIRX_TX(1) / 4l }, { ATIRX_TX(1) / 16l, 0l }, { 0l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    { "B2SBilinear", ATIRX_BIL,
      { { 0l, ATIRX_TX(1) / 4l }, { ATIRX_TX(1) / 16l, 0l }, { 0l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* Magnification filter alone. */
    { "B3SMagOnly", V9X_R2_BILINEAR_TEX_EN,
      { { 0l, ATIRX_TX(1) / 4l }, { ATIRX_TX(1) / 16l, 0l }, { 0l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* Minification filter alone. */
    { "B4SMinOnly", V9X_R2_TEX_BLEND_2X2,
      { { 0l, ATIRX_TX(1) / 4l }, { ATIRX_TX(1) / 16l, 0l }, { 0l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* T along the span instead, S in the middle of column 0. */
    { "B5TBilinear", ATIRX_BIL,
      { { ATIRX_TX(1) / 4l, 0l }, { 0l, ATIRX_TX(1) / 16l }, { 0l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* Both along the span: the 2x2 weights. */
    { "B6Diagonal", ATIRX_BIL,
      { { 0l, 0l }, { ATIRX_TX(1) / 16l, ATIRX_TX(1) / 16l }, { 0l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* Minifying, 2.5 texels a pixel: nearest, both filters, each alone. */
    { "B7MinNearest", 0ul,
      { { 0l, ATIRX_TX(1) / 4l }, { 5l * ATIRX_TX(1) / 2l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    { "B8MinBilinear", ATIRX_BIL,
      { { 0l, ATIRX_TX(1) / 4l }, { 5l * ATIRX_TX(1) / 2l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    { "B9MinMagOnly", V9X_R2_BILINEAR_TEX_EN,
      { { 0l, ATIRX_TX(1) / 4l }, { 5l * ATIRX_TX(1) / 2l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    { "B10MinMinOnly", V9X_R2_TEX_BLEND_2X2,
      { { 0l, ATIRX_TX(1) / 4l }, { 5l * ATIRX_TX(1) / 2l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } }
};

/*
 * /texlod: where minification starts, and from which derivative. The
 * minification filter alone (TEX_BLEND_FCN 2, no BILINEAR_TEX_EN) draws
 * nothing under magnification (B4), so a drawn row says "minifying".
 * /texbil2: the 2x2 combination on the u + v checker, S by 1/16 texel a
 * pixel and T by 1/8 a row.
 */
static const struct atirx_bil_scene atirx_lod_scenes[] = {
    { "L1Sx15", V9X_R2_TEX_BLEND_2X2,
      { { 0l, ATIRX_TX(1) / 4l }, { 15l * ATIRX_TX(1) / 16l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    { "L2Sx16", V9X_R2_TEX_BLEND_2X2,
      { { 0l, ATIRX_TX(1) / 4l }, { ATIRX_TX(1), 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    { "L3Sx17", V9X_R2_TEX_BLEND_2X2,
      { { 0l, ATIRX_TX(1) / 4l }, { 17l * ATIRX_TX(1) / 16l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    { "L4Sx20", V9X_R2_TEX_BLEND_2X2,
      { { 0l, ATIRX_TX(1) / 4l }, { 20l * ATIRX_TX(1) / 16l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    { "L5Sx24", V9X_R2_TEX_BLEND_2X2,
      { { 0l, ATIRX_TX(1) / 4l }, { 24l * ATIRX_TX(1) / 16l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    { "L6Sx32", V9X_R2_TEX_BLEND_2X2,
      { { 0l, ATIRX_TX(1) / 4l }, { 2l * ATIRX_TX(1), 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* S along x 1/2, T down the rows 2: the Y derivative minifies. */
    { "L7Ty32", V9X_R2_TEX_BLEND_2X2,
      { { 0l, 0l }, { ATIRX_TX(1) / 2l, 0l }, { 0l, 2l * ATIRX_TX(1) },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* S along x 1/2, T along x 2: T's X derivative minifies. */
    { "L8Tx32", V9X_R2_TEX_BLEND_2X2,
      { { 0l, 0l }, { ATIRX_TX(1) / 2l, 2l * ATIRX_TX(1) }, { 0l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* S from 1/4 a pixel growing by 1/16 a pixel: the change in the
     * middle of the span, if the decision is per pixel. */
    { "L9Grow", V9X_R2_TEX_BLEND_2X2,
      { { 0l, ATIRX_TX(1) / 4l }, { ATIRX_TX(1) / 4l, 0l }, { 0l, 0l },
        { ATIRX_TX(1) / 16l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* S along x 1/2, S down the rows 2. */
    { "L10Sy32", V9X_R2_TEX_BLEND_2X2,
      { { 0l, ATIRX_TX(1) / 4l }, { ATIRX_TX(1) / 2l, 0l },
        { 2l * ATIRX_TX(1), 0l }, { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } }
};

static const struct atirx_bil_scene atirx_bil3_scenes[] = {
    { "N1Weights", ATIRX_BIL,
      { { 0l, 0l }, { ATIRX_TX(1) / 16l, 0l }, { 0l, ATIRX_TX(1) / 8l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    { "N2Skew", ATIRX_BIL,
      { { ATIRX_TX(3), ATIRX_TX(5) }, { 3l * ATIRX_TX(1) / 16l,
        ATIRX_TX(1) / 16l }, { ATIRX_TX(1) / 16l, 3l * ATIRX_TX(1) / 8l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } }
};

static const struct atirx_bil_scene atirx_bil2_scenes[] = {
    { "W1Weights", ATIRX_BIL,
      { { 0l, 0l }, { ATIRX_TX(1) / 16l, 0l }, { 0l, ATIRX_TX(1) / 8l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } }
};

static void atirx_write_checker(void)
{
    DWORD u;
    DWORD v;
    volatile WORD *base = atirx_fb + ATIRX_CHECKER_OFFSET / 2ul;
    volatile WORD *base2 = atirx_fb + ATIRX_CHECKER2_OFFSET / 2ul;

    for (v = 0ul; v < ATIRX_TEX_SIZE; ++v) {
        for (u = 0ul; u < ATIRX_TEX_SIZE; ++u) {
            base[v * ATIRX_TEX_SIZE + u] =
                (WORD)(((u & 1ul) != 0ul ? 0xf800u : 0u) |
                       ((v & 1ul) != 0ul ? 0x07e0u : 0u) | 0x001fu);
            base2[v * ATIRX_TEX_SIZE + u] =
                (WORD)((((u + v) & 1ul) != 0ul ? 0xf800u : 0u) |
                       ((u & 1ul) != 0ul ? 0x07e0u : 0u) | 0x001fu);
        }
    }
}

static void atirx_write_noise(void)
{
    DWORD index;
    DWORD state = 0x13579bdful;
    volatile WORD *base = atirx_fb + ATIRX_NOISE_OFFSET / 2ul;

    for (index = 0ul; index < ATIRX_TEX_SIZE * ATIRX_TEX_SIZE; ++index) {
        state = state * 1103515245ul + 12345ul;
        base[index] = (WORD)(state >> 16);
    }
}

/* Rows 16..23, columns 16..47, as raw 565 hex. */
static void atirx_dump_hex(const char *prefix)
{
    char key[48];
    char text[8];
    char line[200];
    DWORD x;
    DWORD y;

    for (y = 16ul; y < 24ul; ++y) {
        line[0] = '\0';
        for (x = 16ul; x < 48ul; ++x) {
            atirx_hex(text, *atirx_pixel(x, y), 4);
            lstrcatA(line, text);
            lstrcatA(line, " ");
        }
        lstrcpyA(key, prefix);
        lstrcatA(key, "HEX");
        atirx_decimal(text, y);
        lstrcatA(key, text);
        atirx_key(key, line);
    }
}

/* Rows 16 and 23, columns 15..49: red.green for blue-31 pixels, '-' for
 * anything else. */
static void atirx_dump_rg(const char *prefix, int all_rows)
{
    static const DWORD rows[8] = { 16ul, 23ul, 17ul, 18ul, 19ul, 20ul,
                                   21ul, 22ul };
    char key[48];
    char text[8];
    char line[300];
    DWORD x;
    UINT index;

    for (index = 0u; index < (all_rows ? 8u : 2u); ++index) {
        line[0] = '\0';
        for (x = 15ul; x < 50ul; ++x) {
            WORD value = *atirx_pixel(x, rows[index]);

            if ((value & 31u) != 31u) {
                lstrcatA(line, "- ");
                continue;
            }
            atirx_decimal(text, value >> 11);
            lstrcatA(line, text);
            lstrcatA(line, ".");
            atirx_decimal(text, (value >> 5) & 63u);
            lstrcatA(line, text);
            lstrcatA(line, " ");
        }
        lstrcpyA(key, prefix);
        lstrcatA(key, "RG");
        atirx_decimal(text, rows[index]);
        lstrcatA(key, text);
        atirx_key(key, line);
    }
}

static int atirx_run_texbil(struct v9x_m64_engine *engine,
                            const struct v9x_r2_target *target,
                            const struct atirx_bil_scene *scenes,
                            UINT count, const char *set, DWORD map,
                            int all_rows)
{
    static const struct v9x_r2_flat_trap rect = ATIRX_RECT;
    struct v9x_r2_texture texture;
    v9x_u32 offsets[32];
    v9x_u32 values[32];
    v9x_u32 written;
    UINT index;
    char prefix[48];

    atirx_key("SceneSet", set);
    texture.offset = map;
    texture.log2_width = ATIRX_TEX_LOG2;
    texture.log2_height = ATIRX_TEX_LOG2;
    texture.log2_pitch = ATIRX_TEX_LOG2;
    texture.format = V9X_R2_TEX_FORMAT_565;
    atirx_write_checker();
    atirx_write_noise();

    for (index = 0u; index < count; ++index) {
        const struct atirx_bil_scene *scene = &scenes[index];

        atirx_prefix(prefix, scene->name);
        atirx_key("Scene", scene->name);
        atirx_prepare_block();
        texture.scale_3d_extra = scene->extra;
        if (v9x_r2_build_texture_state(target, &texture, &scene->st,
                                       offsets, values, 32ul, &written) !=
                V9X_STATUS_OK ||
            atirx_emit(engine, scene->name, offsets, values, written) !=
                V9X_STATUS_OK ||
            v9x_r2_build_trap(target, &rect, offsets, values, 32ul,
                              &written) != V9X_STATUS_OK ||
            atirx_emit(engine, scene->name, offsets, values, written) !=
                V9X_STATUS_OK) {
            atirx_key("Result", "TEXBIL-EMIT");
            return 0;
        }
        if (!atirx_finish_draw(engine)) {
            return 0;
        }
        if (map == ATIRX_NOISE_OFFSET) {
            atirx_dump_hex(prefix);
        } else {
            atirx_dump_rg(prefix, all_rows);
        }
        atirx_report_block(prefix);
        atirx_flush();
    }

    offsets[0] = V9X_M64_SCALE_3D_CNTL;
    values[0] = 0ul;
    if (atirx_emit(engine, "end", offsets, values, 1ul) != V9X_STATUS_OK ||
        v9x_m64_wait_idle(engine, ATIRX_SPINS) != V9X_STATUS_OK) {
        return 0;
    }
    atirx_key("Result", "TEXBIL-RUN");
    return 1;
}

/* ---- Phase 4: formats, texture modes, blending, fog, scissor ------------ */

/*
 * /texmix. Three 32x32 maps whose every row is the same 32 test colours,
 * one per column, in RGB565, ARGB1555 and ARGB4444. The rectangle steps S
 * one texel a pixel, so its row 16 shows the operation on all 32 texels.
 * The block's background is the 5AA5 sentinel, which is the destination
 * the blend scenes read.
 */
#define ATIRX_PAL565_OFFSET  0x00224000ul
#define ATIRX_PAL1555_OFFSET 0x00224800ul
#define ATIRX_PAL4444_OFFSET 0x00225000ul

/* Colour channels 0..255 as the S.8.12 interpolators take them. */
#define ATIRX_C8(n) ((v9x_s32)(n) << 16)

struct atirx_mix_scene {
    const char *name;
    v9x_u32 format;          /* V9X_R2_TEX_FORMAT_* */
    v9x_u32 extra;           /* SCALE_3D_CNTL bits */
    v9x_s32 color[4];        /* R, G, B, A starts, 16.16 */
    v9x_s32 alpha_x_inc;
    v9x_u32 fog;             /* DP_FRGD_CLR, 565 */
    int wide;                /* the rectangle from 2 to 62, across the
                              * scissor (8..55) */
};

#define ATIRX_SRC(f) ((v9x_u32)(f) << V9X_R2_BLEND_SRC_SHIFT)
#define ATIRX_DST(f) ((v9x_u32)(f) << V9X_R2_BLEND_DST_SHIFT)
#define ATIRX_WHITE  { ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(255), \
                       ATIRX_C8(255) }

static const struct atirx_mix_scene atirx_mix_scenes[] = {
    /* Formats under replace. */
    { "F1Rgb565", V9X_R2_TEX_FORMAT_565, 0ul, ATIRX_WHITE, 0l, 0ul, 0 },
    { "F2Argb1555", V9X_R2_TEX_FORMAT_1555, 0ul, ATIRX_WHITE, 0l, 0ul, 0 },
    { "F3Argb4444", V9X_R2_TEX_FORMAT_4444, 0ul, ATIRX_WHITE, 0l, 0ul, 0 },
    { "F4Argb1555Aen", V9X_R2_TEX_FORMAT_1555, V9X_R2_TEX_MAP_AEN,
      ATIRX_WHITE, 0l, 0ul, 0 },
    /* The alpha LSB as a mask: alpha 0 not drawn. */
    { "F5Argb1555Mask", V9X_R2_TEX_FORMAT_1555,
      V9X_R2_TEX_MAP_AEN | V9X_R2_TEX_AMASK_AEN, ATIRX_WHITE, 0l, 0ul, 0 },
    { "F6Argb4444Mask", V9X_R2_TEX_FORMAT_4444,
      V9X_R2_TEX_MAP_AEN | V9X_R2_TEX_AMASK_AEN, ATIRX_WHITE, 0l, 0ul, 0 },
    /* Modulate by a flat interpolator colour: do the colour interpolators
     * run under texture mapping at all? */
    { "M1ModWhite", V9X_R2_TEX_FORMAT_565, V9X_R2_TEX_LIGHT_MODULATE,
      ATIRX_WHITE, 0l, 0ul, 0 },
    { "M2ModHalf", V9X_R2_TEX_FORMAT_565, V9X_R2_TEX_LIGHT_MODULATE,
      { ATIRX_C8(128), ATIRX_C8(128), ATIRX_C8(128), ATIRX_C8(255) },
      0l, 0ul, 0 },
    { "M3ModMixed", V9X_R2_TEX_FORMAT_565, V9X_R2_TEX_LIGHT_MODULATE,
      { ATIRX_C8(64), ATIRX_C8(192), ATIRX_C8(255), ATIRX_C8(255) },
      0l, 0ul, 0 },
    /* Alpha decal: texel by its alpha over the interpolator colour. */
    { "M4Decal4444", V9X_R2_TEX_FORMAT_4444,
      V9X_R2_TEX_MAP_AEN | V9X_R2_TEX_LIGHT_DECAL,
      { ATIRX_C8(255), 0l, 0l, ATIRX_C8(255) }, 0l, 0ul, 0 },
    /* Blends over the 5AA5 background. */
    { "A1TexAlpha", V9X_R2_TEX_FORMAT_4444,
      V9X_R2_TEX_MAP_AEN | V9X_R2_ALPHA_FOG_BLEND | ATIRX_SRC(4) |
          ATIRX_DST(5), ATIRX_WHITE, 0l, 0ul, 0 },
    { "A2OneOne", V9X_R2_TEX_FORMAT_565,
      V9X_R2_ALPHA_FOG_BLEND | ATIRX_SRC(1) | ATIRX_DST(1), ATIRX_WHITE,
      0l, 0ul, 0 },
    { "A3ZeroSrc", V9X_R2_TEX_FORMAT_565,
      V9X_R2_ALPHA_FOG_BLEND | ATIRX_SRC(0) | ATIRX_DST(2), ATIRX_WHITE,
      0l, 0ul, 0 },
    { "A4DstZero", V9X_R2_TEX_FORMAT_565,
      V9X_R2_ALPHA_FOG_BLEND | ATIRX_SRC(2) | ATIRX_DST(0), ATIRX_WHITE,
      0l, 0ul, 0 },
    /* Interpolator alpha 64 with a texture that has none. */
    { "A5IterAlpha", V9X_R2_TEX_FORMAT_565,
      V9X_R2_ALPHA_FOG_BLEND | ATIRX_SRC(4) | ATIRX_DST(5),
      { ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(64) },
      0l, 0ul, 0 },
    { "A6InvInv", V9X_R2_TEX_FORMAT_565,
      V9X_R2_ALPHA_FOG_BLEND | ATIRX_SRC(3) | ATIRX_DST(3), ATIRX_WHITE,
      0l, 0ul, 0 },
    /* An alpha ramp, 0 to 248 by 8 a pixel, for the blend weight. */
    { "A7AlphaRamp", V9X_R2_TEX_FORMAT_565,
      V9X_R2_ALPHA_FOG_BLEND | ATIRX_SRC(4) | ATIRX_DST(5),
      { ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(255), 0l },
      ATIRX_C8(8), 0ul, 0 },
    /* Fog: DP_FRGD_CLR, the factor from the alpha interpolator. */
    { "G1FogFlat", V9X_R2_TEX_FORMAT_565, V9X_R2_ALPHA_FOG_FOG,
      { ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(64) },
      0l, 0x001fu, 0 },
    { "G2FogRamp", V9X_R2_TEX_FORMAT_565, V9X_R2_ALPHA_FOG_FOG,
      { ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(255), 0l },
      ATIRX_C8(8), 0xffffu, 0 },
    /* G1/G2 drew black with the blend factors 0: fog with As / 1-As,
     * the fog colour as 565 and as 8888. */
    { "G3FogFactors565", V9X_R2_TEX_FORMAT_565,
      V9X_R2_ALPHA_FOG_FOG | ATIRX_SRC(4) | ATIRX_DST(5),
      { ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(64) },
      0l, 0x0000f800ul, 0 },
    { "G4FogFactors8888", V9X_R2_TEX_FORMAT_565,
      V9X_R2_ALPHA_FOG_FOG | ATIRX_SRC(4) | ATIRX_DST(5),
      { ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(64) },
      0l, 0x00ff0000ul, 0 },
    { "G5FogRamp565", V9X_R2_TEX_FORMAT_565,
      V9X_R2_ALPHA_FOG_FOG | ATIRX_SRC(4) | ATIRX_DST(5),
      { ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(255), 0l },
      ATIRX_C8(8), 0x0000f800ul, 0 },
    /* Fog with one/zero: is the source still the texel? */
    { "G6FogOneZero", V9X_R2_TEX_FORMAT_565,
      V9X_R2_ALPHA_FOG_FOG | ATIRX_SRC(1) | ATIRX_DST(0),
      { ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(64) },
      0l, 0x0000f800ul, 0 },
    { "G7FogZeroOne", V9X_R2_TEX_FORMAT_565,
      V9X_R2_ALPHA_FOG_FOG | ATIRX_SRC(0) | ATIRX_DST(1),
      { ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(255), ATIRX_C8(64) },
      0l, 0x0000f800ul, 0 },
    /* Across the scissor. */
    { "C1Scissor", V9X_R2_TEX_FORMAT_565, 0ul, ATIRX_WHITE, 0l, 0ul, 1 }
};

static WORD atirx_pal_565(DWORD u)
{
    return (WORD)((u << 11) | (((2ul * u + 1ul) & 63ul) << 5) | (31ul - u));
}

static WORD atirx_pal_1555(DWORD u)
{
    return (WORD)(((u & 1ul) << 15) | (u << 10) | ((31ul - u) << 5) |
                  ((7ul * u) & 31ul));
}

static WORD atirx_pal_4444(DWORD u)
{
    return (WORD)(((u >> 1) << 12) | ((15ul - (u >> 1)) << 8) |
                  ((u & 15ul) << 4) | ((5ul * u) & 15ul));
}

static void atirx_write_palettes(void)
{
    DWORD u;
    DWORD v;

    for (v = 0ul; v < ATIRX_TEX_SIZE; ++v) {
        for (u = 0ul; u < ATIRX_TEX_SIZE; ++u) {
            DWORD at = v * ATIRX_TEX_SIZE + u;

            atirx_fb[ATIRX_PAL565_OFFSET / 2ul + at] = atirx_pal_565(u);
            atirx_fb[ATIRX_PAL1555_OFFSET / 2ul + at] = atirx_pal_1555(u);
            atirx_fb[ATIRX_PAL4444_OFFSET / 2ul + at] = atirx_pal_4444(u);
        }
    }
}

/* Rows 16 and 23, columns 0..63 for the wide scene and 16..47 otherwise,
 * as raw 565 hex. */
static void atirx_dump_mix(const char *prefix, int wide)
{
    static const DWORD rows[2] = { 16ul, 23ul };
    char key[48];
    char text[8];
    char line[340];
    DWORD x;
    UINT index;

    for (index = 0u; index < 2u; ++index) {
        line[0] = '\0';
        for (x = wide ? 0ul : 16ul; x < (wide ? 64ul : 48ul); ++x) {
            atirx_hex(text, *atirx_pixel(x, rows[index]), 4);
            lstrcatA(line, text);
            lstrcatA(line, " ");
        }
        lstrcpyA(key, prefix);
        lstrcatA(key, "HEX");
        atirx_decimal(text, rows[index]);
        lstrcatA(key, text);
        atirx_key(key, line);
    }
}

static int atirx_run_texmix(struct v9x_m64_engine *engine,
                            const struct v9x_r2_target *target)
{
    static const struct v9x_r2_flat_trap rect = ATIRX_RECT;
    struct v9x_r2_texture texture;
    struct v9x_r2_flat_trap trap;
    struct v9x_r2_st st;
    v9x_u32 offsets[32];
    v9x_u32 values[32];
    v9x_u32 written;
    UINT index;
    UINT count = sizeof(atirx_mix_scenes) / sizeof(atirx_mix_scenes[0]);
    UINT channel;
    char prefix[48];

    atirx_key("SceneSet", "texmix");
    atirx_write_palettes();
    texture.log2_width = ATIRX_TEX_LOG2;
    texture.log2_height = ATIRX_TEX_LOG2;
    texture.log2_pitch = ATIRX_TEX_LOG2;
    /* S one texel a pixel from texel 0's centre, T mid-row. */
    st.start[0] = ATIRX_TX(1) / 2l;
    st.start[1] = ATIRX_TX(1) / 2l;
    st.xinc_start[0] = ATIRX_TX(1);
    st.xinc_start[1] = 0l;
    st.y_inc[0] = 0l;
    st.y_inc[1] = 0l;
    st.x_inc2[0] = 0l;
    st.x_inc2[1] = 0l;
    st.y_inc2[0] = 0l;
    st.y_inc2[1] = 0l;
    st.xy_inc2[0] = 0l;
    st.xy_inc2[1] = 0l;

    for (index = 0u; index < count; ++index) {
        const struct atirx_mix_scene *scene = &atirx_mix_scenes[index];
        v9x_u32 at = 0ul;

        atirx_prefix(prefix, scene->name);
        atirx_key("Scene", scene->name);
        atirx_prepare_block();
        texture.format = scene->format;
        texture.offset = scene->format == V9X_R2_TEX_FORMAT_565
            ? ATIRX_PAL565_OFFSET
            : scene->format == V9X_R2_TEX_FORMAT_1555
                ? ATIRX_PAL1555_OFFSET : ATIRX_PAL4444_OFFSET;
        texture.scale_3d_extra = scene->extra;
        trap = rect;
        if (scene->wide) {
            trap.x = 2ul;
            trap.trail_x = 62ul;
        }
        if (v9x_r2_build_texture_state(target, &texture, &st, offsets,
                                       values, 32ul, &written) !=
                V9X_STATUS_OK ||
            atirx_emit(engine, scene->name, offsets, values, written) !=
                V9X_STATUS_OK) {
            atirx_key("Result", "TEXMIX-STATE");
            return 0;
        }
        /* The colour and alpha interpolators, flat but for alpha's X
         * increment, after SCALE_3D_CNTL (RRG p.6-7: accumulators only
         * with a non-zero function), then the fog colour. */
        for (channel = 0u; channel < 3u; ++channel) {
            offsets[at] = V9X_R2_RED_X_INC + 12ul * channel;
            values[at++] = 0ul;
            offsets[at] = V9X_R2_RED_Y_INC + 12ul * channel;
            values[at++] = 0ul;
            offsets[at] = V9X_R2_RED_START + 12ul * channel;
            values[at++] = (v9x_u32)scene->color[channel] &
                           V9X_R2_COLOR_MASK;
        }
        offsets[at] = V9X_R2_ALPHA_X_INC;
        values[at++] = (v9x_u32)scene->alpha_x_inc & V9X_R2_COLOR_MASK;
        offsets[at] = V9X_R2_ALPHA_Y_INC;
        values[at++] = 0ul;
        offsets[at] = V9X_R2_ALPHA_START;
        values[at++] = (v9x_u32)scene->color[3] & V9X_R2_COLOR_MASK;
        offsets[at] = V9X_M64_DP_FRGD_CLR;
        values[at++] = scene->fog;
        if (atirx_emit(engine, scene->name, offsets, values, at) !=
                V9X_STATUS_OK ||
            v9x_r2_build_trap(target, &trap, offsets, values, 32ul,
                              &written) != V9X_STATUS_OK ||
            atirx_emit(engine, scene->name, offsets, values, written) !=
                V9X_STATUS_OK) {
            atirx_key("Result", "TEXMIX-EMIT");
            return 0;
        }
        if (!atirx_finish_draw(engine)) {
            return 0;
        }
        atirx_dump_mix(prefix, scene->wide);
        atirx_report_block(prefix);
        atirx_flush();
    }

    offsets[0] = V9X_M64_SCALE_3D_CNTL;
    values[0] = 0ul;
    if (atirx_emit(engine, "end", offsets, values, 1ul) != V9X_STATUS_OK ||
        v9x_m64_wait_idle(engine, ATIRX_SPINS) != V9X_STATUS_OK) {
        return 0;
    }
    atirx_key("Result", "TEXMIX-RUN");
    return 1;
}

/* ---- The scanout start, watched on the monitor -------------------------- */

/*
 * /crtc: whether CRTC_OFF_PITCH moves the picture in the VBE modes, and
 * when a write takes effect. The agent's screenshots read the primary's
 * memory, not the scanout, so the answer is what someone at the monitor
 * sees. docs\issues\2026-10-02-rage-iic-flips-never-presented.md.
 *
 * Read-only first: the CRTC must say 1024x768 at offset 0 with a pitch of
 * 1024 pixels (the desktop), or nothing is written. A test picture is
 * drawn at VRAM 0x200000, past the 1.5 MiB front buffer. Then:
 *   A (10 s): the scanout pointed at the picture, then back;
 *   B (10 s): each frame, the picture at line 384 and the desktop at line
 *   600. A register applied at once shows a band of the picture across
 *   the middle; one latched at the frame start shows only the desktop.
 * The original value is written back on every path out.
 */
#define ATIRX_CRTC_H_TOTAL_DISP 0x400u
#define ATIRX_CRTC_V_TOTAL_DISP 0x408u
#define ATIRX_CRTC_VLINE        0x410u
#define ATIRX_CRTC_OFF_PITCH    0x414u
#define ATIRX_CRTC_PICTURE      0x00200000ul
#define ATIRX_CRTC_WIDTH        1024ul
#define ATIRX_CRTC_HEIGHT       768ul
#define ATIRX_CRTC_BAND_TOP     384ul
#define ATIRX_CRTC_BAND_BOTTOM  600ul
#define ATIRX_CRTC_SPIN         20000000ul

static DWORD atirx_crtc_vline(void)
{
    return (atirx_read(0, ATIRX_CRTC_VLINE) >> 16) & 0x7fful;
}

/* Spin until the current line is in [low, high); 0 if it never comes. */
static int atirx_crtc_wait_line(DWORD low, DWORD high)
{
    DWORD spins;

    for (spins = 0ul; spins < ATIRX_CRTC_SPIN; ++spins) {
        DWORD line = atirx_crtc_vline();

        if (line >= low && line < high) {
            return 1;
        }
    }
    return 0;
}

static void atirx_crtc_picture(void)
{
    DWORD x;
    DWORD y;
    volatile WORD *base = atirx_fb + ATIRX_CRTC_PICTURE / 2ul;

    /* Top half red, bottom half blue, a white diagonal band and a green
     * frame 16 pixels wide: unmistakable, and nothing like the desktop. */
    for (y = 0ul; y < ATIRX_CRTC_HEIGHT; ++y) {
        for (x = 0ul; x < ATIRX_CRTC_WIDTH; ++x) {
            WORD value = y < ATIRX_CRTC_HEIGHT / 2ul ? 0xf800u : 0x001fu;

            if (x < 16ul || y < 16ul || x >= ATIRX_CRTC_WIDTH - 16ul ||
                y >= ATIRX_CRTC_HEIGHT - 16ul) {
                value = 0x07e0u;
            } else if ((x > y ? x - y : y - x) < 24ul) {
                value = 0xffffu;
            }
            base[y * ATIRX_CRTC_WIDTH + x] = value;
        }
    }
}

static void atirx_crtc_hold(DWORD ms)
{
    DWORD start = GetTickCount();

    while (GetTickCount() - start < ms) {
        Sleep(50);
    }
}

static int atirx_run_crtc(int read_only)
{
    DWORD h_total;
    DWORD v_total;
    DWORD original;
    DWORD picture;
    DWORD h_disp;
    DWORD v_disp;
    DWORD v_all;
    DWORD lines[8];
    DWORD index;
    DWORD frames = 0ul;
    DWORD misses = 0ul;
    DWORD start;
    DWORD readback;
    char key[32];
    char text[16];

    atirx_key("SceneSet", "crtc");
    h_total = atirx_read(0, ATIRX_CRTC_H_TOTAL_DISP);
    v_total = atirx_read(0, ATIRX_CRTC_V_TOTAL_DISP);
    original = atirx_read(0, ATIRX_CRTC_OFF_PITCH);
    h_disp = (((h_total >> 16) & 0xfful) + 1ul) * 8ul;
    v_disp = ((v_total >> 16) & 0x7fful) + 1ul;
    v_all = (v_total & 0x7fful) + 1ul;
    atirx_key_hex("CrtcHTotalDisp", h_total);
    atirx_key_hex("CrtcVTotalDisp", v_total);
    atirx_key_hex("CrtcOffPitch", original);
    atirx_key_dec("CrtcHDisp", h_disp);
    atirx_key_dec("CrtcVDisp", v_disp);
    atirx_key_dec("CrtcVTotal", v_all);
    for (index = 0ul; index < 8ul; ++index) {
        lines[index] = atirx_crtc_vline();
        Sleep(3);
    }
    for (index = 0ul; index < 8ul; ++index) {
        lstrcpyA(key, "CrtcVline");
        atirx_decimal(text, index);
        lstrcatA(key, text);
        atirx_key_dec(key, lines[index]);
    }
    atirx_flush();

    /* The desktop, exactly, or nothing is written. */
    if (h_disp != ATIRX_CRTC_WIDTH || v_disp != ATIRX_CRTC_HEIGHT ||
        (original & 0x000ffffful) != 0ul ||
        ((original >> 22) & 0x3fful) != ATIRX_CRTC_WIDTH / 8ul ||
        v_all <= ATIRX_CRTC_BAND_BOTTOM) {
        atirx_key("Result", "CRTC-NOT-DESKTOP");
        return 0;
    }
    if (read_only) {
        atirx_key("Result", "CRTC-READ");
        return 1;
    }
    picture = (original & 0xfff00000ul) | (ATIRX_CRTC_PICTURE / 8ul);
    atirx_crtc_picture();
    atirx_key("Phase", "picture drawn; A starts in 5 s");
    atirx_flush();
    atirx_crtc_hold(5000ul);

    /* A: the whole picture for ten seconds. */
    atirx_write(0, ATIRX_CRTC_OFF_PITCH, picture);
    readback = atirx_read(0, ATIRX_CRTC_OFF_PITCH);
    atirx_key_hex("PhaseAReadback", readback);
    atirx_key("Phase", "A: picture");
    atirx_flush();
    atirx_crtc_hold(10000ul);
    atirx_write(0, ATIRX_CRTC_OFF_PITCH, original);
    atirx_key_hex("PhaseARestored", atirx_read(0, ATIRX_CRTC_OFF_PITCH));
    atirx_key("Phase", "A done; B starts in 5 s");
    atirx_flush();
    atirx_crtc_hold(5000ul);

    /* B: picture at line 384, desktop at line 600, every frame. */
    atirx_key("Phase", "B: band test");
    atirx_flush();
    start = GetTickCount();
    while (GetTickCount() - start < 10000ul) {
        if (!atirx_crtc_wait_line(ATIRX_CRTC_BAND_TOP,
                                  ATIRX_CRTC_BAND_TOP + 8ul)) {
            ++misses;
            break;
        }
        atirx_write(0, ATIRX_CRTC_OFF_PITCH, picture);
        if (!atirx_crtc_wait_line(ATIRX_CRTC_BAND_BOTTOM,
                                  ATIRX_CRTC_BAND_BOTTOM + 8ul)) {
            ++misses;
            break;
        }
        atirx_write(0, ATIRX_CRTC_OFF_PITCH, original);
        ++frames;
    }
    atirx_write(0, ATIRX_CRTC_OFF_PITCH, original);
    atirx_key_dec("PhaseBFrames", frames);
    atirx_key_dec("PhaseBMisses", misses);
    atirx_key_hex("PhaseBRestored", atirx_read(0, ATIRX_CRTC_OFF_PITCH));
    atirx_key("Result", misses == 0ul ? "CRTC-RUN" : "CRTC-LINE-LOST");
    return 1;
}

static void atirx_prefix(char *prefix, const char *name)
{
    lstrcpyA(prefix, name);
    lstrcatA(prefix, "_");
}

/* ---- A read-only picture of VRAM -------------------------------------- */

/*
 * /vramdump: the whole 4 MiB aperture to C:\V9XDIAG\VRAM.BIN, and the CRTC
 * registers beside it in ATIRX.TXT. Reads only, and taken before the
 * engine initialisation every other mode does, so it can run while a
 * Direct3D application is drawing and show what the engine left in the
 * back, front and Z buffers - which the agent's screenshots, reading the
 * GDI primary, cannot.
 */
#define ATIRX_VRAM_BYTES  0x00400000ul
#define ATIRX_DUMP_CHUNK  0x00010000ul

static BYTE atirx_dump_buffer[ATIRX_DUMP_CHUNK];

/*
 * The engine's cost per feature: one 256x256 quad drawn through the HAL's
 * own builders (v9x_r2_check_draw, _build_draw_state, _split_triangle,
 * _build_piece), its register stream built once and emitted
 * ATIRX_FILL_REPEAT times, timed by the TSC from the first write until
 * the engine is idle. Each scene adds one feature, so the differences
 * price them. The TSC rate is measured against GetTickCount.
 */
#define ATIRX_FILL_TARGET    0x00230000ul   /* 256x256 565, 128 KiB */
#define ATIRX_FILL_Z         0x00250000ul   /* 256x256 Z, 128 KiB */
#define ATIRX_FILL_TEX64     0x00270000ul   /* 64x64 565 */
#define ATIRX_FILL_TEX256    0x00272000ul   /* 256x256 565, to 0x292000 */
#define ATIRX_FILL_EDGE      256ul
#define ATIRX_FILL_PITCH     512ul
#define ATIRX_FILL_REPEAT    16ul
#define ATIRX_FILL_DWORDS    6144ul

/* Where /fill draws; /fillz moves them. */
static DWORD atirx_fill_target = ATIRX_FILL_TARGET;
static DWORD atirx_fill_z = ATIRX_FILL_Z;
static DWORD atirx_fill_pitch = ATIRX_FILL_PITCH;
/* SCALE_3D_CNTL bits /fillcache clears and sets after the HAL's builder:
 * ATI's driver drew Half-Life with bit 5 (our TEX_CACHE_DIS) clear and
 * bits 0-3 and 9 set (ATIRX /sample, 2026-10-03). */
static DWORD atirx_fill_scale_clear = 0ul;
static DWORD atirx_fill_scale_set = 0ul;

static DWORD atirx_tsc_low(void);
#pragma aux atirx_tsc_low = 0x0f 0x31 value [eax] modify exact [eax edx];

struct atirx_fill_scene {
    const char *name;
    v9x_u32 textured;
    v9x_u32 filter;             /* 1 nearest, 2 linear */
    v9x_u32 log2_texture;       /* 6 or 8 */
    v9x_u32 depth;              /* 0, or 1 for LESSEQUAL with write */
    v9x_u32 src_blend;          /* 0: no blend */
    v9x_u32 dst_blend;
    v9x_u32 perspective;        /* q 1 at the top, 0.4 at the bottom */
    double repeat;              /* map widths across the quad; 0: default */
};

static const struct atirx_fill_scene atirx_fill_scenes[] = {
    { "flat",           0ul, 0ul, 0ul, 0ul, 0ul, 0ul, 0ul },
    { "flat-z",         0ul, 0ul, 0ul, 1ul, 0ul, 0ul, 0ul },
    { "point64",        1ul, 1ul, 6ul, 0ul, 0ul, 0ul, 0ul },
    { "bilinear64",     1ul, 2ul, 6ul, 0ul, 0ul, 0ul, 0ul },
    { "bilinear256",    1ul, 2ul, 8ul, 0ul, 0ul, 0ul, 0ul },
    { "bilinear64-z",   1ul, 2ul, 6ul, 1ul, 0ul, 0ul, 0ul },
    { "bilinear64-z-persp", 1ul, 2ul, 6ul, 1ul, 0ul, 0ul, 1ul },
    { "bilinear64-z-alpha", 1ul, 2ul, 6ul, 1ul, 5ul, 6ul, 0ul },
    /* Quake 2's lightmap pass: ZERO, SRCCOLOR over the world pass. */
    { "bilinear64-z-lightmap", 1ul, 2ul, 6ul, 1ul, 1ul, 3ul, 0ul },
    { "point64-z",      1ul, 1ul, 6ul, 1ul, 0ul, 0ul, 0ul },
    /* Minification. Level 0 of a 256x256 map at 4 texels a pixel, as the
     * HAL samples every mip-mapped texture, against the 64x64 map at 1:1
     * that mip level 2 would be. Scenes 10-15. */
    { "bilinear256-min4",   1ul, 2ul, 8ul, 0ul, 0ul, 0ul, 0ul, 4.0 },
    { "bilinear64-min1",    1ul, 2ul, 6ul, 0ul, 0ul, 0ul, 0ul, 4.0 },
    { "bilinear256-min4-z", 1ul, 2ul, 8ul, 1ul, 0ul, 0ul, 0ul, 4.0 },
    { "bilinear64-min1-z",  1ul, 2ul, 6ul, 1ul, 0ul, 0ul, 0ul, 4.0 },
    { "point256-min4",      1ul, 1ul, 8ul, 0ul, 0ul, 0ul, 0ul, 4.0 },
    { "point64-min1",       1ul, 1ul, 6ul, 0ul, 0ul, 0ul, 0ul, 4.0 }
};

static v9x_u32 atirx_fill_offsets[ATIRX_FILL_DWORDS];

/* No runtime: memset is not linked. */
static void atirx_zero(void *target, DWORD bytes)
{
    BYTE *at = (BYTE *)target;

    while (bytes-- != 0ul) {
        *at++ = 0u;
    }
}
static v9x_u32 atirx_fill_values[ATIRX_FILL_DWORDS];

/* One scene's whole stream: state, then both triangles' pieces. 0 when a
 * builder refuses it. */
static v9x_u32 atirx_fill_build(const struct atirx_fill_scene *scene,
                                DWORD fb_bytes)
{
    static struct v9x_r2_draw_vertex pieces[V9X_R2_DRAW_SPLIT_MAX * 3u];
    static struct v9x_r2_texture_fit fits[V9X_R2_DRAW_SPLIT_MAX];
    struct v9x_m64_draw_request request;
    struct v9x_r2_draw_decision decision;
    struct v9x_r2_draw_state state;
    struct v9x_r2_draw_vertex corner[4];
    v9x_u32 at = 0ul;
    v9x_u32 written = 0ul;
    v9x_u32 triangle;
    v9x_u32 k;
    double repeat = scene->repeat != 0.0 ? scene->repeat
                    : scene->log2_texture == 8ul ? 0.75 : 3.0;

    atirx_zero(&request, sizeof(request));
    request.target_format = 1ul;
    request.target_width = ATIRX_FILL_EDGE;
    request.target_height = ATIRX_FILL_EDGE;
    request.scissor_right = ATIRX_FILL_EDGE;
    request.scissor_bottom = ATIRX_FILL_EDGE;
    request.write_mask = 7ul;
    request.shade_mode = 2ul;
    if (scene->depth != 0ul) {
        request.depth_enable = 1ul;
        request.depth_bits = 16ul;
        request.depth_func = 4ul;       /* LESSEQUAL */
        request.depth_write = 1ul;
    }
    if (scene->textured != 0ul) {
        request.textured = 1ul;
        request.texture_format = V9X_M64_TEXTURE_FORMAT_RGB565;
        request.texture_width = 1ul << scene->log2_texture;
        request.texture_height = 1ul << scene->log2_texture;
        request.texture_levels = 1ul;
        request.texture_min_filter = scene->filter;
        request.texture_mag_filter = scene->filter;
        request.texture_address = 1ul;  /* WRAP */
        request.texture_op = 2ul;       /* MODULATE */
    }
    if (scene->src_blend != 0ul) {
        request.blend_enable = 1ul;
        request.src_blend = scene->src_blend;
        request.dst_blend = scene->dst_blend;
    }
    request.vertex_alpha_opaque = scene->src_blend == 5ul ? 0ul : 1ul;
    if (v9x_r2_check_draw(&request, 0ul, &decision) != V9X_M64_REFUSE_NONE) {
        return 0ul;
    }

    atirx_zero(&state, sizeof(state));
    state.target.offset = atirx_fill_target;
    state.target.pitch_bytes = atirx_fill_pitch;
    state.target.width = ATIRX_FILL_EDGE;
    state.target.height = ATIRX_FILL_EDGE;
    state.target.vram_bytes = fb_bytes;
    state.target.scissor_right = ATIRX_FILL_EDGE - 1ul;
    state.target.scissor_bottom = ATIRX_FILL_EDGE - 1ul;
    state.depth_enable = request.depth_enable;
    state.depth_offset = atirx_fill_z;
    state.depth_pitch_bytes = atirx_fill_pitch;
    state.depth_func = request.depth_func;
    state.depth_write = request.depth_write;
    state.textured = request.textured;
    state.texture.offset = scene->log2_texture == 8ul ? ATIRX_FILL_TEX256
                                                      : ATIRX_FILL_TEX64;
    state.texture.log2_width = scene->log2_texture;
    state.texture.log2_height = scene->log2_texture;
    state.texture.log2_pitch = scene->log2_texture;
    state.texture.format = decision.texture_format;
    if (v9x_r2_build_draw_state(&state, &decision, atirx_fill_offsets,
                                atirx_fill_values, ATIRX_FILL_DWORDS,
                                &written) != V9X_STATUS_OK) {
        return 0ul;
    }
    atirx_fill_values[0] = (atirx_fill_values[0] & ~atirx_fill_scale_clear) |
                           atirx_fill_scale_set;
    at = written;

    for (k = 0ul; k < 4ul; ++k) {
        v9x_u32 right = (k == 1ul || k == 2ul) ? 1ul : 0ul;
        v9x_u32 bottom = k >= 2ul ? 1ul : 0ul;

        corner[k].x = (v9x_s32)(right * ATIRX_FILL_EDGE * 16ul);
        corner[k].y = (v9x_s32)(bottom * ATIRX_FILL_EDGE * 16ul);
        corner[k].z = 0x8000ul;
        corner[k].argb = scene->src_blend == 5ul ? 0x80ffffffu
                                                 : 0xffffffffu;
        corner[k].fog = 255ul;
        corner[k].q = (scene->perspective != 0ul && bottom != 0ul) ? 0.4
                                                                   : 1.0;
        corner[k].tu = right != 0ul ? repeat : 0.0;
        corner[k].tv = bottom != 0ul ? repeat : 0.0;
    }
    for (triangle = 0ul; triangle < 2ul; ++triangle) {
        struct v9x_r2_draw_vertex v[3];
        v9x_u32 count = 0ul;
        v9x_u32 piece;

        v[0] = corner[0];
        v[1] = triangle == 0ul ? corner[1] : corner[2];
        v[2] = triangle == 0ul ? corner[2] : corner[3];
        if (v9x_r2_split_triangle(&state, &decision, v, pieces, fits,
                                  &count) != V9X_STATUS_OK) {
            return 0ul;
        }
        for (piece = 0ul; piece < count; ++piece) {
            written = 0ul;
            if (v9x_r2_build_piece(&state, &decision, &pieces[piece * 3ul],
                                   &fits[piece], atirx_fill_offsets + at,
                                   atirx_fill_values + at,
                                   ATIRX_FILL_DWORDS - at, &written, 0, 0,
                                   0) != V9X_STATUS_OK) {
                return 0ul;
            }
            at += written;
        }
    }
    return at;
}

static void atirx_fill_textures(void)
{
    DWORD i;
    DWORD seed = 0x12345678ul;
    volatile WORD *tex64 = atirx_fb + ATIRX_FILL_TEX64 / 2ul;
    volatile WORD *tex256 = atirx_fb + ATIRX_FILL_TEX256 / 2ul;

    for (i = 0ul; i < 64ul * 64ul; ++i) {
        seed = seed * 1103515245ul + 12345ul;
        tex64[i] = (WORD)(seed >> 16);
    }
    for (i = 0ul; i < 256ul * 256ul; ++i) {
        seed = seed * 1103515245ul + 12345ul;
        tex256[i] = (WORD)(seed >> 16);
    }
}

static void atirx_fill_clear_z(void)
{
    DWORD i;
    volatile WORD *z = atirx_fb + atirx_fill_z / 2ul;

    for (i = 0ul; i < ATIRX_FILL_EDGE * ATIRX_FILL_EDGE; ++i) {
        z[(i / ATIRX_FILL_EDGE) * (atirx_fill_pitch / 2ul) +
          i % ATIRX_FILL_EDGE] = 0xffffu;
    }
}

/* The scenes in `mask` (bit per scene) at the current placement, their
 * keys prefixed by `tag`. */
static int atirx_fill_scenes_run(struct v9x_m64_engine *engine,
                                 DWORD fb_bytes, DWORD mask, const char *tag)
{
    DWORD scene;
    char prefix[48];

    for (scene = 0ul;
         scene < sizeof(atirx_fill_scenes) / sizeof(atirx_fill_scenes[0]);
         ++scene) {
        const struct atirx_fill_scene *s = &atirx_fill_scenes[scene];
        v9x_u32 count;
        DWORD start;
        DWORD cycles;
        DWORD repeat;
        int ok = 1;

        if ((mask & (1ul << scene)) == 0ul) {
            continue;
        }
        count = atirx_fill_build(s, fb_bytes);
        lstrcpyA(prefix, tag);
        lstrcatA(prefix, s->name);
        lstrcatA(prefix, "_Dwords");
        atirx_key_dec(prefix, count);
        if (count == 0ul) {
            continue;
        }
        atirx_fill_clear_z();
        if (!atirx_finish_draw(engine)) {
            return 0;
        }
        start = atirx_tsc_low();
        for (repeat = 0ul; repeat < ATIRX_FILL_REPEAT && ok; ++repeat) {
            v9x_u32 done;

            for (done = 0ul; done < count; done += ATIRX_CHUNK) {
                v9x_u32 chunk = count - done;

                if (chunk > ATIRX_CHUNK) {
                    chunk = ATIRX_CHUNK;
                }
                if (v9x_m64_emit_batch(engine, atirx_fill_offsets + done,
                                       atirx_fill_values + done, chunk,
                                       ATIRX_SPINS) != V9X_STATUS_OK) {
                    ok = 0;
                    break;
                }
            }
        }
        if (!ok || !atirx_finish_draw(engine)) {
            atirx_key("Result", "FILL-EMIT");
            return 0;
        }
        cycles = atirx_tsc_low() - start;
        lstrcpyA(prefix, tag);
        lstrcatA(prefix, s->name);
        lstrcatA(prefix, "_Cycles");
        atirx_key_dec(prefix, cycles);
        atirx_flush();
    }
    return 1;
}

static void atirx_fill_tsc_rate(void)
{
    DWORD tick0;
    DWORD tsc0;

    /* The TSC's rate over about a second of wall time. */
    tick0 = GetTickCount();
    while (GetTickCount() == tick0) {
    }
    tick0 = GetTickCount();
    tsc0 = atirx_tsc_low();
    while (GetTickCount() - tick0 < 1000ul) {
    }
    atirx_key_dec("TscPerSecond", atirx_tsc_low() - tsc0);
    atirx_key_dec("Repeat", ATIRX_FILL_REPEAT);
    atirx_key_dec("PixelsPerRepeat", ATIRX_FILL_EDGE * ATIRX_FILL_EDGE);
}

/* /fillcache: the textured scenes as the HAL builds them, then with bit 5
 * clear, then with ATI's low bits as well. */
#define ATIRX_FILLCACHE_SCENES 0x0000fffcul

static int atirx_run_fillcache(struct v9x_m64_engine *engine,
                               DWORD fb_bytes)
{
    atirx_key("SceneSet", "fillcache");
    atirx_fill_textures();
    atirx_fill_tsc_rate();
    if (!atirx_fill_scenes_run(engine, fb_bytes, ATIRX_FILLCACHE_SCENES,
                               "A_")) {
        return 0;
    }
    atirx_fill_scale_clear = 0x00000020ul;
    if (!atirx_fill_scenes_run(engine, fb_bytes, ATIRX_FILLCACHE_SCENES,
                               "C_")) {
        return 0;
    }
    atirx_fill_scale_set = 0x0000020ful;
    if (!atirx_fill_scenes_run(engine, fb_bytes, ATIRX_FILLCACHE_SCENES,
                               "D_")) {
        return 0;
    }
    atirx_key("Result", "PASS");
    return 1;
}

static int atirx_run_fill(struct v9x_m64_engine *engine, DWORD fb_bytes)
{
    atirx_key("SceneSet", "fill");
    atirx_fill_textures();
    atirx_fill_tsc_rate();
    if (!atirx_fill_scenes_run(engine, fb_bytes, 0xfffffffful, "")) {
        return 0;
    }
    atirx_key("Result", "PASS");
    return 1;
}

/*
 * The same with a Z buffer at several distances from the colour buffer, at
 * Half-Life's 1280-byte pitch: flat with Z and bilinear with Z (scenes 1
 * and 5) per placement. Z read and write took flat fill from 1.5 to 3.8
 * engine clocks a pixel; this asks whether SDRAM page or bank conflicts
 * between the two surfaces are that cost.
 */
#define ATIRX_FILLZ_TARGET   0x00300000ul   /* 256 rows of 1280: 0x50000 */
#define ATIRX_FILLZ_BASE     0x00350000ul
#define ATIRX_FILLZ_PITCH    1280ul
#define ATIRX_FILLZ_SCENES   0x00000022ul

static int atirx_run_fillz(struct v9x_m64_engine *engine, DWORD fb_bytes)
{
    static const DWORD deltas[] = {
        0x00000ul, 0x00800ul, 0x01000ul, 0x02000ul, 0x04000ul, 0x08000ul,
        0x10000ul, 0x20000ul
    };
    DWORD k;

    atirx_key("SceneSet", "fillz");
    atirx_fill_textures();
    atirx_fill_tsc_rate();
    /* Half-Life's pitch, Z right after the colour buffer as before. */
    atirx_fill_target = ATIRX_FILLZ_TARGET;
    atirx_fill_pitch = ATIRX_FILLZ_PITCH;
    for (k = 0ul; k < sizeof(deltas) / sizeof(deltas[0]); ++k) {
        char tag[16];

        atirx_fill_z = ATIRX_FILLZ_BASE + deltas[k];
        tag[0] = 'Z';
        atirx_hex(tag + 1, atirx_fill_z, 6);
        lstrcatA(tag, "_");
        if (!atirx_fill_scenes_run(engine, fb_bytes, ATIRX_FILLZ_SCENES,
                                   tag)) {
            return 0;
        }
    }
    /* And the 512-byte pitch of /fill, for the same scenes here. */
    atirx_fill_pitch = ATIRX_FILL_PITCH;
    atirx_fill_z = ATIRX_FILLZ_BASE;
    if (!atirx_fill_scenes_run(engine, fb_bytes, ATIRX_FILLZ_SCENES,
                               "P512_")) {
        return 0;
    }
    atirx_key("Result", "PASS");
    return 1;
}

/* ---- Texture cache coherence --------------------------------------------- */

/*
 * /texcache: whether the engine's texture cache (on with SCALE_3D_CNTL bit
 * 5 clear) returns stale texels after the CPU rewrites a texture, and what
 * clears it. The 32x32 map of /tex at ATIRX_TEX_OFFSET (blue 31 in every
 * texel); between draws the CPU sets every texel's blue to 0 or back, so
 * each drawn pixel's blue says whether it came from memory or the cache.
 * Each scene draws ATIRX_RECT one texel a pixel, and counts the rectangle's
 * pixels by blue: Fresh (the map as written last), Stale (as before), and
 * Other.
 */
#define ATIRX_TEXCACHE_OTHER  0x00226000ul  /* a second 32x32 map */
#define ATIRX_TEXCACHE_HOW_STATE   0u       /* the full texture state */
#define ATIRX_TEXCACHE_HOW_TRAP    1u       /* the trapezoid alone    */
#define ATIRX_TEXCACHE_HOW_TOGGLE  2u       /* bit 5 set, then clear  */
#define ATIRX_TEXCACHE_HOW_TEXOFF  3u       /* TEX_5_OFF rewritten    */

static void atirx_texcache_blue(DWORD offset, DWORD blue)
{
    DWORD i;
    volatile WORD *base = atirx_fb + offset / 2ul;

    for (i = 0ul; i < ATIRX_TEX_SIZE * ATIRX_TEX_SIZE; ++i) {
        base[i] = (WORD)((base[i] & ~31u) | blue);
    }
}

static int atirx_texcache_draw(struct v9x_m64_engine *engine,
                               const struct v9x_r2_target *target,
                               DWORD offset, UINT how, const char *name,
                               DWORD fresh, DWORD stale)
{
    static const struct v9x_r2_flat_trap rect = ATIRX_RECT;
    static const struct v9x_r2_st st = {
        { 0l, 0l }, { ATIRX_TX(1), 0l }, { 0l, ATIRX_TX(1) },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l }
    };
    struct v9x_r2_texture texture;
    v9x_u32 offsets[40];
    v9x_u32 values[40];
    v9x_u32 written = 0ul;
    DWORD counts[3];
    DWORD x;
    DWORD y;
    char key[48];

    texture.offset = offset;
    texture.log2_width = 5ul;
    texture.log2_height = 5ul;
    texture.log2_pitch = 5ul;
    texture.format = V9X_R2_TEX_FORMAT_565;
    texture.scale_3d_extra = 0ul;
    atirx_prepare_block();
    if (how == ATIRX_TEXCACHE_HOW_STATE) {
        if (v9x_r2_build_texture_state(target, &texture, &st, offsets, values,
                                       32ul, &written) != V9X_STATUS_OK) {
            return 0;
        }
        values[0] &= ~V9X_R2_TEX_CACHE_DIS;
    } else if (how == ATIRX_TEXCACHE_HOW_TOGGLE) {
        offsets[0] = V9X_M64_SCALE_3D_CNTL;
        values[0] = V9X_R2_SCALE_3D_TEXTURE | V9X_R2_TEX_CACHE_DIS |
                    V9X_R2_MIP_MAP_DISABLE;
        offsets[1] = V9X_M64_SCALE_3D_CNTL;
        values[1] = V9X_R2_SCALE_3D_TEXTURE | V9X_R2_MIP_MAP_DISABLE;
        written = 2ul;
    } else if (how == ATIRX_TEXCACHE_HOW_TEXOFF) {
        offsets[0] = V9X_R2_TEX_0_OFF + 5ul * 4ul;
        values[0] = offset;
        written = 1ul;
    }
    if (written != 0ul &&
        atirx_emit(engine, name, offsets, values, written) != V9X_STATUS_OK) {
        return 0;
    }
    if (v9x_r2_build_trap(target, &rect, offsets, values, 32ul, &written) !=
            V9X_STATUS_OK ||
        atirx_emit(engine, name, offsets, values, written) != V9X_STATUS_OK ||
        !atirx_finish_draw(engine)) {
        return 0;
    }
    counts[0] = counts[1] = counts[2] = 0ul;
    for (y = 16ul; y < 24ul; ++y) {
        for (x = 16ul; x < 48ul; ++x) {
            DWORD blue = *atirx_pixel(x, y) & 31u;

            counts[blue == fresh ? 0 : blue == stale ? 1 : 2]++;
        }
    }
    lstrcpyA(key, name);
    lstrcatA(key, "_Fresh");
    atirx_key_dec(key, counts[0]);
    lstrcpyA(key, name);
    lstrcatA(key, "_Stale");
    atirx_key_dec(key, counts[1]);
    lstrcpyA(key, name);
    lstrcatA(key, "_Other");
    atirx_key_dec(key, counts[2]);
    atirx_flush();
    return 1;
}

static int atirx_run_texcache(struct v9x_m64_engine *engine,
                              const struct v9x_r2_target *target)
{
    v9x_u32 offsets[1];
    v9x_u32 values[1];
    int ok;

    atirx_key("SceneSet", "texcache");
    atirx_write_texture(ATIRX_TEX_OFFSET, ATIRX_TEX_SIZE, ATIRX_TEX_SIZE);
    atirx_write_texture(ATIRX_TEXCACHE_OTHER, ATIRX_TEX_SIZE, ATIRX_TEX_SIZE);
    atirx_texcache_blue(ATIRX_TEXCACHE_OTHER, 15ul);

    /* Blue 31, cache on: the cold read. */
    ok = atirx_texcache_draw(engine, target, ATIRX_TEX_OFFSET,
                             ATIRX_TEXCACHE_HOW_STATE, "K1Cold", 31ul, 0ul);
    /* Each next scene after the CPU flips the map's blue. */
    atirx_texcache_blue(ATIRX_TEX_OFFSET, 0ul);
    ok = ok && atirx_texcache_draw(engine, target, ATIRX_TEX_OFFSET,
                                   ATIRX_TEXCACHE_HOW_STATE, "K2State",
                                   0ul, 31ul);
    atirx_texcache_blue(ATIRX_TEX_OFFSET, 31ul);
    ok = ok && atirx_texcache_draw(engine, target, ATIRX_TEX_OFFSET,
                                   ATIRX_TEXCACHE_HOW_TRAP, "K3Trap",
                                   31ul, 0ul);
    atirx_texcache_blue(ATIRX_TEX_OFFSET, 0ul);
    ok = ok && atirx_texcache_draw(engine, target, ATIRX_TEX_OFFSET,
                                   ATIRX_TEXCACHE_HOW_TEXOFF, "K4TexOff",
                                   0ul, 31ul);
    atirx_texcache_blue(ATIRX_TEX_OFFSET, 31ul);
    ok = ok && atirx_texcache_draw(engine, target, ATIRX_TEX_OFFSET,
                                   ATIRX_TEXCACHE_HOW_TOGGLE, "K5Toggle",
                                   31ul, 0ul);
    /* The other map (blue 15), then back to the first after a flip. */
    ok = ok && atirx_texcache_draw(engine, target, ATIRX_TEXCACHE_OTHER,
                                   ATIRX_TEXCACHE_HOW_STATE, "K6Other",
                                   15ul, 31ul);
    atirx_texcache_blue(ATIRX_TEX_OFFSET, 0ul);
    ok = ok && atirx_texcache_draw(engine, target, ATIRX_TEX_OFFSET,
                                   ATIRX_TEXCACHE_HOW_STATE, "K7Back",
                                   0ul, 31ul);
    /* The trapezoid alone again, now after the state's flip: does a
     * draw of the same texture reread memory without any state? */
    atirx_texcache_blue(ATIRX_TEX_OFFSET, 31ul);
    ok = ok && atirx_texcache_draw(engine, target, ATIRX_TEX_OFFSET,
                                   ATIRX_TEXCACHE_HOW_TRAP, "K8TrapAgain",
                                   31ul, 0ul);

    offsets[0] = V9X_M64_SCALE_3D_CNTL;
    values[0] = 0ul;
    if (atirx_emit(engine, "end", offsets, values, 1ul) != V9X_STATUS_OK ||
        v9x_m64_wait_idle(engine, ATIRX_SPINS) != V9X_STATUS_OK) {
        return 0;
    }
    atirx_key("Result", ok ? "TEXCACHE-RUN" : "TEXCACHE-FAIL");
    return ok;
}

/* ---- Sampling a running driver ----------------------------------------- */

/*
 * /sample: what the engine is doing under whichever driver is loaded,
 * read only, while an application draws. ATIRX_SAMPLE_COUNT samples
 * ATIRX_SAMPLE_GAP_MS apart; each reads GUI_STAT ATIRX_SAMPLE_BURST times
 * (GUI_ACTIVE, bit 0: the engine's utilisation) and then the 3D state
 * registers once, keeping the commonest ATIRX_SAMPLE_KINDS values of each
 * with their counts, and the lowest and highest non-zero value. Run before
 * the engine initialisation every other mode does, as /vramdump is, and
 * never reads DST_BRES_LNTH (+520), the trigger.
 */
#define ATIRX_SAMPLE_COUNT   6000ul
#define ATIRX_SAMPLE_GAP_MS  5ul      /* a Sleep: the game keeps the CPU */
#define ATIRX_SAMPLE_BURST   8ul
#define ATIRX_SAMPLE_KINDS   12u

static const struct {
    const char *name;
    DWORD offset;
} atirx_sample_regs[] = {
    { "Scale3dCntl", V9X_M64_SCALE_3D_CNTL },
    { "ZCntl", V9X_M64_Z_CNTL },
    { "AlphaTstCntl", V9X_M64_ALPHA_TST_CNTL },
    { "TexSizePitch", V9X_R2_TEX_SIZE_PITCH },
    { "DpPixWidth", V9X_M64_DP_PIX_WIDTH },
    { "DpSrc", V9X_M64_DP_SRC },
    { "DstOffPitch", V9X_M64_DST_OFF_PITCH },
    { "ZOffPitch", V9X_M64_Z_OFF_PITCH },
    { "BusCntl", V9X_M64_BUS_CNTL },
    { "Tex8Off", V9X_R2_TEX_0_OFF + 32ul },
    { "Tex7Off", V9X_R2_TEX_0_OFF + 28ul },
    { "Tex6Off", V9X_R2_TEX_0_OFF + 24ul },
    { "Tex5Off", V9X_R2_TEX_0_OFF + 20ul },
    { "Tex0Off", V9X_R2_TEX_0_OFF }
};

#define ATIRX_SAMPLE_REGS \
    (sizeof(atirx_sample_regs) / sizeof(atirx_sample_regs[0]))

static DWORD atirx_sample_value[ATIRX_SAMPLE_REGS][ATIRX_SAMPLE_KINDS];
static DWORD atirx_sample_count[ATIRX_SAMPLE_REGS][ATIRX_SAMPLE_KINDS];
static DWORD atirx_sample_other[ATIRX_SAMPLE_REGS];
static DWORD atirx_sample_low[ATIRX_SAMPLE_REGS];
static DWORD atirx_sample_high[ATIRX_SAMPLE_REGS];
/* SCALE_3D_CNTL and Z_CNTL read together: which draws run without Z. */
#define ATIRX_SAMPLE_PAIRS   24u
static DWORD atirx_pair_scale[ATIRX_SAMPLE_PAIRS];
static DWORD atirx_pair_z[ATIRX_SAMPLE_PAIRS];
static DWORD atirx_pair_count[ATIRX_SAMPLE_PAIRS];
static DWORD atirx_pair_other;

static void atirx_pair_note(DWORD scale, DWORD z)
{
    UINT kind;

    for (kind = 0u; kind < ATIRX_SAMPLE_PAIRS; ++kind) {
        if (atirx_pair_count[kind] != 0ul && atirx_pair_scale[kind] == scale &&
            atirx_pair_z[kind] == z) {
            ++atirx_pair_count[kind];
            return;
        }
        if (atirx_pair_count[kind] == 0ul) {
            atirx_pair_scale[kind] = scale;
            atirx_pair_z[kind] = z;
            atirx_pair_count[kind] = 1ul;
            return;
        }
    }
    ++atirx_pair_other;
}

static void atirx_sample_note(DWORD reg, DWORD value)
{
    UINT kind;

    if (value != 0ul) {
        if (atirx_sample_low[reg] == 0ul || value < atirx_sample_low[reg]) {
            atirx_sample_low[reg] = value;
        }
        if (value > atirx_sample_high[reg]) {
            atirx_sample_high[reg] = value;
        }
    }
    for (kind = 0u; kind < ATIRX_SAMPLE_KINDS; ++kind) {
        if (atirx_sample_count[reg][kind] != 0ul &&
            atirx_sample_value[reg][kind] == value) {
            ++atirx_sample_count[reg][kind];
            return;
        }
        if (atirx_sample_count[reg][kind] == 0ul) {
            atirx_sample_value[reg][kind] = value;
            atirx_sample_count[reg][kind] = 1ul;
            return;
        }
    }
    ++atirx_sample_other[reg];
}

static int atirx_run_sample(void)
{
    DWORD tick0;
    DWORD tsc0;
    DWORD per_us;
    DWORD busy = 0ul;
    DWORD reads = 0ul;
    DWORD sample;
    DWORD reg;
    char key[48];
    char text[12];

    atirx_key("SceneSet", "sample");
    /* The TSC's rate, for the gap between samples. */
    tick0 = GetTickCount();
    while (GetTickCount() == tick0) {
    }
    tick0 = GetTickCount();
    tsc0 = atirx_tsc_low();
    while (GetTickCount() - tick0 < 250ul) {
    }
    per_us = (atirx_tsc_low() - tsc0) / 250000ul;
    atirx_key_dec("TscPerUs", per_us);

    tick0 = GetTickCount();
    for (sample = 0ul; sample < ATIRX_SAMPLE_COUNT; ++sample) {
        DWORD k;

        for (k = 0ul; k < ATIRX_SAMPLE_BURST; ++k) {
            if ((atirx_read(0, V9X_M64_GUI_STAT) & 1ul) != 0ul) {
                ++busy;
            }
            ++reads;
        }
        for (reg = 0ul; reg < ATIRX_SAMPLE_REGS; ++reg) {
            atirx_sample_note(reg, atirx_read(0, atirx_sample_regs[reg].offset));
        }
        atirx_pair_note(atirx_read(0, V9X_M64_SCALE_3D_CNTL),
                        atirx_read(0, V9X_M64_Z_CNTL));
        Sleep(ATIRX_SAMPLE_GAP_MS);
    }
    atirx_key_dec("Samples", ATIRX_SAMPLE_COUNT);
    atirx_key_dec("WallMs", GetTickCount() - tick0);
    atirx_key_dec("GuiStatReads", reads);
    atirx_key_dec("GuiActive", busy);
    for (reg = 0ul; reg < ATIRX_SAMPLE_REGS; ++reg) {
        UINT kind;

        for (kind = 0u; kind < ATIRX_SAMPLE_KINDS; ++kind) {
            if (atirx_sample_count[reg][kind] == 0ul) {
                break;
            }
            lstrcpyA(key, atirx_sample_regs[reg].name);
            lstrcatA(key, "_");
            atirx_hex(text, atirx_sample_value[reg][kind], 8);
            lstrcatA(key, text);
            atirx_key_dec(key, atirx_sample_count[reg][kind]);
        }
        lstrcpyA(key, atirx_sample_regs[reg].name);
        lstrcatA(key, "_Other");
        atirx_key_dec(key, atirx_sample_other[reg]);
        lstrcpyA(key, atirx_sample_regs[reg].name);
        lstrcatA(key, "_Low");
        atirx_key_hex(key, atirx_sample_low[reg]);
        lstrcpyA(key, atirx_sample_regs[reg].name);
        lstrcatA(key, "_High");
        atirx_key_hex(key, atirx_sample_high[reg]);
    }
    {
        UINT kind;

        for (kind = 0u; kind < ATIRX_SAMPLE_PAIRS; ++kind) {
            if (atirx_pair_count[kind] == 0ul) {
                break;
            }
            lstrcpyA(key, "Pair_");
            atirx_hex(text, atirx_pair_scale[kind], 8);
            lstrcatA(key, text);
            lstrcatA(key, "_");
            atirx_hex(text, atirx_pair_z[kind], 3);
            lstrcatA(key, text);
            atirx_key_dec(key, atirx_pair_count[kind]);
        }
        atirx_key_dec("Pair_Other", atirx_pair_other);
    }
    atirx_key("Result", "PASS");
    return 1;
}

/* ---- Mip-mapping -------------------------------------------------------- */

/*
 * /mip: what the engine does with MIP_MAP_DISABLE clear. A 64x64 565 map
 * with every level to 1x1, each at a 64-byte boundary after the one
 * before (as v9x_d3d_rage2_create_surface places a chain), TEX_n_OFF
 * holding level n. Every texel names itself: red 16 + level, green u,
 * blue v. Each scene is ATIRX_RECT (columns 16..47, rows 16..23) with
 * S/T set by hand in base-map texels (2^20 each, 2^26 / 64), and rows 16,
 * 17, 20 and 23 are dumped as level:u.v, or raw where TEX_BLEND_FCN may
 * mix levels.
 */
#define ATIRX_MIP_OFFSET     0x00230000ul
#define ATIRX_MIP_TOP        6ul
#define ATIRX_MIP_TEXEL      (1l << 20)
#define ATIRX_MT(n)          ((v9x_s32)((n) * (double)ATIRX_MIP_TEXEL))

struct atirx_mip_scene {
    const char *name;
    v9x_u32 mip_enable;
    v9x_u32 extra;           /* SCALE_3D_CNTL filter bits */
    v9x_u32 raw;             /* dump raw words, not level:u.v */
    struct v9x_r2_st st;
    v9x_u32 left;            /* ATIRX_SLOPE_LEFT, not ATIRX_RECT */
    v9x_u32 tall;            /* the 64x16 chain, not 64x64 */
};

#define ATIRX_MIP_ST(sx, tx, sy, ty, sx2) \
    { { 0l, 0l }, { ATIRX_MT(sx), ATIRX_MT(tx) }, \
      { ATIRX_MT(sy), ATIRX_MT(ty) }, { ATIRX_MT(sx2), 0l }, \
      { 0l, 0l }, { 0l, 0l } }

static const struct atirx_mip_scene atirx_mip_scenes[] = {
    /* Control: the HAL's state, 4 texels a pixel, level 6 expected. */
    { "M0Disabled", 0ul, 0ul, 0ul, ATIRX_MIP_ST(4.0, 0.0, 0.0, 0.0, 0.0) },
    /* S along the span, nothing per row: the level against the ratio. */
    { "A050", 1ul, 0ul, 0ul, ATIRX_MIP_ST(0.5, 0.0, 0.0, 0.0, 0.0) },
    { "A100", 1ul, 0ul, 0ul, ATIRX_MIP_ST(1.0, 0.0, 0.0, 0.0, 0.0) },
    { "A125", 1ul, 0ul, 0ul, ATIRX_MIP_ST(1.25, 0.0, 0.0, 0.0, 0.0) },
    { "A150", 1ul, 0ul, 0ul, ATIRX_MIP_ST(1.5, 0.0, 0.0, 0.0, 0.0) },
    { "A175", 1ul, 0ul, 0ul, ATIRX_MIP_ST(1.75, 0.0, 0.0, 0.0, 0.0) },
    { "A200", 1ul, 0ul, 0ul, ATIRX_MIP_ST(2.0, 0.0, 0.0, 0.0, 0.0) },
    { "A250", 1ul, 0ul, 0ul, ATIRX_MIP_ST(2.5, 0.0, 0.0, 0.0, 0.0) },
    { "A300", 1ul, 0ul, 0ul, ATIRX_MIP_ST(3.0, 0.0, 0.0, 0.0, 0.0) },
    { "A400", 1ul, 0ul, 0ul, ATIRX_MIP_ST(4.0, 0.0, 0.0, 0.0, 0.0) },
    { "A600", 1ul, 0ul, 0ul, ATIRX_MIP_ST(6.0, 0.0, 0.0, 0.0, 0.0) },
    { "A800", 1ul, 0ul, 0ul, ATIRX_MIP_ST(8.0, 0.0, 0.0, 0.0, 0.0) },
    { "A1600", 1ul, 0ul, 0ul, ATIRX_MIP_ST(16.0, 0.0, 0.0, 0.0, 0.0) },
    { "A3200", 1ul, 0ul, 0ul, ATIRX_MIP_ST(32.0, 0.0, 0.0, 0.0, 0.0) },
    { "A6400", 1ul, 0ul, 0ul, ATIRX_MIP_ST(64.0, 0.0, 0.0, 0.0, 0.0) },
    /* T along the span instead: does T count as S does? */
    { "B200", 1ul, 0ul, 0ul, ATIRX_MIP_ST(0.0, 2.0, 0.0, 0.0, 0.0) },
    { "B400", 1ul, 0ul, 0ul, ATIRX_MIP_ST(0.0, 4.0, 0.0, 0.0, 0.0) },
    { "B800", 1ul, 0ul, 0ul, ATIRX_MIP_ST(0.0, 8.0, 0.0, 0.0, 0.0) },
    /* Both along the span: the larger, the sum, or the length? */
    { "AB22", 1ul, 0ul, 0ul, ATIRX_MIP_ST(2.0, 2.0, 0.0, 0.0, 0.0) },
    { "AB44", 1ul, 0ul, 0ul, ATIRX_MIP_ST(4.0, 4.0, 0.0, 0.0, 0.0) },
    /* Half a texel along the span, a large step per row: is y read? */
    { "C400", 1ul, 0ul, 0ul, ATIRX_MIP_ST(0.5, 0.0, 4.0, 0.0, 0.0) },
    { "C1600", 1ul, 0ul, 0ul, ATIRX_MIP_ST(0.5, 0.0, 16.0, 0.0, 0.0) },
    { "D400", 1ul, 0ul, 0ul, ATIRX_MIP_ST(0.5, 0.0, 0.0, 4.0, 0.0) },
    { "D1600", 1ul, 0ul, 0ul, ATIRX_MIP_ST(0.5, 0.0, 0.0, 16.0, 0.0) },
    /* The increment growing along the span, 1 + k/2 at column k: is the
     * level chosen per pixel, and where are the thresholds? */
    { "E1", 1ul, 0ul, 0ul, ATIRX_MIP_ST(1.0, 0.0, 0.0, 0.0, 0.5) },
    { "E2", 1ul, 0ul, 0ul, ATIRX_MIP_ST(0.25, 0.0, 0.0, 0.0, 0.125) },
    /* Three texels a pixel under each TEX_BLEND_FCN, raw: 0 nearest, 2
     * the 2x2 blend measured in one map, 1 and 3 not measured. */
    { "F0", 1ul, 0x00000000ul, 1ul, ATIRX_MIP_ST(3.0, 0.0, 0.0, 0.0, 0.0) },
    { "F1", 1ul, 0x04000000ul, 1ul, ATIRX_MIP_ST(3.0, 0.0, 0.0, 0.0, 0.0) },
    { "F2", 1ul, 0x08000000ul, 1ul, ATIRX_MIP_ST(3.0, 0.0, 0.0, 0.0, 0.0) },
    { "F3", 1ul, 0x0c000000ul, 1ul, ATIRX_MIP_ST(3.0, 0.0, 0.0, 0.0, 0.0) },
    /* The same at 1.5, between levels 6 and 5. */
    { "G1", 1ul, 0x04000000ul, 1ul, ATIRX_MIP_ST(1.5, 0.0, 0.0, 0.0, 0.0) },
    { "G3", 1ul, 0x0c000000ul, 1ul, ATIRX_MIP_ST(1.5, 0.0, 0.0, 0.0, 0.0) },
    /* Negative increments: S starts at 31 base texels so the samples
     * stay readable. Is the level from the magnitude? */
    { "N1SxNeg", 1ul, 0ul, 0ul,
      { { ATIRX_MT(31.0), 0l }, { ATIRX_MT(-4.0), 0l }, { 0l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    { "N2TxNeg", 1ul, 0ul, 0ul,
      { { 0l, ATIRX_MT(31.0) }, { 0l, ATIRX_MT(-4.0) }, { 0l, 0l },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    { "N3SyNeg", 1ul, 0ul, 0ul,
      { { ATIRX_MT(31.0), 0l }, { ATIRX_MT(0.5), 0l },
        { ATIRX_MT(-4.0), 0l }, { 0l, 0l }, { 0l, 0l }, { 0l, 0l } } },
    /* The span walked leftward (DST_X_DIR clear), S +4 a step. */
    { "L1Left", 1ul, 0ul, 0ul, ATIRX_MIP_ST(4.0, 0.0, 0.0, 0.0, 0.0), 1ul },
    /* Past the 1x1 level: clamped, or outside the chain? */
    { "K128", 1ul, 0ul, 0ul, ATIRX_MIP_ST(128.0, 0.0, 0.0, 0.0, 0.0) },
    { "K1024", 1ul, 0ul, 0ul, ATIRX_MIP_ST(1024.0, 0.0, 0.0, 0.0, 0.0) },
    /* A 64x16 chain (64x16, 32x8, 16x4, 8x2, 4x1, 2x1, 1x1): T one base
     * texel a row, so v names the row and wraps at the level's height. */
    { "W100", 1ul, 0ul, 0ul, ATIRX_MIP_ST(1.0, 0.0, 0.0, 1.0, 0.0), 0ul, 1ul },
    { "W400", 1ul, 0ul, 0ul, ATIRX_MIP_ST(4.0, 0.0, 0.0, 1.0, 0.0), 0ul, 1ul },
    { "W800", 1ul, 0ul, 0ul, ATIRX_MIP_ST(8.0, 0.0, 0.0, 1.0, 0.0), 0ul, 1ul },
    { "W3200", 1ul, 0ul, 0ul, ATIRX_MIP_ST(32.0, 0.0, 0.0, 1.0, 0.0), 0ul,
      1ul },
    /* Magnifying (half a texel a pixel) with mip-mapping on: each
     * TEX_BLEND_FCN without and with BILINEAR_TEX_EN, raw. B4 found FCN 2
     * without BILINEAR_TEX_EN drawing nothing as it magnified. */
    { "H0", 1ul, 0x00000000ul, 1ul, ATIRX_MIP_ST(0.5, 0.0, 0.0, 0.0, 0.0) },
    { "H1", 1ul, 0x04000000ul, 1ul, ATIRX_MIP_ST(0.5, 0.0, 0.0, 0.0, 0.0) },
    { "H2", 1ul, 0x08000000ul, 1ul, ATIRX_MIP_ST(0.5, 0.0, 0.0, 0.0, 0.0) },
    { "H3", 1ul, 0x0c000000ul, 1ul, ATIRX_MIP_ST(0.5, 0.0, 0.0, 0.0, 0.0) },
    { "HB0", 1ul, 0x02000000ul, 1ul, ATIRX_MIP_ST(0.5, 0.0, 0.0, 0.0, 0.0) },
    { "HB1", 1ul, 0x06000000ul, 1ul, ATIRX_MIP_ST(0.5, 0.0, 0.0, 0.0, 0.0) },
    { "HB2", 1ul, 0x0a000000ul, 1ul, ATIRX_MIP_ST(0.5, 0.0, 0.0, 0.0, 0.0) },
    { "HB3", 1ul, 0x0e000000ul, 1ul, ATIRX_MIP_ST(0.5, 0.0, 0.0, 0.0, 0.0) },
    /* Minifying 3:1 with BILINEAR_TEX_EN as well: does it change FCN 1? */
    { "FB1", 1ul, 0x06000000ul, 1ul, ATIRX_MIP_ST(3.0, 0.0, 0.0, 0.0, 0.0) }
};

#define ATIRX_MIP_TALL_OFFSET 0x00238000ul
#define ATIRX_MIP_TALL_HEIGHT 4ul           /* log2: 64x16 */

static DWORD atirx_mip_level_offset[ATIRX_MIP_TOP + 1ul];
static DWORD atirx_mip_tall_offset[ATIRX_MIP_TOP + 1ul];

/* A chain from 2^top x 2^height down to 1x1, each level's height halved
 * with its width and never below 1. */
static void atirx_write_mip_chain(DWORD at, DWORD log2_height,
                                  DWORD *level_offset)
{
    DWORD level;

    for (level = ATIRX_MIP_TOP + 1ul; level-- > 0ul;) {
        DWORD edge = 1ul << level;
        DWORD drop = ATIRX_MIP_TOP - level;
        DWORD rows = drop >= log2_height ? 1ul
                                         : 1ul << (log2_height - drop);
        DWORD u;
        DWORD v;
        volatile WORD *map = atirx_fb + at / 2ul;

        level_offset[level] = at;
        for (v = 0ul; v < rows; ++v) {
            for (u = 0ul; u < edge; ++u) {
                map[v * edge + u] = (WORD)(((16ul + level) << 11) |
                                           ((u & 63ul) << 5) | (v & 31ul));
            }
        }
        at = (at + edge * rows * 2ul + 63ul) & ~63ul;
    }
}

static void atirx_dump_mip_row(const char *prefix, DWORD y, int raw)
{
    char key[48];
    char text[12];
    DWORD x;

    lstrcpyA(key, prefix);
    lstrcatA(key, "R");
    atirx_decimal(text, y);
    lstrcatA(key, text);
    atirx_text(key);
    atirx_text("=");
    for (x = 16ul; x < 48ul; ++x) {
        WORD pixel = *atirx_pixel(x, y);

        if (raw || pixel == ATIRX_SENTINEL || (pixel >> 11) < 16u) {
            atirx_hex(text, pixel, 4);
        } else {
            text[0] = (char)('0' + ((pixel >> 11) - 16u));
            text[1] = ':';
            atirx_hex(text + 2, (pixel >> 5) & 63u, 2);
            text[4] = '.';
            atirx_hex(text + 5, pixel & 31u, 2);
        }
        atirx_text(text);
        atirx_text(x == 47ul ? "\r\n" : " ");
    }
}

static int atirx_run_mip(struct v9x_m64_engine *engine,
                         const struct v9x_r2_target *target)
{
    static const struct v9x_r2_flat_trap rect = ATIRX_RECT;
    static const struct v9x_r2_flat_trap slope_left = ATIRX_SLOPE_LEFT;
    struct v9x_r2_texture texture;
    v9x_u32 offsets[48];
    v9x_u32 values[48];
    v9x_u32 written;
    DWORD level;
    UINT index;
    char prefix[48];

    atirx_key("SceneSet", "mip");
    atirx_write_mip_chain(ATIRX_MIP_OFFSET, ATIRX_MIP_TOP,
                          atirx_mip_level_offset);
    atirx_write_mip_chain(ATIRX_MIP_TALL_OFFSET, ATIRX_MIP_TALL_HEIGHT,
                          atirx_mip_tall_offset);
    for (level = 0ul; level <= ATIRX_MIP_TOP; ++level) {
        char key[16];

        lstrcpyA(key, "Level");
        atirx_decimal(key + 5, level);
        atirx_key_hex(key, atirx_mip_level_offset[level]);
    }
    texture.log2_width = ATIRX_MIP_TOP;
    texture.log2_pitch = ATIRX_MIP_TOP;
    texture.format = V9X_R2_TEX_FORMAT_565;

    for (index = 0u;
         index < sizeof(atirx_mip_scenes) / sizeof(atirx_mip_scenes[0]);
         ++index) {
        const struct atirx_mip_scene *scene = &atirx_mip_scenes[index];
        const DWORD *chain = scene->tall != 0ul ? atirx_mip_tall_offset
                                                : atirx_mip_level_offset;

        texture.offset = chain[ATIRX_MIP_TOP];
        texture.log2_height = scene->tall != 0ul ? ATIRX_MIP_TALL_HEIGHT
                                                 : ATIRX_MIP_TOP;
        atirx_prefix(prefix, scene->name);
        atirx_key("Scene", scene->name);
        atirx_prepare_block();
        texture.scale_3d_extra = scene->extra;
        if (v9x_r2_build_texture_state(target, &texture, &scene->st,
                                       offsets, values, 32ul, &written) !=
            V9X_STATUS_OK) {
            atirx_key("Result", "MIP-STATE");
            return 0;
        }
        if (scene->mip_enable != 0ul) {
            values[0] &= ~V9X_R2_MIP_MAP_DISABLE;
            for (level = 0ul; level < ATIRX_MIP_TOP; ++level) {
                offsets[written] = V9X_R2_TEX_0_OFF + level * 4ul;
                values[written++] = chain[level];
            }
        }
        atirx_key_hex("Scale3dCntl", values[0]);
        if (atirx_emit(engine, scene->name, offsets, values, written) !=
                V9X_STATUS_OK ||
            v9x_r2_build_trap(target,
                              scene->left != 0ul ? &slope_left : &rect,
                              offsets, values, 32ul, &written) !=
                V9X_STATUS_OK ||
            atirx_emit(engine, scene->name, offsets, values, written) !=
                V9X_STATUS_OK) {
            atirx_key("Result", "MIP-EMIT");
            return 0;
        }
        if (!atirx_finish_draw(engine)) {
            return 0;
        }
        atirx_dump_mip_row(prefix, 16ul, (int)scene->raw);
        atirx_dump_mip_row(prefix, 17ul, (int)scene->raw);
        atirx_dump_mip_row(prefix, 20ul, (int)scene->raw);
        atirx_dump_mip_row(prefix, 23ul, (int)scene->raw);
        atirx_report_block(prefix);
        atirx_flush();
    }

    offsets[0] = V9X_M64_SCALE_3D_CNTL;
    values[0] = 0ul;
    if (atirx_emit(engine, "end", offsets, values, 1ul) != V9X_STATUS_OK ||
        v9x_m64_wait_idle(engine, ATIRX_SPINS) != V9X_STATUS_OK) {
        return 0;
    }
    atirx_key("Result", "MIP-RUN");
    return 1;
}

/*
 * The clock PLL's registers, read only: CLOCK_CNTL (+490) byte 1 takes
 * PLL_ADDR in bits 7:2 with PLL_WR_EN (bit 1) clear, byte 2 returns
 * PLL_DATA. Byte accesses, as atyfb's aty_ld_pll_ct does, so byte 0
 * (CLOCK_SEL and the strobe) is never written. The index the driver left
 * is put back. For the memory and engine clocks Velocity9x runs at.
 */
#define ATIRX_CLOCK_CNTL      0x490u
#define ATIRX_PLL_REGISTERS   0x20u

static int atirx_run_pll(void)
{
    volatile BYTE *clock_cntl = atirx_mmio + ATIRX_CLOCK_CNTL;
    BYTE saved_index = clock_cntl[1];
    char name[8];
    DWORD index;

    atirx_key("SceneSet", "pll");
    atirx_key_hex("ClockCntl", atirx_read(0, ATIRX_CLOCK_CNTL));
    for (index = 0u; index < ATIRX_PLL_REGISTERS; ++index) {
        clock_cntl[1] = (BYTE)(index << 2);
        name[0] = 'P';
        name[1] = 'l';
        name[2] = 'l';
        name[3] = "0123456789ABCDEF"[(index >> 4) & 0xfu];
        name[4] = "0123456789ABCDEF"[index & 0xfu];
        name[5] = '\0';
        atirx_key_hex(name, (DWORD)clock_cntl[2]);
    }
    clock_cntl[1] = (BYTE)(saved_index & 0xfcu);
    atirx_key_hex("ClockCntlAfter", atirx_read(0, ATIRX_CLOCK_CNTL));
    atirx_key_hex("MemCntl", atirx_read(0, 0x4b0u));
    atirx_key("Result", "PASS");
    return 1;
}

static int atirx_run_vramdump(void)
{
    HANDLE file;
    DWORD at;
    DWORD written;
    DWORD index;
    const volatile BYTE *source = (const volatile BYTE *)atirx_fb;

    atirx_key("SceneSet", "vramdump");
    atirx_key_hex("CrtcOffPitch", atirx_read(0, 0x414u));
    atirx_key_hex("CrtcVTotalDisp", atirx_read(0, 0x408u));
    atirx_key_hex("CrtcHTotalDisp", atirx_read(0, 0x400u));
    atirx_key_hex("GuiStat", atirx_read(0, V9X_M64_GUI_STAT));
    /* The 3D state an application's last batch left, read back as Phase
     * 3's /regs did. Never 0x520 (DST_BRES_LNTH), the trigger. */
    {
        static const DWORD state_regs[] = {
            0x500u, 0x50cu, 0x524u, 0x528u, 0x52cu, 0x530u, 0x538u, 0x53cu,
            0x540u, 0x548u, 0x54cu, 0x5b4u, 0x5fcu, 0x6a8u, 0x6b4u, 0x6c0u,
            0x6c4u, 0x6c8u, 0x6d0u, 0x6d4u, 0x6d8u, 0x708u, 0x730u, 0x770u,
            0x5c0u, 0x5c4u, 0x5c8u, 0x5ccu, 0x5d0u, 0x5d4u, 0x5d8u, 0x5dcu,
            0x5e0u, 0x5e4u, 0x5e8u,
            0x740u, 0x744u, 0x748u, 0x74cu, 0x750u, 0x754u, 0x758u, 0x75cu,
            0x760u, 0x764u, 0x768u, 0x76cu,
            0x7c0u, 0x7c4u, 0x7c8u, 0x7ccu, 0x7d0u, 0x7d4u, 0x7d8u, 0x7dcu,
            0x7e0u, 0x7e4u, 0x7e8u, 0x7ecu, 0x7f0u, 0x7f4u, 0x7f8u
        };
        char name[16];
        UINT reg;

        for (reg = 0u; reg < sizeof(state_regs) / sizeof(state_regs[0]);
             ++reg) {
            lstrcpyA(name, "Reg");
            atirx_hex(name + 3, state_regs[reg], 3);
            atirx_key_hex(name, atirx_read(0, state_regs[reg]));
        }
    }
    atirx_flush();
    file = CreateFileA("C:\\V9XDIAG\\VRAM.BIN", GENERIC_WRITE, 0, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        atirx_key("Result", "VRAMDUMP-OPEN");
        return 0;
    }
    for (at = 0ul; at < ATIRX_VRAM_BYTES; at += ATIRX_DUMP_CHUNK) {
        for (index = 0ul; index < ATIRX_DUMP_CHUNK; ++index) {
            atirx_dump_buffer[index] = source[at + index];
        }
        if (!WriteFile(file, atirx_dump_buffer, ATIRX_DUMP_CHUNK, &written,
                       0) || written != ATIRX_DUMP_CHUNK) {
            CloseHandle(file);
            atirx_key("Result", "VRAMDUMP-WRITE");
            return 0;
        }
    }
    CloseHandle(file);
    atirx_key("Result", "VRAMDUMP-OK");
    return 1;
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
    /* Before the engine is touched: /vramdump may run under an
     * application that is using it. */
    if (atirx_has_switch(GetCommandLineA(), "/sample")) {
        int completed = atirx_run_sample();

        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
    if (atirx_has_switch(GetCommandLineA(), "/pll")) {
        int completed = atirx_run_pll();

        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
    if (atirx_has_switch(GetCommandLineA(), "/vramdump")) {
        int completed = atirx_run_vramdump();

        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }

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

    if (atirx_has_switch(GetCommandLineA(), "/mip")) {
        int completed = atirx_run_mip(&engine, &target);

        atirx_key_dec("FifoTimeouts", engine.fifo_timeouts);
        atirx_key_dec("IdleTimeouts", engine.idle_timeouts);
        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
    if (atirx_has_switch(GetCommandLineA(), "/texcache")) {
        int completed = atirx_run_texcache(&engine, &target);

        atirx_key_dec("FifoTimeouts", engine.fifo_timeouts);
        atirx_key_dec("IdleTimeouts", engine.idle_timeouts);
        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
    if (atirx_has_switch(GetCommandLineA(), "/fillcache")) {
        int completed = atirx_run_fillcache(&engine, map.fb_bytes);

        atirx_key_dec("FifoTimeouts", engine.fifo_timeouts);
        atirx_key_dec("IdleTimeouts", engine.idle_timeouts);
        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
    if (atirx_has_switch(GetCommandLineA(), "/fillz")) {
        int completed = atirx_run_fillz(&engine, map.fb_bytes);

        atirx_key_dec("FifoTimeouts", engine.fifo_timeouts);
        atirx_key_dec("IdleTimeouts", engine.idle_timeouts);
        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
    if (atirx_has_switch(GetCommandLineA(), "/fill")) {
        int completed = atirx_run_fill(&engine, map.fb_bytes);

        atirx_key_dec("FifoTimeouts", engine.fifo_timeouts);
        atirx_key_dec("IdleTimeouts", engine.idle_timeouts);
        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
    if (atirx_has_switch(GetCommandLineA(), "/crtc")) {
        int completed = atirx_run_crtc(
            atirx_has_switch(GetCommandLineA(), "/crtcread"));

        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
    if (atirx_has_switch(GetCommandLineA(), "/texmix")) {
        int completed = atirx_run_texmix(&engine, &target);

        atirx_key_dec("FifoTimeouts", engine.fifo_timeouts);
        atirx_key_dec("IdleTimeouts", engine.idle_timeouts);
        atirx_key_dec("Resets", engine.reset_count);
        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
    if (atirx_has_switch(GetCommandLineA(), "/texbil") ||
        atirx_has_switch(GetCommandLineA(), "/texlod")) {
        const char *line = GetCommandLineA();
        int completed;

        if (atirx_has_switch(line, "/texlod")) {
            completed = atirx_run_texbil(
                &engine, &target, atirx_lod_scenes,
                sizeof(atirx_lod_scenes) / sizeof(atirx_lod_scenes[0]),
                "texlod", ATIRX_CHECKER_OFFSET, 1);
        } else if (atirx_has_switch(line, "/texbil3")) {
            completed = atirx_run_texbil(
                &engine, &target, atirx_bil3_scenes,
                sizeof(atirx_bil3_scenes) / sizeof(atirx_bil3_scenes[0]),
                "texbil3", ATIRX_NOISE_OFFSET, 1);
        } else if (atirx_has_switch(line, "/texbil2")) {
            completed = atirx_run_texbil(
                &engine, &target, atirx_bil2_scenes,
                sizeof(atirx_bil2_scenes) / sizeof(atirx_bil2_scenes[0]),
                "texbil2", ATIRX_CHECKER2_OFFSET, 1);
        } else {
            completed = atirx_run_texbil(
                &engine, &target, atirx_bil_scenes,
                sizeof(atirx_bil_scenes) / sizeof(atirx_bil_scenes[0]),
                "texbil", ATIRX_CHECKER_OFFSET, 0);
        }

        atirx_key_dec("FifoTimeouts", engine.fifo_timeouts);
        atirx_key_dec("IdleTimeouts", engine.idle_timeouts);
        atirx_key_dec("Resets", engine.reset_count);
        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
    if (atirx_has_switch(GetCommandLineA(), "/textri")) {
        int completed;

        /* /textri2: sixty other triangles, q over 1..4. */
        if (atirx_has_switch(GetCommandLineA(), "/textri2")) {
            atirx_lcg = 0x9e3779b9ul;
            completed = atirx_run_textri(&engine, &target, 3.0);
        } else {
            completed = atirx_run_textri(&engine, &target, 1.0);
        }

        atirx_key_dec("FifoTimeouts", engine.fifo_timeouts);
        atirx_key_dec("IdleTimeouts", engine.idle_timeouts);
        atirx_key_dec("Resets", engine.reset_count);
        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
    if (atirx_has_switch(GetCommandLineA(), "/tex")) {
        int completed = atirx_has_switch(GetCommandLineA(), "/texprec")
            ? atirx_run_tex(&engine, &target, atirx_prec_scenes,
                            atirx_build_prec_scenes(), "texprec")
            : atirx_run_tex(&engine, &target, atirx_tex_scenes,
                            sizeof(atirx_tex_scenes) /
                                sizeof(atirx_tex_scenes[0]), "tex");

        atirx_key_dec("FifoTimeouts", engine.fifo_timeouts);
        atirx_key_dec("IdleTimeouts", engine.idle_timeouts);
        atirx_key_dec("Resets", engine.reset_count);
        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
    if (atirx_has_switch(GetCommandLineA(), "/zbuf")) {
        int completed = atirx_run_z(&engine, &target);

        atirx_key_dec("FifoTimeouts", engine.fifo_timeouts);
        atirx_key_dec("IdleTimeouts", engine.idle_timeouts);
        atirx_key_dec("Resets", engine.reset_count);
        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
    if (atirx_has_switch(GetCommandLineA(), "/gouraud")) {
        int completed = atirx_run_gouraud(&engine, &target);

        atirx_key_dec("FifoTimeouts", engine.fifo_timeouts);
        atirx_key_dec("IdleTimeouts", engine.idle_timeouts);
        atirx_key_dec("Resets", engine.reset_count);
        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
    if (atirx_has_switch(GetCommandLineA(), "/shade")) {
        int completed = atirx_run_shade(&engine, &target);

        atirx_key_dec("FifoTimeouts", engine.fifo_timeouts);
        atirx_key_dec("IdleTimeouts", engine.idle_timeouts);
        atirx_key_dec("Resets", engine.reset_count);
        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
    if (atirx_has_switch(GetCommandLineA(), "/regs")) {
        int completed = atirx_run_regs(&engine);

        atirx_key_dec("FifoTimeouts", engine.fifo_timeouts);
        atirx_key_dec("IdleTimeouts", engine.idle_timeouts);
        CloseHandle(atirx_out);
        ExitProcess(completed ? 0u : 1u);
    }
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
