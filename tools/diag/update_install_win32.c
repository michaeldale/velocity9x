/*
 * V9XUPD.EXE: installing an update the way SetupX would, without Device
 * Manager (docs\plans\optional-update-checker-and-auto-updater.md).
 *
 * The display driver, mini-VDD and HAL are loaded while Windows runs, so
 * nothing in the system directory is overwritten here. Every new file is
 * staged under an 8.3 name on the system drive and moved into place by
 * WININIT.INI [rename] lines at the next restart, all of them at once.
 * Only dest=src lines are written, never NUL=: real-mode WININIT performs a
 * NUL= deletion even when the rename after it fails, which is how a 0-byte
 * display driver and no desktop happened on 2026-08-30. The registry half
 * of the INF is applied now, after a backup of everything it touches, and
 * /FINISH checks the result after the restart.
 *
 * Runtime-free: KERNEL32, USER32, ADVAPI32 statics; VERSION.DLL is loaded
 * at run time for the one file (GLIDE2X.DLL) whose copy depends on it.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/sha256.h"
#include "velocity9x/update_proto.h"
#include "velocity9x/zipread.h"
#include "update_win32.h"

#ifndef HKEY_DYN_DATA
#define HKEY_DYN_DATA ((HKEY)0x80000006ul)
#endif

#define V9X_INSTANCES_MAX   8u
#define V9X_INF_FILE_MAX    (256ul * 1024ul)
/* No file the INF copies is anywhere near this; a larger entry is not
 * ours. */
#define V9X_PACKAGE_FILE_MAX (4ul * 1024ul * 1024ul)
/* Its own section: [Velocity9xUpdate] holds the check record and
 * ReportUrl, which an install must not erase. */
#define V9X_UPDATE_SECTION  "Velocity9xInstall"
#define V9X_RUNONCE_KEY     "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"
#define V9X_RUNONCE_VALUE   "V9xUpdateFinish"
#define V9X_BACKUP_ROOT     V9X_DIAG_DIR "\\UPDATE"
#define V9X_REG_DEPTH_MAX   8u

typedef DWORD (WINAPI *v9x_version_size_fn)(LPSTR, LPDWORD);
typedef BOOL (WINAPI *v9x_version_info_fn)(LPSTR, DWORD, DWORD, LPVOID);
typedef BOOL (WINAPI *v9x_version_query_fn)(LPVOID, LPSTR, LPVOID *, PUINT);

static struct v9x_inflate_state v9x_install_inflate;

