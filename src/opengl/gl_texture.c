/*
 * Texture objects and images (gl_texture.h). Pure; storage through the
 * allocator the context was given. No C runtime, so copies and zeroing are
 * byte loops and no float-to-integer cast appears (the environment colour
 * goes through gl_state's rounding by way of an integer scale).
 */
#include "gl_texture.h"

#define V9X_GL_TEXTURE_GROW 32ul

static void v9x_gl_tex_zero(void *memory, v9x_u32 bytes)
{
    v9x_u8 *cursor = (v9x_u8 *)memory;

    while (bytes-- != 0ul) {
        *cursor++ = 0u;
    }
}

static void v9x_gl_texobj_defaults(V9X_GL_TEXOBJ *object, GLuint name)
{
    v9x_gl_tex_zero(object, sizeof(*object));
    object->name = name;
    object->in_use = 1;
    /* The initial state (table 6.x): mipmapped minification, linear
     * magnification, repeat on both axes. */
    object->min_filter = V9X_GL_NEAREST_MIPMAP_LINEAR;
    object->mag_filter = V9X_GL_LINEAR;
    object->wrap_s = V9X_GL_REPEAT;
    object->wrap_t = V9X_GL_REPEAT;
}

void v9x_gl_textures_init(V9X_GL_TEXTURES *textures, V9X_GL_ALLOC_FN alloc,
                          V9X_GL_FREE_FN release)
{
    unsigned int i;

    textures->alloc = alloc;
    textures->release = release;
    v9x_gl_texobj_defaults(&textures->default_object, 0u);
    textures->objects = 0;
    textures->capacity = 0ul;
    textures->next_name = 1u;
    textures->bound = 0u;
    textures->env_mode = V9X_GL_MODULATE;
    for (i = 0u; i < 4u; ++i) {
        textures->env_color[i] = 0.0f;
    }
    textures->unpack_alignment = 4;
    textures->unpack_row_length = 0;
    textures->unpack_skip_rows = 0;
    textures->unpack_skip_pixels = 0;
    for (i = 0u; i < 6u; ++i) {
        textures->pack[i] = i == 5u ? 4 : 0;
    }
}

static void v9x_gl_texobj_free_levels(V9X_GL_TEXTURES *textures,
                                      V9X_GL_TEXOBJ *object)
{
    unsigned int level;

    for (level = 0u; level < V9X_GL_TEXTURE_LEVELS; ++level) {
        if (object->levels[level].texels != 0) {
            textures->release(object->levels[level].texels);
            object->levels[level].texels = 0;
        }
        object->levels[level].width = 0ul;
        object->levels[level].height = 0ul;
    }
}

void v9x_gl_textures_release(V9X_GL_TEXTURES *textures)
{
    v9x_u32 i;

    v9x_gl_texobj_free_levels(textures, &textures->default_object);
    for (i = 0ul; i < textures->capacity; ++i) {
        if (textures->objects[i].in_use) {
            v9x_gl_texobj_free_levels(textures, &textures->objects[i]);
        }
    }
    if (textures->objects != 0) {
        textures->release(textures->objects);
    }
    textures->objects = 0;
    textures->capacity = 0ul;
    textures->bound = 0u;
}

static V9X_GL_TEXOBJ *v9x_gl_texobj_find(V9X_GL_TEXTURES *textures,
                                         GLuint name)
{
    v9x_u32 i;

    if (name == 0u) {
        return &textures->default_object;
    }
    for (i = 0ul; i < textures->capacity; ++i) {
        if (textures->objects[i].in_use && textures->objects[i].name == name) {
            return &textures->objects[i];
        }
    }
    return 0;
}

/* A free slot, the table grown when there is none. Null when the
 * allocator refuses. */
