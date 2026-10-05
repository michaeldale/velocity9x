/*
 * V9XGL.DLL, the OpenGL installable client driver
 * (docs\plans\opengl-1.1-icd.md, Phase 3).
 *
 * The Drv* exports OPENGL32 calls, the contexts they create, and the GL
 * commands that are implemented so far: glClear through the HAL's render
 * interface, glFinish and glFlush likewise, and the state commands whose
 * whole effect is in src\opengl\gl_state.c (clear colour and depth,
 * viewport, scissor, masks, enables, errors). Every other slot of the
 * generated 336-entry table is a typed stub that records GL_INVALID_OPERATION
 * on the current context: a development placeholder, tracked in
 * docs\plans\opengl-1.1-requirements.md, never a silent success.
 *
 * Contexts come from DrvCreateLayerContext(hdc, 0) on 98SE (measured,
 * Phase 0.9) and DrvCreateContext elsewhere. One context is current per
 * thread, through the ICD's own TLS slot; a context current on another
 * thread cannot be made current here. A critical section guards the context
 * table and the device; GL commands on the current context take it too,
 * which is the plan's lock order: the ICD's section, then the Win16 mutex
 * inside each render-interface call, never the reverse.
 *
 * One pixel format: the desktop's 16 bits as the HAL's describe offers it
 * (565 or 555), double-buffered, 16-bit depth, no alpha, stencil or
 * accumulation. When the engine offers none - the ViRGE on a 565 desktop -
 * the ICD lists no formats and OPENGL32 serves its generic ones.
 *
 * Every Drv* call and every first call of a stub is appended to
 * C:\V9XDIAG\V9XGL.LOG with its process id. Per process, not shared.
 * Imports KERNEL32 and USER32 only; DirectDraw is loaded at run time.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "gl_state.h"
#include "gl_surface.h"
#include "gl_prim.h"
#include "gl_texture.h"
#include "gl_get.h"
#include "gl_pixels.h"
#include "gl_varray.h"

#define V9X_GL_API __stdcall
static void v9x_gl_stub_called(unsigned int slot);
#define V9X_GL_STUB_HOOK(slot) v9x_gl_stub_called(slot)
#define V9X_GL_DEFINE_STUBS
#include "gl_dispatch_gen.h"

/* The ICD-side names of the runtime's types (Mesa gldrv.h, ReactOS icd.h). */
typedef ULONG V9X_DHGLRC;
typedef struct v9x_glcltproctable {
    int cEntries;
    V9X_GL_PROC entries[V9X_GL_SLOT_COUNT];
} V9X_GLCLTPROCTABLE;
typedef void (__stdcall *V9X_PFN_SETPROCTABLE)(V9X_GLCLTPROCTABLE *table);

#define V9X_GL_VENDOR     0x1F00u
#define V9X_GL_RENDERER   0x1F01u
#define V9X_GL_VERSION    0x1F02u
#define V9X_GL_EXTENSIONS 0x1F03u

#define V9X_GL_CONTEXTS_MAX 16u

/*
 * Triangles held across glEnd while nothing they are drawn with changes:
 * Quake 2 ends a primitive every two or three triangles, and each batch
 * through the render interface costs a lock, a validation and a
 * submission whatever its size. The batch keeps its own copy of the
 * texture description (its levels point at the object's images, which do
 * not change before it is drawn: every texture command, clear, read,
 * finish, flush, swap and context change draws it first) and the texture's
 * name, so its hardware copy is found by name when it is drawn.
 */
typedef struct v9x_gl_pending {
    V9X_R3D_ABI_TEXTURE texture;
    V9X_R3D_ABI_LEVEL levels[V9X_GL_TEXTURE_LEVELS];
    /* The second unit (GL_SGIS_multitexture), storage NONE without one,
     * kept the same way, and its s and t two floats a vertex. */
    V9X_R3D_ABI_TEXTURE texture1;
    V9X_R3D_ABI_LEVEL levels1[V9X_GL_TEXTURE_LEVELS];
    GLuint texture1_name;
    GLfloat texcoords1[2u * 3u * V9X_R3D_ABI_BATCH_MAX];
    V9X_R3D_ABI_STATE state;
    GLuint texture_name;
    unsigned int targets;
    /* Whether the batch's fragment alpha is read (alpha test, a source
     * alpha blend factor): what a texture stored with alpha one may
     * substitute for it (v9x_gl_tex_as_1555). */
    int alpha_used;
    v9x_u32 triangles;
    V9X_R3D_ABI_VERTEX vertices[3u * V9X_R3D_ABI_BATCH_MAX];
} V9X_GL_PENDING;

typedef struct v9x_gl_context {
    int in_use;
    /* The thread it is current on, or zero. */
    DWORD owner;
    V9X_GL_DRAWABLE *drawable;
    int bound_once;
    V9X_GL_STATE state;
    V9X_GL_PIPELINE pipeline;
    V9X_GL_TEXTURES textures;
    /* Client state (2.8): the vertex arrays. */
    V9X_GL_ARRAYS arrays;
    V9X_GL_PENDING pending;
    /* The levels a batch's texture names, valid for the draw call, and its
     * second unit's. */
    V9X_R3D_ABI_LEVEL levels[V9X_GL_TEXTURE_LEVELS];
    V9X_R3D_ABI_LEVEL levels1[V9X_GL_TEXTURE_LEVELS];
} V9X_GL_CONTEXT;

static const char v9x_gl_build_id[] = "V9XGL build=" V9X_BUILD_ID;
static const char *const v9x_gl_slot_names[V9X_GL_SLOT_COUNT] = V9X_GL_SLOT_NAMES;
static V9X_GLCLTPROCTABLE v9x_gl_table = { V9X_GL_SLOT_COUNT, V9X_GL_DISPATCH_INIT };
static DWORD v9x_gl_slot_calls[V9X_GL_SLOT_COUNT];
static DWORD v9x_gl_sequence;

/* Texture copies made and refilled, failed batches by result, and the
 * time of the last periodic report. */
static DWORD v9x_gl_hw_creates;
static DWORD v9x_gl_hw_create_failures;
static DWORD v9x_gl_hw_uploads;
/* Of those, the ones that refilled only what changed. */
static DWORD v9x_gl_hw_partial_uploads;
static DWORD v9x_gl_hw_upload_kb;
/* Draws that sampled a square copy of a non-square or too-small image. */
static DWORD v9x_gl_hw_squared_draws;
/* Batches sent without an alpha test that could discard nothing. */
static DWORD v9x_gl_alpha_tests_dropped;
static DWORD v9x_gl_stub_total;
/* A squared copy's draw: the batch with s and t scaled to the square, and
 * the second unit's coordinates scaled to its own. */
static V9X_R3D_ABI_VERTEX v9x_gl_scaled_vertices[3u * V9X_R3D_ABI_BATCH_MAX];
static GLfloat v9x_gl_scaled_tex1[2u * 3u * V9X_R3D_ABI_BATCH_MAX];
/* Batches and triangles drawn with two units, and the SGIS entry points'
 * first calls (logged once each). */
static DWORD v9x_gl_mtex_batches;
static DWORD v9x_gl_mtex_triangles;
static int v9x_gl_mtex_select_seen;
static int v9x_gl_mtex_coord_seen;
#define V9X_GL_RESULT_SLOTS 10u
static DWORD v9x_gl_draw_failures[V9X_GL_RESULT_SLOTS];
static DWORD v9x_gl_failures_dumped;
static DWORD v9x_gl_report_last;
#define V9X_GL_REPORT_MS 10000ul

/*
 * Where a frame's time goes, per report interval: swaps (frames), and the
 * performance-counter ticks spent presenting, in the interface's draw and
 * in texture uploads. Only the low 32 bits of the counter are used; a
 * difference between two reads is right across one wrap, an hour at the
 * 1.19 MHz a Win98 PC counts at.
 */
static DWORD v9x_gl_swaps;
static DWORD v9x_gl_present_ticks;
static DWORD v9x_gl_upload_ticks;

static DWORD v9x_gl_ticks(void)
{
    LARGE_INTEGER now;

    if (!QueryPerformanceCounter(&now)) {
        return 0ul;
    }
    return now.LowPart;
}

/* Ticks as milliseconds without 64-bit arithmetic. */
static DWORD v9x_gl_ticks_ms(DWORD ticks)
{
    LARGE_INTEGER frequency;
    DWORD per_ms;

    if (!QueryPerformanceFrequency(&frequency) || frequency.HighPart != 0 ||
        frequency.LowPart < 1000ul) {
        return 0ul;
    }
    per_ms = frequency.LowPart / 1000ul;
    return ticks / per_ms;
}

/*
 * Where an OpenGL frame's time goes, by TSC (2026-10-01): the vertex
 * entry points (which include any flush a full batch triggers), every
 * flush, and inside a flush the window re-bind and the render
 * interface's draw; plus glBegin/glEnd and the swap. Half-Life's OpenGL
 * renderer ran at 40% of its Direct3D one on the netbook with the HAL
 * only a quarter of the frame; these say where the rest is.
 *
 * RDTSC as opcode bytes, like the HAL's (ddhal_internal.h), and only
 * after CPUID says the CPU has a TSC, with EFLAGS.ID probed first so a
 * CPU without CPUID is never asked. Each bucket is a lo/hi pair of
 * low-dword deltas, every one under a batch or a frame. The wall bucket
 * advances at every flush and swap, never seconds apart while drawing,
 * so it does not wrap either; the report converts with it.
 */
static DWORD v9x_gl_rdtsc_low(void);
#pragma aux v9x_gl_rdtsc_low = 0x0f 0x31 value [eax] modify exact [eax edx];
static DWORD v9x_gl_cpuid_present(void);
#pragma aux v9x_gl_cpuid_present = \
    0x9c 0x58 0x8b 0xc8 0x35 0x00 0x00 0x20 0x00 0x50 0x9d 0x9c 0x58 \
    0x51 0x9d 0x33 0xc1 0xc1 0xe8 0x15 0x83 0xe0 0x01 \
    value [eax] modify exact [eax ecx];
static DWORD v9x_gl_cpuid1_edx(void);
#pragma aux v9x_gl_cpuid1_edx = \
    0x53 0xb8 0x01 0x00 0x00 0x00 0x0f 0xa2 0x5b \
    value [edx] modify exact [eax ecx edx];
#define V9X_GL_CPUID1_EDX_TSC 0x00000010ul
/* A non-negative double as a DWORD, through fistp as gl_prim.c does: no
 * C runtime, so no __CHP. */
static long v9x_gl_tsc_to_long(double value);
#pragma aux v9x_gl_tsc_to_long = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];

#define V9X_GL_TSC_WALL            0u
#define V9X_GL_TSC_VERTEX          1u
#define V9X_GL_TSC_FLUSH           2u
#define V9X_GL_TSC_FLUSH_IN_VERTEX 3u
#define V9X_GL_TSC_BIND            4u
#define V9X_GL_TSC_IFACE           5u
#define V9X_GL_TSC_BEGINEND        6u
#define V9X_GL_TSC_SWAP            7u
#define V9X_GL_TSC_SINK            8u
#define V9X_GL_TSC_SINK_PREP       9u
/* The batch sink and Begin split further (2026-10-01): glBegin alone; in
 * the sink, the held batch drawn because this one differs or it is full,
 * the hold and vertex copy, and the prep's three parts. */
#define V9X_GL_TSC_BEGIN           10u
#define V9X_GL_TSC_SINK_FLUSH      11u
#define V9X_GL_TSC_SINK_COPY       12u
#define V9X_GL_TSC_PREP_TEXTURE    13u
#define V9X_GL_TSC_PREP_STATE      14u
#define V9X_GL_TSC_PREP_SAME       15u
/* The hardware copy (create, evict, upload) and render-interface draws
 * whose texture went to the CPU instead (2026-10-02, Serious Sam). */
#define V9X_GL_TSC_HWTEX           16u
#define V9X_GL_TSC_IFACE_CPU       17u
/* Entry points by kind (2026-10-05, Half-Life under multitexture): each
 * texture command whole, the held batch it draws first, the SGIS calls and
 * glEnable/glDisable/glIsEnabled. Each with a call count (entry line). */
#define V9X_GL_TSC_TEXSUB          18u
#define V9X_GL_TSC_TEXIMAGE        19u
#define V9X_GL_TSC_TEXBIND         20u
#define V9X_GL_TSC_TEXPARAM        21u
#define V9X_GL_TSC_TEXENV          22u
#define V9X_GL_TSC_TEXOTHER        23u
#define V9X_GL_TSC_TEXFLUSH        24u
#define V9X_GL_TSC_SGIS_SELECT     25u
#define V9X_GL_TSC_SGIS_COORD      26u
#define V9X_GL_TSC_ENABLE          27u
#define V9X_GL_TSC_BUCKETS         28u

/* Why v9x_gl_hw_texture answered no, counted per call (2026-10-02) and
 * logged as the hwno line: the batch is then drawn from its CPU copy. */
#define V9X_GL_HWNO_NOT_CPU      0u   /* no CPU image, or no hw textures */
#define V9X_GL_HWNO_SQUARE_SIDE  1u   /* no square copy fits the limits  */
#define V9X_GL_HWNO_TOO_BIG      2u   /* past hw_texture_size_max        */
#define V9X_GL_HWNO_FORMAT       3u   /* a format the engine cannot take */
#define V9X_GL_HWNO_NO_OBJECT    4u
#define V9X_GL_HWNO_NO_RECORD    5u   /* HeapAlloc of the record failed  */
#define V9X_GL_HWNO_TABLE_FULL   6u   /* V9X_GL_HWTEX_MAX live records   */
#define V9X_GL_HWNO_UNUSABLE     7u   /* an earlier upload failed        */
#define V9X_GL_HWNO_BACKOFF      8u   /* waiting V9X_GL_HWTEX_RETRY uses */
#define V9X_GL_HWNO_MAKE_FAILED  9u   /* no surface even after evicting  */
#define V9X_GL_HWNO_SQUARE_ALLOC 10u
#define V9X_GL_HWNO_UPLOAD       11u
#define V9X_GL_HWNO_COUNT        12u
static DWORD v9x_gl_hwno[V9X_GL_HWNO_COUNT];
static DWORD v9x_gl_count_sinks;
static DWORD v9x_gl_tsc_state;  /* 0 unknown, 1 usable, 2 absent */
static DWORD v9x_gl_tsc[V9X_GL_TSC_BUCKETS * 2u];
/* Calls per bucket, for the entry line; reset with the buckets. */
static DWORD v9x_gl_tsc_calls[V9X_GL_TSC_BUCKETS];
/* The vertex pipeline's stage profile (V9X_GL_PIPELINE.profile), shared by
 * every context of the process and reported and cleared with the tsc line. */
static v9x_u32 v9x_gl_prim_profile[V9X_GL_PRIM_PROF_DWORDS];
static DWORD v9x_gl_tsc_wall_last;
static DWORD v9x_gl_tsc_wall_valid;
static DWORD v9x_gl_in_vertex;
static DWORD v9x_gl_count_vertices;
static DWORD v9x_gl_count_flushes;
static DWORD v9x_gl_count_draws;

static int v9x_gl_tsc_usable(void)
{
    if (v9x_gl_tsc_state == 0ul) {
        v9x_gl_tsc_state = 2ul;
        if (v9x_gl_cpuid_present() != 0ul &&
            (v9x_gl_cpuid1_edx() & V9X_GL_CPUID1_EDX_TSC) != 0ul) {
            v9x_gl_tsc_state = 1ul;
        }
    }
    return v9x_gl_tsc_state == 1ul;
}

static DWORD v9x_gl_tsc_begin(void)
{
    return v9x_gl_tsc_usable() ? v9x_gl_rdtsc_low() : 0ul;
}

static void v9x_gl_tsc_add(unsigned int bucket, DWORD delta)
{
    v9x_gl_tsc[bucket * 2u] += delta;
    if (v9x_gl_tsc[bucket * 2u] < delta) {
        ++v9x_gl_tsc[bucket * 2u + 1u];
    }
}

static void v9x_gl_tsc_end(unsigned int bucket, DWORD started)
{
    if (v9x_gl_tsc_usable()) {
        v9x_gl_tsc_add(bucket, v9x_gl_rdtsc_low() - started);
    }
}

/* Advance the wall bucket to now. */
static void v9x_gl_tsc_wall(void)
{
    DWORD now;

    if (!v9x_gl_tsc_usable()) {
        return;
    }
    now = v9x_gl_rdtsc_low();
    if (v9x_gl_tsc_wall_valid) {
        v9x_gl_tsc_add(V9X_GL_TSC_WALL, now - v9x_gl_tsc_wall_last);
    }
    v9x_gl_tsc_wall_last = now;
    v9x_gl_tsc_wall_valid = 1ul;
}

/* A bucket in milliseconds of the interval whose wall cycles and
 * GetTickCount milliseconds are given. */
static DWORD v9x_gl_tsc_ms(unsigned int bucket, double wall,
                           DWORD interval_ms)
{
    double cycles = (double)v9x_gl_tsc[bucket * 2u + 1u] * 4294967296.0 +
                    (double)v9x_gl_tsc[bucket * 2u];

    if (wall <= 0.0) {
        return 0ul;
    }
    return (DWORD)v9x_gl_tsc_to_long(cycles * (double)interval_ms / wall);
}