static DWORD v9x_install_length(const char *text)
{
    DWORD length = 0ul;

    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static void v9x_install_append(char *buffer, DWORD capacity, const char *text)
{
    DWORD used = v9x_install_length(buffer);

    while (*text != '\0' && used + 1ul < capacity) {
        buffer[used++] = *text++;
    }
    buffer[used] = '\0';
}

static void v9x_install_copy(char *buffer, DWORD capacity, const char *text)
{
    buffer[0] = '\0';
    v9x_install_append(buffer, capacity, text);
}

static BOOL v9x_install_fits(const char *text, DWORD capacity)
{
    return v9x_install_length(text) < capacity;
}

static void v9x_install_fail(struct v9x_update_job *job, const char *what,
                             const char *detail)
{
    v9x_install_copy(job->error, sizeof(job->error), what);
    if (detail != 0 && detail[0] != '\0') {
        v9x_install_append(job->error, sizeof(job->error), " ");
        v9x_install_append(job->error, sizeof(job->error), detail);
    }
}

static void v9x_install_hex(const BYTE digest[32], char text[65])
{
    static const char digits[] = "0123456789abcdef";
    unsigned int index;

    for (index = 0u; index < 32u; ++index) {
        text[index * 2u] = digits[digest[index] >> 4];
        text[index * 2u + 1u] = digits[digest[index] & 0x0Fu];
    }
    text[64] = '\0';
}

/* SHA-256 of a file on disk; FALSE if it cannot be read. */
static BOOL v9x_install_hash_file(const char *path, BYTE digest[32])
{
    struct v9x_sha256 context;
    BYTE chunk[4096];
    HANDLE handle;
    DWORD read;

    handle = CreateFileA(path, GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (handle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    v9x_sha256_init(&context);
    for (;;) {
        if (!ReadFile(handle, chunk, sizeof(chunk), &read, 0)) {
            CloseHandle(handle);
            return FALSE;
        }
        if (read == 0ul) {
            break;
        }
        v9x_sha256_update(&context, chunk, read);
    }
    CloseHandle(handle);
    v9x_sha256_final(&context, digest);
    return TRUE;
}

static BOOL v9x_install_write_file(const char *path, const BYTE *data,
                                   DWORD length)
{
    HANDLE handle;
    DWORD written = 0ul;
    BOOL ok;

    handle = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, 0);
    if (handle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    ok = WriteFile(handle, data, length, &written, 0) && written == length;
    CloseHandle(handle);
    return ok;
}

/* The same rule settings_syncmodes.c applies: is a device present this
 * boot whose driver is Display\<instance>? HKLM\Enum keeps the devnode of
 * a card that has been taken out; HKEY_DYN_DATA lists only present ones. */
static BOOL v9x_install_is_live(const char *instance)
{
    char wanted[32];
    HKEY live_root;
    DWORD index = 0ul;
    BOOL found = FALSE;

    wsprintfA(wanted, "Display\\%s", instance);
    if (RegOpenKeyExA(HKEY_DYN_DATA, "Config Manager\\Enum", 0, KEY_READ,
                      &live_root) != ERROR_SUCCESS) {
        return FALSE;
    }
    while (!found) {
        char node[32];
        char hardware[160];
        char enum_path[176];
        char driver[32];
        DWORD size = sizeof(node);
        DWORD type;
        DWORD hardware_size = sizeof(hardware);
        DWORD driver_size = sizeof(driver);
        HKEY node_key;
        HKEY enum_key;

        if (RegEnumKeyExA(live_root, index++, node, &size, 0, 0, 0, 0) !=
            ERROR_SUCCESS) {
            break;
        }
        if (RegOpenKeyExA(live_root, node, 0, KEY_READ, &node_key) !=
            ERROR_SUCCESS) {
            continue;
        }
        hardware[0] = '\0';
        if (RegQueryValueExA(node_key, "HardWareKey", 0, &type,
                             (BYTE *)hardware, &hardware_size) !=
                ERROR_SUCCESS || type != REG_SZ || hardware[0] == '\0') {
            RegCloseKey(node_key);
            continue;
        }
        RegCloseKey(node_key);
        wsprintfA(enum_path, "Enum\\%s", hardware);
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, enum_path, 0, KEY_READ,
                          &enum_key) != ERROR_SUCCESS) {
            continue;
        }
        driver[0] = '\0';
        if (RegQueryValueExA(enum_key, "Driver", 0, &type, (BYTE *)driver,
                             &driver_size) == ERROR_SUCCESS &&
            type == REG_SZ && lstrcmpiA(driver, wanted) == 0) {
            found = TRUE;
        }
        RegCloseKey(enum_key);
    }
    RegCloseKey(live_root);
    return found;
}

static BOOL v9x_install_query(HKEY key, const char *name, char *value,
                              DWORD capacity)
{
    DWORD type;
    DWORD size = capacity;

    value[0] = '\0';
    if (RegQueryValueExA(key, name, 0, &type, (BYTE *)value, &size) !=
            ERROR_SUCCESS || type != REG_SZ || size == 0ul ||
        value[0] == '\0') {
        value[0] = '\0';
        return FALSE;
    }
    value[capacity - 1ul] = '\0';
    return TRUE;
}

/*
 * The one Velocity9x display instance an update applies to: a class key
 * the INF marked with V9xFamily, and of several, the one a present device
 * uses. An install that predates the marker has none and is refused:
 * without the family the server cannot choose a package.
 */
BOOL v9x_install_find(struct v9x_install *install, char *why, DWORD why_size)
{
    char found[V9X_INSTANCES_MAX][16];
    char windows[MAX_PATH];
    unsigned int count = 0u;
    unsigned int live = 0u;
    unsigned int chosen = 0u;
    unsigned int index;
    DWORD enumerated = 0ul;
    HKEY display;
    HKEY key;
    char path[96];

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, V9X_DISPLAY_CLASS_KEY, 0, KEY_READ,
                      &display) != ERROR_SUCCESS) {
        v9x_install_copy(why, why_size, "The display class key is missing.");
        return FALSE;
    }
    for (;;) {
        char name[16];
        char family[32];
        DWORD size = sizeof(name);

        if (RegEnumKeyExA(display, enumerated++, name, &size, 0, 0, 0, 0) !=
            ERROR_SUCCESS) {
            break;
        }
        if (RegOpenKeyExA(display, name, 0, KEY_READ, &key) !=
            ERROR_SUCCESS) {
            continue;
        }
        if (v9x_install_query(key, "V9xFamily", family, sizeof(family)) &&
            count < V9X_INSTANCES_MAX) {
            lstrcpyA(found[count++], name);
        }
        RegCloseKey(key);
    }
    RegCloseKey(display);

    if (count == 0u) {
        v9x_install_copy(why, why_size,
            "No Velocity9x installation that records its card family was "
            "found. Installations from before the updater do not; install "
            "the new release by hand once, through Display Properties, and "
            "later updates can use this button.");
        return FALSE;
    }
    if (count > 1u) {
        for (index = 0u; index < count; ++index) {
            if (v9x_install_is_live(found[index])) {
                chosen = index;
                ++live;
            }
        }
        if (live != 1u) {
            v9x_install_copy(why, why_size,
                "More than one Velocity9x display entry is in the registry "
                "and the one in use could not be told apart. Install the "
                "new release by hand.");
            return FALSE;
        }
    }

    lstrcpyA(install->instance, found[chosen]);
    wsprintfA(path, V9X_DISPLAY_CLASS_KEY "\\%s", install->instance);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        v9x_install_copy(why, why_size, "The display driver key went away.");
        return FALSE;
    }
    (void)v9x_install_query(key, "V9xFamily", install->family,
                            sizeof(install->family));
    (void)v9x_install_query(key, "InfSection", install->inf_section,
                            sizeof(install->inf_section));
    (void)v9x_install_query(key, "InfPath", windows, sizeof(windows));
    RegCloseKey(key);
    if (install->inf_section[0] == '\0' || windows[0] == '\0') {
        v9x_install_copy(why, why_size,
            "The display driver key does not record the INF it was "
            "installed from. Install the new release by hand.");
        return FALSE;
    }

    /* InfPath is a bare name: SetupX keeps an OEM INF in INF\OTHER (the
     * ViRGE guest's was VELOCI~1.INF there, 2026-10-09), and a shipped one
     * in INF itself. Never guess from the newest file. */
    {
        char inf_name[64];
        UINT length;

        lstrcpynA(inf_name, windows, sizeof(inf_name));
        length = GetWindowsDirectoryA(windows, sizeof(windows));
        if (length == 0u || length + 80u > sizeof(windows)) {
            v9x_install_copy(why, why_size, "No Windows directory.");
            return FALSE;
        }
        wsprintfA(install->inf_path, "%s\\INF\\OTHER\\%s", windows, inf_name);
        if (GetFileAttributesA(install->inf_path) == 0xFFFFFFFFul) {
            wsprintfA(install->inf_path, "%s\\INF\\%s", windows, inf_name);
        }
        if (GetFileAttributesA(install->inf_path) == 0xFFFFFFFFul) {
            v9x_install_copy(why, why_size,
                "The INF this driver was installed from is missing. "
                "Install the new release by hand.");
            return FALSE;
        }
    }
    return TRUE;
}

