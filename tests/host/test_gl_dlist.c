/*
 * Tests for OpenGL display lists: the list store (src\opengl\gl_dlist.c)
 * and the save and replay code scripts\lib\gl-dispatch.ps1 generates from
 * gl_entrypoints.psd1 and gl_dlist_rules.psd1. The census forwarders are
 * aimed at recording functions, as the ICD aims them at its
 * implementations, so a call compiled into a list and replayed can be
 * compared argument for argument with the call made.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../src/opengl/gl_dlist.h"

static V9X_GL_STATE dl_state;
static V9X_GL_DLISTS dl_lists;
static unsigned int dl_uncompiled;
static unsigned int dl_special;
static unsigned int gl_dlist_failures;

#define V9X_GL_API __stdcall
static void (__stdcall *dl_target[512])(void);
#define V9X_GL_CENSUS_HOOK(slot) ((void)(slot))
#define V9X_GL_CENSUS_TARGET(slot) dl_target[slot]
#define V9X_GL_DEFINE_CENSUS
#define V9X_GL_DEFINE_SAVE
#define V9X_GL_SAVING() (dl_lists.compiling_name != 0u)
#define V9X_GL_DLIST_NODE(slot, words) \
    v9x_gl_dlists_node(&dl_lists, (slot), (words))
#define V9X_GL_DLIST_COPY(to, from, n) memcpy((to), (from), (n))
#define V9X_GL_DLIST_COMPILE_ONLY() \
    (dl_lists.compiling_mode == V9X_GL_COMPILE)
#define V9X_GL_DLIST_UNCOMPILED(slot) ((void)(slot), ++dl_uncompiled)
#define V9X_GL_REPLAY_TARGET(slot) dl_target[slot]
#define V9X_GL_REPLAY_SPECIAL(slot, args, words) \
    ((void)(slot), (void)(args), (void)(words), ++dl_special)
#include "gl_dispatch_gen.h"

static int v9x_gl_save_glArrayElement(GLint i)
{
    (void)i;
    return 0;
}

static int v9x_gl_save_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    (void)mode;
    (void)first;
    (void)count;
    return 0;
}

static int v9x_gl_save_glDrawElements(GLenum mode, GLsizei count,
                                      GLenum type, const GLvoid *indices)
{
    (void)mode;
    (void)count;
    (void)type;
    (void)indices;
    return 0;
}

#define DCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++gl_dlist_failures; \
    } \
} while (0)

#define DL_UNSIGNED_BYTE 0x1401u
#define DL_FLOAT         0x1406u
#define DL_LIGHT0        0x4000u
#define DL_POSITION      0x1203u
#define DL_TEXTURE_2D    0x0DE1u
#define DL_RGBA          0x1908u

static const char *const dl_names[V9X_GL_SLOT_COUNT] = V9X_GL_SLOT_NAMES;
static const V9X_GL_PROC dl_census[V9X_GL_SLOT_COUNT] = V9X_GL_CENSUS_INIT;

static unsigned int dl_slot(const char *name)
{
    unsigned int slot;

    for (slot = 0u; slot < V9X_GL_SLOT_COUNT; ++slot) {
        if (strcmp(dl_names[slot], name) == 0) {
            return slot;
        }
    }
    printf("FAIL no slot %s\n", name);
    ++gl_dlist_failures;
    return 0u;
}

/* The forwarder a program's call reaches, by name. */
#define DL_CALL(name) ((V9X_GL_PFN_##name)dl_census[dl_slot(#name)])

/* ---- Recording implementations ------------------------------------- */

static unsigned int rec_color3f_calls;
static GLfloat rec_color3f[3];
static unsigned int rec_color3fv_calls;
static GLfloat rec_color3fv[3];
static unsigned int rec_translated_calls;
static GLdouble rec_translated[3];
static unsigned int rec_vertex2s_calls;
static GLshort rec_vertex2s[2];
static unsigned int rec_lightfv_calls;
static GLenum rec_lightfv_light;
static GLenum rec_lightfv_pname;
static GLfloat rec_lightfv[4];
static unsigned int rec_lightf_calls;
static GLfloat rec_lightf;
static unsigned int rec_pop_calls;
static unsigned int rec_teximage_calls;
static unsigned int rec_genlists_calls;
static GLuint rec_called[16];
static unsigned int rec_called_count;

static void dl_replay(void *user, v9x_u32 slot, const v9x_u32 *args,
                      v9x_u32 words)
{
    (void)user;
    v9x_gl_dlist_replay_node(slot, args, words);
}

