/*
 * The Win16 mutex, as seen from inside a HAL callback.
 *
 * Instrument for docs\plans\opengl-1.1-icd.md Phase 0.2: the tree has assumed
 * since the first D3D engine that DirectDraw holds the Win16 mutex around
 * every HAL callback (d3d_i9xx.c, gdi-acceleration.md), and nothing has
 * measured it. This module resolves KERNEL32's _ConfirmWin16Lock (#96) at
 * DriverInit and lets each callback record what it answers - 1 when the
 * calling thread holds the mutex, 0 when it does not, which is what the
 * calibration probe established
 * (docs\decisions\2026-09-26-98se-confirmwin16lock-is-one-when-held-and-getprocaddress-refuses-the-ordinals.md).
 *
 * Resolution is by walking KERNEL32's export table, because GetProcAddress
 * refuses these ordinals on 98SE. The walk itself is the OS-free
 * src\common\pe_export.c; this file supplies the module base and the image
 * size and touches nothing it has not first shown to be readable. The four
 * ordinals are resolved together. #96 is what the callbacks sample; #93,
 * #97 and #98 - GetpWin16Lock, _EnterSysLevel and _LeaveSysLevel - are what
 * the render interface takes the mutex with (v9x_win16_enter/leave), because
 * its calls come from the ICD directly and not through DirectDraw, which is
 * what holds the mutex around every HAL callback (Phase 0.2).
 *
 * KERNEL32 is mapped at one address in every process on Win9x, so the
 * resolved pointers are valid from whichever process DirectDraw calls in,
 * and being file-scope statics in a fully shared image they are resolved once.
 */
#include "ddhal_internal.h"
#include "velocity9x/pe_export.h"

#define V9X_WIN16_ORD_GETPWIN16LOCK 93ul
#define V9X_WIN16_ORD_CONFIRM       96ul
#define V9X_WIN16_ORD_ENTER         97ul
#define V9X_WIN16_ORD_LEAVE         98ul

/*
 * Bits of d3d_diagnostics.win16_resolved, one per step, so a run that
 * resolves nothing says which step refused. The first HAL build tested the
 * whole of SizeOfImage with IsBadReadPtr and stopped at HEADERS on 98SE:
 * KERNEL32's mapping has a page inside its image that the test refuses, so
 * only the two ranges the walk reads are tested now.
 */
#define V9X_WIN16_RESOLVED_KERNEL32 0x0001ul /* GetModuleHandleA answered   */
#define V9X_WIN16_RESOLVED_HEADERS  0x0002ul /* first page readable, PE32   */
#define V9X_WIN16_RESOLVED_TABLE    0x0004ul /* directory located and readable */
#define V9X_WIN16_RESOLVED_ENTRIES  0x0008ul /* the function table is readable */
#define V9X_WIN16_RESOLVED_93       0x0010ul
#define V9X_WIN16_RESOLVED_96       0x0020ul
#define V9X_WIN16_RESOLVED_97       0x0040ul
#define V9X_WIN16_RESOLVED_98       0x0080ul
#define V9X_WIN16_RESOLVED_GPA_ANY  0x0100ul /* GetProcAddress by ordinal gave a pointer */
#define V9X_WIN16_RESOLVED_DONE     0x8000ul /* this function ran           */

/* The first page holds every header the size query reads. */
#define V9X_WIN16_HEADER_BYTES 0x1000ul
/* The export directory is 40 bytes; KERNEL32 on 98SE has 865 functions, and
 * a table claiming more than this is not one worth reading. */
#define V9X_WIN16_DIRECTORY_BYTES 40ul
#define V9X_WIN16_FUNCTION_COUNT_MAX 65536ul

typedef DWORD (WINAPI *V9X_WIN16_CONFIRM)(void);
/* GetpWin16Lock stores the mutex's SYSLEVEL; _EnterSysLevel and
 * _LeaveSysLevel take and release it, recursively (Wine's syslevel.c
 * declares all three WINAPI, and the DX3 DDRAW.DLL imports them by these
 * ordinals). */
typedef VOID (WINAPI *V9X_WIN16_GETLOCK)(void **lock);
typedef VOID (WINAPI *V9X_WIN16_SYSLEVEL)(void *lock);

static V9X_WIN16_CONFIRM v9x_win16_confirm;
static V9X_WIN16_SYSLEVEL v9x_win16_enter_level;
static V9X_WIN16_SYSLEVEL v9x_win16_leave_level;
static void *v9x_win16_lock;
static DWORD v9x_win16_resolved_mask;

static DWORD v9x_win16_walk(const BYTE *image, DWORD image_bytes, DWORD ordinal)
{
    v9x_u32 rva = 0ul;

    if (v9x_pe_export_by_ordinal(image, image_bytes, ordinal, &rva) !=
        V9X_PE_EXPORT_OK) {
        return 0ul;
    }
    return (DWORD)image + rva;
}

