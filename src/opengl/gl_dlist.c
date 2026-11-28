/*
 * OpenGL 1.1 display lists (gl_dlist.h): names, compiled words, replay's
 * walk.
 */
#include "gl_dlist.h"

#define V9X_GL_DLIST_TABLE_FIRST 64u
#define V9X_GL_DLIST_WORDS_FIRST 64u
#define V9X_GL_DLIST_TOMBSTONE   V9X_GL_DLIST_NULL

#define V9X_GL_DLIST_UNSIGNED_BYTE  0x1401u
#define V9X_GL_DLIST_SHORT          0x1402u
#define V9X_GL_DLIST_UNSIGNED_SHORT 0x1403u
#define V9X_GL_DLIST_INT            0x1404u
#define V9X_GL_DLIST_UNSIGNED_INT   0x1405u
#define V9X_GL_DLIST_FLOAT          0x1406u
#define V9X_GL_DLIST_BYTE           0x1400u

void v9x_gl_dlists_init(V9X_GL_DLISTS *lists, V9X_GL_DLIST_ALLOC_FN alloc_fn,
                        V9X_GL_DLIST_FREE_FN free_fn)
{
    lists->alloc_fn = alloc_fn;
    lists->free_fn = free_fn;
    lists->table = 0;
    lists->table_size = 0ul;
    lists->table_used = 0ul;
    lists->highest = 0u;
    lists->compiling_name = 0u;
    lists->compiling_mode = 0u;
    lists->compiling.name = 0u;
    lists->compiling.words = 0;
    lists->compiling.count = 0ul;
    lists->compiling.capacity = 0ul;
    lists->base = 0u;
    lists->depth = 0ul;
    lists->out_of_memory = 0;
}

static v9x_u32 v9x_gl_dlist_hash(GLuint name, v9x_u32 size)
{
    /* Knuth's multiplicative hash: names come in runs from glGenLists. */
    return ((v9x_u32)name * 2654435761ul) & (size - 1ul);
}

static int v9x_gl_dlist_tombstone(const V9X_GL_DLIST *entry)
{
    return entry->name == 0u && entry->capacity == V9X_GL_DLIST_TOMBSTONE;
}

static V9X_GL_DLIST *v9x_gl_dlist_find(const V9X_GL_DLISTS *lists,
                                       GLuint name)
{
    v9x_u32 at;
    v9x_u32 probes;

    if (lists->table == 0 || name == 0u) {
        return 0;
    }
    at = v9x_gl_dlist_hash(name, lists->table_size);
    for (probes = 0ul; probes < lists->table_size; ++probes) {
        V9X_GL_DLIST *entry = &lists->table[at];

        if (entry->name == name) {
            return entry;
        }
        if (entry->name == 0u && !v9x_gl_dlist_tombstone(entry)) {
            return 0;
        }
        at = (at + 1ul) & (lists->table_size - 1ul);
    }
    return 0;
}

/* An entry for a name known to be absent, the table already large enough;
 * the first tombstone or empty slot on its probe path. */
static V9X_GL_DLIST *v9x_gl_dlist_slot(V9X_GL_DLISTS *lists, GLuint name)
{
    v9x_u32 at = v9x_gl_dlist_hash(name, lists->table_size);

    while (lists->table[at].name != 0u) {
        at = (at + 1ul) & (lists->table_size - 1ul);
    }
    return &lists->table[at];
}

/* Room for `more` names: the table grown (doubling, rehashed, tombstones
 * dropped) to stay at most half full. Zero when memory ran out. */
static int v9x_gl_dlist_room(V9X_GL_DLISTS *lists, v9x_u32 more)
{
    V9X_GL_DLIST *old = lists->table;
    v9x_u32 old_size = lists->table_size;
    v9x_u32 size = old_size != 0ul ? old_size : V9X_GL_DLIST_TABLE_FIRST;
    v9x_u32 i;

    while ((lists->table_used + more) * 2ul > size) {
        size *= 2ul;
    }
    if (size == old_size) {
        return 1;
    }
    lists->table = (V9X_GL_DLIST *)lists->alloc_fn(size * sizeof(V9X_GL_DLIST));
    if (lists->table == 0) {
        lists->table = old;
        return 0;
    }
    lists->table_size = size;
    lists->table_used = 0ul;
    for (i = 0ul; i < size; ++i) {
        lists->table[i].name = 0u;
        lists->table[i].words = 0;
        lists->table[i].count = 0ul;
        lists->table[i].capacity = 0ul;
    }
    for (i = 0ul; i < old_size; ++i) {
        if (old[i].name != 0u) {
            *v9x_gl_dlist_slot(lists, old[i].name) = old[i];
            ++lists->table_used;
        }
    }
    if (old != 0) {
        lists->free_fn(old);
    }
    return 1;
}