static void __stdcall rec_glColor3f(GLfloat r, GLfloat g, GLfloat b)
{
    ++rec_color3f_calls;
    rec_color3f[0] = r;
    rec_color3f[1] = g;
    rec_color3f[2] = b;
}

static void __stdcall rec_glColor3fv(const GLfloat *v)
{
    ++rec_color3fv_calls;
    memcpy(rec_color3fv, v, sizeof(rec_color3fv));
}

static void __stdcall rec_glTranslated(GLdouble x, GLdouble y, GLdouble z)
{
    ++rec_translated_calls;
    rec_translated[0] = x;
    rec_translated[1] = y;
    rec_translated[2] = z;
}

static void __stdcall rec_glVertex2s(GLshort x, GLshort y)
{
    ++rec_vertex2s_calls;
    rec_vertex2s[0] = x;
    rec_vertex2s[1] = y;
}

static void __stdcall rec_glLightfv(GLenum light, GLenum pname,
                                    const GLfloat *params)
{
    ++rec_lightfv_calls;
    rec_lightfv_light = light;
    rec_lightfv_pname = pname;
    memcpy(rec_lightfv, params, sizeof(rec_lightfv));
}

static void __stdcall rec_glLightf(GLenum light, GLenum pname, GLfloat p)
{
    (void)light;
    (void)pname;
    ++rec_lightf_calls;
    rec_lightf = p;
}

static void __stdcall rec_glPopMatrix(void)
{
    ++rec_pop_calls;
}

static void __stdcall rec_glTexImage2D(GLenum target, GLint level,
                                       GLint internal, GLsizei width,
                                       GLsizei height, GLint border,
                                       GLenum format, GLenum type,
                                       const GLvoid *pixels)
{
    (void)target;
    (void)level;
    (void)internal;
    (void)width;
    (void)height;
    (void)border;
    (void)format;
    (void)type;
    (void)pixels;
    ++rec_teximage_calls;
}

static GLuint __stdcall rec_glGenLists(GLsizei range)
{
    ++rec_genlists_calls;
    return v9x_gl_dlists_gen(&dl_state, &dl_lists, range);
}

static void __stdcall rec_glNewList(GLuint list, GLenum mode)
{
    v9x_gl_dlists_new(&dl_state, &dl_lists, list, mode);
}

static void __stdcall rec_glEndList(void)
{
    v9x_gl_dlists_end(&dl_state, &dl_lists);
}

static void __stdcall rec_glCallList(GLuint list)
{
    if (rec_called_count < 16u) {
        rec_called[rec_called_count] = list;
    }
    ++rec_called_count;
    (void)v9x_gl_dlists_call(&dl_lists, list, dl_replay, 0);
}

static void __stdcall rec_glCallLists(GLsizei n, GLenum type,
                                      const GLvoid *lists)
{
    GLsizei i;
    GLuint name;

    for (i = 0; i < n; ++i) {
        if (v9x_gl_dlists_name_at(&dl_lists, lists, type, (v9x_u32)i,
                                  &name)) {
            rec_glCallList(name);
        }
    }
}

static void __stdcall rec_glListBase(GLuint base)
{
    dl_lists.base = base;
}

static void *dl_alloc(v9x_u32 bytes)
{
    return malloc(bytes);
}

static void dl_free(void *memory)
{
    free(memory);
}