/* The same, for a vertex pipeline stage. */
static DWORD v9x_gl_prim_profile_ms(unsigned int stage, double wall,
                                    DWORD interval_ms)
{
    double cycles =
        (double)v9x_gl_prim_profile[stage * 2u + 1u] * 4294967296.0 +
        (double)v9x_gl_prim_profile[stage * 2u];

    if (wall <= 0.0) {
        return 0ul;
    }
    return (DWORD)v9x_gl_tsc_to_long(cycles * (double)interval_ms / wall);
}

static double v9x_gl_tsc_wall_cycles(void)
{
    v9x_gl_tsc_wall();
    return (double)v9x_gl_tsc[V9X_GL_TSC_WALL * 2u + 1u] * 4294967296.0 +
           (double)v9x_gl_tsc[V9X_GL_TSC_WALL * 2u];
}

static DWORD v9x_gl_tsc_bucket_ms(unsigned int bucket, DWORD interval_ms)
{
    if (!v9x_gl_tsc_usable()) {
        return 0ul;
    }
    return v9x_gl_tsc_ms(bucket, v9x_gl_tsc_wall_cycles(), interval_ms);
}

static void v9x_gl_tsc_log(DWORD interval_ms, DWORD swaps)
{
    /* The entry line is the longest: about 200 characters of format and
     * twenty numbers of up to ten digits. */
    char text[480];
    double wall;
    unsigned int i;

    if (!v9x_gl_tsc_usable()) {
        return;
    }
    wall = v9x_gl_tsc_wall_cycles();
    wsprintfA(text, "tsc frames=%lu wall-ms=%lu vertex-ms=%lu "
              "flush-in-vertex-ms=%lu flush-other-ms=%lu bind-ms=%lu "
              "iface-ms=%lu beginend-ms=%lu swap-ms=%lu vertices=%lu "
              "flushes=%lu draws=%lu sink-ms=%lu sink-prep-ms=%lu sinks=%lu",
              swaps,
              v9x_gl_tsc_ms(V9X_GL_TSC_WALL, wall, interval_ms),
              v9x_gl_tsc_ms(V9X_GL_TSC_VERTEX, wall, interval_ms),
              v9x_gl_tsc_ms(V9X_GL_TSC_FLUSH_IN_VERTEX, wall, interval_ms),
              v9x_gl_tsc_ms(V9X_GL_TSC_FLUSH, wall, interval_ms),
              v9x_gl_tsc_ms(V9X_GL_TSC_BIND, wall, interval_ms),
              v9x_gl_tsc_ms(V9X_GL_TSC_IFACE, wall, interval_ms),
              v9x_gl_tsc_ms(V9X_GL_TSC_BEGINEND, wall, interval_ms),
              v9x_gl_tsc_ms(V9X_GL_TSC_SWAP, wall, interval_ms),
              v9x_gl_count_vertices, v9x_gl_count_flushes,
              v9x_gl_count_draws,
              v9x_gl_tsc_ms(V9X_GL_TSC_SINK, wall, interval_ms),
              v9x_gl_tsc_ms(V9X_GL_TSC_SINK_PREP, wall, interval_ms),
              v9x_gl_count_sinks);
    v9x_gl_log(text);
    wsprintfA(text, "sink begin-ms=%lu flush-ms=%lu copy-ms=%lu "
              "prep-texture-ms=%lu prep-state-ms=%lu prep-same-ms=%lu",
              v9x_gl_tsc_ms(V9X_GL_TSC_BEGIN, wall, interval_ms),
              v9x_gl_tsc_ms(V9X_GL_TSC_SINK_FLUSH, wall, interval_ms),
              v9x_gl_tsc_ms(V9X_GL_TSC_SINK_COPY, wall, interval_ms),
              v9x_gl_tsc_ms(V9X_GL_TSC_PREP_TEXTURE, wall, interval_ms),
              v9x_gl_tsc_ms(V9X_GL_TSC_PREP_STATE, wall, interval_ms),
              v9x_gl_tsc_ms(V9X_GL_TSC_PREP_SAME, wall, interval_ms));
    v9x_gl_log(text);
    wsprintfA(text, "hwno hwtex-ms=%lu iface-cpu-ms=%lu not-cpu=%lu "
              "square-side=%lu too-big=%lu format=%lu no-object=%lu "
              "no-record=%lu table-full=%lu unusable=%lu backoff=%lu "
              "make-failed=%lu square-alloc=%lu upload=%lu",
              v9x_gl_tsc_ms(V9X_GL_TSC_HWTEX, wall, interval_ms),
              v9x_gl_tsc_ms(V9X_GL_TSC_IFACE_CPU, wall, interval_ms),
              v9x_gl_hwno[0], v9x_gl_hwno[1], v9x_gl_hwno[2],
              v9x_gl_hwno[3], v9x_gl_hwno[4], v9x_gl_hwno[5],
              v9x_gl_hwno[6], v9x_gl_hwno[7], v9x_gl_hwno[8],
              v9x_gl_hwno[9], v9x_gl_hwno[10], v9x_gl_hwno[11]);
    v9x_gl_log(text);
    for (i = 0u; i < V9X_GL_HWNO_COUNT; ++i) {
        v9x_gl_hwno[i] = 0ul;
    }
    wsprintfA(text, "prim transform-ms=%lu inside-ms=%lu window-ms=%lu "
              "assemble-ms=%lu history-ms=%lu fast=%lu clipped=%lu "
              "culled=%lu",
              v9x_gl_prim_profile_ms(V9X_GL_PRIM_PROF_TRANSFORM, wall,
                                     interval_ms),
              v9x_gl_prim_profile_ms(V9X_GL_PRIM_PROF_INSIDE, wall,
                                     interval_ms),
              v9x_gl_prim_profile_ms(V9X_GL_PRIM_PROF_WINDOW, wall,
                                     interval_ms),
              v9x_gl_prim_profile_ms(V9X_GL_PRIM_PROF_ASSEMBLE, wall,
                                     interval_ms),
              v9x_gl_prim_profile_ms(V9X_GL_PRIM_PROF_HISTORY, wall,
                                     interval_ms),
              v9x_gl_prim_profile[V9X_GL_PRIM_PROF_FAST],
              v9x_gl_prim_profile[V9X_GL_PRIM_PROF_CLIPPED],
              v9x_gl_prim_profile[V9X_GL_PRIM_PROF_CULLED]);
    v9x_gl_log(text);
    for (i = 0u; i < V9X_GL_PRIM_PROF_DWORDS; ++i) {
        v9x_gl_prim_profile[i] = 0ul;
    }
    wsprintfA(text, "entry texsub-ms=%lu/%lu teximage-ms=%lu/%lu "
              "bind-ms=%lu/%lu param-ms=%lu/%lu env-ms=%lu/%lu "
              "other-ms=%lu/%lu tex-flush-ms=%lu/%lu select-ms=%lu/%lu "
              "mtexcoord-ms=%lu/%lu enable-ms=%lu/%lu",
              v9x_gl_tsc_ms(V9X_GL_TSC_TEXSUB, wall, interval_ms),
              v9x_gl_tsc_calls[V9X_GL_TSC_TEXSUB],
              v9x_gl_tsc_ms(V9X_GL_TSC_TEXIMAGE, wall, interval_ms),
              v9x_gl_tsc_calls[V9X_GL_TSC_TEXIMAGE],
              v9x_gl_tsc_ms(V9X_GL_TSC_TEXBIND, wall, interval_ms),
              v9x_gl_tsc_calls[V9X_GL_TSC_TEXBIND],
              v9x_gl_tsc_ms(V9X_GL_TSC_TEXPARAM, wall, interval_ms),
              v9x_gl_tsc_calls[V9X_GL_TSC_TEXPARAM],
              v9x_gl_tsc_ms(V9X_GL_TSC_TEXENV, wall, interval_ms),
              v9x_gl_tsc_calls[V9X_GL_TSC_TEXENV],
              v9x_gl_tsc_ms(V9X_GL_TSC_TEXOTHER, wall, interval_ms),
              v9x_gl_tsc_calls[V9X_GL_TSC_TEXOTHER],
              v9x_gl_tsc_ms(V9X_GL_TSC_TEXFLUSH, wall, interval_ms),
              v9x_gl_tsc_calls[V9X_GL_TSC_TEXFLUSH],
              v9x_gl_tsc_ms(V9X_GL_TSC_SGIS_SELECT, wall, interval_ms),
              v9x_gl_tsc_calls[V9X_GL_TSC_SGIS_SELECT],
              v9x_gl_tsc_ms(V9X_GL_TSC_SGIS_COORD, wall, interval_ms),
              v9x_gl_tsc_calls[V9X_GL_TSC_SGIS_COORD],
              v9x_gl_tsc_ms(V9X_GL_TSC_ENABLE, wall, interval_ms),
              v9x_gl_tsc_calls[V9X_GL_TSC_ENABLE]);
    v9x_gl_log(text);
    for (i = 0u; i < V9X_GL_TSC_BUCKETS * 2u; ++i) {
        v9x_gl_tsc[i] = 0ul;
    }
    for (i = 0u; i < V9X_GL_TSC_BUCKETS; ++i) {
        v9x_gl_tsc_calls[i] = 0ul;
    }
    v9x_gl_count_vertices = 0ul;
    v9x_gl_count_flushes = 0ul;
    v9x_gl_count_draws = 0ul;
    v9x_gl_count_sinks = 0ul;
}
#define V9X_GL_FAILURES_DUMPED_MAX 6ul

static int v9x_gl_overrides_installed;
static CRITICAL_SECTION v9x_gl_lock;
static DWORD v9x_gl_tls = 0xfffffffful;
static V9X_GL_CONTEXT v9x_gl_contexts[V9X_GL_CONTEXTS_MAX];

/* One line, appended: sequence, process, text. Open/append/close each time
 * so a process that dies mid-run leaves everything it logged. */
void v9x_gl_log(const char *text)
{
    HANDLE file;
    /* The longest text (the tsc entry line, under 480) plus the prefix. */
    char line[520];
    DWORD written;
    int length;

    file = CreateFileA(V9X_DIAG_GL_LOG, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    SetFilePointer(file, 0, 0, FILE_END);
    length = wsprintfA(line, "%lu pid=%08lX %s\r\n", ++v9x_gl_sequence,
                       GetCurrentProcessId(), text);
    WriteFile(file, line, (DWORD)length, &written, 0);
    CloseHandle(file);
}

void v9x_gl_log3(const char *format, v9x_u32 a, v9x_u32 b, v9x_u32 c)
{
    char text[256];

    wsprintfA(text, format, a, b, c);
    v9x_gl_log(text);
}

static V9X_GL_CONTEXT *v9x_gl_current(void)
{
    if (v9x_gl_tls == 0xfffffffful) {
        return 0;
    }
    return (V9X_GL_CONTEXT *)TlsGetValue(v9x_gl_tls);
}

static void v9x_gl_stub_called(unsigned int slot)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_error(&context->state, V9X_GL_INVALID_OPERATION);
    }
    if (slot >= V9X_GL_SLOT_COUNT) {
        return;
    }
    ++v9x_gl_stub_total;
    if (v9x_gl_slot_calls[slot]++ == 0ul) {
        char text[128];

        wsprintfA(text, "stub first-call slot=%u %s", slot,
                  v9x_gl_slot_names[slot]);
        v9x_gl_log(text);
    }
}

/* ---- GL commands --------------------------------------------------- */

/* Whether the engine combines two textures in a draw, which is when
 * GL_SGIS_multitexture is offered (docs\plans\gen3-sgis-multitexture.md). */
static int v9x_gl_two_units(void)
{
    const V9X_R3D_ABI_DESCRIBE *description = v9x_gl_device_description();

    return description != 0 && description->texture_units >= 2ul;
}

static const GLubyte * V9X_GL_API v9x_gl_get_string(GLenum name)
{
    const V9X_R3D_ABI_DESCRIBE *description = v9x_gl_device_description();

    if (v9x_gl_current() == 0) {
        return 0;
    }
    switch (name) {
    case V9X_GL_VENDOR:
        return (const GLubyte *)"Velocity9x";
    case V9X_GL_RENDERER:
        return (const GLubyte *)(description != 0 ? description->renderer
                                                  : "Velocity9x");
    case V9X_GL_VERSION:
        return (const GLubyte *)"1.1.0";
    case V9X_GL_EXTENSIONS:
        /* With the trailing space: GLQuake looks for the name followed by
         * one (gl_vidnt.c:580), so it must not end the string bare. */
        return (const GLubyte *)(v9x_gl_two_units()
                                     ? "GL_SGIS_multitexture " : "");
    default:
        v9x_gl_state_error(&v9x_gl_current()->state, V9X_GL_INVALID_ENUM);
        return 0;
    }
}

static GLenum V9X_GL_API v9x_gl_get_error(void)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    return context != 0 ? v9x_gl_state_get_error(&context->state)
                        : V9X_GL_NO_ERROR;
}

static void V9X_GL_API v9x_gl_clear_color(GLclampf red, GLclampf green,
                                          GLclampf blue, GLclampf alpha)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_clear_color(&context->state, red, green, blue, alpha);
    }
}

static void V9X_GL_API v9x_gl_clear_depth(GLclampd depth)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_clear_depth(&context->state, depth);
    }
}

static void V9X_GL_API v9x_gl_viewport(GLint x, GLint y, GLsizei width,
                                       GLsizei height)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_viewport(&context->state, x, y, width, height);
    }
}

static void V9X_GL_API v9x_gl_scissor(GLint x, GLint y, GLsizei width,
                                      GLsizei height)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_scissor(&context->state, x, y, width, height);
    }
}

static void V9X_GL_API v9x_gl_color_mask(GLboolean red, GLboolean green,
                                         GLboolean blue, GLboolean alpha)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_color_mask(&context->state, red, green, blue, alpha);
    }
}

static void V9X_GL_API v9x_gl_depth_mask(GLboolean flag)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_state_depth_mask(&context->state, flag);
    }
}

static void V9X_GL_API v9x_gl_enable(GLenum cap)
{
    DWORD started = v9x_gl_tsc_begin();
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0 &&
        !v9x_gl_tex_enable_selected(&context->textures, cap, 1)) {
        v9x_gl_state_enable(&context->state, cap, 1);
    }
    ++v9x_gl_tsc_calls[V9X_GL_TSC_ENABLE];
    v9x_gl_tsc_end(V9X_GL_TSC_ENABLE, started);
}

static void V9X_GL_API v9x_gl_disable(GLenum cap)
{
    DWORD started = v9x_gl_tsc_begin();
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0 &&
        !v9x_gl_tex_enable_selected(&context->textures, cap, 0)) {
        v9x_gl_state_enable(&context->state, cap, 0);
    }
    ++v9x_gl_tsc_calls[V9X_GL_TSC_ENABLE];
    v9x_gl_tsc_end(V9X_GL_TSC_ENABLE, started);
}

static GLboolean V9X_GL_API v9x_gl_is_enabled(GLenum cap)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    GLboolean enabled;

    if (context == 0) {
        return 0;
    }
    /* The client arrays are glIsEnabled's too (2.8), but not glEnable's. */
    if (v9x_gl_arrays_is_enabled(&context->arrays, cap, &enabled) ||
        v9x_gl_tex_is_enabled_selected(&context->textures, cap, &enabled)) {
        return enabled;
    }
    return v9x_gl_state_is_enabled(&context->state, cap);
}

/*
 * Re-bind the current context's window: the client area may have changed
 * size, and the surfaces with it. The first bind of a context also sets its
 * viewport and scissor (gl_state.c).
 */
static V9X_GL_DRAWABLE *v9x_gl_bind_window(V9X_GL_CONTEXT *context,
                                           HWND window)
{
    V9X_GL_DRAWABLE *drawable;
    v9x_u32 width;
    v9x_u32 height;
    int resized;

    drawable = v9x_gl_drawable_bind(window, &resized);
    if (drawable == 0) {
        return 0;
    }
    v9x_gl_drawable_size(drawable, &width, &height);
    v9x_gl_state_drawable(&context->state, width, height,
                          v9x_gl_device_format(), !context->bound_once);
    context->bound_once = 1;
    context->drawable = drawable;
    return drawable;
}

/* The window a drawable belongs to is the one DrvSetContext was handed; it
 * is kept on the context so a command can re-bind after a resize. */
static HWND v9x_gl_context_window(const V9X_GL_CONTEXT *context);

/* describe again after a mode change, dropping every texture copy. */
static int v9x_gl_redescribe_all(void);
/* Draw the context's held batch, if any. */
static void v9x_gl_pending_flush(V9X_GL_CONTEXT *context);
/* The surface a colour buffer (V9X_GL_DRAW_*) is drawn in. */
static void *v9x_gl_drawable_target(V9X_GL_DRAWABLE *drawable,
                                    unsigned int which);

