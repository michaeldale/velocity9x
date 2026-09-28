#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

/* Phase 4 texture cache visibility.  One RGB565 replace scene is drawn
 * repeatedly at identical state and texture address while the CPU rewrites
 * the uniform texel between draws, alternating red and green.  With
 * TEX_CACHE_FLUSH in TEX_CNTL every draw must sample the texel just
 * written.  The same sequence is then repeated without the flush as a
 * sensitivity control: if it never shows the previous texel, this test
 * cannot show that the flush is what makes uploads visible, and the report
 * says so.
 *
 * Replace of an RGB565 texel into an RGB565 target is exact (item 10). */
#define ATITM_MAGIC 0x4b495441ul
#define ATITM_DIOC 30u
#define ATITM_TEXT_PATH "C:\\V9XDIAG\\ATI4TM.TXT"
#define ATITM_PASS_STATUS 0x0001fffful
#define ATITM_INTERIOR_BIT 0x00001000ul
#define ATITM_STATE_COUNT 19ul
#define ATITM_SETUP_COUNT 19ul
#define ATITM_SCALE_REPLACE 0x00010081ul
#define ATITM_TEX_SIZE_PITCH 0x40040444ul
#define ATITM_VERTEX_ARGB 0xff808080ul
#define ATITM_TEX_CNTL_FLUSH 0x40860000ul
#define ATITM_TEX_CNTL_NO_FLUSH 0x40060000ul
#define ATITM_RED 0xf800u
#define ATITM_GREEN 0x07e0u
#define ATITM_DRAWS 16ul
#define ATITM_TRANSCRIPT_COUNT 38ul
#define ATITM_WIDTH 64ul
#define ATITM_HEIGHT 28ul
#define ATITM_COVERED_PIXELS 256ul

struct atitm_result {
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
    DWORD write_offsets[ATITM_TRANSCRIPT_COUNT];
    DWORD write_values[ATITM_TRANSCRIPT_COUNT];
    WORD pixels[ATITM_WIDTH * ATITM_HEIGHT];
};

typedef char atitm_result_size_is_4016[
    sizeof(struct atitm_result) == 4016 ? 1 : -1];

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

/* Keys are "<prefix>NN.<field>" so the report stays one flat INI section. */
static void draw_key(char *key, const char *prefix, DWORD draw,
                     const char *field)
{
    int at = 0;
    while (*prefix != '\0') {
        key[at++] = *prefix++;
    }
    key[at++] = (char)('0' + draw / 10ul);
    key[at++] = (char)('0' + draw % 10ul);
    key[at++] = '.';
    while (*field != '\0') {
        key[at++] = *field++;
    }
    key[at] = '\0';
}

static void transcript(HANDLE file, const struct atitm_result *result)
{
    DWORD index;
    char offset_key[] = "WriteOffset00";
    char value_key[] = "WriteValue00";
    for (index = 0ul; index < ATITM_TRANSCRIPT_COUNT; ++index) {
        offset_key[11] = (char)('0' + index / 10ul);
        offset_key[12] = (char)('0' + index % 10ul);
        value_key[10] = (char)('0' + index / 10ul);
        value_key[11] = (char)('0' + index % 10ul);
        hx(file, offset_key, result->write_offsets[index]);
        hx(file, value_key, result->write_values[index]);
    }
}

static int atitm_draw_safe(const struct atitm_result *result)
{
    return (result->status | ATITM_INTERIOR_BIT) == ATITM_PASS_STATUS &&
           result->state_count == ATITM_STATE_COUNT &&
           result->setup_count == ATITM_SETUP_COUNT &&
           result->interior_mismatch == 0ul &&
           result->exterior_mismatch == 0ul &&
           result->guard_mismatch == 0ul &&
           result->restore_mismatch == 0ul &&
           result->reserved == 0ul &&
           result->timeout_stage == 0ul &&
           result->changed_pixels == ATITM_COVERED_PIXELS &&
           result->min_x == 8ul && result->min_y == 6ul &&
           result->max_x == 38ul && result->max_y == 21ul;
}

/* Runs one sequence and returns the number of draws that sampled the texel
 * just written; *stale counts draws that sampled the previous one. */