static void dl_reset(void)
{
    memset(dl_target, 0, sizeof(dl_target));
    dl_target[dl_slot("glColor3f")] = (V9X_GL_PROC)rec_glColor3f;
    dl_target[dl_slot("glColor3fv")] = (V9X_GL_PROC)rec_glColor3fv;
    dl_target[dl_slot("glTranslated")] = (V9X_GL_PROC)rec_glTranslated;
    dl_target[dl_slot("glVertex2s")] = (V9X_GL_PROC)rec_glVertex2s;
    dl_target[dl_slot("glLightfv")] = (V9X_GL_PROC)rec_glLightfv;
    dl_target[dl_slot("glLightf")] = (V9X_GL_PROC)rec_glLightf;
    dl_target[dl_slot("glPopMatrix")] = (V9X_GL_PROC)rec_glPopMatrix;
    dl_target[dl_slot("glTexImage2D")] = (V9X_GL_PROC)rec_glTexImage2D;
    dl_target[dl_slot("glGenLists")] = (V9X_GL_PROC)rec_glGenLists;
    dl_target[dl_slot("glNewList")] = (V9X_GL_PROC)rec_glNewList;
    dl_target[dl_slot("glEndList")] = (V9X_GL_PROC)rec_glEndList;
    dl_target[dl_slot("glCallList")] = (V9X_GL_PROC)rec_glCallList;
    dl_target[dl_slot("glCallLists")] = (V9X_GL_PROC)rec_glCallLists;
    dl_target[dl_slot("glListBase")] = (V9X_GL_PROC)rec_glListBase;
    v9x_gl_state_init(&dl_state);
    v9x_gl_dlists_init(&dl_lists, dl_alloc, dl_free);
    rec_color3f_calls = 0u;
    rec_color3fv_calls = 0u;
    rec_translated_calls = 0u;
    rec_vertex2s_calls = 0u;
    rec_lightfv_calls = 0u;
    rec_lightf_calls = 0u;
    rec_pop_calls = 0u;
    rec_teximage_calls = 0u;
    rec_genlists_calls = 0u;
    rec_called_count = 0u;
    dl_uncompiled = 0u;
    dl_special = 0u;
}

/* ---- The store ------------------------------------------------------ */

static void test_names(void)
{
    GLuint first;

    dl_reset();
    DCHECK(v9x_gl_dlists_gen(&dl_state, &dl_lists, 0) == 0u);
    DCHECK(dl_state.error == V9X_GL_NO_ERROR);
    DCHECK(v9x_gl_dlists_gen(&dl_state, &dl_lists, -1) == 0u);
    DCHECK(dl_state.error == V9X_GL_INVALID_VALUE);
    dl_state.error = V9X_GL_NO_ERROR;
    first = v9x_gl_dlists_gen(&dl_state, &dl_lists, 3);
    DCHECK(first == 1u);
    DCHECK(v9x_gl_dlists_is_list(&dl_lists, 1u) &&
           v9x_gl_dlists_is_list(&dl_lists, 3u));
    DCHECK(!v9x_gl_dlists_is_list(&dl_lists, 4u));
    DCHECK(!v9x_gl_dlists_is_list(&dl_lists, 0u));
    /* A program's own name above the run moves the next run past it. */
    v9x_gl_dlists_new(&dl_state, &dl_lists, 5u, V9X_GL_COMPILE);
    v9x_gl_dlists_end(&dl_state, &dl_lists);
    DCHECK(v9x_gl_dlists_is_list(&dl_lists, 5u));
    DCHECK(v9x_gl_dlists_gen(&dl_state, &dl_lists, 2) == 6u);
    v9x_gl_dlists_delete(&dl_state, &dl_lists, 2u, 2);
    DCHECK(v9x_gl_dlists_is_list(&dl_lists, 1u));
    DCHECK(!v9x_gl_dlists_is_list(&dl_lists, 2u));
    DCHECK(!v9x_gl_dlists_is_list(&dl_lists, 3u));
    v9x_gl_dlists_delete(&dl_state, &dl_lists, 1u, -1);
    DCHECK(dl_state.error == V9X_GL_INVALID_VALUE);
    dl_state.error = V9X_GL_NO_ERROR;
    /* Many names: the table grows and keeps every one. */
    first = v9x_gl_dlists_gen(&dl_state, &dl_lists, 5000);
    DCHECK(first == 8u);
    DCHECK(v9x_gl_dlists_is_list(&dl_lists, 8u) &&
           v9x_gl_dlists_is_list(&dl_lists, 5007u) &&
           v9x_gl_dlists_is_list(&dl_lists, 1u) &&
           !v9x_gl_dlists_is_list(&dl_lists, 2u));
    DCHECK(dl_state.error == V9X_GL_NO_ERROR);
}

