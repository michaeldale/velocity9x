#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

/* Phase 4 item 10: replace, modulate and alpha-decal texture environments.
 * Four uniform 8x8 textures (RGB565, ARGB1555 with alpha set, ARGB1555 with
 * alpha clear, ARGB4444) are drawn under each environment twice: unblended,
 * which shows the colour the texture stage produces, and blended with the
 * proven SRCALPHA/INVSRCALPHA pair over 0xA55A, which exposes its alpha.
 *
 * Build a assumed OpenGL semantics and no model fitted.  Its safe REVIEW
 * measured these instead, which this build asserts:
 *   REPLACE     C = Ct               A = At, or Af for RGB565
 *   MODULATE    C = Ct*Cf            A = At, or Af for RGB565 (not At*Af)
 *   ALPHA_DECAL C = lerp(Cf, Ct, W)  A = Af; W = At, or Af for RGB565
 *
 * The rounding of these products is not settled; three different item 9
 * blend rules still fit.  Rounding belongs to the Phase 6 fallback gate, so
 * a scene passes within one 565 unit per channel of the /255 truncating
 * rule.  Each rejected semantic must miss by more than one unit in at least
 * one scene, so the tolerance cannot hide a wrong environment. */
#define ATITE_MAGIC 0x4b495441ul
#define ATITE_DIOC 27u
#define ATITE_TEXT_PATH "C:\\V9XDIAG\\ATI4TE.TXT"
#define ATITE_BIN_PATH "C:\\V9XDIAG\\ATI4TE.BIN"
#define ATITE_PASS_STATUS 0x0001fffful
#define ATITE_INTERIOR_BIT 0x00001000ul
#define ATITE_STATE_COUNT 19ul
#define ATITE_SETUP_COUNT 19ul
#define ATITE_VERTEX_ARGB 0x4d7e4925ul
#define ATITE_DESTINATION 0xa55au
#define ATITE_SCALE_UNBLENDED 0x00010081ul
#define ATITE_SCALE_BLENDED 0x002c0881ul
#define ATITE_TEX_MAP_AEN 0x40000000ul
#define ATITE_LIGHT_SHIFT 22
#define ATITE_TEXTURES 4
#define ATITE_ENVS 3
#define ATITE_SCENES 24
#define ATITE_TOLERANCE 1ul
#define ATITE_TRANSCRIPT_COUNT 38ul
#define ATITE_WIDTH 64ul
#define ATITE_HEIGHT 28ul
#define ATITE_COVERED_PIXELS 256ul

#define FMT_565 0
#define FMT_1555 1
#define FMT_4444 2

#define ENV_REPLACE 0
#define ENV_MODULATE 1
#define ENV_DECAL 2

/* The measured semantics, then the alternatives build a disproved. */
#define SEM_MEASURED 0
#define SEM_MODULATE_ALPHA_PRODUCT 1
#define SEM_DECAL_ALPHA_TEXEL 2
#define SEM_DECAL_565_REPLACE 3
#define SEM_565_ALPHA_OPAQUE 4
#define SEM_COUNT 5

struct atite_result {
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
    DWORD write_offsets[ATITE_TRANSCRIPT_COUNT];
    DWORD write_values[ATITE_TRANSCRIPT_COUNT];
    WORD pixels[ATITE_WIDTH * ATITE_HEIGHT];
};

typedef char atite_result_size_is_4016[
    sizeof(struct atite_result) == 4016 ? 1 : -1];

struct atite_texture {
    const char *name;
    int format;
    WORD texel;
    DWORD size_pitch;
    DWORD aen;
};

/* TEX_SIZE_PITCH words are the ones items 4 and 7 proved for 8x8 textures;
 * TEX_MAP_AEN takes texture alpha only for the formats that carry it. */
static const struct atite_texture atite_textures[ATITE_TEXTURES] = {
    { "RGB565", FMT_565, 0x13a3u, 0x40040444ul, 0ul },
    { "ARGB1555-A1", FMT_1555, 0xa105u, 0x30040444ul, ATITE_TEX_MAP_AEN },
    { "ARGB1555-A0", FMT_1555, 0x6984u, 0x30040444ul, ATITE_TEX_MAP_AEN },
    { "ARGB4444", FMT_4444, 0x5025u, 0xf0040444ul, ATITE_TEX_MAP_AEN }
};