static V9X_GL_TEXOBJ *v9x_gl_texobj_slot(V9X_GL_TEXTURES *textures)
{
    V9X_GL_TEXOBJ *grown;
    v9x_u32 i;
    v9x_u32 capacity;

    for (i = 0ul; i < textures->capacity; ++i) {
        if (!textures->objects[i].in_use) {
            return &textures->objects[i];
        }
    }
    capacity = textures->capacity + V9X_GL_TEXTURE_GROW;
    grown = (V9X_GL_TEXOBJ *)textures->alloc(capacity *
                                             sizeof(V9X_GL_TEXOBJ));
    if (grown == 0) {
        return 0;
    }
    v9x_gl_tex_zero(grown, capacity * sizeof(V9X_GL_TEXOBJ));
    for (i = 0ul; i < textures->capacity * sizeof(V9X_GL_TEXOBJ); ++i) {
        ((v9x_u8 *)grown)[i] = ((const v9x_u8 *)textures->objects)[i];
    }
    if (textures->objects != 0) {
        textures->release(textures->objects);
    }
    textures->objects = grown;
    i = textures->capacity;
    textures->capacity = capacity;
    return &textures->objects[i];
}

static int v9x_gl_tex_allowed(V9X_GL_STATE *state)
{
    if (state->in_begin) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return 0;
    }
    return 1;
}

void v9x_gl_tex_gen(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                    GLsizei n, GLuint *names)
{
    GLsizei i;

    if (!v9x_gl_tex_allowed(state)) {
        return;
    }
    if (n < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < n; ++i) {
        while (textures->next_name == 0u ||
               v9x_gl_texobj_find(textures, textures->next_name) != 0) {
            ++textures->next_name;
        }
        names[i] = textures->next_name++;
    }
}

void v9x_gl_tex_delete(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                       GLsizei n, const GLuint *names)
{
    GLsizei i;

    if (!v9x_gl_tex_allowed(state)) {
        return;
    }
    if (n < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < n; ++i) {
        V9X_GL_TEXOBJ *object;

        /* Zero and names that are not textures are silently ignored. */
        if (names[i] == 0u) {
            continue;
        }
        object = v9x_gl_texobj_find(textures, names[i]);
        if (object == 0) {
            continue;
        }
        v9x_gl_texobj_free_levels(textures, object);
        object->in_use = 0;
        if (textures->bound == names[i]) {
            textures->bound = 0u;
        }
    }
}

GLboolean v9x_gl_tex_is(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                        GLuint name)
{
    if (!v9x_gl_tex_allowed(state)) {
        return 0;
    }
    return name != 0u && v9x_gl_texobj_find(textures, name) != 0 ? 1 : 0;
}

void v9x_gl_tex_bind(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                     GLenum target, GLuint name)
{
    V9X_GL_TEXOBJ *object;

    if (!v9x_gl_tex_allowed(state)) {
        return;
    }
    /* TEXTURE_1D is a legal target in GL 1.1 and is not implemented yet;
     * refusing it names that honestly rather than binding nothing. */
    if (target != V9X_GL_TEXTURE_2D) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    if (name != 0u && v9x_gl_texobj_find(textures, name) == 0) {
        object = v9x_gl_texobj_slot(textures);
        if (object == 0) {
            v9x_gl_state_error(state, V9X_GL_OUT_OF_MEMORY);
            return;
        }
        v9x_gl_texobj_defaults(object, name);
    }
    textures->bound = name;
}

static int v9x_gl_tex_filter_valid(GLint value, int mipmaps)
{
    if (value == (GLint)V9X_GL_NEAREST || value == (GLint)V9X_GL_LINEAR) {
        return 1;
    }
    return mipmaps && value >= (GLint)V9X_GL_NEAREST_MIPMAP_NEAREST &&
           value <= (GLint)V9X_GL_LINEAR_MIPMAP_LINEAR;
}