static void test_new_and_end_errors(void)
{
    dl_reset();
    v9x_gl_dlists_end(&dl_state, &dl_lists);
    DCHECK(dl_state.error == V9X_GL_INVALID_OPERATION);
    dl_state.error = V9X_GL_NO_ERROR;
    v9x_gl_dlists_new(&dl_state, &dl_lists, 0u, V9X_GL_COMPILE);
    DCHECK(dl_state.error == V9X_GL_INVALID_VALUE);
    dl_state.error = V9X_GL_NO_ERROR;
    v9x_gl_dlists_new(&dl_state, &dl_lists, 1u, 0x1234u);
    DCHECK(dl_state.error == V9X_GL_INVALID_ENUM);
    DCHECK(dl_lists.compiling_name == 0u);
    dl_state.error = V9X_GL_NO_ERROR;
    v9x_gl_dlists_new(&dl_state, &dl_lists, 1u, V9X_GL_COMPILE);
    v9x_gl_dlists_new(&dl_state, &dl_lists, 2u, V9X_GL_COMPILE);
    DCHECK(dl_state.error == V9X_GL_INVALID_OPERATION);
    DCHECK(dl_lists.compiling_name == 1u);
    v9x_gl_dlists_end(&dl_state, &dl_lists);
    dl_state.error = V9X_GL_NO_ERROR;
    dl_state.in_begin = 1;
    v9x_gl_dlists_new(&dl_state, &dl_lists, 1u, V9X_GL_COMPILE);
    DCHECK(dl_state.error == V9X_GL_INVALID_OPERATION);
    DCHECK(dl_lists.compiling_name == 0u);
}

static void test_helpers(void)
{
    unsigned char two[4] = { 0x01u, 0x02u, 0x00u, 0x05u };
    GLfloat floats[2] = { 7.0f, 300.0f };
    GLuint name;

    dl_reset();
    dl_lists.base = 10u;
    DCHECK(v9x_gl_dlists_name_at(&dl_lists, two, V9X_GL_2_BYTES, 0u,
                                 &name) && name == 10u + 0x0102u);
    DCHECK(v9x_gl_dlists_name_at(&dl_lists, two, V9X_GL_2_BYTES, 1u,
                                 &name) && name == 15u);
    DCHECK(v9x_gl_dlists_name_at(&dl_lists, floats, DL_FLOAT, 1u, &name) &&
           name == 310u);
    DCHECK(!v9x_gl_dlists_name_at(&dl_lists, two, 0x1234u, 0u, &name));
    DCHECK(v9x_gl_dlist_lists_bytes(5, DL_UNSIGNED_BYTE) == 5ul);
    DCHECK(v9x_gl_dlist_lists_bytes(5, V9X_GL_3_BYTES) == 15ul);
    DCHECK(v9x_gl_dlist_lists_bytes(-1, DL_UNSIGNED_BYTE) == 0ul);
    DCHECK(v9x_gl_dlist_lists_bytes(5, 0x1234u) == 0ul);
    DCHECK(v9x_gl_dlist_pname_count(DL_POSITION) == 4ul);
    DCHECK(v9x_gl_dlist_pname_count(0x1204u) == 3ul);  /* SPOT_DIRECTION */
    DCHECK(v9x_gl_dlist_pname_count(0x1205u) == 1ul);  /* SPOT_EXPONENT */
}

/* ---- Compile and replay through the generated code ------------------- */

static void test_compile_only(void)
{
    GLfloat colour[3] = { 0.25f, 0.5f, 0.75f };
    GLfloat position[4] = { 1.0f, -2.0f, 3.5f, 0.0f };

    dl_reset();
    DL_CALL(glNewList)(1u, V9X_GL_COMPILE);
    DL_CALL(glColor3f)(0.1f, 0.2f, 0.3f);
    DL_CALL(glColor3fv)(colour);
    DL_CALL(glTranslated)(1.0e-9, -2.5, 1.0e12);
    DL_CALL(glVertex2s)((GLshort)-7, (GLshort)32767);
    DL_CALL(glLightfv)(DL_LIGHT0, DL_POSITION, position);
    DL_CALL(glLightf)(DL_LIGHT0, 0x1205u, 12.5f);
    DL_CALL(glPopMatrix)();
    DL_CALL(glEndList)();
    DCHECK(dl_state.error == V9X_GL_NO_ERROR);
    /* GL_COMPILE: nothing ran. */
    DCHECK(rec_color3f_calls == 0u && rec_color3fv_calls == 0u &&
           rec_translated_calls == 0u && rec_vertex2s_calls == 0u &&
           rec_lightfv_calls == 0u && rec_lightf_calls == 0u &&
           rec_pop_calls == 0u);
    /* What a pointer pointed at was copied: changing it now changes
     * nothing in the list. */
    colour[0] = 9.0f;
    position[1] = 9.0f;
    DL_CALL(glCallList)(1u);
    DCHECK(rec_color3f_calls == 1u && rec_color3f[0] == 0.1f &&
           rec_color3f[1] == 0.2f && rec_color3f[2] == 0.3f);
    DCHECK(rec_color3fv_calls == 1u && rec_color3fv[0] == 0.25f &&
           rec_color3fv[1] == 0.5f && rec_color3fv[2] == 0.75f);
    DCHECK(rec_translated_calls == 1u && rec_translated[0] == 1.0e-9 &&
           rec_translated[1] == -2.5 && rec_translated[2] == 1.0e12);
    DCHECK(rec_vertex2s_calls == 1u && rec_vertex2s[0] == -7 &&
           rec_vertex2s[1] == 32767);
    DCHECK(rec_lightfv_calls == 1u && rec_lightfv_light == DL_LIGHT0 &&
           rec_lightfv_pname == DL_POSITION && rec_lightfv[0] == 1.0f &&
           rec_lightfv[1] == -2.0f && rec_lightfv[2] == 3.5f &&
           rec_lightfv[3] == 0.0f);
    DCHECK(rec_lightf_calls == 1u && rec_lightf == 12.5f);
    DCHECK(rec_pop_calls == 1u);
    /* Called again, it runs again. */
    DL_CALL(glCallList)(1u);
    DCHECK(rec_color3f_calls == 2u && rec_pop_calls == 2u);
}

