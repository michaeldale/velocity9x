/* Host benchmark of the Rage II per-triangle path on Quake-2-like
 * perspective floor and wall triangles. Not part of the tree. */
#include <stdio.h>
#include <string.h>
#include "velocity9x/ati_rage2.h"
#include "velocity9x/ati_rage2_draw.h"
#include "velocity9x/ati_mach64_engine.h"

static unsigned long rdtsc_low(void);
#pragma aux rdtsc_low = 0x0f 0x31 value [eax] modify exact [eax edx];

#define MAX_TRI 4096
static struct v9x_r2_draw_vertex tris[MAX_TRI][3];
static int tri_count;

static void project(double x, double y, double z, double s, double t,
                    struct v9x_r2_draw_vertex *v)
{
    double sx = 320.0 + 320.0 * x / z;
    double sy = 240.0 - 320.0 * y / z;
    v->x = v9x_r2_snap((v9x_s32)(sx * 16.0));
    v->y = v9x_r2_snap((v9x_s32)(sy * 16.0));
    v->z = (v9x_u32)(65535.0 * (1.0 - 8.0 / z));
    v->argb = 0xffc0c0c0ul;
    v->fog = 255ul;
    v->q = 1.0 / z;
    v->tu = s;
    v->tv = t;
}

static int on_screen(const struct v9x_r2_draw_vertex *v)
{
    int k;
    for (k = 0; k < 3; ++k) {
        if (v[k].x < 0 || v[k].x > 639 * 16 || v[k].y < 0 ||
            v[k].y > 479 * 16) {
            return 0;
        }
    }
    return 1;
}

/* A plane cell (four corners) into two triangles. */
static void add_quad(double c[4][5])
{
    struct v9x_r2_draw_vertex v[4];
    int k;
    for (k = 0; k < 4; ++k) {
        if (c[k][2] < 12.0) {
            return;
        }
        project(c[k][0], c[k][1], c[k][2], c[k][3], c[k][4], &v[k]);
    }
    if (tri_count + 2 > MAX_TRI) {
        return;
    }
    tris[tri_count][0] = v[0]; tris[tri_count][1] = v[1];
    tris[tri_count][2] = v[2];
    if (on_screen(tris[tri_count])) ++tri_count;
    tris[tri_count][0] = v[0]; tris[tri_count][1] = v[2];
    tris[tri_count][2] = v[3];
    if (on_screen(tris[tri_count])) ++tri_count;
}

static void make_scene(double cell)
{
    double x, z, y;
    double c[4][5];
    tri_count = 0;
    /* floor y = -48, ceiling y = 80, walls x = +-160 */
    for (z = 8.0; z < 1024.0; z += cell) {
        for (x = -160.0; x < 160.0; x += cell) {
            double y0;
            int pass;
            for (pass = 0; pass < 2; ++pass) {
                y0 = pass == 0 ? -48.0 : 80.0;
                c[0][0] = x;        c[0][1] = y0; c[0][2] = z;
                c[1][0] = x + cell; c[1][1] = y0; c[1][2] = z;
                c[2][0] = x + cell; c[2][1] = y0; c[2][2] = z + cell;
                c[3][0] = x;        c[3][1] = y0; c[3][2] = z + cell;
                for (y = 0; y < 4; ++y) {
                    int k = (int)y;
                    c[k][3] = c[k][0] / 64.0;
                    c[k][4] = c[k][2] / 64.0;
                }
                add_quad(c);
            }
        }
        for (y = -48.0; y < 80.0; y += cell) {
            int side;
            for (side = 0; side < 2; ++side) {
                double xw = side == 0 ? -160.0 : 160.0;
                int k;
                c[0][0] = xw; c[0][1] = y;        c[0][2] = z;
                c[1][0] = xw; c[1][1] = y;        c[1][2] = z + cell;
                c[2][0] = xw; c[2][1] = y + cell; c[2][2] = z + cell;
                c[3][0] = xw; c[3][1] = y + cell; c[3][2] = z;
                for (k = 0; k < 4; ++k) {
                    c[k][3] = c[k][2] / 64.0;
                    c[k][4] = c[k][1] / 64.0;
                }
                add_quad(c);
            }
        }
    }
}


