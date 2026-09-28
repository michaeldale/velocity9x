#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

/* Phase 4 item 9: every ADD factor pair proposed for publication.  Sources
 * are ZERO, ONE, SRCALPHA, INVSRCALPHA, DESTCOLOR and INVDESTCOLOR;
 * destinations are ZERO, ONE, SRCCOLOR, INVSRCCOLOR, SRCALPHA and
 * INVSRCALPHA.  Destination-alpha factors and SRCALPHASAT are excluded: an
 * RGB565 or XRGB1555 target stores no alpha, so their result is undefined.
 *
 * The hardware rounding rule is not yet measured.  Each pair is compared
 * with every candidate CPU model that reproduces the two proven pairs, and
 * the table passes only if one model predicts all 36 pairs.  The source
 * ARGB was chosen so that, under every candidate model, changing either
 * factor field of any pair to another listed factor changes the pixel. */
#define ATIBT_MAGIC 0x4a495441ul
#define ATIBT_DIOC 26u
#define ATIBT_TEXT_PATH "C:\\V9XDIAG\\ATI4BT.TXT"
#define ATIBT_BIN_PATH "C:\\V9XDIAG\\ATI4BT.BIN"
#define ATIBT_PASS_STATUS 0x0001fffful
#define ATIBT_INTERIOR_BIT 0x00001000ul
#define ATIBT_STATE_COUNT 17ul
#define ATIBT_SETUP_COUNT 19ul
#define ATIBT_SOURCE_ARGB 0xd4b16100ul
#define ATIBT_DESTINATION 0xa55au
#define ATIBT_ADD_BASE 0x000008c1ul
#define ATIBT_SOURCE_SHIFT 16
#define ATIBT_DEST_SHIFT 19
#define ATIBT_FACTORS 6
#define ATIBT_PAIRS 36
#define ATIBT_MODELS 10
#define ATIBT_TRANSCRIPT_COUNT 38ul
#define ATIBT_WIDTH 64ul
#define ATIBT_HEIGHT 28ul
#define ATIBT_COVERED_PIXELS 256ul

#define F_ZERO 0
#define F_ONE 1
#define F_SRCALPHA 2
#define F_INVSRCALPHA 3
#define F_DESTCOLOR 4
#define F_INVDESTCOLOR 5
#define F_SRCCOLOR 6
#define F_INVSRCCOLOR 7

struct atibt_result {
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
    DWORD write_offsets[ATIBT_TRANSCRIPT_COUNT];
    DWORD write_values[ATIBT_TRANSCRIPT_COUNT];
    WORD pixels[ATIBT_WIDTH * ATIBT_HEIGHT];
};

typedef char atibt_result_size_is_4016[
    sizeof(struct atibt_result) == 4016 ? 1 : -1];

struct atibt_factor {
    const char *name;
    DWORD d3d;
    DWORD field;
    int kind;
};

/* SCALE_3D_CNTL ALPHA_BLEND_SRC / ALPHA_BLEND_DST codes, as encoded by
 * v9x_m64_build_blend_control (3D RAGE LT PRO Register Reference). */
static const struct atibt_factor atibt_sources[ATIBT_FACTORS] = {
    { "ZERO", 1ul, 0ul, F_ZERO },
    { "ONE", 2ul, 1ul, F_ONE },
    { "SRCALPHA", 5ul, 4ul, F_SRCALPHA },
    { "INVSRCALPHA", 6ul, 5ul, F_INVSRCALPHA },
    { "DESTCOLOR", 9ul, 2ul, F_DESTCOLOR },
    { "INVDESTCOLOR", 10ul, 3ul, F_INVDESTCOLOR }
};

static const struct atibt_factor atibt_destinations[ATIBT_FACTORS] = {
    { "ZERO", 1ul, 0ul, F_ZERO },
    { "ONE", 2ul, 1ul, F_ONE },
    { "SRCCOLOR", 3ul, 2ul, F_SRCCOLOR },
    { "INVSRCCOLOR", 4ul, 3ul, F_INVSRCCOLOR },
    { "SRCALPHA", 5ul, 4ul, F_SRCALPHA },
    { "INVSRCALPHA", 6ul, 5ul, F_INVSRCALPHA }
};

