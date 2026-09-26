/*
 * Vertex arrays (OpenGL 1.1 section 2.8). glArrayElement is defined by the
 * specification as the sequence of immediate-mode calls it replaces -
 * edge flag, texture coordinate, colour, normal, then vertex - and that is
 * how it is implemented: the fetched values go through the same pipeline
 * entries glTexCoord, glColor and glVertex use, so an array draw and the
 * equivalent glBegin/glEnd cannot differ.
 */
#include "gl_varray.h"

#define GL_VARRAY_INDEX_UNSIGNED_BYTE   V9X_GL_UNSIGNED_BYTE

/* glGetPointerv names (table 6.6, 6.29, 6.30). */
#define GL_VARRAY_VERTEX_ARRAY_POINTER  0x808Eu
#define GL_VARRAY_EDGE_FLAG_POINTER     0x8093u
#define GL_VARRAY_FEEDBACK_POINTER      0x0DF0u
#define GL_VARRAY_SELECTION_POINTER     0x0DF3u

/* glInterleavedArrays' formats, table 2.5, in enum order from V2F. */
#define GL_VARRAY_V2F                   0x2A20u
#define GL_VARRAY_FORMAT_COUNT          14u

/* The 2^32 - 1 and 2^16 - 1 and 2^8 - 1 divisors of table 2.6. */
#define GL_VARRAY_UINT_MAX_F            4294967295.0
#define GL_VARRAY_USHORT_MAX_F          65535.0
#define GL_VARRAY_UBYTE_MAX_F           255.0

static const GLenum gl_varray_caps[V9X_GL_ARRAY_COUNT] = {
    V9X_GL_VERTEX_ARRAY, V9X_GL_NORMAL_ARRAY, V9X_GL_COLOR_ARRAY,
    V9X_GL_INDEX_ARRAY, V9X_GL_TEXTURE_COORD_ARRAY, V9X_GL_EDGE_FLAG_ARRAY
};

/*
 * Table 2.5, one row a format: whether it has texture coordinates, a
 * colour and a normal; their sizes; the colour's type (ubyte or float);
 * the byte offsets of colour, normal and vertex; and the record size.
 * f is sizeof(GLfloat) = 4 and c, four ubytes rounded up to a float, 4.
 */
typedef struct gl_varray_layout {
    unsigned char texture;
    unsigned char colour;
    unsigned char normal;
    unsigned char texture_size;
    unsigned char colour_size;
    unsigned char vertex_size;
    unsigned char colour_ubyte;
    unsigned char colour_offset;
    unsigned char normal_offset;
    unsigned char vertex_offset;
    unsigned char record;
} GL_VARRAY_LAYOUT;

static const GL_VARRAY_LAYOUT gl_varray_layouts[GL_VARRAY_FORMAT_COUNT] = {
    /*  t  c  n  st sc sv ub   pc  pn  pv   s */
    {   0, 0, 0, 0, 0, 2, 0,   0,  0,  0,   8 },   /* V2F */
    {   0, 0, 0, 0, 0, 3, 0,   0,  0,  0,  12 },   /* V3F */
    {   0, 1, 0, 0, 4, 2, 1,   0,  0,  4,  12 },   /* C4UB_V2F */
    {   0, 1, 0, 0, 4, 3, 1,   0,  0,  4,  16 },   /* C4UB_V3F */
    {   0, 1, 0, 0, 3, 3, 0,   0,  0, 12,  24 },   /* C3F_V3F */
    {   0, 0, 1, 0, 0, 3, 0,   0,  0, 12,  24 },   /* N3F_V3F */
    {   0, 1, 1, 0, 4, 3, 0,   0, 16, 28,  40 },   /* C4F_N3F_V3F */
    {   1, 0, 0, 2, 0, 3, 0,   0,  0,  8,  20 },   /* T2F_V3F */
    {   1, 0, 0, 4, 0, 4, 0,   0,  0, 16,  32 },   /* T4F_V4F */
    {   1, 1, 0, 2, 4, 3, 1,   8,  0, 12,  24 },   /* T2F_C4UB_V3F */
    {   1, 1, 0, 2, 3, 3, 0,   8,  0, 20,  32 },   /* T2F_C3F_V3F */
    {   1, 0, 1, 2, 0, 3, 0,   0,  8, 20,  32 },   /* T2F_N3F_V3F */
    {   1, 1, 1, 2, 4, 3, 0,   8, 24, 36,  48 },   /* T2F_C4F_N3F_V3F */
    {   1, 1, 1, 4, 4, 4, 0,  16, 32, 44,  60 }    /* T4F_C4F_N3F_V4F */
};

