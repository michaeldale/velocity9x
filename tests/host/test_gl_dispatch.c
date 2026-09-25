/*
 * Tests for the generated OpenGL dispatch table.
 *
 * The table is generated from src\opengl\gl_entrypoints.psd1 by
 * scripts\lib\gl-dispatch.ps1 into gl_dispatch_gen.h beside this test. What
 * is asserted is the contract the ICD relies on: 336 slots in the order the
 * two reference headers agree on (Begin at 7, Viewport at 305 closing 1.0,
 * ArrayElement at 306 opening 1.1, PushClientAttrib at 335), no duplicate
 * names, no null entry, and stubs that report their own slot when called
 * through their typed pointer and return a zero of their type.
 */
#include <stdio.h>
#include <string.h>

#define V9X_GL_API __stdcall
static unsigned int gl_last_slot = 9999u;
static unsigned int gl_hook_calls = 0u;
#define V9X_GL_STUB_HOOK(slot) (gl_last_slot = (slot), ++gl_hook_calls)
#define V9X_GL_DEFINE_STUBS
#include "gl_dispatch_gen.h"

static unsigned int gl_failures = 0u;

#define GLCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++gl_failures; \
    } \
} while (0)

static const char *const gl_names[V9X_GL_SLOT_COUNT] = V9X_GL_SLOT_NAMES;
static const V9X_GL_PROC gl_table[V9X_GL_SLOT_COUNT] = V9X_GL_DISPATCH_INIT;

static unsigned int gl_slot_of(const char *name)
{
    unsigned int slot;

    for (slot = 0u; slot < V9X_GL_SLOT_COUNT; ++slot) {
        if (strcmp(gl_names[slot], name) == 0) {
            return slot;
        }
    }
    return V9X_GL_SLOT_COUNT;
}

static void test_order_and_shape(void)
{
    unsigned int slot;
    unsigned int other;

    /* The last 1.1 slot is the count less one; a constant comparison would
     * be folded and Watcom refuses the unreachable branch under -we. */
    GLCHECK(gl_slot_of("glPushClientAttrib") + 1u == V9X_GL_SLOT_COUNT);
    GLCHECK(gl_slot_of("glPushClientAttrib") == 335u);
    GLCHECK(strcmp(gl_names[0], "glNewList") == 0);
    GLCHECK(strcmp(gl_names[7], "glBegin") == 0);
    GLCHECK(strcmp(gl_names[305], "glViewport") == 0);
    GLCHECK(strcmp(gl_names[306], "glArrayElement") == 0);
    GLCHECK(strcmp(gl_names[307], "glBindTexture") == 0);
    GLCHECK(strcmp(gl_names[333], "glTexSubImage2D") == 0);
    GLCHECK(strcmp(gl_names[335], "glPushClientAttrib") == 0);
    for (slot = 0u; slot < V9X_GL_SLOT_COUNT; ++slot) {
        GLCHECK(gl_table[slot] != 0);
        GLCHECK(strncmp(gl_names[slot], "gl", 2) == 0);
        for (other = slot + 1u; other < V9X_GL_SLOT_COUNT; ++other) {
            if (strcmp(gl_names[slot], gl_names[other]) == 0) {
                printf("FAIL duplicate slot name %s at %u and %u\n",
                       gl_names[slot], slot, other);
                ++gl_failures;
            }
        }
    }
}

static void test_stubs_report_their_slot(void)
{
    unsigned int slot;

    slot = gl_slot_of("glBegin");
    GLCHECK(slot == 7u);
    gl_last_slot = 9999u;
    ((V9X_GL_PFN_glBegin)gl_table[slot])(4u);
    GLCHECK(gl_last_slot == 7u);

    slot = gl_slot_of("glVertex3f");
    GLCHECK(slot < V9X_GL_SLOT_COUNT);
    gl_last_slot = 9999u;
    ((V9X_GL_PFN_glVertex3f)gl_table[slot])(1.0f, 2.0f, 3.0f);
    GLCHECK(gl_last_slot == slot);

    slot = gl_slot_of("glColor3d");
    GLCHECK(slot < V9X_GL_SLOT_COUNT);
    gl_last_slot = 9999u;
    ((V9X_GL_PFN_glColor3d)gl_table[slot])(0.5, 0.25, 0.125);
    GLCHECK(gl_last_slot == slot);

    slot = gl_slot_of("glIsEnabled");
    GLCHECK(slot < V9X_GL_SLOT_COUNT);
    gl_last_slot = 9999u;
    GLCHECK(((V9X_GL_PFN_glIsEnabled)gl_table[slot])(0x0B71u) == 0);
    GLCHECK(gl_last_slot == slot);

    slot = gl_slot_of("glGetString");
    GLCHECK(slot < V9X_GL_SLOT_COUNT);
    GLCHECK(((V9X_GL_PFN_glGetString)gl_table[slot])(0x1F00u) == 0);

    slot = gl_slot_of("glGetError");
    GLCHECK(slot < V9X_GL_SLOT_COUNT);
    GLCHECK(((V9X_GL_PFN_glGetError)gl_table[slot])() == 0u);

    slot = gl_slot_of("glEndList");
    GLCHECK(slot == 1u);
    gl_last_slot = 9999u;
    ((V9X_GL_PFN_glEndList)gl_table[slot])();
    GLCHECK(gl_last_slot == 1u);
    GLCHECK(gl_hook_calls == 7u);
}

unsigned int v9x_run_gl_dispatch_tests(void)
{
    gl_failures = 0u;
    gl_hook_calls = 0u;
    test_order_and_shape();
    test_stubs_report_their_slot();
    if (gl_failures == 0u) {
        puts("PASS: generated OpenGL dispatch table");
    }
    return gl_failures;
}
