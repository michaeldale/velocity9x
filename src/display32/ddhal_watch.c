/*
 * How each program that loads the HAL ends.
 *
 * The Carmageddon II reports of 2026-10-11 (V9X-DWFJKP, V9X-1TKP9Q) ended in
 * both renderers with every driver counter clean: an orderly DirectDraw
 * teardown, no fault capture, and a Glide DLL that detached without
 * grGlideShutdown. Whether the game called ExitProcess, crashed under a
 * filter of its own, or showed a message first was not recorded anywhere,
 * nor what memory it had been told it had - and the one difference between
 * the run that played and the run that did not was Windows using 2 GB instead
 * of 512 MB (docs\issues\2026-10-10-carmageddon2-exits-after-intro-gma900.md).
 *
 * So when the HAL attaches to a process it replaces four of the program's
 * import slots, and each replacement records into the shared block
 * (V9X_WATCH in win9x_ddraw_abi.h) before calling what the slot held:
 *
 * - ExitProcess, and TerminateProcess of the caller itself: the exit code and
 *   the return address, as a module base and an offset to disassemble.
 * - MessageBoxA: caption and text, recorded before the box is shown, so a
 *   message the player dismissed or never saw is still read.
 * - SetUnhandledExceptionFilter: the program's filter is kept and called
 *   after the HAL's, instead of replacing it. DriverInit used to set the
 *   HAL's filter over whatever the program had, and a program setting its
 *   own afterwards removed the HAL's; either way one fault capture was lost.
 *
 * The slots are the program's own (its main module's import address table),
 * found by the address the loader wrote in them (src\common\pe_import.c).
 * A call through another module - a C runtime DLL's exit - is not seen; the
 * detach record still says whether the process ended or only unloaded the
 * HAL.
 *
 * The HAL is a shared-arena DLL on Windows 9x: one copy of its code and data
 * serves every process (win16lock.c relies on the same), and DirectDraw loads
 * it once, in DDHELP. A game calls into it without ever loading it, so its
 * DLL_PROCESS_ATTACH never runs there - the first build of this file watched
 * only DDHELP.EXE (86Box ViRGE guest, boot 661). So the first CreateSurface,
 * Direct3D context or render-interface describe a process makes loads the
 * HAL into it by name (v9x_watch_note). That gives the process its own
 * attach, which does the work, and its own detach, which records how it
 * ended; and the load holds the HAL mapped for as long as the process lives,
 * so the replaced slots can never point at unmapped code.
 *
 * The per-process state below is a table keyed by process id, not plain
 * statics, and the KERNEL32 and USER32 addresses resolved once are valid in
 * every process because both are system DLLs mapped at one address
 * everywhere. Two processes noting themselves at the same instant could race
 * for a table entry; a lost entry costs only that process's records.
 *
 * On FreeLibrary the slots and the filter are put back, because the code
 * they point at may be unmapped once the last process lets the HAL go.
 */
#include "ddhal_internal.h"
#include "velocity9x/pe_export.h"
#include "velocity9x/pe_import.h"

/* Processes with the HAL loaded at once; more than this go unwatched. */
#define V9X_WATCH_LOCAL_COUNT 16
/* Replaced slots kept per process for FreeLibrary to put back. A program
 * importing one function through more than one slot is unusual; two each. */
#define V9X_WATCH_SLOT_COUNT 8
#define V9X_WATCH_SLOTS_EACH 2ul
/* The first page holds every header the size query reads (win16lock.c). */
#define V9X_WATCH_HEADER_BYTES 0x1000ul
/* The HAL's module name (build-ddraw-hal-dll.ps1, option modname), which
 * GetModuleHandleA answers for only in a process that has loaded it. */
#define V9X_WATCH_HAL_MODULE "V9XHAL.DLL"