/* Candidate rules: factor denominator, inverse minuend, round-to-nearest
 * before clamping, and round-to-nearest 8-to-565 conversion.  These are the
 * ten of twelve simple rules that reproduce ONE/ONE 0xFD5A and
 * SRCALPHA/INVSRCALPHA 0xD2AD on this board. */
struct atibt_model {
    DWORD denominator;
    DWORD inverse;
    int round_blend;
    int round_pack;
};

static const struct atibt_model atibt_models[ATIBT_MODELS] = {
    { 255ul, 255ul, 0, 0 }, { 255ul, 255ul, 0, 1 },
    { 255ul, 255ul, 1, 0 }, { 255ul, 255ul, 1, 1 },
    { 256ul, 255ul, 0, 0 }, { 256ul, 255ul, 1, 0 },
    { 256ul, 256ul, 0, 0 }, { 256ul, 256ul, 0, 1 },
    { 256ul, 256ul, 1, 0 }, { 256ul, 256ul, 1, 1 }
};

static DWORD atibt_factor_numerator(int kind, DWORD source, DWORD dest,
                                    DWORD alpha,
                                    const struct atibt_model *model)
{
    switch (kind) {
    case F_ONE: return model->denominator;
    case F_SRCALPHA: return alpha;
    case F_INVSRCALPHA: return model->inverse - alpha;
    case F_DESTCOLOR: return dest;
    case F_INVDESTCOLOR: return model->inverse - dest;
    case F_SRCCOLOR: return source;
    case F_INVSRCCOLOR: return model->inverse - source;
    default: return 0ul;
    }
}

static WORD atibt_predict(const struct atibt_factor *source_factor,
                          const struct atibt_factor *dest_factor,
                          const struct atibt_model *model)
{
    DWORD source[3];
    DWORD dest[3];
    DWORD out[3];
    DWORD alpha;
    DWORD sum;
    DWORD channel;
    DWORD r5;
    DWORD g6;
    DWORD b5;

    alpha = (ATIBT_SOURCE_ARGB >> 24) & 255ul;
    source[0] = (ATIBT_SOURCE_ARGB >> 16) & 255ul;
    source[1] = (ATIBT_SOURCE_ARGB >> 8) & 255ul;
    source[2] = ATIBT_SOURCE_ARGB & 255ul;
    r5 = (ATIBT_DESTINATION >> 11) & 31ul;
    g6 = (ATIBT_DESTINATION >> 5) & 63ul;
    b5 = ATIBT_DESTINATION & 31ul;
    dest[0] = (r5 << 3) | (r5 >> 2);
    dest[1] = (g6 << 2) | (g6 >> 4);
    dest[2] = (b5 << 3) | (b5 >> 2);

    for (channel = 0ul; channel < 3ul; ++channel) {
        sum = source[channel] *
              atibt_factor_numerator(source_factor->kind, source[channel],
                                     dest[channel], alpha, model) +
              dest[channel] *
              atibt_factor_numerator(dest_factor->kind, source[channel],
                                     dest[channel], alpha, model);
        if (model->round_blend) {
            out[channel] = (2ul * sum + model->denominator) /
                           (2ul * model->denominator);
        } else {
            out[channel] = sum / model->denominator;
        }
        if (out[channel] > 255ul) {
            out[channel] = 255ul;
        }
    }

    if (model->round_pack) {
        return (WORD)((((out[0] * 62ul + 255ul) / 510ul) << 11) |
                      (((out[1] * 126ul + 255ul) / 510ul) << 5) |
                      ((out[2] * 62ul + 255ul) / 510ul));
    }
    return (WORD)(((out[0] >> 3) << 11) | ((out[1] >> 2) << 5) |
                  (out[2] >> 3));
}

static DWORD atibt_crc32(const BYTE *data, DWORD length)
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