void v9x_gl_arrays_init(V9X_GL_ARRAYS *arrays)
{
    unsigned int i;

    for (i = 0u; i < V9X_GL_ARRAY_COUNT; ++i) {
        arrays->array[i].enabled = 0;
        arrays->array[i].size = 4;
        arrays->array[i].type = V9X_GL_FLOAT;
        arrays->array[i].stride = 0;
        arrays->array[i].pointer = 0;
    }
    arrays->array[V9X_GL_ARRAY_NORMAL].size = 3;
    arrays->array[V9X_GL_ARRAY_INDEX].size = 1;
    arrays->array[V9X_GL_ARRAY_EDGE_FLAG].size = 1;
    arrays->array[V9X_GL_ARRAY_EDGE_FLAG].type = V9X_GL_UNSIGNED_BYTE;
}

static int gl_varray_cap_index(GLenum cap)
{
    unsigned int i;

    for (i = 0u; i < V9X_GL_ARRAY_COUNT; ++i) {
        if (gl_varray_caps[i] == cap) {
            return (int)i;
        }
    }
    return -1;
}

void v9x_gl_arrays_client_state(V9X_GL_STATE *state, V9X_GL_ARRAYS *arrays,
                                GLenum cap, int enable)
{
    int index = gl_varray_cap_index(cap);

    if (index < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    arrays->array[index].enabled = enable ? 1 : 0;
}

int v9x_gl_arrays_is_enabled(const V9X_GL_ARRAYS *arrays, GLenum cap,
                             GLboolean *enabled)
{
    int index = gl_varray_cap_index(cap);

    if (index < 0) {
        return 0;
    }
    *enabled = arrays->array[index].enabled ? 1u : 0u;
    return 1;
}

/* Bytes one component of `type` occupies, for a tightly packed stride. */
static GLsizei gl_varray_type_bytes(GLenum type)
{
    if (type == V9X_GL_BYTE || type == V9X_GL_UNSIGNED_BYTE) {
        return 1;
    }
    if (type == V9X_GL_SHORT || type == V9X_GL_UNSIGNED_SHORT) {
        return 2;
    }
    if (type == V9X_GL_DOUBLE) {
        return 8;
    }
    return 4;
}

/* 2.8's accepted types per array (table 2.4). */
static int gl_varray_type_allowed(unsigned int which, GLenum type)
{
    if (type == V9X_GL_SHORT || type == V9X_GL_INT ||
        type == V9X_GL_FLOAT || type == V9X_GL_DOUBLE) {
        return 1;
    }
    if (which == V9X_GL_ARRAY_NORMAL) {
        return type == V9X_GL_BYTE;
    }
    if (which == V9X_GL_ARRAY_INDEX) {
        return type == GL_VARRAY_INDEX_UNSIGNED_BYTE;
    }
    if (which == V9X_GL_ARRAY_COLOR) {
        return type == V9X_GL_BYTE || type == V9X_GL_UNSIGNED_BYTE ||
               type == V9X_GL_UNSIGNED_SHORT || type == V9X_GL_UNSIGNED_INT;
    }
    return 0;
}

static int gl_varray_size_allowed(unsigned int which, GLint size)
{
    if (which == V9X_GL_ARRAY_VERTEX) {
        return size >= 2 && size <= 4;
    }
    if (which == V9X_GL_ARRAY_COLOR) {
        return size == 3 || size == 4;
    }
    if (which == V9X_GL_ARRAY_TEXCOORD) {
        return size >= 1 && size <= 4;
    }
    if (which == V9X_GL_ARRAY_NORMAL) {
        return size == 3;
    }
    return size == 1;
}

void v9x_gl_arrays_pointer(V9X_GL_STATE *state, V9X_GL_ARRAYS *arrays,
                           unsigned int which, GLint size, GLenum type,
                           GLsizei stride, const void *pointer)
{
    V9X_GL_ARRAY *array;

    if (which >= V9X_GL_ARRAY_COUNT || which == V9X_GL_ARRAY_EDGE_FLAG) {
        return;
    }
    if (!gl_varray_size_allowed(which, size) || stride < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    if (!gl_varray_type_allowed(which, type)) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    array = &arrays->array[which];
    array->size = size;
    array->type = type;
    array->stride = stride;
    array->pointer = pointer;
}

void v9x_gl_arrays_edge_flag_pointer(V9X_GL_STATE *state,
                                     V9X_GL_ARRAYS *arrays, GLsizei stride,
                                     const void *pointer)
{
    if (stride < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    arrays->array[V9X_GL_ARRAY_EDGE_FLAG].stride = stride;
    arrays->array[V9X_GL_ARRAY_EDGE_FLAG].pointer = pointer;
}

void v9x_gl_arrays_get_pointer(V9X_GL_STATE *state,
                               const V9X_GL_ARRAYS *arrays, GLenum pname,
                               void **out)
{
    if (pname >= GL_VARRAY_VERTEX_ARRAY_POINTER &&
        pname <= GL_VARRAY_EDGE_FLAG_POINTER) {
        /* The pointer names run in the array order V9X_GL_ARRAY_*. */
        *out = (void *)arrays->array[pname -
                                     GL_VARRAY_VERTEX_ARRAY_POINTER].pointer;
        return;
    }
    if (pname == GL_VARRAY_FEEDBACK_POINTER ||
        pname == GL_VARRAY_SELECTION_POINTER) {
        *out = 0;
        return;
    }
    v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
}

/* An unsigned 32-bit value as a double without the runtime's unsigned
 * conversion helper, which the ICD does not link: halve, convert as
 * signed, double, and add the low bit back. */
static double gl_varray_unsigned(v9x_u32 value)
{
    return (double)(long)(value >> 1) * 2.0 + (double)(long)(value & 1ul);
}

/*
 * Component `index` of the element at `base` as a float. With `normalize`
 * (colours and normals), integers map by table 2.6: unsigned c to
 * c / (2^b - 1), signed c to (2c + 1) / (2^b - 1). Otherwise they convert
 * directly.
 */
static GLfloat gl_varray_component(const void *base, GLenum type,
                                   unsigned int index, int normalize)
{
    double value;

    if (type == V9X_GL_FLOAT) {
        return ((const GLfloat *)base)[index];
    }
    if (type == V9X_GL_DOUBLE) {
        return (GLfloat)((const GLdouble *)base)[index];
    }
    if (type == V9X_GL_UNSIGNED_BYTE) {
        value = (double)((const GLubyte *)base)[index];
        return (GLfloat)(normalize ? value / GL_VARRAY_UBYTE_MAX_F : value);
    }
    if (type == V9X_GL_BYTE) {
        value = (double)((const GLbyte *)base)[index];
        return (GLfloat)(normalize
                         ? (2.0 * value + 1.0) / GL_VARRAY_UBYTE_MAX_F
                         : value);
    }
    if (type == V9X_GL_UNSIGNED_SHORT) {
        value = (double)((const GLushort *)base)[index];
        return (GLfloat)(normalize ? value / GL_VARRAY_USHORT_MAX_F : value);
    }
    if (type == V9X_GL_SHORT) {
        value = (double)((const GLshort *)base)[index];
        return (GLfloat)(normalize
                         ? (2.0 * value + 1.0) / GL_VARRAY_USHORT_MAX_F
                         : value);
    }
    if (type == V9X_GL_UNSIGNED_INT) {
        value = gl_varray_unsigned(((const v9x_u32 *)base)[index]);
        return (GLfloat)(normalize ? value / GL_VARRAY_UINT_MAX_F : value);
    }
    value = (double)((const GLint *)base)[index];
    return (GLfloat)(normalize ? (2.0 * value + 1.0) / GL_VARRAY_UINT_MAX_F
                               : value);
}

/* Element `index` of an array: pointer + index * stride, where stride 0
 * means the elements are tightly packed. The index is the caller's; a
 * value outside the client's array faults in the client's thread, as it
 * would in any GL. */
static const void *gl_varray_address(const V9X_GL_ARRAY *array, GLint index)
{
    long stride = (long)array->stride;

    if (stride == 0l) {
        stride = (long)array->size * (long)gl_varray_type_bytes(array->type);
    }
    return (const v9x_u8 *)array->pointer + (long)index * stride;
}

/* Up to four components into out[], the rest left as the caller set
 * them (the defaults 0, 0, 0, 1). */
static void gl_varray_fetch(const V9X_GL_ARRAY *array, GLint index,
                            int normalize, GLfloat *out)
{
    const void *base = gl_varray_address(array, index);
    unsigned int i;

    for (i = 0u; i < (unsigned int)array->size; ++i) {
        out[i] = gl_varray_component(base, array->type, i, normalize);
    }
}

void v9x_gl_arrays_element(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                           const V9X_GL_ARRAYS *arrays, GLint index)
{
    const V9X_GL_ARRAY *array;
    GLfloat v[4];

    array = &arrays->array[V9X_GL_ARRAY_TEXCOORD];
    if (array->enabled) {
        v[0] = 0.0f;
        v[1] = 0.0f;
        v[2] = 0.0f;
        v[3] = 1.0f;
        gl_varray_fetch(array, index, 0, v);
        v9x_gl_prim_texcoord(pipeline, v[0], v[1], v[2], v[3]);
    }
    array = &arrays->array[V9X_GL_ARRAY_COLOR];
    if (array->enabled) {
        v[3] = 1.0f;
        gl_varray_fetch(array, index, 1, v);
        v9x_gl_prim_color(pipeline, v[0], v[1], v[2], v[3]);
    }
    array = &arrays->array[V9X_GL_ARRAY_NORMAL];
    if (array->enabled) {
        gl_varray_fetch(array, index, 1, v);
        v9x_gl_prim_normal(pipeline, v[0], v[1], v[2]);
    }
    array = &arrays->array[V9X_GL_ARRAY_VERTEX];
    if (array->enabled) {
        v[2] = 0.0f;
        v[3] = 1.0f;
        gl_varray_fetch(array, index, 0, v);
        v9x_gl_prim_vertex(state, pipeline, v[0], v[1], v[2], v[3]);
    }
}

/* The checks glDrawArrays and glDrawElements share, then glBegin. Zero
 * when an error was recorded and nothing may be drawn. */
static int gl_varray_begin(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                           GLenum mode, GLsizei count)
{
    if (state->in_begin) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return 0;
    }
    if (count < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return 0;
    }

    /* glBegin records INVALID_ENUM for a bad mode and stays outside. */
    v9x_gl_prim_begin(state, pipeline, mode);
    return state->in_begin;
}

void v9x_gl_arrays_draw(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                        const V9X_GL_ARRAYS *arrays, GLenum mode,
                        GLint first, GLsizei count)
{
    GLsizei i;

    if (!gl_varray_begin(state, pipeline, mode, count)) {
        return;
    }
    for (i = 0; i < count; ++i) {
        v9x_gl_arrays_element(state, pipeline, arrays, first + i);
    }
    v9x_gl_prim_end(state, pipeline);
}

void v9x_gl_arrays_draw_elements(V9X_GL_STATE *state,
                                 V9X_GL_PIPELINE *pipeline,
                                 const V9X_GL_ARRAYS *arrays, GLenum mode,
                                 GLsizei count, GLenum type,
                                 const void *indices)
{
    GLsizei i;
    GLint index;

    if (type != V9X_GL_UNSIGNED_BYTE && type != V9X_GL_UNSIGNED_SHORT &&
        type != V9X_GL_UNSIGNED_INT) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    if (!gl_varray_begin(state, pipeline, mode, count)) {
        return;
    }
    for (i = 0; i < count; ++i) {
        if (type == V9X_GL_UNSIGNED_BYTE) {
            index = (GLint)((const GLubyte *)indices)[i];
        } else if (type == V9X_GL_UNSIGNED_SHORT) {
            index = (GLint)((const GLushort *)indices)[i];
        } else {
            index = (GLint)((const GLuint *)indices)[i];
        }
        v9x_gl_arrays_element(state, pipeline, arrays, index);
    }
    v9x_gl_prim_end(state, pipeline);
}

/*
 * glInterleavedArrays as 2.8 defines it: the edge-flag and index arrays
 * are disabled, the texture, colour and normal arrays enabled or disabled
 * by the format, the vertex array enabled, and each pointer set into the
 * record with the record's stride, or the caller's when non-zero.
 */
void v9x_gl_arrays_interleaved(V9X_GL_STATE *state, V9X_GL_ARRAYS *arrays,
                               GLenum format, GLsizei stride,
                               const void *pointer)
{
    const GL_VARRAY_LAYOUT *layout;
    const v9x_u8 *base = (const v9x_u8 *)pointer;
    V9X_GL_ARRAY *array;

    if (format < GL_VARRAY_V2F ||
        format >= GL_VARRAY_V2F + GL_VARRAY_FORMAT_COUNT) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    if (stride < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    layout = &gl_varray_layouts[format - GL_VARRAY_V2F];
    if (stride == 0) {
        stride = (GLsizei)layout->record;
    }

    arrays->array[V9X_GL_ARRAY_EDGE_FLAG].enabled = 0;
    arrays->array[V9X_GL_ARRAY_INDEX].enabled = 0;

    array = &arrays->array[V9X_GL_ARRAY_TEXCOORD];
    array->enabled = layout->texture;
    if (layout->texture) {
        array->size = layout->texture_size;
        array->type = V9X_GL_FLOAT;
        array->stride = stride;
        array->pointer = base;
    }
    array = &arrays->array[V9X_GL_ARRAY_COLOR];
    array->enabled = layout->colour;
    if (layout->colour) {
        array->size = layout->colour_size;
        array->type = layout->colour_ubyte ? V9X_GL_UNSIGNED_BYTE
                                           : V9X_GL_FLOAT;
        array->stride = stride;
        array->pointer = base + layout->colour_offset;
    }
    array = &arrays->array[V9X_GL_ARRAY_NORMAL];
    array->enabled = layout->normal;
    if (layout->normal) {
        array->size = 3;
        array->type = V9X_GL_FLOAT;
        array->stride = stride;
        array->pointer = base + layout->normal_offset;
    }
    array = &arrays->array[V9X_GL_ARRAY_VERTEX];
    array->enabled = 1;
    array->size = layout->vertex_size;
    array->type = V9X_GL_FLOAT;
    array->stride = stride;
    array->pointer = base + layout->vertex_offset;
}