void v9x_gl_tex_parameter(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                          GLenum target, GLenum pname, GLint value)
{
    V9X_GL_TEXOBJ *object;

    if (!v9x_gl_tex_allowed(state)) {
        return;
    }
    if (target != V9X_GL_TEXTURE_2D) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    object = v9x_gl_texobj_find(textures, textures->bound);
    switch (pname) {
    case V9X_GL_TEXTURE_MIN_FILTER:
    case V9X_GL_TEXTURE_MAG_FILTER:
        if (!v9x_gl_tex_filter_valid(value,
                                     pname == V9X_GL_TEXTURE_MIN_FILTER)) {
            v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
            return;
        }
        if (pname == V9X_GL_TEXTURE_MIN_FILTER) {
            object->min_filter = (GLenum)value;
        } else {
            object->mag_filter = (GLenum)value;
        }
        return;
    case V9X_GL_TEXTURE_WRAP_S:
    case V9X_GL_TEXTURE_WRAP_T:
        if (value != (GLint)V9X_GL_CLAMP && value != (GLint)V9X_GL_REPEAT) {
            v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
            return;
        }
        if (pname == V9X_GL_TEXTURE_WRAP_S) {
            object->wrap_s = (GLenum)value;
        } else {
            object->wrap_t = (GLenum)value;
        }
        return;
    case 0x1004u:               /* TEXTURE_BORDER_COLOR: no borders yet */
    case 0x8066u:               /* TEXTURE_PRIORITY: residency is moot */
        return;
    default:
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
}

void v9x_gl_tex_env(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                    GLenum target, GLenum pname, const GLfloat *values)
{
    unsigned int i;

    if (!v9x_gl_tex_allowed(state)) {
        return;
    }
    if (target != V9X_GL_TEXTURE_ENV) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    if (pname == V9X_GL_TEXTURE_ENV_MODE) {
        /* The mode arrives as a float; compared against each enum rather
         * than cast, since a float-to-integer cast is a C runtime call the
         * ICD does not link. The four values are exact in a float. */
        static const GLenum modes[4] = {
            V9X_GL_MODULATE, V9X_GL_DECAL, V9X_GL_REPLACE, V9X_GL_BLEND_ENV
        };

        for (i = 0u; i < 4u; ++i) {
            if (values[0] == (GLfloat)modes[i]) {
                textures->env_mode = modes[i];
                return;
            }
        }
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    if (pname == V9X_GL_TEXTURE_ENV_COLOR) {
        for (i = 0u; i < 4u; ++i) {
            GLfloat value = values[i];

            textures->env_color[i] = value < 0.0f ? 0.0f
                                   : (value > 1.0f ? 1.0f : value);
        }
        return;
    }
    v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
}

void v9x_gl_pixel_store(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                        GLenum pname, GLint value)
{
    if (!v9x_gl_tex_allowed(state)) {
        return;
    }
    switch (pname) {
    case V9X_GL_UNPACK_ALIGNMENT:
    case 0x0D05u:               /* PACK_ALIGNMENT */
        if (value != 1 && value != 2 && value != 4 && value != 8) {
            v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
            return;
        }
        if (pname == V9X_GL_UNPACK_ALIGNMENT) {
            textures->unpack_alignment = value;
        } else {
            textures->pack[5] = value;
        }
        return;
    case 0x0CF2u:               /* UNPACK_ROW_LENGTH */
    case 0x0CF3u:               /* UNPACK_SKIP_ROWS */
    case 0x0CF4u:               /* UNPACK_SKIP_PIXELS */
    case 0x0D02u:               /* PACK_ROW_LENGTH */
    case 0x0D03u:               /* PACK_SKIP_ROWS */
    case 0x0D04u:               /* PACK_SKIP_PIXELS */
        if (value < 0) {
            v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
            return;
        }
        if (pname == 0x0CF2u) {
            textures->unpack_row_length = value;
        } else if (pname == 0x0CF3u) {
            textures->unpack_skip_rows = value;
        } else if (pname == 0x0CF4u) {
            textures->unpack_skip_pixels = value;
        } else {
            textures->pack[pname - 0x0D00u] = value;
        }
        return;
    case 0x0CF0u:               /* UNPACK_SWAP_BYTES: no multi-byte types */
    case 0x0CF1u:               /* UNPACK_LSB_FIRST: bitmaps only */
    case 0x0D00u:
    case 0x0D01u:
        return;
    default:
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
}

/* The base internal format an internalformat names, or zero (3.8.1, table
 * 3.15 and 3.16: 1..4 and the base and sized symbolic formats). */
static GLenum v9x_gl_tex_base_format(GLint internal_format)
{
    GLint f = internal_format;

    if (f == 1 || f == (GLint)V9X_GL_LUMINANCE ||
        (f >= 0x803F && f <= 0x8042)) {
        return V9X_GL_LUMINANCE;
    }
    if (f == 2 || f == (GLint)V9X_GL_LUMINANCE_ALPHA ||
        (f >= 0x8043 && f <= 0x8048)) {
        return V9X_GL_LUMINANCE_ALPHA;
    }
    if (f == 3 || f == (GLint)V9X_GL_RGB || f == 0x2A10 ||
        (f >= 0x804F && f <= 0x8054)) {
        return V9X_GL_RGB;
    }
    if (f == 4 || f == (GLint)V9X_GL_RGBA || (f >= 0x8055 && f <= 0x805B)) {
        return V9X_GL_RGBA;
    }
    if (f == (GLint)V9X_GL_ALPHA || (f >= 0x803B && f <= 0x803E)) {
        return V9X_GL_ALPHA;
    }
    if (f == (GLint)V9X_GL_INTENSITY || (f >= 0x804A && f <= 0x804D)) {
        return V9X_GL_INTENSITY;
    }
    return 0u;
}

/* Components per pixel of a transfer format, or zero. */
static unsigned int v9x_gl_tex_components(GLenum format)
{
    switch (format) {
    case V9X_GL_ALPHA:
    case V9X_GL_LUMINANCE:
    case 0x1903u:           /* RED */
    case 0x1904u:           /* GREEN */
    case 0x1905u:           /* BLUE */
        return 1u;
    case V9X_GL_LUMINANCE_ALPHA:
        return 2u;
    case V9X_GL_RGB:
        return 3u;
    case V9X_GL_RGBA:
        return 4u;
    default:
        return 0u;
    }
}

static int v9x_gl_tex_power_of_two(GLsizei value)
{
    return value > 0 && (value & (value - 1)) == 0 &&
           value <= (GLsizei)V9X_GL_TEXTURE_SIZE_MAX;
}

/* A byte as an n-bit field, rounded (c * (2^n - 1) + 127) / 255. */
static v9x_u32 v9x_gl_tex_field(v9x_u32 value, v9x_u32 bits)
{
    v9x_u32 maximum = (1ul << bits) - 1ul;

    return (value * maximum + 127ul) / 255ul;
}

/*
 * One source pixel as the object's 16-bit texel: first to R, G, B, A as
 * 3.6.4's conversion to RGBA does (L to R=G=B, a missing alpha to 1, a
 * missing colour to 0), then to the base internal format's components
 * (table 3.15), then packed into the storage layout.
 */
static v9x_u16 v9x_gl_tex_texel(const v9x_u8 *source, GLenum format,
                                GLenum base)
{
    v9x_u32 r = 0ul;
    v9x_u32 g = 0ul;
    v9x_u32 b = 0ul;
    v9x_u32 a = 255ul;
    v9x_u32 l;

    switch (format) {
    case V9X_GL_ALPHA:
        a = source[0];
        break;
    case V9X_GL_LUMINANCE:
        r = g = b = source[0];
        break;
    case V9X_GL_LUMINANCE_ALPHA:
        r = g = b = source[0];
        a = source[1];
        break;
    case 0x1903u:
        r = source[0];
        break;
    case 0x1904u:
        g = source[0];
        break;
    case 0x1905u:
        b = source[0];
        break;
    case V9X_GL_RGB:
        r = source[0];
        g = source[1];
        b = source[2];
        break;
    default:
        r = source[0];
        g = source[1];
        b = source[2];
        a = source[3];
        break;
    }
    l = r;      /* LUMINANCE and INTENSITY take R (table 3.15) */
    switch (base) {
    case V9X_GL_RGB:
        return (v9x_u16)((v9x_gl_tex_field(r, 5ul) << 11) |
                         (v9x_gl_tex_field(g, 6ul) << 5) |
                         v9x_gl_tex_field(b, 5ul));
    case V9X_GL_LUMINANCE:
        return (v9x_u16)((v9x_gl_tex_field(l, 5ul) << 11) |
                         (v9x_gl_tex_field(l, 6ul) << 5) |
                         v9x_gl_tex_field(l, 5ul));
    case V9X_GL_ALPHA:
        /* White colour: the environment's colour-from-fragment cases use
         * MODULATE, and a white texel makes that the fragment's colour. */
        return (v9x_u16)((v9x_gl_tex_field(a, 4ul) << 12) | 0x0FFFul);
    case V9X_GL_LUMINANCE_ALPHA:
        return (v9x_u16)((v9x_gl_tex_field(a, 4ul) << 12) |
                         (v9x_gl_tex_field(l, 4ul) * 0x111ul));
    case V9X_GL_INTENSITY:
        return (v9x_u16)((v9x_gl_tex_field(l, 4ul) << 12) |
                         (v9x_gl_tex_field(l, 4ul) * 0x111ul));
    default:                /* RGBA */
        return (v9x_u16)((v9x_gl_tex_field(a, 4ul) << 12) |
                         (v9x_gl_tex_field(r, 4ul) << 8) |
                         (v9x_gl_tex_field(g, 4ul) << 4) |
                         v9x_gl_tex_field(b, 4ul));
    }
}

/* The source byte offset of pixel (x, y) under the unpack parameters
 * (3.6.3: row length, skips, alignment). */
static v9x_u32 v9x_gl_tex_source_offset(const V9X_GL_TEXTURES *textures,
                                        GLsizei width, unsigned int components,
                                        GLsizei x, GLsizei y)
{
    v9x_u32 row_pixels = textures->unpack_row_length > 0
        ? (v9x_u32)textures->unpack_row_length : (v9x_u32)width;
    v9x_u32 alignment = (v9x_u32)textures->unpack_alignment;
    v9x_u32 row_bytes = row_pixels * components;

    row_bytes = (row_bytes + alignment - 1ul) / alignment * alignment;
    return ((v9x_u32)textures->unpack_skip_rows + (v9x_u32)y) * row_bytes +
           ((v9x_u32)textures->unpack_skip_pixels + (v9x_u32)x) * components;
}

static int v9x_gl_tex_format_checks(V9X_GL_STATE *state, GLenum target,
                                    GLenum format, GLenum type)
{
    if (target != V9X_GL_TEXTURE_2D || v9x_gl_tex_components(format) == 0u) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return 0;
    }
    /* UNSIGNED_BYTE only in this slice: the other transfer types (3.6.4)
     * are legal and not yet converted, and are refused rather than read
     * wrongly. */
    if (type != V9X_GL_UNSIGNED_BYTE) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return 0;
    }
    return 1;
}

void v9x_gl_tex_image_2d(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                         GLenum target, GLint level, GLint internal_format,
                         GLsizei width, GLsizei height, GLint border,
                         GLenum format, GLenum type, const void *pixels)
{
    V9X_GL_TEXOBJ *object;
    V9X_GL_TEXLEVEL *slot;
    GLenum base;
    v9x_u16 *texels;
    unsigned int components;
    GLsizei x;
    GLsizei y;

    if (!v9x_gl_tex_allowed(state) ||
        !v9x_gl_tex_format_checks(state, target, format, type)) {
        return;
    }
    base = v9x_gl_tex_base_format(internal_format);
    /* Borders (border 1) are legal in GL 1.1 and not implemented; refused
     * as a value the implementation cannot take. */
    if (base == 0u || level < 0 || level >= (GLint)V9X_GL_TEXTURE_LEVELS ||
        border != 0 || !v9x_gl_tex_power_of_two(width) ||
        !v9x_gl_tex_power_of_two(height)) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    object = v9x_gl_texobj_find(textures, textures->bound);
    texels = (v9x_u16 *)textures->alloc((v9x_u32)width * (v9x_u32)height *
                                        2ul);
    if (texels == 0) {
        v9x_gl_state_error(state, V9X_GL_OUT_OF_MEMORY);
        return;
    }
    components = v9x_gl_tex_components(format);
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            v9x_u16 value = 0u;

            if (pixels != 0) {
                value = v9x_gl_tex_texel(
                    (const v9x_u8 *)pixels +
                        v9x_gl_tex_source_offset(textures, width, components,
                                                 x, y),
                    format, base);
            }
            texels[(v9x_u32)y * (v9x_u32)width + (v9x_u32)x] = value;
        }
    }
    slot = &object->levels[level];
    if (slot->texels != 0) {
        textures->release(slot->texels);
    }
    slot->texels = texels;
    slot->width = (v9x_u32)width;
    slot->height = (v9x_u32)height;
    if (level == 0) {
        object->base_format = base;
        object->storage_format =
            base == V9X_GL_RGB || base == V9X_GL_LUMINANCE
                ? V9X_R3D_ABI_FORMAT_RGB565 : V9X_R3D_ABI_FORMAT_ARGB4444;
    }
}