static DWORD atitm_sequence(HANDLE device, HANDLE file, const char *prefix,
                            DWORD tex_cntl, DWORD *stale, int *safe,
                            struct atitm_result *result)
{
    DWORD input[6];
    DWORD bytes;
    DWORD draw;
    DWORD fresh = 0ul;
    WORD texel;
    WORD previous;
    char key[40];

    *stale = 0ul;
    for (draw = 0ul; draw < ATITM_DRAWS; ++draw) {
        texel = (draw & 1ul) == 0ul ? ATITM_RED : ATITM_GREEN;
        previous = (draw & 1ul) == 0ul ? ATITM_GREEN : ATITM_RED;
        input[0] = ATITM_SCALE_REPLACE;
        input[1] = ATITM_TEX_SIZE_PITCH;
        input[2] = texel;
        input[3] = ATITM_VERTEX_ARGB;
        input[4] = texel;
        input[5] = tex_cntl;

        bytes = 0ul;
        if (!DeviceIoControl(device, ATITM_DIOC, input, sizeof(input),
                             result, sizeof(*result), &bytes, 0) ||
            bytes != sizeof(*result) || result->magic != ATITM_MAGIC) {
            draw_key(key, prefix, draw, "Stop");
            line(file, key, "dioc-refused");
            *safe = 0;
            return fresh;
        }

        draw_key(key, prefix, draw, "Texel565");
        hx(file, key, texel);
        draw_key(key, prefix, draw, "Observed565");
        hx(file, key, result->first_actual);
        draw_key(key, prefix, draw, "Status");
        hx(file, key, result->status);

        if ((WORD)result->first_actual == texel) {
            ++fresh;
        } else if (draw != 0ul && (WORD)result->first_actual == previous) {
            ++*stale;
        }
        if (!atitm_draw_safe(result)) {
            draw_key(key, prefix, draw, "Stop");
            line(file, key, "unsafe");
            *safe = 0;
            return fresh;
        }
    }
    return fresh;
}

void WINAPI V9xAtiMach64TextureMutationEntry(void)
{
    static struct atitm_result result;
    HANDLE device;
    HANDLE file;
    DWORD bytes;
    DWORD flush_fresh;
    DWORD flush_stale;
    DWORD control_fresh = 0ul;
    DWORD control_stale = 0ul;
    char heading[] = "[AtiMach64Phase4TextureMutation]\r\n";
    int safe = 1;
    int pass;

    CreateDirectoryA("C:\\V9XDIAG", 0);
    file = CreateFileA(ATITM_TEXT_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }

    WriteFile(file, heading, (DWORD)lstrlenA(heading), &bytes, 0);
    line(file, "Build", V9X_BUILD_ID);
    line(file, "Operation", "guarded-rgb565-texture-rewrite-at-fixed-state");
    dn(file, "DrawsPerSequence", ATITM_DRAWS);

    device = CreateFileA("\\\\.\\ATIEN.VXD", 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        line(file, "Result", "REVIEW");
        line(file, "StopReason", "vxd-load");
        CloseHandle(file);
        ExitProcess(2u);
    }

    flush_fresh = atitm_sequence(device, file, "Flush", ATITM_TEX_CNTL_FLUSH,
                                 &flush_stale, &safe, &result);
    if (safe) {
        control_fresh = atitm_sequence(device, file, "NoFlush",
                                       ATITM_TEX_CNTL_NO_FLUSH,
                                       &control_stale, &safe, &result);
    }
    CloseHandle(device);

    dn(file, "FlushFreshDraws", flush_fresh);
    dn(file, "FlushStaleDraws", flush_stale);
    dn(file, "NoFlushFreshDraws", control_fresh);
    dn(file, "NoFlushStaleDraws", control_stale);
    line(file, "ControlShowsStaleCache",
         control_stale != 0ul ? "yes" : "no");
    transcript(file, &result);

    /* The gate is the flushed sequence.  The control only states whether
     * the test is sensitive; it cannot fail the gate. */
    pass = safe && flush_fresh == ATITM_DRAWS && flush_stale == 0ul;
    line(file, "Result", pass ? "PASS" : "REVIEW");
    CloseHandle(file);
    ExitProcess(pass ? 0u : 1u);
}
