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

/* Bytes per entry of the fixed-size DirectX 7 records (d3dhal.h, the
 * table in docs\specifications\haswell-vertex-stage-tnl.md section 3):
 * D3DMATERIAL7, CREATELIGHT's index, SETTRANSFORM's type and D3DMATRIX,
 * TEXBLT's handles, point, rectangle and flags, STATESET's operation,
 * parameter and type, SETPRIORITY's handle and priority,
 * SETRENDERTARGET's two handles, SETTEXLOD's handle and level, and
 * SETCLIPPLANE's index and plane. */
static v9x_u32 v9x_dp2_dx7_entry_bytes(v9x_u32 op)
{
    switch (op) {
    case V9X_DP2OP_SETMATERIAL:     return 68ul;
    case V9X_DP2OP_CREATELIGHT:     return 4ul;
    case V9X_DP2OP_SETTRANSFORM:    return 68ul;
    case V9X_DP2OP_TEXBLT:          return 36ul;
    case V9X_DP2OP_STATESET:        return 12ul;
    case V9X_DP2OP_SETPRIORITY:     return 8ul;
    case V9X_DP2OP_SETRENDERTARGET: return 8ul;
    case V9X_DP2OP_SETTEXLOD:       return 8ul;
    default:                        return 20ul;   /* SETCLIPPLANE */
    }
}

/* Bytes of each DirectX 8 record's fixed entry (DDK d3dhal.h; the table in
 * docs\specifications\ddi8-on-win9x.md section 5): handles, strides and
 * counts, the draws' primitive type and counts, VOLUMEBLT's two handles,
 * point, D3DBOX and flags, BUFFERBLT's two handles, offset, D3DRANGE and
 * flags, MULTIPLYTRANSFORM's type and D3DMATRIX, ADDDIRTYRECT's handle and
 * RECTL, and ADDDIRTYBOX's handle and D3DBOX. */
static v9x_u32 v9x_dp2_dx8_entry_bytes(v9x_u32 op)
{
    switch (op) {
    case V9X_DP2OP_CREATEVERTEXSHADER:    return 12ul;
    case V9X_DP2OP_SETVERTEXSHADERCONST:  return 8ul;
    case V9X_DP2OP_SETSTREAMSOURCE:       return 12ul;
    case V9X_DP2OP_SETSTREAMSOURCEUM:     return 8ul;
    case V9X_DP2OP_SETINDICES:            return 8ul;
    case V9X_DP2OP_DRAWPRIMITIVE:         return 12ul;
    case V9X_DP2OP_DRAWINDEXEDPRIMITIVE:  return 24ul;
    case V9X_DP2OP_CREATEPIXELSHADER:     return 8ul;
    case V9X_DP2OP_SETPIXELSHADERCONST:   return 8ul;
    case V9X_DP2OP_CLIPPEDTRIANGLEFAN:    return 12ul;
    case V9X_DP2OP_DRAWPRIMITIVE2:        return 12ul;
    case V9X_DP2OP_DRAWINDEXEDPRIMITIVE2: return 24ul;
    case V9X_DP2OP_VOLUMEBLT:             return 48ul;
    case V9X_DP2OP_BUFFERBLT:             return 24ul;
    case V9X_DP2OP_MULTIPLYTRANSFORM:     return 68ul;
    case V9X_DP2OP_ADDDIRTYRECT:          return 20ul;
    case V9X_DP2OP_ADDDIRTYBOX:           return 28ul;
    default:                              return 4ul;  /* one handle */
    }
}

/*
 * The length of count entries of a DirectX 8 record at data, within left
 * bytes, into *bytes; 0 when it runs past them. The shader records carry
 * their declaration and code after each entry, and the constant records
 * dwCount four-float registers; every size is checked against what is
 * left before it is added, so a huge one cannot wrap.
 */