/* Find an entry by name without case, with no 8.3 limit on the name -
 * VELOCITY9X.INF is fourteen characters - and extract it. */
static BOOL v9x_install_extract(struct v9x_update_job *job,
                                const struct v9x_zip *zip, const char *name,
                                BYTE *output, DWORD capacity, DWORD *length)
{
    struct v9x_zip_entry entry;
    v9x_u16 index;
    v9x_u16 match = 0xFFFFu;
    v9x_u32 extracted = 0ul;
    DWORD name_length = v9x_install_length(name);

    for (index = 0u; index < zip->entry_count; ++index) {
        v9x_u16 position;

        if (v9x_zip_entry_at(zip, index, &entry) != V9X_ZIP_OK) {
            v9x_install_fail(job, "The package's directory is damaged.", 0);
            return FALSE;
        }
        if (entry.name_length != name_length) {
            continue;
        }
        for (position = 0u; position < entry.name_length; ++position) {
            char left = (char)entry.name[position];
            char right = name[position];

            if (left >= 'a' && left <= 'z') {
                left = (char)(left - 'a' + 'A');
            }
            if (right >= 'a' && right <= 'z') {
                right = (char)(right - 'a' + 'A');
            }
            if (left != right) {
                break;
            }
        }
        if (position != entry.name_length) {
            continue;
        }
        if (match != 0xFFFFu) {
            v9x_install_fail(job, "The package holds two files named", name);
            return FALSE;
        }
        match = index;
    }
    if (match == 0xFFFFu) {
        v9x_install_fail(job, "The package has no", name);
        return FALSE;
    }
    if (v9x_zip_extract(zip, match, &v9x_install_inflate, output, capacity,
                        &extracted) != V9X_ZIP_OK) {
        v9x_install_fail(job, "Could not unpack", name);
        return FALSE;
    }
    *length = extracted;
    return TRUE;
}

/* The file version of an image, as major << 16 | minor in high and
 * build << 16 | private in low; FALSE without a version resource. */
static BOOL v9x_install_file_version(const char *path, DWORD *high,
                                     DWORD *low)
{
    static HMODULE version_dll;
    static v9x_version_size_fn size_fn;
    static v9x_version_info_fn info_fn;
    static v9x_version_query_fn query_fn;
    VS_FIXEDFILEINFO *fixed;
    UINT fixed_size = 0u;
    DWORD handle = 0ul;
    DWORD size;
    BYTE *buffer;
    BOOL ok = FALSE;

    if (version_dll == 0) {
        version_dll = LoadLibraryA("VERSION.DLL");
        if (version_dll == 0) {
            return FALSE;
        }
        size_fn = (v9x_version_size_fn)GetProcAddress(version_dll,
            "GetFileVersionInfoSizeA");
        info_fn = (v9x_version_info_fn)GetProcAddress(version_dll,
            "GetFileVersionInfoA");
        query_fn = (v9x_version_query_fn)GetProcAddress(version_dll,
            "VerQueryValueA");
    }
    if (size_fn == 0 || info_fn == 0 || query_fn == 0) {
        return FALSE;
    }
    size = size_fn((LPSTR)path, &handle);
    if (size == 0ul) {
        return FALSE;
    }
    buffer = (BYTE *)VirtualAlloc(0, size, MEM_COMMIT, PAGE_READWRITE);
    if (buffer == 0) {
        return FALSE;
    }
    if (info_fn((LPSTR)path, 0ul, size, buffer) &&
        query_fn(buffer, "\\", (LPVOID *)&fixed, &fixed_size) &&
        fixed_size >= sizeof(*fixed)) {
        *high = fixed->dwFileVersionMS;
        *low = fixed->dwFileVersionLS;
        ok = TRUE;
    }
    VirtualFree(buffer, 0ul, MEM_RELEASE);
    return ok;
}

/* COPYFLG_NO_VERSION_DIALOG: keep the installed file when it is newer. A
 * 3dfx card's own GLIDE2X.DLL is versioned 1.00 or 2.61, ours carries the
 * Velocity9x version (scripts\lib\inf.ps1). */
static BOOL v9x_install_keep_newer(const char *installed, const char *staged)
{
    DWORD installed_high;
    DWORD installed_low;
    DWORD staged_high;
    DWORD staged_low;

    if (GetFileAttributesA(installed) == 0xFFFFFFFFul) {
        return FALSE;
    }
    if (!v9x_install_file_version(installed, &installed_high,
                                  &installed_low) ||
        !v9x_install_file_version(staged, &staged_high, &staged_low)) {
        /* An installed file with no version is not ours: keep it, as
         * SetupX would rather than overwrite something it cannot rank. */
        return v9x_install_file_version(staged, &staged_high, &staged_low);
    }
    if (installed_high != staged_high) {
        return installed_high > staged_high;
    }
    return installed_low > staged_low;
}

/*
 * Unpack the verified package, plan the installed model's section of its
 * INF, and stage every file it copies under its own name in <drive>:\V9XNEW,
 * on the system directory's drive so WININIT can rename across directories.
 * Nothing installed is touched here.
 */