void v9x_win16_resolve(void)
{
    HMODULE kernel32;
    const BYTE *image;
    v9x_u32 image_bytes = 0ul;
    v9x_u32 directory_rva = 0ul;
    v9x_u32 functions_rva = 0ul;
    v9x_u32 function_count = 0ul;
    DWORD mask = V9X_WIN16_RESOLVED_DONE;
    DWORD address;

    v9x_win16_confirm = 0;
    v9x_win16_enter_level = 0;
    v9x_win16_leave_level = 0;
    v9x_win16_lock = 0;
    kernel32 = GetModuleHandleA("KERNEL32.DLL");
    if (kernel32 == 0) {
        v9x_win16_resolved_mask = mask;
        return;
    }
    mask |= V9X_WIN16_RESOLVED_KERNEL32;
    if (GetProcAddress(kernel32, (LPCSTR)V9X_WIN16_ORD_CONFIRM) != 0) {
        mask |= V9X_WIN16_RESOLVED_GPA_ANY;
    }
    image = (const BYTE *)kernel32;
    if (IsBadReadPtr(image, V9X_WIN16_HEADER_BYTES) ||
        v9x_pe_size_of_image(image, V9X_WIN16_HEADER_BYTES, &image_bytes) !=
            V9X_PE_EXPORT_OK) {
        v9x_win16_resolved_mask = mask;
        return;
    }
    mask |= V9X_WIN16_RESOLVED_HEADERS;
    /* The two ranges the walk reads, each proven readable before the pure
     * resolver reads it: the 40-byte directory, then the function table it
     * names. The resolver re-checks both against image_bytes arithmetically. */
    if (v9x_pe_export_table(image, image_bytes, &directory_rva,
                            &functions_rva, &function_count) !=
            V9X_PE_EXPORT_OK ||
        IsBadReadPtr(image + directory_rva, V9X_WIN16_DIRECTORY_BYTES)) {
        v9x_win16_resolved_mask = mask;
        return;
    }
    mask |= V9X_WIN16_RESOLVED_TABLE;
    if (function_count > V9X_WIN16_FUNCTION_COUNT_MAX ||
        IsBadReadPtr(image + functions_rva, function_count * 4ul)) {
        v9x_win16_resolved_mask = mask;
        return;
    }
    mask |= V9X_WIN16_RESOLVED_ENTRIES;
    /* The three the render interface needs, kept only as a set: a mutex it
     * could enter and not leave, or leave without entering, is worse than
     * refusing every call with NOT_READY. The walk has already bounded each
     * RVA inside KERNEL32's image. */
    {
        DWORD getlock = v9x_win16_walk(image, image_bytes,
                                       V9X_WIN16_ORD_GETPWIN16LOCK);
        DWORD enter = v9x_win16_walk(image, image_bytes, V9X_WIN16_ORD_ENTER);
        DWORD leave = v9x_win16_walk(image, image_bytes, V9X_WIN16_ORD_LEAVE);

        if (getlock != 0ul) {
            mask |= V9X_WIN16_RESOLVED_93;
        }
        if (enter != 0ul) {
            mask |= V9X_WIN16_RESOLVED_97;
        }
        if (leave != 0ul) {
            mask |= V9X_WIN16_RESOLVED_98;
        }
        if (getlock != 0ul && enter != 0ul && leave != 0ul) {
            void *lock = 0;

            ((V9X_WIN16_GETLOCK)getlock)(&lock);
            if (lock != 0) {
                v9x_win16_lock = lock;
                v9x_win16_enter_level = (V9X_WIN16_SYSLEVEL)enter;
                v9x_win16_leave_level = (V9X_WIN16_SYSLEVEL)leave;
            }
        }
    }
    address = v9x_win16_walk(image, image_bytes, V9X_WIN16_ORD_CONFIRM);
    if (address != 0ul) {
        mask |= V9X_WIN16_RESOLVED_96;
        v9x_win16_confirm = (V9X_WIN16_CONFIRM)address;
    }
    v9x_win16_resolved_mask = mask;
}

void v9x_win16_publish(void)
{
    if (v9x_hal != 0) {
        v9x_hal->d3d_diagnostics.win16_resolved = v9x_win16_resolved_mask;
    }
}

void v9x_win16_sample(DWORD site)
{
    DWORD answer;

    if (v9x_hal == 0 || v9x_win16_confirm == 0 ||
        site >= V9X_WIN16_SITE_COUNT) {
        return;
    }
    /* The answer is the calling thread's recursion depth on the mutex: 0
     * is not held, and run1 measured 2 inside every Lock callback. */
    answer = v9x_win16_confirm();
    ++v9x_hal->d3d_diagnostics.win16_calls[site];
    if (answer != 0ul) {
        ++v9x_hal->d3d_diagnostics.win16_held[site];
    } else {
        ++v9x_hal->d3d_diagnostics.win16_unheld[site];
    }
    if (answer > v9x_hal->d3d_diagnostics.win16_depth_max[site]) {
        v9x_hal->d3d_diagnostics.win16_depth_max[site] = answer;
    }
    v9x_hal->d3d_diagnostics.win16_last[site] = answer;
}

int v9x_win16_enter(void)
{
    if (v9x_win16_lock == 0 || v9x_win16_enter_level == 0 ||
        v9x_win16_leave_level == 0) {
        return 0;
    }
    v9x_win16_enter_level(v9x_win16_lock);
    return 1;
}

void v9x_win16_leave(void)
{
    if (v9x_win16_lock == 0 || v9x_win16_leave_level == 0) {
        return;
    }
    v9x_win16_leave_level(v9x_win16_lock);
}