static void test_compile_and_execute(void)
{
    dl_reset();
    DL_CALL(glNewList)(4u, V9X_GL_COMPILE_AND_EXECUTE);
    DL_CALL(glColor3f)(1.0f, 0.0f, 0.0f);
    DL_CALL(glEndList)();
    DCHECK(rec_color3f_calls == 1u);
    DL_CALL(glCallList)(4u);
    DCHECK(rec_color3f_calls == 2u);
    /* Redefined: the new words replace the old. */
    DL_CALL(glNewList)(4u, V9X_GL_COMPILE);
    DL_CALL(glPopMatrix)();
    DL_CALL(glEndList)();
    DL_CALL(glCallList)(4u);
    DCHECK(rec_color3f_calls == 2u && rec_pop_calls == 1u);
    /* Deleted: calling it does nothing. */
    v9x_gl_dlists_delete(&dl_state, &dl_lists, 4u, 1);
    DL_CALL(glCallList)(4u);
    DCHECK(rec_pop_calls == 1u);
}

static void test_not_compiled(void)
{
    dl_reset();
    DL_CALL(glNewList)(1u, V9X_GL_COMPILE);
    /* 5.4's immediate commands run now and leave no node. */
    DCHECK(DL_CALL(glGenLists)(2) == 2u);
    DCHECK(rec_genlists_calls == 1u);
    /* A Deferred command runs now too, and is counted. */
    DL_CALL(glTexImage2D)(DL_TEXTURE_2D, 0, 4, 1, 1, 0, DL_RGBA,
                          DL_UNSIGNED_BYTE, "abcd");
    DCHECK(rec_teximage_calls == 1u && dl_uncompiled == 1u);
    DL_CALL(glEndList)();
    DCHECK(dl_lists.compiling_name == 0u);
    DL_CALL(glCallList)(1u);
    DCHECK(rec_genlists_calls == 1u && rec_teximage_calls == 1u);
    /* Not compiling: the save step is not offered at all. */
    DL_CALL(glTexImage2D)(DL_TEXTURE_2D, 0, 4, 1, 1, 0, DL_RGBA,
                          DL_UNSIGNED_BYTE, "abcd");
    DCHECK(rec_teximage_calls == 2u && dl_uncompiled == 1u);
}

static void test_call_lists_and_nesting(void)
{
    GLubyte offsets[3] = { 0u, 2u, 1u };

    dl_reset();
    DL_CALL(glNewList)(10u, V9X_GL_COMPILE);
    DL_CALL(glColor3f)(0.0f, 0.0f, 1.0f);
    DL_CALL(glEndList)();
    DL_CALL(glNewList)(11u, V9X_GL_COMPILE);
    DL_CALL(glPopMatrix)();
    DL_CALL(glEndList)();
    DL_CALL(glNewList)(12u, V9X_GL_COMPILE);
    DL_CALL(glCallList)(11u);
    DL_CALL(glPopMatrix)();
    DL_CALL(glEndList)();
    /* glListBase and glCallLists compile like any command: the offsets
     * are copied, and the base is the one in force when replayed. */
    DL_CALL(glNewList)(20u, V9X_GL_COMPILE);
    DL_CALL(glListBase)(10u);
    DL_CALL(glCallLists)(3, DL_UNSIGNED_BYTE, offsets);
    DL_CALL(glEndList)();
    DCHECK(rec_called_count == 0u);
    offsets[0] = 99u;
    DL_CALL(glCallList)(20u);
    DCHECK(rec_called_count == 5u);
    DCHECK(rec_called[0] == 20u && rec_called[1] == 10u &&
           rec_called[2] == 12u && rec_called[3] == 11u &&
           rec_called[4] == 11u);
    DCHECK(rec_color3f_calls == 1u && rec_pop_calls == 3u);
    DCHECK(dl_lists.base == 10u);
    /* A list that calls itself stops at 64 deep (5.4). */
    DL_CALL(glNewList)(30u, V9X_GL_COMPILE);
    DL_CALL(glPopMatrix)();
    DL_CALL(glCallList)(30u);
    DL_CALL(glEndList)();
    rec_pop_calls = 0u;
    DL_CALL(glCallList)(30u);
    DCHECK(rec_pop_calls == V9X_GL_DLIST_NESTING);
    DCHECK(dl_lists.depth == 0ul);
}