static void V9X_GL_API v9x_gl_clear(GLbitfield mask)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();
    const V9X_R3D_INTERFACE *iface = v9x_gl_device_interface();
    const V9X_R3D_ABI_DESCRIBE *description = v9x_gl_device_description();
    V9X_GL_DRAWABLE *drawable;
    V9X_GL_CLEAR_PLAN plan;
    V9X_R3D_ABI_CLEAR clear;
    V9X_R3D_ABI_RECT rect;
    v9x_u32 result;
    unsigned int targets;
    unsigned int pass;
    v9x_u32 depth_pending;

    if (context == 0 || iface == 0) {
        return;
    }
    v9x_gl_pending_flush(context);
    EnterCriticalSection(&v9x_gl_lock);
    drawable = v9x_gl_bind_window(context, v9x_gl_context_window(context));
    if (drawable == 0) {
        v9x_gl_state_error(&context->state, V9X_GL_OUT_OF_MEMORY);
        LeaveCriticalSection(&v9x_gl_lock);
        return;
    }
    if (!v9x_gl_state_clear(&context->state, mask, 1, &plan)) {
        LeaveCriticalSection(&v9x_gl_lock);
        return;
    }
    rect.left = plan.rect_left;
    rect.top = plan.rect_top;
    rect.right = plan.rect_right;
    rect.bottom = plan.rect_bottom;
    clear.struct_bytes = sizeof(clear);
    clear.color_value = plan.color_value;
    clear.depth_value = plan.depth_value;
    clear.write_mask = plan.write_mask;
    clear.rects = &rect;
    clear.rect_count = 1ul;

    /*
     * Each colour buffer the draw buffer names (4.2.3 clears those), the
     * shared depth buffer with the first of them only. With GL_NONE the
     * colour is not cleared but the depth still is, against the back
     * buffer's surface, which the interface needs as a target.
     */
    targets = v9x_gl_state_draw_targets(&context->state);
    depth_pending = plan.clear_depth;
    for (pass = 0u; pass < 2u; ++pass) {
        unsigned int which = pass == 0u ? V9X_GL_DRAW_BACK
                                        : V9X_GL_DRAW_FRONT;
        int colour = (targets & which) != 0u && plan.clear_color != 0ul;

        if (!colour && !(pass == 1u && depth_pending != 0ul)) {
            continue;
        }
        clear.generation = description->generation;
        clear.target.surface = colour
            ? v9x_gl_drawable_target(drawable, which)
            : v9x_gl_drawable_back(drawable);
        clear.depth.surface = v9x_gl_drawable_depth(drawable);
        clear.clear_color = colour ? 1ul : 0ul;
        clear.clear_depth = depth_pending;
        clear.write_depth = depth_pending;
        if (clear.target.surface == 0) {
            v9x_gl_state_error(&context->state, V9X_GL_OUT_OF_MEMORY);
            continue;
        }
        if (clear.clear_color == 0ul && clear.clear_depth == 0ul) {
            continue;
        }
        result = iface->clear(&clear);
        if (result == V9X_R3D_RESULT_STALE && v9x_gl_redescribe_all()) {
            /* A mode change: new generation, and the surfaces made again. */
            drawable = v9x_gl_bind_window(context,
                                          v9x_gl_context_window(context));
            if (drawable == 0) {
                break;
            }
            clear.generation = description->generation;
            clear.target.surface = colour
                ? v9x_gl_drawable_target(drawable, which)
                : v9x_gl_drawable_back(drawable);
            clear.depth.surface = v9x_gl_drawable_depth(drawable);
            result = clear.target.surface != 0 ? iface->clear(&clear)
                                               : V9X_R3D_RESULT_NO_MEMORY;
        }
        if (result != V9X_R3D_RESULT_OK) {
            v9x_gl_log3("glClear result=%lu mask=%08lX", result,
                        (v9x_u32)mask, 0ul);
            continue;
        }
        depth_pending = 0ul;
        if (colour && which == V9X_GL_DRAW_FRONT) {
            v9x_gl_drawable_show_front(drawable);
        }
    }
    LeaveCriticalSection(&v9x_gl_lock);
}

static void V9X_GL_API v9x_gl_finish(void)
{
    const V9X_R3D_INTERFACE *iface = v9x_gl_device_interface();
    const V9X_R3D_ABI_DESCRIBE *description = v9x_gl_device_description();
    v9x_u32 result;

    if (v9x_gl_current() == 0 || iface == 0) {
        return;
    }
    v9x_gl_pending_flush(v9x_gl_current());
    result = iface->finish(description->generation);
    if (result != V9X_R3D_RESULT_OK) {
        v9x_gl_log3("glFinish result=%lu", result, 0ul, 0ul);
    }
}

/*
 * glReadPixels (4.3.2) from the back buffer. glEnd hands every batched
 * triangle to the interface and the call is an error inside glBegin, so
 * nothing is held here; finish makes the submitted work complete, and the
 * Lock's HAL side drains again before the CPU reads. READ_BUFFER FRONT
 * reads the back buffer too: after SwapBuffers the two hold the same
 * image, and the window's pixels on the primary may be covered.
 */
static void V9X_GL_API v9x_gl_read_pixels(GLint x, GLint y, GLsizei width,
                                          GLsizei height, GLenum format,
                                          GLenum type, GLvoid *pixels)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();
    const V9X_R3D_INTERFACE *iface = v9x_gl_device_interface();
    const V9X_R3D_ABI_DESCRIBE *description = v9x_gl_device_description();
    V9X_GL_DRAWABLE *drawable;
    V9X_GL_READ_PLAN plan;
    const void *surface;
    v9x_u32 pitch;
    v9x_u32 surface_width;
    v9x_u32 surface_height;
    v9x_u32 result;
    int front;

    if (context == 0 || iface == 0) {
        return;
    }
    if (!v9x_gl_read_plan(&context->state, &context->textures, x, y, width,
                          height, format, type, &plan)) {
        return;
    }
    v9x_gl_pending_flush(context);
    EnterCriticalSection(&v9x_gl_lock);
    drawable = v9x_gl_bind_window(context, v9x_gl_context_window(context));
    if (drawable == 0) {
        v9x_gl_state_error(&context->state, V9X_GL_OUT_OF_MEMORY);
        LeaveCriticalSection(&v9x_gl_lock);
        return;
    }
    result = iface->finish(description->generation);
    if (result != V9X_R3D_RESULT_OK) {
        v9x_gl_log3("glReadPixels finish result=%lu", result, 0ul, 0ul);
    }
    front = v9x_gl_state_reads_front(&context->state) &&
            v9x_gl_drawable_front_existing(drawable) != 0;
    if (!v9x_gl_drawable_lock(drawable, front, &surface, &pitch)) {
        LeaveCriticalSection(&v9x_gl_lock);
        return;
    }
    v9x_gl_drawable_size(drawable, &surface_width, &surface_height);
    v9x_gl_read_convert(&plan, surface, pitch, surface_width, surface_height,
                        v9x_gl_device_format(), pixels);
    v9x_gl_drawable_unlock(drawable, front);
    LeaveCriticalSection(&v9x_gl_lock);
}

static void V9X_GL_API v9x_gl_flush(void)
{
    const V9X_R3D_INTERFACE *iface = v9x_gl_device_interface();
    const V9X_R3D_ABI_DESCRIBE *description = v9x_gl_device_description();

    if (v9x_gl_current() == 0 || iface == 0) {
        return;
    }
    v9x_gl_pending_flush(v9x_gl_current());
    (void)iface->flush(description->generation);
}

/* ---- Matrix commands (2.10.2), through gl_matrix.c ------------------ */

#define V9X_GL_WITH_CONTEXT(call) do { \
    V9X_GL_CONTEXT *context_ = v9x_gl_current(); \
    if (context_ != 0) { \
        call; \
    } \
} while (0)

static void V9X_GL_API v9x_gl_matrix_mode(GLenum mode)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_matrix_mode(&context_->state, mode));
}

static void V9X_GL_API v9x_gl_load_identity(void)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_load_identity(&context_->state));
}

static void V9X_GL_API v9x_gl_load_matrixf(const GLfloat *m)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_load_matrix(&context_->state, m));
}

static void V9X_GL_API v9x_gl_mult_matrixf(const GLfloat *m)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_mult_matrix(&context_->state, m));
}

/* The double forms convert on entry: the stacks are single precision, which
 * is what the specification's implementation latitude allows (2.10.2). */
static void v9x_gl_to_floats(const GLdouble *in, GLfloat *out)
{
    unsigned int index;

    for (index = 0u; index < 16u; ++index) {
        out[index] = (GLfloat)in[index];
    }
}

static void V9X_GL_API v9x_gl_load_matrixd(const GLdouble *m)
{
    GLfloat f[16];

    v9x_gl_to_floats(m, f);
    V9X_GL_WITH_CONTEXT(v9x_gl_state_load_matrix(&context_->state, f));
}

static void V9X_GL_API v9x_gl_mult_matrixd(const GLdouble *m)
{
    GLfloat f[16];

    v9x_gl_to_floats(m, f);
    V9X_GL_WITH_CONTEXT(v9x_gl_state_mult_matrix(&context_->state, f));
}

static void V9X_GL_API v9x_gl_translatef(GLfloat x, GLfloat y, GLfloat z)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_translate(&context_->state, x, y, z));
}

static void V9X_GL_API v9x_gl_translated(GLdouble x, GLdouble y, GLdouble z)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_translate(&context_->state, (GLfloat)x,
                                               (GLfloat)y, (GLfloat)z));
}

static void V9X_GL_API v9x_gl_scalef(GLfloat x, GLfloat y, GLfloat z)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_scale(&context_->state, x, y, z));
}

static void V9X_GL_API v9x_gl_scaled(GLdouble x, GLdouble y, GLdouble z)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_scale(&context_->state, (GLfloat)x,
                                           (GLfloat)y, (GLfloat)z));
}

static void V9X_GL_API v9x_gl_rotatef(GLfloat angle, GLfloat x, GLfloat y,
                                      GLfloat z)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_rotate(&context_->state, angle, x, y,
                                            z));
}

static void V9X_GL_API v9x_gl_rotated(GLdouble angle, GLdouble x,
                                      GLdouble y, GLdouble z)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_rotate(&context_->state, (GLfloat)angle,
                                            (GLfloat)x, (GLfloat)y,
                                            (GLfloat)z));
}

static void V9X_GL_API v9x_gl_frustum(GLdouble left, GLdouble right,
                                      GLdouble bottom, GLdouble top,
                                      GLdouble near_plane, GLdouble far_plane)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_frustum(&context_->state, left, right,
                                             bottom, top, near_plane,
                                             far_plane));
}

static void V9X_GL_API v9x_gl_ortho(GLdouble left, GLdouble right,
                                    GLdouble bottom, GLdouble top,
                                    GLdouble near_plane, GLdouble far_plane)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_ortho(&context_->state, left, right,
                                           bottom, top, near_plane,
                                           far_plane));
}

static void V9X_GL_API v9x_gl_push_matrix(void)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_push_matrix(&context_->state));
}

static void V9X_GL_API v9x_gl_pop_matrix(void)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_pop_matrix(&context_->state));
}

/* ---- Textures (3.8), through gl_texture.c --------------------------- */

/* A float as a whole number, rounded to nearest, without the C runtime a
 * cast would call (__CHP): the float forms of the integer commands carry
 * enums and counts as whole numbers (2.3). */
static long v9x_gl_whole(GLfloat value);
#pragma aux v9x_gl_whole =     "sub esp,4"     "fistp dword ptr [esp]"     "pop eax"     parm [8087] value [eax] modify exact [eax];

/* The texture tables' storage: the process heap, which the ICD owns. */
static void *v9x_gl_heap_alloc(v9x_u32 bytes)
{
    return HeapAlloc(GetProcessHeap(), 0, bytes);
}

static void v9x_gl_heap_free(void *memory)
{
    HeapFree(GetProcessHeap(), 0, memory);
}

/* Every texture command draws the held batch first: its levels point at
 * images these commands replace, delete or rebind. */
#define V9X_GL_WITH_TEXTURES_IN(bucket, call) do { \
    DWORD entry_started_ = v9x_gl_tsc_begin(); \
    V9X_GL_CONTEXT *context_ = v9x_gl_current(); \
    if (context_ != 0) { \
        DWORD flush_started_ = v9x_gl_tsc_begin(); \
        if (context_->pending.triangles != 0ul) { \
            ++v9x_gl_tsc_calls[V9X_GL_TSC_TEXFLUSH]; \
        } \
        v9x_gl_pending_flush(context_); \
        v9x_gl_tsc_end(V9X_GL_TSC_TEXFLUSH, flush_started_); \
        call; \
    } \
    ++v9x_gl_tsc_calls[bucket]; \
    v9x_gl_tsc_end(bucket, entry_started_); \
} while (0)
#define V9X_GL_WITH_TEXTURES(call) \
    V9X_GL_WITH_TEXTURES_IN(V9X_GL_TSC_TEXOTHER, call)

/*
 * A texture command that changes no texel storage - binding, parameters,
 * the environment, pixel store, name generation - without drawing the held
 * batch first. The batch carries its own copy of everything it was
 * described with (the combine, environment colour, filters, the levels'
 * pointers) and reaches its object only by name for the hardware copy, so
 * none of these can change what it draws. The commands that do change or
 * free texels - glTexImage2D, glTexSubImage2D, glDeleteTextures - still
 * draw it first. Half-Life calls glTexEnv about 540 times a frame under
 * multitexture and glBindTexture about 80, and flushing on each made
 * nearly every one of its ~427 draws a frame (2026-10-05).
 */
#define V9X_GL_WITH_TEXTURE_STATE_IN(bucket, call) do { \
    DWORD entry_started_ = v9x_gl_tsc_begin(); \
    V9X_GL_CONTEXT *context_ = v9x_gl_current(); \
    if (context_ != 0) { \
        call; \
    } \
    ++v9x_gl_tsc_calls[bucket]; \
    v9x_gl_tsc_end(bucket, entry_started_); \
} while (0)

static void V9X_GL_API v9x_gl_gen_textures(GLsizei n, GLuint *names)
{
    V9X_GL_WITH_TEXTURE_STATE_IN(V9X_GL_TSC_TEXOTHER,
        v9x_gl_tex_gen(&context_->state, &context_->textures,
                                        n, names));
}

static void V9X_GL_API v9x_gl_delete_textures(GLsizei n, const GLuint *names)
{
    V9X_GL_WITH_TEXTURES(v9x_gl_tex_delete(&context_->state,
                                           &context_->textures, n, names));
}

static GLboolean V9X_GL_API v9x_gl_is_texture(GLuint name)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    return context != 0 ? v9x_gl_tex_is(&context->state, &context->textures,
                                        name)
                        : 0;
}

static void V9X_GL_API v9x_gl_bind_texture(GLenum target, GLuint name)
{
    V9X_GL_WITH_TEXTURE_STATE_IN(V9X_GL_TSC_TEXBIND,
        v9x_gl_tex_bind(&context_->state,
                                         &context_->textures, target, name));
}

static void V9X_GL_API v9x_gl_tex_parameteri(GLenum target, GLenum pname,
                                             GLint value)
{
    V9X_GL_WITH_TEXTURE_STATE_IN(V9X_GL_TSC_TEXPARAM,
        v9x_gl_tex_parameter(&context_->state,
                                              &context_->textures, target,
                                              pname, value));
}

/* The float and vector forms carry enums as whole numbers (2.3). */
static void V9X_GL_API v9x_gl_tex_parameterf(GLenum target, GLenum pname,
                                             GLfloat value)
{
    v9x_gl_tex_parameteri(target, pname, (GLint)v9x_gl_whole(value));
}

static void V9X_GL_API v9x_gl_tex_parameteriv(GLenum target, GLenum pname,
                                              const GLint *values)
{
    v9x_gl_tex_parameteri(target, pname, values[0]);
}

static void V9X_GL_API v9x_gl_tex_parameterfv(GLenum target, GLenum pname,
                                              const GLfloat *values)
{
    v9x_gl_tex_parameteri(target, pname, (GLint)v9x_gl_whole(values[0]));
}

static void V9X_GL_API v9x_gl_tex_envfv(GLenum target, GLenum pname,
                                        const GLfloat *values)
{
    V9X_GL_WITH_TEXTURE_STATE_IN(V9X_GL_TSC_TEXENV,
        v9x_gl_tex_env(&context_->state,
                                        &context_->textures, target, pname,
                                        values));
}

static void V9X_GL_API v9x_gl_tex_envf(GLenum target, GLenum pname,
                                       GLfloat value)
{
    GLfloat values[4];

    values[0] = value;
    values[1] = 0.0f;
    values[2] = 0.0f;
    values[3] = 0.0f;
    v9x_gl_tex_envfv(target, pname, values);
}

static void V9X_GL_API v9x_gl_tex_envi(GLenum target, GLenum pname,
                                       GLint value)
{
    v9x_gl_tex_envf(target, pname, (GLfloat)value);
}

/* Integer colours map to [0,1] linearly from the full range (2.3); an
 * integer mode is the enum itself. */
static void V9X_GL_API v9x_gl_tex_enviv(GLenum target, GLenum pname,
                                        const GLint *values)
{
    GLfloat converted[4];
    unsigned int i;

    for (i = 0u; i < 4u; ++i) {
        converted[i] = pname == V9X_GL_TEXTURE_ENV_COLOR
            ? (GLfloat)((double)values[i] / 2147483647.0)
            : (GLfloat)values[i];
        if (pname != V9X_GL_TEXTURE_ENV_COLOR) {
            break;
        }
    }
    v9x_gl_tex_envfv(target, pname, converted);
}

