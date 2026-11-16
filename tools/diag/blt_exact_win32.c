#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

/*
 * V9XBLTX.EXE: DirectDraw fills and copies between video-memory surfaces,
 * each checked byte for byte against a model kept in system memory.
 *
 * V9XDDP and dispbench's DBDD sample pixels inside the destination
 * rectangle. A blit engine that writes a few bytes past a row's end, or a
 * whole row too many, passes them. This program compares every byte of
 * the destination surface after every blit, row padding included, and
 * counts the wrong bytes inside the rectangle and outside it separately:
 * a wrong byte outside is an overrun.
 *
 * It runs at the desktop's depth in a normal (windowed) cooperative level,
 * so the depth is chosen by setting the desktop first (V9XMSW). Which
 * blits the driver gave its engine is not visible from here: run
 * V9XTRACE before and after and subtract CountBltEngine.
 *
 * Written for Phase 3 of docs\plans\ati-oland-gfx6.md (SI's DMA engine
 * copies by byte count, one packet a row, and fills only whole dwords),
 * but nothing in it is Oland's. Writes C:\V9XDIAG\V9XBLTX.INI.
 *
 * Arguments: /random:<n> random operations after the fixed cases
 * (default 300); /selftest:1 first corrupts one byte by CPU and checks the
 * comparison reports it ([SelfTest] Result=DETECTED).
 */
#define BX_INI          "C:\\V9XDIAG\\V9XBLTX.INI"
#define BX_WIDTH        512ul
#define BX_HEIGHT       256ul
#define BX_RANDOM       300ul

#define DDSD_CAPS 0x00000001ul
#define DDSD_HEIGHT 0x00000002ul
#define DDSD_WIDTH 0x00000004ul
#define DDSCAPS_OFFSCREENPLAIN 0x00000040ul
#define DDSCAPS_VIDEOMEMORY 0x00004000ul
#define DDSCL_NORMAL 0x00000008ul
#define DDLOCK_WAIT 0x00000001ul
#define DDBLT_COLORFILL 0x00000400ul
#define DDBLT_WAIT 0x01000000ul

typedef struct bx_pixel_format {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    DWORD dwRGBBitCount;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwRGBAlphaBitMask;
} BX_PIXEL_FORMAT;

typedef struct bx_surface_desc {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    LONG lPitch;
    DWORD dwBackBufferCount;
    DWORD dwMipMapCount;
    DWORD dwAlphaBitDepth;
    DWORD dwReserved;
    void *lpSurface;
    DWORD colorkeys[8];
    BX_PIXEL_FORMAT ddpfPixelFormat;
    DWORD ddsCaps;
} BX_SURFACE_DESC;

typedef char bx_surface_desc_is_108[sizeof(BX_SURFACE_DESC) == 108 ? 1 : -1];

/* DDBLTFX: dwFillColor is the 21st dword; the structure is 100 bytes. */
#define BX_BLTFX_DWORDS      25
#define BX_BLTFX_FILL_COLOR  20

typedef struct bx_object { void **vtbl; } BX_OBJECT;

#define DD_RELEASE 2
#define DD_CREATE_SURFACE 6
#define DD_SET_COOPERATIVE_LEVEL 20
#define DDS_BLT 5
#define DDS_LOCK 25
#define DDS_UNLOCK 32

typedef HRESULT (WINAPI *bx_create_fn)(void *, BX_OBJECT **, void *);
typedef ULONG (WINAPI *bx_release_fn)(BX_OBJECT *);
typedef HRESULT (WINAPI *bx_create_surface_fn)(BX_OBJECT *,
    BX_SURFACE_DESC *, BX_OBJECT **, void *);
typedef HRESULT (WINAPI *bx_coop_fn)(BX_OBJECT *, HWND, DWORD);
typedef HRESULT (WINAPI *bx_blt_fn)(BX_OBJECT *, RECT *, BX_OBJECT *,
                                    RECT *, DWORD, DWORD *);
typedef HRESULT (WINAPI *bx_lock_fn)(BX_OBJECT *, RECT *, BX_SURFACE_DESC *,
                                     DWORD, HANDLE);
typedef HRESULT (WINAPI *bx_unlock_fn)(BX_OBJECT *, void *);

/* One video-memory surface and its model. */
typedef struct bx_surface {
    BX_OBJECT *object;
    BYTE *model;
    DWORD pitch;
    DWORD bytes;
    DWORD address;
} BX_SURFACE;

