#include <stdio.h>
#include <string.h>
#include "../../src/display32/d3d/d3d_dp2.h"

static unsigned int failures;
#define CHECK(e) do { if (!(e)) { ++failures; \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
} } while (0)

#define STRIDE 32ul
#define POOL 16ul

/* A stand-in vertex pool: what the walker hands back is a pointer, and the
 * test turns it back into an index. */
static v9x_u8 pool[POOL * STRIDE];

/* The command buffer, DWORD aligned so the _IMM padding is predictable. */
static v9x_u32 command_words[128];
#define COMMANDS ((v9x_u8 *)command_words)
static v9x_u32 used;

typedef struct capture {
    v9x_u32 states[8][2];
    v9x_u32 state_count;
    v9x_u32 stages[4][3];
    v9x_u32 stage_count;
    /* Triangles as vertex numbers; a negative-free encoding: an _IMM vertex
     * is 100 + its position in the inline run. */
    v9x_u32 tris[16][3];
    v9x_u32 tri_count;
    const v9x_u8 *imm_base;
} CAPTURE;

static v9x_u32 number(CAPTURE *c, const v9x_u8 *v)
{
    if (v >= pool && v < pool + sizeof(pool)) {
        return (v9x_u32)((v - pool) / STRIDE);
    }
    if (c->imm_base != 0 && v >= c->imm_base) {
        return 100ul + (v9x_u32)((v - c->imm_base) / STRIDE);
    }
    return 999ul;
}

static void on_state(void *user, v9x_u32 state, v9x_u32 value)
{
    CAPTURE *c = (CAPTURE *)user;

    if (c->state_count < 8ul) {
        c->states[c->state_count][0] = state;
        c->states[c->state_count][1] = value;
    }
    ++c->state_count;
}

static void on_stage(void *user, v9x_u32 stage, v9x_u32 state, v9x_u32 value)
{
    CAPTURE *c = (CAPTURE *)user;

    if (c->stage_count < 4ul) {
        c->stages[c->stage_count][0] = stage;
        c->stages[c->stage_count][1] = state;
        c->stages[c->stage_count][2] = value;
    }
    ++c->stage_count;
}

static void add_tri(CAPTURE *c, v9x_u32 a, v9x_u32 b, v9x_u32 d)
{
    if (c->tri_count < 16ul) {
        c->tris[c->tri_count][0] = a;
        c->tris[c->tri_count][1] = b;
        c->tris[c->tri_count][2] = d;
    }
    ++c->tri_count;
}

static void on_triangle(void *user, const v9x_u8 *a, const v9x_u8 *b,
                        const v9x_u8 *d)
{
    CAPTURE *c = (CAPTURE *)user;

    add_tri(c, number(c, a), number(c, b), number(c, d));
}

static void on_list(void *user, const v9x_u8 *first, v9x_u32 triangles)
{
    CAPTURE *c = (CAPTURE *)user;
    v9x_u32 i;

    for (i = 0ul; i < triangles; ++i) {
        add_tri(c, number(c, first + (i * 3ul) * STRIDE),
                number(c, first + (i * 3ul + 1ul) * STRIDE),
                number(c, first + (i * 3ul + 2ul) * STRIDE));
    }
}

static void on_fan(void *user, const v9x_u8 *first, v9x_u32 vertices)
{
    CAPTURE *c = (CAPTURE *)user;
    v9x_u32 i;

    for (i = 1ul; i + 1ul < vertices; ++i) {
        add_tri(c, number(c, first), number(c, first + i * STRIDE),
                number(c, first + (i + 1ul) * STRIDE));
    }
}

static v9x_u32 records_seen;
static v9x_u32 record_bytes;
static v9x_u32 last_record_op;

static void on_record(void *user, v9x_u32 op, v9x_u32 count,
                      const v9x_u8 *payload, v9x_u32 bytes)
{
    (void)user; (void)count; (void)payload;
    ++records_seen;
    record_bytes += bytes;
    last_record_op = op;
}