/* Keys are "PairNN.<field>" so the report stays one flat INI section. */
static void pair_key(char *key, DWORD pair, const char *field)
{
    int index = 0;
    key[index++] = 'P';
    key[index++] = 'a';
    key[index++] = 'i';
    key[index++] = 'r';
    key[index++] = (char)('0' + pair / 10ul);
    key[index++] = (char)('0' + pair % 10ul);
    key[index++] = '.';
    while (*field != '\0') {
        key[index++] = *field++;
    }
    key[index] = '\0';
}

static void transcript(HANDLE file, const struct atibt_result *result)
{
    DWORD index;
    char offset_key[] = "WriteOffset00";
    char value_key[] = "WriteValue00";
    for (index = 0ul; index < ATIBT_TRANSCRIPT_COUNT; ++index) {
        offset_key[11] = (char)('0' + index / 10ul);
        offset_key[12] = (char)('0' + index % 10ul);
        value_key[10] = (char)('0' + index / 10ul);
        value_key[11] = (char)('0' + index % 10ul);
        hx(file, offset_key, result->write_offsets[index]);
        hx(file, value_key, result->write_values[index]);
    }
}

/* A pair is safe when every counter the single-pair gates required is zero
 * and the changed region matches what the observed pixel implies. */
static int atibt_pair_safe(const struct atibt_result *result)
{
    if ((result->status | ATIBT_INTERIOR_BIT) != ATIBT_PASS_STATUS ||
        result->state_count != ATIBT_STATE_COUNT ||
        result->setup_count != ATIBT_SETUP_COUNT ||
        result->interior_mismatch != 0ul ||
        result->exterior_mismatch != 0ul ||
        result->guard_mismatch != 0ul ||
        result->restore_mismatch != 0ul ||
        result->reserved != 0ul ||
        result->timeout_stage != 0ul) {
        return 0;
    }

    if (result->first_actual == ATIBT_DESTINATION) {
        return result->changed_pixels == 0ul;
    }

    return result->changed_pixels == ATIBT_COVERED_PIXELS &&
           result->min_x == 8ul && result->min_y == 6ul &&
           result->max_x == 38ul && result->max_y == 21ul;
}