#define BX_OP_FILL       0
#define BX_OP_COPY       1     /* source surface to destination */
#define BX_OP_SELF       2     /* destination to itself */

static BX_SURFACE bx_source;
static BX_SURFACE bx_destination;
static DWORD bx_bpp;
static DWORD bx_seed = 0x2545f491ul;
static DWORD bx_case;
static DWORD bx_cases_bad;
static DWORD bx_blt_failed;
static DWORD bx_bytes_inside_bad;
static DWORD bx_bytes_outside_bad;
static DWORD bx_reported;
static BYTE *bx_row;

/* ---- text ------------------------------------------------------------- */

static void bx_hex(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < 8; ++index) {
        text[index] = digits[(value >> ((7 - index) * 4)) & 15u];
    }
    text[8] = '\0';
}

static char *bx_decimal(char *text, DWORD value)
{
    char reversed[12];
    int count = 0;

    do {
        reversed[count++] = (char)('0' + value % 10ul);
        value /= 10ul;
    } while (value != 0ul);
    while (count > 0) {
        *text++ = reversed[--count];
    }
    *text = '\0';
    return text;
}

static char *bx_append(char *text, const char *tail)
{
    while (*tail != '\0') {
        *text++ = *tail++;
    }
    *text = '\0';
    return text;
}

static void bx_key_hex(const char *section, const char *key, DWORD value)
{
    char text[12];

    bx_hex(text, value);
    WritePrivateProfileStringA(section, key, text, BX_INI);
}

static void bx_key_dec(const char *section, const char *key, DWORD value)
{
    char text[12];

    bx_decimal(text, value);
    WritePrivateProfileStringA(section, key, text, BX_INI);
}

/* ---- surfaces --------------------------------------------------------- */

static DWORD bx_random(void)
{
    /* Numerical Recipes' LCG; the high bits are the better ones. */
    bx_seed = bx_seed * 1664525ul + 1013904223ul;
    return bx_seed;
}

static void bx_copy_bytes(BYTE *to, const BYTE *from, DWORD count)
{
    while (count-- != 0ul) {
        *to++ = *from++;
    }
}

static BYTE *bx_lock(BX_SURFACE *surface)
{
    BX_SURFACE_DESC desc;
    BYTE *bytes = (BYTE *)&desc;
    DWORD index;

    for (index = 0ul; index < sizeof(desc); ++index) {
        bytes[index] = 0u;
    }
    desc.dwSize = sizeof(desc);
    if (((bx_lock_fn)surface->object->vtbl[DDS_LOCK])(
            surface->object, 0, &desc, DDLOCK_WAIT, 0) != 0) {
        return 0;
    }
    return (BYTE *)desc.lpSurface;
}

static void bx_unlock(BX_SURFACE *surface)
{
    ((bx_unlock_fn)surface->object->vtbl[DDS_UNLOCK])(surface->object, 0);
}

static int bx_create(BX_OBJECT *ddraw, BX_SURFACE *surface)
{
    BX_SURFACE_DESC desc;
    BYTE *bytes = (BYTE *)&desc;
    BYTE *pixels;
    DWORD index;

    for (index = 0ul; index < sizeof(desc); ++index) {
        bytes[index] = 0u;
    }
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = BX_WIDTH;
    desc.dwHeight = BX_HEIGHT;
    desc.ddsCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    if (((bx_create_surface_fn)ddraw->vtbl[DD_CREATE_SURFACE])(
            ddraw, &desc, &surface->object, 0) != 0 ||
        surface->object == 0) {
        return 0;
    }

    for (index = 0ul; index < sizeof(desc); ++index) {
        bytes[index] = 0u;
    }
    desc.dwSize = sizeof(desc);
    if (((bx_lock_fn)surface->object->vtbl[DDS_LOCK])(
            surface->object, 0, &desc, DDLOCK_WAIT, 0) != 0 ||
        desc.lpSurface == 0) {
        return 0;
    }
    surface->pitch = (DWORD)desc.lPitch;
    surface->bytes = surface->pitch * BX_HEIGHT;
    surface->address = (DWORD)desc.lpSurface;
    bx_bpp = desc.ddpfPixelFormat.dwRGBBitCount;
    surface->model = (BYTE *)VirtualAlloc(0, surface->bytes, MEM_COMMIT,
                                          PAGE_READWRITE);
    if (surface->model == 0) {
        bx_unlock(surface);
        return 0;
    }

    /* Every byte, padding included, starts as noise the model also holds. */
    pixels = (BYTE *)desc.lpSurface;
    for (index = 0ul; index < surface->bytes; ++index) {
        BYTE value = (BYTE)(bx_random() >> 24);

        pixels[index] = value;
        surface->model[index] = value;
    }
    bx_unlock(surface);
    return 1;
}