typedef VOID (WINAPI *V9X_WATCH_EXIT_FN)(UINT code);
typedef BOOL (WINAPI *V9X_WATCH_TERMINATE_FN)(HANDLE process, UINT code);
typedef int (WINAPI *V9X_WATCH_MESSAGE_FN)(HWND owner, LPCSTR text,
                                          LPCSTR caption, UINT type);
typedef LPTOP_LEVEL_EXCEPTION_FILTER (WINAPI *V9X_WATCH_FILTER_FN)(
    LPTOP_LEVEL_EXCEPTION_FILTER filter);

typedef struct v9x_watch_local {
    DWORD pid;
    DWORD hooks;
    int published;
    DWORD record;            /* its V9X_WATCH_PROCESS slot, once published */
    DWORD attach_tick;
    MEMORYSTATUS memory;
    char name[V9X_WATCH_NAME_BYTES];
    DWORD image;
    LPTOP_LEVEL_EXCEPTION_FILTER chained;
    DWORD slot_count;
    DWORD slot_address[V9X_WATCH_SLOT_COUNT];
    DWORD slot_original[V9X_WATCH_SLOT_COUNT];
    DWORD slot_hook[V9X_WATCH_SLOT_COUNT];
} V9X_WATCH_LOCAL;

static V9X_WATCH_LOCAL v9x_watch_locals[V9X_WATCH_LOCAL_COUNT];
/* The HAL's own path, from the first attach (DDHELP's), for v9x_watch_note
 * to load it by in every other process. */
static char v9x_watch_hal_path[MAX_PATH];

static V9X_WATCH_EXIT_FN v9x_watch_real_exit;
static V9X_WATCH_TERMINATE_FN v9x_watch_real_terminate;
static V9X_WATCH_MESSAGE_FN v9x_watch_real_message;
static V9X_WATCH_FILTER_FN v9x_watch_real_filter;

static V9X_WATCH_LOCAL *v9x_watch_find(DWORD pid)
{
    DWORD index;

    for (index = 0ul; index < V9X_WATCH_LOCAL_COUNT; ++index) {
        if (v9x_watch_locals[index].pid == pid) {
            return &v9x_watch_locals[index];
        }
    }
    return 0;
}

static V9X_WATCH *v9x_watch_shared(void)
{
    if (v9x_hal == 0) {
        return 0;
    }
    return &v9x_hal->d3d_diagnostics.watch;
}

/* The base of the module an address lies in: its allocation base, which for
 * code in an image is the image's. 0 when the address is not mapped. */