static int v9x_dp2_dx8_length(v9x_u32 op, v9x_u32 count,
                              const v9x_u8 *data, v9x_u32 left,
                              v9x_u32 *bytes)
{
    v9x_u32 fixed = v9x_dp2_dx8_entry_bytes(op);
    v9x_u32 need = 0ul;
    v9x_u32 i;

    if (op != V9X_DP2OP_CREATEVERTEXSHADER &&
        op != V9X_DP2OP_CREATEPIXELSHADER &&
        op != V9X_DP2OP_SETVERTEXSHADERCONST &&
        op != V9X_DP2OP_SETPIXELSHADERCONST) {
        need = count * fixed;
        if (need > left) {
            return 0;
        }
        *bytes = need;
        return 1;
    }
    for (i = 0ul; i < count; ++i) {
        const v9x_u8 *entry = data + need;
        v9x_u32 room;
        v9x_u32 extra;

        if (left - need < fixed) {
            return 0;
        }
        room = left - need - fixed;
        if (op == V9X_DP2OP_CREATEVERTEXSHADER) {
            v9x_u32 declaration = v9x_dp2_dword(entry + 4);
            v9x_u32 code = v9x_dp2_dword(entry + 8);

            if (declaration > room || code > room - declaration) {
                return 0;
            }
            extra = declaration + code;
        } else if (op == V9X_DP2OP_CREATEPIXELSHADER) {
            extra = v9x_dp2_dword(entry + 4);
            if (extra > room) {
                return 0;
            }
        } else {
            extra = v9x_dp2_dword(entry + 4);
            if (extra > room / 16ul) {
                return 0;
            }
            extra *= 16ul;
        }
        need += fixed + extra;
    }
    *bytes = need;
    return 1;
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
    result->points = 0ul;
    result->lines = 0ul;
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
            if (sink->point == 0) {
                ++result->undrawn;
                break;
            }
            /* D3DHAL_DP2POINTS: WORD count, WORD start vertex. */
            for (i = 0ul; i < count; ++i) {
                v9x_u32 n = v9x_dp2_word(data + i * 4ul);
                v9x_u32 first = v9x_dp2_word(data + i * 4ul + 2ul);
                v9x_u32 k;

                for (k = 0ul; k < n; ++k) {
                    sink->point(sink->user, v9x_dp2_vertex(stream, first + k));
                }
                result->points += n;
            }
            break;

        case V9X_DP2OP_INDEXEDLINELIST:
            need = count * 4ul;
            if (need > left ||
                !v9x_dp2_indices(stream, data, count * 2ul, 2ul, 0ul)) {
                goto malformed;
            }
            if (sink->line == 0) {
                ++result->undrawn;
                break;
            }
            for (i = 0ul; i < count; ++i) {
                sink->line(sink->user,
                           v9x_dp2_vertex(stream, v9x_dp2_word(data + i * 4ul)),
                           v9x_dp2_vertex(stream,
                                          v9x_dp2_word(data + i * 4ul + 2ul)));
            }
            result->lines += count;
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
            if (sink->line == 0) {
                ++result->undrawn;
                break;
            }
            {
                v9x_u32 first = v9x_dp2_word(data);

                for (i = 0ul; i < count; ++i) {
                    sink->line(sink->user,
                               v9x_dp2_vertex(stream, first + i * 2ul),
                               v9x_dp2_vertex(stream, first + i * 2ul + 1ul));
                }
            }
            result->lines += count;
            break;

        case V9X_DP2OP_LINESTRIP:
            need = 2ul;
            if (need > left ||
                (count != 0ul &&
                 !v9x_dp2_span(stream, v9x_dp2_word(data), count + 1ul))) {
                goto malformed;
            }
            if (sink->line == 0) {
                ++result->undrawn;
                break;
            }
            {
                v9x_u32 first = v9x_dp2_word(data);

                for (i = 0ul; i < count; ++i) {
                    sink->line(sink->user,
                               v9x_dp2_vertex(stream, first + i),
                               v9x_dp2_vertex(stream, first + i + 1ul));
                }
            }
            result->lines += count;
            break;

        case V9X_DP2OP_INDEXEDLINESTRIP:
            /* The start vertex, then count + 1 indices relative to it, as
             * the indexed strip and fan below (the DDK's d3ddx6.c). */
            need = count != 0ul ? 2ul + (count + 1ul) * 2ul : 0ul;
            if (need > left ||
                (count != 0ul &&
                 !v9x_dp2_indices(stream, data + 2, count + 1ul, 2ul,
                                  v9x_dp2_word(data)))) {
                goto malformed;
            }
            if (sink->line == 0) {
                ++result->undrawn;
                break;
            }
            for (i = 0ul; i < count; ++i) {
                v9x_u32 base = v9x_dp2_word(data);

                sink->line(sink->user,
                           v9x_dp2_vertex(stream,
                                          base + v9x_dp2_word(data + 2ul +
                                                              i * 2ul)),
                           v9x_dp2_vertex(stream,
                                          base + v9x_dp2_word(data + 4ul +
                                                              i * 2ul)));
            }
            result->lines += count;
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
            /* D3DHAL_DP2STARTVERTEX, then count + 2 indices relative to it
             * (the DDK's d3ddx6.c). Read without the start vertex, every
             * DX5 indexed strip at DDI 6 was malformed or drawn from the
             * wrong vertices (V9XD32 T11, A8U4I5, 2026-10-24). */
            need = count != 0ul ? 2ul + (count + 2ul) * 2ul : 0ul;
            if (need > left ||
                (count != 0ul &&
                 !v9x_dp2_indices(stream, data + 2, count + 2ul, 2ul,
                                  v9x_dp2_word(data)))) {
                goto malformed;
            }
            for (i = 0ul; i < count; ++i) {
                v9x_u32 swap = i & 1ul;
                v9x_u32 base = v9x_dp2_word(data);
                const v9x_u8 *words = data + 2;

                sink->triangle(
                    sink->user,
                    v9x_dp2_vertex(stream,
                                   base + v9x_dp2_word(words +
                                                       (i + swap) * 2ul)),
                    v9x_dp2_vertex(stream,
                                   base + v9x_dp2_word(words +
                                                       (i + 1ul - swap) *
                                                           2ul)),
                    v9x_dp2_vertex(stream,
                                   base + v9x_dp2_word(words +
                                                       (i + 2ul) * 2ul)));
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
            /* The start vertex, then count + 2 indices relative to it, as
             * the indexed strip above. */
            need = count != 0ul ? 2ul + (count + 2ul) * 2ul : 0ul;
            if (need > left ||
                (count != 0ul &&
                 !v9x_dp2_indices(stream, data + 2, count + 2ul, 2ul,
                                  v9x_dp2_word(data)))) {
                goto malformed;
            }
            for (i = 0ul; i < count; ++i) {
                v9x_u32 base = v9x_dp2_word(data);
                const v9x_u8 *words = data + 2;

                sink->triangle(
                    sink->user,
                    v9x_dp2_vertex(stream, base + v9x_dp2_word(words)),
                    v9x_dp2_vertex(stream,
                                   base + v9x_dp2_word(words +
                                                       (i + 1ul) * 2ul)),
                    v9x_dp2_vertex(stream,
                                   base + v9x_dp2_word(words +
                                                       (i + 2ul) * 2ul)));
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
            if (sink->line == 0) {
                ++result->undrawn;
                break;
            }
            {
                const v9x_u8 *inline_vertices = data + v9x_dp2_pad(data);

                for (i = 0ul; i < count; ++i) {
                    sink->line(sink->user,
                               inline_vertices +
                                   i * 2ul * stream->vertex_stride,
                               inline_vertices +
                                   (i * 2ul + 1ul) * stream->vertex_stride);
                }
            }
            result->lines += count;
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

                if (count != 0ul && sink->indexed_list != 0 &&
                    sink->indexed_list(sink->user,
                                       v9x_dp2_vertex(stream, base), base,
                                       data + 2, count)) {
                    result->triangles += count;
                    break;
                }
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
            if (sink->line == 0) {
                ++result->undrawn;
                break;
            }
            {
                v9x_u32 base = v9x_dp2_word(data);

                for (i = 0ul; i < count; ++i) {
                    const v9x_u8 *l = data + 2ul + i * 4ul;

                    sink->line(sink->user,
                               v9x_dp2_vertex(stream, base + v9x_dp2_word(l)),
                               v9x_dp2_vertex(stream,
                                              base + v9x_dp2_word(l + 2)));
                }
            }
            result->lines += count;
            break;

        case V9X_DP2OP_VIEWPORTINFO:
            /* The runtime clips to the viewport for pre-transformed
             * vertices, so only a T&L device reads the rectangle: a dx7
             * sink is shown it (hardware T&L step 6), and it is consumed
             * whatever the sink answers. */
            need = count * 16ul;
            if (need > left) {
                goto malformed;
            }
            if (sink->dx7 != 0) {
                (void)sink->dx7(sink->user, op, count, data, need);
            }
            break;

        case V9X_DP2OP_WINFO:
        case V9X_DP2OP_ZRANGE:
            need = count * 8ul;
            if (need > left) {
                goto malformed;
            }
            if (op == V9X_DP2OP_ZRANGE && sink->dx7 != 0) {
                (void)sink->dx7(sink->user, op, count, data, need);
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

        /*
         * The DirectX 7 records, each length from d3dhal.h: fixed-size
         * entries, except SETLIGHT, whose entry carries a D3DLIGHT7 only
         * for V9X_DP2_SETLIGHT_DATA, and CLEAR, one D3DHAL_DP2CLEAR header
         * (flags, colour, depth, stencil) then `count` rectangles.
         */
        case V9X_DP2OP_SETMATERIAL:
        case V9X_DP2OP_CREATELIGHT:
        case V9X_DP2OP_SETTRANSFORM:
        case V9X_DP2OP_TEXBLT:
        case V9X_DP2OP_STATESET:
        case V9X_DP2OP_SETPRIORITY:
        case V9X_DP2OP_SETRENDERTARGET:
        case V9X_DP2OP_SETTEXLOD:
        case V9X_DP2OP_SETCLIPPLANE:
        case V9X_DP2OP_SETLIGHT:
        case V9X_DP2OP_CLEAR:
            if (sink->dx7 == 0) {
                result->status = V9X_DP2_UNPARSED;
                return;
            }
            if (op == V9X_DP2OP_SETLIGHT) {
                for (i = 0ul; i < count; ++i) {
                    if (left - need < 8ul) {
                        goto malformed;
                    }
                    need += 8ul;
                    if (v9x_dp2_dword(data + need - 4ul) ==
                        V9X_DP2_SETLIGHT_DATA) {
                        if (left - need < V9X_DP2_LIGHT7_BYTES) {
                            goto malformed;
                        }
                        need += V9X_DP2_LIGHT7_BYTES;
                    }
                }
            } else if (op == V9X_DP2OP_CLEAR) {
                need = 16ul + count * 16ul;
                if (need > left) {
                    goto malformed;
                }
            } else {
                need = count * v9x_dp2_dx7_entry_bytes(op);
                if (need > left) {
                    goto malformed;
                }
            }
            if (!sink->dx7(sink->user, op, count, data, need)) {
                result->status = V9X_DP2_UNPARSED;
                return;
            }
            break;

        /* The DirectX 8 records: lengths here, meaning in the sink. The
         * patches' length depends on flags and their caps are zero, so
         * they go back to the runtime. */
        case V9X_DP2OP_DRAWRECTPATCH:
        case V9X_DP2OP_DRAWTRIPATCH:
            result->status = V9X_DP2_UNPARSED;
            return;

        case V9X_DP2OP_CREATEVERTEXSHADER:
        case V9X_DP2OP_DELETEVERTEXSHADER:
        case V9X_DP2OP_SETVERTEXSHADER:
        case V9X_DP2OP_SETVERTEXSHADERCONST:
        case V9X_DP2OP_SETSTREAMSOURCE:
        case V9X_DP2OP_SETSTREAMSOURCEUM:
        case V9X_DP2OP_SETINDICES:
        case V9X_DP2OP_DRAWPRIMITIVE:
        case V9X_DP2OP_DRAWINDEXEDPRIMITIVE:
        case V9X_DP2OP_CREATEPIXELSHADER:
        case V9X_DP2OP_DELETEPIXELSHADER:
        case V9X_DP2OP_SETPIXELSHADER:
        case V9X_DP2OP_SETPIXELSHADERCONST:
        case V9X_DP2OP_CLIPPEDTRIANGLEFAN:
        case V9X_DP2OP_DRAWPRIMITIVE2:
        case V9X_DP2OP_DRAWINDEXEDPRIMITIVE2:
        case V9X_DP2OP_VOLUMEBLT:
        case V9X_DP2OP_BUFFERBLT:
        case V9X_DP2OP_MULTIPLYTRANSFORM:
        case V9X_DP2OP_ADDDIRTYRECT:
        case V9X_DP2OP_ADDDIRTYBOX:
            if (sink->dx8 == 0) {
                result->status = V9X_DP2_UNPARSED;
                return;
            }
            if (!v9x_dp2_dx8_length(op, count, data, left, &need)) {
                goto malformed;
            }
            if (!sink->dx8(sink->user, op, count, data, need)) {
                result->status = V9X_DP2_UNPARSED;
                return;
            }
            break;

        default:
            result->status = V9X_DP2_UNPARSED;
            return;
        }

        if (op < 64ul) {
            result->ops_seen[op >> 5] |= 1ul << (op & 31ul);
        }
        if (sink->record != 0) {
            sink->record(sink->user, op, count, data, need);
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

/* d3dtypes.h: the D3DFVF_ bits this driver reads. */
#define V9X_DP2_FVF_POSITION_MASK 0x00eul
#define V9X_DP2_FVF_XYZRHW        0x004ul
#define V9X_DP2_FVF_NORMAL        0x010ul
#define V9X_DP2_FVF_RESERVED1     0x020ul
#define V9X_DP2_FVF_DIFFUSE       0x040ul
#define V9X_DP2_FVF_SPECULAR      0x080ul
#define V9X_DP2_FVF_TEXCOUNT(f)   (((f) >> 8) & 0xful)
/* D3DFVF_TEXTUREFORMAT1..4 are encoded 3, 0, 1, 2 at bits 16 + 2i. */
#define V9X_DP2_FVF_TEXFLOATS(f, i) \
    ("\2\3\4\1"[((f) >> (16ul + 2ul * (i))) & 3ul])

int v9x_dp2_fvf_layout(v9x_u32 fvf, V9X_DP2_FVF *layout)
{
    v9x_u32 at = 16ul;
    v9x_u32 sets;
    v9x_u32 i;

    if ((fvf & V9X_DP2_FVF_POSITION_MASK) != V9X_DP2_FVF_XYZRHW ||
        (fvf & V9X_DP2_FVF_NORMAL) != 0ul) {
        return 0;
    }
    sets = V9X_DP2_FVF_TEXCOUNT(fvf);
    if (sets > 8ul) {
        return 0;
    }
    /* DX6's D3DLVERTEX padding DWORD, still a legal FVF bit. */
    if ((fvf & V9X_DP2_FVF_RESERVED1) != 0ul) {
        at += 4ul;
    }
    layout->diffuse = V9X_DP2_FVF_ABSENT;
    layout->specular = V9X_DP2_FVF_ABSENT;
    layout->tex0 = V9X_DP2_FVF_ABSENT;
    layout->tex0_floats = 0ul;
    layout->tex_sets = sets;
    if ((fvf & V9X_DP2_FVF_DIFFUSE) != 0ul) {
        layout->diffuse = at;
        at += 4ul;
    }
    if ((fvf & V9X_DP2_FVF_SPECULAR) != 0ul) {
        layout->specular = at;
        at += 4ul;
    }
    for (i = 0ul; i < sets; ++i) {
        v9x_u32 floats = (v9x_u32)V9X_DP2_FVF_TEXFLOATS(fvf, i);

        if (i == 0ul) {
            layout->tex0 = at;
            layout->tex0_floats = floats;
        }
        layout->tex[i] = at;
        layout->tex_floats[i] = floats;
        at += floats * 4ul;
    }
    layout->stride = at;
    return 1;
}

int v9x_dp2_fvf_is_tlvertex(const V9X_DP2_FVF *layout)
{
    return layout->stride == 32ul && layout->diffuse == 16ul &&
           layout->specular == 20ul && layout->tex0 == 24ul &&
           layout->tex0_floats == 2ul;
}

static void v9x_dp2_put(v9x_u8 *p, v9x_u32 value)
{
    p[0] = (v9x_u8)value;
    p[1] = (v9x_u8)(value >> 8);
    p[2] = (v9x_u8)(value >> 16);
    p[3] = (v9x_u8)(value >> 24);
}

void v9x_dp2_fvf_convert(const V9X_DP2_FVF *layout, const v9x_u8 *source,
                         v9x_u8 *tlvertex)
{
    v9x_dp2_fvf_convert_set(layout, 0ul, source, tlvertex);
}

void v9x_dp2_fvf_convert_set(const V9X_DP2_FVF *layout, v9x_u32 set,
                             const v9x_u8 *source, v9x_u8 *tlvertex)
{
    v9x_u32 i;
    v9x_u32 tex = V9X_DP2_FVF_ABSENT;
    v9x_u32 floats = 0ul;

    /* The stage's set if the vertex has it, else set 0: an index past the
     * format is the application's error, and reading set 0 keeps the read
     * inside the vertex. */
    if (set < layout->tex_sets) {
        tex = layout->tex[set];
        floats = layout->tex_floats[set];
    } else if (layout->tex_sets != 0ul) {
        tex = layout->tex[0];
        floats = layout->tex_floats[0];
    }
    /*
     * Both on a dword boundary - every vertex buffer the runtime hands
     * over, its strides being whole dwords - a dword at a time: built a
     * byte at a time, this was over half the HAL's time in DB3D's
     * TriBatch at DDI 6 (A8U4I5, 2026-10-24). Every layout offset is a
     * whole number of dwords (v9x_dp2_fvf_layout), so no field straddles.
     */
    if (((unsigned long)source & 3ul) == 0ul &&
        ((unsigned long)tlvertex & 3ul) == 0ul) {
        const v9x_u32 *from = (const v9x_u32 *)source;
        v9x_u32 *to = (v9x_u32 *)tlvertex;

        to[0] = from[0];
        to[1] = from[1];
        to[2] = from[2];
        to[3] = from[3];
        to[4] = layout->diffuse != V9X_DP2_FVF_ABSENT
                    ? from[layout->diffuse / 4ul] : 0xfffffffful;
        to[5] = layout->specular != V9X_DP2_FVF_ABSENT
                    ? from[layout->specular / 4ul] : 0xff000000ul;
        to[6] = tex != V9X_DP2_FVF_ABSENT ? from[tex / 4ul] : 0ul;
        to[7] = tex != V9X_DP2_FVF_ABSENT && floats >= 2ul
                    ? from[tex / 4ul + 1ul] : 0ul;
        return;
    }
    for (i = 0ul; i < 16ul; ++i) {
        tlvertex[i] = source[i];
    }
    v9x_dp2_put(tlvertex + 16, layout->diffuse != V9X_DP2_FVF_ABSENT
                                   ? v9x_dp2_dword(source + layout->diffuse)
                                   : 0xfffffffful);
    v9x_dp2_put(tlvertex + 20, layout->specular != V9X_DP2_FVF_ABSENT
                                   ? v9x_dp2_dword(source + layout->specular)
                                   : 0xff000000ul);
    /* Float bits are copied, never converted: 0ul is +0.0f. */
    v9x_dp2_put(tlvertex + 24, tex != V9X_DP2_FVF_ABSENT
                                   ? v9x_dp2_dword(source + tex)
                                   : 0ul);
    v9x_dp2_put(tlvertex + 28, tex != V9X_DP2_FVF_ABSENT && floats >= 2ul
                                   ? v9x_dp2_dword(source + tex + 4ul)
                                   : 0ul);
}

void v9x_dp2_fvf_texcoords(const V9X_DP2_FVF *layout, v9x_u32 set,
                           const v9x_u8 *source, v9x_u8 *pair)
{
    v9x_u32 tex = V9X_DP2_FVF_ABSENT;
    v9x_u32 floats = 0ul;

    if (set < layout->tex_sets) {
        tex = layout->tex[set];
        floats = layout->tex_floats[set];
    } else if (layout->tex_sets != 0ul) {
        tex = layout->tex[0];
        floats = layout->tex_floats[0];
    }
    v9x_dp2_put(pair, tex != V9X_DP2_FVF_ABSENT
                          ? v9x_dp2_dword(source + tex) : 0ul);
    v9x_dp2_put(pair + 4, tex != V9X_DP2_FVF_ABSENT && floats >= 2ul
                              ? v9x_dp2_dword(source + tex + 4ul) : 0ul);
}

/* Floats through the bytes, so the walker stays free of aliasing casts. */
typedef union v9x_dp2_float_bits {
    float f;
    v9x_u32 u;
} V9X_DP2_FLOAT_BITS;

static float v9x_dp2_get_float(const v9x_u8 *p)
{
    V9X_DP2_FLOAT_BITS bits;

    bits.u = v9x_dp2_dword(p);
    return bits.f;
}

/* Below this a divisor is taken as zero: the pair is left as given rather
 * than sent to the engine as an infinity. */
#define V9X_DP2_PROJECT_EPSILON 1.0e-20f

void v9x_dp2_fvf_project(const V9X_DP2_FVF *layout, v9x_u32 set,
                         v9x_u32 ttff, const v9x_u8 *source, float *uv)
{
    v9x_u32 count = ttff & V9X_DP2_TTFF_COUNT_MASK;
    v9x_u32 tex;
    v9x_u32 floats;
    float q;

    if ((ttff & V9X_DP2_TTFF_PROJECTED) == 0ul || count < 2ul ||
        count > 4ul || layout->tex_sets == 0ul) {
        return;
    }
    if (set >= layout->tex_sets) {
        set = 0ul;
    }
    tex = layout->tex[set];
    floats = layout->tex_floats[set];
    if (floats < count) {
        return;
    }
    q = v9x_dp2_get_float(source + tex + (count - 1ul) * 4ul);
    if (q < V9X_DP2_PROJECT_EPSILON && q > -V9X_DP2_PROJECT_EPSILON) {
        return;
    }
    uv[0] = uv[0] / q;
    uv[1] = uv[1] / q;
}

/* A copy of the 32-byte vertex at source with sx and sy moved by dx, dy. */
static void v9x_dp2_moved(const v9x_u8 *source, float dx, float dy,
                          v9x_u8 *out)
{
    V9X_DP2_FLOAT_BITS bits;
    v9x_u32 i;

    for (i = 0ul; i < 32ul; ++i) {
        out[i] = source[i];
    }
    bits.f = v9x_dp2_get_float(source) + dx;
    v9x_dp2_put(out, bits.u);
    bits.f = v9x_dp2_get_float(source + 4) + dy;
    v9x_dp2_put(out + 4, bits.u);
}

v9x_u32 v9x_dp2_line_quad(const v9x_u8 *a, const v9x_u8 *b, v9x_u8 *out)
{
    float dx = v9x_dp2_get_float(b) - v9x_dp2_get_float(a);
    float dy = v9x_dp2_get_float(b + 4) - v9x_dp2_get_float(a + 4);
    float adx = dx < 0.0f ? -dx : dx;
    float ady = dy < 0.0f ? -dy : dy;
    /* Across the minor axis, one pixel past the line (d3d_dp2.h). */
    float ox = adx >= ady ? 0.0f : 1.0f;
    float oy = adx >= ady ? 1.0f : 0.0f;

    if (adx == 0.0f && ady == 0.0f) {
        return 0ul;
    }
    /* A+ A B+, then A B B+: both start on a copy of a. */
    v9x_dp2_moved(a, ox, oy, out);
    v9x_dp2_moved(a, 0.0f, 0.0f, out + 32);
    v9x_dp2_moved(b, ox, oy, out + 64);
    v9x_dp2_moved(a, 0.0f, 0.0f, out + 96);
    v9x_dp2_moved(b, 0.0f, 0.0f, out + 128);
    v9x_dp2_moved(b, ox, oy, out + 160);
    return 2ul;
}

v9x_u32 v9x_dp2_point_quad(const v9x_u8 *v, v9x_u8 *out)
{
    v9x_dp2_moved(v, 0.0f, 0.0f, out);
    v9x_dp2_moved(v, 1.0f, 0.0f, out + 32);
    v9x_dp2_moved(v, 1.0f, 1.0f, out + 64);
    v9x_dp2_moved(v, 0.0f, 0.0f, out + 96);
    v9x_dp2_moved(v, 1.0f, 1.0f, out + 128);
    v9x_dp2_moved(v, 0.0f, 1.0f, out + 160);
    return 2ul;
}

/* The most primitives one DirectX 8 draw may name. Far above any real
 * draw, and low enough that the vertex arithmetic below cannot wrap. */
#define V9X_DP2_DRAW_MAX_PRIMITIVES 0x00ffffffful

/* Vertices a draw of count primitives takes, or 0 for a type this does
 * not know. */
static v9x_u32 v9x_dp2_draw_span(v9x_u32 primitive, v9x_u32 count)
{
    switch (primitive) {
    case V9X_D3DPT_POINTLIST:     return count;
    case V9X_D3DPT_LINELIST:      return count * 2ul;
    case V9X_D3DPT_LINESTRIP:     return count + 1ul;
    case V9X_D3DPT_TRIANGLELIST:  return count * 3ul;
    case V9X_D3DPT_TRIANGLESTRIP: return count + 2ul;
    case V9X_D3DPT_TRIANGLEFAN:   return count + 2ul;
    default:                      return 0ul;
    }
}

/* The draw's i-th vertex as a vertex number, in *number; 0 when it lies
 * outside the vertices. i is below the draw's span, which the caller has
 * already held inside the indices. */
static int v9x_dp2_draw_number(const V9X_DP2_DRAW *draw, v9x_u32 i,
                               v9x_u32 *number)
{
    v9x_u32 raw;

    if (draw->indices == 0) {
        raw = draw->first + i;
        if (raw >= draw->vertex_count) {
            return 0;
        }
        *number = raw;
        return 1;
    }
    raw = draw->index_bytes == 4ul
              ? v9x_dp2_dword(draw->indices + (draw->first + i) * 4ul)
              : v9x_dp2_word(draw->indices + (draw->first + i) * 2ul);
    if (draw->base < 0l) {
        v9x_u32 below = (v9x_u32)(-(draw->base + 1l)) + 1ul;

        if (raw < below) {
            return 0;
        }
        raw -= below;
    } else {
        if (raw >= draw->vertex_count ||
            (v9x_u32)draw->base >= draw->vertex_count - raw) {
            return 0;
        }
        raw += (v9x_u32)draw->base;
    }
    if (raw >= draw->vertex_count) {
        return 0;
    }
    *number = raw;
    return 1;
}

static const v9x_u8 *v9x_dp2_draw_vertex(const V9X_DP2_DRAW *draw,
                                         v9x_u32 i)
{
    v9x_u32 number = 0ul;

    (void)v9x_dp2_draw_number(draw, i, &number);
    return draw->vertices + number * draw->vertex_stride;
}

int v9x_dp2_negative_base(V9X_DP2_DRAW *draw, const v9x_u8 *stream,
                          v9x_u32 stream_bytes, v9x_s32 offset)
{
    v9x_u32 stride = draw->vertex_stride;
    v9x_u32 below;
    v9x_u32 whole;
    v9x_u32 rest;

    if (stride == 0ul || offset >= 0l) {
        return 0;
    }
    /* -offset without overflowing on the most negative one. */
    below = (v9x_u32)(-(offset + 1l)) + 1ul;
    whole = below / stride + (below % stride != 0ul ? 1ul : 0ul);
    rest = whole * stride - below;
    if (rest > stream_bytes) {
        return 0;
    }
    draw->vertices = stream + rest;
    draw->vertex_count = (stream_bytes - rest) / stride;
    draw->base = -(v9x_s32)whole;
    return 1;
}

v9x_u32 v9x_dp2_draw(const V9X_DP2_DRAW *draw, const V9X_DP2_SINK *sink,
                     V9X_DP2_RESULT *result)
{
    v9x_u32 span;
    v9x_u32 number;
    v9x_u32 i;

    if (draw->count == 0ul) {
        return V9X_DP2_OK;
    }
    if (draw->count > V9X_DP2_DRAW_MAX_PRIMITIVES) {
        return V9X_DP2_MALFORMED;
    }
    span = v9x_dp2_draw_span(draw->primitive, draw->count);
    if (span == 0ul) {
        return V9X_DP2_MALFORMED;
    }
    if (draw->indices != 0 &&
        ((draw->index_bytes != 2ul && draw->index_bytes != 4ul) ||
         draw->first > draw->index_count ||
         span > draw->index_count - draw->first)) {
        return V9X_DP2_MALFORMED;
    }
    if (draw->indices == 0 && draw->first > 0xfffffffful - span) {
        return V9X_DP2_MALFORMED;
    }

    /* Every vertex first, so a bad draw draws nothing. */
    for (i = 0ul; i < span; ++i) {
        if (!v9x_dp2_draw_number(draw, i, &number)) {
            return V9X_DP2_MALFORMED;
        }
    }

    switch (draw->primitive) {
    case V9X_D3DPT_POINTLIST:
        if (sink->point == 0) {
            ++result->undrawn;
            break;
        }
        for (i = 0ul; i < draw->count; ++i) {
            sink->point(sink->user, v9x_dp2_draw_vertex(draw, i));
        }
        result->points += draw->count;
        break;

    case V9X_D3DPT_LINELIST:
    case V9X_D3DPT_LINESTRIP:
        if (sink->line == 0) {
            ++result->undrawn;
            break;
        }
        for (i = 0ul; i < draw->count; ++i) {
            v9x_u32 a = draw->primitive == V9X_D3DPT_LINELIST ? i * 2ul : i;

            sink->line(sink->user, v9x_dp2_draw_vertex(draw, a),
                       v9x_dp2_draw_vertex(draw, a + 1ul));
        }
        result->lines += draw->count;
        break;

    case V9X_D3DPT_TRIANGLELIST:
        if (draw->indices == 0) {
            sink->list(sink->user, v9x_dp2_draw_vertex(draw, 0ul),
                       draw->count);
            break;
        }
        /* The indexed-list sink takes WORD indices relative to a vertex
         * that exists, so only a 16-bit list whose base is not negative. */
        if (draw->index_bytes == 2ul && draw->base >= 0l &&
            sink->indexed_list != 0 &&
            sink->indexed_list(sink->user,
                               draw->vertices + (v9x_u32)draw->base *
                                                    draw->vertex_stride,
                               (v9x_u32)draw->base,
                               draw->indices + draw->first * 2ul,
                               draw->count)) {
            break;
        }
        for (i = 0ul; i < draw->count; ++i) {
            sink->triangle(sink->user, v9x_dp2_draw_vertex(draw, i * 3ul),
                           v9x_dp2_draw_vertex(draw, i * 3ul + 1ul),
                           v9x_dp2_draw_vertex(draw, i * 3ul + 2ul));
        }
        break;

    case V9X_D3DPT_TRIANGLESTRIP:
        for (i = 0ul; i < draw->count; ++i) {
            v9x_u32 swap = i & 1ul;

            sink->triangle(sink->user,
                           v9x_dp2_draw_vertex(draw, i + swap),
                           v9x_dp2_draw_vertex(draw, i + 1ul - swap),
                           v9x_dp2_draw_vertex(draw, i + 2ul));
        }
        break;

    default:
        if (draw->indices == 0) {
            sink->fan(sink->user, v9x_dp2_draw_vertex(draw, 0ul),
                      draw->count + 2ul);
            break;
        }
        for (i = 0ul; i < draw->count; ++i) {
            sink->triangle(sink->user, v9x_dp2_draw_vertex(draw, 0ul),
                           v9x_dp2_draw_vertex(draw, i + 1ul),
                           v9x_dp2_draw_vertex(draw, i + 2ul));
        }
        break;
    }
    if (draw->primitive >= V9X_D3DPT_TRIANGLELIST) {
        result->triangles += draw->count;
    }
    return V9X_DP2_OK;
}
