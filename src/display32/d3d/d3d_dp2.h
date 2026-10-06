/*
 * The DrawPrimitives2 command stream, walked.
 *
 * DrawPrimitives2 is the DirectX 6 driver interface (DDI 6): one call hands
 * the driver a buffer of D3DHAL_DP2COMMAND records - render states, texture
 * stage states and primitives - and a separate pool of vertices those
 * primitives index into. A driver that publishes it through
 * GUID_D3DCallbacks3 is what the Direct3D 8 runtime needs before it will use
 * hardware at all: on the Rage XL (A8U4I5, 2026-10-06) DxDiag 9.0c skipped
 * its D3D8 test with "the display driver does not support it", and the GUID
 * table showed the runtime asking for GUID_D3DCallbacks3 and
 * GUID_ZPixelFormats and being declined
 * (docs\probe\a8u4i5-rage-xl-pci-2026-10-06\README.md).
 *
 * This file only walks. It knows the record layouts and nothing about what a
 * state means or how a triangle is drawn: the caller supplies a sink, the
 * same arrangement r3d uses, so all of it compiles on the host and is held
 * to tests\host\test_d3d_dp2.c.
 *
 * THE RECORD LAYOUTS are transcribed from the Windows SDK's d3dhal.h
 * (10.0.26100.0, um), which still carries the DirectX 7 DDK declarations;
 * the Windows 98 DDK predates DrawPrimitives2 and has none of them. Opcode
 * numbers are the D3DHAL_DP2OPERATION values. The low opcodes deliberately
 * equal the execute-buffer D3DOP_* numbers with the same payloads, which is
 * how the runtime passes an execute buffer through this interface
 * (D3DHALDP2_EXECUTEBUFFER).
 *
 * WHAT IS NOT PARSED IS HANDED BACK, never skipped. An opcode this walker
 * does not know stops the walk with V9X_DP2_UNPARSED and the offset of that
 * record; the caller reports D3DERR_COMMAND_UNPARSED and dwErrorOffset, and
 * the runtime parses the record itself. That is the interface's own
 * contract for execute-buffer instructions such as D3DOP_PROCESSVERTICES,
 * and skipping a record of unknown length is impossible anyway.
 *
 * EVERY LENGTH AND EVERY INDEX IS CHECKED before it is used, the same
 * memory-safety boundary as the DX5 indexed path in d3d_core.c: the counts
 * are WORDs from the application's side of the runtime, and a trusted one is
 * an arbitrary read past either buffer.
 */
#ifndef VELOCITY9X_D3D_DP2_H
#define VELOCITY9X_D3D_DP2_H

#include "velocity9x/types.h"

/* D3DHAL_DP2OPERATION, d3dhal.h. Only the DirectX 6 set; later opcodes are
 * DirectX 7 and 8 records that a DDI 6 driver is never sent and that this
 * walker hands back unparsed if it is. */
#define V9X_DP2OP_POINTS                1ul
#define V9X_DP2OP_INDEXEDLINELIST       2ul
#define V9X_DP2OP_INDEXEDTRIANGLELIST   3ul
#define V9X_DP2OP_RENDERSTATE           8ul
#define V9X_DP2OP_LINELIST             15ul
#define V9X_DP2OP_LINESTRIP            16ul
#define V9X_DP2OP_INDEXEDLINESTRIP     17ul
#define V9X_DP2OP_TRIANGLELIST         18ul
#define V9X_DP2OP_TRIANGLESTRIP        19ul
#define V9X_DP2OP_INDEXEDTRIANGLESTRIP 20ul
#define V9X_DP2OP_TRIANGLEFAN          21ul
#define V9X_DP2OP_INDEXEDTRIANGLEFAN   22ul
#define V9X_DP2OP_TRIANGLEFAN_IMM      23ul
#define V9X_DP2OP_LINELIST_IMM         24ul
#define V9X_DP2OP_TEXTURESTAGESTATE    25ul
#define V9X_DP2OP_INDEXEDTRIANGLELIST2 26ul
#define V9X_DP2OP_INDEXEDLINELIST2     27ul
#define V9X_DP2OP_VIEWPORTINFO         28ul
#define V9X_DP2OP_WINFO                29ul
#define V9X_DP2OP_SETPALETTE           30ul
#define V9X_DP2OP_UPDATEPALETTE        31ul
#define V9X_DP2OP_ZRANGE               32ul

/* How a walk ended. */
#define V9X_DP2_OK        0ul  /* every record consumed                      */
#define V9X_DP2_UNPARSED  1ul  /* an opcode this walker does not parse       */
#define V9X_DP2_MALFORMED 2ul  /* a length or an index outside its buffer    */

/*
 * What the walker hands its caller. Vertex arguments point into the vertex
 * pool (or, for the _IMM records, into the command buffer) at a vertex of
 * the stream's stride; the walker has checked that every one lies inside its
 * buffer. A list or fan pointer is the first of a contiguous run.
 *
 * Triangles keep Direct3D's winding: a strip's odd triangles arrive with
 * their first two vertices swapped, the same rule as the DX5 indexed path,
 * so that back-face culling sees one winding for the whole strip.
 */