/* A new, empty list named `name`, known absent, with room made. A reused
 * tombstone counts again in table_used; growth recounts. */
static V9X_GL_DLIST *v9x_gl_dlist_claim(V9X_GL_DLISTS *lists, GLuint name)
{
    V9X_GL_DLIST *entry = v9x_gl_dlist_slot(lists, name);

    entry->name = name;
    entry->words = 0;
    entry->count = 0ul;
    entry->capacity = 0ul;
    ++lists->table_used;
    return entry;
}

GLuint v9x_gl_dlists_gen(V9X_GL_STATE *state, V9X_GL_DLISTS *lists,
                         GLsizei range)
{
    GLuint first;
    GLuint name;
    GLsizei i;

    if (range < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return 0u;
    }
    if (range == 0) {
        return 0u;
    }
    /* Past every name handed out: a run that is certainly unused, unless
     * the program defined lists with names of its own above it, or is
     * compiling one (not in the table until glEndList). */
    first = lists->highest + 1u;
    for (i = 0; i < range; ++i) {
        if (v9x_gl_dlist_find(lists, first + (GLuint)i) != 0 ||
            first + (GLuint)i == lists->compiling_name) {
            first = first + (GLuint)i + 1u;
            i = -1;
        }
    }
    if (!v9x_gl_dlist_room(lists, (v9x_u32)range)) {
        v9x_gl_state_error(state, V9X_GL_OUT_OF_MEMORY);
        return 0u;
    }
    for (i = 0; i < range; ++i) {
        name = first + (GLuint)i;
        (void)v9x_gl_dlist_claim(lists, name);
    }
    lists->highest = first + (GLuint)range - 1u;
    return first;
}

static void v9x_gl_dlist_free_words(V9X_GL_DLISTS *lists,
                                    V9X_GL_DLIST *entry)
{
    if (entry->words != 0) {
        lists->free_fn(entry->words);
    }
    entry->words = 0;
    entry->count = 0ul;
    entry->capacity = 0ul;
}

void v9x_gl_dlists_delete(V9X_GL_STATE *state, V9X_GL_DLISTS *lists,
                          GLuint list, GLsizei range)
{
    GLsizei i;

    if (range < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < range; ++i) {
        V9X_GL_DLIST *entry = v9x_gl_dlist_find(lists, list + (GLuint)i);

        if (entry != 0) {
            v9x_gl_dlist_free_words(lists, entry);
            entry->name = 0u;
            entry->capacity = V9X_GL_DLIST_TOMBSTONE;
            /* A tombstone still lengthens probes; table_used keeps
             * counting it until the next growth drops it. */
        }
    }
}

GLboolean v9x_gl_dlists_is_list(const V9X_GL_DLISTS *lists, GLuint list)
{
    return v9x_gl_dlist_find(lists, list) != 0 ? (GLboolean)1 : (GLboolean)0;
}

void v9x_gl_dlists_new(V9X_GL_STATE *state, V9X_GL_DLISTS *lists,
                       GLuint list, GLenum mode)
{
    if (state->in_begin || lists->compiling_name != 0u) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return;
    }
    if (list == 0u) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    if (mode != V9X_GL_COMPILE && mode != V9X_GL_COMPILE_AND_EXECUTE) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    lists->compiling_name = list;
    lists->compiling_mode = mode;
    lists->compiling.count = 0ul;
    lists->out_of_memory = 0;
}