/* ---- the model -------------------------------------------------------- */

static void bx_model_fill(BX_SURFACE *surface, const RECT *rect, DWORD colour)
{
    DWORD bytes_per_pixel = bx_bpp / 8ul;
    LONG x;
    LONG y;

    for (y = rect->top; y < rect->bottom; ++y) {
        BYTE *row = surface->model + (DWORD)y * surface->pitch;

        for (x = rect->left; x < rect->right; ++x) {
            BYTE *pixel = row + (DWORD)x * bytes_per_pixel;
            DWORD byte;

            for (byte = 0ul; byte < bytes_per_pixel; ++byte) {
                pixel[byte] = (BYTE)(colour >> (byte * 8ul));
            }
        }
    }
}

/* Copies through a whole-rectangle scratch buffer, so an overlapping
 * same-surface copy is modelled as DirectDraw defines it: as if the source
 * were read entirely before the destination is written. */
static void bx_model_copy(BX_SURFACE *to, const RECT *to_rect,
                          const BX_SURFACE *from, const RECT *from_rect)
{
    DWORD bytes_per_pixel = bx_bpp / 8ul;
    DWORD row_bytes = (DWORD)(from_rect->right - from_rect->left) *
                      bytes_per_pixel;
    DWORD rows = (DWORD)(from_rect->bottom - from_rect->top);
    DWORD row;

    for (row = 0ul; row < rows; ++row) {
        bx_copy_bytes(bx_row + row * row_bytes,
                      from->model + ((DWORD)from_rect->top + row) * from->pitch +
                          (DWORD)from_rect->left * bytes_per_pixel,
                      row_bytes);
    }
    for (row = 0ul; row < rows; ++row) {
        bx_copy_bytes(to->model + ((DWORD)to_rect->top + row) * to->pitch +
                          (DWORD)to_rect->left * bytes_per_pixel,
                      bx_row + row * row_bytes, row_bytes);
    }
}

/* ---- one case --------------------------------------------------------- */

static int bx_inside(const RECT *rect, DWORD offset, DWORD pitch)
{
    DWORD y = offset / pitch;
    DWORD x = (offset % pitch) / (bx_bpp / 8ul);

    return (LONG)y >= rect->top && (LONG)y < rect->bottom &&
           (LONG)x >= rect->left && (LONG)x < rect->right;
}

static const char *bx_op_name(int op)
{
    if (op == BX_OP_FILL) {
        return "fill";
    }
    return (op == BX_OP_COPY) ? "copy" : "self";
}

/* Lock the destination, compare every byte with the model, then take the
 * surface's real contents as the model so a failure does not carry into
 * the cases after it. */
