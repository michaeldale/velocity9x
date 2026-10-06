/*
 * The DrawPrimitives2 command walker; d3d_dp2.h says what it is for and the
 * rules it keeps. Pure, and host-tested in tests\host\test_d3d_dp2.c.
 */
#include <stddef.h>

#include "d3d_dp2.h"

/* Little-endian reads a byte at a time: a record follows a WORD-sized
 * payload as often as a DWORD one, so nothing here is aligned. */
static v9x_u32 v9x_dp2_word(const v9x_u8 *p)
{
    return (v9x_u32)p[0] | ((v9x_u32)p[1] << 8);
}

static v9x_u32 v9x_dp2_dword(const v9x_u8 *p)
{
    return v9x_dp2_word(p) | (v9x_dp2_word(p + 2) << 16);
}

/* The pool's vertex at index, or null outside the pool. */
static const v9x_u8 *v9x_dp2_vertex(const V9X_DP2_STREAM *stream,
                                    v9x_u32 index)
{
    if (index >= stream->vertex_count) {
        return 0;
    }
    return stream->vertices + index * stream->vertex_stride;
}

/* Whether vertices [first, first + count) all lie in the pool. */
static int v9x_dp2_span(const V9X_DP2_STREAM *stream, v9x_u32 first,
                        v9x_u32 count)
{
    return first <= stream->vertex_count &&
           count <= stream->vertex_count - first;
}

/* Whether every one of count WORD indices at data, each plus base, names a
 * vertex in the pool. Checked before any of them is drawn, so a strip or
 * fan with one bad index draws nothing rather than a prefix. */
static int v9x_dp2_indices(const V9X_DP2_STREAM *stream, const v9x_u8 *data,
                           v9x_u32 count, v9x_u32 step, v9x_u32 base)
{
    v9x_u32 i;

    for (i = 0ul; i < count; ++i) {
        if (base + v9x_dp2_word(data + i * step) >= stream->vertex_count) {
            return 0;
        }
    }
    return 1;
}

/*
 * Bytes from p to the next DWORD boundary. The _IMM records align their
 * inline vertices to an absolute DWORD address, not to an offset in the
 * buffer, so this is taken from the pointer itself.
 */
static v9x_u32 v9x_dp2_pad(const v9x_u8 *p)
{
    return (v9x_u32)((4u - ((size_t)p & 3u)) & 3u);
}

