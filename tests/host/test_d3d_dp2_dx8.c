/*
 * The DirectX 8 half of the DrawPrimitives2 walker (d3d_dp2.h): the DX8
 * records' lengths, and v9x_dp2_draw, which turns a draw from a stream
 * into the sink's calls. Lengths are the DDK's d3dhal.h, tabled in
 * docs\specifications\ddi8-on-win9x.md section 5.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/display32/d3d/d3d_dp2.h"

static unsigned int failures;
#define CHECK(e) do { if (!(e)) { ++failures; \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
} } while (0)

#define STRIDE 20ul
#define POOL 16ul

static v9x_u8 pool[POOL * STRIDE];

static v9x_u32 command_words[256];
#define COMMANDS ((v9x_u8 *)command_words)
static v9x_u32 used;

static void put8(v9x_u32 value) { COMMANDS[used++] = (v9x_u8)value; }
static void put16(v9x_u32 value) { put8(value & 0xfful); put8(value >> 8); }
static void put32(v9x_u32 value) { put16(value & 0xfffful); put16(value >> 16); }
static void command(v9x_u32 op, v9x_u32 count)
{
    put8(op); put8(0ul); put16(count);
}
static void zeros(v9x_u32 bytes)
{
    v9x_u32 i;

    for (i = 0ul; i < bytes; ++i) {
        put8(0ul);
    }
}

/* What the sink was handed: triangles, lines and points as vertex
 * numbers, and the DX8 records as opcode and length. */
static v9x_u32 tris[16][3];
static v9x_u32 tri_count;
static v9x_u32 lines[8][2];
static v9x_u32 line_count;
static v9x_u32 points[8];
static v9x_u32 point_count;
static v9x_u32 dx8_ops[32];
static v9x_u32 dx8_lengths[32];
static v9x_u32 dx8_counts[32];
static v9x_u32 dx8_calls;
static int dx8_answer = 1;
static int indexed_take;
static v9x_u32 indexed_calls;
static const v9x_u8 *indexed_first;
static const v9x_u8 *indexed_words;
static v9x_u32 indexed_triangles;

static v9x_u32 number(const v9x_u8 *v)
{
    if (v >= pool && v < pool + sizeof(pool)) {
        return (v9x_u32)((v - pool) / STRIDE);
    }
    return 999ul;
}

static void add_tri(v9x_u32 a, v9x_u32 b, v9x_u32 c)
{
    if (tri_count < 16ul) {
        tris[tri_count][0] = a;
        tris[tri_count][1] = b;
        tris[tri_count][2] = c;
    }
    ++tri_count;
}

static void on_state(void *user, v9x_u32 state, v9x_u32 value)
{
    (void)user; (void)state; (void)value;
}

static void on_stage(void *user, v9x_u32 stage, v9x_u32 state,
                     v9x_u32 value)
{
    (void)user; (void)stage; (void)state; (void)value;
}

static void on_triangle(void *user, const v9x_u8 *a, const v9x_u8 *b,
                        const v9x_u8 *c)
{
    (void)user;
    add_tri(number(a), number(b), number(c));
}

static void on_list(void *user, const v9x_u8 *first, v9x_u32 triangles)
{
    v9x_u32 i;

    (void)user;
    for (i = 0ul; i < triangles; ++i) {
        add_tri(number(first + (i * 3ul) * STRIDE),
                number(first + (i * 3ul + 1ul) * STRIDE),
                number(first + (i * 3ul + 2ul) * STRIDE));
    }
}

static void on_fan(void *user, const v9x_u8 *first, v9x_u32 vertices)
{
    v9x_u32 i;

    (void)user;
    for (i = 1ul; i + 1ul < vertices; ++i) {
        add_tri(number(first), number(first + i * STRIDE),
                number(first + (i + 1ul) * STRIDE));
    }
}

static void on_line(void *user, const v9x_u8 *a, const v9x_u8 *b)
{
    (void)user;
    if (line_count < 8ul) {
        lines[line_count][0] = number(a);
        lines[line_count][1] = number(b);
    }
    ++line_count;
}

