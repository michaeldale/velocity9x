/*
 * Tests for the trace snapshot's identity rules: the text copies the shared
 * block holds and the process table (include\velocity9x\diag_identity.h).
 *
 * The table is what attributes a snapshot's counters to a program, so the
 * properties that matter are that one program is one row however its path
 * is spelled, and that a full table loses rows visibly - counted - rather
 * than overwriting the first program it saw.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/diag_identity.h"

static unsigned int identity_failures = 0u;

#define IDCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++identity_failures; \
    } \
} while (0)

/* A short text is copied and the rest of the array is zeroed. */
static void test_copy_zeroes_tail(void)
{
    char out[8];
    unsigned int index;

    memset(out, 'x', sizeof(out));
    v9x_diag_copy_text(out, "abc", sizeof(out));
    IDCHECK(strcmp(out, "abc") == 0);
    for (index = 3u; index < sizeof(out); ++index) {
        IDCHECK(out[index] == '\0');
    }
}

/* A long text is cut to fit and still terminated. */
static void test_copy_truncates(void)
{
    char out[4];

    v9x_diag_copy_text(out, "abcdef", sizeof(out));
    IDCHECK(strcmp(out, "abc") == 0);
}

/* A null source leaves an empty, zeroed field. */
static void test_copy_null(void)
{
    char out[4];

    memset(out, 'x', sizeof(out));
    v9x_diag_copy_text(out, 0, sizeof(out));
    IDCHECK(out[0] == '\0' && out[3] == '\0');
}

/* Every separator Windows 98 hands back ends the directory part. */
static void test_base_name(void)
{
    char out[V9X_DIAG_PROCESS_NAME_BYTES];

    v9x_diag_base_name(out, "C:\\QUAKE2\\QUAKE2.EXE", sizeof(out));
    IDCHECK(strcmp(out, "QUAKE2.EXE") == 0);
    v9x_diag_base_name(out, "C:/Games/hl.exe", sizeof(out));
    IDCHECK(strcmp(out, "hl.exe") == 0);
    v9x_diag_base_name(out, "A:SETUP.EXE", sizeof(out));
    IDCHECK(strcmp(out, "SETUP.EXE") == 0);
    v9x_diag_base_name(out, "PLAIN.EXE", sizeof(out));
    IDCHECK(strcmp(out, "PLAIN.EXE") == 0);
    v9x_diag_base_name(out, "C:\\DIR\\", sizeof(out));
    IDCHECK(out[0] == '\0');
    v9x_diag_base_name(out, "C:\\X\\AVERYLONGPROGRAMNAME.EXE", sizeof(out));
    IDCHECK(strcmp(out, "AVERYLONGPROGRA") == 0);
}

/* One program is one row, counted by kind, whatever case its name is in. */
static void test_one_row_per_program(void)
{
    struct v9x_diag_identity identity;

    memset(&identity, 0, sizeof(identity));
    v9x_diag_note_process(&identity, "QUAKE2.EXE", V9X_DIAG_PROCESS_GL, 500ul);
    v9x_diag_note_process(&identity, "quake2.exe", V9X_DIAG_PROCESS_GL, 900ul);
    v9x_diag_note_process(&identity, "Quake2.Exe", V9X_DIAG_PROCESS_D3D, 950ul);
    IDCHECK(identity.process_count == 1ul);
    IDCHECK(strcmp(identity.processes[0].name, "QUAKE2.EXE") == 0);
    IDCHECK(identity.processes[0].gl_describes == 2ul);
    IDCHECK(identity.processes[0].d3d_contexts == 1ul);
    /* The first sighting's time, not the latest. */
    IDCHECK(identity.processes[0].first_uptime_ms == 500ul);
    IDCHECK(identity.process_unrecorded == 0ul);
}