void v9x_dp2_walk(const V9X_DP2_STREAM *stream, const V9X_DP2_SINK *sink,
                  V9X_DP2_RESULT *result)
{
    v9x_u32 at = 0ul;

    result->status = V9X_DP2_OK;
    result->stop_offset = 0ul;
    result->stop_op = 0ul;
    result->records = 0ul;
    result->triangles = 0ul;
    result->states = 0ul;
    result->stage_states = 0ul;
    result->undrawn = 0ul;
    result->ops_seen[0] = 0ul;
    result->ops_seen[1] = 0ul;

    while (at < stream->command_bytes) {
        const v9x_u8 *record = stream->commands + at;
        const v9x_u8 *data = record + 4;
        v9x_u32 left = stream->command_bytes - at;
        v9x_u32 op;
        v9x_u32 count;
        v9x_u32 need = 0ul;
        v9x_u32 i;

        result->stop_offset = at;
        if (left < 4ul) {
            result->stop_op = 0ul;
            result->status = V9X_DP2_MALFORMED;
            return;
        }
        op = (v9x_u32)record[0];
        count = v9x_dp2_word(record + 2);
        result->stop_op = op;
        left -= 4ul;

        switch (op) {
        case V9X_DP2OP_POINTS:
            need = count * 4ul;
            if (need > left) {
                goto malformed;
            }
            for (i = 0ul; i < count; ++i) {
                if (!v9x_dp2_span(stream, v9x_dp2_word(data + i * 4ul + 2ul),
                                  v9x_dp2_word(data + i * 4ul))) {
                    goto malformed;
                }
            }
            ++result->undrawn;
            break;

        case V9X_DP2OP_INDEXEDLINELIST:
            need = count * 4ul;
            if (need > left ||
                !v9x_dp2_indices(stream, data, count * 2ul, 2ul, 0ul)) {
                goto malformed;
            }
            ++result->undrawn;
            break;

        case V9X_DP2OP_INDEXEDTRIANGLELIST:
            /* D3DHAL_DP2INDEXEDTRIANGLELIST: three indices and the edge
             * flags, which only a wireframe fill reads. */
            need = count * 8ul;
            if (need > left) {
                goto malformed;
            }
            for (i = 0ul; i < count; ++i) {
                if (!v9x_dp2_indices(stream, data + i * 8ul, 3ul, 2ul, 0ul)) {
                    goto malformed;
                }
            }
            for (i = 0ul; i < count; ++i) {
                const v9x_u8 *t = data + i * 8ul;

                sink->triangle(sink->user,
                               v9x_dp2_vertex(stream, v9x_dp2_word(t)),
                               v9x_dp2_vertex(stream, v9x_dp2_word(t + 2)),
                               v9x_dp2_vertex(stream, v9x_dp2_word(t + 4)));
            }
            result->triangles += count;
            break;

        case V9X_DP2OP_RENDERSTATE:
            /* D3DHAL_DP2RENDERSTATE: the state, then a DWORD or a float in
             * the same four bytes, passed on as bits. */
            need = count * 8ul;
            if (need > left) {
                goto malformed;
            }
            for (i = 0ul; i < count; ++i) {
                sink->render_state(sink->user,
                                   v9x_dp2_dword(data + i * 8ul),
                                   v9x_dp2_dword(data + i * 8ul + 4ul));
            }
            result->states += count;
            break;

        case V9X_DP2OP_LINELIST:
            need = 2ul;
            if (need > left ||
                !v9x_dp2_span(stream, v9x_dp2_word(data), count * 2ul)) {
                goto malformed;
            }
            ++result->undrawn;
            break;

        case V9X_DP2OP_LINESTRIP:
            need = 2ul;
            if (need > left ||
                (count != 0ul &&
                 !v9x_dp2_span(stream, v9x_dp2_word(data), count + 1ul))) {
                goto malformed;
            }
            ++result->undrawn;
            break;

        case V9X_DP2OP_INDEXEDLINESTRIP:
            need = count != 0ul ? (count + 1ul) * 2ul : 0ul;
            if (need > left ||
                !v9x_dp2_indices(stream, data, need / 2ul, 2ul, 0ul)) {
                goto malformed;
            }
            ++result->undrawn;
            break;

        case V9X_DP2OP_TRIANGLELIST:
            need = 2ul;
            if (need > left ||
                !v9x_dp2_span(stream, v9x_dp2_word(data), count * 3ul)) {
                goto malformed;
            }
            if (count != 0ul) {
                sink->list(sink->user,
                           v9x_dp2_vertex(stream, v9x_dp2_word(data)), count);
            }
            result->triangles += count;
            break;

        case V9X_DP2OP_TRIANGLESTRIP:
            need = 2ul;
            if (need > left ||
                (count != 0ul &&
                 !v9x_dp2_span(stream, v9x_dp2_word(data), count + 2ul))) {
                goto malformed;
            }
            {
                v9x_u32 start = v9x_dp2_word(data);

                for (i = 0ul; i < count; ++i) {
                    v9x_u32 swap = i & 1ul;

                    sink->triangle(sink->user,
                                   v9x_dp2_vertex(stream, start + i + swap),
                                   v9x_dp2_vertex(stream, start + i + 1ul - swap),
                                   v9x_dp2_vertex(stream, start + i + 2ul));
                }
            }
            result->triangles += count;
            break;

        case V9X_DP2OP_INDEXEDTRIANGLESTRIP:
            need = count != 0ul ? (count + 2ul) * 2ul : 0ul;
            if (need > left ||
                !v9x_dp2_indices(stream, data, need / 2ul, 2ul, 0ul)) {
                goto malformed;
            }
            for (i = 0ul; i < count; ++i) {
                v9x_u32 swap = i & 1ul;

                sink->triangle(
                    sink->user,
                    v9x_dp2_vertex(stream,
                                   v9x_dp2_word(data + (i + swap) * 2ul)),
                    v9x_dp2_vertex(stream,
                                   v9x_dp2_word(data + (i + 1ul - swap) * 2ul)),
                    v9x_dp2_vertex(stream,
                                   v9x_dp2_word(data + (i + 2ul) * 2ul)));
            }
            result->triangles += count;
            break;

        case V9X_DP2OP_TRIANGLEFAN:
            need = 2ul;
            if (need > left ||
                (count != 0ul &&
                 !v9x_dp2_span(stream, v9x_dp2_word(data), count + 2ul))) {
                goto malformed;
            }
            if (count != 0ul) {
                sink->fan(sink->user,
                          v9x_dp2_vertex(stream, v9x_dp2_word(data)),
                          count + 2ul);
            }
            result->triangles += count;
            break;

        case V9X_DP2OP_INDEXEDTRIANGLEFAN:
            need = count != 0ul ? (count + 2ul) * 2ul : 0ul;
            if (need > left ||
                !v9x_dp2_indices(stream, data, need / 2ul, 2ul, 0ul)) {
                goto malformed;
            }
            for (i = 0ul; i < count; ++i) {
                sink->triangle(
                    sink->user,
                    v9x_dp2_vertex(stream, v9x_dp2_word(data)),
                    v9x_dp2_vertex(stream,
                                   v9x_dp2_word(data + (i + 1ul) * 2ul)),
                    v9x_dp2_vertex(stream,
                                   v9x_dp2_word(data + (i + 2ul) * 2ul)));
            }
            result->triangles += count;
            break;

        case V9X_DP2OP_TRIANGLEFAN_IMM:
            /* D3DHAL_DP2TRIANGLEFAN_IMM's edge flags, then count + 2
             * vertices inline from the next DWORD boundary. */
            if (left < 4ul) {
                goto malformed;
            }
            {
                const v9x_u8 *inline_vertices = data + 4;
                v9x_u32 pad = v9x_dp2_pad(inline_vertices);
                v9x_u32 bytes = count != 0ul
                    ? (count + 2ul) * stream->vertex_stride : 0ul;

                need = 4ul + pad + bytes;
                if (need > left) {
                    goto malformed;
                }
                if (count != 0ul) {
                    sink->fan(sink->user, inline_vertices + pad, count + 2ul);
                }
            }
            result->triangles += count;
            break;

        case V9X_DP2OP_LINELIST_IMM:
            need = v9x_dp2_pad(data) + count * 2ul * stream->vertex_stride;
            if (need > left) {
                goto malformed;
            }
            ++result->undrawn;
            break;

        case V9X_DP2OP_TEXTURESTAGESTATE:
            /* D3DHAL_DP2TEXTURESTAGESTATE: WORD stage, WORD state, DWORD
             * value. */
            need = count * 8ul;
            if (need > left) {
                goto malformed;
            }
            for (i = 0ul; i < count; ++i) {
                sink->stage_state(sink->user,
                                  v9x_dp2_word(data + i * 8ul),
                                  v9x_dp2_word(data + i * 8ul + 2ul),
                                  v9x_dp2_dword(data + i * 8ul + 4ul));
            }
            result->stage_states += count;
            break;

        case V9X_DP2OP_INDEXEDTRIANGLELIST2:
            /* D3DHAL_DP2STARTVERTEX, then three indices a triangle, each
             * relative to the start vertex. */
            need = 2ul + count * 6ul;
            if (need > left ||
                !v9x_dp2_indices(stream, data + 2, count * 3ul, 2ul,
                                 v9x_dp2_word(data))) {
                goto malformed;
            }
            {
                v9x_u32 base = v9x_dp2_word(data);

                for (i = 0ul; i < count; ++i) {
                    const v9x_u8 *t = data + 2ul + i * 6ul;

                    sink->triangle(
                        sink->user,
                        v9x_dp2_vertex(stream, base + v9x_dp2_word(t)),
                        v9x_dp2_vertex(stream, base + v9x_dp2_word(t + 2)),
                        v9x_dp2_vertex(stream, base + v9x_dp2_word(t + 4)));
                }
            }
            result->triangles += count;
            break;

        case V9X_DP2OP_INDEXEDLINELIST2:
            need = 2ul + count * 4ul;
            if (need > left ||
                !v9x_dp2_indices(stream, data + 2, count * 2ul, 2ul,
                                 v9x_dp2_word(data))) {
                goto malformed;
            }
            ++result->undrawn;
            break;

        case V9X_DP2OP_VIEWPORTINFO:
            /* The runtime clips to the viewport for a driver that reports
             * no guard band, so the rectangle is consumed and not used. */
            need = count * 16ul;
            if (need > left) {
                goto malformed;
            }
            break;

        case V9X_DP2OP_WINFO:
        case V9X_DP2OP_ZRANGE:
            need = count * 8ul;
            if (need > left) {
                goto malformed;
            }
            break;

        case V9X_DP2OP_SETPALETTE:
            need = count * 12ul;
            if (need > left) {
                goto malformed;
            }
            ++result->undrawn;
            break;

        case V9X_DP2OP_UPDATEPALETTE:
            /* Each D3DHAL_DP2UPDATEPALETTE is followed by its own entries,
             * so the record's length is the sum. */
            for (i = 0ul; i < count; ++i) {
                v9x_u32 entries;

                if (left - need < 8ul) {
                    goto malformed;
                }
                entries = v9x_dp2_word(data + need + 6ul);
                need += 8ul;
                if (entries * 4ul > left - need) {
                    goto malformed;
                }
                need += entries * 4ul;
            }
            ++result->undrawn;
            break;

        default:
            result->status = V9X_DP2_UNPARSED;
            return;
        }

        if (op < 64ul) {
            result->ops_seen[op >> 5] |= 1ul << (op & 31ul);
        }
        ++result->records;
        at += 4ul + need;
    }
    result->stop_offset = at;
    result->stop_op = 0ul;
    return;

malformed:
    result->status = V9X_DP2_MALFORMED;
}
