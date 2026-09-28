#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

/* Phase 4 item 11: hardware scissor.  The proven Phase 3 flat triangle is
 * drawn first with the scissor covering the whole 64x28 target; that dump
 * is this session's reference.  Each further scene narrows SC_LEFT_RIGHT
 * and SC_TOP_BOTTOM, and every one of its 1,792 pixels must equal the
 * reference inside the rectangle and the 0xA55A sentinel outside it.
 *
 * Rectangles are half-open, as the shared builder takes them, and are
 * encoded as it encodes them: inclusive right = right - 1, inclusive
 * bottom = bottom - 1.  The split lines cross the triangle at columns and
 * rows where it has pixels on both sides, so an off-by-one in either
 * inclusive edge changes the image. */
#define ATISC_MAGIC 0x4c495441ul
#define ATISC_DIOC 28u
#define ATISC_TEXT_PATH "C:\\V9XDIAG\\ATI4SC.TXT"
#define ATISC_BIN_PATH "C:\\V9XDIAG\\ATI4SC.BIN"
#define ATISC_PASS_STATUS 0x0001fffful
#define ATISC_STATE_COUNT 17ul
#define ATISC_SETUP_COUNT 19ul
#define ATISC_SENTINEL 0xa55au
#define ATISC_REFERENCE_PIXELS 256ul
#define ATISC_SCENES 10
#define ATISC_TRANSCRIPT_COUNT 38ul
#define ATISC_WIDTH 64ul
#define ATISC_HEIGHT 28ul

struct atisc_result {
    DWORD magic;
    DWORD status;
    DWORD command_status;
    DWORD revision_class;
    DWORD bar0;
    DWORD bar2;
    DWORD chip_id;
    DWORD config_stat0;
    DWORD gui_before;
    DWORD gui_after;
    DWORD mem_before;
    DWORD mem_after;
    DWORD target_offset;
    DWORD dst_off_pitch;
    DWORD color;
    DWORD one_over_area;
    DWORD interior_mismatch;
    DWORD exterior_mismatch;
    DWORD guard_mismatch;
    DWORD restore_mismatch;
    DWORD changed_pixels;
    DWORD min_x;
    DWORD min_y;
    DWORD max_x;
    DWORD max_y;
    DWORD fifo_sample;
    DWORD state_count;
    DWORD setup_count;
    DWORD reserved;
    DWORD timeout_stage;
    DWORD first_actual;
    DWORD first_expected;
    DWORD write_offsets[ATISC_TRANSCRIPT_COUNT];
    DWORD write_values[ATISC_TRANSCRIPT_COUNT];
    WORD pixels[ATISC_WIDTH * ATISC_HEIGHT];
};

typedef char atisc_result_size_is_4016[
    sizeof(struct atisc_result) == 4016 ? 1 : -1];

struct atisc_rect {
    const char *name;
    DWORD left;
    DWORD top;
    DWORD right;
    DWORD bottom;
};

/* The reference triangle covers (8,6) through (38,21). */
static const struct atisc_rect atisc_rects[ATISC_SCENES] = {
    { "Full", 0ul, 0ul, 64ul, 28ul },
    { "LeftOfX20", 0ul, 0ul, 20ul, 28ul },
    { "RightFromX20", 20ul, 0ul, 64ul, 28ul },
    { "AboveY12", 0ul, 0ul, 64ul, 12ul },
    { "BelowFromY12", 0ul, 12ul, 64ul, 28ul },
    { "InteriorBox", 14ul, 9ul, 30ul, 17ul },
    { "ColumnX20", 20ul, 0ul, 21ul, 28ul },
    { "RowY12", 0ul, 12ul, 64ul, 13ul },
    { "VertexCorner", 8ul, 6ul, 12ul, 10ul },
    { "MissesTriangle", 40ul, 0ul, 64ul, 28ul }
};

static DWORD atisc_crc32(const BYTE *data, DWORD length)
{
    DWORD crc = 0xfffffffful;
    DWORD index;
    int bit;
    for (index = 0ul; index < length; ++index) {
        crc ^= data[index];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320ul & (0ul - (crc & 1ul)));
        }
    }
    return ~crc;
}

