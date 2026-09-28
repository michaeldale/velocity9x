#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

/* Phase 4 item 12: fog with blending disabled.  The Phase 3 flat triangle
 * is drawn with ALPHA_FOG_EN=2 and the SRCALPHA/INVSRCALPHA blend fields,
 * which Mesa and Utah-GLX describe as blending the fragment with DP_FOG_CLR
 * by the vertex specular alpha:  C = Cv*f + Cfog*(1-f).
 *
 * The vertex ARGB alpha (0x30) differs from every specular alpha used, so
 * a factor taken from vertex alpha would be visible.  Scene 0 has fog off
 * with specular alpha 0 and must leave the vertex colour untouched.
 *
 * Rounding is not settled (see items 9 and 10), so a scene passes within
 * one 565 unit per channel of the /255 truncating rule.  Every rejected
 * alternative must miss some scene by more than one unit. */
#define ATIFG_MAGIC 0x4d495441ul
#define ATIFG_DIOC 29u
#define ATIFG_TEXT_PATH "C:\\V9XDIAG\\ATI4FG.TXT"
#define ATIFG_BIN_PATH "C:\\V9XDIAG\\ATI4FG.BIN"
#define ATIFG_PASS_STATUS 0x0001fffful
#define ATIFG_INTERIOR_BIT 0x00001000ul
#define ATIFG_STATE_COUNT 17ul
#define ATIFG_SETUP_COUNT 19ul
#define ATIFG_SCALE_FOG_OFF 0x000100c1ul
#define ATIFG_SCALE_FOG_ON 0x002c10c1ul
#define ATIFG_VERTEX_ARGB 0x30b04c28ul
#define ATIFG_SENTINEL 0xa55au
#define ATIFG_SCENES 7
#define ATIFG_TOLERANCE 1ul
#define ATIFG_TRANSCRIPT_COUNT 38ul
#define ATIFG_WIDTH 64ul
#define ATIFG_HEIGHT 28ul
#define ATIFG_COVERED_PIXELS 256ul

#define SEM_MEASURED 0
#define SEM_VERTEX_ALPHA_FACTOR 1
#define SEM_INVERTED_FACTOR 2
#define SEM_FOG_IGNORED 3
#define SEM_COUNT 4

struct atifg_result {
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
    DWORD write_offsets[ATIFG_TRANSCRIPT_COUNT];
    DWORD write_values[ATIFG_TRANSCRIPT_COUNT];
    WORD pixels[ATIFG_WIDTH * ATIFG_HEIGHT];
};

typedef char atifg_result_size_is_4016[
    sizeof(struct atifg_result) == 4016 ? 1 : -1];

struct atifg_scene {
    int fog;
    DWORD fog_color;
    DWORD factor;
};

static const struct atifg_scene atifg_scenes[ATIFG_SCENES] = {
    { 0, 0x0020d0e0ul, 0ul },
    { 1, 0x0020d0e0ul, 255ul },
    { 1, 0x0020d0e0ul, 0ul },
    { 1, 0x0020d0e0ul, 128ul },
    { 1, 0x0020d0e0ul, 64ul },
    { 1, 0x0020d0e0ul, 192ul },
    { 1, 0x00f01030ul, 128ul }
};

static const char *const atifg_sem_names[SEM_COUNT] = {
    "Measured", "VertexAlphaFactor", "InvertedFactor", "FogIgnored"
};

static WORD atifg_predict(const struct atifg_scene *scene, int semantics)
{
    DWORD vertex[3];
    DWORD fog[3];
    DWORD out[3];
    DWORD factor;
    DWORD channel;

    vertex[0] = (ATIFG_VERTEX_ARGB >> 16) & 255ul;
    vertex[1] = (ATIFG_VERTEX_ARGB >> 8) & 255ul;
    vertex[2] = ATIFG_VERTEX_ARGB & 255ul;
    fog[0] = (scene->fog_color >> 16) & 255ul;
    fog[1] = (scene->fog_color >> 8) & 255ul;
    fog[2] = scene->fog_color & 255ul;

    factor = scene->factor;
    if (semantics == SEM_VERTEX_ALPHA_FACTOR) {
        factor = (ATIFG_VERTEX_ARGB >> 24) & 255ul;
    } else if (semantics == SEM_INVERTED_FACTOR) {
        factor = 255ul - factor;
    }

    for (channel = 0ul; channel < 3ul; ++channel) {
        if (!scene->fog || semantics == SEM_FOG_IGNORED) {
            out[channel] = vertex[channel];
        } else {
            out[channel] = (vertex[channel] * factor +
                            fog[channel] * (255ul - factor)) / 255ul;
        }
    }
    return (WORD)(((out[0] >> 3) << 11) | ((out[1] >> 2) << 5) |
                  (out[2] >> 3));
}