void v9x_gl_tex_sub_image_2d(V9X_GL_STATE *state, V9X_GL_TEXTURES *textures,
                             GLenum target, GLint level, GLint xoffset,
                             GLint yoffset, GLsizei width, GLsizei height,
                             GLenum format, GLenum type, const void *pixels)
{
    V9X_GL_TEXOBJ *object;
    V9X_GL_TEXLEVEL *slot;
    unsigned int components;
    GLsizei x;
    GLsizei y;

    if (!v9x_gl_tex_allowed(state) ||
        !v9x_gl_tex_format_checks(state, target, format, type)) {
        return;
    }
    if (level < 0 || level >= (GLint)V9X_GL_TEXTURE_LEVELS) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    object = v9x_gl_texobj_find(textures, textures->bound);
    slot = &object->levels[level];
    if (slot->texels == 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return;
    }
    if (xoffset < 0 || yoffset < 0 || width < 0 || height < 0 ||
        (v9x_u32)xoffset + (v9x_u32)width > slot->width ||
        (v9x_u32)yoffset + (v9x_u32)height > slot->height) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    components = v9x_gl_tex_components(format);
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            slot->texels[((v9x_u32)yoffset + (v9x_u32)y) * slot->width +
                         (v9x_u32)xoffset + (v9x_u32)x] =
                v9x_gl_tex_texel(
                    (const v9x_u8 *)pixels +
                        v9x_gl_tex_source_offset(textures, width, components,
                                                 x, y),
                    format, object->base_format);
        }
    }
}