static void test_many_lists(void)
{
    GLuint first;
    GLuint i;

    dl_reset();
    first = DL_CALL(glGenLists)(2000);
    for (i = 0u; i < 2000u; ++i) {
        DL_CALL(glNewList)(first + i, V9X_GL_COMPILE);
        DL_CALL(glLightf)(DL_LIGHT0, 0x1205u, (GLfloat)i);
        DL_CALL(glEndList)();
    }
    DL_CALL(glCallList)(first + 1234u);
    DCHECK(rec_lightf_calls == 1u && rec_lightf == 1234.0f);
    DL_CALL(glCallList)(first + 1999u);
    DCHECK(rec_lightf_calls == 2u && rec_lightf == 1999.0f);
    DCHECK(dl_state.error == V9X_GL_NO_ERROR);
}

static v9x_u32 long_seen_slot;
static v9x_u32 long_seen_words;
static int long_seen_right;
static unsigned int long_seen_nodes;

static void long_node(void *user, v9x_u32 slot, const v9x_u32 *args,
                      v9x_u32 words)
{
    v9x_u32 i;

    (void)user;
    ++long_seen_nodes;
    long_seen_slot = slot;
    long_seen_words = words;
    if (slot == 310ul && words != 70000ul) {
        long_seen_right = 0;
    }
    for (i = 0ul; i < words; ++i) {
        if (args[i] != i * 3ul) {
            long_seen_right = 0;
        }
    }
}

/* A node past the header's 16-bit length, between two short ones: the
 * long form carries its length in the next word. */
static void test_long_node(void)
{
    v9x_u32 *args;
    v9x_u32 i;

    dl_reset();
    v9x_gl_dlists_new(&dl_state, &dl_lists, 1u, V9X_GL_COMPILE);
    DCHECK(v9x_gl_dlists_node(&dl_lists, 7ul, 0ul) != 0);
    args = v9x_gl_dlists_node(&dl_lists, 310ul, 70000ul);
    DCHECK(args != 0);
    if (args != 0) {
        for (i = 0ul; i < 70000ul; ++i) {
            args[i] = i * 3ul;
        }
    }
    args = v9x_gl_dlists_node(&dl_lists, 8ul, 0xFFFFul);
    DCHECK(args != 0);
    if (args != 0) {
        for (i = 0ul; i < 0xFFFFul; ++i) {
            args[i] = i * 3ul;
        }
    }
    v9x_gl_dlists_end(&dl_state, &dl_lists);
    long_seen_nodes = 0u;
    long_seen_right = 1;
    DCHECK(v9x_gl_dlists_call(&dl_lists, 1u, long_node, 0) == 1);
    DCHECK(long_seen_nodes == 3u);
    DCHECK(long_seen_slot == 8ul && long_seen_words == 0xFFFFul &&
           long_seen_right);
    DCHECK(dl_state.error == V9X_GL_NO_ERROR);
}

unsigned int v9x_run_gl_dlist_tests(void)
{
    gl_dlist_failures = 0u;
    test_names();
    test_new_and_end_errors();
    test_helpers();
    test_compile_only();
    test_compile_and_execute();
    test_not_compiled();
    test_call_lists_and_nesting();
    test_many_lists();
    test_long_node();
    DCHECK(dl_special == 0u);
    if (gl_dlist_failures == 0u) {
        printf("gl_dlist: all tests passed\n");
    }
    return gl_dlist_failures;
}