static void hex(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    int index;
    text[0] = '0';
    text[1] = 'x';
    for (index = 0; index < 8; ++index) {
        text[2 + index] = digits[(value >> ((7 - index) * 4)) & 15u];
    }
    text[10] = '\0';
}

static void dec(char *text, DWORD value)
{
    char reverse[11];
    int count = 0;
    int index = 0;
    if (value == 0ul) {
        text[0] = '0';
        text[1] = '\0';
        return;
    }
    while (value != 0ul) {
        reverse[count++] = (char)('0' + value % 10ul);
        value /= 10ul;
    }
    while (count != 0) {
        text[index++] = reverse[--count];
    }
    text[index] = '\0';
}

static void line(HANDLE file, const char *key, const char *value)
{
    DWORD written;
    WriteFile(file, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(file, "=", 1ul, &written, 0);
    WriteFile(file, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(file, "\r\n", 2ul, &written, 0);
}

static void hx(HANDLE file, const char *key, DWORD value)
{
    char text[11];
    hex(text, value);
    line(file, key, text);
}

static void dn(HANDLE file, const char *key, DWORD value)
{
    char text[11];
    dec(text, value);
    line(file, key, text);
}

/* Keys are "SceneNN.<field>" so the report stays one flat INI section. */
static void scene_key(char *key, DWORD scene, const char *field)
{
    int at = 0;
    key[at++] = 'S';
    key[at++] = 'c';
    key[at++] = 'e';
    key[at++] = 'n';
    key[at++] = 'e';
    key[at++] = (char)('0' + scene / 10ul);
    key[at++] = (char)('0' + scene % 10ul);
    key[at++] = '.';
    while (*field != '\0') {
        key[at++] = *field++;
    }
    key[at] = '\0';
}

static void transcript(HANDLE file, const struct atisc_result *result)
{
    DWORD index;
    char offset_key[] = "WriteOffset00";
    char value_key[] = "WriteValue00";
    for (index = 0ul; index < ATISC_TRANSCRIPT_COUNT; ++index) {
        offset_key[11] = (char)('0' + index / 10ul);
        offset_key[12] = (char)('0' + index % 10ul);
        value_key[10] = (char)('0' + index / 10ul);
        value_key[11] = (char)('0' + index % 10ul);
        hx(file, offset_key, result->write_offsets[index]);
        hx(file, value_key, result->write_values[index]);
    }
}

static int atisc_scene_safe(const struct atisc_result *result)
{
    return result->status == ATISC_PASS_STATUS &&
           result->state_count == ATISC_STATE_COUNT &&
           result->setup_count == ATISC_SETUP_COUNT &&
           result->interior_mismatch == 0ul &&
           result->exterior_mismatch == 0ul &&
           result->guard_mismatch == 0ul &&
           result->restore_mismatch == 0ul &&
           result->reserved == 0ul &&
           result->timeout_stage == 0ul;
}

void WINAPI V9xAtiMach64ScissorTableEntry(void)
{
    static struct atisc_result result;
    static WORD reference[ATISC_WIDTH * ATISC_HEIGHT];
    DWORD input[2];
    HANDLE device;
    HANDLE file;
    HANDLE bin;
    DWORD bytes;
    DWORD scene;
    DWORD x;
    DWORD y;
    DWORD index;
    DWORD expected_pixels;
    DWORD mismatches;
    DWORD completed = 0ul;
    DWORD failed_scenes = 0ul;
    WORD expected;
    const struct atisc_rect *rect;
    char key[40];
    char heading[] = "[AtiMach64Phase4ScissorTable]\r\n";
    int safe = 1;
    int inside;
    int pass;

    CreateDirectoryA("C:\\V9XDIAG", 0);
    file = CreateFileA(ATISC_TEXT_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    bin = CreateFileA(ATISC_BIN_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (bin == INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        ExitProcess(5u);
    }

    WriteFile(file, heading, (DWORD)lstrlenA(heading), &bytes, 0);
    line(file, "Build", V9X_BUILD_ID);
    line(file, "Operation", "guarded-rgb565-flat-triangle-scissor-table");
    dn(file, "SceneCount", ATISC_SCENES);

    device = CreateFileA("\\\\.\\ATIEN.VXD", 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        line(file, "Result", "REVIEW");
        line(file, "StopReason", "vxd-load");
        CloseHandle(bin);
        CloseHandle(file);
        ExitProcess(2u);
    }

    /* The first unsafe scene stops the table. */
    for (scene = 0ul; scene < ATISC_SCENES; ++scene) {
        rect = &atisc_rects[scene];
        input[0] = ((rect->right - 1ul) << 16) | rect->left;
        input[1] = ((rect->bottom - 1ul) << 16) | rect->top;

        scene_key(key, scene, "Rect");
        line(file, key, rect->name);
        scene_key(key, scene, "ScLeftRight");
        hx(file, key, input[0]);
        scene_key(key, scene, "ScTopBottom");
        hx(file, key, input[1]);

        bytes = 0ul;
        if (!DeviceIoControl(device, ATISC_DIOC, input, sizeof(input),
                             &result, sizeof(result), &bytes, 0) ||
            bytes != sizeof(result) || result.magic != ATISC_MAGIC) {
            scene_key(key, scene, "Stop");
            line(file, key, "dioc-refused");
            safe = 0;
            break;
        }
        WriteFile(bin, result.pixels, sizeof(result.pixels), &bytes, 0);

        if (scene == 0ul) {
            for (index = 0ul; index < ATISC_WIDTH * ATISC_HEIGHT; ++index) {
                reference[index] = result.pixels[index];
            }
        }

        expected_pixels = 0ul;
        mismatches = 0ul;
        for (y = 0ul; y < ATISC_HEIGHT; ++y) {
            for (x = 0ul; x < ATISC_WIDTH; ++x) {
                index = y * ATISC_WIDTH + x;
                inside = x >= rect->left && x < rect->right &&
                         y >= rect->top && y < rect->bottom;
                expected = inside ? reference[index] : ATISC_SENTINEL;
                if (expected != ATISC_SENTINEL) {
                    ++expected_pixels;
                }
                if (result.pixels[index] != expected) {
                    ++mismatches;
                }
            }
        }

        scene_key(key, scene, "Status");
        hx(file, key, result.status);
        scene_key(key, scene, "ChangedPixels");
        dn(file, key, result.changed_pixels);
        scene_key(key, scene, "ExpectedChangedPixels");
        dn(file, key, expected_pixels);
        scene_key(key, scene, "PixelMismatches");
        dn(file, key, mismatches);
        scene_key(key, scene, "ExteriorMismatches");
        dn(file, key, result.exterior_mismatch);
        scene_key(key, scene, "GuardMismatches");
        dn(file, key, result.guard_mismatch);
        scene_key(key, scene, "RestoreMismatches");
        dn(file, key, result.restore_mismatch);
        scene_key(key, scene, "RecoveryResetCount");
        dn(file, key, result.reserved);
        scene_key(key, scene, "TimeoutStage");
        dn(file, key, result.timeout_stage);
        scene_key(key, scene, "PixelCrc32");
        hx(file, key, atisc_crc32((const BYTE *)result.pixels,
                                  sizeof(result.pixels)));

        ++completed;
        if (mismatches != 0ul || result.changed_pixels != expected_pixels) {
            ++failed_scenes;
        }
        /* The reference itself must be the proven 256-pixel triangle. */
        if (scene == 0ul && result.changed_pixels != ATISC_REFERENCE_PIXELS) {
            ++failed_scenes;
        }
        if (!atisc_scene_safe(&result)) {
            scene_key(key, scene, "Stop");
            line(file, key, "unsafe");
            safe = 0;
            break;
        }
    }
    CloseHandle(device);
    CloseHandle(bin);

    dn(file, "CompletedScenes", completed);
    dn(file, "FailedScenes", failed_scenes);
    transcript(file, &result);

    pass = safe && completed == ATISC_SCENES && failed_scenes == 0ul;
    line(file, "Result", pass ? "PASS" : "REVIEW");
    CloseHandle(file);
    ExitProcess(pass ? 0u : 1u);
}