static void V9X_GL_API v9x_gl_pixel_storei(GLenum pname, GLint value)
{
    V9X_GL_WITH_TEXTURE_STATE_IN(V9X_GL_TSC_TEXOTHER,
        v9x_gl_pixel_store(&context_->state,
                                            &context_->textures, pname,
                                            value));
}

static void V9X_GL_API v9x_gl_pixel_storef(GLenum pname, GLfloat value)
{
    v9x_gl_pixel_storei(pname, (GLint)v9x_gl_whole(value));
}

static void V9X_GL_API v9x_gl_api_tex_image_2d(GLenum target, GLint level,
                                           GLint internal_format,
                                           GLsizei width, GLsizei height,
                                           GLint border, GLenum format,
                                           GLenum type, const GLvoid *pixels)
{
    V9X_GL_WITH_TEXTURES_IN(V9X_GL_TSC_TEXIMAGE,
        v9x_gl_tex_image_2d(&context_->state,
                                             &context_->textures, target,
                                             level, internal_format, width,
                                             height, border, format, type,
                                             pixels));
}

static void V9X_GL_API v9x_gl_api_tex_sub_image_2d(GLenum target, GLint level,
                                               GLint xoffset, GLint yoffset,
                                               GLsizei width, GLsizei height,
                                               GLenum format, GLenum type,
                                               const GLvoid *pixels)
{
    V9X_GL_WITH_TEXTURES_IN(V9X_GL_TSC_TEXSUB,
        v9x_gl_tex_sub_image_2d(&context_->state,
                                                 &context_->textures, target,
                                                 level, xoffset, yoffset,
                                                 width, height, format, type,
                                                 pixels));
}

/* ---- Geometry (2.6-2.11), through gl_prim.c ------------------------- */

/*
 * The pipeline's sink: one batch of up to 64 screen-space triangles to the
 * render interface, drawn into the context's back and depth buffers with
 * its fragment state. A batch only ever holds triangles from one
 * glBegin/glEnd, and no state command is legal between those, so the state
 * read here is the state the triangles were made with.
 */
/* ---- Hardware textures: GL images as surfaces the engine samples ---- */

/*
 * The ICD's copy of one texture object's images in video memory. Made when
 * the engine describes surface textures and the object fits them, filled
 * again whenever the object's images change (its revision), and dropped
 * with the object, the context, or a mode change. `unusable` remembers an
 * upload that failed, so it is not retried each draw.
 *
 * Video memory is small (about 5 MB free on the 945GSE netbook), and a
 * game's textures are not: every live copy is in one table, stamped with
 * the use clock, and when DirectDraw cannot make a new one the least
 * recently used are evicted - surface released, record kept - until it
 * can. An evicted copy is made again on its next use. A copy that still
 * cannot be made waits V9X_GL_HWTEX_RETRY uses of the clock before it is
 * tried again, and until then the texture is drawn from its CPU copy.
 * Releasing a surface mid-frame is safe: the HAL's DestroySurface drains
 * the engine before the memory goes back.
 */
typedef struct v9x_gl_hwtex {
    void *surface;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 levels;
    v9x_u32 format;
    v9x_u32 revision;
    int filled;
    int unusable;
    v9x_u32 last_used;
    v9x_u32 retry_at;
    unsigned int slot;
    /* A square copy (gl_texture.h v9x_gl_tex_square_fill) of an image that
     * is not one: its padding follows the address mode it was built for. */
    int squared;
    int clamp;
} V9X_GL_HWTEX;

#define V9X_GL_HWTEX_MAX      2048u

static int v9x_gl_hw_no(unsigned int reason)
{
    ++v9x_gl_hwno[reason];
    return 0;
}
#define V9X_GL_HWTEX_RETRY    256ul
#define V9X_GL_HWTEX_EVICT_MAX 64u

static V9X_GL_HWTEX *v9x_gl_hwtex_live[V9X_GL_HWTEX_MAX];
static unsigned int v9x_gl_hwtex_count;
static v9x_u32 v9x_gl_hwtex_clock;
static DWORD v9x_gl_hw_evictions;

/* V9X_GL_TEXTURES.hw_release: the surface, the table entry, the record. */
static void v9x_gl_hwtex_free(void *memory)
{
    V9X_GL_HWTEX *hw = (V9X_GL_HWTEX *)memory;
    unsigned int last;

    v9x_gl_hwtex_release(hw->surface);
    if (hw->slot < v9x_gl_hwtex_count && v9x_gl_hwtex_live[hw->slot] == hw) {
        last = v9x_gl_hwtex_count - 1u;
        v9x_gl_hwtex_live[hw->slot] = v9x_gl_hwtex_live[last];
        v9x_gl_hwtex_live[hw->slot]->slot = hw->slot;
        v9x_gl_hwtex_count = last;
    }
    HeapFree(GetProcessHeap(), 0, hw);
}

/* A copy the draw being built already names - unit 0's, while unit 1's is
 * made - which eviction must not release under it. */
static const V9X_GL_HWTEX *v9x_gl_hwtex_pinned;

/* The least recently used copy that holds a surface, other than `keep` and
 * the pinned one. */
static V9X_GL_HWTEX *v9x_gl_hwtex_oldest(const V9X_GL_HWTEX *keep)
{
    V9X_GL_HWTEX *oldest = 0;
    unsigned int i;

    for (i = 0u; i < v9x_gl_hwtex_count; ++i) {
        V9X_GL_HWTEX *candidate = v9x_gl_hwtex_live[i];

        if (candidate != keep && candidate != v9x_gl_hwtex_pinned &&
            candidate->surface != 0 &&
            (oldest == 0 || candidate->last_used < oldest->last_used)) {
            oldest = candidate;
        }
    }
    return oldest;
}

/* Make `hw`'s surface, evicting the least recently used copies while
 * DirectDraw has no room. Non-zero when it exists. */
static int v9x_gl_hwtex_make(V9X_GL_HWTEX *hw)
{
    unsigned int evicted;

    for (evicted = 0u; ; ++evicted) {
        V9X_GL_HWTEX *victim;

        hw->surface = v9x_gl_hwtex_create(hw->width, hw->height, hw->levels,
                                          hw->format);
        ++v9x_gl_hw_creates;
        if (hw->surface != 0) {
            hw->filled = 0;
            return 1;
        }
        ++v9x_gl_hw_create_failures;
        victim = evicted < V9X_GL_HWTEX_EVICT_MAX ? v9x_gl_hwtex_oldest(hw)
                                                  : 0;
        if (victim == 0) {
            hw->retry_at = v9x_gl_hwtex_clock + V9X_GL_HWTEX_RETRY;
            return 0;
        }
        v9x_gl_hwtex_release(victim->surface);
        victim->surface = 0;
        victim->filled = 0;
        ++v9x_gl_hw_evictions;
    }
}

/* After a mode change every surface is lost: forget every copy, in every
 * context, then describe again. */
static int v9x_gl_redescribe_all(void)
{
    unsigned int index;

    for (index = 0u; index < V9X_GL_CONTEXTS_MAX; ++index) {
        if (v9x_gl_contexts[index].in_use) {
            v9x_gl_textures_drop_hw(&v9x_gl_contexts[index].textures);
        }
    }
    return v9x_gl_device_redescribe();
}

/* A unit's bound texture as the interface's CPU description, its alpha op
 * normalised when nothing reads the fragment's alpha (gl_texture.h). */
static void v9x_gl_describe_texture(V9X_GL_CONTEXT *context, v9x_u32 unit,
                                    V9X_R3D_ABI_TEXTURE *texture,
                                    V9X_R3D_ABI_LEVEL *levels)
{
    const V9X_R3D_ABI_DESCRIBE *description = v9x_gl_device_description();

    v9x_gl_tex_describe_unit(&context->state, &context->textures, unit,
                             texture, levels);
    /* Past the interface's largest texture the draw is refused as
     * invalid and not drawn (a 512x256 on the Rage XL, 2026-10-03). */
    if (description != 0) {
        v9x_gl_tex_fit(&context->textures, unit, texture, levels,
                       description->texture_size_max);
    }
    if (!v9x_gl_prim_fragment_alpha_used(&context->state,
                                         &context->pipeline)) {
        v9x_gl_tex_fragment_alpha_unused(texture);
    }
}

/*
 * The square copy's levels, built from the image's: level n is side >> n
 * square from the image's level n, or its last once the image's chain is
 * shorter (a copy grown to the sampler's minimum has more levels). Heap
 * storage the caller frees; zero when it cannot be had.
 */
static v9x_u16 *v9x_gl_hwtex_square(const V9X_R3D_ABI_TEXTURE *texture,
                                    v9x_u32 side, v9x_u32 levels, int clamp,
                                    V9X_R3D_ABI_LEVEL *out)
{
    v9x_u32 bytes = 0ul;
    v9x_u32 level;
    v9x_u16 *storage;
    v9x_u16 *at;

    for (level = 0ul; level < levels; ++level) {
        bytes += (side >> level) * (side >> level) * 2ul;
    }
    storage = (v9x_u16 *)HeapAlloc(GetProcessHeap(), 0, bytes);
    if (storage == 0) {
        return 0;
    }
    at = storage;
    for (level = 0ul; level < levels; ++level) {
        const V9X_R3D_ABI_LEVEL *source = &texture->levels[
            level < texture->level_count ? level : texture->level_count - 1ul];
        v9x_u32 edge = side >> level;

        v9x_gl_tex_square_fill((const v9x_u16 *)source->pixels,
                               source->pitch, source->width, source->height,
                               at, edge, clamp);
        out[level].pixels = at;
        out[level].width = edge;
        out[level].height = edge;
        out[level].pitch = edge * 2ul;
        out[level].bytes = edge * edge * 2ul;
        at += edge * edge;
    }
    return storage;
}

/*
 * Replace a CPU description with the object's surface when the engine
 * samples surfaces and the object fits what describe allows, squaring it
 * for an engine that samples only squares (or a minimum edge). Non-zero
 * when `texture` now names a surface; then *scale_s and *scale_t are what
 * the draw's s and t must be multiplied by, 1 unless the copy is squared.
 * Runs under the ICD's critical section.
 */
static int v9x_gl_hw_texture(V9X_GL_CONTEXT *context, GLuint name,
                             int alpha_used, V9X_R3D_ABI_TEXTURE *texture,
                             float *scale_s, float *scale_t)
{
    const V9X_R3D_ABI_DESCRIBE *description = v9x_gl_device_description();
    V9X_R3D_ABI_LEVEL squared_levels[V9X_GL_TEXTURE_LEVELS];
    const V9X_R3D_ABI_LEVEL *upload_levels;
    V9X_GL_TEXOBJ *object;
    V9X_GL_HWTEX *hw;
    v9x_u16 *squared_storage = 0;
    v9x_u32 hw_format;
    int to_1555;
    int squared;
    int clamp;
    v9x_u32 level;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 side;
    v9x_u32 levels;
    DWORD upload_start;
    int uploaded;
    /* A refill of only the rectangles that changed (below). */
    V9X_GL_TEXRECT rects[V9X_GL_TEXTURE_LEVELS];
    int partial = 0;

    *scale_s = 1.0f;
    *scale_t = 1.0f;
    if (texture->storage != V9X_R3D_ABI_TEXTURE_CPU ||
        description->hw_texture_size_max == 0ul) {
        return v9x_gl_hw_no(V9X_GL_HWNO_NOT_CPU);
    }
    width = texture->levels[0].width;
    height = texture->levels[0].height;
    clamp = texture->address == V9X_R3D_ABI_ADDRESS_CLAMP;
    squared = (description->hw_texture_shape &
               V9X_R3D_ABI_HWTEX_SQUARE) != 0ul && height != width;
    if (width < description->hw_texture_size_min ||
        height < description->hw_texture_size_min) {
        squared = 1;
    }
    side = width;
    levels = texture->level_count;
    if (squared) {
        side = v9x_gl_tex_square_side(width, height,
                                      description->hw_texture_size_min,
                                      description->hw_texture_size_max);
        if (side == 0ul) {
            return v9x_gl_hw_no(V9X_GL_HWNO_SQUARE_SIDE);
        }
        /* A chain runs to 1x1, so the square one has log2(side) + 1. */
        if (levels > 1ul) {
            levels = 1ul;
            while ((side >> (levels - 1ul)) > 1ul) {
                ++levels;
            }
        }
    } else if (width > description->hw_texture_size_max ||
               height > description->hw_texture_size_max) {
        return v9x_gl_hw_no(V9X_GL_HWNO_TOO_BIG);
    }
    /* The layout the engine samples: the image's own, or for an RGB image
     * on an engine without 565 (the ViRGE), 1555 with alpha one. */
    hw_format = texture->format;
    to_1555 = 0;
    if ((description->texture_formats & (1ul << hw_format)) == 0ul) {
        if (hw_format != V9X_R3D_ABI_FORMAT_RGB565 ||
            (description->texture_formats &
             (1ul << V9X_R3D_ABI_FORMAT_ARGB1555)) == 0ul) {
            return v9x_gl_hw_no(V9X_GL_HWNO_FORMAT);
        }
        hw_format = V9X_R3D_ABI_FORMAT_ARGB1555;
        to_1555 = 1;
    }
    /* GL 1.1 textures are powers of two already (gl_texture.c refuses any
     * other size), so V9X_R3D_ABI_HWTEX_POW2 always holds here. */

    object = v9x_gl_tex_object(&context->textures, name);
    if (object == 0) {
        return v9x_gl_hw_no(V9X_GL_HWNO_NO_OBJECT);
    }
    hw = (V9X_GL_HWTEX *)object->hw;
    if (hw != 0 && (hw->width != side || hw->height != (squared ? side
                                                                : height) ||
                    hw->levels != levels || hw->format != hw_format ||
                    hw->squared != squared ||
                    (squared && hw->clamp != clamp))) {
        v9x_gl_hwtex_free(hw);
        object->hw = 0;
        hw = 0;
    }
    if (hw == 0) {
        hw = (V9X_GL_HWTEX *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                       sizeof(V9X_GL_HWTEX));
        if (hw == 0) {
            return v9x_gl_hw_no(V9X_GL_HWNO_NO_RECORD);
        }
        if (v9x_gl_hwtex_count >= V9X_GL_HWTEX_MAX) {
            HeapFree(GetProcessHeap(), 0, hw);
            return v9x_gl_hw_no(V9X_GL_HWNO_TABLE_FULL);
        }
        hw->width = side;
        hw->height = squared ? side : height;
        hw->levels = levels;
        hw->format = hw_format;
        hw->squared = squared;
        hw->clamp = clamp;
        hw->slot = v9x_gl_hwtex_count;
        v9x_gl_hwtex_live[v9x_gl_hwtex_count++] = hw;
        object->hw = hw;
    }
    ++v9x_gl_hwtex_clock;
    if (hw->unusable) {
        return v9x_gl_hw_no(V9X_GL_HWNO_UNUSABLE);
    }
    if (hw->surface == 0) {
        if (v9x_gl_hwtex_clock < hw->retry_at) {
            return v9x_gl_hw_no(V9X_GL_HWNO_BACKOFF);
        }
        if (!v9x_gl_hwtex_make(hw)) {
            return v9x_gl_hw_no(V9X_GL_HWNO_MAKE_FAILED);
        }
    }
    hw->last_used = v9x_gl_hwtex_clock;
    if (!hw->filled || hw->revision != object->revision) {
        upload_levels = texture->levels;
        if (squared) {
            squared_storage = v9x_gl_hwtex_square(texture, side, levels,
                                                  clamp, squared_levels);
            if (squared_storage == 0) {
                return v9x_gl_hw_no(V9X_GL_HWNO_SQUARE_ALLOC);
            }
            upload_levels = squared_levels;
        }
        /*
         * Only what changed, when the copy can be brought up that way: it
         * was filled, it is not a square copy (whose texels are a
         * rearrangement), and its levels are the object's own from level 0
         * (v9x_gl_tex_fit did not drop any). Every level it would lock is
         * a drain of the GPU, so a level nothing touched is not locked.
         * Quake 2 and Half-Life update a lightmap a surface at a time under
         * multitexture, and refilling the whole 32 KB page each time was
         * most of what made it slower (2026-10-05).
         */
        partial = hw->filled && !squared &&
                  levels == texture->level_count &&
                  texture->levels[0].pixels == object->levels[0].texels;
        for (level = 0ul; partial && level < levels; ++level) {
            if (!v9x_gl_tex_dirty_rect(object, hw->revision, level,
                                       &rects[level])) {
                partial = 0;
            }
        }
        upload_start = v9x_gl_ticks();
        uploaded = 0;
        if (partial) {
            uploaded = v9x_gl_hwtex_upload_rects(hw->surface, levels,
                                                 upload_levels, rects,
                                                 to_1555);
            if (uploaded) {
                ++v9x_gl_hw_partial_uploads;
            } else {
                /* A level that would not lock (lost, restored): whole. */
                partial = 0;
            }
        }
        if (!partial) {
            uploaded = v9x_gl_hwtex_upload(hw->surface, levels,
                                           upload_levels, to_1555);
        }
        v9x_gl_upload_ticks += v9x_gl_ticks() - upload_start;
        if (!uploaded) {
            if (squared_storage != 0) {
                HeapFree(GetProcessHeap(), 0, squared_storage);
            }
            v9x_gl_hwtex_release(hw->surface);
            hw->surface = 0;
            hw->unusable = 1;
            return v9x_gl_hw_no(V9X_GL_HWNO_UPLOAD);
        }
        hw->revision = object->revision;
        hw->filled = 1;
        /* The one copy is current: changes count from here. */
        v9x_gl_tex_dirty_reset(object);
        ++v9x_gl_hw_uploads;
        for (level = 0ul; !partial && level < levels; ++level) {
            v9x_gl_hw_upload_kb += upload_levels[level].bytes / 1024ul;
        }
        if (squared_storage != 0) {
            HeapFree(GetProcessHeap(), 0, squared_storage);
        }
    }
    if (squared) {
        ++v9x_gl_hw_squared_draws;
        *scale_s = (float)(v9x_s32)width / (float)(v9x_s32)side;
        *scale_t = (float)(v9x_s32)height / (float)(v9x_s32)side;
    }

    if (to_1555) {
        v9x_gl_tex_as_1555(texture, alpha_used);
    }
    texture->storage = V9X_R3D_ABI_TEXTURE_HW;
    texture->surface.surface = hw->surface;
    texture->levels = 0;
    texture->level_count = 0ul;
    return 1;
}

