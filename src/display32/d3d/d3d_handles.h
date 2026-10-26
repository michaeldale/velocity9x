/*
 * CreateSurfaceEx handles (DDI 7, docs\plans\hardware-tnl.md step 5).
 *
 * A DirectX 7 runtime names textures (D3DTSS_TEXTUREMAP), render targets
 * (D3DDP2OP_SETRENDERTARGET) and TEXBLT surfaces by a DWORD handle it
 * assigns per DirectDraw local object and announces through
 * CreateSurfaceEx. This table maps (local object, handle) to the surface's
 * local half, and holds one value for its caller beside it: the DX6-style
 * texture record the DDI 6 code draws from, so a texture change does not
 * search the texture table.
 *
 * Pure: no DDHAL header and no HAL state, so scripts\build-host.ps1
 * compiles it and tests\host\test_d3d_handles.c holds it. d3d_core.c owns
 * the one table and applies the answers.
 *
 * Open addressing from handle mod V9X_D3D_SURFACE_HANDLES, a slot ever used
 * keeping the probe going after it is freed, so a lookup stops at the first
 * slot never used. The runtime numbers handles from 1 upward per local
 * object, so most land in their own slot.
 */
#ifndef VELOCITY9X_D3D_HANDLES_H
#define VELOCITY9X_D3D_HANDLES_H

#include "velocity9x/types.h"

#define V9X_D3D_SURFACE_HANDLES 512u

typedef struct v9x_d3d_surface_handle {
    v9x_u32 handle;         /* 0: free                               */
    v9x_u32 used;           /* ever held a handle: probing goes on    */
    const void *local;      /* the DirectDraw local object            */
    void *surface;          /* its DDRAWI_DDRAWSURFACE_LCL            */
    v9x_u32 user;           /* the caller's value; 0 when none        */
} V9X_D3D_SURFACE_HANDLE;

typedef struct v9x_d3d_handles {
    V9X_D3D_SURFACE_HANDLE entry[V9X_D3D_SURFACE_HANDLES];
} V9X_D3D_HANDLES;

void v9x_d3d_handles_init(V9X_D3D_HANDLES *table);

/* The entry for (local, handle), made if there is none; null when the
 * handle is 0 or the table is full. A new entry's user value is 0; an
 * existing one keeps its own unless its surface changes. */
V9X_D3D_SURFACE_HANDLE *v9x_d3d_handles_set(V9X_D3D_HANDLES *table,
                                            const void *local,
                                            v9x_u32 handle, void *surface);

/* The entry for (local, handle), or null. */
V9X_D3D_SURFACE_HANDLE *v9x_d3d_handles_find(V9X_D3D_HANDLES *table,
                                             const void *local,
                                             v9x_u32 handle);

/* Frees one entry, every entry of a local object, or every entry naming a
 * surface; each answers how many it freed. */
v9x_u32 v9x_d3d_handles_drop(V9X_D3D_HANDLES *table, const void *local,
                             v9x_u32 handle);
v9x_u32 v9x_d3d_handles_drop_local(V9X_D3D_HANDLES *table,
                                   const void *local);
v9x_u32 v9x_d3d_handles_drop_surface(V9X_D3D_HANDLES *table,
                                     const void *surface);

#endif /* VELOCITY9X_D3D_HANDLES_H */