BOOL v9x_install_prepare(struct v9x_update_job *job)
{
    struct v9x_zip zip;
    char system_dir[MAX_PATH];
    char stage_dir[16];
    char windows[MAX_PATH];
    char path[MAX_PATH];
    BYTE *buffer;
    DWORD length;
    unsigned int index;
    BOOL ok = FALSE;

    job->file_count = 0u;
    if (v9x_zip_open(&zip, job->zip, job->zip_length) != V9X_ZIP_OK) {
        v9x_install_fail(job, "The package is not a readable zip file.", 0);
        return FALSE;
    }
    if (GetSystemDirectoryA(system_dir, sizeof(system_dir)) == 0u ||
        GetWindowsDirectoryA(windows, sizeof(windows)) == 0u ||
        v9x_install_length(system_dir) + 16ul > MAX_PATH) {
        v9x_install_fail(job, "No Windows system directory.", 0);
        return FALSE;
    }

    /* Someone else's pending WININIT work would run in the same restart,
     * and Windows deletes the file once it has run: never merge into it. */
    wsprintfA(path, "%s\\WININIT.INI", windows);
    if (GetFileAttributesA(path) != 0xFFFFFFFFul) {
        v9x_install_fail(job,
            "Another program has changes waiting for the next restart "
            "(WININIT.INI). Restart Windows, then check for updates again.",
            0);
        return FALSE;
    }

    job->inf = (char *)VirtualAlloc(0, V9X_INF_FILE_MAX, MEM_COMMIT,
                                    PAGE_READWRITE);
    buffer = (BYTE *)VirtualAlloc(0, V9X_PACKAGE_FILE_MAX, MEM_COMMIT,
                                  PAGE_READWRITE);
    if (job->inf == 0 || buffer == 0) {
        v9x_install_fail(job, "Not enough memory to unpack the update.", 0);
        goto done;
    }
    if (!v9x_install_extract(job, &zip, "VELOCITY9X.INF", (BYTE *)job->inf,
                             V9X_INF_FILE_MAX, &job->inf_length)) {
        goto done;
    }
    if (!v9x_inf_plan(job->inf, job->inf_length,
                      job->install.inf_section, job->plan)) {
        v9x_install_fail(job, "The new INF cannot be applied directly:",
                         job->plan->error);
        goto done;
    }
    if (job->plan->copies > V9X_STAGED_MAX) {
        v9x_install_fail(job, "The new INF copies too many files.", 0);
        goto done;
    }

    stage_dir[0] = system_dir[0];
    stage_dir[1] = '\0';
    v9x_install_append(stage_dir, sizeof(stage_dir), ":\\V9XNEW");
    CreateDirectoryA(stage_dir, 0);

    for (index = 0u; index < job->plan->count; ++index) {
        const struct v9x_inf_op *op = &job->plan->ops[index];
        struct v9x_staged_file *file;
        struct v9x_sha256 context;

        if (op->kind != V9X_INF_OP_COPY) {
            continue;
        }
        v9x_progress_set(op->name);
        if (!v9x_install_extract(job, &zip, op->name, buffer,
                                 V9X_PACKAGE_FILE_MAX, &length)) {
            goto done;
        }
        file = &job->files[job->file_count];
        v9x_install_copy(file->name, sizeof(file->name), op->name);
        CharUpperA(file->name);
        wsprintfA(file->staged, "%s\\%s", stage_dir, file->name);
        wsprintfA(file->target, "%s\\%s", system_dir, file->name);
        if (!v9x_install_write_file(file->staged, buffer, length)) {
            v9x_install_fail(job, "Could not write", file->staged);
            goto done;
        }
        v9x_sha256_init(&context);
        v9x_sha256_update(&context, buffer, length);
        v9x_sha256_final(&context, file->sha256);
        /* Read it back: the rename at restart moves what is on disk, not
         * what was in memory. */
        {
            BYTE check[32];
            unsigned int byte;

            if (!v9x_install_hash_file(file->staged, check)) {
                v9x_install_fail(job, "Could not read back", file->staged);
                goto done;
            }
            for (byte = 0u; byte < 32u; ++byte) {
                if (check[byte] != file->sha256[byte]) {
                    v9x_install_fail(job, "The staged copy differs:",
                                     file->staged);
                    goto done;
                }
            }
        }
        if (op->copy_flags == V9X_INF_COPY_IF_NEWER &&
            v9x_install_keep_newer(file->target, file->staged)) {
            /* Not staged: the installed file is newer (a 3dfx card's
             * GLIDE2X.DLL). The staged copy is left for the record. */
            continue;
        }
        ++job->file_count;
    }
    ok = TRUE;

done:
    if (buffer != 0) {
        VirtualFree(buffer, 0ul, MEM_RELEASE);
    }
    return ok;
}