static void on_point(void *user, const v9x_u8 *v)
{
    (void)user;
    if (point_count < 8ul) {
        points[point_count] = number(v);
    }
    ++point_count;
}

static int on_indexed(void *user, const v9x_u8 *first, v9x_u32 base,
                      const v9x_u8 *indices, v9x_u32 triangles)
{
    (void)user; (void)base;
    ++indexed_calls;
    indexed_first = first;
    indexed_words = indices;
    indexed_triangles = triangles;
    return indexed_take;
}

static int on_dx8(void *user, v9x_u32 op, v9x_u32 count,
                  const v9x_u8 *payload, v9x_u32 bytes)
{
    (void)user; (void)payload;
    if (dx8_calls < 32ul) {
        dx8_ops[dx8_calls] = op;
        dx8_lengths[dx8_calls] = bytes;
        dx8_counts[dx8_calls] = count;
    }
    ++dx8_calls;
    return dx8_answer;
}

static void reset(void)
{
    tri_count = 0ul;
    line_count = 0ul;
    point_count = 0ul;
    dx8_calls = 0ul;
    dx8_answer = 1;
    indexed_take = 0;
    indexed_calls = 0ul;
    used = 0ul;
}

static void sink_of(V9X_DP2_SINK *k, int with_dx8)
{
    k->user = 0;
    k->render_state = on_state;
    k->stage_state = on_stage;
    k->list = on_list;
    k->fan = on_fan;
    k->triangle = on_triangle;
    k->point = on_point;
    k->line = on_line;
    k->record = 0;
    k->indexed_list = on_indexed;
    k->dx7 = 0;
    k->dx8 = with_dx8 ? on_dx8 : 0;
}

static void walk(int with_dx8, V9X_DP2_RESULT *r)
{
    V9X_DP2_STREAM s;
    V9X_DP2_SINK k;

    s.commands = COMMANDS;
    s.command_bytes = used;
    s.vertices = pool;
    s.vertex_count = POOL;
    s.vertex_stride = STRIDE;
    sink_of(&k, with_dx8);
    v9x_dp2_walk(&s, &k, r);
}

static void clear_result(V9X_DP2_RESULT *r)
{
    memset(r, 0, sizeof(*r));
}

static int tri_is(v9x_u32 n, v9x_u32 a, v9x_u32 b, v9x_u32 c)
{
    return n < 16ul && tris[n][0] == a && tris[n][1] == b && tris[n][2] == c;
}

/* A draw over the pool, without indices. */
static void plain(V9X_DP2_DRAW *d, v9x_u32 primitive, v9x_u32 first,
                  v9x_u32 count)
{
    d->primitive = primitive;
    d->count = count;
    d->vertices = pool;
    d->vertex_count = POOL;
    d->vertex_stride = STRIDE;
    d->first = first;
    d->base = 0l;
    d->indices = 0;
    d->index_count = 0ul;
    d->index_bytes = 2ul;
}