static const char *const atite_env_names[ATITE_ENVS] = {
    "REPLACE", "MODULATE", "ALPHADECAL"
};

static const char *const atite_sem_names[SEM_COUNT] = {
    "Measured", "ModulateAlphaProduct", "DecalAlphaTexel",
    "Decal565Replace", "Rgb565AlphaOpaque"
};

/* Bit replication, as the item 4 and 7 texture probes observed. */
static DWORD atite_expand(DWORD value, DWORD bits)
{
    DWORD shifted = value << (8ul - bits);
    return shifted | (shifted >> bits);
}

static void atite_expand_565(DWORD pixel, DWORD *rgb)
{
    rgb[0] = atite_expand((pixel >> 11) & 31ul, 5ul);
    rgb[1] = atite_expand((pixel >> 5) & 63ul, 6ul);
    rgb[2] = atite_expand(pixel & 31ul, 5ul);
}

static WORD atite_pack(const DWORD *rgb)
{
    return (WORD)(((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) |
                  (rgb[2] >> 3));
}

/* x*w + y*(255-w), divided by 255 and truncated. */
static DWORD atite_lerp(DWORD x, DWORD y, DWORD w)
{
    return (x * w + y * (255ul - w)) / 255ul;
}

static WORD atite_predict(const struct atite_texture *texture, int env,
                          int blended, int semantics)
{
    DWORD vertex[3];
    DWORD tex[3];
    DWORD color[3];
    DWORD dest[3];
    DWORD vertex_alpha;
    DWORD tex_alpha;
    DWORD alpha;
    DWORD weight;
    DWORD channel;
    int has_alpha = texture->format != FMT_565;

    vertex_alpha = (ATITE_VERTEX_ARGB >> 24) & 255ul;
    vertex[0] = (ATITE_VERTEX_ARGB >> 16) & 255ul;
    vertex[1] = (ATITE_VERTEX_ARGB >> 8) & 255ul;
    vertex[2] = ATITE_VERTEX_ARGB & 255ul;

    if (texture->format == FMT_565) {
        atite_expand_565(texture->texel, tex);
        tex_alpha = vertex_alpha;
        if (semantics == SEM_565_ALPHA_OPAQUE) {
            tex_alpha = 255ul;
        }
    } else if (texture->format == FMT_1555) {
        tex[0] = atite_expand((texture->texel >> 10) & 31ul, 5ul);
        tex[1] = atite_expand((texture->texel >> 5) & 31ul, 5ul);
        tex[2] = atite_expand(texture->texel & 31ul, 5ul);
        tex_alpha = (texture->texel & 0x8000u) != 0u ? 255ul : 0ul;
    } else {
        tex[0] = atite_expand((texture->texel >> 8) & 15ul, 4ul);
        tex[1] = atite_expand((texture->texel >> 4) & 15ul, 4ul);
        tex[2] = atite_expand(texture->texel & 15ul, 4ul);
        tex_alpha = atite_expand((texture->texel >> 12) & 15ul, 4ul);
    }

    /* tex_alpha is the alpha the texture stage supplies: the texel's with
     * TEX_MAP_AEN set, otherwise the vertex alpha. */
    alpha = tex_alpha;
    weight = tex_alpha;
    for (channel = 0ul; channel < 3ul; ++channel) {
        if (env == ENV_REPLACE) {
            color[channel] = tex[channel];
        } else if (env == ENV_MODULATE) {
            color[channel] = tex[channel] * vertex[channel] / 255ul;
        } else if (!has_alpha && semantics == SEM_DECAL_565_REPLACE) {
            color[channel] = tex[channel];
        } else {
            color[channel] = atite_lerp(tex[channel], vertex[channel],
                                        weight);
        }
    }
    if (env == ENV_MODULATE && has_alpha &&
        semantics == SEM_MODULATE_ALPHA_PRODUCT) {
        alpha = tex_alpha * vertex_alpha / 255ul;
    }
    if (env == ENV_DECAL) {
        alpha = vertex_alpha;
        if (has_alpha && semantics == SEM_DECAL_ALPHA_TEXEL) {
            alpha = tex_alpha;
        }
    }

    if (!blended) {
        return atite_pack(color);
    }

    atite_expand_565(ATITE_DESTINATION, dest);
    for (channel = 0ul; channel < 3ul; ++channel) {
        color[channel] = atite_lerp(color[channel], dest[channel], alpha);
    }
    return atite_pack(color);
}

/* Largest per-channel difference, in 565 units. */
static DWORD atite_distance(WORD a, WORD b)
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

static DWORD atite_crc32(const BYTE *data, DWORD length)
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

/* Keys are "<prefix>NN.<field>" so the report stays one flat INI section. */
static void indexed_key(char *key, const char *prefix, DWORD index,
                        const char *field)
{
    int at = 0;
    while (*prefix != '\0') {
        key[at++] = *prefix++;
    }
    key[at++] = (char)('0' + index / 10ul);
    key[at++] = (char)('0' + index % 10ul);
    if (field != 0) {
        key[at++] = '.';
        while (*field != '\0') {
            key[at++] = *field++;
        }
    }
    key[at] = '\0';
}

static void transcript(HANDLE file, const struct atite_result *result)
{
    DWORD index;
    char offset_key[] = "WriteOffset00";
    char value_key[] = "WriteValue00";
    for (index = 0ul; index < ATITE_TRANSCRIPT_COUNT; ++index) {
        offset_key[11] = (char)('0' + index / 10ul);
        offset_key[12] = (char)('0' + index % 10ul);
        value_key[10] = (char)('0' + index / 10ul);
        value_key[11] = (char)('0' + index % 10ul);
        hx(file, offset_key, result->write_offsets[index]);
        hx(file, value_key, result->write_values[index]);
    }
}

static int atite_scene_safe(const struct atite_result *result)
{
    if ((result->status | ATITE_INTERIOR_BIT) != ATITE_PASS_STATUS ||
        result->state_count != ATITE_STATE_COUNT ||
        result->setup_count != ATITE_SETUP_COUNT ||
        result->interior_mismatch != 0ul ||
        result->exterior_mismatch != 0ul ||
        result->guard_mismatch != 0ul ||
        result->restore_mismatch != 0ul ||
        result->reserved != 0ul ||
        result->timeout_stage != 0ul) {
        return 0;
    }

    if (result->first_actual == ATITE_DESTINATION) {
        return result->changed_pixels == 0ul;
    }

    return result->changed_pixels == ATITE_COVERED_PIXELS &&
           result->min_x == 8ul && result->min_y == 6ul &&
           result->max_x == 38ul && result->max_y == 21ul;
}

void WINAPI V9xAtiMach64TexenvTableEntry(void)
{
    static struct atite_result result;
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
    int semantics;
    int rejected_visible = 1;
    const struct atite_texture *texture;
    int env;
    int blended;
    char key[40];
    char heading[] = "[AtiMach64Phase4TextureEnvironmentTable]\r\n";
    int safe = 1;
    int pass;

    CreateDirectoryA("C:\\V9XDIAG", 0);
    file = CreateFileA(ATITE_TEXT_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    bin = CreateFileA(ATITE_BIN_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (bin == INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        ExitProcess(5u);
    }

    WriteFile(file, heading, (DWORD)lstrlenA(heading), &bytes, 0);
    line(file, "Build", V9X_BUILD_ID);
    line(file, "Operation", "guarded-texture-environment-table");
    hx(file, "VertexArgb", ATITE_VERTEX_ARGB);
    hx(file, "Destination565", ATITE_DESTINATION);
    dn(file, "SceneCount", ATITE_SCENES);
    dn(file, "Tolerance565", ATITE_TOLERANCE);

    device = CreateFileA("\\\\.\\ATIEN.VXD", 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        line(file, "Result", "REVIEW");
        line(file, "StopReason", "vxd-load");
        CloseHandle(bin);
        CloseHandle(file);
        ExitProcess(2u);
    }

    /* Each scene uploads its texture into the same guarded page and pulses
     * TEX_CACHE_FLUSH in its state.  The first unsafe scene stops the
     * table. */
    for (scene = 0ul; scene < ATITE_SCENES; ++scene) {
        texture = &atite_textures[scene / (ATITE_ENVS * 2ul)];
        env = (int)((scene / 2ul) % ATITE_ENVS);
        blended = (int)(scene & 1ul);
        input[0] = (blended ? ATITE_SCALE_BLENDED : ATITE_SCALE_UNBLENDED) |
                   texture->aen | ((DWORD)env << ATITE_LIGHT_SHIFT);
        input[1] = texture->size_pitch;
        input[2] = texture->texel;
        input[3] = ATITE_VERTEX_ARGB;
        input[4] = atite_predict(texture, env, blended, SEM_MEASURED);

        indexed_key(key, "Scene", scene, "Texture");
        line(file, key, texture->name);
        indexed_key(key, "Scene", scene, "Environment");
        line(file, key, atite_env_names[env]);
        indexed_key(key, "Scene", scene, "Blended");
        dn(file, key, (DWORD)blended);
        indexed_key(key, "Scene", scene, "Scale3dCntl");
        hx(file, key, input[0]);

        bytes = 0ul;
        if (!DeviceIoControl(device, ATITE_DIOC, input, sizeof(input),
                             &result, sizeof(result), &bytes, 0) ||
            bytes != sizeof(result) || result.magic != ATITE_MAGIC) {
            indexed_key(key, "Scene", scene, "Stop");
            line(file, key, "dioc-refused");
            safe = 0;
            break;
        }

        WriteFile(bin, result.pixels, sizeof(result.pixels), &bytes, 0);
        observed = (WORD)result.first_actual;
        distance = atite_distance(observed, (WORD)input[4]);
        if (distance == 0ul) {
            ++exact;
        }
        if (distance <= ATITE_TOLERANCE) {
            ++within;
        }
        for (semantics = 1; semantics < SEM_COUNT; ++semantics) {
            DWORD miss = atite_distance(observed,
                atite_predict(texture, env, blended, semantics));
            if (miss > rejected_worst[semantics]) {
                rejected_worst[semantics] = miss;
            }
        }

        indexed_key(key, "Scene", scene, "Status");
        hx(file, key, result.status);
        indexed_key(key, "Scene", scene, "Observed565");
        hx(file, key, result.first_actual);
        indexed_key(key, "Scene", scene, "Predicted565");
        hx(file, key, result.first_expected);
        indexed_key(key, "Scene", scene, "Distance565");
        dn(file, key, distance);
        indexed_key(key, "Scene", scene, "ProbeNonUniform");
        dn(file, key, result.interior_mismatch);
        indexed_key(key, "Scene", scene, "ExteriorMismatches");
        dn(file, key, result.exterior_mismatch);
        indexed_key(key, "Scene", scene, "TextureOrGuardMismatches");
        dn(file, key, result.guard_mismatch);
        indexed_key(key, "Scene", scene, "RestoreMismatches");
        dn(file, key, result.restore_mismatch);
        indexed_key(key, "Scene", scene, "ChangedPixels");
        dn(file, key, result.changed_pixels);
        indexed_key(key, "Scene", scene, "RecoveryResetCount");
        dn(file, key, result.reserved);
        indexed_key(key, "Scene", scene, "TimeoutStage");
        dn(file, key, result.timeout_stage);
        indexed_key(key, "Scene", scene, "PixelCrc32");
        hx(file, key, atite_crc32((const BYTE *)result.pixels,
                                  sizeof(result.pixels)));

        ++completed;
        if (!atite_scene_safe(&result)) {
            indexed_key(key, "Scene", scene, "Stop");
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

    /* A rejected semantic that stays inside the tolerance everywhere would
     * make the tolerance, not the hardware, decide the verdict. */
    for (semantics = 1; semantics < SEM_COUNT; ++semantics) {
        char rejected_key[48];
        lstrcpyA(rejected_key, "RejectedWorst565.");
        lstrcatA(rejected_key, atite_sem_names[semantics]);
        dn(file, rejected_key, rejected_worst[semantics]);
        if (rejected_worst[semantics] <= ATITE_TOLERANCE) {
            rejected_visible = 0;
        }
    }

    /* The last completed scene's transcript documents the write order. */
    transcript(file, &result);

    pass = safe && completed == ATITE_SCENES && within == ATITE_SCENES &&
           rejected_visible;
    line(file, "Result", pass ? "PASS" : "REVIEW");
    CloseHandle(file);
    ExitProcess(pass ? 0u : 1u);
}