typedef struct v9x_dp2_sink {
    void *user;
    void (*render_state)(void *user, v9x_u32 state, v9x_u32 value);
    void (*stage_state)(void *user, v9x_u32 stage, v9x_u32 state,
                        v9x_u32 value);
    void (*list)(void *user, const v9x_u8 *first, v9x_u32 triangles);
    void (*fan)(void *user, const v9x_u8 *first, v9x_u32 vertices);
    void (*triangle)(void *user, const v9x_u8 *a, const v9x_u8 *b,
                     const v9x_u8 *c);
    /* Optional, may be null: points and lines, one call each. With these
     * null the records are consumed and counted as undrawn. */
    void (*point)(void *user, const v9x_u8 *v);
    void (*line)(void *user, const v9x_u8 *a, const v9x_u8 *b);
    /* Optional, may be null: every record consumed, after the callbacks
     * above, with its payload and payload length. For instruments. */
    void (*record)(void *user, v9x_u32 op, v9x_u32 count,
                   const v9x_u8 *payload, v9x_u32 bytes);
} V9X_DP2_SINK;

typedef struct v9x_dp2_stream {
    const v9x_u8 *commands;      /* the first record                  */
    v9x_u32 command_bytes;       /* bytes of records from there        */
    const v9x_u8 *vertices;      /* vertex 0 of the pool               */
    v9x_u32 vertex_count;        /* vertices in the pool               */
    v9x_u32 vertex_stride;       /* bytes per vertex, pool and _IMM    */
} V9X_DP2_STREAM;

typedef struct v9x_dp2_result {
    v9x_u32 status;              /* V9X_DP2_*                          */
    v9x_u32 stop_offset;         /* bytes from commands to the record
                                    that stopped the walk, or to the end */
    v9x_u32 stop_op;             /* that record's opcode               */
    v9x_u32 records;             /* records consumed                   */
    v9x_u32 triangles;           /* triangles handed to the sink       */
    v9x_u32 states;              /* render states handed over          */
    v9x_u32 stage_states;        /* texture stage states handed over   */
    /* Records parsed and consumed but not drawn: points and lines when
     * the sink takes none, and the palette records. */
    v9x_u32 undrawn;
    /* Points and lines handed to the sink. */
    v9x_u32 points;
    v9x_u32 lines;
    /* One bit per opcode below 64 that was consumed. */
    v9x_u32 ops_seen[2];
} V9X_DP2_RESULT;

void v9x_dp2_walk(const V9X_DP2_STREAM *stream, const V9X_DP2_SINK *sink,
                  V9X_DP2_RESULT *result);

/*
 * Flexible vertex formats (dwVertexType), turned into the D3DTLVERTEX the
 * engines draw.
 *
 * A driver that reports dwFVFCaps zero is held to D3DFVF_TLVERTEX, and the
 * Direct3D 8 runtime will not use such a driver at all: d3d8.dll
 * 4.09.0000.0904 audits the legacy caps (0x40f6d0) and, finding FVFCaps
 * zero, cuts every format's operations down to display-mode only, so
 * GetDeviceCaps answers D3DERR_NOTAVAILABLE (0x8876086A, GitHub issue 2).
 * With one texture coordinate set reported, the runtime sends whatever
 * pre-transformed layout the application declared: XYZRHW always, then
 * DIFFUSE, SPECULAR and coordinate sets only if present.
 *
 * Only XYZRHW positions are taken, as no engine here transforms. A missing
 * diffuse is opaque white and a missing specular black with a fog factor of
 * one (no fog), which is what the runtime's own rasteriser assumes; missing
 * coordinates are zero. Of the coordinate sets only the first is kept.
 */
#define V9X_DP2_FVF_ABSENT 0xfffffffful

typedef struct v9x_dp2_fvf {
    v9x_u32 stride;              /* bytes per vertex                   */
    v9x_u32 diffuse;             /* byte offsets, or V9X_DP2_FVF_ABSENT */
    v9x_u32 specular;
    v9x_u32 tex0;
    v9x_u32 tex0_floats;         /* 1 to 4 when tex0 is present        */
} V9X_DP2_FVF;

/* Fill layout for fvf; 0 when this driver cannot draw the format. */
int v9x_dp2_fvf_layout(v9x_u32 fvf, V9X_DP2_FVF *layout);

/* Whether the layout is D3DTLVERTEX itself, so no copy is needed. */
int v9x_dp2_fvf_is_tlvertex(const V9X_DP2_FVF *layout);

/* One vertex of layout at source into the 32 bytes of a D3DTLVERTEX. */
void v9x_dp2_fvf_convert(const V9X_DP2_FVF *layout, const v9x_u8 *source,
                         v9x_u8 *tlvertex);

/*
 * Points and lines as triangles, for engines that draw nothing else.
 *
 * A DirectX 6 point is one pixel and a line one pixel wide, whatever the
 * point size (DDI 6 has no point sprites). A line becomes a quad from a to
 * b one pixel deep across its minor axis, from the line to one pixel past
 * it, and a point the unit square from its position to one pixel past. Not
 * half a pixel either side: the engines sample at the pixel centre plus
 * half a pixel, so a quad centred on row 150 lit row 149 on the Rage XL
 * (2026-10-07, tools\diag\dp2_repro_win32.c), while [c, c + 1) holds both
 * c and c + 0.5 and lands on the same row under either convention. The
 * major axis keeps a and b, so the last pixel is left out as Direct3D
 * does. Every output vertex is a copy of a
 * D3DTLVERTEX (32 bytes) with only sx or sy moved, and both triangles of a
 * line start on a copy of a, so flat shading takes a's colour as Direct3D
 * does. Each returns the triangles written to out (6 vertices of room): 2,
 * or 0 for a line of no length.
 */
v9x_u32 v9x_dp2_line_quad(const v9x_u8 *a, const v9x_u8 *b, v9x_u8 *out);
v9x_u32 v9x_dp2_point_quad(const v9x_u8 *v, v9x_u8 *out);

#endif
