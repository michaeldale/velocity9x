/*
 * Tests for the CreateSurfaceEx handle table (src\display32\d3d\
 * d3d_handles.c): handles are per local object, a handle re-announced with
 * another surface forgets its user value, colliding handles probe past each
 * other and past freed slots, and the three drops free what they name.
 */
#include <stdio.h>
#include "../../src/display32/d3d/d3d_handles.h"

static unsigned int handle_failures;

#define HCHECK(e) do { if (!(e)) { ++handle_failures; \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
} } while (0)

static V9X_D3D_HANDLES table;

unsigned int v9x_run_d3d_handles_tests(void)
{
    int local_a;
    int local_b;
    int surface[4];
    V9X_D3D_SURFACE_HANDLE *e;

    handle_failures = 0u;
    v9x_d3d_handles_init(&table);

    /* Per local object: handle 1 of A and of B are different entries. */
    HCHECK(v9x_d3d_handles_set(&table, &local_a, 1ul, &surface[0]) != 0);
    HCHECK(v9x_d3d_handles_set(&table, &local_b, 1ul, &surface[1]) != 0);
    e = v9x_d3d_handles_find(&table, &local_a, 1ul);
    HCHECK(e != 0 && e->surface == &surface[0]);
    e = v9x_d3d_handles_find(&table, &local_b, 1ul);
    HCHECK(e != 0 && e->surface == &surface[1]);
    HCHECK(v9x_d3d_handles_find(&table, &local_a, 2ul) == 0);
    HCHECK(v9x_d3d_handles_set(&table, &local_a, 0ul, &surface[0]) == 0);

    /* The user value stays while the surface does. */
    e = v9x_d3d_handles_find(&table, &local_a, 1ul);
    e->user = 77ul;
    HCHECK(v9x_d3d_handles_set(&table, &local_a, 1ul, &surface[0])->user ==
           77ul);
    HCHECK(v9x_d3d_handles_set(&table, &local_a, 1ul, &surface[2])->user ==
           0ul);

    /* 1 + 1024 lands on handle 1's slot and probes on; freeing B's handle
     * 1 in between keeps it findable. */
    HCHECK(v9x_d3d_handles_set(&table, &local_a,
                               1ul + V9X_D3D_SURFACE_HANDLES,
                               &surface[3]) != 0);
    HCHECK(v9x_d3d_handles_drop(&table, &local_b, 1ul) == 1ul);
    e = v9x_d3d_handles_find(&table, &local_a, 1ul + V9X_D3D_SURFACE_HANDLES);
    HCHECK(e != 0 && e->surface == &surface[3]);
    HCHECK(v9x_d3d_handles_find(&table, &local_b, 1ul) == 0);

    /* By surface, then by local object. */
    HCHECK(v9x_d3d_handles_drop_surface(&table, &surface[3]) == 1ul);
    HCHECK(v9x_d3d_handles_set(&table, &local_b, 5ul, &surface[1]) != 0);
    HCHECK(v9x_d3d_handles_drop_local(&table, &local_a) == 1ul);
    HCHECK(v9x_d3d_handles_find(&table, &local_a, 1ul) == 0);
    HCHECK(v9x_d3d_handles_find(&table, &local_b, 5ul) != 0);

    if (handle_failures == 0u) {
        printf("PASS: CreateSurfaceEx handle table\n");
    }
    return handle_failures;
}