static v9x_u32 lines_seen[8][2];
static v9x_u32 line_count;
static v9x_u32 points_seen[8];
static v9x_u32 point_count;

static void on_line(void *user, const v9x_u8 *a, const v9x_u8 *b)
{
    (void)user;
    if (line_count < 8ul) {
        lines_seen[line_count][0] = (v9x_u32)((a - pool) / STRIDE);
        lines_seen[line_count][1] = (v9x_u32)((b - pool) / STRIDE);
    }
    ++line_count;
}

static void on_point(void *user, const v9x_u8 *v)
{
    (void)user;
    if (point_count < 8ul) {
        points_seen[point_count] = (v9x_u32)((v - pool) / STRIDE);
    }
    ++point_count;
}

static float float_at(const v9x_u8 *p)
{
    float f;

    memcpy(&f, p, sizeof(f));
    return f;
}

static void set_xy(v9x_u8 *v, float x, float y)
{
    memcpy(v, &x, sizeof(x));
    memcpy(v + 4, &y, sizeof(y));
}

static void put8(v9x_u32 value) { COMMANDS[used++] = (v9x_u8)value; }
static void put16(v9x_u32 value) { put8(value & 0xfful); put8(value >> 8); }
static void put32(v9x_u32 value) { put16(value & 0xfffful); put16(value >> 16); }
static void command(v9x_u32 op, v9x_u32 count)
{
    put8(op); put8(0ul); put16(count);
}

static void walk(CAPTURE *c, V9X_DP2_RESULT *r)
{
    V9X_DP2_STREAM s;
    V9X_DP2_SINK k;

    s.commands = COMMANDS;
    s.command_bytes = used;
    s.vertices = pool;
    s.vertex_count = POOL;
    s.vertex_stride = STRIDE;
    k.user = c;
    k.render_state = on_state;
    k.stage_state = on_stage;
    k.list = on_list;
    k.fan = on_fan;
    k.triangle = on_triangle;
    k.record = 0;
    k.point = 0;
    k.line = 0;
    v9x_dp2_walk(&s, &k, r);
}

static int tri_is(const CAPTURE *c, v9x_u32 n, v9x_u32 a, v9x_u32 b,
                  v9x_u32 d)
{
    return c->tris[n][0] == a && c->tris[n][1] == b && c->tris[n][2] == d;
}