/* REGEDIT4 text for one value, the same escaping regedit writes. */
static void v9x_install_reg_value(HANDLE file, const char *name, DWORD type,
                                  const BYTE *data, DWORD size)
{
    char line[1024];
    DWORD used;
    DWORD written;
    DWORD index;

    line[0] = '\0';
    if (name[0] == '\0') {
        v9x_install_copy(line, sizeof(line), "@=");
    } else {
        v9x_install_copy(line, sizeof(line), "\"");
        for (index = 0ul; name[index] != '\0'; ++index) {
            char one[3];

            one[0] = name[index];
            one[1] = '\0';
            if (name[index] == '\\' || name[index] == '"') {
                one[0] = '\\';
                one[1] = name[index];
                one[2] = '\0';
            }
            v9x_install_append(line, sizeof(line), one);
        }
        v9x_install_append(line, sizeof(line), "\"=");
    }
    if (type == REG_SZ) {
        v9x_install_append(line, sizeof(line), "\"");
        for (index = 0ul; index < size && data[index] != 0u; ++index) {
            char one[3];

            one[0] = (char)data[index];
            one[1] = '\0';
            if (data[index] == '\\' || data[index] == '"') {
                one[0] = '\\';
                one[1] = (char)data[index];
                one[2] = '\0';
            }
            v9x_install_append(line, sizeof(line), one);
        }
        v9x_install_append(line, sizeof(line), "\"");
    } else if (type == REG_DWORD && size == 4ul) {
        char text[20];

        wsprintfA(text, "dword:%08lx", *(const DWORD *)data);
        v9x_install_append(line, sizeof(line), text);
    } else {
        v9x_install_append(line, sizeof(line), "hex:");
        for (index = 0ul; index < size; ++index) {
            char text[4];

            wsprintfA(text, index == 0ul ? "%02x" : ",%02x",
                      (unsigned int)data[index]);
            v9x_install_append(line, sizeof(line), text);
        }
    }
    v9x_install_append(line, sizeof(line), "\r\n");
    used = v9x_install_length(line);
    (void)WriteFile(file, line, used, &written, 0);
}

/* Every value and subkey under key, as REGEDIT4 sections, for a backup a
 * user can merge back by double-clicking. */
static void v9x_install_reg_export(HANDLE file, HKEY key, const char *path,
                                   unsigned int depth)
{
    char line[512];
    DWORD written;
    DWORD index;
    char name[256];
    BYTE data[512];

    wsprintfA(line, "\r\n[%s]\r\n", path);
    (void)WriteFile(file, line, v9x_install_length(line), &written, 0);
    for (index = 0ul;; ++index) {
        DWORD name_size = sizeof(name);
        DWORD data_size = sizeof(data);
        DWORD type;

        if (RegEnumValueA(key, index, name, &name_size, 0, &type, data,
                          &data_size) != ERROR_SUCCESS) {
            break;
        }
        v9x_install_reg_value(file, name, type, data, data_size);
    }
    if (depth >= V9X_REG_DEPTH_MAX) {
        return;
    }
    for (index = 0ul;; ++index) {
        DWORD name_size = sizeof(name);
        char child_path[512];
        HKEY child;

        if (RegEnumKeyExA(key, index, name, &name_size, 0, 0, 0, 0) !=
            ERROR_SUCCESS) {
            break;
        }
        if (v9x_install_length(path) + name_size + 2ul > sizeof(child_path) ||
            RegOpenKeyExA(key, name, 0, KEY_READ, &child) != ERROR_SUCCESS) {
            continue;
        }
        wsprintfA(child_path, "%s\\%s", path, name);
        v9x_install_reg_export(file, child, child_path, depth + 1u);
        RegCloseKey(child);
    }
}

static HKEY v9x_install_root(v9x_u16 root, HKEY driver_key)
{
    if (root == V9X_INF_ROOT_HKR) {
        return driver_key;
    }
    if (root == V9X_INF_ROOT_HKLM) {
        return HKEY_LOCAL_MACHINE;
    }
    return HKEY_CLASSES_ROOT;
}

static const char *v9x_install_root_name(v9x_u16 root)
{
    return root == V9X_INF_ROOT_HKCR ? "HKEY_CLASSES_ROOT"
                                     : "HKEY_LOCAL_MACHINE";
}

/*
 * Back up what the update replaces: the installed files, the whole driver
 * key, the HKLM and HKCR values the INF writes or deletes, and the live
 * OEM INF. RECOVER.TXT in the package says how to put them back.
 */
static BOOL v9x_install_backup(struct v9x_update_job *job, HKEY driver_key)
{
    char path[MAX_PATH];
    char version[16];
    HANDLE file;
    DWORD written;
    unsigned int index;

    /* V0_15_0: the version without dots, so the folder name is 8.3. */
    v9x_install_copy(version, sizeof(version), "V" V9X_VERSION_STRING);
    for (index = 0u; version[index] != '\0'; ++index) {
        if (version[index] == '.') {
            version[index] = '_';
        }
    }
    CreateDirectoryA(V9X_DIAG_DIR, 0);
    CreateDirectoryA(V9X_BACKUP_ROOT, 0);
    v9x_install_copy(job->backup_dir, sizeof(job->backup_dir),
                     V9X_BACKUP_ROOT "\\BACKUP");
    CreateDirectoryA(job->backup_dir, 0);
    v9x_install_append(job->backup_dir, sizeof(job->backup_dir), "\\");
    v9x_install_append(job->backup_dir, sizeof(job->backup_dir), version);
    CreateDirectoryA(job->backup_dir, 0);

    for (index = 0u; index < job->file_count; ++index) {
        const struct v9x_staged_file *staged = &job->files[index];

        if (GetFileAttributesA(staged->target) == 0xFFFFFFFFul) {
            continue;
        }
        wsprintfA(path, "%s\\%s", job->backup_dir, staged->name);
        if (!CopyFileA(staged->target, path, FALSE)) {
            v9x_install_fail(job, "Could not back up", staged->target);
            return FALSE;
        }
    }
    wsprintfA(path, "%s\\OEM.INF", job->backup_dir);
    if (!CopyFileA(job->install.inf_path, path, FALSE)) {
        v9x_install_fail(job, "Could not back up", job->install.inf_path);
        return FALSE;
    }

    wsprintfA(path, "%s\\REGISTRY.REG", job->backup_dir);
    file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        v9x_install_fail(job, "Could not write", path);
        return FALSE;
    }
    (void)WriteFile(file, "REGEDIT4\r\n", 10ul, &written, 0);
    {
        char key_path[128];

        wsprintfA(key_path, "HKEY_LOCAL_MACHINE\\" V9X_DISPLAY_CLASS_KEY
                  "\\%s", job->install.instance);
        v9x_install_reg_export(file, driver_key, key_path, 0u);
    }
    for (index = 0u; index < job->plan->count; ++index) {
        const struct v9x_inf_op *op = &job->plan->ops[index];
        char line[V9X_INF_KEY_MAX + 32];
        BYTE data[512];
        DWORD size = sizeof(data);
        DWORD type;
        HKEY key;

        if ((op->kind != V9X_INF_OP_SET_STRING &&
             op->kind != V9X_INF_OP_DEL_VALUE) ||
            op->root == V9X_INF_ROOT_HKR) {
            continue;
        }
        if (RegOpenKeyExA(v9x_install_root(op->root, driver_key), op->key,
                          0, KEY_READ, &key) != ERROR_SUCCESS) {
            continue;
        }
        if (RegQueryValueExA(key, op->name, 0, &type, data, &size) ==
            ERROR_SUCCESS) {
            wsprintfA(line, "\r\n[%s\\%s]\r\n",
                      v9x_install_root_name(op->root), op->key);
            (void)WriteFile(file, line, v9x_install_length(line), &written,
                            0);
            v9x_install_reg_value(file, op->name, type, data, size);
        }
        RegCloseKey(key);
    }
    CloseHandle(file);
    return TRUE;
}