/*
 * The first draws of each distinct state, logged once each: what a game
 * actually asks for, in the terms the render interface is given, so two
 * engines' runs of the same scene can be compared entry by entry. Bounded
 * by the table, so a long session writes a few dozen lines, not one a draw.
 */
#define V9X_GL_STATE_SEEN_MAX 48u

static v9x_u32 v9x_gl_state_seen[V9X_GL_STATE_SEEN_MAX][5];
static unsigned int v9x_gl_state_seen_count;
/* The same, for the states the engine refused: what a game asks for that
 * sends it to the CPU. */
static v9x_u32 v9x_gl_refused_seen[V9X_GL_STATE_SEEN_MAX][5];
static unsigned int v9x_gl_refused_seen_count;

static void v9x_gl_note_state_in(v9x_u32 (*seen)[5], unsigned int *count,
                                 const char *tag,
                                 const V9X_GL_CONTEXT *context,
                                 const V9X_R3D_ABI_DRAW *draw);

static void v9x_gl_note_state(const V9X_GL_CONTEXT *context,
                              const V9X_R3D_ABI_DRAW *draw)
{
    v9x_gl_note_state_in(v9x_gl_state_seen, &v9x_gl_state_seen_count,
                         "state", context, draw);
}

static void v9x_gl_note_refused(const V9X_GL_CONTEXT *context,
                                const V9X_R3D_ABI_DRAW *draw)
{
    v9x_gl_note_state_in(v9x_gl_refused_seen, &v9x_gl_refused_seen_count,
                         "refused", context, draw);
}

static void v9x_gl_note_state_in(v9x_u32 (*seen)[5], unsigned int *count,
                                 const char *tag,
                                 const V9X_GL_CONTEXT *context,
                                 const V9X_R3D_ABI_DRAW *draw)
{
    const V9X_R3D_ABI_TEXTURE *texture = &draw->texture;
    const V9X_R3D_ABI_TEXTURE *texture1 = &draw->texture1;
    v9x_u32 key[5];
    v9x_u32 edge = 0ul;
    unsigned int i;
    char text[240];

    /* The size from the batch's CPU description, which a surface
     * texture's does not carry. */
    if (context->pending.texture.storage == V9X_R3D_ABI_TEXTURE_CPU &&
        context->pending.texture.levels != 0) {
        edge = (context->pending.texture.levels[0].width << 16) |
               context->pending.texture.levels[0].height;
    }
    key[0] = context->pending.texture_name;
    key[1] = (texture->storage << 24) | (texture->format << 16) |
             (texture->color_op << 8) | texture->alpha_op;
    key[2] = (draw->state.blend_enable << 24) | (draw->state.src_blend << 16) |
             (draw->state.dst_blend << 8) | draw->state.alpha_test_enable;
    key[3] = (draw->state.depth_enable << 24) | (draw->state.depth_func << 16) |
             (draw->state.depth_write << 8) | texture->mip;
    /* The second unit's combine: which env modes a game sets on it is
     * what the plan's census asks (gen3-sgis-multitexture.md). */
    key[4] = (texture1->storage << 24) | (texture1->format << 16) |
             (texture1->color_op << 8) | texture1->alpha_op;
    for (i = 0u; i < *count; ++i) {
        if (seen[i][0] == key[0] && seen[i][1] == key[1] &&
            seen[i][2] == key[2] && seen[i][3] == key[3] &&
            seen[i][4] == key[4]) {
            return;
        }
    }
    if (*count >= V9X_GL_STATE_SEEN_MAX) {
        return;
    }
    for (i = 0u; i < 5u; ++i) {
        seen[*count][i] = key[i];
    }
    ++*count;
    wsprintfA(text, "%s tex=%lu size=%08lX tex=%08lX blend=%08lX "
              "depth=%08lX colour=%08lX mask=%lX scissor=%lu,%lu,%lu,%lu "
              "fog=%lu tex1=%lu/%08lX env1=%06lX",
              tag, key[0], edge, key[1], key[2], key[3],
              draw->vertices != 0 ? draw->vertices[0].color : 0ul,
              draw->state.write_mask, draw->state.scissor_left,
              draw->state.scissor_top, draw->state.scissor_right,
              draw->state.scissor_bottom, draw->state.fog_enable,
              (DWORD)context->pending.texture1_name, key[4],
              texture1->env_color);
    v9x_gl_log(text);
}

/*
 * Where the process's batches went, logged when a context is deleted: the
 * numbers that say which remaining path costs a game its frame rate.
 * Batches and triangles for each of: untextured; a texture the engine
 * sampled from its own surface; a texture sent as CPU levels because it
 * does not fit the engine's surface rules (non-square counted apart); and
 * a surface texture the engine refused, sent again as CPU levels.
 */
#define V9X_GL_PATH_UNTEXTURED   0u
#define V9X_GL_PATH_HW_TEXTURE   1u
#define V9X_GL_PATH_CPU_TEXTURE  2u
#define V9X_GL_PATH_CPU_NONSQUARE 3u
#define V9X_GL_PATH_HW_REFUSED   4u
#define V9X_GL_PATH_COUNT        5u

static DWORD v9x_gl_path_batches[V9X_GL_PATH_COUNT];
static DWORD v9x_gl_path_triangles[V9X_GL_PATH_COUNT];

static void v9x_gl_path_note(unsigned int path, v9x_u32 triangles)
{
    ++v9x_gl_path_batches[path];
    v9x_gl_path_triangles[path] += triangles;
}

static void v9x_gl_path_log(void);

static void v9x_gl_counters_log(void)
{
    /* Thirteen counters of up to ten digits past a 180-character format. */
    char text[400];

    v9x_gl_path_log();
    wsprintfA(text, "counters hwtex live=%lu creates=%lu create-failed=%lu "
              "evictions=%lu uploads=%lu partial=%lu "
              "upload-kb=%lu squared=%lu alpha-dropped=%lu stubs=%lu "
              "failed r4=%lu r7=%lu r8=%lu other=%lu",
              (DWORD)v9x_gl_hwtex_count, v9x_gl_hw_creates,
              v9x_gl_hw_create_failures, v9x_gl_hw_evictions,
              v9x_gl_hw_uploads, v9x_gl_hw_partial_uploads,
              v9x_gl_hw_upload_kb,
              v9x_gl_hw_squared_draws, v9x_gl_alpha_tests_dropped,
              v9x_gl_stub_total,
              v9x_gl_draw_failures[4], v9x_gl_draw_failures[7],
              v9x_gl_draw_failures[8],
              v9x_gl_draw_failures[0] + v9x_gl_draw_failures[1] +
                  v9x_gl_draw_failures[2] + v9x_gl_draw_failures[3] +
                  v9x_gl_draw_failures[5] + v9x_gl_draw_failures[6] +
                  v9x_gl_draw_failures[9]);
    v9x_gl_log(text);
}

/* Every ten seconds while drawing: a game that runs slowly is measured
 * while it runs, not only when it exits. */
static void v9x_gl_counters_tick(void)
{
    DWORD now = GetTickCount();

    if (now - v9x_gl_report_last >= V9X_GL_REPORT_MS) {
        char text[160];

        wsprintfA(text, "time interval-ms=%lu swaps=%lu present-ms=%lu "
                  "draw-ms=%lu upload-ms=%lu",
                  now - v9x_gl_report_last, v9x_gl_swaps,
                  v9x_gl_ticks_ms(v9x_gl_present_ticks),
                  v9x_gl_tsc_bucket_ms(V9X_GL_TSC_IFACE,
                                       now - v9x_gl_report_last),
                  v9x_gl_ticks_ms(v9x_gl_upload_ticks));
        v9x_gl_log(text);
        v9x_gl_tsc_log(now - v9x_gl_report_last, v9x_gl_swaps);
        v9x_gl_swaps = 0ul;
        v9x_gl_present_ticks = 0ul;
        v9x_gl_upload_ticks = 0ul;
        v9x_gl_report_last = now;
        v9x_gl_counters_log();
    }
}

/* A float's bits, for a log line wsprintf cannot print as a float. */
static DWORD v9x_gl_bits(float value)
{
    return *(const DWORD *)&value;
}

/*
 * A failed batch: counted by result, and the first few dumped with the
 * first triangle's vertices as raw bits (sx sy sz rhw tu tv), which is
 * what an engine's range checks read. After that only the count, so a game
 * that fails a batch every frame does not also open the log every frame.
 */
static void v9x_gl_note_failure(v9x_u32 result, const V9X_GL_PENDING *pending,
                                v9x_u32 submitted)
{
    char text[240];
    unsigned int i;

    ++v9x_gl_draw_failures[result < V9X_GL_RESULT_SLOTS ? result : 0u];
    if (v9x_gl_failures_dumped >= V9X_GL_FAILURES_DUMPED_MAX) {
        return;
    }
    ++v9x_gl_failures_dumped;
    v9x_gl_log3("draw result=%lu triangles=%lu submitted=%lu", result,
                pending->triangles, submitted);
    for (i = 0u; i < 3u && i < pending->triangles * 3ul; ++i) {
        const V9X_R3D_ABI_VERTEX *v = &pending->vertices[i];

        wsprintfA(text, "  v%u sx=%08lX sy=%08lX sz=%08lX rhw=%08lX "
                  "tu=%08lX tv=%08lX", i, v9x_gl_bits(v->sx),
                  v9x_gl_bits(v->sy), v9x_gl_bits(v->sz),
                  v9x_gl_bits(v->rhw), v9x_gl_bits(v->tu),
                  v9x_gl_bits(v->tv));
        v9x_gl_log(text);
    }
}

static void v9x_gl_path_log(void)
{
    char text[200];

    wsprintfA(text, "paths batches/triangles untextured=%lu/%lu "
              "hw=%lu/%lu cpu=%lu/%lu cpu-nonsquare=%lu/%lu "
              "hw-refused=%lu/%lu two-units=%lu/%lu",
              v9x_gl_path_batches[0], v9x_gl_path_triangles[0],
              v9x_gl_path_batches[1], v9x_gl_path_triangles[1],
              v9x_gl_path_batches[2], v9x_gl_path_triangles[2],
              v9x_gl_path_batches[3], v9x_gl_path_triangles[3],
              v9x_gl_path_batches[4], v9x_gl_path_triangles[4],
              v9x_gl_mtex_batches, v9x_gl_mtex_triangles);
    v9x_gl_log(text);
}

/* The surface a colour buffer is drawn in: the back, or the front made on
 * first use. */
static void *v9x_gl_drawable_target(V9X_GL_DRAWABLE *drawable,
                                    unsigned int which)
{
    return which == V9X_GL_DRAW_FRONT ? v9x_gl_drawable_front(drawable)
                                      : v9x_gl_drawable_back(drawable);
}

/*
 * The held batch into one colour buffer. The front's result is shown on
 * the window at once: GL_FRONT drawing is what the application means to be
 * seen without a swap, and nothing else would put it there.
 */
static v9x_u32 v9x_gl_draw_into(V9X_GL_CONTEXT *context, unsigned int which,
                                V9X_R3D_ABI_OUTCOME *outcome)
{
    const V9X_R3D_INTERFACE *iface = v9x_gl_device_interface();
    const V9X_R3D_ABI_DESCRIBE *description = v9x_gl_device_description();
    V9X_GL_PENDING *pending = &context->pending;
    V9X_GL_DRAWABLE *drawable;
    V9X_R3D_ABI_DRAW draw;
    v9x_u32 result;
    unsigned int i;
    unsigned int path;
    int hardware;
    int two_units = pending->texture1.storage != V9X_R3D_ABI_TEXTURE_NONE;
    float scale_s;
    float scale_t;
    float scale_s1 = 1.0f;
    float scale_t1 = 1.0f;
    DWORD draw_start;

    draw_start = v9x_gl_tsc_begin();
    drawable = v9x_gl_bind_window(context, v9x_gl_context_window(context));
    v9x_gl_tsc_end(V9X_GL_TSC_BIND, draw_start);
    if (drawable == 0) {
        return V9X_R3D_RESULT_NO_MEMORY;
    }
    for (i = 0u; i < sizeof(draw); ++i) {
        ((BYTE *)&draw)[i] = 0u;
    }
    draw.struct_bytes = sizeof(draw);
    draw.generation = description->generation;
    draw.target.surface = v9x_gl_drawable_target(drawable, which);
    draw.depth.surface = v9x_gl_drawable_depth(drawable);
    if (draw.target.surface == 0) {
        return V9X_R3D_RESULT_NO_MEMORY;
    }
    draw.texture = pending->texture;
    draw.state = pending->state;
    draw.vertices = pending->vertices;
    draw.triangle_count = pending->triangles;
    if (two_units) {
        draw.texture1 = pending->texture1;
        draw.texcoords1 = pending->texcoords1;
        ++v9x_gl_mtex_batches;
        v9x_gl_mtex_triangles += pending->triangles;
    }
    v9x_gl_note_state(context, &draw);
    if (draw.texture.storage == V9X_R3D_ABI_TEXTURE_NONE) {
        path = V9X_GL_PATH_UNTEXTURED;
    } else {
        path = draw.texture.levels[0].width != draw.texture.levels[0].height
            ? V9X_GL_PATH_CPU_NONSQUARE : V9X_GL_PATH_CPU_TEXTURE;
    }
    draw_start = v9x_gl_tsc_begin();
    hardware = v9x_gl_hw_texture(context, pending->texture_name,
                                 pending->alpha_used, &draw.texture,
                                 &scale_s, &scale_t);
    /* Both units on surfaces or both on the CPU: an engine that samples
     * surfaces refuses CPU levels, and the software fallback takes no
     * surface, so a draw with one of each could be drawn by neither. */
    if (two_units) {
        const V9X_GL_TEXOBJ *object0 =
            v9x_gl_tex_object(&context->textures, pending->texture_name);
        int hardware1;

        v9x_gl_hwtex_pinned = hardware && object0 != 0
            ? (const V9X_GL_HWTEX *)object0->hw : 0;
        hardware1 = v9x_gl_hw_texture(context, pending->texture1_name,
                                      pending->alpha_used, &draw.texture1,
                                      &scale_s1, &scale_t1);
        v9x_gl_hwtex_pinned = 0;
        if (hardware1 != hardware) {
            draw.texture = pending->texture;
            draw.texture1 = pending->texture1;
            hardware = 0;
        }
    }
    v9x_gl_tsc_end(V9X_GL_TSC_HWTEX, draw_start);
    if (hardware) {
        path = V9X_GL_PATH_HW_TEXTURE;
        if (scale_s != 1.0f || scale_t != 1.0f) {
            for (i = 0u; i < pending->triangles * 3ul; ++i) {
                v9x_gl_scaled_vertices[i] = pending->vertices[i];
                v9x_gl_scaled_vertices[i].tu *= scale_s;
                v9x_gl_scaled_vertices[i].tv *= scale_t;
            }
            draw.vertices = v9x_gl_scaled_vertices;
        }
        if (two_units && (scale_s1 != 1.0f || scale_t1 != 1.0f)) {
            for (i = 0u; i < pending->triangles * 3ul; ++i) {
                v9x_gl_scaled_tex1[i * 2u] =
                    pending->texcoords1[i * 2u] * scale_s1;
                v9x_gl_scaled_tex1[i * 2u + 1u] =
                    pending->texcoords1[i * 2u + 1u] * scale_t1;
            }
            draw.texcoords1 = v9x_gl_scaled_tex1;
        }
    }
    /* An alpha test that can discard nothing is not sent (gl_prim.h). The
     * test reads unit 0's alpha only, so a second unit keeps it. */
    if (!two_units &&
        v9x_gl_prim_alpha_test_passes(&draw.state, &draw.texture,
                                      draw.vertices,
                                      pending->triangles * 3ul)) {
        draw.state.alpha_test_enable = 0ul;
        ++v9x_gl_alpha_tests_dropped;
        /* The texture's alpha op was chosen with the test reading the
         * fragment's alpha; without it, and with no alpha blend factor,
         * nothing does, so it takes the form every engine has. */
        if (!v9x_gl_prim_blend_reads_alpha(&draw.state)) {
            v9x_gl_tex_fragment_alpha_unused(&draw.texture);
        }
    }
    draw_start = v9x_gl_tsc_begin();
    result = iface->draw(&draw, outcome);
    v9x_gl_tsc_end(V9X_GL_TSC_IFACE, draw_start);
    if (!hardware && draw.texture.storage == V9X_R3D_ABI_TEXTURE_CPU) {
        v9x_gl_tsc_end(V9X_GL_TSC_IFACE_CPU, draw_start);
    }
        ++v9x_gl_count_draws;
    if (result == V9X_R3D_RESULT_UNSUPPORTED && hardware) {
        /* Refused before anything was emitted: the same batch with the CPU
         * copy, which the software fallback draws, at its own s and t. */
        path = V9X_GL_PATH_HW_REFUSED;
        v9x_gl_note_refused(context, &draw);
        draw.texture = pending->texture;
        draw.vertices = pending->vertices;
        if (two_units) {
            draw.texture1 = pending->texture1;
            draw.texcoords1 = pending->texcoords1;
        }
        draw_start = v9x_gl_tsc_begin();
        result = iface->draw(&draw, outcome);
        v9x_gl_tsc_end(V9X_GL_TSC_IFACE, draw_start);
        ++v9x_gl_count_draws;
    }
    v9x_gl_path_note(path, pending->triangles);
    if (result == V9X_R3D_RESULT_STALE && v9x_gl_redescribe_all()) {
        drawable = v9x_gl_bind_window(context,
                                      v9x_gl_context_window(context));
        if (drawable == 0) {
            return V9X_R3D_RESULT_NO_MEMORY;
        }
        draw.generation = description->generation;
        draw.target.surface = v9x_gl_drawable_target(drawable, which);
        draw.depth.surface = v9x_gl_drawable_depth(drawable);
        if (draw.target.surface == 0) {
            return V9X_R3D_RESULT_NO_MEMORY;
        }
        draw.texture = pending->texture;
        draw.vertices = pending->vertices;
        if (two_units) {
            draw.texture1 = pending->texture1;
            draw.texcoords1 = pending->texcoords1;
        }
        draw_start = v9x_gl_tsc_begin();
        result = iface->draw(&draw, outcome);
        v9x_gl_tsc_end(V9X_GL_TSC_IFACE, draw_start);
        ++v9x_gl_count_draws;
    }
    if (result == V9X_R3D_RESULT_OK && which == V9X_GL_DRAW_FRONT) {
        v9x_gl_drawable_show_front(drawable);
    }
    return result;
}