unsigned int v9x_run_d3d_dp2_tests(void)
{
    CAPTURE c;
    V9X_DP2_RESULT r;
    v9x_u32 i;

    failures = 0u;

    /* States and a non-indexed list: the list is a window on the pool. */
    memset(&c, 0, sizeof(c)); used = 0ul;
    command(V9X_DP2OP_RENDERSTATE, 2ul);
    put32(1ul); put32(0x1234ul);
    put32(22ul); put32(1ul);
    command(V9X_DP2OP_TRIANGLELIST, 2ul); put16(3ul);
    walk(&c, &r);
    CHECK(r.status == V9X_DP2_OK);
    CHECK(r.records == 2ul && r.stop_offset == used);
    CHECK(c.state_count == 2ul && c.states[0][0] == 1ul &&
          c.states[0][1] == 0x1234ul && c.states[1][0] == 22ul);
    CHECK(c.tri_count == 2ul && tri_is(&c, 0, 3, 4, 5) &&
          tri_is(&c, 1, 6, 7, 8));
    CHECK(r.triangles == 2ul && r.states == 2ul);
    CHECK((r.ops_seen[0] & (1ul << 8)) != 0ul &&
          (r.ops_seen[0] & (1ul << 18)) != 0ul);

    /* A strip swaps every odd triangle's first two vertices. */
    memset(&c, 0, sizeof(c)); used = 0ul;
    command(V9X_DP2OP_TRIANGLESTRIP, 3ul); put16(0ul);
    walk(&c, &r);
    CHECK(r.status == V9X_DP2_OK && c.tri_count == 3ul);
    CHECK(tri_is(&c, 0, 0, 1, 2) && tri_is(&c, 1, 2, 1, 3) &&
          tri_is(&c, 2, 2, 3, 4));

    /* An indexed strip of one triangle is three WORDs, so the next record
     * starts two bytes off a DWORD boundary and must still parse. */
    memset(&c, 0, sizeof(c)); used = 0ul;
    command(V9X_DP2OP_INDEXEDTRIANGLESTRIP, 2ul);
    put16(5ul); put16(6ul); put16(7ul); put16(8ul);
    command(V9X_DP2OP_INDEXEDTRIANGLESTRIP, 1ul);
    put16(1ul); put16(2ul); put16(3ul);
    command(V9X_DP2OP_RENDERSTATE, 1ul); put32(9ul); put32(2ul);
    walk(&c, &r);
    CHECK(r.status == V9X_DP2_OK && r.records == 3ul);
    CHECK(c.tri_count == 3ul && tri_is(&c, 0, 5, 6, 7) &&
          tri_is(&c, 1, 7, 6, 8) && tri_is(&c, 2, 1, 2, 3));
    CHECK(c.state_count == 1ul && c.states[0][0] == 9ul &&
          c.states[0][1] == 2ul);

    /* Fans, indexed and not, keep the first vertex as every apex. */
    memset(&c, 0, sizeof(c)); used = 0ul;
    command(V9X_DP2OP_INDEXEDTRIANGLEFAN, 2ul);
    put16(9ul); put16(1ul); put16(2ul); put16(3ul);
    command(V9X_DP2OP_TRIANGLEFAN, 1ul); put16(10ul);
    walk(&c, &r);
    CHECK(r.status == V9X_DP2_OK && c.tri_count == 3ul);
    CHECK(tri_is(&c, 0, 9, 1, 2) && tri_is(&c, 1, 9, 2, 3) &&
          tri_is(&c, 2, 10, 11, 12));

    /* The two indexed-list forms: edge flags ignored, and indices relative
     * to the start vertex. */
    memset(&c, 0, sizeof(c)); used = 0ul;
    command(V9X_DP2OP_INDEXEDTRIANGLELIST, 1ul);
    put16(4ul); put16(0ul); put16(15ul); put16(0x0007ul);
    command(V9X_DP2OP_INDEXEDTRIANGLELIST2, 1ul);
    put16(10ul); put16(0ul); put16(1ul); put16(2ul);
    walk(&c, &r);
    CHECK(r.status == V9X_DP2_OK && c.tri_count == 2ul);
    CHECK(tri_is(&c, 0, 4, 0, 15) && tri_is(&c, 1, 10, 11, 12));

    /* A fan's inline vertices start at the next DWORD boundary. The record
     * here begins at offset 10, its edge flags end at 18, so two bytes of
     * padding put the vertices at 20 - and the record after them parses. */
    memset(&c, 0, sizeof(c)); used = 0ul;
    command(V9X_DP2OP_INDEXEDTRIANGLESTRIP, 1ul);
    put16(1ul); put16(2ul); put16(3ul);
    CHECK(used == 10ul);
    command(V9X_DP2OP_TRIANGLEFAN_IMM, 1ul); put32(0ul);
    put16(0ul);
    c.imm_base = COMMANDS + used;
    CHECK(used == 20ul);
    for (i = 0ul; i < 3ul * STRIDE; ++i) {
        put8(0ul);
    }
    command(V9X_DP2OP_RENDERSTATE, 1ul); put32(7ul); put32(1ul);
    walk(&c, &r);
    CHECK(r.status == V9X_DP2_OK && r.records == 3ul);
    CHECK(c.tri_count == 2ul && tri_is(&c, 1, 100, 101, 102));
    CHECK(c.state_count == 1ul && c.states[0][0] == 7ul);

    /* Stage states arrive as WORD stage, WORD state, DWORD value. */
    memset(&c, 0, sizeof(c)); used = 0ul;
    command(V9X_DP2OP_TEXTURESTAGESTATE, 2ul);
    put16(0ul); put16(16ul); put32(2ul);
    put16(1ul); put16(1ul); put32(1ul);
    walk(&c, &r);
    CHECK(r.status == V9X_DP2_OK && c.stage_count == 2ul);
    CHECK(c.stages[0][0] == 0ul && c.stages[0][1] == 16ul &&
          c.stages[0][2] == 2ul && c.stages[1][0] == 1ul);

    /* Points, lines and palettes are consumed, not drawn, and the walk goes
     * on past them. */
    memset(&c, 0, sizeof(c)); used = 0ul;
    command(V9X_DP2OP_POINTS, 1ul); put16(2ul); put16(4ul);
    command(V9X_DP2OP_LINELIST, 2ul); put16(0ul);
    command(V9X_DP2OP_INDEXEDLINESTRIP, 2ul);
    put16(1ul); put16(2ul); put16(3ul);
    command(V9X_DP2OP_UPDATEPALETTE, 1ul);
    put32(5ul); put16(0ul); put16(2ul); put32(0ul); put32(0ul);
    command(V9X_DP2OP_VIEWPORTINFO, 1ul);
    put32(0ul); put32(0ul); put32(640ul); put32(480ul);
    command(V9X_DP2OP_TRIANGLELIST, 1ul); put16(0ul);
    walk(&c, &r);
    CHECK(r.status == V9X_DP2_OK && r.records == 6ul);
    CHECK(r.undrawn == 4ul && c.tri_count == 1ul);

    /* An opcode the walker does not parse is handed back at its own offset,
     * with what came before it already consumed. */
    memset(&c, 0, sizeof(c)); used = 0ul;
    command(V9X_DP2OP_RENDERSTATE, 1ul); put32(9ul); put32(1ul);
    command(9ul, 1ul); put32(0ul);
    walk(&c, &r);
    CHECK(r.status == V9X_DP2_UNPARSED);
    CHECK(r.stop_offset == 12ul && r.stop_op == 9ul && r.records == 1ul);
    CHECK(c.state_count == 1ul);

    /* An index past the pool stops the walk at that record, and a strip
     * with one bad index draws none of its triangles. */
    memset(&c, 0, sizeof(c)); used = 0ul;
    command(V9X_DP2OP_TRIANGLELIST, 1ul); put16(0ul);
    command(V9X_DP2OP_INDEXEDTRIANGLESTRIP, 2ul);
    put16(1ul); put16(2ul); put16(3ul); put16(POOL);
    walk(&c, &r);
    CHECK(r.status == V9X_DP2_MALFORMED && r.stop_offset == 6ul);
    CHECK(c.tri_count == 1ul);

    /* A list running off the end of the pool. */
    memset(&c, 0, sizeof(c)); used = 0ul;
    command(V9X_DP2OP_TRIANGLELIST, 2ul); put16(POOL - 3ul);
    walk(&c, &r);
    CHECK(r.status == V9X_DP2_MALFORMED && c.tri_count == 0ul);

    /* A count claiming more than the buffer holds. */
    memset(&c, 0, sizeof(c)); used = 0ul;
    command(V9X_DP2OP_RENDERSTATE, 2ul); put32(9ul); put32(1ul);
    walk(&c, &r);
    CHECK(r.status == V9X_DP2_MALFORMED && c.state_count == 0ul);

    /* A trailing fragment too short to be a record. */
    memset(&c, 0, sizeof(c)); used = 0ul;
    command(V9X_DP2OP_TRIANGLELIST, 0ul); put16(0ul);
    put16(0ul);
    walk(&c, &r);
    CHECK(r.status == V9X_DP2_MALFORMED && r.stop_offset == 6ul);

    /* An empty buffer is a successful walk of nothing. */
    memset(&c, 0, sizeof(c)); used = 0ul;
    walk(&c, &r);
    CHECK(r.status == V9X_DP2_OK && r.records == 0ul && r.stop_offset == 0ul);

    /* Flexible vertex formats. */
    {
        V9X_DP2_FVF f;
        v9x_u32 in[12];
        v9x_u32 out[8];

        /* D3DFVF_TLVERTEX is the engines' own layout. */
        CHECK(v9x_dp2_fvf_layout(0x1c4ul, &f) && f.stride == 32ul &&
              v9x_dp2_fvf_is_tlvertex(&f));
        /* XYZRHW|DIFFUSE|TEX1: no specular, 28 bytes. */
        CHECK(v9x_dp2_fvf_layout(0x144ul, &f) && f.stride == 28ul &&
              f.diffuse == 16ul && f.specular == V9X_DP2_FVF_ABSENT &&
              f.tex0 == 20ul && !v9x_dp2_fvf_is_tlvertex(&f));
        /* XYZRHW|TEX2 with set 0 one float and set 1 three floats. */
        CHECK(v9x_dp2_fvf_layout(0x204ul | (3ul << 16) | (1ul << 18), &f) &&
              f.tex0 == 16ul && f.tex0_floats == 1ul && f.stride == 32ul);
        /* RESERVED1 pads a DWORD before diffuse. */
        CHECK(v9x_dp2_fvf_layout(0x064ul, &f) && f.diffuse == 20ul &&
              f.stride == 24ul);
        /* Untransformed positions and normals are refused. */
        CHECK(!v9x_dp2_fvf_layout(0x152ul, &f));
        CHECK(!v9x_dp2_fvf_layout(0x014ul, &f));

        /* XYZRHW|SPECULAR|TEX1: diffuse defaults to white. */
        CHECK(v9x_dp2_fvf_layout(0x184ul, &f) && f.stride == 28ul);
        in[0] = 1ul; in[1] = 2ul; in[2] = 3ul; in[3] = 4ul;
        in[4] = 0x11223344ul; in[5] = 5ul; in[6] = 6ul;
        v9x_dp2_fvf_convert(&f, (const v9x_u8 *)in, (v9x_u8 *)out);
        CHECK(out[0] == 1ul && out[3] == 4ul && out[4] == 0xfffffffful &&
              out[5] == 0x11223344ul && out[6] == 5ul && out[7] == 6ul);

        /* XYZRHW alone: specular is black with no fog, coordinates 0. */
        CHECK(v9x_dp2_fvf_layout(0x004ul, &f) && f.stride == 16ul);
        v9x_dp2_fvf_convert(&f, (const v9x_u8 *)in, (v9x_u8 *)out);
        CHECK(out[4] == 0xfffffffful && out[5] == 0xff000000ul &&
              out[6] == 0ul && out[7] == 0ul);

        /* The optional record callback sees every record consumed. */
        memset(&c, 0, sizeof(c)); used = 0ul;
        command(V9X_DP2OP_RENDERSTATE, 1ul); put32(9ul); put32(2ul);
        command(V9X_DP2OP_TRIANGLELIST, 1ul); put16(0ul);
        {
            V9X_DP2_STREAM s;
            V9X_DP2_SINK k;

            s.commands = COMMANDS; s.command_bytes = used;
            s.vertices = pool; s.vertex_count = POOL; s.vertex_stride = STRIDE;
            k.user = &c; k.render_state = on_state; k.stage_state = on_stage;
            k.list = on_list; k.fan = on_fan; k.triangle = on_triangle;
            k.record = on_record;
            k.point = 0; k.line = 0;
            records_seen = 0ul; record_bytes = 0ul;
            v9x_dp2_walk(&s, &k, &r);
            CHECK(r.status == V9X_DP2_OK && records_seen == 2ul &&
                  record_bytes == 10ul && last_record_op ==
                  V9X_DP2OP_TRIANGLELIST);
        }

        /* The walker steps a pool by the format's stride. */
        memset(&c, 0, sizeof(c)); used = 0ul;
        command(V9X_DP2OP_TRIANGLELIST, 1ul); put16(1ul);
        {
            V9X_DP2_STREAM s;
            V9X_DP2_SINK k;

            s.commands = COMMANDS; s.command_bytes = used;
            s.vertices = pool; s.vertex_count = 4ul; s.vertex_stride = 16ul;
            k.user = &c; k.render_state = on_state; k.stage_state = on_stage;
            k.list = on_list; k.fan = on_fan; k.triangle = on_triangle;
            k.record = 0;
            k.point = 0; k.line = 0;
            v9x_dp2_walk(&s, &k, &r);
            CHECK(r.status == V9X_DP2_OK && r.triangles == 1ul);
            /* Vertices 1 to 3 need a pool of four. */
            s.vertex_count = 3ul;
            v9x_dp2_walk(&s, &k, &r);
            CHECK(r.status == V9X_DP2_MALFORMED);
        }
    }

    /* Points and lines, when the sink takes them. */
    {
        V9X_DP2_STREAM s;
        V9X_DP2_SINK k;
        v9x_u8 quad[6 * 32];
        v9x_u8 va[32], vb[32];

        memset(&c, 0, sizeof(c)); used = 0ul;
        command(V9X_DP2OP_LINESTRIP, 2ul); put16(3ul);
        command(V9X_DP2OP_INDEXEDLINELIST2, 1ul); put16(4ul);
        put16(1ul); put16(5ul);
        command(V9X_DP2OP_POINTS, 1ul); put16(2ul); put16(7ul);
        s.commands = COMMANDS; s.command_bytes = used;
        s.vertices = pool; s.vertex_count = POOL; s.vertex_stride = STRIDE;
        k.user = &c; k.render_state = on_state; k.stage_state = on_stage;
        k.list = on_list; k.fan = on_fan; k.triangle = on_triangle;
        k.record = 0; k.point = on_point; k.line = on_line;
        line_count = 0ul; point_count = 0ul;
        v9x_dp2_walk(&s, &k, &r);
        CHECK(r.status == V9X_DP2_OK && r.lines == 3ul && r.points == 2ul &&
              r.undrawn == 0ul);
        CHECK(line_count == 3ul && lines_seen[0][0] == 3ul &&
              lines_seen[0][1] == 4ul && lines_seen[1][0] == 4ul &&
              lines_seen[1][1] == 5ul && lines_seen[2][0] == 5ul &&
              lines_seen[2][1] == 9ul);
        CHECK(point_count == 2ul && points_seen[0] == 7ul &&
              points_seen[1] == 8ul);

        /* Without the callbacks the same stream draws nothing. */
        walk(&c, &r);
        CHECK(r.status == V9X_DP2_OK && r.undrawn == 3ul && r.lines == 0ul);

        /* An x-major line widens in y, both triangles start on a. */
        memset(va, 0, sizeof(va)); memset(vb, 0, sizeof(vb));
        set_xy(va, 10.0f, 20.0f); set_xy(vb, 30.0f, 25.0f);
        va[16] = 0x11u; vb[16] = 0x22u;
        CHECK(v9x_dp2_line_quad(va, vb, quad) == 2ul);
        CHECK(float_at(quad) == 10.0f && float_at(quad + 4) == 21.0f);
        CHECK(float_at(quad + 32 + 4) == 20.0f);
        CHECK(float_at(quad + 64) == 30.0f && float_at(quad + 68) == 26.0f);
        CHECK(quad[16] == 0x11u && quad[96 + 16] == 0x11u &&
              quad[128 + 16] == 0x22u);
        /* A y-major line widens in x. */
        set_xy(vb, 12.0f, 40.0f);
        CHECK(v9x_dp2_line_quad(va, vb, quad) == 2ul);
        CHECK(float_at(quad) == 11.0f && float_at(quad + 4) == 20.0f);
        /* No length, no triangles. */
        CHECK(v9x_dp2_line_quad(va, va, quad) == 0ul);
        /* A point is the unit square from its position. */
        CHECK(v9x_dp2_point_quad(va, quad) == 2ul);
        CHECK(float_at(quad) == 10.0f && float_at(quad + 4) == 20.0f &&
              float_at(quad + 64) == 11.0f && float_at(quad + 68) == 21.0f);
    }

    return failures;
}