/* The registry half of the plan: DelReg, then AddReg, in INF order. */
static BOOL v9x_install_registry(struct v9x_update_job *job, HKEY driver_key)
{
    unsigned int index;

    for (index = 0u; index < job->plan->count; ++index) {
        const struct v9x_inf_op *op = &job->plan->ops[index];
        HKEY root = v9x_install_root(op->root, driver_key);
        HKEY key;
        DWORD disposition;
        LONG result;

        if (op->kind == V9X_INF_OP_DEL_KEY) {
            /* Win9x RegDeleteKey removes the subkeys too, as SetupX's
             * DelReg of DEFAULT and MODES expects. Absent is fine. */
            result = RegDeleteKeyA(root, op->key);
            if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND &&
                result != ERROR_PATH_NOT_FOUND) {
                v9x_install_fail(job, "Could not delete registry key",
                                 op->key);
                return FALSE;
            }
            continue;
        }
        if (op->kind == V9X_INF_OP_DEL_VALUE) {
            if (op->key[0] == '\0') {
                (void)RegDeleteValueA(root, op->name);
                continue;
            }
            if (RegOpenKeyExA(root, op->key, 0, KEY_WRITE, &key) ==
                ERROR_SUCCESS) {
                (void)RegDeleteValueA(key, op->name);
                RegCloseKey(key);
            }
            continue;
        }
        if (op->kind != V9X_INF_OP_SET_STRING) {
            continue;
        }
        if (op->key[0] == '\0') {
            key = root;
        } else if (RegCreateKeyExA(root, op->key, 0, 0, 0, KEY_WRITE, 0,
                                   &key, &disposition) != ERROR_SUCCESS) {
            v9x_install_fail(job, "Could not create registry key", op->key);
            return FALSE;
        }
        result = RegSetValueExA(key, op->name, 0, REG_SZ,
                                (const BYTE *)op->data,
                                v9x_install_length(op->data) + 1ul);
        if (key != root) {
            RegCloseKey(key);
        }
        if (result != ERROR_SUCCESS) {
            v9x_install_fail(job, "Could not set registry value", op->name);
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * WININIT.INI: one dest=src rename per staged file, both 8.3 short paths,
 * which real-mode WININIT needs (a nine-character directory stopped it on
 * 2026-10-09). Written to WININIT.TMP and moved into place, so the file
 * never exists half-written; the move fails if another WININIT.INI
 * appeared meanwhile.
 */
static BOOL v9x_install_wininit(struct v9x_update_job *job, char *wininit)
{
    char windows[MAX_PATH];
    char temporary[MAX_PATH];
    char text[V9X_STAGED_MAX * 2u * 80u + 16u];
    unsigned int index;

    if (GetWindowsDirectoryA(windows, sizeof(windows)) == 0u) {
        v9x_install_fail(job, "No Windows directory.", 0);
        return FALSE;
    }
    wsprintfA(wininit, "%s\\WININIT.INI", windows);
    wsprintfA(temporary, "%s\\WININIT.TMP", windows);
    v9x_install_copy(text, sizeof(text), "[rename]\r\n");
    for (index = 0u; index < job->file_count; ++index) {
        const struct v9x_staged_file *file = &job->files[index];
        char target[MAX_PATH];
        char staged[MAX_PATH];
        char system_short[MAX_PATH];
        char system_dir[MAX_PATH];

        /* The target may not exist yet (a file new in this release), so
         * shorten its directory and append the 8.3 name. */
        lstrcpynA(system_dir, file->target, sizeof(system_dir));
        system_dir[v9x_install_length(system_dir) -
                   v9x_install_length(file->name) - 1ul] = '\0';
        if (GetShortPathNameA(system_dir, system_short,
                              sizeof(system_short)) == 0ul ||
            GetShortPathNameA(file->staged, staged, sizeof(staged)) == 0ul) {
            v9x_install_fail(job, "No short name for", file->staged);
            return FALSE;
        }
        wsprintfA(target, "%s\\%s", system_short, file->name);
        if (!v9x_install_fits(target, 80ul) ||
            !v9x_install_fits(staged, 80ul)) {
            v9x_install_fail(job, "Path too long for WININIT:", target);
            return FALSE;
        }
        v9x_install_append(text, sizeof(text), target);
        v9x_install_append(text, sizeof(text), "=");
        v9x_install_append(text, sizeof(text), staged);
        v9x_install_append(text, sizeof(text), "\r\n");
    }
    if (!v9x_install_write_file(temporary, (const BYTE *)text,
                                v9x_install_length(text))) {
        v9x_install_fail(job, "Could not write", temporary);
        return FALSE;
    }
    if (!MoveFileA(temporary, wininit)) {
        DeleteFileA(temporary);
        v9x_install_fail(job, "Could not create", wininit);
        return FALSE;
    }
    return TRUE;
}

static void v9x_install_record(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_UPDATE_SECTION, key, value,
                               V9X_DIAG_UPDATE_INI);
}

/*
 * Make the update happen at the next restart: back up, write WININIT.INI,
 * apply the registry, replace the live OEM INF, and leave /FINISH in
 * RunOnce. A failure after WININIT.INI exists removes it again - it is this
 * program's own file - so nothing half-applied waits for the restart.
 */
BOOL v9x_install_commit(struct v9x_update_job *job)
{
    char key_path[96];
    char wininit[MAX_PATH];
    char text[96];
    char system_dir[MAX_PATH];
    HKEY driver_key;
    HKEY run_once;
    unsigned int index;

    wsprintfA(key_path, V9X_DISPLAY_CLASS_KEY "\\%s", job->install.instance);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key_path, 0,
                      KEY_READ | KEY_WRITE, &driver_key) != ERROR_SUCCESS) {
        v9x_install_fail(job, "Could not open the display driver key.", 0);
        return FALSE;
    }
    if (!v9x_install_backup(job, driver_key)) {
        RegCloseKey(driver_key);
        return FALSE;
    }
    if (!v9x_install_wininit(job, wininit)) {
        RegCloseKey(driver_key);
        return FALSE;
    }
    if (!v9x_install_registry(job, driver_key)) {
        RegCloseKey(driver_key);
        DeleteFileA(wininit);
        v9x_install_append(job->error, sizeof(job->error),
            ". The registry may be part-way: merge REGISTRY.REG from ");
        v9x_install_append(job->error, sizeof(job->error), job->backup_dir);
        return FALSE;
    }
    RegCloseKey(driver_key);

    /* The live OEM INF, so a later reinstall from Device Manager installs
     * this version rather than the old one. Not fatal: the driver itself is
     * staged. */
    (void)v9x_install_write_file(job->install.inf_path, (const BYTE *)job->inf,
                                 job->inf_length);

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA(V9X_UPDATE_SECTION, 0, 0, V9X_DIAG_UPDATE_INI);
    v9x_install_record("State", "staged");
    v9x_install_record("From", V9X_VERSION_STRING);
    v9x_install_record("To", job->release.version);
    v9x_install_record("ToBuild", job->release.build);
    v9x_install_record("Backup", job->backup_dir);
    wsprintfA(text, "%u", job->file_count);
    v9x_install_record("Files", text);
    for (index = 0u; index < job->file_count; ++index) {
        char key[16];
        char hex[65];

        wsprintfA(key, "File%u", index);
        v9x_install_record(key, job->files[index].target);
        v9x_install_hex(job->files[index].sha256, hex);
        wsprintfA(key, "Sha%u", index);
        v9x_install_record(key, hex);
    }
    WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_UPDATE_INI);

    if (GetSystemDirectoryA(system_dir, sizeof(system_dir)) != 0u &&
        RegCreateKeyExA(HKEY_LOCAL_MACHINE, V9X_RUNONCE_KEY, 0, 0, 0,
                        KEY_WRITE, 0, &run_once, 0) == ERROR_SUCCESS) {
        char command[MAX_PATH + 32];

        wsprintfA(command, "%s\\V9XUPD.EXE /FINISH", system_dir);
        (void)RegSetValueExA(run_once, V9X_RUNONCE_VALUE, 0, REG_SZ,
                             (const BYTE *)command,
                             v9x_install_length(command) + 1ul);
        RegCloseKey(run_once);
    }
    return TRUE;
}