static int v9x_gl_tex_uses_mipmaps(GLenum min_filter)
{
    return min_filter >= V9X_GL_NEAREST_MIPMAP_NEAREST &&
           min_filter <= V9X_GL_LINEAR_MIPMAP_LINEAR;
}

/* How many levels the object samples when complete, or zero when it is
 * not (3.8.9): level 0 present, and with a mipmap filter every level down
 * to 1x1, each extent max(1, half the one before). */
static v9x_u32 v9x_gl_tex_complete(const V9X_GL_TEXOBJ *object)
{
    v9x_u32 width = object->levels[0].width;
    v9x_u32 height = object->levels[0].height;
    v9x_u32 level;

    if (object->levels[0].texels == 0) {
        return 0ul;
    }
    if (!v9x_gl_tex_uses_mipmaps(object->min_filter)) {
        return 1ul;
    }
    for (level = 1ul; width > 1ul || height > 1ul; ++level) {
        width = width > 1ul ? width / 2ul : 1ul;
        height = height > 1ul ? height / 2ul : 1ul;
        if (level >= V9X_GL_TEXTURE_LEVELS ||
            object->levels[level].texels == 0 ||
            object->levels[level].width != width ||
            object->levels[level].height != height) {
            return 0ul;
        }
    }
    return level;
}