static void bx_check(int op, const RECT *to_rect, const RECT *from_rect,
                     DWORD colour, HRESULT hr)
{
    BYTE *pixels;
    DWORD inside = 0ul;
    DWORD outside = 0ul;
    DWORD first = 0xfffffffful;
    DWORD first_outside = 0xfffffffful;
    DWORD index;

    ++bx_case;
    if (hr != 0) {
        ++bx_blt_failed;
    }
    pixels = bx_lock(&bx_destination);
    if (pixels == 0) {
        ++bx_cases_bad;
        return;
    }
    for (index = 0ul; index < bx_destination.bytes; ++index) {
        if (pixels[index] != bx_destination.model[index]) {
            if (first == 0xfffffffful) {
                first = index;
            }
            if (bx_inside(to_rect, index, bx_destination.pitch)) {
                ++inside;
            } else {
                if (first_outside == 0xfffffffful) {
                    first_outside = index;
                }
                ++outside;
            }
            bx_destination.model[index] = pixels[index];
        }
    }
    bx_unlock(&bx_destination);

    if (inside == 0ul && outside == 0ul && hr == 0) {
        return;
    }
    ++bx_cases_bad;
    bx_bytes_inside_bad += inside;
    bx_bytes_outside_bad += outside;

    /* The first 40 bad cases in full; the totals cover the rest. */
    if (bx_reported < 40ul) {
        char section[16];
        char text[96];
        char *end;

        ++bx_reported;
        end = bx_append(section, "Bad");
        bx_decimal(end, bx_case);
        WritePrivateProfileStringA(section, "Op", bx_op_name(op), BX_INI);
        end = bx_decimal(text, (DWORD)to_rect->left);
        end = bx_append(end, ",");
        end = bx_decimal(end, (DWORD)to_rect->top);
        end = bx_append(end, " ");
        end = bx_decimal(end, (DWORD)(to_rect->right - to_rect->left));
        end = bx_append(end, "x");
        bx_decimal(end, (DWORD)(to_rect->bottom - to_rect->top));
        WritePrivateProfileStringA(section, "Dest", text, BX_INI);
        if (from_rect != 0) {
            end = bx_decimal(text, (DWORD)from_rect->left);
            end = bx_append(end, ",");
            bx_decimal(end, (DWORD)from_rect->top);
            WritePrivateProfileStringA(section, "Source", text, BX_INI);
        } else {
            bx_key_hex(section, "Colour", colour);
        }
        bx_key_hex(section, "Hr", (DWORD)hr);
        bx_key_dec(section, "BytesInside", inside);
        bx_key_dec(section, "BytesOutside", outside);
        if (first != 0xfffffffful) {
            bx_key_dec(section, "FirstY", first / bx_destination.pitch);
            bx_key_dec(section, "FirstByteInRow", first % bx_destination.pitch);
        }
        if (first_outside != 0xfffffffful) {
            bx_key_dec(section, "FirstOutsideY",
                       first_outside / bx_destination.pitch);
            bx_key_dec(section, "FirstOutsideByteInRow",
                       first_outside % bx_destination.pitch);
        }
    }
}

static void bx_fill(LONG x, LONG y, LONG w, LONG h, DWORD colour)
{
    DWORD fx[BX_BLTFX_DWORDS];
    RECT rect;
    HRESULT hr;
    int index;

    for (index = 0; index < BX_BLTFX_DWORDS; ++index) {
        fx[index] = 0ul;
    }
    fx[0] = sizeof(fx);
    fx[BX_BLTFX_FILL_COLOR] = colour;
    rect.left = x;
    rect.top = y;
    rect.right = x + w;
    rect.bottom = y + h;
    hr = ((bx_blt_fn)bx_destination.object->vtbl[DDS_BLT])(
        bx_destination.object, &rect, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, fx);
    if (hr == 0) {
        bx_model_fill(&bx_destination, &rect, colour);
    }
    bx_check(BX_OP_FILL, &rect, 0, colour, hr);
}

static void bx_copy(int op, LONG sx, LONG sy, LONG dx, LONG dy, LONG w, LONG h)
{
    BX_SURFACE *from = (op == BX_OP_SELF) ? &bx_destination : &bx_source;
    RECT to_rect;
    RECT from_rect;
    HRESULT hr;

    to_rect.left = dx;
    to_rect.top = dy;
    to_rect.right = dx + w;
    to_rect.bottom = dy + h;
    from_rect.left = sx;
    from_rect.top = sy;
    from_rect.right = sx + w;
    from_rect.bottom = sy + h;
    hr = ((bx_blt_fn)bx_destination.object->vtbl[DDS_BLT])(
        bx_destination.object, &to_rect, from->object, &from_rect,
        DDBLT_WAIT, 0);
    if (hr == 0) {
        bx_model_copy(&bx_destination, &to_rect, from, &from_rect);
    }
    bx_check(op, &to_rect, &from_rect, 0ul, hr);
}

/* A colour the depth holds whole: no alpha byte at 32 bpp, which the
 * driver need not keep. */
static DWORD bx_colour(DWORD value)
{
    return (bx_bpp == 16ul) ? (value & 0x0000fffful) : (value & 0x00fffffful);
}

static LONG bx_below(DWORD limit)
{
    return (LONG)((bx_random() >> 8) % limit);
}

/* ---- cases ------------------------------------------------------------ */