static void v9x_gl_pending_flush(V9X_GL_CONTEXT *context)
{
    V9X_GL_PENDING *pending;
    V9X_R3D_ABI_OUTCOME outcome;
    unsigned int pass;
    v9x_u32 result;
    DWORD flush_started;

    if (context == 0 || context->pending.triangles == 0ul ||
        v9x_gl_device_interface() == 0) {
        return;
    }
    flush_started = v9x_gl_tsc_begin();
    pending = &context->pending;
    EnterCriticalSection(&v9x_gl_lock);
    for (pass = 0u; pass < 2u; ++pass) {
        unsigned int which = pass == 0u ? V9X_GL_DRAW_BACK
                                        : V9X_GL_DRAW_FRONT;

        if ((pending->targets & which) == 0u) {
            continue;
        }
        outcome.submitted = 0ul;
        result = v9x_gl_draw_into(context, which, &outcome);
        if (result != V9X_R3D_RESULT_OK) {
            v9x_gl_note_failure(result, pending, outcome.submitted);
        }
    }
    pending->triangles = 0ul;
    ++v9x_gl_count_flushes;
    v9x_gl_tsc_end(v9x_gl_in_vertex ? V9X_GL_TSC_FLUSH_IN_VERTEX
                                    : V9X_GL_TSC_FLUSH, flush_started);
    v9x_gl_tsc_wall();
    v9x_gl_counters_tick();
    LeaveCriticalSection(&v9x_gl_lock);
}

/*
 * The pipeline's sink: the batch joins the held one when it is drawn the
 * same way into the same buffers and fits, and otherwise the held one is
 * drawn and this one held in its place. GL_NONE holds nothing.
 */
static int v9x_gl_draw_batch_body(V9X_GL_CONTEXT *context,
                                  const V9X_R3D_ABI_VERTEX *vertices,
                                  const GLfloat *texcoords1,
                                  v9x_u32 triangle_count,
                                  DWORD *prep_started);

static int v9x_gl_draw_batch(void *user, const V9X_R3D_ABI_VERTEX *vertices,
                             const GLfloat *texcoords1,
                             v9x_u32 triangle_count)
{
    V9X_GL_CONTEXT *context = (V9X_GL_CONTEXT *)user;

    DWORD sink_started = v9x_gl_tsc_begin();
    DWORD prep_started;
    int result;

    ++v9x_gl_count_sinks;
    result = v9x_gl_draw_batch_body(context, vertices, texcoords1,
                                    triangle_count, &prep_started);
    v9x_gl_tsc_end(V9X_GL_TSC_SINK, sink_started);
    return result;
}

static int v9x_gl_draw_batch_body(V9X_GL_CONTEXT *context,
                                  const V9X_R3D_ABI_VERTEX *vertices,
                                  const GLfloat *texcoords1,
                                  v9x_u32 triangle_count,
                                  DWORD *prep_started)
{
    V9X_GL_PENDING *pending = &context->pending;
    V9X_R3D_ABI_TEXTURE texture;
    V9X_R3D_ABI_TEXTURE texture1;
    V9X_R3D_ABI_STATE state;
    unsigned int targets;
    v9x_u32 i;
    v9x_u32 level;
    int same;
    /* Unit 1 enabled with unit 0 off: its texture is the draw's one, in
     * the interface's unit 0, with its coordinates as tu/tv. */
    int unit1_alone = 0;
    GLuint name0 = context->textures.units[0].bound;
    DWORD part_started;

    if (v9x_gl_device_interface() == 0) {
        return 0;
    }
    targets = v9x_gl_state_draw_targets(&context->state);
    if (targets == 0u || triangle_count == 0ul) {
        return 1;
    }
    *prep_started = v9x_gl_tsc_begin();
    v9x_gl_describe_texture(context, 0ul, &texture, context->levels);
    texture1.storage = V9X_R3D_ABI_TEXTURE_NONE;
    if (texcoords1 != 0) {
        v9x_gl_describe_texture(context, 1ul, &texture1, context->levels1);
        if (texture1.storage == V9X_R3D_ABI_TEXTURE_NONE) {
            texcoords1 = 0;
        } else if (texture.storage == V9X_R3D_ABI_TEXTURE_NONE) {
            texture = texture1;
            texture1.storage = V9X_R3D_ABI_TEXTURE_NONE;
            name0 = context->textures.units[1].bound;
            unit1_alone = 1;
        }
    }
    part_started = v9x_gl_tsc_begin();
    v9x_gl_tsc_end(V9X_GL_TSC_PREP_TEXTURE, *prep_started);
    v9x_gl_prim_abi_state(&context->state, &context->pipeline, &state);
    v9x_gl_tsc_end(V9X_GL_TSC_PREP_STATE, part_started);
    part_started = v9x_gl_tsc_begin();
    same = pending->triangles == 0ul ||
           (pending->targets == targets &&
            pending->triangles + triangle_count <= V9X_R3D_ABI_BATCH_MAX &&
            v9x_gl_prim_same_draw(&pending->texture, &pending->state,
                                  &texture, &state) &&
            pending->texture1.storage == texture1.storage &&
            (texture1.storage == V9X_R3D_ABI_TEXTURE_NONE ||
             v9x_gl_prim_same_draw(&pending->texture1, &pending->state,
                                   &texture1, &state)));
    v9x_gl_tsc_end(V9X_GL_TSC_PREP_SAME, part_started);
    v9x_gl_tsc_end(V9X_GL_TSC_SINK_PREP, *prep_started);
    if (!same) {
        part_started = v9x_gl_tsc_begin();
        v9x_gl_pending_flush(context);
        v9x_gl_tsc_end(V9X_GL_TSC_SINK_FLUSH, part_started);
    }
    part_started = v9x_gl_tsc_begin();
    if (pending->triangles == 0ul) {
        pending->texture = texture;
        if (texture.storage == V9X_R3D_ABI_TEXTURE_CPU) {
            for (level = 0ul; level < texture.level_count; ++level) {
                pending->levels[level] = texture.levels[level];
            }
            pending->texture.levels = pending->levels;
        }
        pending->texture1 = texture1;
        if (texture1.storage == V9X_R3D_ABI_TEXTURE_CPU) {
            for (level = 0ul; level < texture1.level_count; ++level) {
                pending->levels1[level] = texture1.levels[level];
            }
            pending->texture1.levels = pending->levels1;
        }
        pending->texture1_name = context->textures.units[1].bound;
        pending->state = state;
        pending->targets = targets;
        pending->texture_name = name0;
        pending->alpha_used =
            v9x_gl_prim_fragment_alpha_used(&context->state,
                                            &context->pipeline);
    }
    for (i = 0ul; i < triangle_count * 3ul; ++i) {
        pending->vertices[pending->triangles * 3ul + i] = vertices[i];
    }
    if (unit1_alone) {
        for (i = 0ul; i < triangle_count * 3ul; ++i) {
            pending->vertices[pending->triangles * 3ul + i].tu =
                texcoords1[i * 2ul];
            pending->vertices[pending->triangles * 3ul + i].tv =
                texcoords1[i * 2ul + 1ul];
        }
    } else if (texture1.storage != V9X_R3D_ABI_TEXTURE_NONE) {
        for (i = 0ul; i < triangle_count * 6ul; ++i) {
            pending->texcoords1[pending->triangles * 6ul + i] = texcoords1[i];
        }
    }
    pending->triangles += triangle_count;
    v9x_gl_tsc_end(V9X_GL_TSC_SINK_COPY, part_started);
    if (pending->triangles == V9X_R3D_ABI_BATCH_MAX) {
        part_started = v9x_gl_tsc_begin();
        v9x_gl_pending_flush(context);
        v9x_gl_tsc_end(V9X_GL_TSC_SINK_FLUSH, part_started);
    }
    return 1;
}

#define V9X_GL_WITH_PIPELINE(call) do { \
    V9X_GL_CONTEXT *context_ = v9x_gl_current(); \
    if (context_ != 0) { \
        call; \
    } \
} while (0)

static void V9X_GL_API v9x_gl_begin(GLenum mode)
{
    DWORD started = v9x_gl_tsc_begin();
    V9X_GL_CONTEXT *context = v9x_gl_current();

    /* Unit 1's coordinates are carried only when they can be drawn; its
     * enable cannot change before glEnd. */
    if (context != 0 && !context->state.in_begin) {
        v9x_gl_pipeline_units(&context->pipeline,
                              context->textures.units[1].enabled &&
                                      v9x_gl_two_units()
                                  ? 2ul : 1ul);
    }
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_begin(&context_->state,
                                           &context_->pipeline, mode));
    v9x_gl_tsc_end(V9X_GL_TSC_BEGINEND, started);
    v9x_gl_tsc_end(V9X_GL_TSC_BEGIN, started);
}

static void V9X_GL_API v9x_gl_end(void)
{
    DWORD started = v9x_gl_tsc_begin();

    V9X_GL_WITH_PIPELINE(v9x_gl_prim_end(&context_->state,
                                         &context_->pipeline));
    v9x_gl_tsc_end(V9X_GL_TSC_BEGINEND, started);
}

static void v9x_gl_vertex4(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    DWORD started = v9x_gl_tsc_begin();

    v9x_gl_in_vertex = 1ul;
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_vertex(&context_->state,
                                            &context_->pipeline, x, y, z,
                                            w));
    v9x_gl_in_vertex = 0ul;
    ++v9x_gl_count_vertices;
    v9x_gl_tsc_end(V9X_GL_TSC_VERTEX, started);
}

static void V9X_GL_API v9x_gl_vertex2f(GLfloat x, GLfloat y)
{
    v9x_gl_vertex4(x, y, 0.0f, 1.0f);
}

static void V9X_GL_API v9x_gl_vertex2fv(const GLfloat *v)
{
    v9x_gl_vertex4(v[0], v[1], 0.0f, 1.0f);
}

static void V9X_GL_API v9x_gl_vertex2i(GLint x, GLint y)
{
    v9x_gl_vertex4((GLfloat)x, (GLfloat)y, 0.0f, 1.0f);
}

static void V9X_GL_API v9x_gl_vertex2d(GLdouble x, GLdouble y)
{
    v9x_gl_vertex4((GLfloat)x, (GLfloat)y, 0.0f, 1.0f);
}

static void V9X_GL_API v9x_gl_vertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    v9x_gl_vertex4(x, y, z, 1.0f);
}

static void V9X_GL_API v9x_gl_vertex3fv(const GLfloat *v)
{
    v9x_gl_vertex4(v[0], v[1], v[2], 1.0f);
}

static void V9X_GL_API v9x_gl_vertex3i(GLint x, GLint y, GLint z)
{
    v9x_gl_vertex4((GLfloat)x, (GLfloat)y, (GLfloat)z, 1.0f);
}

static void V9X_GL_API v9x_gl_vertex3d(GLdouble x, GLdouble y, GLdouble z)
{
    v9x_gl_vertex4((GLfloat)x, (GLfloat)y, (GLfloat)z, 1.0f);
}

static void V9X_GL_API v9x_gl_vertex4f(GLfloat x, GLfloat y, GLfloat z,
                                       GLfloat w)
{
    v9x_gl_vertex4(x, y, z, w);
}

static void V9X_GL_API v9x_gl_vertex4fv(const GLfloat *v)
{
    v9x_gl_vertex4(v[0], v[1], v[2], v[3]);
}

static void v9x_gl_color4(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_color(&context_->pipeline, r, g, b, a));
}

/* Unsigned bytes map c / 255 (table 2.6). */
#define V9X_GL_UB(value) ((GLfloat)(value) * (1.0f / 255.0f))

static void V9X_GL_API v9x_gl_color3f(GLfloat r, GLfloat g, GLfloat b)
{
    v9x_gl_color4(r, g, b, 1.0f);
}

static void V9X_GL_API v9x_gl_color3fv(const GLfloat *v)
{
    v9x_gl_color4(v[0], v[1], v[2], 1.0f);
}

static void V9X_GL_API v9x_gl_color3d(GLdouble r, GLdouble g, GLdouble b)
{
    v9x_gl_color4((GLfloat)r, (GLfloat)g, (GLfloat)b, 1.0f);
}

static void V9X_GL_API v9x_gl_color4f(GLfloat r, GLfloat g, GLfloat b,
                                      GLfloat a)
{
    v9x_gl_color4(r, g, b, a);
}

static void V9X_GL_API v9x_gl_color4fv(const GLfloat *v)
{
    v9x_gl_color4(v[0], v[1], v[2], v[3]);
}

static void V9X_GL_API v9x_gl_color3ub(GLubyte r, GLubyte g, GLubyte b)
{
    v9x_gl_color4(V9X_GL_UB(r), V9X_GL_UB(g), V9X_GL_UB(b), 1.0f);
}

static void V9X_GL_API v9x_gl_color3ubv(const GLubyte *v)
{
    v9x_gl_color4(V9X_GL_UB(v[0]), V9X_GL_UB(v[1]), V9X_GL_UB(v[2]), 1.0f);
}

static void V9X_GL_API v9x_gl_color4ub(GLubyte r, GLubyte g, GLubyte b,
                                       GLubyte a)
{
    v9x_gl_color4(V9X_GL_UB(r), V9X_GL_UB(g), V9X_GL_UB(b), V9X_GL_UB(a));
}

static void V9X_GL_API v9x_gl_color4ubv(const GLubyte *v)
{
    v9x_gl_color4(V9X_GL_UB(v[0]), V9X_GL_UB(v[1]), V9X_GL_UB(v[2]),
                  V9X_GL_UB(v[3]));
}

static void V9X_GL_API v9x_gl_texcoord2f(GLfloat s, GLfloat t)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_texcoord(&context_->pipeline, s, t,
                                              0.0f, 1.0f));
}

static void V9X_GL_API v9x_gl_texcoord2fv(const GLfloat *v)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_texcoord(&context_->pipeline, v[0], v[1],
                                              0.0f, 1.0f));
}

/*
 * GL_SGIS_multitexture, through DrvGetProcAddress: glSelectTextureSGIS and
 * glMTexCoord2f(v)SGIS, the three the census found GLQuake and Quake 2
 * load. Unit 0's coordinate is glTexCoord's; unit 1's is its own. A target
 * that is neither unit is INVALID_ENUM.
 */