void v9x_gl_dlists_end(V9X_GL_STATE *state, V9X_GL_DLISTS *lists)
{
    V9X_GL_DLIST *entry;
    GLuint name = lists->compiling_name;

    if (name == 0u) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return;
    }
    lists->compiling_name = 0u;
    lists->compiling_mode = 0u;
    if (lists->out_of_memory) {
        v9x_gl_state_error(state, V9X_GL_OUT_OF_MEMORY);
    }
    entry = v9x_gl_dlist_find(lists, name);
    if (entry == 0) {
        if (!v9x_gl_dlist_room(lists, 1ul)) {
            v9x_gl_state_error(state, V9X_GL_OUT_OF_MEMORY);
            lists->compiling.count = 0ul;
            return;
        }
        entry = v9x_gl_dlist_claim(lists, name);
    }
    /* The compiled words become the list's, and its old words the next
     * compile's buffer: no copy either way. */
    {
        v9x_u32 *words = entry->words;
        v9x_u32 capacity = entry->capacity;

        entry->words = lists->compiling.words;
        entry->count = lists->compiling.count;
        entry->capacity = lists->compiling.capacity;
        lists->compiling.words = words;
        lists->compiling.capacity = capacity;
        lists->compiling.count = 0ul;
    }
    if (name > lists->highest) {
        lists->highest = name;
    }
}

v9x_u32 *v9x_gl_dlists_node(V9X_GL_DLISTS *lists, v9x_u32 slot,
                            v9x_u32 words)
{
    V9X_GL_DLIST *c = &lists->compiling;
    v9x_u32 head = words >= V9X_GL_DLIST_LONG ? 2ul : 1ul;
    v9x_u32 need;
    v9x_u32 *node;

    if (lists->compiling_name == 0u) {
        return 0;
    }
    /* A node past the list's 4 GiB of address space cannot be held. */
    if (words > 0x3FFFFFFFul - c->count - head) {
        lists->out_of_memory = 1;
        return 0;
    }
    need = c->count + head + words;
    if (need > c->capacity) {
        v9x_u32 capacity = c->capacity != 0ul ? c->capacity
                                              : V9X_GL_DLIST_WORDS_FIRST;
        v9x_u32 *grown;
        v9x_u32 i;

        while (capacity < need) {
            capacity *= 2ul;
        }
        grown = (v9x_u32 *)lists->alloc_fn(capacity * 4ul);
        if (grown == 0) {
            lists->out_of_memory = 1;
            return 0;
        }
        for (i = 0ul; i < c->count; ++i) {
            grown[i] = c->words[i];
        }
        if (c->words != 0) {
            lists->free_fn(c->words);
        }
        c->words = grown;
        c->capacity = capacity;
    }
    node = &c->words[c->count];
    c->count = need;
    if (head == 2ul) {
        node[0] = (slot & 0xFFFFul) | (V9X_GL_DLIST_LONG << 16);
        node[1] = words;
        return node + 2;
    }
    node[0] = (slot & 0xFFFFul) | (words << 16);
    return node + 1;
}

int v9x_gl_dlists_call(V9X_GL_DLISTS *lists, GLuint list,
                       V9X_GL_DLIST_NODE_FN node, void *user)
{
    const V9X_GL_DLIST *entry;
    const v9x_u32 *words;
    v9x_u32 count;
    v9x_u32 at = 0ul;

    if (lists->depth >= V9X_GL_DLIST_NESTING) {
        return 0;
    }
    entry = v9x_gl_dlist_find(lists, list);
    if (entry == 0) {
        return 1;
    }
    words = entry->words;
    count = entry->count;
    ++lists->depth;
    while (at < count) {
        v9x_u32 header = words[at];
        v9x_u32 length = header >> 16;
        v9x_u32 head = 1ul;

        if (length == V9X_GL_DLIST_LONG) {
            if (at + 1ul >= count) {
                break;
            }
            length = words[at + 1ul];
            head = 2ul;
        }
        if (length > count - at - head) {
            break;
        }
        node(user, header & 0xFFFFul, &words[at + head], length);
        at += head + length;
    }
    --lists->depth;
    return 1;
}