/* Programs take rows in the order they were first seen. */
static void test_rows_in_order(void)
{
    struct v9x_diag_identity identity;

    memset(&identity, 0, sizeof(identity));
    v9x_diag_note_process(&identity, "A.EXE", V9X_DIAG_PROCESS_D3D, 1ul);
    v9x_diag_note_process(&identity, "B.EXE", V9X_DIAG_PROCESS_GL, 2ul);
    v9x_diag_note_process(&identity, "A.EXE", V9X_DIAG_PROCESS_D3D, 3ul);
    IDCHECK(identity.process_count == 2ul);
    IDCHECK(strcmp(identity.processes[0].name, "A.EXE") == 0);
    IDCHECK(strcmp(identity.processes[1].name, "B.EXE") == 0);
    IDCHECK(identity.processes[0].d3d_contexts == 2ul);
    IDCHECK(identity.processes[1].gl_describes == 1ul);
    IDCHECK(identity.processes[1].d3d_contexts == 0ul);
}

/* A full table counts new programs instead of overwriting old rows, and a
 * program it already holds is still counted in its row. */
static void test_full_table(void)
{
    struct v9x_diag_identity identity;
    char name[8];
    unsigned int index;

    memset(&identity, 0, sizeof(identity));
    for (index = 0u; index < V9X_DIAG_PROCESS_SLOTS; ++index) {
        name[0] = (char)('A' + index);
        strcpy(name + 1, ".EXE");
        v9x_diag_note_process(&identity, name, V9X_DIAG_PROCESS_D3D, 1ul);
    }
    IDCHECK(identity.process_count == (v9x_u32)V9X_DIAG_PROCESS_SLOTS);
    v9x_diag_note_process(&identity, "LATE.EXE", V9X_DIAG_PROCESS_D3D, 2ul);
    v9x_diag_note_process(&identity, "LATE.EXE", V9X_DIAG_PROCESS_GL, 3ul);
    IDCHECK(identity.process_count == (v9x_u32)V9X_DIAG_PROCESS_SLOTS);
    IDCHECK(identity.process_unrecorded == 2ul);
    IDCHECK(strcmp(identity.processes[0].name, "A.EXE") == 0);
    v9x_diag_note_process(&identity, "a.exe", V9X_DIAG_PROCESS_D3D, 4ul);
    IDCHECK(identity.processes[0].d3d_contexts == 2ul);
    IDCHECK(identity.process_unrecorded == 2ul);
}

/* An empty name, or no table, records nothing; an unknown kind is not
 * counted as either. */
static void test_rejects(void)
{
    struct v9x_diag_identity identity;

    memset(&identity, 0, sizeof(identity));
    v9x_diag_note_process(&identity, "", V9X_DIAG_PROCESS_D3D, 1ul);
    v9x_diag_note_process(&identity, 0, V9X_DIAG_PROCESS_D3D, 1ul);
    v9x_diag_note_process(0, "A.EXE", V9X_DIAG_PROCESS_D3D, 1ul);
    IDCHECK(identity.process_count == 0ul);
    IDCHECK(identity.process_unrecorded == 0ul);
    v9x_diag_note_process(&identity, "A.EXE", 7ul, 1ul);
    IDCHECK(identity.process_count == 1ul);
    IDCHECK(identity.processes[0].d3d_contexts == 0ul);
    IDCHECK(identity.processes[0].gl_describes == 0ul);
}

/* A stored name is cut to the field and still matches its own long form. */
static void test_long_name_matches_itself(void)
{
    struct v9x_diag_identity identity;

    memset(&identity, 0, sizeof(identity));
    v9x_diag_note_process(&identity, "AVERYLONGPROGRAMNAME.EXE",
                          V9X_DIAG_PROCESS_GL, 1ul);
    v9x_diag_note_process(&identity, "AVERYLONGPROGRAMNAME.EXE",
                          V9X_DIAG_PROCESS_GL, 2ul);
    IDCHECK(identity.process_count == 1ul);
    IDCHECK(identity.processes[0].gl_describes == 2ul);
    IDCHECK(identity.processes[0].name[V9X_DIAG_PROCESS_NAME_BYTES - 1u] ==
            '\0');
}

unsigned int v9x_run_diag_identity_tests(void)
{
    test_copy_zeroes_tail();
    test_copy_truncates();
    test_copy_null();
    test_base_name();
    test_one_row_per_program();
    test_rows_in_order();
    test_full_table();
    test_rejects();
    test_long_name_matches_itself();
    return identity_failures;
}