static void V9X_GL_API v9x_gl_select_texture_sgis(GLenum target)
{
    DWORD started = v9x_gl_tsc_begin();
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (!v9x_gl_mtex_select_seen) {
        v9x_gl_mtex_select_seen = 1;
        v9x_gl_log3("sgis first glSelectTextureSGIS target=%04lX",
                    (DWORD)target, 0ul, 0ul);
    }
    if (context != 0) {
        v9x_gl_tex_select(&context->state, &context->textures, target);
    }
    ++v9x_gl_tsc_calls[V9X_GL_TSC_SGIS_SELECT];
    v9x_gl_tsc_end(V9X_GL_TSC_SGIS_SELECT, started);
}

static void v9x_gl_mtexcoord_body(GLenum target, GLfloat s, GLfloat t);

static void v9x_gl_mtexcoord(GLenum target, GLfloat s, GLfloat t)
{
    DWORD started = v9x_gl_tsc_begin();

    v9x_gl_mtexcoord_body(target, s, t);
    ++v9x_gl_tsc_calls[V9X_GL_TSC_SGIS_COORD];
    v9x_gl_tsc_end(V9X_GL_TSC_SGIS_COORD, started);
}

static void v9x_gl_mtexcoord_body(GLenum target, GLfloat s, GLfloat t)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (!v9x_gl_mtex_coord_seen) {
        v9x_gl_mtex_coord_seen = 1;
        v9x_gl_log3("sgis first glMTexCoord2fSGIS target=%04lX",
                    (DWORD)target, 0ul, 0ul);
    }
    if (context == 0) {
        return;
    }
    if (target == V9X_GL_TEXTURE0_SGIS) {
        v9x_gl_prim_texcoord(&context->pipeline, s, t, 0.0f, 1.0f);
    } else if (target == V9X_GL_TEXTURE1_SGIS) {
        v9x_gl_prim_texcoord1(&context->pipeline, s, t);
    } else {
        v9x_gl_state_error(&context->state, V9X_GL_INVALID_ENUM);
    }
}

static void V9X_GL_API v9x_gl_mtexcoord2f_sgis(GLenum target, GLfloat s,
                                               GLfloat t)
{
    v9x_gl_mtexcoord(target, s, t);
}

static void V9X_GL_API v9x_gl_mtexcoord2fv_sgis(GLenum target,
                                                const GLfloat *v)
{
    v9x_gl_mtexcoord(target, v[0], v[1]);
}

static void V9X_GL_API v9x_gl_shade_model(GLenum mode)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_shade_model(&context_->state,
                                                 &context_->pipeline, mode));
}

static void V9X_GL_API v9x_gl_cull_face(GLenum mode)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_cull_face(&context_->state,
                                               &context_->pipeline, mode));
}

static void V9X_GL_API v9x_gl_front_face(GLenum mode)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_front_face(&context_->state,
                                                &context_->pipeline, mode));
}

static void V9X_GL_API v9x_gl_depth_func(GLenum func)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_depth_func(&context_->state,
                                                &context_->pipeline, func));
}

static void V9X_GL_API v9x_gl_blend_func(GLenum src, GLenum dst)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_blend_func(&context_->state,
                                                &context_->pipeline, src,
                                                dst));
}

static void V9X_GL_API v9x_gl_alpha_func(GLenum func, GLclampf ref)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_alpha_func(&context_->state,
                                                &context_->pipeline, func,
                                                ref));
}

static void V9X_GL_API v9x_gl_depth_range(GLclampd near_value,
                                          GLclampd far_value)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_depth_range(&context_->state,
                                                 &context_->pipeline,
                                                 near_value, far_value));
}

/* ---- Normals and vertex arrays (2.7, 2.8), gl_varray.c --------------- */

static void V9X_GL_API v9x_gl_normal3f(GLfloat x, GLfloat y, GLfloat z)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_normal(&context_->pipeline, x, y, z));
}

static void V9X_GL_API v9x_gl_normal3fv(const GLfloat *v)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_prim_normal(&context_->pipeline, v[0], v[1],
                                            v[2]));
}

static void V9X_GL_API v9x_gl_enable_client_state(GLenum cap)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_arrays_client_state(&context_->state,
                                                    &context_->arrays, cap,
                                                    1));
}

static void V9X_GL_API v9x_gl_disable_client_state(GLenum cap)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_arrays_client_state(&context_->state,
                                                    &context_->arrays, cap,
                                                    0));
}

static void V9X_GL_API v9x_gl_vertex_pointer(GLint size, GLenum type,
                                             GLsizei stride,
                                             const GLvoid *pointer)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_arrays_pointer(&context_->state,
                                               &context_->arrays,
                                               V9X_GL_ARRAY_VERTEX, size,
                                               type, stride, pointer));
}

static void V9X_GL_API v9x_gl_normal_pointer(GLenum type, GLsizei stride,
                                             const GLvoid *pointer)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_arrays_pointer(&context_->state,
                                               &context_->arrays,
                                               V9X_GL_ARRAY_NORMAL, 3, type,
                                               stride, pointer));
}

static void V9X_GL_API v9x_gl_color_pointer(GLint size, GLenum type,
                                            GLsizei stride,
                                            const GLvoid *pointer)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_arrays_pointer(&context_->state,
                                               &context_->arrays,
                                               V9X_GL_ARRAY_COLOR, size,
                                               type, stride, pointer));
}

static void V9X_GL_API v9x_gl_index_pointer(GLenum type, GLsizei stride,
                                            const GLvoid *pointer)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_arrays_pointer(&context_->state,
                                               &context_->arrays,
                                               V9X_GL_ARRAY_INDEX, 1, type,
                                               stride, pointer));
}

static void V9X_GL_API v9x_gl_texcoord_pointer(GLint size, GLenum type,
                                               GLsizei stride,
                                               const GLvoid *pointer)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_arrays_pointer(&context_->state,
                                               &context_->arrays,
                                               V9X_GL_ARRAY_TEXCOORD, size,
                                               type, stride, pointer));
}

static void V9X_GL_API v9x_gl_edge_flag_pointer(GLsizei stride,
                                                const GLvoid *pointer)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_arrays_edge_flag_pointer(&context_->state,
                                                         &context_->arrays,
                                                         stride, pointer));
}

static void V9X_GL_API v9x_gl_get_pointerv(GLenum pname, GLvoid **out)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_arrays_get_pointer(&context_->state,
                                                   &context_->arrays, pname,
                                                   out));
}

static void V9X_GL_API v9x_gl_array_element(GLint index)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_arrays_element(&context_->state,
                                               &context_->pipeline,
                                               &context_->arrays, index));
}

static void V9X_GL_API v9x_gl_draw_arrays(GLenum mode, GLint first,
                                          GLsizei count)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_arrays_draw(&context_->state,
                                            &context_->pipeline,
                                            &context_->arrays, mode, first,
                                            count));
}

static void V9X_GL_API v9x_gl_draw_elements(GLenum mode, GLsizei count,
                                            GLenum type,
                                            const GLvoid *indices)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_arrays_draw_elements(&context_->state,
                                                     &context_->pipeline,
                                                     &context_->arrays, mode,
                                                     count, type, indices));
}

static void V9X_GL_API v9x_gl_interleaved_arrays(GLenum format,
                                                 GLsizei stride,
                                                 const GLvoid *pointer)
{
    V9X_GL_WITH_PIPELINE(v9x_gl_arrays_interleaved(&context_->state,
                                                   &context_->arrays, format,
                                                   stride, pointer));
}

/* ---- Queries and held state (6.1, 5.6, 3.5.4, 4.2.1), gl_get.c ------- */

static void v9x_gl_get_any(GLenum pname, int kind, void *out)
{
    V9X_GL_CONTEXT *context = v9x_gl_current();

    if (context != 0) {
        v9x_gl_get(&context->state, &context->pipeline, &context->textures,
                   pname, kind, out);
    }
}

static void V9X_GL_API v9x_gl_get_booleanv(GLenum pname, GLboolean *out)
{
    v9x_gl_get_any(pname, V9X_GL_GET_BOOLEAN, out);
}

static void V9X_GL_API v9x_gl_get_integerv(GLenum pname, GLint *out)
{
    v9x_gl_get_any(pname, V9X_GL_GET_INTEGER, out);
}

static void V9X_GL_API v9x_gl_get_floatv(GLenum pname, GLfloat *out)
{
    v9x_gl_get_any(pname, V9X_GL_GET_FLOAT, out);
}

static void V9X_GL_API v9x_gl_get_doublev(GLenum pname, GLdouble *out)
{
    v9x_gl_get_any(pname, V9X_GL_GET_DOUBLE, out);
}

static void V9X_GL_API v9x_gl_hint(GLenum target, GLenum mode)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_hint(&context_->state, target, mode));
}

static void V9X_GL_API v9x_gl_polygon_mode(GLenum face, GLenum mode)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_polygon_mode(&context_->state, face,
                                                  mode));
}

static void V9X_GL_API v9x_gl_draw_buffer(GLenum buffer)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_draw_buffer(&context_->state, buffer));
}

static void V9X_GL_API v9x_gl_read_buffer(GLenum buffer)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_read_buffer(&context_->state, buffer));
}

static void V9X_GL_API v9x_gl_line_width(GLfloat width)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_line_width(&context_->state, width));
}

static void V9X_GL_API v9x_gl_point_size(GLfloat size)
{
    V9X_GL_WITH_CONTEXT(v9x_gl_state_point_size(&context_->state, size));
}

/* ---- The table ----------------------------------------------------- */

static void v9x_gl_set_slot(const char *name, V9X_GL_PROC proc)
{
    unsigned int slot;

    for (slot = 0u; slot < V9X_GL_SLOT_COUNT; ++slot) {
        if (lstrcmpA(v9x_gl_slot_names[slot], name) == 0) {
            v9x_gl_table.entries[slot] = proc;
            return;
        }
    }
    v9x_gl_log(name);
}

/* Each override is assigned to a local of its slot's generated type before
 * it is stored, so a signature that does not match the slot - the stack
 * cleanup a caller relies on - cannot compile. */
#define V9X_GL_OVERRIDE(slot_name, function) do { \
    V9X_GL_PFN_##slot_name typed_ = function; \
    v9x_gl_set_slot(#slot_name, (V9X_GL_PROC)typed_); \
} while (0)

static void v9x_gl_install_overrides(void)
{
    if (v9x_gl_overrides_installed) {
        return;
    }
    v9x_gl_overrides_installed = 1;
    V9X_GL_OVERRIDE(glGetString, v9x_gl_get_string);
    V9X_GL_OVERRIDE(glGetError, v9x_gl_get_error);
    V9X_GL_OVERRIDE(glClearColor, v9x_gl_clear_color);
    V9X_GL_OVERRIDE(glClearDepth, v9x_gl_clear_depth);
    V9X_GL_OVERRIDE(glViewport, v9x_gl_viewport);
    V9X_GL_OVERRIDE(glScissor, v9x_gl_scissor);
    V9X_GL_OVERRIDE(glColorMask, v9x_gl_color_mask);
    V9X_GL_OVERRIDE(glDepthMask, v9x_gl_depth_mask);
    V9X_GL_OVERRIDE(glEnable, v9x_gl_enable);
    V9X_GL_OVERRIDE(glDisable, v9x_gl_disable);
    V9X_GL_OVERRIDE(glIsEnabled, v9x_gl_is_enabled);
    V9X_GL_OVERRIDE(glClear, v9x_gl_clear);
    V9X_GL_OVERRIDE(glFinish, v9x_gl_finish);
    V9X_GL_OVERRIDE(glFlush, v9x_gl_flush);
    V9X_GL_OVERRIDE(glReadPixels, v9x_gl_read_pixels);
    V9X_GL_OVERRIDE(glNormal3f, v9x_gl_normal3f);
    V9X_GL_OVERRIDE(glNormal3fv, v9x_gl_normal3fv);
    V9X_GL_OVERRIDE(glEnableClientState, v9x_gl_enable_client_state);
    V9X_GL_OVERRIDE(glDisableClientState, v9x_gl_disable_client_state);
    V9X_GL_OVERRIDE(glVertexPointer, v9x_gl_vertex_pointer);
    V9X_GL_OVERRIDE(glNormalPointer, v9x_gl_normal_pointer);
    V9X_GL_OVERRIDE(glColorPointer, v9x_gl_color_pointer);
    V9X_GL_OVERRIDE(glIndexPointer, v9x_gl_index_pointer);
    V9X_GL_OVERRIDE(glTexCoordPointer, v9x_gl_texcoord_pointer);
    V9X_GL_OVERRIDE(glEdgeFlagPointer, v9x_gl_edge_flag_pointer);
    V9X_GL_OVERRIDE(glGetPointerv, v9x_gl_get_pointerv);
    V9X_GL_OVERRIDE(glArrayElement, v9x_gl_array_element);
    V9X_GL_OVERRIDE(glDrawArrays, v9x_gl_draw_arrays);
    V9X_GL_OVERRIDE(glDrawElements, v9x_gl_draw_elements);
    V9X_GL_OVERRIDE(glInterleavedArrays, v9x_gl_interleaved_arrays);
    V9X_GL_OVERRIDE(glMatrixMode, v9x_gl_matrix_mode);
    V9X_GL_OVERRIDE(glLoadIdentity, v9x_gl_load_identity);
    V9X_GL_OVERRIDE(glLoadMatrixf, v9x_gl_load_matrixf);
    V9X_GL_OVERRIDE(glLoadMatrixd, v9x_gl_load_matrixd);
    V9X_GL_OVERRIDE(glMultMatrixf, v9x_gl_mult_matrixf);
    V9X_GL_OVERRIDE(glMultMatrixd, v9x_gl_mult_matrixd);
    V9X_GL_OVERRIDE(glTranslatef, v9x_gl_translatef);
    V9X_GL_OVERRIDE(glTranslated, v9x_gl_translated);
    V9X_GL_OVERRIDE(glScalef, v9x_gl_scalef);
    V9X_GL_OVERRIDE(glScaled, v9x_gl_scaled);
    V9X_GL_OVERRIDE(glRotatef, v9x_gl_rotatef);
    V9X_GL_OVERRIDE(glRotated, v9x_gl_rotated);
    V9X_GL_OVERRIDE(glFrustum, v9x_gl_frustum);
    V9X_GL_OVERRIDE(glOrtho, v9x_gl_ortho);
    V9X_GL_OVERRIDE(glPushMatrix, v9x_gl_push_matrix);
    V9X_GL_OVERRIDE(glPopMatrix, v9x_gl_pop_matrix);
    V9X_GL_OVERRIDE(glBegin, v9x_gl_begin);
    V9X_GL_OVERRIDE(glEnd, v9x_gl_end);
    V9X_GL_OVERRIDE(glVertex2f, v9x_gl_vertex2f);
    V9X_GL_OVERRIDE(glVertex2fv, v9x_gl_vertex2fv);
    V9X_GL_OVERRIDE(glVertex2i, v9x_gl_vertex2i);
    V9X_GL_OVERRIDE(glVertex2d, v9x_gl_vertex2d);
    V9X_GL_OVERRIDE(glVertex3f, v9x_gl_vertex3f);
    V9X_GL_OVERRIDE(glVertex3fv, v9x_gl_vertex3fv);
    V9X_GL_OVERRIDE(glVertex3i, v9x_gl_vertex3i);
    V9X_GL_OVERRIDE(glVertex3d, v9x_gl_vertex3d);
    V9X_GL_OVERRIDE(glVertex4f, v9x_gl_vertex4f);
    V9X_GL_OVERRIDE(glVertex4fv, v9x_gl_vertex4fv);
    V9X_GL_OVERRIDE(glColor3f, v9x_gl_color3f);
    V9X_GL_OVERRIDE(glColor3fv, v9x_gl_color3fv);
    V9X_GL_OVERRIDE(glColor3d, v9x_gl_color3d);
    V9X_GL_OVERRIDE(glColor4f, v9x_gl_color4f);
    V9X_GL_OVERRIDE(glColor4fv, v9x_gl_color4fv);
    V9X_GL_OVERRIDE(glColor3ub, v9x_gl_color3ub);
    V9X_GL_OVERRIDE(glColor3ubv, v9x_gl_color3ubv);
    V9X_GL_OVERRIDE(glColor4ub, v9x_gl_color4ub);
    V9X_GL_OVERRIDE(glColor4ubv, v9x_gl_color4ubv);
    V9X_GL_OVERRIDE(glTexCoord2f, v9x_gl_texcoord2f);
    V9X_GL_OVERRIDE(glTexCoord2fv, v9x_gl_texcoord2fv);
    V9X_GL_OVERRIDE(glShadeModel, v9x_gl_shade_model);
    V9X_GL_OVERRIDE(glCullFace, v9x_gl_cull_face);
    V9X_GL_OVERRIDE(glFrontFace, v9x_gl_front_face);
    V9X_GL_OVERRIDE(glDepthFunc, v9x_gl_depth_func);
    V9X_GL_OVERRIDE(glBlendFunc, v9x_gl_blend_func);
    V9X_GL_OVERRIDE(glAlphaFunc, v9x_gl_alpha_func);
    V9X_GL_OVERRIDE(glDepthRange, v9x_gl_depth_range);
    V9X_GL_OVERRIDE(glGenTextures, v9x_gl_gen_textures);
    V9X_GL_OVERRIDE(glDeleteTextures, v9x_gl_delete_textures);
    V9X_GL_OVERRIDE(glIsTexture, v9x_gl_is_texture);
    V9X_GL_OVERRIDE(glBindTexture, v9x_gl_bind_texture);
    V9X_GL_OVERRIDE(glTexParameteri, v9x_gl_tex_parameteri);
    V9X_GL_OVERRIDE(glTexParameterf, v9x_gl_tex_parameterf);
    V9X_GL_OVERRIDE(glTexParameteriv, v9x_gl_tex_parameteriv);
    V9X_GL_OVERRIDE(glTexParameterfv, v9x_gl_tex_parameterfv);
    V9X_GL_OVERRIDE(glTexEnvf, v9x_gl_tex_envf);
    V9X_GL_OVERRIDE(glTexEnvi, v9x_gl_tex_envi);
    V9X_GL_OVERRIDE(glTexEnvfv, v9x_gl_tex_envfv);
    V9X_GL_OVERRIDE(glTexEnviv, v9x_gl_tex_enviv);
    V9X_GL_OVERRIDE(glPixelStorei, v9x_gl_pixel_storei);
    V9X_GL_OVERRIDE(glPixelStoref, v9x_gl_pixel_storef);
    V9X_GL_OVERRIDE(glTexImage2D, v9x_gl_api_tex_image_2d);
    V9X_GL_OVERRIDE(glTexSubImage2D, v9x_gl_api_tex_sub_image_2d);
    V9X_GL_OVERRIDE(glGetBooleanv, v9x_gl_get_booleanv);
    V9X_GL_OVERRIDE(glGetIntegerv, v9x_gl_get_integerv);
    V9X_GL_OVERRIDE(glGetFloatv, v9x_gl_get_floatv);
    V9X_GL_OVERRIDE(glGetDoublev, v9x_gl_get_doublev);
    V9X_GL_OVERRIDE(glHint, v9x_gl_hint);
    V9X_GL_OVERRIDE(glPolygonMode, v9x_gl_polygon_mode);
    V9X_GL_OVERRIDE(glDrawBuffer, v9x_gl_draw_buffer);
    V9X_GL_OVERRIDE(glReadBuffer, v9x_gl_read_buffer);
    V9X_GL_OVERRIDE(glLineWidth, v9x_gl_line_width);
    V9X_GL_OVERRIDE(glPointSize, v9x_gl_point_size);
}

