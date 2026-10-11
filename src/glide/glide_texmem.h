/*
 * The TMU address space a Glide game manages itself
 * (docs\plans\glide-2x-wrapper.md, Phase 1). The game picks every
 * download's start address inside grTexMinAddress..grTexMaxAddress and
 * sizes it with grTexCalcMemRequired; grTexSource names a texture by the
 * same address.
 *
 * The memory is bytes, as on the card (docs\plans\glide-tmu-byte-memory.md):
 * a download writes them and a source reads whatever is there in the
 * format it names. Carmageddon II downloads several textures to address 0
 * and sources earlier ones again without downloading them (netbook,
 * 2026-10-11); a model of whole textures had nothing to give those
 * sources and the menu stayed black.
 *
 * A source's decoded texture is cached under its key (start, even/odd,
 * LODs, aspect, format). Every 4 KiB page carries a generation that each
 * write to it raises, and an entry keeps the sum of its pages' generations
 * from when it was last read: generations only rise, so the sum changes
 * exactly when a byte of the span may have. A source then costs a lookup
 * and a few adds - NFS II SE calls grTexSource tens of thousands of times a
 * second (census) - and the bytes are re-read only when they were written.
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

/*
 * Cache entries: 2 MiB holds 1,024 of the 32x32 ARGB1555 textures NFS II
 * SE uses most (census), and a 512-entry table evicted live ones in its
 * demo race (netbook, 2026-10-08), so four times that. When full the least
 * recently sourced entry goes. grTexSource looks an entry up by start
 * address through a hash of buckets; a bucket chains entries by `next`, an
 * index plus one, zero ending the chain.
 */
#define V9X_GLIDE_TEXMEM_RECORDS 4096u
#define V9X_GLIDE_TEXMEM_BUCKETS 1024u

/* The memory, in pages: 4 MiB, the larger of the two DLLs' reports.
 * GLIDE2X.DLL's grSstQueryHardware says 2 MiB; GLIDE3X.DLL answers
 * GR_MEMORY_TMU with 4 MiB, and Diablo II places textures above 2 MiB
 * (A8U4I5, 2026-10-11). */
#define V9X_GLIDE_TEXMEM_PAGE_BYTES 4096ul
#define V9X_GLIDE_TEXMEM_PAGES      1024u

typedef struct v9x_glide_texrec {
    v9x_u32 in_use;
    v9x_u32 start;
    v9x_u32 end;            /* exclusive */
    v9x_u32 even_odd;
    V9X_GLIDE_TEXINFO info;
    v9x_u32 generations;    /* sum of the span's page generations when read */
    v9x_u32 last_used;      /* the source clock when last sourced */
    v9x_u32 next;
} V9X_GLIDE_TEXREC;

typedef struct v9x_glide_texmem {
    V9X_GLIDE_TEXREC records[V9X_GLIDE_TEXMEM_RECORDS];
    v9x_u32 buckets[V9X_GLIDE_TEXMEM_BUCKETS];
    v9x_u32 page_generation[V9X_GLIDE_TEXMEM_PAGES];
    v9x_u8 *bytes;          /* the caller's; `size` of them */
    v9x_u32 size;
    v9x_u32 clock;
} V9X_GLIDE_TEXMEM;

/* What a source found (v9x_glide_texmem_source). */
#define V9X_GLIDE_TEXMEM_CURRENT 0ul    /* cached, and its bytes unwritten */
#define V9X_GLIDE_TEXMEM_STALE   1ul    /* cached, but its bytes written since */
#define V9X_GLIDE_TEXMEM_NEW     2ul    /* a new entry: nothing cached */

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

/* Empty cache over `size` bytes of memory at `bytes`, which are zeroed.
 * `size` is a multiple of the page, at most V9X_GLIDE_TEXMEM_PAGES of them. */
void v9x_glide_texmem_init(V9X_GLIDE_TEXMEM *mem, v9x_u8 *bytes, v9x_u32 size);

/*
 * grTexDownloadMipMap: the levels even_odd names, from `data` (every level
 * of the chain end to end, largest first), at their offsets from `start`
 * (each selected level after the last, rounded to the granule), clipped to
 * the memory. The span written; 0 for invalid info or a start past the end.
 */
v9x_u32 v9x_glide_texmem_download(V9X_GLIDE_TEXMEM *mem, v9x_u32 start,
                                  v9x_u32 even_odd,
                                  const V9X_GLIDE_TEXINFO *info,
                                  const v9x_u8 *data);

/*
 * grTexSource: the cache entry for this key, made if there is none (the
 * least recently sourced goes when the table is full), and in *state
 * whether what is cached for it can stand (V9X_GLIDE_TEXMEM_*). The entry
 * is marked current: the caller re-reads the bytes for STALE and NEW.
 * -1 for invalid info or a span past the end of memory.
 */
int v9x_glide_texmem_source(V9X_GLIDE_TEXMEM *mem, v9x_u32 start,
                            v9x_u32 even_odd, const V9X_GLIDE_TEXINFO *info,
                            v9x_u32 *state);

#endif /* VELOCITY9X_GLIDE_TEXMEM_H */