static DWORD v9x_watch_module_of(DWORD address)
{
    MEMORY_BASIC_INFORMATION info;

    if (VirtualQuery((LPCVOID)address, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT) {
        return 0ul;
    }
    return (DWORD)info.AllocationBase;
}

/* A string the program handed over, copied only as far as it is readable. */
static void v9x_watch_copy_text(char *destination, DWORD capacity,
                                LPCSTR source)
{
    DWORD index;

    destination[0] = '\0';
    if (source == 0 || IsBadStringPtrA(source, capacity)) {
        return;
    }
    for (index = 0ul; index + 1ul < capacity && source[index] != '\0';
         ++index) {
        destination[index] = source[index];
    }
    destination[index] = '\0';
}

/* The process's slot in the shared block, written once v9x_hal exists. */
static void v9x_watch_publish_local(V9X_WATCH_LOCAL *local)
{
    V9X_WATCH *watch = v9x_watch_shared();
    V9X_WATCH_PROCESS *process;
    DWORD index;

    if (watch == 0 || local->published) {
        return;
    }
    local->record = watch->next_slot % V9X_WATCH_PROCESS_SLOTS;
    watch->next_slot = local->record + 1ul;
    process = &watch->processes[local->record];
    process->pid = local->pid;
    process->image = local->image;
    process->hooks = local->hooks;
    process->attach_tick = local->attach_tick;
    process->ended = V9X_WATCH_ENDED_RUNNING;
    process->ended_tick = 0ul;
    process->memory_load = local->memory.dwMemoryLoad;
    process->total_phys = local->memory.dwTotalPhys;
    process->avail_phys = local->memory.dwAvailPhys;
    process->total_page_file = local->memory.dwTotalPageFile;
    process->avail_page_file = local->memory.dwAvailPageFile;
    process->total_virtual = local->memory.dwTotalVirtual;
    process->avail_virtual = local->memory.dwAvailVirtual;
    for (index = 0ul; index < V9X_WATCH_NAME_BYTES; ++index) {
        process->name[index] = local->name[index];
    }
    local->published = 1;
}

/* Keep the published hooks word current as the filter is installed. */
static void v9x_watch_update_hooks(V9X_WATCH_LOCAL *local)
{
    V9X_WATCH *watch = v9x_watch_shared();

    if (watch == 0 || !local->published ||
        watch->processes[local->record].pid != local->pid) {
        return;
    }
    watch->processes[local->record].hooks = local->hooks;
}

static void v9x_watch_record_exit(DWORD via, DWORD code, DWORD caller)
{
    V9X_WATCH *watch = v9x_watch_shared();

    if (watch == 0) {
        return;
    }
    watch->exit_via = via;
    watch->exit_pid = GetCurrentProcessId();
    watch->exit_code = code;
    watch->exit_caller = caller;
    watch->exit_caller_module = v9x_watch_module_of(caller);
    watch->exit_tick = GetTickCount();
}

/*
 * The replacements. Each is __stdcall with the signature of what it
 * replaces, so the program's call and stack cleanup are unchanged.
 *
 * The caller is read from the stack: in a __stdcall function the first
 * argument's slot lies directly above the return address, and taking that
 * argument's address names its slot. This holds only while the argument
 * is passed on the stack, which WINAPI guarantees.
 */
static VOID WINAPI v9x_watch_exit_process(UINT code)
{
    v9x_watch_record_exit(V9X_WATCH_EXIT_PROCESS, code,
                          ((DWORD *)&code)[-1]);
    v9x_watch_real_exit(code);
}

static BOOL WINAPI v9x_watch_terminate(HANDLE process, UINT code)
{
    /* Only the program ending itself; terminating another process is not
     * an exit of this one. Win9x answers GetCurrentProcess with a constant
     * pseudo-handle, which is what such a call passes. */
    if (process == GetCurrentProcess()) {
        v9x_watch_record_exit(V9X_WATCH_EXIT_TERMINATE, code,
                              ((DWORD *)&process)[-1]);
    }
    return v9x_watch_real_terminate(process, code);
}

static int WINAPI v9x_watch_message_box(HWND owner, LPCSTR text,
                                        LPCSTR caption, UINT type)
{
    V9X_WATCH *watch = v9x_watch_shared();
    DWORD caller = ((DWORD *)&owner)[-1];

    if (watch != 0) {
        ++watch->message_count;
        watch->message_pid = GetCurrentProcessId();
        watch->message_type = type;
        watch->message_caller = caller;
        watch->message_caller_module = v9x_watch_module_of(caller);
        watch->message_tick = GetTickCount();
        v9x_watch_copy_text(watch->message_caption, V9X_WATCH_CAPTION_BYTES,
                            caption);
        v9x_watch_copy_text(watch->message_text, V9X_WATCH_TEXT_BYTES, text);
    }
    return v9x_watch_real_message(owner, text, caption, type);
}

/* The HAL's fault filter: the trace ring to V9XTRACE.INI, the fault into
 * the watch, then the program's own filter if it set one. */
static LONG WINAPI v9x_watch_filter(struct _EXCEPTION_POINTERS *exception)
{
    V9X_WATCH *watch = v9x_watch_shared();
    V9X_WATCH_LOCAL *local = v9x_watch_find(GetCurrentProcessId());
    DWORD code = 0ul;
    DWORD address = 0ul;

    if (exception != 0 && exception->ExceptionRecord != 0) {
        code = exception->ExceptionRecord->ExceptionCode;
        address = (DWORD)exception->ExceptionRecord->ExceptionAddress;
    }
    if (watch != 0) {
        watch->fault_pid = GetCurrentProcessId();
        watch->fault_code = code;
        watch->fault_address = address;
        watch->fault_module = v9x_watch_module_of(address);
        watch->fault_tick = GetTickCount();
    }
    v9x_trace_flush_fault(code, address);
    if (local != 0 && local->chained != 0) {
        return local->chained(exception);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI v9x_watch_set_filter(
    LPTOP_LEVEL_EXCEPTION_FILTER filter)
{
    V9X_WATCH_LOCAL *local = v9x_watch_find(GetCurrentProcessId());
    V9X_WATCH *watch = v9x_watch_shared();
    LPTOP_LEVEL_EXCEPTION_FILTER previous;

    if (local == 0) {
        return v9x_watch_real_filter(filter);
    }
    previous = local->chained;
    local->chained = filter == v9x_watch_filter ? 0 : filter;
    if (watch != 0) {
        ++watch->filter_count;
        watch->filter_pid = local->pid;
        watch->filter_last = (DWORD)filter;
        watch->filter_caller = ((DWORD *)&filter)[-1];
    }
    v9x_watch_real_filter(v9x_watch_filter);
    return previous;
}

/* KERNEL32's and USER32's addresses, the same in every process. USER32 is
 * asked again by each attach until a process that has it loaded answers. */
static void v9x_watch_resolve(void)
{
    HMODULE module;

    if (v9x_watch_real_filter == 0) {
        module = GetModuleHandleA("KERNEL32.DLL");
        if (module != 0) {
            v9x_watch_real_exit = (V9X_WATCH_EXIT_FN)GetProcAddress(
                module, "ExitProcess");
            v9x_watch_real_terminate = (V9X_WATCH_TERMINATE_FN)
                GetProcAddress(module, "TerminateProcess");
            v9x_watch_real_filter = (V9X_WATCH_FILTER_FN)GetProcAddress(
                module, "SetUnhandledExceptionFilter");
        }
    }
    if (v9x_watch_real_message == 0) {
        module = GetModuleHandleA("USER32.DLL");
        if (module != 0) {
            v9x_watch_real_message = (V9X_WATCH_MESSAGE_FN)GetProcAddress(
                module, "MessageBoxA");
        }
    }
}

/* One slot: writable for the one store, then as it was. */
static int v9x_watch_store(DWORD address, DWORD value)
{
    DWORD protection;
    DWORD ignored;

    if (!VirtualProtect((LPVOID)address, sizeof(DWORD), PAGE_READWRITE,
                        &protection)) {
        return 0;
    }
    *(DWORD *)address = value;
    VirtualProtect((LPVOID)address, sizeof(DWORD), protection, &ignored);
    return 1;
}

/* Replace the program's slots holding real with hook; bit on success. */
static void v9x_watch_patch(V9X_WATCH_LOCAL *local, const BYTE *image,
                            DWORD image_bytes, DWORD real, DWORD hook,
                            DWORD bit)
{
    v9x_u32 slots[V9X_WATCH_SLOTS_EACH];
    v9x_u32 found = 0ul;
    v9x_u32 index;
    v9x_u16 status;

    if (real == 0ul) {
        return;
    }
    status = v9x_pe_import_slots(image, image_bytes, real, slots,
                                 V9X_WATCH_SLOTS_EACH, &found);
    if (status != V9X_PE_IMPORT_OK) {
        local->hooks |= V9X_WATCH_FAILED_WALK;
    }
    if (found > V9X_WATCH_SLOTS_EACH) {
        found = V9X_WATCH_SLOTS_EACH;
    }
    for (index = 0ul; index < found; ++index) {
        DWORD address = (DWORD)image + slots[index];

        if (local->slot_count >= V9X_WATCH_SLOT_COUNT) {
            return;
        }
        if (!v9x_watch_store(address, hook)) {
            local->hooks |= V9X_WATCH_FAILED_PROTECT;
            continue;
        }
        local->slot_address[local->slot_count] = address;
        local->slot_original[local->slot_count] = real;
        local->slot_hook[local->slot_count] = hook;
        ++local->slot_count;
        local->hooks |= bit;
    }
}

static void v9x_watch_patch_program(V9X_WATCH_LOCAL *local)
{
    const BYTE *image = (const BYTE *)GetModuleHandleA(0);
    v9x_u32 image_bytes = 0ul;

    local->image = (DWORD)image;
    if (image == 0 || IsBadReadPtr(image, V9X_WATCH_HEADER_BYTES) ||
        v9x_pe_size_of_image(image, V9X_WATCH_HEADER_BYTES, &image_bytes) !=
            V9X_PE_EXPORT_OK ||
        IsBadReadPtr(image, image_bytes)) {
        local->hooks |= V9X_WATCH_FAILED_HEADERS;
        return;
    }
    v9x_watch_patch(local, image, image_bytes, (DWORD)v9x_watch_real_exit,
                    (DWORD)v9x_watch_exit_process,
                    V9X_WATCH_HOOK_EXIT_PROCESS);
    v9x_watch_patch(local, image, image_bytes,
                    (DWORD)v9x_watch_real_terminate,
                    (DWORD)v9x_watch_terminate, V9X_WATCH_HOOK_TERMINATE);
    v9x_watch_patch(local, image, image_bytes, (DWORD)v9x_watch_real_message,
                    (DWORD)v9x_watch_message_box,
                    V9X_WATCH_HOOK_MESSAGE_BOX);
    v9x_watch_patch(local, image, image_bytes, (DWORD)v9x_watch_real_filter,
                    (DWORD)v9x_watch_set_filter, V9X_WATCH_HOOK_FILTER);
}

void v9x_watch_attach(HINSTANCE instance)
{
    DWORD pid = GetCurrentProcessId();
    V9X_WATCH_LOCAL *local = v9x_watch_find(pid);
    char path[MAX_PATH];
    DWORD length;
    DWORD start;
    DWORD index;

    if (v9x_watch_hal_path[0] == '\0') {
        length = GetModuleFileNameA(instance, v9x_watch_hal_path,
                                    sizeof(v9x_watch_hal_path));
        if (length == 0ul || length >= sizeof(v9x_watch_hal_path)) {
            v9x_watch_hal_path[0] = '\0';
        }
    }
    if (local == 0) {
        local = v9x_watch_find(0ul);
    }
    if (local == 0) {
        return;
    }
    local->pid = pid;
    local->hooks = 0ul;
    local->published = 0;
    local->record = 0ul;
    local->attach_tick = GetTickCount();
    local->chained = 0;
    local->slot_count = 0ul;
    local->memory.dwLength = sizeof(local->memory);
    GlobalMemoryStatus(&local->memory);

    length = GetModuleFileNameA(0, path, sizeof(path));
    if (length >= sizeof(path)) {
        length = 0ul;
    }
    path[length] = '\0';
    start = 0ul;
    for (index = 0ul; index < length; ++index) {
        if (path[index] == '\\' || path[index] == '/' || path[index] == ':') {
            start = index + 1ul;
        }
    }
    for (index = 0ul; index + 1ul < V9X_WATCH_NAME_BYTES &&
                      path[start + index] != '\0'; ++index) {
        local->name[index] = path[start + index];
    }
    local->name[index] = '\0';

    v9x_watch_resolve();
    if (v9x_watch_real_filter == 0) {
        return;
    }
    v9x_watch_patch_program(local);
    v9x_watch_install_filter();
    v9x_watch_publish_local(local);
}

/*
 * From a callback in the calling process: the first time this process is
 * seen, load the HAL into it, which runs v9x_watch_attach there.
 *
 * "Seen" is whether the HAL is loaded in this process, not whether the table
 * has its id. A process ended by TerminateProcess or by a fault never
 * detaches, so its entry stays, and Windows 9x hands its process id to a
 * later process: V9XDDP.EXE went unwatched on the ViRGE guest (boot 662)
 * behind the entries of probes ended that way. Such an entry is taken over;
 * with none free, the oldest is.
 */
void v9x_watch_note(void)
{
    DWORD pid = GetCurrentProcessId();
    V9X_WATCH_LOCAL *local;
    DWORD index;

    if (v9x_watch_hal_path[0] == '\0' ||
        GetModuleHandleA(V9X_WATCH_HAL_MODULE) != 0) {
        return;
    }
    local = v9x_watch_find(pid);
    if (local == 0) {
        local = v9x_watch_find(0ul);
    }
    if (local == 0) {
        local = &v9x_watch_locals[0];
        for (index = 1ul; index < V9X_WATCH_LOCAL_COUNT; ++index) {
            if (v9x_watch_locals[index].attach_tick < local->attach_tick) {
                local = &v9x_watch_locals[index];
            }
        }
    }
    local->pid = pid;
    local->attach_tick = GetTickCount();
    local->hooks = 0ul;
    local->published = 0;
    local->slot_count = 0ul;
    LoadLibraryA(v9x_watch_hal_path);
}

void v9x_watch_install_filter(void)
{
    V9X_WATCH_LOCAL *local = v9x_watch_find(GetCurrentProcessId());
    LPTOP_LEVEL_EXCEPTION_FILTER previous;

    if (local == 0 || v9x_watch_real_filter == 0) {
        /* Unwatched: the filter as DriverInit always set it. */
        SetUnhandledExceptionFilter(v9x_watch_filter);
        return;
    }
    previous = v9x_watch_real_filter(v9x_watch_filter);
    if (previous != v9x_watch_filter) {
        local->chained = previous;
    }
    local->hooks |= V9X_WATCH_FILTER_INSTALLED;
    v9x_watch_update_hooks(local);
}

void v9x_watch_publish(void)
{
    V9X_WATCH_LOCAL *local = v9x_watch_find(GetCurrentProcessId());

    if (local != 0) {
        v9x_watch_publish_local(local);
    }
}

void v9x_watch_detach(int terminating)
{
    V9X_WATCH_LOCAL *local = v9x_watch_find(GetCurrentProcessId());
    V9X_WATCH *watch = v9x_watch_shared();
    LPTOP_LEVEL_EXCEPTION_FILTER previous;
    DWORD index;

    if (local == 0) {
        return;
    }
    if (watch != 0 && local->published &&
        watch->processes[local->record].pid == local->pid) {
        watch->processes[local->record].ended =
            terminating ? V9X_WATCH_ENDED_TERMINATED
                        : V9X_WATCH_ENDED_UNLOADED;
        watch->processes[local->record].ended_tick = GetTickCount();
    }
    /* The program goes on without the HAL: give back what was replaced,
     * each slot only if it still holds the replacement. */
    if (!terminating) {
        for (index = 0ul; index < local->slot_count; ++index) {
            if (*(DWORD *)local->slot_address[index] ==
                local->slot_hook[index]) {
                v9x_watch_store(local->slot_address[index],
                                local->slot_original[index]);
            }
        }
        if ((local->hooks & V9X_WATCH_FILTER_INSTALLED) != 0ul &&
            v9x_watch_real_filter != 0) {
            previous = v9x_watch_real_filter(local->chained);
            if (previous != v9x_watch_filter) {
                /* Someone set a filter past the watch; leave theirs. */
                v9x_watch_real_filter(previous);
            }
        }
    }
    local->pid = 0ul;
}