/* ---- Pixel formats ------------------------------------------------- */

/* The formats offered: one, when the engine can write the desktop's layout,
 * else none. Opening the device here is deliberate: DescribePixelFormat is
 * the runtime's first question, and the answer depends on the engine. */
static int v9x_gl_format_count(void)
{
    int count;

    EnterCriticalSection(&v9x_gl_lock);
    count = v9x_gl_device_open() && v9x_gl_device_format() != 0ul ? 1 : 0;
    LeaveCriticalSection(&v9x_gl_lock);
    return count;
}

static void v9x_gl_describe(PIXELFORMATDESCRIPTOR *pfd)
{
    unsigned int i;
    int is_565 = v9x_gl_device_format() == V9X_R3D_ABI_FORMAT_RGB565;

    for (i = 0u; i < sizeof(*pfd); ++i) {
        ((BYTE *)pfd)[i] = 0u;
    }
    pfd->nSize = sizeof(*pfd);
    pfd->nVersion = 1;
    /* Neither PFD_GENERIC_ flag: that is what marks the format as the
     * ICD's. */
    pfd->dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER |
                   PFD_SWAP_COPY;
    pfd->iPixelType = PFD_TYPE_RGBA;
    pfd->cColorBits = 16;
    pfd->cRedBits = 5;
    pfd->cRedShift = (BYTE)(is_565 ? 11 : 10);
    pfd->cGreenBits = (BYTE)(is_565 ? 6 : 5);
    pfd->cGreenShift = 5;
    pfd->cBlueBits = 5;
    pfd->cBlueShift = 0;
    pfd->cDepthBits = 16;
    pfd->iLayerType = PFD_MAIN_PLANE;
}

/* ---- Contexts ------------------------------------------------------ */

static HWND v9x_gl_windows[V9X_GL_CONTEXTS_MAX];

static HWND v9x_gl_context_window(const V9X_GL_CONTEXT *context)
{
    return v9x_gl_windows[context - v9x_gl_contexts];
}

static V9X_GL_CONTEXT *v9x_gl_context_of(V9X_DHGLRC handle)
{
    if (handle == 0ul || handle > V9X_GL_CONTEXTS_MAX ||
        !v9x_gl_contexts[handle - 1ul].in_use) {
        return 0;
    }
    return &v9x_gl_contexts[handle - 1ul];
}

static V9X_DHGLRC v9x_gl_context_create(HDC hdc)
{
    unsigned int index;
    V9X_DHGLRC handle = 0ul;

    EnterCriticalSection(&v9x_gl_lock);
    if (v9x_gl_device_open() && v9x_gl_device_format() != 0ul) {
        for (index = 0u; index < V9X_GL_CONTEXTS_MAX; ++index) {
            if (!v9x_gl_contexts[index].in_use) {
                V9X_GL_CONTEXT *context = &v9x_gl_contexts[index];

                context->in_use = 1;
                context->owner = 0ul;
                context->drawable = 0;
                context->bound_once = 0;
                v9x_gl_state_init(&context->state);
                v9x_gl_pipeline_init(&context->pipeline);
                v9x_gl_arrays_init(&context->arrays);
                v9x_gl_textures_init(&context->textures, v9x_gl_heap_alloc,
                                     v9x_gl_heap_free);
                context->textures.hw_release = v9x_gl_hwtex_free;
                v9x_gl_pipeline_sink(&context->pipeline, v9x_gl_draw_batch,
                                     context);
                if (v9x_gl_tsc_usable()) {
                    context->pipeline.profile = v9x_gl_prim_profile;
                }
                v9x_gl_windows[index] = WindowFromDC(hdc);
                handle = (V9X_DHGLRC)(index + 1u);
                break;
            }
        }
    }
    LeaveCriticalSection(&v9x_gl_lock);
    return handle;
}

BOOL __stdcall DrvValidateVersion(ULONG version)
{
    v9x_gl_log3("DrvValidateVersion version=%lu", version, 0ul, 0ul);
    return TRUE;
}

void __stdcall DrvSetCallbackProcs(INT count, PROC *procs)
{
    v9x_gl_log3("DrvSetCallbackProcs count=%ld procs=%08lX", (DWORD)count,
                (DWORD)procs, 0ul);
}

LONG __stdcall DrvDescribePixelFormat(HDC hdc, INT index, ULONG bytes,
                                      PIXELFORMATDESCRIPTOR *pfd)
{
    int count = v9x_gl_format_count();

    v9x_gl_log3("DrvDescribePixelFormat index=%ld bytes=%lu -> count=%ld",
                (DWORD)index, bytes, (DWORD)count);
    (void)hdc;
    if (pfd != 0 && count != 0 && index == 1 && bytes >= sizeof(*pfd)) {
        v9x_gl_describe(pfd);
    }
    return count;
}

BOOL __stdcall DrvSetPixelFormat(HDC hdc, LONG index)
{
    v9x_gl_log3("DrvSetPixelFormat hdc=%08lX index=%ld", (DWORD)hdc,
                (DWORD)index, 0ul);
    return index == 1 && v9x_gl_format_count() == 1 ? TRUE : FALSE;
}

V9X_DHGLRC __stdcall DrvCreateContext(HDC hdc)
{
    V9X_DHGLRC context = v9x_gl_context_create(hdc);

    v9x_gl_log3("DrvCreateContext hdc=%08lX -> %lu", (DWORD)hdc, context, 0ul);
    return context;
}

/*
 * 98SE's OPENGL32 creates every context through this entry with layer 0 and
 * never calls DrvCreateContext (run1, 2026-09-26). Layer 0 is the main
 * plane, so it is the same context DrvCreateContext would make; other
 * planes are not offered.
 */
V9X_DHGLRC __stdcall DrvCreateLayerContext(HDC hdc, INT layer)
{
    V9X_DHGLRC context = layer == 0 ? v9x_gl_context_create(hdc) : 0ul;

    v9x_gl_log3("DrvCreateLayerContext hdc=%08lX layer=%ld -> %lu",
                (DWORD)hdc, (DWORD)layer, context);
    return context;
}

BOOL __stdcall DrvDeleteContext(V9X_DHGLRC handle)
{
    V9X_GL_CONTEXT *context;
    BOOL ok = FALSE;

    EnterCriticalSection(&v9x_gl_lock);
    context = v9x_gl_context_of(handle);
    /* A context current on another thread is not this thread's to delete;
     * one current here is released first. */
    if (context != 0 &&
        (context->owner == 0ul || context->owner == GetCurrentThreadId())) {
        if (context->owner == GetCurrentThreadId()) {
            v9x_gl_pending_flush(context);
        }
        context->pending.triangles = 0ul;
        if (v9x_gl_current() == context) {
            TlsSetValue(v9x_gl_tls, 0);
        }
        v9x_gl_textures_release(&context->textures);
        context->in_use = 0;
        context->owner = 0ul;
        context->drawable = 0;
        ok = TRUE;
    }
    LeaveCriticalSection(&v9x_gl_lock);
    v9x_gl_counters_log();
    v9x_gl_log3("DrvDeleteContext context=%lu -> %lu", handle, (DWORD)ok, 0ul);
    return ok;
}

V9X_GLCLTPROCTABLE * __stdcall DrvSetContext(HDC hdc, V9X_DHGLRC handle,
                                             V9X_PFN_SETPROCTABLE set_table)
{
    V9X_GL_CONTEXT *context;
    V9X_GL_CONTEXT *previous = v9x_gl_current();
    V9X_GLCLTPROCTABLE *table = 0;
    DWORD thread = GetCurrentThreadId();
    HWND window = WindowFromDC(hdc);

    (void)set_table;
    if (previous != 0) {
        v9x_gl_pending_flush(previous);
    }
    EnterCriticalSection(&v9x_gl_lock);
    context = v9x_gl_context_of(handle);
    if (context != 0 && (context->owner == 0ul || context->owner == thread) &&
        window != 0) {
        v9x_gl_windows[context - v9x_gl_contexts] = window;
        if (v9x_gl_bind_window(context, window) != 0) {
            if (previous != 0 && previous != context) {
                previous->owner = 0ul;
            }
            context->owner = thread;
            TlsSetValue(v9x_gl_tls, context);
            v9x_gl_install_overrides();
            table = &v9x_gl_table;
        }
    }
    LeaveCriticalSection(&v9x_gl_lock);
    v9x_gl_log3("DrvSetContext hdc=%08lX context=%lu -> %lu", (DWORD)hdc,
                handle, (DWORD)(table != 0));
    return table;
}

BOOL __stdcall DrvReleaseContext(V9X_DHGLRC handle)
{
    V9X_GL_CONTEXT *context;
    BOOL ok = FALSE;

    context = v9x_gl_context_of(handle);
    if (context != 0 && context->owner == GetCurrentThreadId()) {
        v9x_gl_pending_flush(context);
    }
    EnterCriticalSection(&v9x_gl_lock);
    context = v9x_gl_context_of(handle);
    if (context != 0 && context->owner == GetCurrentThreadId()) {
        context->owner = 0ul;
        TlsSetValue(v9x_gl_tls, 0);
        ok = TRUE;
    }
    LeaveCriticalSection(&v9x_gl_lock);
    v9x_gl_log3("DrvReleaseContext context=%lu -> %lu", handle, (DWORD)ok,
                0ul);
    return ok;
}

/* Not yet: state copies and shared lists arrive with the state and texture
 * objects they would copy and share (plan, Phases 4 and 6). Declined, not
 * falsely accepted. */
BOOL __stdcall DrvCopyContext(V9X_DHGLRC source, V9X_DHGLRC target, UINT mask)
{
    v9x_gl_log3("DrvCopyContext source=%lu target=%lu mask=%08lX -> 0",
                source, target, (DWORD)mask);
    return FALSE;
}

BOOL __stdcall DrvShareLists(V9X_DHGLRC first, V9X_DHGLRC second)
{
    v9x_gl_log3("DrvShareLists first=%lu second=%lu -> 0", first, second, 0ul);
    return FALSE;
}

BOOL __stdcall DrvSwapBuffers(HDC hdc)
{
    V9X_GL_DRAWABLE *drawable;
    BOOL ok = FALSE;

    if (v9x_gl_current() != 0) {
        v9x_gl_pending_flush(v9x_gl_current());
    }
    EnterCriticalSection(&v9x_gl_lock);
    drawable = v9x_gl_drawable_find(WindowFromDC(hdc));
    if (drawable != 0) {
        /* OPENGL32 has already called glFinish (Phase 0.9), so everything
         * drawn is complete before the Blt reads it. */
        DWORD present_start = v9x_gl_ticks();

        DWORD swap_started = v9x_gl_tsc_begin();

        ok = v9x_gl_drawable_present(drawable) ? TRUE : FALSE;
        v9x_gl_present_ticks += v9x_gl_ticks() - present_start;
        v9x_gl_tsc_end(V9X_GL_TSC_SWAP, swap_started);
        v9x_gl_tsc_wall();
        ++v9x_gl_swaps;
    }
    LeaveCriticalSection(&v9x_gl_lock);
    if (!ok) {
        v9x_gl_log3("DrvSwapBuffers hdc=%08lX -> 0", (DWORD)hdc, 0ul, 0ul);
    }
    return ok;
}

BOOL __stdcall DrvSwapLayerBuffers(HDC hdc, UINT planes)
{
    v9x_gl_log3("DrvSwapLayerBuffers hdc=%08lX planes=%08lX", (DWORD)hdc,
                (DWORD)planes, 0ul);
    return FALSE;
}

BOOL __stdcall DrvDescribeLayerPlane(HDC hdc, INT index, INT layer,
                                     UINT bytes, LAYERPLANEDESCRIPTOR *descriptor)
{
    (void)hdc;
    (void)index;
    (void)layer;
    (void)bytes;
    (void)descriptor;
    return FALSE;
}

INT __stdcall DrvSetLayerPaletteEntries(HDC hdc, INT layer, INT start,
                                        INT count, const COLORREF *entries)
{
    (void)hdc;
    (void)layer;
    (void)start;
    (void)count;
    (void)entries;
    return 0;
}

INT __stdcall DrvGetLayerPaletteEntries(HDC hdc, INT layer, INT start,
                                        INT count, COLORREF *entries)
{
    (void)hdc;
    (void)layer;
    (void)start;
    (void)count;
    (void)entries;
    return 0;
}

BOOL __stdcall DrvRealizeLayerPalette(HDC hdc, INT layer, BOOL realize)
{
    (void)hdc;
    (void)layer;
    (void)realize;
    return FALSE;
}

/*
 * An extension's entry point, only for an extension GL_EXTENSIONS offers:
 * GLQuake enables glColorTableEXT on any non-null answer whether or not
 * the extension is advertised, so every other name must be null.
 */
PROC __stdcall DrvGetProcAddress(LPCSTR name)
{
    char text[200];
    PROC proc = 0;

    if (name != 0 && v9x_gl_two_units()) {
        if (lstrcmpA(name, "glSelectTextureSGIS") == 0) {
            proc = (PROC)v9x_gl_select_texture_sgis;
        } else if (lstrcmpA(name, "glMTexCoord2fSGIS") == 0) {
            proc = (PROC)v9x_gl_mtexcoord2f_sgis;
        } else if (lstrcmpA(name, "glMTexCoord2fvSGIS") == 0) {
            proc = (PROC)v9x_gl_mtexcoord2fv_sgis;
        }
    }
    wsprintfA(text, "DrvGetProcAddress name=%s -> %s",
              name != 0 ? name : "(null)", proc != 0 ? "entry" : "NULL");
    v9x_gl_log(text);
    return proc;
}

BOOL __stdcall V9xGlEntry(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        CreateDirectoryA(V9X_DIAG_DIR, 0);
        InitializeCriticalSection(&v9x_gl_lock);
        v9x_gl_tls = TlsAlloc();
        v9x_gl_log3("attach instance=%08lX tls=%lu build-marker=%lu",
                    (DWORD)instance, v9x_gl_tls,
                    (DWORD)(v9x_gl_build_id[0] != '\0'));
    } else if (reason == DLL_PROCESS_DETACH) {
        DWORD slot;
        DWORD distinct = 0ul;
        DWORD total = 0ul;

        for (slot = 0ul; slot < V9X_GL_SLOT_COUNT; ++slot) {
            if (v9x_gl_slot_calls[slot] != 0ul) {
                ++distinct;
                total += v9x_gl_slot_calls[slot];
            }
        }
        v9x_gl_log3("detach slots-called=%lu stub-calls=%lu", distinct, total,
                    0ul);
        if (v9x_gl_tls != 0xfffffffful) {
            TlsFree(v9x_gl_tls);
        }
        DeleteCriticalSection(&v9x_gl_lock);
    }
    return TRUE;
}