int v9x_gl_dlists_name_at(const V9X_GL_DLISTS *lists, const void *names,
                          GLenum type, v9x_u32 i, GLuint *name)
{
    const unsigned char *b = (const unsigned char *)names;
    v9x_u32 offset;

    switch (type) {
    case V9X_GL_DLIST_BYTE:
        offset = (v9x_u32)(GLint)((const GLbyte *)names)[i];
        break;
    case V9X_GL_DLIST_UNSIGNED_BYTE:
        offset = b[i];
        break;
    case V9X_GL_DLIST_SHORT:
        offset = (v9x_u32)(GLint)((const GLshort *)names)[i];
        break;
    case V9X_GL_DLIST_UNSIGNED_SHORT:
        offset = ((const GLushort *)names)[i];
        break;
    case V9X_GL_DLIST_INT:
    case V9X_GL_DLIST_UNSIGNED_INT:
        offset = ((const v9x_u32 *)names)[i];
        break;
    case V9X_GL_DLIST_FLOAT:
        /* A float name. A float-to-integer cast needs the C runtime's
         * helper, which the ICD does not link, so the whole part is found
         * bit by bit, comparing f with integers converted to float (an
         * integer-to-float conversion needs none). Names past 2^31 and
         * below 0 read as 0. */
        {
            GLfloat f = ((const GLfloat *)names)[i];
            v9x_u32 whole = 0ul;
            v9x_u32 step;

            if (f > 0.0f) {
                for (step = 0x40000000ul; step != 0ul; step >>= 1) {
                    if ((GLfloat)(GLint)(whole | step) <= f) {
                        whole |= step;
                    }
                }
            }
            offset = whole;
        }
        break;
    case V9X_GL_2_BYTES:
        offset = ((v9x_u32)b[2ul * i] << 8) | b[2ul * i + 1ul];
        break;
    case V9X_GL_3_BYTES:
        offset = ((v9x_u32)b[3ul * i] << 16) |
                 ((v9x_u32)b[3ul * i + 1ul] << 8) | b[3ul * i + 2ul];
        break;
    case V9X_GL_4_BYTES:
        offset = ((v9x_u32)b[4ul * i] << 24) |
                 ((v9x_u32)b[4ul * i + 1ul] << 16) |
                 ((v9x_u32)b[4ul * i + 2ul] << 8) | b[4ul * i + 3ul];
        break;
    default:
        return 0;
    }
    *name = (GLuint)(lists->base + offset);
    return 1;
}

v9x_u32 v9x_gl_dlist_lists_bytes(GLsizei n, GLenum type)
{
    v9x_u32 each;

    if (n < 0) {
        return 0ul;
    }
    switch (type) {
    case V9X_GL_DLIST_BYTE:
    case V9X_GL_DLIST_UNSIGNED_BYTE:
        each = 1ul;
        break;
    case V9X_GL_DLIST_SHORT:
    case V9X_GL_DLIST_UNSIGNED_SHORT:
    case V9X_GL_2_BYTES:
        each = 2ul;
        break;
    case V9X_GL_3_BYTES:
        each = 3ul;
        break;
    case V9X_GL_DLIST_INT:
    case V9X_GL_DLIST_UNSIGNED_INT:
    case V9X_GL_DLIST_FLOAT:
    case V9X_GL_4_BYTES:
        each = 4ul;
        break;
    default:
        return 0ul;
    }
    return each * (v9x_u32)n;
}

v9x_u32 v9x_gl_dlist_pname_count(GLenum pname)
{
    switch (pname) {
    case 0x1200u:   /* AMBIENT */
    case 0x1201u:   /* DIFFUSE */
    case 0x1202u:   /* SPECULAR */
    case 0x1203u:   /* POSITION */
    case 0x1600u:   /* EMISSION */
    case 0x1602u:   /* AMBIENT_AND_DIFFUSE */
    case 0x0B53u:   /* LIGHT_MODEL_AMBIENT */
    case 0x0B66u:   /* FOG_COLOR */
    case 0x2201u:   /* TEXTURE_ENV_COLOR */
    case 0x1004u:   /* TEXTURE_BORDER_COLOR */
    case 0x2501u:   /* OBJECT_PLANE */
    case 0x2502u:   /* EYE_PLANE */
        return 4ul;
    case 0x1204u:   /* SPOT_DIRECTION */
    case 0x1603u:   /* COLOR_INDEXES */
        return 3ul;
    default:
        return 1ul;
    }
}