static void walker_tests(void)
{
    V9X_DP2_RESULT r;
    v9x_u32 i;

    /* Every walked DX8 record, with the bytes each entry carries: fixed
     * parts, and the declaration, code or constants that follow the
     * shader and constant records. */
    reset();
    command(V9X_DP2OP_CREATEVERTEXSHADER, 1ul);
    put32(5ul); put32(8ul); put32(12ul); zeros(20ul);
    command(V9X_DP2OP_DELETEVERTEXSHADER, 1ul); put32(5ul);
    command(V9X_DP2OP_SETVERTEXSHADER, 1ul); put32(0x144ul);
    command(V9X_DP2OP_SETVERTEXSHADERCONST, 1ul);
    put32(0ul); put32(2ul); zeros(32ul);
    command(V9X_DP2OP_SETSTREAMSOURCE, 1ul);
    put32(0ul); put32(7ul); put32(STRIDE);
    command(V9X_DP2OP_SETSTREAMSOURCEUM, 1ul); put32(0ul); put32(STRIDE);
    command(V9X_DP2OP_SETINDICES, 1ul); put32(9ul); put32(2ul);
    command(V9X_DP2OP_DRAWPRIMITIVE, 1ul);
    put32(V9X_D3DPT_TRIANGLELIST); put32(0ul); put32(1ul);
    command(V9X_DP2OP_DRAWINDEXEDPRIMITIVE, 1ul); zeros(24ul);
    command(V9X_DP2OP_CREATEPIXELSHADER, 2ul);
    put32(1ul); put32(8ul); put32(0xffff0101ul); put32(0x0000fffful);
    put32(2ul); put32(4ul); put32(0x0000fffful);
    command(V9X_DP2OP_DELETEPIXELSHADER, 1ul); put32(1ul);
    command(V9X_DP2OP_SETPIXELSHADER, 1ul); put32(2ul);
    command(V9X_DP2OP_SETPIXELSHADERCONST, 1ul);
    put32(0ul); put32(1ul); zeros(16ul);
    command(V9X_DP2OP_CLIPPEDTRIANGLEFAN, 1ul); zeros(12ul);
    command(V9X_DP2OP_DRAWPRIMITIVE2, 1ul); zeros(12ul);
    command(V9X_DP2OP_DRAWINDEXEDPRIMITIVE2, 1ul); zeros(24ul);
    command(V9X_DP2OP_VOLUMEBLT, 1ul); zeros(48ul);
    command(V9X_DP2OP_BUFFERBLT, 1ul); zeros(24ul);
    command(V9X_DP2OP_MULTIPLYTRANSFORM, 1ul); zeros(68ul);
    command(V9X_DP2OP_ADDDIRTYRECT, 2ul); zeros(40ul);
    command(V9X_DP2OP_ADDDIRTYBOX, 1ul); zeros(28ul);
    walk(1, &r);
    CHECK(r.status == V9X_DP2_OK && r.records == 21ul && dx8_calls == 21ul);
    {
        static const v9x_u32 ops[22] = {
            45ul, 46ul, 47ul, 48ul, 49ul, 50ul, 51ul, 52ul, 53ul, 54ul,
            55ul, 56ul, 57ul, 58ul, 59ul, 60ul, 63ul, 64ul, 65ul, 66ul,
            67ul, 0ul
        };
        static const v9x_u32 lengths[22] = {
            32ul, 4ul, 4ul, 40ul, 12ul, 8ul, 8ul, 12ul, 24ul, 28ul,
            4ul, 4ul, 24ul, 12ul, 12ul, 24ul, 48ul, 24ul, 68ul, 40ul,
            28ul, 0ul
        };

        for (i = 0ul; i < 21ul; ++i) {
            CHECK(dx8_ops[i] == ops[i]);
            CHECK(dx8_lengths[i] == lengths[i]);
        }
        CHECK(dx8_counts[9] == 2ul && dx8_counts[19] == 2ul);
    }

    /* Without a dx8 sink the first DX8 record is handed back. */
    reset();
    command(V9X_DP2OP_TRIANGLELIST, 1ul); put16(0ul);
    command(V9X_DP2OP_SETVERTEXSHADER, 1ul); put32(0x144ul);
    walk(0, &r);
    CHECK(r.status == V9X_DP2_UNPARSED && r.stop_offset == 6ul &&
          r.stop_op == V9X_DP2OP_SETVERTEXSHADER && r.records == 1ul);

    /* Nor is a record the sink declines. */
    reset();
    command(V9X_DP2OP_SETVERTEXSHADER, 1ul); put32(0x144ul);
    dx8_answer = 0;
    walk(1, &r);
    CHECK(r.status == V9X_DP2_UNPARSED && r.stop_offset == 0ul &&
          dx8_calls == 1ul);

    /* The patches are handed back even with a sink. */
    reset();
    command(V9X_DP2OP_DRAWRECTPATCH, 1ul); put32(1ul); put32(0ul);
    walk(1, &r);
    CHECK(r.status == V9X_DP2_UNPARSED &&
          r.stop_op == V9X_DP2OP_DRAWRECTPATCH && dx8_calls == 0ul);
    reset();
    command(V9X_DP2OP_DRAWTRIPATCH, 1ul); put32(1ul); put32(0ul);
    walk(1, &r);
    CHECK(r.status == V9X_DP2_UNPARSED && dx8_calls == 0ul);

    /* Code that runs past the buffer, and a size that would wrap. */
    reset();
    command(V9X_DP2OP_CREATEPIXELSHADER, 1ul);
    put32(1ul); put32(16ul); put32(0xffff0101ul);
    walk(1, &r);
    CHECK(r.status == V9X_DP2_MALFORMED && dx8_calls == 0ul);
    reset();
    command(V9X_DP2OP_CREATEVERTEXSHADER, 1ul);
    put32(1ul); put32(0xfffffffcul); put32(8ul); zeros(8ul);
    walk(1, &r);
    CHECK(r.status == V9X_DP2_MALFORMED && dx8_calls == 0ul);
    reset();
    command(V9X_DP2OP_SETPIXELSHADERCONST, 1ul);
    put32(0ul); put32(0x10000000ul);
    walk(1, &r);
    CHECK(r.status == V9X_DP2_MALFORMED);
    reset();
    command(V9X_DP2OP_DRAWPRIMITIVE, 2ul); zeros(12ul);
    walk(1, &r);
    CHECK(r.status == V9X_DP2_MALFORMED && dx8_calls == 0ul);
}