static void bx_fixed_cases(void)
{
    static const LONG fills[][4] = {
        { 0, 0, 512, 256 }, { 1, 1, 1, 1 }, { 3, 5, 7, 9 },
        { 17, 33, 255, 100 }, { 100, 20, 64, 64 }, { 511, 255, 1, 1 },
        { 2, 0, 510, 1 }, { 0, 128, 512, 128 }, { 13, 7, 333, 211 },
        { 0, 0, 2, 256 }, { 510, 0, 2, 256 }, { 1, 0, 2, 256 },
        { 0, 255, 512, 1 }, { 7, 3, 256, 250 }
    };
    static const LONG copies[][6] = {
        /* sx, sy, dx, dy, w, h */
        { 0, 0, 0, 0, 512, 256 }, { 1, 2, 5, 3, 7, 9 },
        { 0, 0, 511, 255, 1, 1 }, { 13, 17, 29, 31, 333, 199 },
        { 100, 100, 0, 0, 412, 156 }, { 0, 0, 1, 0, 511, 256 },
        { 1, 0, 0, 0, 511, 256 }, { 3, 1, 0, 2, 1, 200 },
        { 0, 0, 0, 255, 512, 1 }, { 255, 127, 256, 128, 256, 128 }
    };
    static const LONG selfs[][6] = {
        /* No byte shared: side by side, interleaved rows, stacked. */
        { 0, 0, 256, 0, 200, 100 }, { 0, 10, 260, 20, 250, 200 },
        { 0, 0, 0, 128, 512, 100 }, { 300, 50, 1, 60, 211, 150 },
        { 0, 0, 3, 200, 509, 56 },
        /* Overlapping: the driver must keep these off a byte copy. */
        { 0, 0, 10, 5, 300, 200 }, { 10, 5, 0, 0, 300, 200 },
        { 0, 0, 1, 0, 511, 256 }, { 1, 0, 0, 0, 511, 256 },
        { 0, 1, 0, 0, 512, 255 }, { 0, 0, 0, 1, 512, 255 }
    };
    DWORD index;
    LONG width;

    for (index = 0ul; index < sizeof(fills) / sizeof(fills[0]); ++index) {
        bx_fill(fills[index][0], fills[index][1], fills[index][2],
                fills[index][3], bx_colour(0x00a5c3e1ul + index * 0x01020304ul));
    }
    /* Every width from 1 to 9 pixels at an odd start: the 16 bpp fills
     * that are not whole dwords, and the copies' byte counts. */
    for (width = 1; width <= 9; ++width) {
        bx_fill(5, 40 + width, width, 3, bx_colour(0x005a3c1eul + (DWORD)width));
        bx_copy(BX_OP_COPY, 3, 7, 5, 60 + width, width, 3);
    }
    for (index = 0ul; index < sizeof(copies) / sizeof(copies[0]); ++index) {
        bx_copy(BX_OP_COPY, copies[index][0], copies[index][1],
                copies[index][2], copies[index][3], copies[index][4],
                copies[index][5]);
    }
    for (index = 0ul; index < sizeof(selfs) / sizeof(selfs[0]); ++index) {
        bx_copy(BX_OP_SELF, selfs[index][0], selfs[index][1],
                selfs[index][2], selfs[index][3], selfs[index][4],
                selfs[index][5]);
    }
}

static void bx_random_cases(DWORD count)
{
    DWORD index;

    for (index = 0ul; index < count; ++index) {
        LONG w = 1 + bx_below(BX_WIDTH);
        LONG h = 1 + bx_below(BX_HEIGHT);
        LONG dx = bx_below(BX_WIDTH - (DWORD)w + 1ul);
        LONG dy = bx_below(BX_HEIGHT - (DWORD)h + 1ul);
        LONG sx = bx_below(BX_WIDTH - (DWORD)w + 1ul);
        LONG sy = bx_below(BX_HEIGHT - (DWORD)h + 1ul);

        switch (bx_random() % 3ul) {
        case 0ul:
            bx_fill(dx, dy, w, h, bx_colour(bx_random()));
            break;
        case 1ul:
            bx_copy(BX_OP_COPY, sx, sy, dx, dy, w, h);
            break;
        default:
            bx_copy(BX_OP_SELF, sx, sy, dx, dy, w, h);
            break;
        }
    }
}

