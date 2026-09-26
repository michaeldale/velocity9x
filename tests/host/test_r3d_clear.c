#include <stdio.h>
#include "../../src/display32/r3d/r3d.h"

static unsigned int clear_failures;
#define QC(x) do { if (!(x)) { printf("FAIL %s:%u: %s\n", __FILE__, \
    (unsigned int)__LINE__, #x); ++clear_failures; } } while (0)

#define CW 4u
#define CH 3u
#define CP 6u
static v9x_u16 colors[CP*CH];
static v9x_u16 depths[CP*CH];

static void reset(V9X_R3D_CLEAR *c, V9X_R3D_CLEAR_RECT *r)
{
    unsigned int i;
    for(i=0u;i<CP*CH;++i){colors[i]=0x5aa5u;depths[i]=0x7777u;}
    r->left=1ul;r->top=1ul;r->right=3ul;r->bottom=3ul;
    c->color=colors;c->color_pitch=CP*2ul;c->depth=depths;c->depth_pitch=CP*2ul;
    c->width=CW;c->height=CH;c->format=V9X_R3D_FORMAT_RGB565;
    c->clear_color=1ul;c->clear_depth=1ul;c->color_value=0x00ff8000ul;
    c->depth_value=0x1234ul;c->write_red=1ul;c->write_green=1ul;
    c->write_blue=1ul;c->write_depth=1ul;c->rects=r;c->rect_count=1ul;
}

static void test_partial_color_and_depth(void)
{
    V9X_R3D_CLEAR c; V9X_R3D_CLEAR_RECT r; reset(&c,&r);
    QC(v9x_r3d_clear(&c)!=0);
    QC(colors[1u+CP]==0xfc00u && colors[2u+2u*CP]==0xfc00u);
    QC(depths[1u+CP]==0x1234u && depths[2u+2u*CP]==0x1234u);
    QC(colors[0]==0x5aa5u && depths[3u+CP]==0x7777u);
    QC(colors[4u+CP]==0x5aa5u && colors[5u+CP]==0x5aa5u);
}

static void test_masks_and_rect_list(void)
{
    V9X_R3D_CLEAR c; V9X_R3D_CLEAR_RECT r[2]; reset(&c,&r[0]);
    r[0].left=0ul;r[0].top=0ul;r[0].right=1ul;r[0].bottom=1ul;
    r[1].left=3ul;r[1].top=2ul;r[1].right=4ul;r[1].bottom=3ul;c.rect_count=2ul;
    c.write_green=0ul;c.write_blue=0ul;c.write_depth=0ul;
    QC(v9x_r3d_clear(&c)!=0);
    QC(colors[0]==(v9x_u16)((0x5aa5u&0x07ffu)|0xf800u));
    QC(colors[3u+2u*CP]==colors[0]); QC(colors[1]==0x5aa5u);
    QC(depths[0]==0x7777u && depths[3u+2u*CP]==0x7777u);

    reset(&c,&r[0]); c.format=V9X_R3D_FORMAT_XRGB1555;
    c.write_red=0ul;c.write_green=1ul;c.write_blue=0ul;c.clear_depth=0ul;
    colors[1u+CP]=0xdaa5u;
    QC(v9x_r3d_clear(&c)!=0);
    QC((colors[1u+CP]&0x8000u)!=0u);
    QC((colors[1u+CP]&0x03e0u)==0x0200u);
}

static void test_refuses_before_writing(void)
{
    V9X_R3D_CLEAR c; V9X_R3D_CLEAR_RECT r[2]; reset(&c,&r[0]);
    r[1]=r[0];r[1].right=5ul;c.rect_count=2ul;
    QC(v9x_r3d_clear(&c)==0); QC(colors[1u+CP]==0x5aa5u);
    reset(&c,&r[0]);c.color_pitch=6ul;
    QC(v9x_r3d_clear(&c)==0); QC(colors[1u+CP]==0x5aa5u);
    reset(&c,&r[0]);c.clear_color=0ul;c.color=0;c.write_depth=1ul;
    QC(v9x_r3d_clear(&c)!=0 && depths[1u+CP]==0x1234u);
    reset(&c,&r[0]);c.rect_count=0ul;c.rects=0;
    QC(v9x_r3d_clear(&c)!=0 && colors[1u+CP]==0x5aa5u);
}

unsigned int v9x_run_r3d_clear_tests(void)
{
    test_partial_color_and_depth();test_masks_and_rect_list();
    test_refuses_before_writing();return clear_failures;
}