static void draw_tests(void)
{
    V9X_DP2_SINK k;
    V9X_DP2_DRAW d;
    V9X_DP2_RESULT r;
    v9x_u16 words[8];
    v9x_u32 dwords[4];

    sink_of(&k, 1);

    /* A list from vertex 3: one call, the walker's list. */
    reset(); clear_result(&r);
    plain(&d, V9X_D3DPT_TRIANGLELIST, 3ul, 2ul);
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_OK);
    CHECK(tri_count == 2ul && tri_is(0ul, 3ul, 4ul, 5ul) &&
          tri_is(1ul, 6ul, 7ul, 8ul) && r.triangles == 2ul);

    /* A strip keeps one winding: odd triangles swap their first two. */
    reset(); clear_result(&r);
    plain(&d, V9X_D3DPT_TRIANGLESTRIP, 0ul, 3ul);
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_OK);
    CHECK(tri_count == 3ul && tri_is(0ul, 0ul, 1ul, 2ul) &&
          tri_is(1ul, 2ul, 1ul, 3ul) && tri_is(2ul, 2ul, 3ul, 4ul));

    /* A fan from vertex 1. */
    reset(); clear_result(&r);
    plain(&d, V9X_D3DPT_TRIANGLEFAN, 1ul, 2ul);
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_OK);
    CHECK(tri_count == 2ul && tri_is(0ul, 1ul, 2ul, 3ul) &&
          tri_is(1ul, 1ul, 3ul, 4ul));

    /* Past the pool: nothing drawn. */
    reset(); clear_result(&r);
    plain(&d, V9X_D3DPT_TRIANGLELIST, 14ul, 1ul);
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_MALFORMED && tri_count == 0ul);
    plain(&d, V9X_D3DPT_TRIANGLESTRIP, 0xfffffffful, 1ul);
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_MALFORMED && tri_count == 0ul);
    plain(&d, V9X_D3DPT_TRIANGLELIST, 0ul, 0x80000000ul);
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_MALFORMED);
    plain(&d, 7ul, 0ul, 1ul);
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_MALFORMED);
    plain(&d, V9X_D3DPT_TRIANGLELIST, 0ul, 0ul);
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_OK && tri_count == 0ul);

    /* Indexed, 16-bit, from index 1 with base 2: the indexed-list sink
     * gets the base vertex's address and the indices from the first. */
    words[0] = 9u; words[1] = 0u; words[2] = 1u; words[3] = 2u;
    words[4] = 3u; words[5] = 4u; words[6] = 5u;
    reset(); clear_result(&r);
    plain(&d, V9X_D3DPT_TRIANGLELIST, 1ul, 2ul);
    d.indices = (const v9x_u8 *)words;
    d.index_count = 7ul;
    d.base = 2l;
    indexed_take = 1;
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_OK);
    CHECK(indexed_calls == 1ul && indexed_first == pool + 2ul * STRIDE &&
          indexed_words == (const v9x_u8 *)&words[1] &&
          indexed_triangles == 2ul && tri_count == 0ul &&
          r.triangles == 2ul);

    /* The same, declined by that sink: triangles instead. */
    reset(); clear_result(&r);
    indexed_take = 0;
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_OK);
    CHECK(indexed_calls == 1ul && tri_count == 2ul &&
          tri_is(0ul, 2ul, 3ul, 4ul) && tri_is(1ul, 5ul, 6ul, 7ul));

    /* An index outside the pool once the base is added. */
    reset(); clear_result(&r);
    d.base = 11l;
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_MALFORMED);
    CHECK(indexed_calls == 0ul && tri_count == 0ul);

    /* More indices than the buffer holds. */
    reset(); clear_result(&r);
    d.base = 0l;
    d.first = 3ul;
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_MALFORMED);

    /* 32-bit indices with a negative base, which only the triangle path
     * takes. */
    dwords[0] = 1ul; dwords[1] = 2ul; dwords[2] = 3ul; dwords[3] = 0ul;
    reset(); clear_result(&r);
    plain(&d, V9X_D3DPT_TRIANGLELIST, 0ul, 1ul);
    d.indices = (const v9x_u8 *)dwords;
    d.index_count = 4ul;
    d.index_bytes = 4ul;
    d.base = -1l;
    indexed_take = 1;
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_OK);
    CHECK(indexed_calls == 0ul && tri_count == 1ul &&
          tri_is(0ul, 0ul, 1ul, 2ul));
    /* Index 0 with base -1 names vertex -1. */
    reset(); clear_result(&r);
    d.first = 1ul;
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_MALFORMED && tri_count == 0ul);

    /* An indexed strip keeps the winding rule too. */
    words[0] = 5u; words[1] = 6u; words[2] = 7u; words[3] = 8u;
    reset(); clear_result(&r);
    plain(&d, V9X_D3DPT_TRIANGLESTRIP, 0ul, 2ul);
    d.indices = (const v9x_u8 *)words;
    d.index_count = 4ul;
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_OK);
    CHECK(tri_count == 2ul && tri_is(0ul, 5ul, 6ul, 7ul) &&
          tri_is(1ul, 7ul, 6ul, 8ul));

    /* Lines, strips of lines and points. */
    reset(); clear_result(&r);
    plain(&d, V9X_D3DPT_LINELIST, 0ul, 2ul);
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_OK);
    CHECK(line_count == 2ul && lines[0][0] == 0ul && lines[0][1] == 1ul &&
          lines[1][0] == 2ul && lines[1][1] == 3ul && r.lines == 2ul);
    reset(); clear_result(&r);
    plain(&d, V9X_D3DPT_LINESTRIP, 4ul, 2ul);
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_OK);
    CHECK(line_count == 2ul && lines[0][0] == 4ul && lines[0][1] == 5ul &&
          lines[1][0] == 5ul && lines[1][1] == 6ul);
    reset(); clear_result(&r);
    plain(&d, V9X_D3DPT_POINTLIST, 9ul, 3ul);
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_OK);
    CHECK(point_count == 3ul && points[0] == 9ul && points[2] == 11ul &&
          r.points == 3ul);

    /* With no line or point sink they are counted undrawn. */
    k.line = 0; k.point = 0;
    reset(); clear_result(&r);
    plain(&d, V9X_D3DPT_LINELIST, 0ul, 2ul);
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_OK);
    CHECK(line_count == 0ul && r.undrawn == 1ul && r.lines == 0ul);
}