static DWORD bx_argument(const char *name, DWORD fallback)
{
    const char *line = GetCommandLineA();
    const char *match;
    DWORD value = 0ul;

    for (; *line != '\0'; ++line) {
        const char *a = line;
        const char *b = name;

        while (*b != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        if (*b == '\0') {
            match = a;
            if (*match < '0' || *match > '9') {
                return fallback;
            }
            while (*match >= '0' && *match <= '9') {
                value = value * 10ul + (DWORD)(*match - '0');
                ++match;
            }
            return value;
        }
    }
    return fallback;
}

void WINAPI V9xBltExactEntry(void)
{
    HMODULE ddraw_module;
    bx_create_fn create;
    BX_OBJECT *ddraw = 0;
    DWORD random_count = bx_argument("/random:", BX_RANDOM);
    DWORD started;
    HRESULT hr;

    CreateDirectoryA("C:\\V9XDIAG", 0);
    DeleteFileA(BX_INI);
    WritePrivateProfileStringA("Report", "Build", V9X_BUILD_ID, BX_INI);

    ddraw_module = LoadLibraryA("DDRAW.DLL");
    create = ddraw_module != 0
        ? (bx_create_fn)GetProcAddress(ddraw_module, "DirectDrawCreate") : 0;
    if (create == 0 || create(0, &ddraw, 0) != 0 || ddraw == 0) {
        WritePrivateProfileStringA("Report", "Result", "NO-DDRAW", BX_INI);
        ExitProcess(2u);
    }
    hr = ((bx_coop_fn)ddraw->vtbl[DD_SET_COOPERATIVE_LEVEL])(
        ddraw, GetDesktopWindow(), DDSCL_NORMAL);
    bx_key_hex("Report", "CooperativeHr", (DWORD)hr);

    bx_row = (BYTE *)VirtualAlloc(0, BX_WIDTH * BX_HEIGHT * 4ul, MEM_COMMIT,
                                  PAGE_READWRITE);
    if (bx_row == 0 || !bx_create(ddraw, &bx_source) ||
        !bx_create(ddraw, &bx_destination)) {
        WritePrivateProfileStringA("Report", "Result", "NO-SURFACES", BX_INI);
        ExitProcess(3u);
    }
    bx_key_dec("Report", "Bpp", bx_bpp);
    bx_key_dec("Report", "Pitch", bx_destination.pitch);
    bx_key_hex("Report", "SourceAddress", bx_source.address);
    bx_key_hex("Report", "DestinationAddress", bx_destination.address);
    if (bx_bpp != 16ul && bx_bpp != 32ul) {
        WritePrivateProfileStringA("Report", "Result", "DEPTH-NOT-TESTED",
                                   BX_INI);
        ExitProcess(4u);
    }

    started = GetTickCount();
    if (bx_argument("/selftest:", 0ul) != 0ul) {
        /* The control: one byte changed behind the model's back, far
         * from the fill that follows, must come back as exactly one bad
         * case with one wrong byte outside its rectangle. */
        BYTE *pixels = bx_lock(&bx_destination);

        if (pixels != 0) {
            pixels[bx_destination.bytes - 1ul] ^= 0x5au;
            bx_unlock(&bx_destination);
        }
        bx_fill(0, 0, 4, 4, bx_colour(0x00123456ul));
        bx_key_dec("SelfTest", "CasesBad", bx_cases_bad);
        bx_key_dec("SelfTest", "BytesOutsideBad", bx_bytes_outside_bad);
        WritePrivateProfileStringA("SelfTest", "Result",
            (bx_cases_bad == 1ul && bx_bytes_outside_bad == 1ul &&
             bx_bytes_inside_bad == 0ul) ? "DETECTED" : "MISSED", BX_INI);
        bx_case = 0ul;
        bx_cases_bad = 0ul;
        bx_bytes_outside_bad = 0ul;
        bx_reported = 0ul;
    }
    bx_fixed_cases();
    bx_key_dec("Report", "FixedCases", bx_case);
    bx_key_dec("Report", "FixedCasesBad", bx_cases_bad);
    bx_random_cases(random_count);
    bx_key_dec("Report", "Cases", bx_case);
    bx_key_dec("Report", "CasesBad", bx_cases_bad);
    bx_key_dec("Report", "BltFailed", bx_blt_failed);
    bx_key_dec("Report", "BytesInsideBad", bx_bytes_inside_bad);
    bx_key_dec("Report", "BytesOutsideBad", bx_bytes_outside_bad);
    bx_key_dec("Report", "Milliseconds", GetTickCount() - started);

    ((bx_release_fn)bx_destination.object->vtbl[DD_RELEASE])(
        bx_destination.object);
    ((bx_release_fn)bx_source.object->vtbl[DD_RELEASE])(bx_source.object);
    ((bx_release_fn)ddraw->vtbl[DD_RELEASE])(ddraw);
    WritePrivateProfileStringA("Report", "Result",
                               bx_cases_bad == 0ul ? "PASS" : "FAIL", BX_INI);
    WritePrivateProfileStringA(0, 0, 0, BX_INI);
    ExitProcess(bx_cases_bad == 0ul ? 0u : 1u);
}