void WINAPI V9xAtiMach64BlendTableEntry(void)
{
    static struct atibt_result result;
    static WORD observed[ATIBT_PAIRS];
    DWORD input[3];
    DWORD model_misses[ATIBT_MODELS];
    HANDLE device;
    HANDLE file;
    HANDLE bin;
    DWORD bytes;
    DWORD pair;
    DWORD model;
    DWORD completed = 0ul;
    DWORD matching_models = 0ul;
    DWORD first_model = 0xfffffffful;
    const struct atibt_factor *source_factor;
    const struct atibt_factor *dest_factor;
    WORD prediction;
    char key[32];
    char heading[] = "[AtiMach64Phase4BlendFactorTable]\r\n";
    int safe = 1;
    int pass;

    for (model = 0ul; model < ATIBT_MODELS; ++model) {
        model_misses[model] = 0ul;
    }

    CreateDirectoryA("C:\\V9XDIAG", 0);
    file = CreateFileA(ATIBT_TEXT_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    bin = CreateFileA(ATIBT_BIN_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (bin == INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        ExitProcess(5u);
    }

    WriteFile(file, heading, (DWORD)lstrlenA(heading), &bytes, 0);
    line(file, "Build", V9X_BUILD_ID);
    line(file, "Operation", "guarded-rgb565-add-factor-pair-table");
    hx(file, "SourceArgb", ATIBT_SOURCE_ARGB);
    hx(file, "Destination565", ATIBT_DESTINATION);
    dn(file, "PairCount", ATIBT_PAIRS);
    dn(file, "ModelCount", ATIBT_MODELS);

    device = CreateFileA("\\\\.\\ATIEN.VXD", 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        line(file, "Result", "REVIEW");
        line(file, "StopReason", "vxd-load");
        CloseHandle(bin);
        CloseHandle(file);
        ExitProcess(2u);
    }

    /* One draw per pair, each with its own backup, guard seed, drain and
     * restoration.  The first unsafe pair stops the table: the safety
     * contract permits no further writes after a guard, restore, timeout or
     * reset failure. */
    for (pair = 0ul; pair < ATIBT_PAIRS; ++pair) {
        source_factor = &atibt_sources[pair / ATIBT_FACTORS];
        dest_factor = &atibt_destinations[pair % ATIBT_FACTORS];
        input[0] = ATIBT_ADD_BASE |
                   (source_factor->field << ATIBT_SOURCE_SHIFT) |
                   (dest_factor->field << ATIBT_DEST_SHIFT);
        input[1] = ATIBT_SOURCE_ARGB;
        input[2] = atibt_predict(source_factor, dest_factor,
                                 &atibt_models[0]);

        pair_key(key, pair, "Source");
        line(file, key, source_factor->name);
        pair_key(key, pair, "Destination");
        line(file, key, dest_factor->name);
        pair_key(key, pair, "Scale3dCntl");
        hx(file, key, input[0]);

        bytes = 0ul;
        if (!DeviceIoControl(device, ATIBT_DIOC, input, sizeof(input),
                             &result, sizeof(result), &bytes, 0) ||
            bytes != sizeof(result) || result.magic != ATIBT_MAGIC) {
            pair_key(key, pair, "Stop");
            line(file, key, "dioc-refused");
            safe = 0;
            break;
        }

        WriteFile(bin, result.pixels, sizeof(result.pixels), &bytes, 0);
        observed[pair] = (WORD)result.first_actual;

        pair_key(key, pair, "Status");
        hx(file, key, result.status);
        pair_key(key, pair, "Observed565");
        hx(file, key, result.first_actual);
        for (model = 0ul; model < ATIBT_MODELS; ++model) {
            prediction = atibt_predict(source_factor, dest_factor,
                                       &atibt_models[model]);
            if (prediction != observed[pair]) {
                ++model_misses[model];
            }
        }
        pair_key(key, pair, "Model0Prediction565");
        hx(file, key, result.first_expected);
        pair_key(key, pair, "ProbeNonUniform");
        dn(file, key, result.interior_mismatch);
        pair_key(key, pair, "ExteriorMismatches");
        dn(file, key, result.exterior_mismatch);
        pair_key(key, pair, "GuardMismatches");
        dn(file, key, result.guard_mismatch);
        pair_key(key, pair, "RestoreMismatches");
        dn(file, key, result.restore_mismatch);
        pair_key(key, pair, "ChangedPixels");
        dn(file, key, result.changed_pixels);
        pair_key(key, pair, "RecoveryResetCount");
        dn(file, key, result.reserved);
        pair_key(key, pair, "TimeoutStage");
        dn(file, key, result.timeout_stage);
        pair_key(key, pair, "PixelCrc32");
        hx(file, key, atibt_crc32((const BYTE *)result.pixels,
                                  sizeof(result.pixels)));

        ++completed;
        if (!atibt_pair_safe(&result)) {
            pair_key(key, pair, "Stop");
            line(file, key, "unsafe-or-inconsistent");
            safe = 0;
            break;
        }
    }
    CloseHandle(device);
    CloseHandle(bin);

    dn(file, "CompletedPairs", completed);
    for (model = 0ul; model < ATIBT_MODELS; ++model) {
        char model_key[] = "Model0Misses";
        model_key[5] = (char)('0' + model);
        dn(file, model_key, model_misses[model]);
        if (completed == ATIBT_PAIRS && model_misses[model] == 0ul) {
            ++matching_models;
            if (first_model == 0xfffffffful) {
                first_model = model;
            }
        }
    }
    dn(file, "MatchingModels", matching_models);
    if (first_model != 0xfffffffful) {
        dn(file, "FirstMatchingModel", first_model);
    }

    /* The last completed pair's transcript documents the write order; only
     * SCALE_3D_CNTL and the vertex colours differ between pairs. */
    transcript(file, &result);

    pass = safe && completed == ATIBT_PAIRS && matching_models != 0ul;
    line(file, "Result", pass ? "PASS" : "REVIEW");
    CloseHandle(file);
    ExitProcess(pass ? 0u : 1u);
}