/* A 0..1 value as a byte, rounded, without a float-to-integer cast: the
 * nearest of the 256 steps by comparison. */
static v9x_u32 v9x_gl_tex_byte(GLfloat value)
{
    v9x_u32 low = 0ul;
    v9x_u32 high = 255ul;

    while (low < high) {
        v9x_u32 mid = (low + high + 1ul) / 2ul;

        if ((GLfloat)mid - 0.5f <= value * 255.0f) {
            low = mid;
        } else {
            high = mid - 1ul;
        }
    }
    return low;
}

void v9x_gl_tex_describe(const V9X_GL_STATE *state,
                         V9X_GL_TEXTURES *textures,
                         V9X_R3D_ABI_TEXTURE *out,
                         V9X_R3D_ABI_LEVEL *levels)
{
    const V9X_GL_TEXOBJ *object;
    GLenum base;
    GLenum mode = textures->env_mode;
    int has_alpha;
    v9x_u32 count;
    v9x_u32 level;

    v9x_gl_tex_zero(out, sizeof(*out));
    out->storage = V9X_R3D_ABI_TEXTURE_NONE;
    if (!v9x_gl_state_cap(state, V9X_GL_TEXTURE_2D)) {
        return;
    }
    object = v9x_gl_texobj_find(textures, textures->bound);
    count = object != 0 ? v9x_gl_tex_complete(object) : 0ul;
    if (count == 0ul) {
        return;
    }
    for (level = 0ul; level < count; ++level) {
        const V9X_GL_TEXLEVEL *source = &object->levels[level];

        levels[level].pixels = source->texels;
        levels[level].width = source->width;
        levels[level].height = source->height;
        levels[level].pitch = source->width * 2ul;
        levels[level].bytes = source->width * source->height * 2ul;
    }
    out->storage = V9X_R3D_ABI_TEXTURE_CPU;
    out->format = object->storage_format;
    out->levels = levels;
    out->level_count = count;
    out->mag_filter = object->mag_filter == V9X_GL_LINEAR
        ? V9X_R3D_ABI_FILTER_LINEAR : V9X_R3D_ABI_FILTER_NEAREST;
    out->min_filter = (object->min_filter == V9X_GL_LINEAR ||
                       object->min_filter == V9X_GL_LINEAR_MIPMAP_NEAREST ||
                       object->min_filter == V9X_GL_LINEAR_MIPMAP_LINEAR)
        ? V9X_R3D_ABI_FILTER_LINEAR : V9X_R3D_ABI_FILTER_NEAREST;
    if (!v9x_gl_tex_uses_mipmaps(object->min_filter)) {
        out->mip = V9X_R3D_ABI_MIP_NONE;
    } else if (object->min_filter == V9X_GL_NEAREST_MIPMAP_NEAREST ||
               object->min_filter == V9X_GL_LINEAR_MIPMAP_NEAREST) {
        out->mip = V9X_R3D_ABI_MIP_POINT;
    } else {
        out->mip = V9X_R3D_ABI_MIP_LINEAR;
    }
    /* The rasterizer has one address mode for both axes; CLAMP on S
     * decides it. Different modes per axis are not drawn exactly yet. */
    out->address = object->wrap_s == V9X_GL_CLAMP
        ? V9X_R3D_ABI_ADDRESS_CLAMP : V9X_R3D_ABI_ADDRESS_WRAP;
    out->env_color = (v9x_gl_tex_byte(textures->env_color[0]) << 16) |
                     (v9x_gl_tex_byte(textures->env_color[1]) << 8) |
                     v9x_gl_tex_byte(textures->env_color[2]);

    /* Table 3.18, per base format, as the interface's colour and alpha ops.
     * ALPHA textures are stored white, so colour-from-the-fragment is
     * MODULATE for them. */
    base = object->base_format;
    has_alpha = base != V9X_GL_RGB && base != V9X_GL_LUMINANCE;
    if (mode == V9X_GL_REPLACE) {
        out->color_op = base == V9X_GL_ALPHA ? V9X_R3D_ABI_COLOROP_MODULATE
                                             : V9X_R3D_ABI_COLOROP_REPLACE;
        out->alpha_op = has_alpha ? V9X_R3D_ABI_ALPHAOP_REPLACE
                                  : V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    } else if (mode == V9X_GL_DECAL) {
        /* Undefined for ALPHA, LUMINANCE_ALPHA and INTENSITY (3.8.9); the
         * alpha-bearing ones take the RGBA equation. */
        out->color_op = has_alpha ? V9X_R3D_ABI_COLOROP_DECALALPHA
                                  : V9X_R3D_ABI_COLOROP_REPLACE;
        out->alpha_op = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    } else if (mode == V9X_GL_BLEND_ENV) {
        /* ALPHA keeps the fragment's colour (white texels, MODULATE).
         * INTENSITY's alpha blend toward the environment alpha is not
         * expressible and takes the product. */
        out->color_op = base == V9X_GL_ALPHA ? V9X_R3D_ABI_COLOROP_MODULATE
                                             : V9X_R3D_ABI_COLOROP_BLEND;
        out->alpha_op = has_alpha ? V9X_R3D_ABI_ALPHAOP_MODULATE
                                  : V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    } else {
        out->color_op = V9X_R3D_ABI_COLOROP_MODULATE;
        out->alpha_op = has_alpha ? V9X_R3D_ABI_ALPHAOP_MODULATE
                                  : V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    }
}
