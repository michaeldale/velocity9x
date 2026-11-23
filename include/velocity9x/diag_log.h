/*
 * When an appended diagnostic log is rolled over: the decision half.
 *
 * Six logs the driver writes by default are appended line by line, opened
 * and closed for each line so a process that dies leaves what it wrote:
 *
 *   V9XGL.LOG    the OpenGL ICD          src\opengl\gl_icd.c
 *   V9XGLIDE.LOG GLIDE2X.DLL             src\glide\glide_core.c
 *   V9XGLD3.LOG  GLIDE3X.DLL             src\glide\glide_core.c
 *   V9XCIF.LOG   ATI3DCIF.DLL            src\cif\cif_dll.c
 *   V9XD7.LOG    the HAL's DDI 7 notes   src\display32\d3d\d3d_core.c
 *   V9XPS.LOG    the HAL's DDI 8 shaders src\display32\d3d\d3d_core.c
 *
 * None was bounded across processes or boots. V9XGLD3.LOG reached 64 MB on
 * one owner's machine (docs\decisions\2026-11-21-demo6030-glide3.md), and
 * the 1.0 plan's B12 asks for every default log to be bounded. Every other
 * file the driver writes is a keyed INI, or is rewritten whole each time
 * (CREATE_ALWAYS), or is a fixed set of names (V9XGRAB0-9.BMP), so it does
 * not grow; the audit is in include\velocity9x\diagpaths.h.
 *
 * The rule: before a line is appended, if the file already holds something
 * and the line would take it past V9X_DIAG_LOG_LIMIT, the file is renamed to
 * the same name with the extension OLD (V9XGL.LOG to V9XGL.OLD, replacing
 * any earlier one) and the line starts a new file. So each log keeps between
 * one and two limits of its most recent lines, and the line that crossed the
 * limit is never split. A report (V9XUPD.EXE) sends the last 512 KiB of a
 * file, so the limit is set well above that.
 *
 * This header and src\common\diag_log.c hold the rule and nothing else; each
 * writer applies it with its own Win32 calls, host-tested in
 * tests\host\test_diag_log.c.
 */
#ifndef VELOCITY9X_DIAG_LOG_H
#define VELOCITY9X_DIAG_LOG_H

#include "velocity9x/types.h"

#define V9X_DIAG_LOG_LIMIT (2ul * 1024ul * 1024ul)

/* The extension a rolled-over log is given. */
#define V9X_DIAG_LOG_OLD_EXTENSION "OLD"

/* Whether a file of size bytes is rolled over before a line of adding bytes
 * is appended to it, against limit. An empty file never is. */
int v9x_diag_log_must_roll(v9x_u32 size, v9x_u32 adding, v9x_u32 limit);

/*
 * The rolled-over name of path, written to out (capacity bytes, the
 * terminator included): the extension after the last '.' of the file name
 * replaced by V9X_DIAG_LOG_OLD_EXTENSION. Returns 0, leaving out empty when
 * capacity allows, if the name has no extension, already has that one, or
 * the result does not fit.
 */
int v9x_diag_log_old_path(const char *path, char *out, v9x_u32 capacity);

#endif