/* A negative base vertex offset in bytes (DRAWINDEXEDPRIMITIVE2),
 * whole vertices or not: the vertices start r bytes in, the base is -k. */
static void negative_base_tests(void)
{
    V9X_DP2_SINK k;
    V9X_DP2_DRAW d;
    V9X_DP2_RESULT r;
    v9x_u16 words[3];

    sink_of(&k, 1);

    /* -32 at a stride of 20: k 2, r 8, so 15 whole vertices from pool + 8. */
    plain(&d, V9X_D3DPT_TRIANGLELIST, 0ul, 1ul);
    CHECK(v9x_dp2_negative_base(&d, pool, POOL * STRIDE, -32l));
    CHECK(d.vertices == pool + 8ul && d.base == -2l && d.vertex_count == 15ul);

    /* Whole vertices: r 0, as the aligned rule gave before. */
    plain(&d, V9X_D3DPT_TRIANGLELIST, 0ul, 1ul);
    CHECK(v9x_dp2_negative_base(&d, pool, POOL * STRIDE, -40l));
    CHECK(d.vertices == pool && d.base == -2l && d.vertex_count == POOL);

    /* UT2004's on the 4570 (boot 235): FFFF236C at 28 bytes is -56,468,
     * 2,016 vertices and 20 bytes; k 2,017, r 8. */
    plain(&d, V9X_D3DPT_TRIANGLELIST, 0ul, 1ul);
    d.vertex_stride = 28ul;
    CHECK(v9x_dp2_negative_base(&d, pool, POOL * STRIDE,
                                (v9x_s32)0xFFFF236Cul));
    CHECK(d.vertices == pool + 8ul && d.base == -2017l &&
          d.vertex_count == (POOL * STRIDE - 8ul) / 28ul);

    /* The most negative offset has no overflow. */
    plain(&d, V9X_D3DPT_TRIANGLELIST, 0ul, 1ul);
    CHECK(v9x_dp2_negative_base(&d, pool, POOL * STRIDE,
                                (v9x_s32)0x80000000ul));
    CHECK(d.vertices == pool + 12ul && d.base == -107374183l);

    /* Not this helper's: no stride, an offset that is not negative, a
     * stream shorter than r. */
    plain(&d, V9X_D3DPT_TRIANGLELIST, 0ul, 1ul);
    d.vertex_stride = 0ul;
    CHECK(!v9x_dp2_negative_base(&d, pool, POOL * STRIDE, -32l));
    plain(&d, V9X_D3DPT_TRIANGLELIST, 0ul, 1ul);
    CHECK(!v9x_dp2_negative_base(&d, pool, POOL * STRIDE, 0l));
    CHECK(!v9x_dp2_negative_base(&d, pool, POOL * STRIDE, 40l));
    CHECK(!v9x_dp2_negative_base(&d, pool, 4ul, -32l));

    /* Drawn: indices 2, 3, 4 at base -2 name the vertices at pool + 8,
     * + 28 and + 48, which number() reads as 0, 1 and 2. */
    words[0] = 2u; words[1] = 3u; words[2] = 4u;
    reset(); clear_result(&r);
    plain(&d, V9X_D3DPT_TRIANGLELIST, 0ul, 1ul);
    d.indices = (const v9x_u8 *)words;
    d.index_count = 3ul;
    CHECK(v9x_dp2_negative_base(&d, pool, POOL * STRIDE, -32l));
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_OK);
    CHECK(tri_count == 1ul && tri_is(0ul, 0ul, 1ul, 2ul));
    /* Index 1 is vertex -1, before the stream. */
    words[0] = 1u;
    reset(); clear_result(&r);
    CHECK(v9x_dp2_draw(&d, &k, &r) == V9X_DP2_MALFORMED && tri_count == 0ul);
}

unsigned int v9x_run_d3d_dp2_dx8_tests(void)
{
    failures = 0u;
    walker_tests();
    draw_tests();
    negative_base_tests();
    return failures;
}