/*
 * /FINISH, from RunOnce after the restart: did every rename happen? Each
 * installed file must hash to what was staged. It shows nothing: Windows 98
 * runs RunOnce entries before the desktop and waits for each to exit, so a
 * message box here held the machine at "Windows 98 Setup" until somebody
 * clicked OK (the Win86SE guest, 2026-10-09). It records the outcome and
 * starts /RESULT, which waits for the desktop before saying anything.
 */
void v9x_install_finish(void)
{
    char state[16];
    char value[MAX_PATH];
    char wrong_files[512];
    char running[V9X_RELEASE_BUILD_MAX];
    unsigned int count;
    unsigned int index;
    unsigned int wrong = 0u;
    char system_dir[MAX_PATH];
    char stage_dir[16];

    GetPrivateProfileStringA(V9X_UPDATE_SECTION, "State", "", state,
                             sizeof(state), V9X_DIAG_UPDATE_INI);
    if (lstrcmpiA(state, "staged") != 0) {
        return;
    }
    count = GetPrivateProfileIntA(V9X_UPDATE_SECTION, "Files", 0,
                                  V9X_DIAG_UPDATE_INI);
    wrong_files[0] = '\0';
    for (index = 0u; index < count && index < V9X_STAGED_MAX; ++index) {
        char key[16];
        char expected[72];
        char actual[65];
        BYTE digest[32];

        wsprintfA(key, "File%u", index);
        GetPrivateProfileStringA(V9X_UPDATE_SECTION, key, "", value,
                                 sizeof(value), V9X_DIAG_UPDATE_INI);
        wsprintfA(key, "Sha%u", index);
        GetPrivateProfileStringA(V9X_UPDATE_SECTION, key, "", expected,
                                 sizeof(expected), V9X_DIAG_UPDATE_INI);
        if (!v9x_install_hash_file(value, digest)) {
            actual[0] = '\0';
        } else {
            v9x_install_hex(digest, actual);
        }
        if (lstrcmpiA(actual, expected) != 0) {
            ++wrong;
            if (wrong_files[0] != '\0') {
                v9x_install_append(wrong_files, sizeof(wrong_files), " ");
            }
            v9x_install_append(wrong_files, sizeof(wrong_files), value);
        }
    }

    running[0] = '\0';
    GetPrivateProfileStringA("Velocity9xModes", "Build", "", running,
                             sizeof(running), V9X_DIAG_MODES_INI);
    v9x_install_record("RunningBuild", running);
    if (wrong == 0u && count != 0u) {
        v9x_install_record("State", "committed");
        /* The staged copies were moved, not copied: the folder is empty
         * unless GLIDE2X.DLL was kept, and then only that remains. */
        if (GetSystemDirectoryA(system_dir, sizeof(system_dir)) != 0u) {
            stage_dir[0] = system_dir[0];
            stage_dir[1] = '\0';
            v9x_install_append(stage_dir, sizeof(stage_dir), ":\\V9XNEW");
            (void)RemoveDirectoryA(stage_dir);
        }
    } else {
        v9x_install_record("State", "failed");
        v9x_install_record("NotUpdated", wrong_files);
    }
    v9x_install_record("Shown", "0");
    WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_UPDATE_INI);

    /* Hand the message to a process RunOnce does not wait for. */
    {
        char path[MAX_PATH];
        char command[MAX_PATH + 16];
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        BYTE *raw = (BYTE *)&startup;
        DWORD length = GetModuleFileNameA(0, path, sizeof(path));

        if (length == 0ul || length >= sizeof(path)) {
            return;
        }
        for (index = 0u; index < sizeof(startup); ++index) {
            raw[index] = 0u;
        }
        startup.cb = sizeof(startup);
        wsprintfA(command, "\"%s\" /RESULT", path);
        if (CreateProcessA(path, command, 0, 0, FALSE, 0ul, 0, 0, &startup,
                           &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    }
}

/* How long /RESULT waits for the taskbar before giving up quietly; the
 * outcome stays in V9XUPD.INI either way. */
#define V9X_RESULT_WAIT_SECONDS 300u

/*
 * /RESULT: once Explorer's taskbar exists - the desktop is up and nothing
 * waits on this process - say how the update went, once.
 */
void v9x_install_result(void)
{
    char state[16];
    char to[V9X_RELEASE_VERSION_MAX];
    char build[V9X_RELEASE_BUILD_MAX];
    char running[V9X_RELEASE_BUILD_MAX];
    char backup[MAX_PATH];
    char wrong_files[512];
    char message[1024];
    unsigned int waited;

    GetPrivateProfileStringA(V9X_UPDATE_SECTION, "State", "", state,
                             sizeof(state), V9X_DIAG_UPDATE_INI);
    if (GetPrivateProfileIntA(V9X_UPDATE_SECTION, "Shown", 1,
                              V9X_DIAG_UPDATE_INI) != 0) {
        return;
    }
    for (waited = 0u; FindWindowA("Shell_TrayWnd", 0) == 0; ++waited) {
        if (waited >= V9X_RESULT_WAIT_SECONDS) {
            return;
        }
        Sleep(1000ul);
    }
    /* The taskbar appears before the rest of startup settles. */
    Sleep(5000ul);
    v9x_install_record("Shown", "1");
    WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_UPDATE_INI);

    GetPrivateProfileStringA(V9X_UPDATE_SECTION, "To", "", to, sizeof(to),
                             V9X_DIAG_UPDATE_INI);
    GetPrivateProfileStringA(V9X_UPDATE_SECTION, "ToBuild", "", build,
                             sizeof(build), V9X_DIAG_UPDATE_INI);
    GetPrivateProfileStringA(V9X_UPDATE_SECTION, "RunningBuild", "", running,
                             sizeof(running), V9X_DIAG_UPDATE_INI);
    if (lstrcmpiA(state, "committed") == 0) {
        wsprintfA(message, "Velocity9x was updated to %s (build %s).%s%s",
                  to, build, running[0] != '\0' ? "\r\n\r\nThe driver now "
                  "running reports build " : "", running);
        MessageBoxA(0, message, "Velocity9x", MB_OK | MB_ICONINFORMATION);
        return;
    }
    GetPrivateProfileStringA(V9X_UPDATE_SECTION, "Backup", "", backup,
                             sizeof(backup), V9X_DIAG_UPDATE_INI);
    GetPrivateProfileStringA(V9X_UPDATE_SECTION, "NotUpdated", "",
                             wrong_files, sizeof(wrong_files),
                             V9X_DIAG_UPDATE_INI);
    wsprintfA(message,
              "The Velocity9x update to %s did not complete. These files "
              "are not the new ones:\r\n\r\n%s\r\n\r\nThe previous files and "
              "a REGISTRY.REG of the old settings are in %s. RECOVER.TXT in "
              "the Velocity9x package describes restoring them. Send a "
              "report from the Velocity9x Advanced tab so this can be "
              "looked at.", to, wrong_files, backup);
    MessageBoxA(0, message, "Velocity9x", MB_OK | MB_ICONEXCLAMATION);
}