static double bound_of(const struct v9x_r2_draw_vertex *v, v9x_u32 lw,
                       v9x_u32 lh)
{
    double qmin = v[0].q, qmax = v[0].q, e, r = 0.0;
    int k, axis;
    for (k = 1; k < 3; ++k) {
        if (v[k].q < qmin) qmin = v[k].q;
        if (v[k].q > qmax) qmax = v[k].q;
    }
    e = (qmax - qmin) / (qmax + qmin);
    for (axis = 0; axis < 2; ++axis) {
        double lo, hi, sc = (double)(1ul << (axis == 0 ? lw : lh));
        lo = hi = (axis == 0 ? v[0].tu : v[0].tv) * sc;
        for (k = 1; k < 3; ++k) {
            double t = (axis == 0 ? v[k].tu : v[k].tv) * sc;
            if (t < lo) lo = t;
            if (t > hi) hi = t;
        }
        if ((hi - lo) / 2.0 > r) r = (hi - lo) / 2.0;
    }
    return 8.0 / 3.0 * r * e * e;
}

static void check_bound(const struct v9x_r2_texture *tex)
{
    unsigned long pass = 0, fail_bound = 0, affine = 0;
    double worst_ratio = 0.0;
    int i;
    for (i = 0; i < tri_count; ++i) {
        struct v9x_r2_vertex pos[3];
        struct v9x_r2_tex_coord tco[3];
        struct v9x_r2_flat_trap tr;
        struct v9x_r2_st st;
        v9x_u32 aff = 0;
        double texels, b;
        int k;
        for (k = 0; k < 3; ++k) {
            pos[k].x = tris[i][k].x; pos[k].y = tris[i][k].y;
            tco[k].tu = tris[i][k].tu; tco[k].tv = tris[i][k].tv;
            tco[k].q = tris[i][k].q;
        }
        memset(&tr, 0, sizeof(tr));
        tr.dst_cntl = 1ul;
        v9x_r2_setup_texture(pos, tco, tex, &tr, &st, &aff);
        if (aff) { ++affine; continue; }
        if (v9x_r2_texture_error(pos, tco, tex, &texels) != V9X_STATUS_OK)
            continue;
        b = bound_of(tris[i], tex->log2_width, tex->log2_height);
        if (b < 0.5) ++pass;
        if (texels > b * 1.0000001) ++fail_bound;
        if (b > 0 && texels / b > worst_ratio) worst_ratio = texels / b;
    }
    printf("  bound: %lu/%d under 0.5, %lu affine, %lu grid>bound, "
           "worst grid/bound %.3f\n", pass, tri_count, affine, fail_bound,
           worst_ratio);
}

static int bench_main(void);

#ifdef BENCH_GUI
#include <windows.h>
int PASCAL WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    freopen("C:\\V9XDIAG\\R2BENCH.TXT", "w", stdout);
    bench_main();
    fclose(stdout);
    return 0;
}
#else
int main(void)
{
    return bench_main();
}
#endif

