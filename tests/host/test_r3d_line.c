#include <stdio.h>
#include "../../src/display32/r3d/r3d.h"

static unsigned int line_failures;
#define LCHECK(x) do { if (!(x)) { printf("FAIL %s:%u: %s\n", __FILE__, \
    (unsigned int)__LINE__, #x); ++line_failures; } } while (0)

typedef struct line_sink {
    v9x_s32 x[32], y[32];
    V9X_R3D_VERTEX v[32];
    unsigned int count;
    unsigned int refuse_at;
} LINE_SINK;

static V9X_R3D_VERTEX line_vertex(float x, float y, v9x_u32 color)
{
    V9X_R3D_VERTEX v;
    v.sx=x; v.sy=y; v.sz=0.25f; v.rhw=1.0f; v.color=color;
    v.specular=0ul; v.tu=x/16.0f; v.tv=y/16.0f;
    return v;
}

static int line_fragment(void *user, v9x_s32 x, v9x_s32 y,
                         const V9X_R3D_VERTEX *v)
{
    LINE_SINK *s=(LINE_SINK *)user;
    if (s->count == s->refuse_at) return 0;
    if (s->count >= 32u) return 0;
    s->x[s->count]=x; s->y[s->count]=y; s->v[s->count]=*v; ++s->count;
    return 1;
}

static void sink_reset(LINE_SINK *s)
{
    s->count=0u; s->refuse_at=999u;
}

static void test_points(void)
{
    LINE_SINK s; V9X_R3D_VERTEX v=line_vertex(3.0f,2.0f,0xff123456ul);
    union { v9x_u32 bits; float value; } nan_value;
    sink_reset(&s);
    LCHECK(v9x_r3d_draw_point(&v,8ul,6ul,line_fragment,&s)==1);
    LCHECK(s.count==1u && s.x[0]==3l && s.y[0]==2l);
    LCHECK(s.v[0].sx==3.5f && s.v[0].sy==2.5f);
    v.sx=-0.01f;
    LCHECK(v9x_r3d_draw_point(&v,8ul,6ul,line_fragment,&s)==0);
    nan_value.bits=0x7fc00000ul; v.sx=nan_value.value;
    LCHECK(v9x_r3d_draw_point(&v,8ul,6ul,line_fragment,&s)==-1);
}

static void test_axes_diagonal_and_endpoint(void)
{
    LINE_SINK s; V9X_R3D_VERTEX a,b;
    sink_reset(&s); a=line_vertex(0.2f,2.2f,0xff000000ul);
    b=line_vertex(5.2f,2.2f,0xfffffffful);
    LCHECK(v9x_r3d_draw_line(&a,&b,12ul,8ul,line_fragment,&s)==5);
    LCHECK(s.x[0]==0l && s.x[4]==4l && s.y[2]==2l);
    LCHECK((s.v[0].color & 0xfful) < (s.v[4].color & 0xfful));

    sink_reset(&s); a=line_vertex(4.2f,0.2f,0ul); b=line_vertex(4.2f,5.2f,0ul);
    LCHECK(v9x_r3d_draw_line(&a,&b,12ul,8ul,line_fragment,&s)==5);
    LCHECK(s.y[0]==0l && s.y[4]==4l && s.x[3]==4l);

    sink_reset(&s); a=line_vertex(0.2f,0.2f,0ul); b=line_vertex(5.2f,5.2f,0ul);
    LCHECK(v9x_r3d_draw_line(&a,&b,12ul,8ul,line_fragment,&s)==5);
    LCHECK(s.x[3]==3l && s.y[3]==3l);
}

static void test_connected_and_clipped(void)
{
    LINE_SINK a_sink,b_sink; V9X_R3D_VERTEX a,b,c;
    a=line_vertex(0.2f,1.2f,0ul); b=line_vertex(5.2f,1.2f,0ul);
    c=line_vertex(10.2f,1.2f,0ul);
    sink_reset(&a_sink); sink_reset(&b_sink);
    LCHECK(v9x_r3d_draw_line(&a,&b,12ul,4ul,line_fragment,&a_sink)==5);
    LCHECK(v9x_r3d_draw_line(&b,&c,12ul,4ul,line_fragment,&b_sink)==5);
    LCHECK(a_sink.x[4]==4l && b_sink.x[0]==5l);

    sink_reset(&a_sink); a=line_vertex(-2.2f,2.2f,0ul); b=line_vertex(3.2f,2.2f,0ul);
    LCHECK(v9x_r3d_draw_line(&a,&b,8ul,6ul,line_fragment,&a_sink)==3);
    LCHECK(a_sink.x[0]==0l && a_sink.x[2]==2l);
    sink_reset(&a_sink); a=line_vertex(-2.2f,3.2f,0ul); b=line_vertex(12.2f,3.2f,0ul);
    LCHECK(v9x_r3d_draw_line(&a,&b,8ul,6ul,line_fragment,&a_sink)==8);
    LCHECK(a_sink.x[0]==0l && a_sink.x[7]==7l);

    sink_reset(&a_sink); a_sink.refuse_at=2u;
    LCHECK(v9x_r3d_draw_line(&a,&b,8ul,6ul,line_fragment,&a_sink)==-1);
    LCHECK(a_sink.count==2u);
}

unsigned int v9x_run_r3d_line_tests(void)
{
    test_points(); test_axes_diagonal_and_endpoint(); test_connected_and_clipped();
    return line_failures;
}