/* Largest per-channel difference, in 565 units. */
static DWORD atifg_distance(WORD a, WORD b)
{
    DWORD shift[3];
    DWORD mask[3];
    DWORD channel;
    DWORD x;
    DWORD y;
    DWORD worst = 0ul;
    shift[0] = 11ul; shift[1] = 5ul; shift[2] = 0ul;
    mask[0] = 31ul; mask[1] = 63ul; mask[2] = 31ul;
    for (channel = 0ul; channel < 3ul; ++channel) {
        x = ((DWORD)a >> shift[channel]) & mask[channel];
        y = ((DWORD)b >> shift[channel]) & mask[channel];
        x = x > y ? x - y : y - x;
        if (x > worst) {
            worst = x;
        }
    }
    return worst;
}

static DWORD atifg_crc32(const BYTE *data, DWORD length)
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

static void transcript(HANDLE file, const struct atifg_result *result)
{
    DWORD index;
    char offset_key[] = "WriteOffset00";
    char value_key[] = "WriteValue00";
    for (index = 0ul; index < ATIFG_TRANSCRIPT_COUNT; ++index) {
        offset_key[11] = (char)('0' + index / 10ul);
        offset_key[12] = (char)('0' + index % 10ul);
        value_key[10] = (char)('0' + index / 10ul);
        value_key[11] = (char)('0' + index % 10ul);
        hx(file, offset_key, result->write_offsets[index]);
        hx(file, value_key, result->write_values[index]);
    }
}

static int atifg_scene_safe(const struct atifg_result *result)
{
    if ((result->status | ATIFG_INTERIOR_BIT) != ATIFG_PASS_STATUS ||
        result->state_count != ATIFG_STATE_COUNT ||
        result->setup_count != ATIFG_SETUP_COUNT ||
        result->interior_mismatch != 0ul ||
        result->exterior_mismatch != 0ul ||
        result->guard_mismatch != 0ul ||
        result->restore_mismatch != 0ul ||
        result->reserved != 0ul ||
        result->timeout_stage != 0ul) {
        return 0;
    }

    if (result->first_actual == ATIFG_SENTINEL) {
        return result->changed_pixels == 0ul;
    }

    return result->changed_pixels == ATIFG_COVERED_PIXELS &&
           result->min_x == 8ul && result->min_y == 6ul &&
           result->max_x == 38ul && result->max_y == 21ul;
}

