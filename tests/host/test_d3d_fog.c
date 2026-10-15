/*
 * Tests for Direct3D table fog as a per-vertex factor (d3d_fog.h).
 *
 * The factors are Direct3D's formulas (the SDK's "Fog Formulas") scaled to
 * 255 and rounded: linear (end - d) / (end - start), EXP e^-(d*density),
 * EXP2 e^-((d*density)^2), clamped to 0..1.
 */
#include <stdio.h>
#include <string.h>

#include "../../src/display32/d3d/d3d_fog.h"

static unsigned int fog_failures = 0u;

#define FCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++fog_failures; \
    } \
} while (0)

static v9x_u32 fog_bits(float value)
{
    v9x_u32 bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void fog_set(V9X_D3D_FOG_TABLE *table, v9x_u32 mode, float start,
                    float end, float density)
{
    table->mode = mode;
    table->start = fog_bits(start);
    table->end = fog_bits(end);
    table->density = fog_bits(density);
}

static void test_defaults_and_activity(void)
{
    V9X_D3D_FOG_TABLE table;

    v9x_d3d_fog_table_init(&table);
    FCHECK(table.mode == V9X_D3D_FOG_NONE);
    FCHECK(table.start == fog_bits(0.0f));
    FCHECK(table.end == fog_bits(1.0f));
    FCHECK(table.density == fog_bits(1.0f));
    FCHECK(!v9x_d3d_fog_table_active(1ul, &table));

    table.mode = V9X_D3D_FOG_LINEAR;
    FCHECK(v9x_d3d_fog_table_active(1ul, &table));
    FCHECK(!v9x_d3d_fog_table_active(0ul, &table));
    table.mode = V9X_D3D_FOG_EXP;
    FCHECK(v9x_d3d_fog_table_active(1ul, &table));
    table.mode = V9X_D3D_FOG_EXP2;
    FCHECK(v9x_d3d_fog_table_active(1ul, &table));
    /* An unknown mode is NONE, not a fog the core cannot compute. */
    table.mode = 4ul;
    FCHECK(!v9x_d3d_fog_table_active(1ul, &table));
    FCHECK(v9x_d3d_fog_table_factor(&table, 0.5f) == 255ul);
}

static void test_linear(void)
{
    V9X_D3D_FOG_TABLE table;

    fog_set(&table, V9X_D3D_FOG_LINEAR, 0.25f, 0.75f, 1.0f);
    FCHECK(v9x_d3d_fog_table_factor(&table, 0.25f) == 255ul);
    FCHECK(v9x_d3d_fog_table_factor(&table, 0.75f) == 0ul);
    /* Midpoint: 127.5 rounds either way; the factor is one of the two. */
    FCHECK(v9x_d3d_fog_table_factor(&table, 0.5f) == 127ul ||
           v9x_d3d_fog_table_factor(&table, 0.5f) == 128ul);
    FCHECK(v9x_d3d_fog_table_factor(&table, 0.375f) == 191ul);
    /* Clamped outside the range. */
    FCHECK(v9x_d3d_fog_table_factor(&table, 0.0f) == 255ul);
    FCHECK(v9x_d3d_fog_table_factor(&table, -1.0f) == 255ul);
    FCHECK(v9x_d3d_fog_table_factor(&table, 1.0f) == 0ul);

    /* start == end has no range: unfogged, not a division by zero. */
    fog_set(&table, V9X_D3D_FOG_LINEAR, 0.5f, 0.5f, 1.0f);
    FCHECK(v9x_d3d_fog_table_factor(&table, 0.5f) == 255ul);
    FCHECK(v9x_d3d_fog_table_factor(&table, 0.9f) == 255ul);

    /* Eye units: start 100, end 500. */
    fog_set(&table, V9X_D3D_FOG_LINEAR, 100.0f, 500.0f, 1.0f);
    FCHECK(v9x_d3d_fog_table_factor(&table, 300.0f) == 127ul ||
           v9x_d3d_fog_table_factor(&table, 300.0f) == 128ul);
    FCHECK(v9x_d3d_fog_table_factor(&table, 400.0f) == 64ul);
}

static void test_exponential(void)
{
    V9X_D3D_FOG_TABLE table;

    /* Density 0 is no fog at any depth. */
    fog_set(&table, V9X_D3D_FOG_EXP, 0.0f, 1.0f, 0.0f);
    FCHECK(v9x_d3d_fog_table_factor(&table, 0.9f) == 255ul);
    fog_set(&table, V9X_D3D_FOG_EXP2, 0.0f, 1.0f, 0.0f);
    FCHECK(v9x_d3d_fog_table_factor(&table, 0.9f) == 255ul);

    /* EXP, density 2, depth 0.5: e^-1 = 0.36788, * 255 = 93.81. */
    fog_set(&table, V9X_D3D_FOG_EXP, 0.0f, 1.0f, 2.0f);
    FCHECK(v9x_d3d_fog_table_factor(&table, 0.5f) == 94ul);
    FCHECK(v9x_d3d_fog_table_factor(&table, 0.0f) == 255ul);
    /* e^-2 = 0.13534, * 255 = 34.51. */
    FCHECK(v9x_d3d_fog_table_factor(&table, 1.0f) == 35ul);

    /* EXP2, density 2, depth 0.5: e^-1 again; depth 1: e^-4 = 4.67. */
    fog_set(&table, V9X_D3D_FOG_EXP2, 0.0f, 1.0f, 2.0f);
    FCHECK(v9x_d3d_fog_table_factor(&table, 0.5f) == 94ul);
    FCHECK(v9x_d3d_fog_table_factor(&table, 1.0f) == 5ul);
    /* Far past the curve: zero, not an underflow. */
    FCHECK(v9x_d3d_fog_table_factor(&table, 100.0f) == 0ul);
}

static void test_depth_rule(void)
{
    V9X_D3D_FOG_TABLE table;

    /* A device-space linear range takes Z. */
    fog_set(&table, V9X_D3D_FOG_LINEAR, 0.0f, 1.0f, 1.0f);
    FCHECK(v9x_d3d_fog_table_depth(&table, 0.5f, 0.01f) == 0.5f);
    /* A range past 1.0 is eye units: W = 1/rhw. */
    fog_set(&table, V9X_D3D_FOG_LINEAR, 100.0f, 500.0f, 1.0f);
    FCHECK(v9x_d3d_fog_table_depth(&table, 0.5f, 0.01f) == 100.0f);
    /* Without a usable rhw, Z regardless. */
    FCHECK(v9x_d3d_fog_table_depth(&table, 0.5f, 0.0f) == 0.5f);
    FCHECK(v9x_d3d_fog_table_depth(&table, 0.5f, -1.0f) == 0.5f);
    /* EXP and EXP2 at a device-space density are Z. */
    fog_set(&table, V9X_D3D_FOG_EXP, 100.0f, 500.0f, 1.0f);
    FCHECK(v9x_d3d_fog_table_depth(&table, 0.5f, 0.01f) == 0.5f);
    fog_set(&table, V9X_D3D_FOG_EXP2, 0.0f, 1.0f, 4.0f);
    FCHECK(v9x_d3d_fog_table_depth(&table, 0.5f, 0.01f) == 0.5f);
    /* Thief II's density, 0.0107 (A8U4I5 boot 479): below 1.0 it cannot
     * fog device Z visibly, so it is eye units and the depth is W. */
    fog_set(&table, V9X_D3D_FOG_EXP, 0.0f, 1.0f, 0.0107f);
    FCHECK(v9x_d3d_fog_table_depth(&table, 0.5f, 0.01f) == 100.0f);
    FCHECK(v9x_d3d_fog_table_depth(&table, 0.5f, 0.0f) == 0.5f);
    fog_set(&table, V9X_D3D_FOG_EXP2, 0.0f, 1.0f, 0.0107f);
    FCHECK(v9x_d3d_fog_table_depth(&table, 0.5f, 0.01f) == 100.0f);
    /* Density 0 fogs nothing at any depth; Z is as good as W. */
    fog_set(&table, V9X_D3D_FOG_EXP, 0.0f, 1.0f, 0.0f);
    FCHECK(v9x_d3d_fog_table_factor(&table,
               v9x_d3d_fog_table_depth(&table, 0.5f, 0.01f)) == 255ul);
}

static void test_apply(void)
{
    V9X_D3D_FOG_TABLE table;
    V9X_R3D_VERTEX vertices[3];

    memset(vertices, 0, sizeof(vertices));
    vertices[0].sz = 0.25f;
    vertices[0].rhw = 1.0f;
    vertices[0].specular = 0x00123456ul;
    vertices[1].sz = 0.75f;
    vertices[1].rhw = 1.0f;
    vertices[1].specular = 0xff000000ul;
    vertices[2].sz = 0.375f;
    vertices[2].rhw = 1.0f;
    vertices[2].specular = 0x80abcdeful;

    fog_set(&table, V9X_D3D_FOG_LINEAR, 0.25f, 0.75f, 1.0f);
    v9x_d3d_fog_table_apply(&table, vertices, 3ul);
    /* The alpha is the table's; the vertex's own fog and RGB are kept. */
    FCHECK(vertices[0].specular == 0xff123456ul);
    FCHECK(vertices[1].specular == 0x00000000ul);
    FCHECK(vertices[2].specular == 0xbfabcdeful);
    FCHECK(vertices[0].sz == 0.25f);
}

unsigned int v9x_run_d3d_fog_tests(void)
{
    fog_failures = 0u;
    test_defaults_and_activity();
    test_linear();
    test_exponential();
    test_depth_rule();
    test_apply();
    if (fog_failures == 0u) {
        puts("PASS: Direct3D table fog per vertex");
    }
    return fog_failures;
}