static int bench_main(void)
{
    static struct v9x_r2_draw_vertex pieces[V9X_R2_DRAW_SPLIT_MAX * 3];
    static struct v9x_r2_texture_fit fits[V9X_R2_DRAW_SPLIT_MAX];
    static v9x_u32 offsets[V9X_R2_DRAW_TRAPS_MAX * V9X_R2_DRAW_TRAP_DWORDS];
    static v9x_u32 values[V9X_R2_DRAW_TRAPS_MAX * V9X_R2_DRAW_TRAP_DWORDS];
    static struct v9x_r2_flat_trap traps[V9X_R2_DRAW_TRAPS_MAX];
    struct v9x_m64_draw_request r;
    struct v9x_r2_draw_decision d;
    struct v9x_r2_draw_state s;
    double cells[3] = { 256.0, 128.0, 64.0 };
    int ci;

    memset(&r, 0, sizeof(r));
    r.target_format = 1ul; r.target_width = 640ul; r.target_height = 480ul;
    r.scissor_right = 640ul; r.scissor_bottom = 480ul; r.write_mask = 7ul;
    r.shade_mode = 2ul; r.textured = 1ul;
    r.texture_format = V9X_M64_TEXTURE_FORMAT_RGB565;
    r.texture_width = 64ul; r.texture_height = 64ul; r.texture_levels = 1ul;
    r.texture_min_filter = 2ul; r.texture_mag_filter = 2ul;
    r.texture_address = 1ul; r.texture_op = 2ul;
    r.depth_enable = 1ul; r.depth_bits = 16ul; r.depth_func = 4ul;
    r.depth_write = 1ul;
    if (v9x_r2_check_draw(&r, 0ul, &d) != V9X_M64_REFUSE_NONE) {
        printf("policy refused %lu\n", v9x_r2_check_draw(&r, 0ul, &d));
        return 1;
    }
    memset(&s, 0, sizeof(s));
    s.target.pitch_bytes = 1280ul; s.target.width = 640ul;
    s.target.height = 480ul; s.target.vram_bytes = 0x00400000ul;
    s.target.scissor_right = 639ul; s.target.scissor_bottom = 479ul;
    s.depth_enable = 1ul; s.depth_offset = 0x00200000ul;
    s.depth_pitch_bytes = 1280ul; s.depth_func = 4ul; s.depth_write = 1ul;
    s.textured = 1ul;
    s.texture.offset = 0x00300000ul; s.texture.log2_width = 6ul;
    s.texture.log2_height = 6ul; s.texture.log2_pitch = 6ul;
    s.texture.format = d.texture_format;

    for (ci = 0; ci < 3; ++ci) {
        unsigned long t_split = 0, t_build = 0, t_err = 0, t_tri = 0;
        unsigned long t_tex = 0, t_shade = 0;
        unsigned long n_pieces = 0, n_traps = 0, n_dwords = 0, n_err = 0;
        unsigned long n_fail = 0;
        int i;

        make_scene(cells[ci]);
        check_bound(&s.texture);
        for (i = 0; i < tri_count; ++i) {
            v9x_u32 count = 0, p;
            unsigned long t0 = rdtsc_low();
            v9x_status st = v9x_r2_split_triangle(&s, &d, tris[i], pieces, fits,
                                                  &count);
            t_split += rdtsc_low() - t0;
            if (st != V9X_STATUS_OK) { ++n_fail; continue; }
            n_pieces += count;
            for (p = 0; p < count; ++p) {
                v9x_u32 written = 0, tc = 0, stage = 0, k, tr;
                struct v9x_r2_vertex pos[3];
                struct v9x_r2_tex_coord tco[3];
                v9x_u32 colors[3];
                double texels;

                t0 = rdtsc_low();
                st = v9x_r2_build_piece(&s, &d, &pieces[p * 3], &fits[p], offsets,
                                        values,
                                        V9X_R2_DRAW_TRAPS_MAX *
                                            V9X_R2_DRAW_TRAP_DWORDS,
                                        &written, traps, &tc, &stage);
                t_build += rdtsc_low() - t0;
                if (st != V9X_STATUS_OK) { ++n_fail; continue; }
                n_traps += tc;
                n_dwords += written;
                for (k = 0; k < 3; ++k) {
                    pos[k].x = pieces[p * 3 + k].x;
                    pos[k].y = pieces[p * 3 + k].y;
                    tco[k].tu = pieces[p * 3 + k].tu;
                    tco[k].tv = pieces[p * 3 + k].tv;
                    tco[k].q = pieces[p * 3 + k].q;
                    colors[k] = pieces[p * 3 + k].argb & 0xfffffful;
                }
                /* the pieces of build_piece, timed alone */
                {
                    struct v9x_r2_flat_trap whole[2];
                    v9x_u32 wc;
                    t0 = rdtsc_low();
                    v9x_r2_setup_triangle(&s.target, pos, whole, &wc);
                    t_tri += rdtsc_low() - t0;
                }
                for (tr = 0; tr < tc; ++tr) {
                    struct v9x_r2_st stt;
                    struct v9x_r2_shade sh;
                    t0 = rdtsc_low();
                    v9x_r2_setup_texture(pos, tco, &s.texture, &traps[tr],
                                         &stt, 0);
                    t_tex += rdtsc_low() - t0;
                    t0 = rdtsc_low();
                    v9x_r2_setup_shade(pos, colors, &traps[tr], &sh);
                    v9x_r2_setup_shade(pos, colors, &traps[tr], &sh);
                    t_shade += rdtsc_low() - t0;
                }
                t0 = rdtsc_low();
                v9x_r2_texture_error(pos, tco, &s.texture, &texels);
                t_err += rdtsc_low() - t0;
                ++n_err;
            }
        }
        printf("cell %3.0f: %d tris, %lu pieces (%.2f/tri), %lu traps, "
               "%.1f dwords/tri, %lu fail\n",
               cells[ci], tri_count, n_pieces,
               (double)n_pieces / tri_count, n_traps,
               (double)n_dwords / tri_count, n_fail);
        printf("  per tri cycles: split %.0f  build %.0f  (total %.0f)\n",
               (double)t_split / tri_count, (double)t_build / tri_count,
               (double)(t_split + t_build) / tri_count);
        printf("  per piece: texture_error %.0f  setup_triangle %.0f\n",
               (double)t_err / n_err, (double)t_tri / n_err);
        printf("  per trap: setup_texture %.0f  2x setup_shade %.0f\n",
               (double)t_tex / n_traps, (double)t_shade / n_traps);
    }
    return 0;
}