void WINAPI V9xAtiMach64FogTableEntry(void)
{
    static struct atifg_result result;
    static DWORD rejected_worst[SEM_COUNT];
    DWORD input[5];
    HANDLE device;
    HANDLE file;
    HANDLE bin;
    DWORD bytes;
    DWORD scene;
    DWORD completed = 0ul;
    DWORD exact = 0ul;
    DWORD within = 0ul;
    DWORD distance;
    WORD observed;
    const struct atifg_scene *entry;
    char key[48];
    char heading[] = "[AtiMach64Phase4FogTable]\r\n";
    int semantics;
    int rejected_visible = 1;
    int safe = 1;
    int pass;

    CreateDirectoryA("C:\\V9XDIAG", 0);
    file = CreateFileA(ATIFG_TEXT_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    bin = CreateFileA(ATIFG_BIN_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (bin == INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        ExitProcess(5u);
    }

    WriteFile(file, heading, (DWORD)lstrlenA(heading), &bytes, 0);
    line(file, "Build", V9X_BUILD_ID);
    line(file, "Operation", "guarded-rgb565-flat-triangle-fog-table");
    hx(file, "VertexArgb", ATIFG_VERTEX_ARGB);
    dn(file, "SceneCount", ATIFG_SCENES);
    dn(file, "Tolerance565", ATIFG_TOLERANCE);

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
    for (scene = 0ul; scene < ATIFG_SCENES; ++scene) {
        entry = &atifg_scenes[scene];
        input[0] = entry->fog ? ATIFG_SCALE_FOG_ON : ATIFG_SCALE_FOG_OFF;
        input[1] = entry->fog_color;
        input[2] = ATIFG_VERTEX_ARGB;
        input[3] = entry->factor << 24;
        input[4] = atifg_predict(entry, SEM_MEASURED);

        /* The three VERTEX_n_SPEC_ARGB writes are outside the VxD's
         * 38-entry transcript; their one value is recorded here. */
        scene_key(key, scene, "Scale3dCntl");
        hx(file, key, input[0]);
        scene_key(key, scene, "DpFogClr");
        hx(file, key, input[1]);
        scene_key(key, scene, "VertexSpecArgb");
        hx(file, key, input[3]);

        bytes = 0ul;
        if (!DeviceIoControl(device, ATIFG_DIOC, input, sizeof(input),
                             &result, sizeof(result), &bytes, 0) ||
            bytes != sizeof(result) || result.magic != ATIFG_MAGIC) {
            scene_key(key, scene, "Stop");
            line(file, key, "dioc-refused");
            safe = 0;
            break;
        }
        WriteFile(bin, result.pixels, sizeof(result.pixels), &bytes, 0);

        observed = (WORD)result.first_actual;
        distance = atifg_distance(observed, (WORD)input[4]);
        if (distance == 0ul) {
            ++exact;
        }
        if (distance <= ATIFG_TOLERANCE) {
            ++within;
        }
        for (semantics = 1; semantics < SEM_COUNT; ++semantics) {
            DWORD miss = atifg_distance(observed,
                                        atifg_predict(entry, semantics));
            if (miss > rejected_worst[semantics]) {
                rejected_worst[semantics] = miss;
            }
        }

        scene_key(key, scene, "Status");
        hx(file, key, result.status);
        scene_key(key, scene, "Observed565");
        hx(file, key, result.first_actual);
        scene_key(key, scene, "Predicted565");
        hx(file, key, result.first_expected);
        scene_key(key, scene, "Distance565");
        dn(file, key, distance);
        scene_key(key, scene, "ProbeNonUniform");
        dn(file, key, result.interior_mismatch);
        scene_key(key, scene, "ExteriorMismatches");
        dn(file, key, result.exterior_mismatch);
        scene_key(key, scene, "GuardMismatches");
        dn(file, key, result.guard_mismatch);
        scene_key(key, scene, "RestoreMismatches");
        dn(file, key, result.restore_mismatch);
        scene_key(key, scene, "ChangedPixels");
        dn(file, key, result.changed_pixels);
        scene_key(key, scene, "RecoveryResetCount");
        dn(file, key, result.reserved);
        scene_key(key, scene, "TimeoutStage");
        dn(file, key, result.timeout_stage);
        scene_key(key, scene, "PixelCrc32");
        hx(file, key, atifg_crc32((const BYTE *)result.pixels,
                                  sizeof(result.pixels)));

        ++completed;
        if (!atifg_scene_safe(&result)) {
            scene_key(key, scene, "Stop");
            line(file, key, "unsafe-or-inconsistent");
            safe = 0;
            break;
        }
    }
    CloseHandle(device);
    CloseHandle(bin);

    dn(file, "CompletedScenes", completed);
    dn(file, "ExactScenes", exact);
    dn(file, "WithinToleranceScenes", within);

    /* A rejected semantic inside the tolerance everywhere would let the
     * tolerance, not the hardware, decide the verdict. */
    for (semantics = 1; semantics < SEM_COUNT; ++semantics) {
        char rejected_key[48];
        lstrcpyA(rejected_key, "RejectedWorst565.");
        lstrcatA(rejected_key, atifg_sem_names[semantics]);
        dn(file, rejected_key, rejected_worst[semantics]);
        if (rejected_worst[semantics] <= ATIFG_TOLERANCE) {
            rejected_visible = 0;
        }
    }

    transcript(file, &result);

    pass = safe && completed == ATIFG_SCENES && within == ATIFG_SCENES &&
           rejected_visible;
    line(file, "Result", pass ? "PASS" : "REVIEW");
    CloseHandle(file);
    ExitProcess(pass ? 0u : 1u);
}
