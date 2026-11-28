/*
 * OpenGL 1.1 display lists (5.4): the names, the compiled words, and the
 * walk replay takes. Pure: memory comes through the allocator the ICD
 * hands in, and what a node means is the generated code's business
 * (scripts\lib\gl-dispatch.ps1, V9X_GL_DEFINE_SAVE), so every rule here is
 * a host test (tests\host\test_gl_dlist.c).
 * docs\plans\opengl-display-lists-and-texgen.md.
 *
 * A list is an array of 32-bit words: nodes, each a header word (the
 * slot in the low 16 bits, the node's argument words in the high 16, or
 * V9X_GL_DLIST_LONG there and the count in the next word) and then its
 * arguments. One table of names serves every context, as wglShareLists
 * would have it.
 */
#ifndef VELOCITY9X_GL_DLIST_H
#define VELOCITY9X_GL_DLIST_H

#include "gl_state.h"

#define V9X_GL_COMPILE              0x1300u
#define V9X_GL_COMPILE_AND_EXECUTE  0x1301u
#define V9X_GL_LIST_MODE            0x0B30u
#define V9X_GL_MAX_LIST_NESTING     0x0B31u
#define V9X_GL_LIST_BASE            0x0B32u
#define V9X_GL_LIST_INDEX           0x0B33u
#define V9X_GL_2_BYTES              0x1407u
#define V9X_GL_3_BYTES              0x1408u
#define V9X_GL_4_BYTES              0x1409u

/* 1.1's minimum for GL_MAX_LIST_NESTING. */
#define V9X_GL_DLIST_NESTING        64u
/* A header's length field when the node is this long or longer: the
 * length is then the word after the header (a vertex array draw's copy
 * can pass 256 KiB). */
#define V9X_GL_DLIST_LONG           0xFFFFul
/* A node's argument word for a null pointer, where a byte count goes. */
#define V9X_GL_DLIST_NULL           0xFFFFFFFFul

typedef void *(*V9X_GL_DLIST_ALLOC_FN)(v9x_u32 bytes);
typedef void (*V9X_GL_DLIST_FREE_FN)(void *memory);

typedef struct v9x_gl_dlist {
    GLuint name;            /* 0: an empty slot of the table */
    v9x_u32 *words;
    v9x_u32 count;
    v9x_u32 capacity;
} V9X_GL_DLIST;

typedef struct v9x_gl_dlists {
    V9X_GL_DLIST_ALLOC_FN alloc_fn;
    V9X_GL_DLIST_FREE_FN free_fn;
    /* Open addressing on the name, a power of two in size, at most half
     * full; a deleted entry keeps its place as a tombstone (name 0,
     * capacity V9X_GL_DLIST_NULL). */
    V9X_GL_DLIST *table;
    v9x_u32 table_size;
    v9x_u32 table_used;
    /* The highest name glGenLists has handed out. */
    GLuint highest;
    /* glNewList's list and mode, 0 when nothing is being compiled; the
     * words go into `compiling` and replace the list at glEndList. */
    GLuint compiling_name;
    GLenum compiling_mode;
    V9X_GL_DLIST compiling;
    GLuint base;
    v9x_u32 depth;
    /* Set when a node could not be allocated: the next glEndList reports
     * OUT_OF_MEMORY (5.4 leaves the list undefined). */
    int out_of_memory;
} V9X_GL_DLISTS;

void v9x_gl_dlists_init(V9X_GL_DLISTS *lists, V9X_GL_DLIST_ALLOC_FN alloc_fn,
                        V9X_GL_DLIST_FREE_FN free_fn);

/* glGenLists: the first of `range` consecutive unused names, each made an
 * empty list; 0 for a range of 0, or with INVALID_VALUE for a negative
 * one, or OUT_OF_MEMORY. */
GLuint v9x_gl_dlists_gen(V9X_GL_STATE *state, V9X_GL_DLISTS *lists,
                         GLsizei range);
/* glDeleteLists: INVALID_VALUE for a negative range; unused names in the
 * range are ignored. */
void v9x_gl_dlists_delete(V9X_GL_STATE *state, V9X_GL_DLISTS *lists,
                          GLuint list, GLsizei range);
GLboolean v9x_gl_dlists_is_list(const V9X_GL_DLISTS *lists, GLuint list);

/* glNewList: INVALID_VALUE for list 0, INVALID_ENUM for another mode,
 * INVALID_OPERATION while compiling or between Begin and End. */
void v9x_gl_dlists_new(V9X_GL_STATE *state, V9X_GL_DLISTS *lists,
                       GLuint list, GLenum mode);
/* glEndList: INVALID_OPERATION when nothing is being compiled; else the
 * compiled words replace the list's. */
void v9x_gl_dlists_end(V9X_GL_STATE *state, V9X_GL_DLISTS *lists);

/* A node of `words` argument words appended to the list being compiled:
 * where its arguments go, or null when none is being compiled, it is too
 * long, or memory ran out (out_of_memory is then set). */
v9x_u32 *v9x_gl_dlists_node(V9X_GL_DLISTS *lists, v9x_u32 slot,
                            v9x_u32 words);

/* What replay calls for each node, in order. */
typedef void (*V9X_GL_DLIST_NODE_FN)(void *user, v9x_u32 slot,
                                     const v9x_u32 *args, v9x_u32 words);

/*
 * glCallList: each node of `list` handed to `node`; nothing for a name
 * that is not a list. Non-zero when it ran; zero at the nesting limit,
 * where the call does nothing (5.4). No command a list holds can create,
 * delete or redefine one (5.4 executes those immediately), so the walk
 * keeps the words it started with.
 */
int v9x_gl_dlists_call(V9X_GL_DLISTS *lists, GLuint list,
                       V9X_GL_DLIST_NODE_FN node, void *user);

/*
 * glCallLists' names: element i of `names` as `type` gives (as 5.4's
 * table) an offset that glListBase's base is added to. Zero with
 * INVALID_ENUM for a type it does not name; non-zero with the name in
 * *name otherwise.
 */
int v9x_gl_dlists_name_at(const V9X_GL_DLISTS *lists, const void *names,
                          GLenum type, v9x_u32 i, GLuint *name);

/* The bytes glCallLists reads from its `lists` for n names of `type`; 0
 * for a negative n or a type it does not name. */
v9x_u32 v9x_gl_dlist_lists_bytes(GLsizei n, GLenum type);

/* The values a vector glFog, glLight, glLightModel, glMaterial,
 * glTexParameter, glTexEnv or glTexGen form reads for `pname`: 4 for the
 * colours, positions and planes, 3 for SPOT_DIRECTION and COLOR_INDEXES,
 * 1 for the rest. */
v9x_u32 v9x_gl_dlist_pname_count(GLenum pname);

#endif /* VELOCITY9X_GL_DLIST_H */
