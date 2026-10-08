/*
 * The TMU address space a Glide game manages itself
 * (docs\plans\glide-2x-wrapper.md, Phase 1). The game picks every
 * download's start address inside grTexMinAddress..grTexMaxAddress and
 * sizes it with grTexCalcMemRequired; grTexSource names a texture by the
 * same address. Here a download becomes a record; one that overlaps
 * earlier records replaces them, as new bytes over old memory would; and a
 * source is matched to its record. Each record carries a serial so the
 * DLL's hardware copy can tell a re-download from the texture it holds.
 *
 * Pure: no Windows headers.
 */
#ifndef VELOCITY9X_GLIDE_TEXMEM_H
#define VELOCITY9X_GLIDE_TEXMEM_H

#include "velocity9x/types.h"
#include "glide_api.h"

/*
 * A level's bytes are rounded up to this. The TMU addresses texture
 * memory in 8-byte units; whether retail Glide pads small levels more is
 * unknown. It does not matter for correctness, since the game places
 * textures by this module's own grTexCalcMemRequired: NFS II SE's 256x256
 * ARGB4444 downloads landed exactly 0x20000 apart (census).
 */
#define V9X_GLIDE_TEX_GRANULE 8ul

/* GrTexInfo less its data pointer. */
typedef struct v9x_glide_texinfo {
    v9x_u32 small_lod;
    v9x_u32 large_lod;
    v9x_u32 aspect;
    v9x_u32 format;
} V9X_GLIDE_TEXINFO;

/* Enough for 2 MiB of the smallest textures NFS II SE downloaded (2x2,
 * census) eight times over; when full the oldest record goes. */
#define V9X_GLIDE_TEXMEM_RECORDS 512u

typedef struct v9x_glide_texrec {
    v9x_u32 in_use;
    v9x_u32 start;
    v9x_u32 end;            /* exclusive */
    v9x_u32 even_odd;
    V9X_GLIDE_TEXINFO info;
    v9x_u32 serial;
} V9X_GLIDE_TEXREC;

typedef struct v9x_glide_texmem {
    V9X_GLIDE_TEXREC records[V9X_GLIDE_TEXMEM_RECORDS];
    v9x_u32 next_serial;
} V9X_GLIDE_TEXMEM;

/* A level's dimensions; V9X_FALSE for an LOD or aspect out of range. */
v9x_u16 v9x_glide_level_size(v9x_u32 lod, v9x_u32 aspect, v9x_u32 *width,
                             v9x_u32 *height);

/* A level's bytes in TMU memory, rounded to the granule; 0 if invalid. */
v9x_u32 v9x_glide_level_bytes(v9x_u32 lod, v9x_u32 aspect, v9x_u32 format);

/* The bytes a chain occupies, counting only the LODs even_odd names
 * (grTexTextureMemRequired; grTexCalcMemRequired is the BOTH case). The
 * LOD order is taken from the values. 0 if the info is invalid. */
v9x_u32 v9x_glide_texmem_required(const V9X_GLIDE_TEXINFO *info,
                                  v9x_u32 even_odd);

void v9x_glide_texmem_init(V9X_GLIDE_TEXMEM *mem);

/* Records a download; the record's index, or -1 for invalid info. */
int v9x_glide_texmem_download(V9X_GLIDE_TEXMEM *mem, v9x_u32 start,
                              v9x_u32 even_odd, const V9X_GLIDE_TEXINFO *info);

/* The record grTexSource names: same start, LODs, aspect, format and
 * even_odd, or -1 when no download matches. */
int v9x_glide_texmem_find(const V9X_GLIDE_TEXMEM *mem, v9x_u32 start,
                          v9x_u32 even_odd, const V9X_GLIDE_TEXINFO *info);

#endif /* VELOCITY9X_GLIDE_TEXMEM_H */
